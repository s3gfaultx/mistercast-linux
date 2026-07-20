#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "stream/GroovyMister.h"

#include "core/FrameSequence.h"
#include "stream/StreamTimingPolicy.h"

#include <lz4.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace mistercast {
namespace {

constexpr std::uint8_t kCmdClose = 1;

std::string socketError(const char* operation)
{
    return std::string(operation) + ": " + std::strerror(errno);
}

} // namespace

GroovyMister::GroovyMister()
    : compressed_(static_cast<std::size_t>(LZ4_compressBound(kMaximumFrameBytes)))
    , messages_(kMaxPayloadPackets)
    , vectors_(kMaxPayloadPackets)
{
}

GroovyMister::~GroovyMister()
{
    close();
}

bool GroovyMister::connect(
    const std::string& host,
    bool enableAudio,
    const Modeline& modeline,
    std::string& error,
    std::uint16_t port)
{
    close();
    mode_ = modeline;
    deliveryMargin_.reset(mode_);
    lineDuration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::nano>(
            static_cast<double>(mode_.hTotal) / mode_.pixelClockMHz * 1000.0));
    frameDuration_ = lineDuration_ * mode_.vTotal / (mode_.interlaced ? 2 : 1);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* addresses = nullptr;

    const auto service = std::to_string(port);
    const int resolveResult = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);

    if (resolveResult != 0) {
        error = "Unable to resolve MiSTer host: " + std::string(gai_strerror(resolveResult));
        return false;
    }

    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        socket_ = ::socket(address->ai_family, address->ai_socktype | SOCK_CLOEXEC, address->ai_protocol);
        if (socket_ < 0) {
            continue;
        }
        if (::connect(socket_, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        ::close(socket_);
        socket_ = -1;
    }

    freeaddrinfo(addresses);

    if (socket_ < 0) {
        error = socketError("Unable to connect UDP socket");
        return false;
    }

    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = socketError("Unable to make UDP socket nonblocking");
        close();
        return false;
    }

    int sendBuffer = 2 * 1024 * 1024;
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, &sendBuffer, sizeof(sendBuffer)) < 0) {
        error = socketError("Unable to configure UDP send buffer");
        close();
        return false;
    }

    int pathMtuDiscovery = IP_PMTUDISC_DO;
    setsockopt(socket_, IPPROTO_IP, IP_MTU_DISCOVER, &pathMtuDiscovery, sizeof(pathMtuDiscovery));

    audioEnabled_ = enableAudio;
    streamBroken_ = false;

    const auto initCommand = GroovyProtocolCodec::makeInitCommand(enableAudio);
    if (!sendCommand(initCommand, error)) {
        close();
        return false;
    }

    const auto ackTime = receiveStatus(std::chrono::milliseconds(60));
    if (ackTime == 0) {
        error = "MiSTer did not acknowledge CMD_INIT within 60 ms";
        close();
        return false;
    }

    networkRoundTrip_ = std::chrono::nanoseconds(ackTime);
    connected_ = true;

    const auto switchCommand = GroovyProtocolCodec::makeModeSwitchCommand(mode_);
    if (!sendCommand(switchCommand, error)) {
        close();
        return false;
    }

    lastSync_ = std::chrono::steady_clock::now();

    return true;
}

void GroovyMister::close()
{
    if (socket_ >= 0) {
        if (connected_ && !streamBroken_) {
            const std::uint8_t command = kCmdClose;
            ::send(socket_, &command, sizeof(command), MSG_DONTWAIT);
        }
        ::close(socket_);
    }

    socket_ = -1;
    connected_ = false;
    streamBroken_ = false;
    audioEnabled_ = false;
    status_ = {};
    lastSync_ = {};
    lastStreamDuration_ = {};
    statusGeneration_ = 0;
    consumedStatusGeneration_ = 0;
    lastFrameNumber_ = 0;
    diagnostics_ = {};
}

