#include "app/StreamingCoordinator.h"

#include "stream/StreamConfig.h"

namespace mistercast {

StreamingCoordinator::StreamingCoordinator(QObject* parent)
    : QObject(parent)
    , audioCapture_(this)
    , videoCapture_(mailbox_, this)
    , portal_(this)
    , controller_(mailbox_, audioCapture_, this)
{
    connect(&portal_, &ScreenCastPortal::streamReady, this,
        [this](int descriptor, std::uint32_t nodeId, std::uint64_t pipeWireSerial) {
            QString error;
            if (!videoCapture_.start(descriptor, nodeId, pipeWireSerial, error)) {
                sourceReady_ = false;
                videoCapture_.stop();
                portal_.stop();
                emit failed(error);
                emit stateChanged();
            }
        });

    connect(&portal_, &ScreenCastPortal::cancelled,
        this, &StreamingCoordinator::sourceSelectionCancelled);

    connect(&portal_, &ScreenCastPortal::failed, this,
        [this](const QString& message) {
            sourceReady_ = false;
            stopStreaming();
            videoCapture_.stop();
            emit failed(message);
            emit stateChanged();
        });

    connect(&videoCapture_, &PipeWireVideoCapture::started, this,
        [this](std::uint32_t width, std::uint32_t height, const QString& format) {
            sourceReady_ = true;
            emit sourceStarted(width, height, format);
            emit stateChanged();
        });

    connect(&videoCapture_, &PipeWireVideoCapture::failed, this,
        [this](const QString& message) {
            sourceReady_ = false;
            stopStreaming();
            videoCapture_.stop();
            portal_.stop();
            emit failed(message);
            emit stateChanged();
        });

    connect(&audioCapture_, &PipeWireAudioCapture::failed, this,
        [this](const QString& message) {
            stopStreaming();
            emit failed(message);
        });

    connect(&controller_, &StreamController::started, this,
        [this] {
            emit streamStarted();
            emit stateChanged();
        });

    connect(&controller_, &StreamController::stopped, this,
        [this] {
            if (controller_.running()) {
                return;
            }
            audioCapture_.stop();
            emit streamStopped();
            emit stateChanged();
        });

    connect(&controller_, &StreamController::failed,
        this, &StreamingCoordinator::failed);

    connect(&controller_, &StreamController::warning,
        this, &StreamingCoordinator::warning);

    connect(&controller_, &StreamController::videoDiagnostics,
        this, &StreamingCoordinator::videoDiagnostics);

    connect(&controller_, &StreamController::audioDiagnostics,
        this, &StreamingCoordinator::audioDiagnostics);

    connect(&controller_, &StreamController::fpgaDiagnostics,
        this, &StreamingCoordinator::fpgaDiagnostics);
}

StreamingCoordinator::~StreamingCoordinator()
{
    shutdown();
}

void StreamingCoordinator::selectSource()
{
    if (controller_.running()) {
        return;
    }

    sourceReady_ = false;

    videoCapture_.stop();
    emit sourceSelectionStarted();
    emit stateChanged();
    portal_.selectSource();
}

void StreamingCoordinator::startStreaming(std::string host, bool audioEnabled)
{
    if (!sourceReady_ || controller_.running()) {
        return;
    }

    if (audioEnabled) {
        QString error;
        if (!audioCapture_.start(error)) {
            emit failed(error);
            return;
        }
    }

    emit streamConnecting(audioEnabled);

    StreamConfig config{
        std::move(host),
        audioEnabled,
        outputMode_,
        modeEpoch_,
    };

    if (!controller_.start(std::move(config))) {
        audioCapture_.stop();
        emit failed(QStringLiteral("Unable to start the stream worker"));
    }

    emit stateChanged();
}

void StreamingCoordinator::stopStreaming()
{
    const bool wasStreaming = controller_.running();

    controller_.stop();
    audioCapture_.stop();

    if (wasStreaming) {
        emit streamStopped();
        emit stateChanged();
    }
}

void StreamingCoordinator::shutdown()
{
    stopStreaming();

    videoCapture_.stop();
    portal_.stop();

    sourceReady_ = false;
}

void StreamingCoordinator::setOutputMode(const Modeline& modeline)
{
    outputMode_ = modeline;
    modeEpoch_ = videoCapture_.setOutputMode(modeline);
}

void StreamingCoordinator::setCropSettings(CropSettings settings)
{
    videoCapture_.setCropSettings(settings);
}

} // namespace mistercast
