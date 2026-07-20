#include "core/FrameMailbox.h"

#include <algorithm>

namespace mistercast {

void FrameMailbox::publish(
    std::span<const std::uint8_t> frame,
    std::uint16_t width,
    std::uint16_t height,
    ModeEpoch modeEpoch,
    Clock::time_point capturedAt)
{
    if (frame.empty() || frame.size() > latest_.size()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        std::copy(frame.begin(), frame.end(), latest_.begin());
        metadata_ = FrameMetadata{
            frame.size(), width, height, modeEpoch, capturedAt, Clock::now()};
        ++sequence_;
    }
    
    changed_.notify_all();
}

void FrameMailbox::invalidate()
{
    {
        std::lock_guard lock(mutex_);
        metadata_ = {};
        ++sequence_;
    }

    changed_.notify_all();
}

FrameReadResult FrameMailbox::waitForLatest(
    std::array<std::uint8_t, kMaximumFrameBytes>& destination,
    FrameCursor& cursor,
    FrameMetadata& metadata,
    const FrameSpec& required,
    std::chrono::milliseconds timeout,
    std::stop_token stopToken)
{
    std::unique_lock lock(mutex_);
    
    const bool ready = changed_.wait_for(lock, stopToken, timeout, [&] {
        return sequence_ != cursor.sequence;
    });

    if (!ready || sequence_ == 0) {
        return FrameReadResult::NoChange;
    }

    return copyLatestIfCompatible(destination, cursor, metadata, required);
}

FrameReadResult FrameMailbox::tryReadLatest(
    std::array<std::uint8_t, kMaximumFrameBytes>& destination,
    FrameCursor& cursor,
    FrameMetadata& metadata,
    const FrameSpec& required)
{
    std::lock_guard lock(mutex_);
    
    if (sequence_ == 0 || sequence_ == cursor.sequence) {
        return FrameReadResult::NoChange;
    }

    return copyLatestIfCompatible(destination, cursor, metadata, required);
}

FrameReadResult FrameMailbox::copyLatestIfCompatible(
    std::array<std::uint8_t, kMaximumFrameBytes>& destination,
    FrameCursor& cursor,
    FrameMetadata& metadata,
    const FrameSpec& required)
{
    cursor.sequence = sequence_;
    
    if (metadata_.bytes != required.bytes || metadata_.width != required.width ||
        metadata_.height != required.height || metadata_.modeEpoch != required.modeEpoch) {
        return FrameReadResult::Incompatible;
    }

    std::copy_n(latest_.begin(), metadata_.bytes, destination.begin());

    metadata = metadata_;
    
    return FrameReadResult::Copied;
}

} // namespace mistercast
