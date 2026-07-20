#pragma once

#include "core/Modeline.h"
#include "core/StreamTypes.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>

namespace mistercast {

struct FrameMetadata {
    std::size_t bytes{};
    std::uint16_t width{};
    std::uint16_t height{};
    ModeEpoch modeEpoch{};
    std::chrono::steady_clock::time_point capturedAt{};
    std::chrono::steady_clock::time_point publishedAt{};
};

enum class FrameReadResult {
    NoChange,
    Incompatible,
    Copied,
};

class FrameMailbox {
public:
    using Clock = std::chrono::steady_clock;

    void publish(
        std::span<const std::uint8_t> frame,
        std::uint16_t width,
        std::uint16_t height,
        ModeEpoch modeEpoch,
        Clock::time_point capturedAt = Clock::now());
    void invalidate();

    FrameReadResult waitForLatest(
        std::array<std::uint8_t, kMaximumFrameBytes>& destination,
        FrameCursor& cursor,
        FrameMetadata& metadata,
        const FrameSpec& required,
        std::chrono::milliseconds timeout,
        std::stop_token stopToken = {});
    FrameReadResult tryReadLatest(
        std::array<std::uint8_t, kMaximumFrameBytes>& destination,
        FrameCursor& cursor,
        FrameMetadata& metadata,
        const FrameSpec& required);

private:
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::array<std::uint8_t, kMaximumFrameBytes> latest_{};
    FrameMetadata metadata_{};
    std::uint64_t sequence_{};

    FrameReadResult copyLatestIfCompatible(
        std::array<std::uint8_t, kMaximumFrameBytes>& destination,
        FrameCursor& cursor,
        FrameMetadata& metadata,
        const FrameSpec& required);
};

} // namespace mistercast
