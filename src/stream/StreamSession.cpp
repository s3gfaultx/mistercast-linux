#include "stream/StreamSession.h"

#include "core/AudioFrameCadence.h"
#include "core/FrameFields.h"
#include "core/FrameSequence.h"
#include "stream/GroovyMister.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace mistercast {

StreamSession::StreamSession(
    FrameMailbox& mailbox,
    PipeWireAudioCapture& audioCapture,
    StreamConfig config)
    : mailbox_(mailbox)
    , audioCapture_(audioCapture)
    , config_(std::move(config))
{
}

void StreamSession::run(std::stop_token stopToken)
{
    std::array<std::uint8_t, kMaximumFrameBytes> frame{};
    std::array<std::uint8_t, kMaximumFrameBytes> fieldFrame{};

    FrameCursor captureCursor;
    FrameMetadata frameMetadata;

    const FrameSpec requiredFrame{
        config_.modeline.fullFrameBytes(),
        config_.modeline.hActive,
        config_.modeline.vActive,
        config_.modeEpoch,
    };

    const auto frameDeadline = FrameMailbox::Clock::now() + std::chrono::seconds(3);
    bool matchingFrame = false;
    
    while (!stopToken.stop_requested() && FrameMailbox::Clock::now() < frameDeadline) {
        FrameMetadata candidate;
        if (mailbox_.waitForLatest(
                frame,
                captureCursor,
                candidate,
                requiredFrame,
                std::chrono::milliseconds(100),
                stopToken) == FrameReadResult::Copied) {
            frameMetadata = candidate;
            matchingFrame = true;
            
            break;
        }
    }

    if (!matchingFrame) {
        if (!stopToken.stop_requested()) {
            emit failed(QStringLiteral(
                "No desktop frame for the selected CRT mode arrived within three seconds"));
        }
        
        emit stopped();

        return;
    }

    GroovyMister transport;

    std::string error;
    if (!transport.connect(
            config_.host, config_.audioEnabled, config_.modeline, error)) {
        emit failed(QString::fromStdString(error));
        emit stopped();

        return;
    }

    std::array<std::int16_t, 2'402> audioSamples{};
    std::uint32_t frameNumber = synchronizeFrameNumber(1, transport.status().frame);
    AudioFrameCadence audioCadence(config_.modeline);
    std::size_t nextAudioFrames = config_.audioEnabled ? audioCadence.next() : 0;
    
    if (config_.audioEnabled) {
        const auto startupSamples =
            (nextAudioFrames + audioCapture_.jitterReserveFrames()) * 2;
        audioCapture_.waitForBufferedSamples(
            startupSamples, std::chrono::milliseconds(100), stopToken);
        
        transport.resetSync();
    }
    
    if (stopToken.stop_requested()) {
        transport.close();
        
        emit stopped();

        return;
    }

    emit started();

    std::array<double, 120> dequeueAgesMs{};
    std::chrono::nanoseconds totalConversion{};
    std::chrono::nanoseconds totalReadyWait{};
    std::chrono::nanoseconds totalCompression{};
    std::chrono::nanoseconds totalCapacityWait{};
    std::chrono::nanoseconds totalSubmission{};
    std::uint64_t totalPayload{};
    std::uint16_t deliveryReserveLines{};
    std::uint32_t statisticFrames{};
    std::uint32_t capturedFrames{};
    std::uint32_t reusedFrames{};
    std::uint32_t droppedFrames{};
    std::uint64_t audioUnderflowPackets{};
    std::uint64_t audioUnderflowFrames{};
    bool warnedCoreAudioDisabled = false;

    const auto sendAudio = [&] {
        if (!config_.audioEnabled) {
            return true;
        }

        const auto audioFrames = nextAudioFrames;
        nextAudioFrames = audioCadence.next();
        const auto sampleCount = audioFrames * 2;
        auto packet = std::span(audioSamples).first(sampleCount);
        const auto read = audioCapture_.read(packet);

        if (read < sampleCount) {
            ++audioUnderflowPackets;
            audioUnderflowFrames += (sampleCount - read) / 2;
        }

        std::fill(
            packet.begin() + static_cast<std::ptrdiff_t>(read), packet.end(), 0);

        if (!transport.sendAudio(packet, error)) {
            emit failed(QString::fromStdString(error));
            return false;
        }

        return true;
    };

    while (!stopToken.stop_requested()) {
        const FpgaStatus fpgaStatus = transport.status();
        frameNumber = synchronizeFrameNumber(frameNumber, fpgaStatus.frame);
        const auto preflightStarted = FrameMailbox::Clock::now();
        const auto capacity = transport.prepareFrame();

        if (!capacity.ready) {
            ++droppedFrames;

            if (droppedFrames == 1) {
                emit warning(QString::fromStdString(capacity.warning));
            }

            if (!sendAudio()) {
                break;
            }

            transport.waitSync();
            ++frameNumber;
            continue;
        }

        FrameMetadata newestCapturedAt;
        bool capturedNewFrame = false;
        
        if (mailbox_.tryReadLatest(
                frame, captureCursor, newestCapturedAt, requiredFrame) ==
            FrameReadResult::Copied) {
            frameMetadata = newestCapturedAt;
            capturedNewFrame = true;
        }
        
        if (stopToken.stop_requested()) {
            break;
        }

        const auto sendStarted = FrameMailbox::Clock::now();
        FieldParity field = FieldParity::Field0OddSourceLines;
        std::span<const std::uint8_t> payload(
            frame.data(), config_.modeline.payloadBytes());
        
         if (config_.modeline.interlaced) {
            field = interlacedFieldForFrame(
                frameNumber, fpgaStatus.frame, fpgaStatus.vgaField);
            
            if (!extractInterlacedField(
                    std::span(frame).first(config_.modeline.fullFrameBytes()),
                    config_.modeline,
                    field,
                    fieldFrame)) {
                emit failed(QStringLiteral("Unable to extract the interlaced field"));
                break;
            }

            payload = std::span(fieldFrame).first(config_.modeline.payloadBytes());
        }

        auto videoResult = transport.sendFrame(payload, frameNumber, field);
        videoResult.timings.capacityWait += capacity.wait;

        if (videoResult.status == VideoSubmitStatus::Fatal) {
            emit failed(QString::fromStdString(videoResult.error));

            break;
        }

        if (!sendAudio()) {
            break;
        }

        if (videoResult.status == VideoSubmitStatus::DroppedBeforeCommand) {
            ++droppedFrames;

            if (droppedFrames == 1) {
                emit warning(QString::fromStdString(videoResult.error));
            }

            transport.waitSync();

            ++frameNumber;

            continue;
        }

        if (capturedNewFrame) {
            dequeueAgesMs[capturedFrames] =
                std::chrono::duration<double, std::milli>(
                    sendStarted - frameMetadata.capturedAt).count();
            totalConversion += frameMetadata.publishedAt - frameMetadata.capturedAt;
            totalReadyWait += preflightStarted > frameMetadata.publishedAt
                ? preflightStarted - frameMetadata.publishedAt
                : std::chrono::nanoseconds{};
            ++capturedFrames;
        } else {
            ++reusedFrames;
        }

        totalCompression += videoResult.timings.compression;
        totalCapacityWait += videoResult.timings.capacityWait;
        totalSubmission += videoResult.timings.socketSubmission;
        totalPayload += videoResult.timings.transmittedBytes;
        deliveryReserveLines = videoResult.timings.deliveryReserveLines;
        
        ++statisticFrames;

        transport.waitSync();

        if (statisticFrames == dequeueAgesMs.size()) {
            if (config_.audioEnabled && !transport.status().audio &&
                !warnedCoreAudioDisabled) {
                emit warning(QStringLiteral(
                    "The MiSTer core reports network audio disabled; enable audio in the core OSD"));
                warnedCoreAudioDisabled = true;
            }

            auto sortedAges = dequeueAgesMs;
            std::sort(
                sortedAges.begin(),
                sortedAges.begin() + static_cast<std::ptrdiff_t>(capturedFrames));
            
            const auto percentileIndex = [](double percentile, std::size_t count) {
                return std::min(
                    count - 1,
                    static_cast<std::size_t>(
                        percentile * static_cast<double>(count - 1)));
            };

            emit videoDiagnostics(VideoDiagnostics{
                .frameNumber = frameNumber,
                .capturedFrames = capturedFrames,
                .reusedFrames = reusedFrames,
                .medianDequeueAgeMs = capturedFrames == 0 ? 0.0 :
                    sortedAges[percentileIndex(0.50, capturedFrames)],
                .p95DequeueAgeMs = capturedFrames == 0 ? 0.0 :
                    sortedAges[percentileIndex(0.95, capturedFrames)],
                .maximumDequeueAgeMs = capturedFrames == 0 ? 0.0 :
                    sortedAges[capturedFrames - 1],
                .averageCaptureProcessingUs = capturedFrames == 0 ? 0.0 :
                    std::chrono::duration<double, std::micro>(totalConversion).count() /
                    capturedFrames,
                .averageReadyWaitUs = capturedFrames == 0 ? 0.0 :
                    std::chrono::duration<double, std::micro>(totalReadyWait).count() /
                    capturedFrames,
                .averageCompressionUs =
                    std::chrono::duration<double, std::micro>(totalCompression).count() /
                    statisticFrames,
                .averageCapacityWaitUs =
                    std::chrono::duration<double, std::micro>(totalCapacityWait).count() /
                    statisticFrames,
                .averageSubmissionUs =
                    std::chrono::duration<double, std::micro>(totalSubmission).count() /
                    statisticFrames,
                .averagePayloadBytes =
                    static_cast<std::uint32_t>(totalPayload / statisticFrames),
                .deliveryReserveLines = deliveryReserveLines,
                .intervalDroppedFrames = droppedFrames,
            });

            const auto audioCaptureDiagnostics = audioCapture_.diagnostics();
            const auto& transportDiagnostics = transport.diagnostics();

            emit audioDiagnostics(AudioDiagnostics{
                .requestedPackets = transportDiagnostics.audioPacketsRequested,
                .sentPackets = transportDiagnostics.audioPacketsSent,
                .coreDisabledPackets = transportDiagnostics.audioPacketsCoreDisabled,
                .underflowPackets = audioUnderflowPackets,
                .underflowFrames = audioUnderflowFrames,
                .staleFramesDiscarded =
                    audioCaptureDiagnostics.staleSamplesDiscarded / 2,
                .overflowFrames = audioCaptureDiagnostics.overflowSamples / 2,
                .bufferedFrames = audioCaptureDiagnostics.bufferedSamples / 2,
                .jitterReserveFrames = static_cast<std::uint32_t>(
                    audioCapture_.jitterReserveFrames()),
                .socketQueueHighWaterBytes =
                    transportDiagnostics.socketQueueHighWaterBytes,
                .pacingOverruns = transportDiagnostics.pacingOverruns,
                .maximumPacingLatenessMs =
                    std::chrono::duration<double, std::milli>(
                        transportDiagnostics.maximumPacingLateness).count(),
            });

            emit fpgaDiagnostics(FpgaDiagnostics{
                .statusSamples = transportDiagnostics.fpgaStatusSamples,
                .fallbackSamples = transportDiagnostics.fpgaFrameskipSamples,
                .unsyncedSamples = transportDiagnostics.fpgaUnsyncedSamples,
                .queueEmptySamples = transportDiagnostics.fpgaQueueEmptySamples,
            });

            totalConversion = {};
            totalReadyWait = {};
            totalCompression = {};
            totalCapacityWait = {};
            totalSubmission = {};

            totalPayload = 0;
            statisticFrames = 0;
            capturedFrames = 0;
            reusedFrames = 0;
            droppedFrames = 0;
        }
        
        ++frameNumber;
    }

    transport.close();
    
    emit stopped();
}

} // namespace mistercast
