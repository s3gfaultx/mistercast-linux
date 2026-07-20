#include "core/FrameMailbox.h"
#include "core/StereoSampleRing.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>

using namespace mistercast;

int main()
{
    StereoSampleRing ring;

    std::atomic_bool audioProducerDone{};
    std::jthread audioProducer([&] {
        std::array<std::int16_t, 128> samples{};
        
        for (std::uint32_t iteration = 0; iteration < 10'000; ++iteration) {
            samples.fill(static_cast<std::int16_t>(iteration));
            ring.write(samples);
        }

        audioProducerDone.store(true, std::memory_order_release);
    });

    std::uint64_t samplesRead = 0;
    std::array<std::int16_t, 128> samples{};
    
    while (!audioProducerDone.load(std::memory_order_acquire) ||
           ring.diagnostics().bufferedSamples != 0) {
        ring.waitForSamples(2, std::chrono::milliseconds(1), {});
        const auto count = ring.read(samples, 0);
        
        if (count % 2 != 0) {
            return 1;
        }

        samplesRead += count;
    }

    if (samplesRead == 0) {
        return 2;
    }

    FrameMailbox mailbox;
    std::atomic_bool frameProducerDone{};
    std::jthread frameProducer([&] {
        std::array<std::uint8_t, 96> frame{};
        
        for (std::uint32_t iteration = 0; iteration < 10'000; ++iteration) {
            frame.fill(static_cast<std::uint8_t>(iteration));
            mailbox.publish(frame, 8, 4, ModeEpoch{3});
        }

        frameProducerDone.store(true, std::memory_order_release);
    });

    const FrameSpec required{96, 8, 4, ModeEpoch{3}};

    FrameCursor cursor;
    FrameMetadata metadata;

    std::array<std::uint8_t, kMaximumFrameBytes> frame{};
    std::uint64_t framesRead = 0;
    
    const auto validateFrame = [&] {
        for (const auto value : std::span(frame).first(required.bytes)) {
            if (value != frame.front()) {
                return false;
            }
        }

        ++framesRead;

        return true;
    };
    while (!frameProducerDone.load(std::memory_order_acquire)) {
        if (mailbox.tryReadLatest(frame, cursor, metadata, required) !=
            FrameReadResult::Copied) {
            std::this_thread::yield();
            continue;
        }

        if (!validateFrame()) {
            return 3;
        }
    }
    if (mailbox.tryReadLatest(frame, cursor, metadata, required) ==
            FrameReadResult::Copied &&
        !validateFrame()) {
            
        return 3;
    }
    return framesRead == 0 ? 4 : 0;
}
