#pragma once

#include <QMetaType>

#include <cstdint>

namespace mistercast {

struct VideoDiagnostics {
    std::uint64_t frameNumber{};
    std::uint32_t capturedFrames{};
    std::uint32_t reusedFrames{};
    double medianDequeueAgeMs{};
    double p95DequeueAgeMs{};
    double maximumDequeueAgeMs{};
    double averageCaptureProcessingUs{};
    double averageReadyWaitUs{};
    double averageCompressionUs{};
    double averageCapacityWaitUs{};
    double averageSubmissionUs{};
    std::uint32_t averagePayloadBytes{};
    std::uint16_t deliveryReserveLines{};
    std::uint32_t intervalDroppedFrames{};
};

struct AudioDiagnostics {
    std::uint64_t requestedPackets{};
    std::uint64_t sentPackets{};
    std::uint64_t coreDisabledPackets{};
    std::uint64_t underflowPackets{};
    std::uint64_t underflowFrames{};
    std::uint64_t staleFramesDiscarded{};
    std::uint64_t overflowFrames{};
    std::uint64_t bufferedFrames{};
    std::uint32_t jitterReserveFrames{};
    std::uint32_t socketQueueHighWaterBytes{};
    std::uint64_t pacingOverruns{};
    double maximumPacingLatenessMs{};
};

struct FpgaDiagnostics {
    std::uint64_t statusSamples{};
    std::uint64_t fallbackSamples{};
    std::uint64_t unsyncedSamples{};
    std::uint64_t queueEmptySamples{};
};

} // namespace mistercast

Q_DECLARE_METATYPE(mistercast::VideoDiagnostics)
Q_DECLARE_METATYPE(mistercast::AudioDiagnostics)
Q_DECLARE_METATYPE(mistercast::FpgaDiagnostics)
