#pragma once

#include "core/StereoSampleRing.h"

#include <QObject>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>

#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>

namespace mistercast {

class PipeWireAudioCapture final : public QObject {
    Q_OBJECT

public:
    explicit PipeWireAudioCapture(QObject* parent = nullptr);
    ~PipeWireAudioCapture() override;

    bool start(QString& error);
    void stop();
    std::size_t read(std::span<std::int16_t> destination);
    [[nodiscard]] std::size_t jitterReserveFrames() const
    {
        return jitterReserve_.frames();
    }
    bool waitForBufferedSamples(
        std::size_t samples,
        std::chrono::milliseconds timeout,
        std::stop_token stopToken);
    AudioCaptureDiagnostics diagnostics() const;

signals:
    void failed(const QString& message);

public:
    static void onStateChanged(
        void* data, pw_stream_state oldState, pw_stream_state state, const char* error);
    static void onProcess(void* data);

private:
    void processAudio();

    StereoSampleRing sampleRing_;

    pw_thread_loop* loop_{};
    pw_context* context_{};
    pw_core* core_{};
    pw_stream* stream_{};
    spa_hook streamListener_{};
    std::atomic_bool running_{};
    std::atomic_uint64_t generation_{};
    AdaptiveJitterReserve jitterReserve_;
    bool loopStarted_{};
};

} // namespace mistercast
