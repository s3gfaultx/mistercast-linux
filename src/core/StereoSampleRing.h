#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <stop_token>

namespace mistercast {

struct AudioCaptureDiagnostics {
    std::uint64_t staleSamplesDiscarded{};
    std::uint64_t overflowSamples{};
    std::size_t bufferedSamples{};
};

class AdaptiveJitterReserve {
public:
    static constexpr std::size_t kMinimumFrames = 128;
    static constexpr std::size_t kInitialFrames = 256;
    static constexpr std::size_t kMaximumFrames = 512;
    static constexpr std::uint32_t kSuccessfulReadsPerStep = 600;

    void reset();
    void observeRead(bool complete);
    [[nodiscard]] std::size_t frames() const { return frames_; }

private:
    std::size_t frames_{kInitialFrames};
    std::uint32_t successfulReads_{};
};

class StereoSampleRing {
public:
    static constexpr std::size_t kCapacitySamples = 8'192;

    template<typename SampleAt>
    void writeGenerated(std::size_t sampleCount, SampleAt sampleAt)
    {
        sampleCount -= sampleCount % 2;
        {
            std::lock_guard lock(mutex_);
            maximumWriteSamples_ = std::max(
                maximumWriteSamples_, std::min(sampleCount, samples_.size()));
            for (std::size_t i = 0; i < sampleCount; ++i) {
                if (available_ == samples_.size()) {
                    readPosition_ = (readPosition_ + 1) % samples_.size();
                    --available_;
                    ++overflowSamples_;
                }
                samples_[writePosition_] = sampleAt(i);
                writePosition_ = (writePosition_ + 1) % samples_.size();
                ++available_;
            }
        }
        samplesAvailable_.notify_all();
    }

    void observeDeliveryQuantum(std::size_t sampleCount);
    void write(std::span<const std::int16_t> samples);
    std::size_t read(std::span<std::int16_t> destination, std::size_t reserveFrames);
    bool waitForSamples(
        std::size_t samples,
        std::chrono::milliseconds timeout,
        std::stop_token stopToken);
    void clear();
    [[nodiscard]] AudioCaptureDiagnostics diagnostics() const;

private:
    std::array<std::int16_t, kCapacitySamples> samples_{};
    std::size_t readPosition_{};
    std::size_t writePosition_{};
    std::size_t available_{};
    std::uint64_t staleSamplesDiscarded_{};
    std::uint64_t overflowSamples_{};
    std::size_t maximumWriteSamples_{};
    mutable std::mutex mutex_;
    std::condition_variable_any samplesAvailable_;
};

} // namespace mistercast