VideoSubmitResult GroovyMister::sendFrame(
    std::span<const std::uint8_t> frame,
    std::uint32_t frameNumber,
    FieldParity field)
{
    VideoSubmitResult result;

    if (!connected_) {
        result.error = "Groovy_MiSTer transport is not connected";
        return result;
    }

    if (frame.size() != mode_.payloadBytes() ||
        (!mode_.interlaced && field != FieldParity::Field0OddSourceLines)) {
        result.error = "Frame payload does not match the selected modeline";
        return result;
    }

    const auto compressionStart = std::chrono::steady_clock::now();
    const int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(frame.data()),
        compressed_.data(),
        static_cast<int>(frame.size()),
        static_cast<int>(compressed_.size()));
    const auto compressionEnd = std::chrono::steady_clock::now();

    if (compressedSize <= 0) {
        result.error = "Unable to compress frame for the negotiated LZ4 stream";
        return result;
    }

    const auto payload = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(compressed_.data()),
        static_cast<std::size_t>(compressedSize));

    const auto syncLine = calculateDeliverySyncLine(
        mode_,
        payload.size(),
        lastFrameNumber_,
        lastStreamDuration_,
        networkRoundTrip_,
        kMtuPayload,
        deliveryMargin_.minimumLines());
    const auto size = static_cast<std::uint32_t>(payload.size());
    const auto command = GroovyProtocolCodec::makeBlitCommand(
        frameNumber, field, syncLine, size);

    if (!waitForSendCapacity(
            result.error, result.timings.capacityWait, std::chrono::milliseconds(0))) {
        result.status = VideoSubmitStatus::DroppedBeforeCommand;
        result.timings.compression = compressionEnd - compressionStart;
        return result;
    }

    if (!sendCommand(command, result.error)) {
        return result;
    }

    const auto sendStart = std::chrono::steady_clock::now();
    if (!sendPayload(payload, result.error)) {
        return result;
    }

    const auto sendEnd = std::chrono::steady_clock::now();

    lastStreamDuration_ = sendEnd - sendStart;
    lastFrameNumber_ = frameNumber;

    result.status = VideoSubmitStatus::Sent;
    result.timings.compression = compressionEnd - compressionStart;
    result.timings.socketSubmission = lastStreamDuration_;
    result.timings.transmittedBytes = payload.size();
    result.timings.deliveryReserveLines = static_cast<std::uint16_t>(mode_.vTotal - syncLine);

    return result;
}

FrameCapacityResult GroovyMister::prepareFrame()
{
    FrameCapacityResult result;
    result.ready = waitForSendCapacity(
        result.warning, result.wait, std::chrono::milliseconds(3));
    return result;
}

bool GroovyMister::sendAudio(std::span<const std::int16_t> samples, std::string& error)
{
    if (!audioEnabled_ || samples.empty()) {
        return true;
    }

    ++diagnostics_.audioPacketsRequested;

    if (!status_.audio) {
        ++diagnostics_.audioPacketsCoreDisabled;
        return true;
    }

    const auto byteSize = samples.size_bytes();
    if (byteSize > 0xffff) {
        error = "Audio packet exceeds Groovy_MiSTer protocol limit";
        return false;
    }

    const auto protocolSize = static_cast<std::uint16_t>(byteSize);
    const auto command = GroovyProtocolCodec::makeAudioCommand(protocolSize);
    if (!sendCommand(command, error)) {
        return false;
    }

    const bool sent = sendPayload(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size_bytes()),
        error);
    
        if (sent) {
        ++diagnostics_.audioPacketsSent;
    }

    return sent;
}

void GroovyMister::resetSync()
{
    lastSync_ = std::chrono::steady_clock::now();
    consumedStatusGeneration_ = statusGeneration_;
}

