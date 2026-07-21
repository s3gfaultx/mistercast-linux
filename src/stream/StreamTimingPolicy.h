#pragma once

#include "core/Modeline.h"
#include "stream/GroovyProtocolCodec.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace mistercast {

class AdaptiveDeliveryMargin {
public:
    static constexpr std::uint32_t kHealthySamplesPerStep = 300;
    static constexpr std::uint16_t kLinesPerStep = 4;

    explicit AdaptiveDeliveryMargin(const Modeline& mode = kDefaultModeline);

    void reset(const Modeline& mode);
    void observe(const FpgaStatus& status);
    [[nodiscard]] std::uint16_t minimumLines() const { return currentLines_; }

private:
    std::uint16_t conservativeLines_{};
    std::uint16_t floorLines_{};
    std::uint16_t currentLines_{};
    std::uint32_t healthySamples_{};
};

[[nodiscard]] std::uint16_t calculateDeliverySyncLine(
    const Modeline& mode,
    std::size_t payloadBytes,
    std::uint32_t lastFrameNumber,
    std::chrono::nanoseconds lastStreamDuration,
    std::chrono::nanoseconds networkRoundTrip,
    std::size_t mtuPayload = 1'472,
    std::uint16_t minimumDeliveryLines = 0);

[[nodiscard]] std::int64_t calculatePacingLineCorrection(
    const Modeline& mode,
    const FpgaStatus& status);

// Target wire rate for pacing the UDP payload burst. Kept just under the
// Groovy_MiSTer 1 Gb link so a faster source NIC (e.g. 2.5 Gb) cannot overrun
// an intermediate switch's egress buffer toward the MiSTer.
inline constexpr std::uint64_t kPacingBitsPerSecond = 950'000'000;
// Packets released between pacing gates (~49 KB per burst) and the per-packet
// wire overhead (Ethernet/IP/UDP), matching the wire model in
// calculateDeliverySyncLine.
inline constexpr std::size_t kPacingBurstPackets = 32;
inline constexpr std::size_t kPacingPacketWireOverhead = 66;

// Time offset from the start of a send at which `wireBytesReleased` bytes
// should have been released at the target rate. Pure integer arithmetic.
[[nodiscard]] std::chrono::nanoseconds pacingReleaseOffset(
    std::uint64_t wireBytesReleased,
    std::uint64_t bitsPerSecond = kPacingBitsPerSecond);

} // namespace mistercast
