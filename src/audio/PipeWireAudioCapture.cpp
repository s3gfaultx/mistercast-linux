#include "audio/PipeWireAudioCapture.h"

#include <QMetaObject>

#include <spa/param/audio/format-utils.h>
#include <spa/param/format-utils.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace mistercast {
namespace {

const pw_stream_events kAudioStreamEvents = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = PipeWireAudioCapture::onStateChanged;
    events.process = PipeWireAudioCapture::onProcess;
    return events;
}();

} // namespace

PipeWireAudioCapture::PipeWireAudioCapture(QObject* parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
}

PipeWireAudioCapture::~PipeWireAudioCapture()
{
    stop();
}

bool PipeWireAudioCapture::start(QString& error)
{
    stop();

    loop_ = pw_thread_loop_new("mistercast-audio", nullptr);

    if (loop_ == nullptr) {
        error = QStringLiteral("Unable to create the PipeWire audio loop");
        return false;
    }

    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);

    if (context_ == nullptr || pw_thread_loop_start(loop_) < 0) {
        error = QStringLiteral("Unable to start the PipeWire audio context");
        stop();
        return false;
    }

    loopStarted_ = true;

    pw_thread_loop_lock(loop_);

    core_ = pw_context_connect(context_, nullptr, 0);

    if (core_ == nullptr) {
        pw_thread_loop_unlock(loop_);
        error = QStringLiteral("Unable to connect to PipeWire for audio capture");
        stop();
        return false;
    }

    auto* properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        PW_KEY_STREAM_CAPTURE_SINK, "true",
        PW_KEY_NODE_LATENCY, "128/48000",
        PW_KEY_NODE_NAME, "mistercast-audio-capture",
        nullptr);

    stream_ = pw_stream_new(core_, "MiSTerCast output audio", properties);

    if (stream_ == nullptr) {
        pw_thread_loop_unlock(loop_);
        error = QStringLiteral("Unable to create the PipeWire audio stream");
        stop();
        return false;
    }

    pw_stream_add_listener(stream_, &streamListener_, &kAudioStreamEvents, this);

    std::array<std::uint8_t, 1024> parameterBuffer{};
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(parameterBuffer.data(), parameterBuffer.size());
    
    spa_audio_info_raw audioInfo{};
    audioInfo.format = SPA_AUDIO_FORMAT_S16;
    audioInfo.rate = 48'000;
    audioInfo.channels = 2;
    audioInfo.position[0] = SPA_AUDIO_CHANNEL_FL;
    audioInfo.position[1] = SPA_AUDIO_CHANNEL_FR;

    const spa_pod* parameters[] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audioInfo),
    };

    running_ = true;

    const int result = pw_stream_connect(
        stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        parameters, 1);

    pw_thread_loop_unlock(loop_);
    
    if (result < 0) {
        running_ = false;
        error = QStringLiteral("Unable to connect the PipeWire output monitor stream");
        stop();
        return false;
    }

    return true;
}

void PipeWireAudioCapture::stop()
{
    running_ = false;

    ++generation_;

    if (loop_ != nullptr && loopStarted_) {
        pw_thread_loop_lock(loop_);
    }

    if (stream_ != nullptr) {
        pw_stream_disconnect(stream_);
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }

    if (core_ != nullptr) {
        pw_core_disconnect(core_);
        core_ = nullptr;
    }

    if (loop_ != nullptr && loopStarted_) {
        pw_thread_loop_unlock(loop_);
        pw_thread_loop_stop(loop_);
        loopStarted_ = false;
    }

    if (context_ != nullptr) {
        pw_context_destroy(context_);
        context_ = nullptr;
    }

    if (loop_ != nullptr) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }

    sampleRing_.clear();
    jitterReserve_.reset();
}

std::size_t PipeWireAudioCapture::read(std::span<std::int16_t> destination)
{
    const auto read = sampleRing_.read(destination, jitterReserve_.frames());
    const auto requested = destination.size() - destination.size() % 2;
    jitterReserve_.observeRead(read == requested);

    return read;
}

bool PipeWireAudioCapture::waitForBufferedSamples(
    std::size_t samples,
    std::chrono::milliseconds timeout,
    std::stop_token stopToken)
{
    return sampleRing_.waitForSamples(samples, timeout, stopToken);
}

AudioCaptureDiagnostics PipeWireAudioCapture::diagnostics() const
{
    return sampleRing_.diagnostics();
}

void PipeWireAudioCapture::onStateChanged(
    void* data,
    pw_stream_state,
    pw_stream_state state,
    const char* error)
{
    if (state != PW_STREAM_STATE_ERROR) {
        return;
    }

    auto* capture = static_cast<PipeWireAudioCapture*>(data);

    if (!capture->running_) {
        return;
    }

    const QString message = QStringLiteral("PipeWire audio stream failed: %1")
                                .arg(QString::fromLocal8Bit(error == nullptr ? "unknown error" : error));

    const auto generation = capture->generation_.load();

    QMetaObject::invokeMethod(capture, [capture, message, generation] {
        if (generation == capture->generation_.load()) {
            emit capture->failed(message);
        }
    });
}

void PipeWireAudioCapture::onProcess(void* data)
{
    static_cast<PipeWireAudioCapture*>(data)->processAudio();
}

void PipeWireAudioCapture::processAudio()
{
    constexpr std::size_t kMaximumBuffersPerCallback = 32;
    for (std::size_t bufferIndex = 0;
         bufferIndex < kMaximumBuffersPerCallback;
         ++bufferIndex) {
        pw_buffer* buffer = pw_stream_dequeue_buffer(stream_);

        if (buffer == nullptr) {
            return;
        }

        const spa_buffer* spaBuffer = buffer->buffer;

        if (spaBuffer->n_datas > 0 && spaBuffer->datas[0].data != nullptr &&
            spaBuffer->datas[0].chunk != nullptr && spaBuffer->datas[0].maxsize != 0 &&
            (spaBuffer->datas[0].chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) == 0) {

            const auto& data = spaBuffer->datas[0];
            const std::size_t offset = data.chunk->offset % data.maxsize;
            const auto* bytes = static_cast<const std::uint8_t*>(data.data);
            const std::size_t validBytes = std::min<std::size_t>(data.chunk->size, data.maxsize);

            auto sampleCount = validBytes / sizeof(std::int16_t);
            sampleCount -= sampleCount % 2;

            const bool empty = (data.chunk->flags & SPA_CHUNK_FLAG_EMPTY) != 0;

            sampleRing_.writeGenerated(sampleCount, [&](std::size_t i) {
                std::int16_t sample = 0;

                if (!empty) {
                    const std::size_t byteIndex = (offset + i * sizeof(sample)) % data.maxsize;

                    if (byteIndex + sizeof(sample) <= data.maxsize) {
                        std::memcpy(&sample, bytes + byteIndex, sizeof(sample));
                    } else {
                        std::array<std::uint8_t, sizeof(sample)> wrapped{};
                        wrapped[0] = bytes[byteIndex];
                        wrapped[1] = bytes[0];
                        std::memcpy(&sample, wrapped.data(), sizeof(sample));
                    }
                }

                return sample;
            });
        }

        pw_stream_queue_buffer(stream_, buffer);
    }
}

} // namespace mistercast
