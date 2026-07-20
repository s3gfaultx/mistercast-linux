#include "core/AudioFrameCadence.h"
#include "core/Modeline.h"
#include "stream/GroovyMister.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <span>
#include <string>

namespace {

std::atomic_bool stopRequested{};

void requestStop(int)
{
    stopRequested.store(true, std::memory_order_relaxed);
}

std::array<std::uint8_t, mistercast::kMaximumFrameBytes> makeLatencyPattern(std::uint32_t frame)
{
    std::array<std::uint8_t, mistercast::kMaximumFrameBytes> output{};
    constexpr std::array<std::array<std::uint8_t, 3>, 8> bars{{
        {255, 255, 255}, {0, 255, 255}, {255, 255, 0}, {0, 255, 0},
        {255, 0, 255}, {0, 0, 255}, {255, 0, 0}, {0, 0, 0},
    }};

    for (std::uint32_t y = 0; y < mistercast::kDefaultModeline.vActive; ++y) {
        for (std::uint32_t x = 0; x < mistercast::kDefaultModeline.hActive; ++x) {
            const auto index = (static_cast<std::size_t>(y) * mistercast::kDefaultModeline.hActive + x) * 3;
            const auto& color = bars[std::min<std::size_t>(x / 40, bars.size() - 1)];
            output[index] = color[0];
            output[index + 1] = color[1];
            output[index + 2] = color[2];
        }
    }

    const bool flash = (frame & 1u) != 0;
    for (std::uint32_t y = 8; y < 72; ++y) {
        for (std::uint32_t x = 8; x < 72; ++x) {
            const auto index = (static_cast<std::size_t>(y) * mistercast::kDefaultModeline.hActive + x) * 3;
            output[index] = flash ? 255 : 0;
            output[index + 1] = flash ? 255 : 0;
            output[index + 2] = flash ? 255 : 0;
        }
    }
    return output;
}

int runPattern(const std::string& host, bool tone)
{
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    mistercast::GroovyMister transport;
    std::string error;
    if (!transport.connect(host, tone, mistercast::kDefaultModeline, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout << "Streaming 320x240p60 latency pattern to " << host
              << ". Press Ctrl+C to stop.\n";

    std::uint32_t frameNumber = 1;
    std::chrono::nanoseconds totalCompression{};
    std::chrono::nanoseconds totalSubmission{};
    std::uint64_t totalBytes = 0;
    std::uint32_t statisticFrames = 0;
    std::uint32_t droppedFrames = 0;
    mistercast::AudioFrameCadence audioCadence(mistercast::kDefaultModeline);
    std::array<std::int16_t, 1'600> audio{};
    double tonePhase = 0.0;

    while (!stopRequested.load(std::memory_order_relaxed)) {
        std::size_t audioFrames = 0;
        
        if (tone) {
            audioFrames = audioCadence.next();
            
            for (std::size_t i = 0; i < audioFrames; ++i) {
                const auto sample = static_cast<std::int16_t>(std::sin(tonePhase) * 8'000.0);

                audio[i * 2] = sample;
                audio[i * 2 + 1] = sample;

                tonePhase += 2.0 * std::numbers::pi * 440.0 / 48'000.0;
                
                if (tonePhase >= 2.0 * std::numbers::pi) {
                    tonePhase -= 2.0 * std::numbers::pi;
                }
            }
        }

        const auto capacity = transport.prepareFrame();
        if (!capacity.ready) {
            ++droppedFrames;
            if (tone && !transport.sendAudio(
                    std::span(audio).first(audioFrames * 2), error)) {
                std::cerr << error << '\n';
                return 1;
            }
            transport.waitSync();
            ++frameNumber;
            continue;
        }

        auto frame = makeLatencyPattern(frameNumber);

        const auto videoResult = transport.sendFrame(
                std::span(frame).first(mistercast::kDefaultModeline.payloadBytes()),
                frameNumber,
                mistercast::FieldParity::Field0OddSourceLines);

        if (videoResult.status == mistercast::VideoSubmitStatus::Fatal) {
            std::cerr << videoResult.error << '\n';
            return 1;
        }

        if (tone && !transport.sendAudio(
                std::span(audio).first(audioFrames * 2), error)) {
            std::cerr << error << '\n';
            return 1;
        }

        if (videoResult.status == mistercast::VideoSubmitStatus::DroppedBeforeCommand) {
            ++droppedFrames;
            transport.waitSync();
            ++frameNumber;
            continue;
        }

        totalCompression += videoResult.timings.compression;
        totalSubmission += videoResult.timings.socketSubmission;
        totalBytes += videoResult.timings.transmittedBytes;
        ++statisticFrames;

        transport.waitSync();

        if (statisticFrames == 120) {
            const auto compressionUs =
                std::chrono::duration<double, std::micro>(totalCompression).count() / 120.0;
            const auto submissionUs =
                std::chrono::duration<double, std::micro>(totalSubmission).count() / 120.0;
            
                std::cout << "frame=" << frameNumber
                      << " avg_compress_us=" << compressionUs
                      << " avg_submit_us=" << submissionUs
                      << " avg_payload_bytes=" << totalBytes / 120
                      << " drops=" << droppedFrames;

            if (tone) {
                std::cout << " core_audio=" << (transport.status().audio ? "on" : "off");
            }

            std::cout << '\n';

            totalCompression = {};
            totalSubmission = {};
            totalBytes = 0;
            statisticFrames = 0;
            droppedFrames = 0;
        }

        ++frameNumber;
    }

    transport.close();
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("MiSTerCast");
    QApplication::setOrganizationName("MiSTerCast");

    QCommandLineParser parser;
    parser.setApplicationDescription("Low-latency Wayland desktop streaming to Groovy_MiSTer");
    parser.addHelpOption();
    QCommandLineOption patternOption(
        QStringList{"p", "pattern"},
        "Stream a generated 320x240p60 latency pattern to HOST.",
        "HOST");
    parser.addOption(patternOption);
    QCommandLineOption toneOption(
        QStringList{"t", "tone"},
        "Stream the latency pattern and a generated 440 Hz tone to HOST.",
        "HOST");
    parser.addOption(toneOption);
    parser.process(application);

    if (parser.isSet(patternOption)) {
        return runPattern(parser.value(patternOption).toStdString(), false);
    }
    if (parser.isSet(toneOption)) {
        return runPattern(parser.value(toneOption).toStdString(), true);
    }

    mistercast::MainWindow window;
    window.show();
    return application.exec();
}
