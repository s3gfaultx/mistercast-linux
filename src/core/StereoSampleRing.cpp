#include "core/StereoSampleRing.h"

namespace mistercast {

void StereoSampleRing::observeDeliveryQuantum(std::size_t sampleCount)
{
    std::lock_guard lock(mutex_);
    maximumWriteSamples_ = std::max(
        maximumWriteSamples_, std::min(sampleCount - sampleCount % 2, samples_.size()));
}

void AdaptiveJitterReserve::reset()
{
    frames_ = kInitialFrames;
    successfulReads_ = 0;
}

void AdaptiveJitterReserve::observeRead(bool complete)
{
    if (!complete) {
        frames_ = std::min(kMaximumFrames, frames_ + std::size_t{128});
        successfulReads_ = 0;
        return;
    }

    if (++successfulReads_ >= kSuccessfulReadsPerStep) {
        frames_ = std::max(kMinimumFrames, frames_ - std::size_t{128});
        successfulReads_ = 0;
    }
}

void StereoSampleRing::write(std::span<const std::int16_t> samples)
{
    writeGenerated(samples.size(), [samples](std::size_t index) {
        return samples[index];
    });
}

std::size_t StereoSampleRing::read(
    std::span<std::int16_t> destination,
    std::size_t reserveFrames)
{
    std::lock_guard lock(mutex_);
    const std::size_t writableSamples = destination.size() - destination.size() % 2;
    const std::size_t targetMaximum =
        reserveFrames * 2 + writableSamples + maximumWriteSamples_;

    if (available_ > targetMaximum) {
        std::size_t discard = available_ - targetMaximum;
        discard -= discard % 2;
        readPosition_ = (readPosition_ + discard) % samples_.size();
        available_ -= discard;
        staleSamplesDiscarded_ += discard;
    }

    auto count = std::min(writableSamples, available_);

    count -= count % 2;

    for (std::size_t i = 0; i < count; ++i) {
        destination[i] = samples_[readPosition_];
        readPosition_ = (readPosition_ + 1) % samples_.size();
    }

    available_ -= count;

    return count;
}

bool StereoSampleRing::waitForSamples(
    std::size_t samples,
    std::chrono::milliseconds timeout,
    std::stop_token stopToken)
{
    std::unique_lock lock(mutex_);

    return samplesAvailable_.wait_for(lock, stopToken, timeout, [this, samples] {
        return available_ >= samples;
    });
}

void StereoSampleRing::clear()
{
    {
        std::lock_guard lock(mutex_);
        readPosition_ = 0;
        writePosition_ = 0;
        available_ = 0;
        staleSamplesDiscarded_ = 0;
        overflowSamples_ = 0;
        maximumWriteSamples_ = 0;
    }

    samplesAvailable_.notify_all();
}

AudioCaptureDiagnostics StereoSampleRing::diagnostics() const
{
    std::lock_guard lock(mutex_);
    
    return {
        staleSamplesDiscarded_,
        overflowSamples_,
        available_,
    };
}

} // namespace mistercast
