#pragma once

#include "audio/PipeWireAudioCapture.h"
#include "core/FrameMailbox.h"
#include "stream/StreamConfig.h"
#include "stream/StreamDiagnostics.h"

#include <QObject>

#include <stop_token>

namespace mistercast {

class StreamSession final : public QObject {
    Q_OBJECT

public:
    StreamSession(
        FrameMailbox& mailbox,
        PipeWireAudioCapture& audioCapture,
        StreamConfig config);

    void run(std::stop_token stopToken);

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
    StreamConfig config_;
};

} // namespace mistercast
