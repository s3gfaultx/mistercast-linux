#include "core/Modeline.h"
#include "stream/GroovyMister.h"

#include <QtTest>

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <poll.h>
#include <span>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace mistercast;

namespace {

ssize_t receivePacket(int socket, std::span<std::uint8_t> buffer, sockaddr_in& peer)
{
    pollfd descriptor{socket, POLLIN, 0};
    if (poll(&descriptor, 1, 1000) <= 0) {
        return -1;
    }
    socklen_t peerLength = sizeof(peer);
    return recvfrom(
        socket, buffer.data(), buffer.size(), 0,
        reinterpret_cast<sockaddr*>(&peer), &peerLength);
}

} // namespace

class GroovyMisterTest final : public QObject {
    Q_OBJECT

private slots:
    void lz4InterlacedProtocolAndAckCoherence();
};

void GroovyMisterTest::lz4InterlacedProtocolAndAckCoherence()
{
    const Modeline mode{
        "320x480i", 6.700, 320, 336, 367, 426, 480, 488, 493, 525, true};
    const int server = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    QVERIFY(server >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    QVERIFY(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

    socklen_t addressLength = sizeof(address);
    QVERIFY(getsockname(server, reinterpret_cast<sockaddr*>(&address), &addressLength) == 0);
    const auto port = ntohs(address.sin_port);
    std::atomic_bool protocolValid{true};
    const auto check = [&protocolValid](bool condition) {
        if (!condition) {
            protocolValid.store(false);
        }
    };

    std::jthread mock([&] {
        std::array<std::uint8_t, 2048> packet{};
        sockaddr_in peer{};

        const auto initSize = receivePacket(server, packet, peer);
        const std::array<std::uint8_t, 5> expectedInit{2, 1, 3, 2, 0};
        check(initSize == static_cast<ssize_t>(expectedInit.size()) &&
            std::equal(expectedInit.begin(), expectedInit.end(), packet.begin()));

        std::array<std::uint8_t, 13> ack{};
        ack[12] = 0x47;
        check(sendto(
                  server, ack.data(), ack.size(), 0,
                  reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) ==
            static_cast<ssize_t>(ack.size()));

        const auto switchSize = receivePacket(server, packet, peer);
        const std::array<std::uint8_t, 26> expectedSwitch{
            0x03, 0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x1a, 0x40,
            0x40, 0x01, 0x50, 0x01, 0x6f, 0x01, 0xaa, 0x01,
            0xe0, 0x01, 0xe8, 0x01, 0xed, 0x01, 0x0d, 0x02, 0x01};
        check(switchSize == static_cast<ssize_t>(expectedSwitch.size()) &&
            std::equal(expectedSwitch.begin(), expectedSwitch.end(), packet.begin()));
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        std::memcpy(&width, packet.data() + 9, sizeof(width));
        std::memcpy(&height, packet.data() + 17, sizeof(height));
        check(width == mode.hActive && height == mode.vActive && packet[25] == 1);

        const auto blitSize = receivePacket(server, packet, peer);
        check(blitSize == 12 && packet[0] == 7 && packet[5] == 1);
        const std::array<std::uint8_t, 8> expectedFirstBlit{
            7, 1, 0, 0, 0, 1, 6, 1};
        check(std::equal(expectedFirstBlit.begin(), expectedFirstBlit.end(), packet.begin()));
        std::uint32_t compressedSize = 0;
        std::memcpy(&compressedSize, packet.data() + 8, sizeof(compressedSize));
        check(compressedSize > 0 && compressedSize < mode.payloadBytes());

        std::size_t payloadReceived = 0;
        while (payloadReceived < compressedSize) {
            const auto payloadSize = receivePacket(server, packet, peer);
            if (payloadSize <= 0) {
                protocolValid = false;
                break;
            }
            check(static_cast<std::size_t>(payloadSize) ==
                std::min<std::size_t>(1472, compressedSize - payloadReceived));
            payloadReceived += static_cast<std::size_t>(payloadSize);
        }
        check(payloadReceived == compressedSize);

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::array<std::uint8_t, 13> frameAck{};
        const std::uint32_t firstEcho = 1;
        const std::uint32_t firstDisplayed = 1;
        std::memcpy(frameAck.data(), &firstEcho, sizeof(firstEcho));
        std::memcpy(frameAck.data() + 6, &firstDisplayed, sizeof(firstDisplayed));
        frameAck[12] = 0xc7;
        check(sendto(
                  server, frameAck.data(), frameAck.size(), 0,
                  reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) ==
            static_cast<ssize_t>(frameAck.size()));

        const std::uint32_t duplicateDisplayed = 2;
        std::memcpy(frameAck.data() + 6, &duplicateDisplayed, sizeof(duplicateDisplayed));
        frameAck[12] = 0xe7;
        check(sendto(
                  server, frameAck.data(), frameAck.size(), 0,
                  reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) ==
            static_cast<ssize_t>(frameAck.size()));

        const auto audioCommandSize = receivePacket(server, packet, peer);
        const std::array<std::uint8_t, 3> expectedAudio{4, 8, 0};
        check(audioCommandSize == static_cast<ssize_t>(expectedAudio.size()) &&
            std::equal(expectedAudio.begin(), expectedAudio.end(), packet.begin()));
        std::uint16_t audioSize = 0;
        std::memcpy(&audioSize, packet.data() + 1, sizeof(audioSize));
        check(audioSize == 8);
        const auto audioPayloadSize = receivePacket(server, packet, peer);
        check(audioPayloadSize == audioSize);

        const auto incompressibleBlitSize = receivePacket(server, packet, peer);
        check(incompressibleBlitSize == 12 && packet[0] == 7 && packet[5] == 0);
        std::uint32_t randomCompressedSize = 0;
        std::memcpy(&randomCompressedSize, packet.data() + 8, sizeof(randomCompressedSize));
        check(randomCompressedSize >= mode.payloadBytes());
        payloadReceived = 0;
        while (payloadReceived < randomCompressedSize) {
            const auto payloadSize = receivePacket(server, packet, peer);
            if (payloadSize <= 0) {
                protocolValid = false;
                break;
            }
            check(static_cast<std::size_t>(payloadSize) ==
                std::min<std::size_t>(1472, randomCompressedSize - payloadReceived));
            payloadReceived += static_cast<std::size_t>(payloadSize);
        }
        check(payloadReceived == randomCompressedSize);

        frameAck.fill(0);
        const std::uint32_t secondEcho = 2;
        const std::uint32_t secondDisplayed = 2;
        std::memcpy(frameAck.data(), &secondEcho, sizeof(secondEcho));
        std::memcpy(frameAck.data() + 6, &secondDisplayed, sizeof(secondDisplayed));
        frameAck[12] = 0xef;
        check(sendto(
                  server, frameAck.data(), frameAck.size(), 0,
                  reinterpret_cast<const sockaddr*>(&peer), sizeof(peer)) ==
            static_cast<ssize_t>(frameAck.size()));
    });

    GroovyMister transport;
    std::string error;
    QVERIFY2(transport.connect("127.0.0.1", true, mode, error, port), error.c_str());
    const auto capacity = transport.prepareFrame();
    QVERIFY2(capacity.ready, capacity.warning.c_str());
    std::vector<std::uint8_t> frame(mode.payloadBytes());
    auto videoResult = transport.sendFrame(
        frame, 1, FieldParity::Field1EvenSourceLines);
    QVERIFY2(videoResult.status == VideoSubmitStatus::Sent, videoResult.error.c_str());
    transport.waitSync();
    QCOMPARE(transport.status().frameEcho, std::uint32_t{1});
    QCOMPARE(transport.status().frame, std::uint32_t{1});
    QVERIFY(!transport.status().vgaField);
    const std::array<std::int16_t, 4> audio{1, 2, 3, 4};
    QVERIFY2(transport.sendAudio(audio, error), error.c_str());
    QCOMPARE(transport.diagnostics().audioPacketsRequested, std::uint64_t{1});
    QCOMPARE(transport.diagnostics().audioPacketsSent, std::uint64_t{1});
    QCOMPARE(transport.diagnostics().audioPacketsCoreDisabled, std::uint64_t{0});

    std::uint32_t random = 0x12345678;
    for (auto& byte : frame) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        byte = static_cast<std::uint8_t>(random);
    }
    videoResult = transport.sendFrame(
        frame, 2, FieldParity::Field0OddSourceLines);
    QVERIFY2(videoResult.status == VideoSubmitStatus::Sent, videoResult.error.c_str());
    QVERIFY(videoResult.timings.transmittedBytes >= mode.payloadBytes());
    transport.waitSync();
    QCOMPARE(transport.status().frameEcho, std::uint32_t{2});
    QCOMPARE(transport.diagnostics().fpgaStatusSamples, std::uint64_t{2});
    QCOMPARE(transport.diagnostics().fpgaFrameskipSamples, std::uint64_t{1});
    QCOMPARE(transport.diagnostics().fpgaUnsyncedSamples, std::uint64_t{0});
    QCOMPARE(transport.diagnostics().fpgaQueueEmptySamples, std::uint64_t{0});
    QCOMPARE(transport.diagnostics().audioPacketsSent, std::uint64_t{1});
    const std::array<std::uint8_t, 1> invalidFrame{};
    videoResult = transport.sendFrame(
        invalidFrame, 3, FieldParity::Field0OddSourceLines);
    QCOMPARE(videoResult.status, VideoSubmitStatus::Fatal);
    QVERIFY(!videoResult.error.empty());
    transport.close();

    mock.join();
    close(server);
    QVERIFY(protocolValid.load());
}

QTEST_APPLESS_MAIN(GroovyMisterTest)

#include "GroovyMisterTest.moc"
