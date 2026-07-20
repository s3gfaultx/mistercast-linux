#pragma once

#include "core/FrameMailbox.h"
#include "core/FrameProcessor.h"
#include "core/Modeline.h"

#include <QObject>

#include <pipewire/pipewire.h>
#include <spa/param/video/raw.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace mistercast {

class PipeWireVideoCapture final : public QObject {
    Q_OBJECT

public:
    explicit PipeWireVideoCapture(FrameMailbox& mailbox, QObject* parent = nullptr);
    ~PipeWireVideoCapture() override;

    bool start(
        int pipeWireFileDescriptor,
        std::uint32_t nodeId,
        std::uint64_t pipeWireSerial,
        QString& error);
    void stop();
    void setCropSettings(CropSettings settings);
    ModeEpoch setOutputMode(const Modeline& modeline);

signals:
    void started(std::uint32_t width, std::uint32_t height, const QString& format);
    void failed(const QString& message);

public:
    static void onStateChanged(
        void* data, pw_stream_state oldState, pw_stream_state state, const char* error);
    static void onParamChanged(void* data, std::uint32_t id, const spa_pod* parameter);
    static void onProcess(void* data);

private:
    void handleStateChanged(pw_stream_state state, const char* error);
    void handleParamChanged(std::uint32_t id, const spa_pod* parameter);
    void processFrame();
    static bool pixelFormat(spa_video_format format, PixelFormat& output);
    static QString formatName(spa_video_format format);

    FrameMailbox& mailbox_;
    pw_thread_loop* loop_{};
    pw_context* context_{};
    pw_core* core_{};
    pw_stream* stream_{};
    spa_hook streamListener_{};
    spa_video_info_raw format_{};
    CropSettings crop_{};
    std::mutex cropMutex_;
    Modeline outputMode_{kDefaultModeline};
    mutable std::mutex modeMutex_;
    ModeEpoch modeEpoch_{1};
    std::array<std::uint8_t, kMaximumFrameBytes> converted_{};
    std::array<std::uint8_t, kMaximumFrameBytes> previousConverted_{};
    std::mutex frameMutex_;
    std::uint16_t convertedWidth_{};
    std::uint16_t convertedHeight_{};
    std::uint16_t cachedConvertedWidth_{};
    std::uint16_t cachedConvertedHeight_{};
    FrameMailbox::Clock::time_point convertedCapturedAt_{};
    bool hasConvertedFrame_{};
    bool convertedFrameIsSynthetic_{};
    std::atomic_bool running_{};
    std::atomic_uint64_t generation_{};
    bool loopStarted_{};
    bool reachedStreamingState_{};
};

} // namespace mistercast