void GroovyMister::waitSync()
{
    if (!connected_) {
        return;
    }

    const auto baseTarget = lastSync_ + frameDuration_;
    auto target = baseTarget;

    const auto refreshStatus = [this, baseTarget, &target] {
        pollStatus();
        if (statusGeneration_ == consumedStatusGeneration_ ||
            status_.frameEcho == 0 || status_.frame == 0) {
            return false;
        }

        const auto lineCorrection = calculatePacingLineCorrection(mode_, status_);
        target = baseTarget + lineDuration_ * lineCorrection;
        consumedStatusGeneration_ = statusGeneration_;
        return true;
    };

    refreshStatus();

    const auto spinWindow = std::chrono::microseconds(200);
    bool overrunRecorded = false;
    const auto recordOverrun = [this, &target, &overrunRecorded] {
        const auto now = std::chrono::steady_clock::now();
        if (!overrunRecorded && now > target) {
            ++diagnostics_.pacingOverruns;
            diagnostics_.maximumPacingLateness = std::max(
                diagnostics_.maximumPacingLateness,
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - target));
            overrunRecorded = true;
        }
    };

    recordOverrun();

    for (;;) {
        const auto now = std::chrono::steady_clock::now();

        if (now >= target) {
            break;
        }

        if (target > now + spinWindow) {
            const bool awaitingAck = frameSequenceAfter(lastFrameNumber_, status_.frameEcho);
            if (awaitingAck) {
                const auto pollDelay = std::min(
                    target - now - spinWindow,
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::milliseconds(1)));

                std::this_thread::sleep_for(pollDelay);
                
                if (refreshStatus()) {
                    recordOverrun();
                }
            } else {
                std::this_thread::sleep_until(target - spinWindow);
            }
        } else {
            std::this_thread::yield();
        }
    }

    const auto finished = std::chrono::steady_clock::now();
    lastSync_ = finished > target + frameDuration_ ? finished : target;
}

void GroovyMister::pollStatus()
{
    receiveStatus(std::chrono::milliseconds(0));
}

bool GroovyMister::sendCommand(std::span<const std::uint8_t> command, std::string& error)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);

    for (;;) {
        const auto result = ::send(socket_, command.data(), command.size(), MSG_DONTWAIT);
        
        if (result == static_cast<ssize_t>(command.size())) {
            return true;
        }

        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) &&
            std::chrono::steady_clock::now() < deadline) {
            pollfd descriptor{socket_, POLLOUT, 0};
            ::poll(&descriptor, 1, 1);
            continue;
        }

        error = socketError("Unable to send Groovy_MiSTer command");

        if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            streamBroken_ = true;
        }

        return false;
    }
}

bool GroovyMister::sendPayload(
    std::span<const std::uint8_t> payload,
    std::string& error)
{
    const std::size_t packetCount = (payload.size() + kMtuPayload - 1) / kMtuPayload;
    
    if (packetCount > messages_.size()) {
        error = "UDP payload exceeds preallocated packet capacity";
        return false;
    }

    std::memset(messages_.data(), 0, packetCount * sizeof(messages_[0]));
    
    for (std::size_t i = 0; i < packetCount; ++i) {
        const auto offset = i * kMtuPayload;
        vectors_[i].iov_base = const_cast<std::uint8_t*>(payload.data() + offset);
        vectors_[i].iov_len = std::min(kMtuPayload, payload.size() - offset);
        messages_[i].msg_hdr.msg_iov = &vectors_[i];
        messages_[i].msg_hdr.msg_iovlen = 1;
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::max(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::milliseconds(5)),
                 frameDuration_ / 2);
    
                 std::size_t sent = 0;

    while (sent < packetCount) {
        const int result = sendmmsg(
            socket_,
            messages_.data() + sent,
            static_cast<unsigned int>(packetCount - sent),
            MSG_DONTWAIT);
        
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }

        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto now = std::chrono::steady_clock::now();
            
            if (now >= deadline) {
                streamBroken_ = true;
                error = "UDP payload submission deadline exceeded; stream aborted";

                return false;
            }

            pollfd descriptor{socket_, POLLOUT, 0};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now + std::chrono::milliseconds(1));
            
                if (::poll(&descriptor, 1, static_cast<int>(remaining.count())) > 0 &&
                (descriptor.revents & POLLOUT) != 0) {
                continue;
            }
            
            streamBroken_ = true;

            error = "UDP send queue remained full while completing a frame; stream aborted";

            return false;
        }

        streamBroken_ = true;

        error = socketError("Unable to submit UDP frame payload");

        return false;
    }

    sampleSocketQueue();

    return true;
}

