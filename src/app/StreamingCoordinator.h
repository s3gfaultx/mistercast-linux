#pragma once

#include "audio/PipeWireAudioCapture.h"
#include "capture/PipeWireVideoCapture.h"
#include "capture/ScreenCastPortal.h"
#include "core/FrameMailbox.h"
#include "core/FrameProcessor.h"
#include "core/Modeline.h"
#include "stream/StreamController.h"
#include "stream/StreamDiagnostics.h"

#include <QObject>

#include <cstdint>
#include <string>

namespace mistercast {

class StreamingCoordinator final : public QObject {
    Q_OBJECT

public:
    explicit StreamingCoordinator(QObject* parent = nullptr);
    ~StreamingCoordinator() override;

    void selectSource();
    void startStreaming(std::string host, bool audioEnabled);
    void stopStreaming();
    void shutdown();
    void setOutputMode(const Modeline& modeline);
    void setCropSettings(CropSettings settings);

    [[nodiscard]] bool sourceReady() const { return sourceReady_; }
    [[nodiscard]] bool streaming() const { return controller_.running(); }

signals:
    void sourceSelectionStarted();
    void sourceSelectionCancelled();
    void sourceStarted(
        std::uint32_t width,
        std::uint32_t height,
        const QString& format);
    void streamConnecting(bool audioEnabled);
    void streamStarted();
    void streamStopped();
    void stateChanged();
    void failed(const QString& message);
    void warning(const QString& message);
    void videoDiagnostics(const VideoDiagnostics& snapshot);
    void audioDiagnostics(const AudioDiagnostics& snapshot);
    void fpgaDiagnostics(const FpgaDiagnostics& snapshot);

private:
    FrameMailbox mailbox_;
    PipeWireAudioCapture audioCapture_;
    PipeWireVideoCapture videoCapture_;
    ScreenCastPortal portal_;
    StreamController controller_;
    Modeline outputMode_{kDefaultModeline};
    ModeEpoch modeEpoch_{1};
    bool sourceReady_{};
};

} // namespace mistercast
