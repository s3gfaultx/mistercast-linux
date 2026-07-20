#pragma once

#include "audio/PipeWireAudioCapture.h"
#include "core/FrameMailbox.h"
#include "stream/StreamConfig.h"
#include "stream/StreamDiagnostics.h"

#include <QObject>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace mistercast {

class StreamSession;

class StreamController final : public QObject {
    Q_OBJECT

public:
    StreamController(
        FrameMailbox& mailbox,
        PipeWireAudioCapture& audioCapture,
        QObject* parent = nullptr);
    ~StreamController() override;

    bool start(StreamConfig config);
    void stop();
    [[nodiscard]] bool running() const { return running_.load(); }

signals:
    void started();
    void stopped();
    void failed(const QString& message);
    void warning(const QString& message);
    void videoDiagnostics(const VideoDiagnostics& snapshot);
    void audioDiagnostics(const AudioDiagnostics& snapshot);
    void fpgaDiagnostics(const FpgaDiagnostics& snapshot);

private:
    FrameMailbox& mailbox_;
    PipeWireAudioCapture& audioCapture_;
    std::unique_ptr<StreamSession> session_;
    std::unique_ptr<std::jthread> thread_;
    std::atomic_bool running_{};
    std::uint64_t sessionGeneration_{};
};

} // namespace mistercast