bool GroovyMister::waitForSendCapacity(
    std::string& error,
    std::chrono::nanoseconds& elapsed,
    std::chrono::milliseconds maximumWait)
{
    const auto started = std::chrono::steady_clock::now();
    const auto recordElapsed = [&] {
        elapsed = std::chrono::steady_clock::now() - started;
    };
    const auto deadline = std::chrono::steady_clock::now() + maximumWait;
    for (;;) {
        const int queuedBytes = sampleSocketQueue();
        if (queuedBytes < 0) {
            recordElapsed();
            return true;
        }

        constexpr int lowWatermark = 16 * 1024;
        if (queuedBytes <= lowWatermark) {
            recordElapsed();
            return true;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            recordElapsed();
            error = "UDP queue held " + std::to_string(queuedBytes) +
                " bytes; newest frame dropped to avoid latency growth";
            return false;
        }

        pollfd descriptor{socket_, POLLOUT, 0};
        ::poll(&descriptor, 1, 1);
    }
}

int GroovyMister::sampleSocketQueue()
{
    int queuedBytes = 0;

    if (ioctl(socket_, TIOCOUTQ, &queuedBytes) < 0) {
        return -1;
    }

    diagnostics_.socketQueueHighWaterBytes = std::max(
        diagnostics_.socketQueueHighWaterBytes,
        static_cast<std::uint32_t>(std::max(queuedBytes, 0)));

    return queuedBytes;
}

std::uint64_t GroovyMister::receiveStatus(std::chrono::milliseconds timeout)
{
    if (socket_ < 0) {
        return 0;
    }

    const auto start = std::chrono::steady_clock::now();

    pollfd descriptor{socket_, POLLIN, 0};

    const int timeoutValue = static_cast<int>(timeout.count());
    
    if (::poll(&descriptor, 1, timeoutValue) <= 0) {
        return 0;
    }

    std::uint64_t elapsed = 0;
    for (;;) {
        GroovyProtocolCodec::StatusPacket packet{};
        const auto received = ::recv(socket_, packet.data(), packet.size(), MSG_DONTWAIT);
        if (received == static_cast<ssize_t>(packet.size())) {
            if (!parseStatus(packet)) {
                continue;
            }

            elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());

            continue;
        }
        if (received == 1) {
            elapsed = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());

            continue;
        }

        break;
    }

    return std::max<std::uint64_t>(elapsed, elapsed == 0 ? 0 : 1);
}

bool GroovyMister::parseStatus(
    std::span<const std::uint8_t, GroovyProtocolCodec::kStatusPacketSize> packet)
{
    const auto status = GroovyProtocolCodec::decodeStatus(packet);
    if (statusGeneration_ != 0 &&
        !frameSequenceAfter(status.frameEcho, status_.frameEcho)) {
        return false;
    }

    status_ = status;

    if (connected_) {
        deliveryMargin_.observe(status_);
        ++diagnostics_.fpgaStatusSamples;
        diagnostics_.fpgaFrameskipSamples += status_.vgaFrameskip ? 1 : 0;
        diagnostics_.fpgaUnsyncedSamples += status_.vramSynced ? 0 : 1;
        diagnostics_.fpgaQueueEmptySamples += status_.vramQueue ? 0 : 1;
    }

    ++statusGeneration_;
    
    return true;
}

} // namespace mistercast
