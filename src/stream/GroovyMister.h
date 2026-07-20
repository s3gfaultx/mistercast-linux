#pragma once

#include "core/Modeline.h"
#include "core/StreamTypes.h"
#include "stream/GroovyProtocolCodec.h"
#include "stream/StreamTimingPolicy.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct mmsghdr;
struct iovec;

namespace mistercast {

struct StreamTimings {
    std::chrono::nanoseconds compression{};
    std::chrono::nanoseconds capacityWait{};
    std::chrono::nanoseconds socketSubmission{};
    std::size_t transmittedBytes{};
    std::uint16_t deliveryReserveLines{};
};

enum class VideoSubmitStatus {
    Sent,
    DroppedBeforeCommand,
    Fatal,
};

struct VideoSubmitResult {
    VideoSubmitStatus status{VideoSubmitStatus::Fatal};
    StreamTimings timings{};
    std::string error;
};

struct FrameCapacityResult {
    bool ready{};
    std::chrono::nanoseconds wait{};
    std::string warning;
};

struct TransportDiagnostics {
    std::uint64_t audioPacketsRequested{};
    std::uint64_t audioPacketsSent{};
    std::uint64_t audioPacketsCoreDisabled{};
    std::uint64_t pacingOverruns{};
    std::uint64_t fpgaStatusSamples{};
    std::uint64_t fpgaFrameskipSamples{};
    std::uint64_t fpgaUnsyncedSamples{};
    std::uint64_t fpgaQueueEmptySamples{};
    std::uint32_t socketQueueHighWaterBytes{};
    std::chrono::nanoseconds maximumPacingLateness{};
};

class GroovyMister {
public:
    static constexpr std::uint16_t kDefaultPort = 32100;

    GroovyMister();
    ~GroovyMister();

    GroovyMister(const GroovyMister&) = delete;
    GroovyMister& operator=(const GroovyMister&) = delete;

    bool connect(
        const std::string& host,
        bool enableAudio,
        const Modeline& modeline,
        std::string& error,
        std::uint16_t port = kDefaultPort);
    void close();

    VideoSubmitResult sendFrame(
        std::span<const std::uint8_t> frame,
        std::uint32_t frameNumber,
        FieldParity field);
    FrameCapacityResult prepareFrame();
    bool sendAudio(std::span<const std::int16_t> samples, std::string& error);
    void resetSync();
    void waitSync();

    [[nodiscard]] const FpgaStatus& status() const { return status_; }
    [[nodiscard]] const TransportDiagnostics& diagnostics() const { return diagnostics_; }

private:
    static constexpr std::size_t kMtuPayload = 1472;
    static constexpr std::size_t kMaximumCompressedBytes =
        kMaximumFrameBytes + kMaximumFrameBytes / 255 + 16;
    static constexpr std::size_t kMaxPayloadPackets =
        (kMaximumCompressedBytes + kMtuPayload - 1) / kMtuPayload;

    bool sendCommand(std::span<const std::uint8_t> command, std::string& error);
    bool sendPayload(std::span<const std::uint8_t> payload, std::string& error);
    bool waitForSendCapacity(
        std::string& error,
        std::chrono::nanoseconds& elapsed,
        std::chrono::milliseconds maximumWait);
    
    int sampleSocketQueue();
    void pollStatus();
    std::uint64_t receiveStatus(std::chrono::milliseconds timeout);
    bool parseStatus(
        std::span<const std::uint8_t, GroovyProtocolCodec::kStatusPacketSize> packet);
    int socket_{-1};
    
    bool connected_{};
    bool streamBroken_{};
    bool audioEnabled_{};

    FpgaStatus status_{};
    Modeline mode_{kDefaultModeline};
    AdaptiveDeliveryMargin deliveryMargin_;
    std::vector<char> compressed_;
    std::vector<mmsghdr> messages_;
    std::vector<iovec> vectors_;

    std::chrono::steady_clock::time_point lastSync_{};
    std::chrono::nanoseconds frameDuration_{};
    std::chrono::nanoseconds lineDuration_{};
    std::chrono::nanoseconds lastStreamDuration_{};
    std::chrono::nanoseconds networkRoundTrip_{};
    std::uint32_t lastFrameNumber_{};
    std::uint64_t statusGeneration_{};
    std::uint64_t consumedStatusGeneration_{};
    TransportDiagnostics diagnostics_{};
};

} // namespace mistercast
