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

} // namespace mistercast
