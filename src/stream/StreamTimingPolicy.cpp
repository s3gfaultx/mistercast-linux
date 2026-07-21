#include "stream/StreamTimingPolicy.h"

#include <algorithm>

namespace mistercast {

AdaptiveDeliveryMargin::AdaptiveDeliveryMargin(const Modeline& mode)
{
    reset(mode);
}

void AdaptiveDeliveryMargin::reset(const Modeline& mode)
{
    conservativeLines_ = std::max<std::uint16_t>(1, mode.vTotal / 2);
    floorLines_ = std::max<std::uint16_t>(1, mode.vTotal * 3 / 8);
    currentLines_ = conservativeLines_;
    healthySamples_ = 0;
}

void AdaptiveDeliveryMargin::observe(const FpgaStatus& status)
{
    const bool healthy = status.vramSynced && !status.vgaFrameskip && status.vramQueue;
    if (!healthy) {
        currentLines_ = conservativeLines_;
        healthySamples_ = 0;
        return;
    }

    if (currentLines_ <= floorLines_) {
        return;
    }

    if (++healthySamples_ < kHealthySamplesPerStep) {
        return;
    }

    currentLines_ = static_cast<std::uint16_t>(std::max<int>(
        floorLines_, static_cast<int>(currentLines_) - kLinesPerStep));
    healthySamples_ = 0;
}

std::uint16_t calculateDeliverySyncLine(
    const Modeline& mode,
    std::size_t payloadBytes,
    std::uint32_t lastFrameNumber,
    std::chrono::nanoseconds lastStreamDuration,
    std::chrono::nanoseconds networkRoundTrip,
    std::size_t mtuPayload,
    std::uint16_t minimumDeliveryLines)
{
    if (lastFrameNumber < 10) {
        return mode.vTotal / 2;
    }

    const auto lineDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::nano>(
            static_cast<double>(mode.hTotal) / mode.pixelClockMHz * 1000.0));
    const auto frameDuration =
        lineDuration * mode.vTotal / (mode.interlaced ? 2 : 1);
    constexpr auto safetyMargin = std::chrono::microseconds(2500);
    const auto packetCount = (payloadBytes + mtuPayload - 1) / mtuPayload;
    const auto wireBytes = payloadBytes + packetCount * 66;
    const auto wireDuration = std::chrono::nanoseconds(
        static_cast<std::int64_t>(wireBytes * 8));
    const auto pipelineDuration = std::max(lastStreamDuration, wireDuration) +
        networkRoundTrip / 2 + safetyMargin;
    const auto protocolCountDuration = frameDuration / mode.vTotal;
    const auto measuredCounts =
        (pipelineDuration.count() + protocolCountDuration.count() - 1) /
        protocolCountDuration.count();
    const auto minimumLines = minimumDeliveryLines == 0
        ? mode.vTotal / 2
        : std::min<std::uint16_t>(minimumDeliveryLines, mode.vTotal - 1);
    const auto requiredLines = std::clamp<std::int64_t>(
        std::max<std::int64_t>(measuredCounts, minimumLines),
        1,
        mode.vTotal - 1);

    return static_cast<std::uint16_t>(mode.vTotal - requiredLines);
}

std::int64_t calculatePacingLineCorrection(
    const Modeline& mode,
    const FpgaStatus& status)
{
    const auto interlaceShift = mode.interlaced ? 1 : 0;
    const auto echoedLine =
        (static_cast<std::int64_t>(status.frameEcho - 1) * mode.vTotal +
         status.vCountEcho) >> interlaceShift;
    const auto displayedLine =
        (static_cast<std::int64_t>(status.frame) * mode.vTotal + status.vCount) >>
        interlaceShift;

    return std::clamp<std::int64_t>(
        (echoedLine - displayedLine) / 2,
        -static_cast<std::int64_t>(mode.vTotal / 4),
        static_cast<std::int64_t>(mode.vTotal / 4));
}

std::chrono::nanoseconds pacingReleaseOffset(
    std::uint64_t wireBytesReleased,
    std::uint64_t bitsPerSecond)
{
    if (bitsPerSecond == 0) {
        return std::chrono::nanoseconds::zero();
    }

    return std::chrono::nanoseconds(static_cast<std::int64_t>(
        wireBytesReleased * 8 * 1'000'000'000 / bitsPerSecond));
}

} // namespace mistercast
