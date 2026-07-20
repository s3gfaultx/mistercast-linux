#include "core/AudioFrameCadence.h"
#include "core/FrameFields.h"
#include "core/FrameMailbox.h"
#include "core/FrameProcessor.h"
#include "core/FrameSequence.h"
#include "core/Modeline.h"
#include "core/ModelineCatalog.h"
#include "core/StereoSampleRing.h"
#include "stream/GroovyProtocolCodec.h"
#include "stream/StreamTimingPolicy.h"

#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

using namespace mistercast;

class CoreTest final : public QObject {
    Q_OBJECT

private slots:
    void bgrIdentity();
    void rgbConversion();
    void wideSourceCropsToFourByThree();
    void quarterTurnRotations();
    void flip180();
    void resizeBgrPreservesWholeFrame();
    void audioCadenceTracksModeline();
    void audioCadenceInterlacedGoldens();
    void interlacedFieldExtraction();
    void modelineCatalogValidation();
    void bundledModelinesLoad();
    void protocolCodecGoldens();
    void streamTimingPolicyGoldens();
    void adaptiveDeliveryMarginRecoversOnPressure();
    void interlacedFrameSequence();
    void mailboxNonblockingReuse();
    void stereoRingReserveAndAlignment();
    void adaptiveAudioReserveRespondsToUnderflow();
    void stereoRingOverflow();
    void stereoRingWaitsForProducer();
};

void CoreTest::bgrIdentity()
{
    std::vector<std::uint8_t> source(kDefaultModeline.fullFrameBytes());
    for (std::size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<std::uint8_t>(i & 0xff);
    }
    std::vector<std::uint8_t> destination(kDefaultModeline.fullFrameBytes());
    const SourceFrame frame{
        source.data(), kDefaultModeline.hActive, kDefaultModeline.vActive,
        static_cast<std::int32_t>(kDefaultModeline.hActive * 3), PixelFormat::Bgr};
    QVERIFY(FrameProcessor::convertToBgr(
        frame, destination, kDefaultModeline.hActive, kDefaultModeline.vActive));
    QVERIFY(std::equal(source.begin(), source.end(), destination.begin()));
}

void CoreTest::rgbConversion()
{
    const auto framePixels = kDefaultModeline.fullFrameBytes() / 3;
    std::vector<std::uint8_t> source(framePixels * 4, 0);
    for (std::size_t i = 0; i < framePixels; ++i) {
        source[i * 4] = 10;
        source[i * 4 + 1] = 20;
        source[i * 4 + 2] = 30;
    }
    std::vector<std::uint8_t> destination(kDefaultModeline.fullFrameBytes());
    const SourceFrame frame{
        source.data(), kDefaultModeline.hActive, kDefaultModeline.vActive,
        static_cast<std::int32_t>(kDefaultModeline.hActive * 4), PixelFormat::Rgbx};
    QVERIFY(FrameProcessor::convertToBgr(
        frame, destination, kDefaultModeline.hActive, kDefaultModeline.vActive));
    QCOMPARE(destination[0], std::uint8_t{30});
    QCOMPARE(destination[1], std::uint8_t{20});
    QCOMPARE(destination[2], std::uint8_t{10});
}

void CoreTest::wideSourceCropsToFourByThree()
{
    constexpr std::uint32_t width = 640;
    constexpr std::uint32_t height = 240;
    std::vector<std::uint8_t> source(static_cast<std::size_t>(width) * height * 3, 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            source[(static_cast<std::size_t>(y) * width + x) * 3] =
                static_cast<std::uint8_t>(x / 2);
        }
    }
    std::vector<std::uint8_t> destination(kDefaultModeline.fullFrameBytes());
    const SourceFrame frame{
        source.data(), width, height, static_cast<std::int32_t>(width * 3), PixelFormat::Bgr};
    QVERIFY(FrameProcessor::convertToBgr(
        frame, destination, kDefaultModeline.hActive, kDefaultModeline.vActive));
    QCOMPARE(destination[0], std::uint8_t{80});
}

void CoreTest::flip180()
{
    std::vector<std::uint8_t> source(kDefaultModeline.fullFrameBytes(), 0);
    source[0] = 10;
    const auto lastPixel = kDefaultModeline.fullFrameBytes() - 3;
    source[lastPixel] = 20;
    std::vector<std::uint8_t> destination(kDefaultModeline.fullFrameBytes());
    const SourceFrame frame{
        source.data(), kDefaultModeline.hActive, kDefaultModeline.vActive,
        static_cast<std::int32_t>(kDefaultModeline.hActive * 3), PixelFormat::Bgr};
    CropSettings crop;
    crop.rotation = Rotation::Flip180;
    QVERIFY(FrameProcessor::convertToBgr(
        frame, destination, kDefaultModeline.hActive, kDefaultModeline.vActive, crop));
    QCOMPARE(destination[0], std::uint8_t{20});
    QCOMPARE(destination[lastPixel], std::uint8_t{10});
}

void CoreTest::quarterTurnRotations()
{
    constexpr std::uint32_t width = 3;
    constexpr std::uint32_t height = 4;
    std::array<std::uint8_t, width * height * 3> source{};
    for (std::size_t pixel = 0; pixel < width * height; ++pixel) {
        source[pixel * 3] = static_cast<std::uint8_t>(pixel);
    }
    const SourceFrame frame{
        source.data(), width, height, static_cast<std::int32_t>(width * 3), PixelFormat::Bgr};
    std::array<std::uint8_t, width * height * 3> destination{};
    CropSettings crop;
    crop.rotation = Rotation::Clockwise90;
    QVERIFY(FrameProcessor::convertToBgr(frame, destination, 4, 3, crop));
    const std::array<std::uint8_t, 12> clockwise{
        9, 6, 3, 0,
        10, 7, 4, 1,
        11, 8, 5, 2,
    };
    for (std::size_t pixel = 0; pixel < clockwise.size(); ++pixel) {
        QCOMPARE(destination[pixel * 3], clockwise[pixel]);
    }

    crop.rotation = Rotation::CounterClockwise90;
    QVERIFY(FrameProcessor::convertToBgr(frame, destination, 4, 3, crop));
    const std::array<std::uint8_t, 12> counterClockwise{
        2, 5, 8, 11,
        1, 4, 7, 10,
        0, 3, 6, 9,
    };
    for (std::size_t pixel = 0; pixel < counterClockwise.size(); ++pixel) {
        QCOMPARE(destination[pixel * 3], counterClockwise[pixel]);
    }
}

void CoreTest::resizeBgrPreservesWholeFrame()
{
    const std::array<std::uint8_t, 12> source{
        1, 2, 3, 4, 5, 6,
        7, 8, 9, 10, 11, 12,
    };
    std::array<std::uint8_t, 48> destination{};
    QVERIFY(FrameProcessor::resizeBgr(source, 2, 2, destination, 4, 4));
    QCOMPARE(destination[0], std::uint8_t{1});
    QCOMPARE(destination[9], std::uint8_t{4});
    QCOMPARE(destination[36], std::uint8_t{7});
    QCOMPARE(destination[45], std::uint8_t{10});
}

void CoreTest::audioCadenceTracksModeline()
{
    constexpr std::size_t videoFrames = 60'029;
    AudioFrameCadence cadence(kDefaultModeline);
    std::size_t audioFrames = 0;
    for (std::size_t i = 0; i < videoFrames; ++i) {
        const auto next = cadence.next();
        QVERIFY(next == 799 || next == 800);
        audioFrames += next;
    }
    const auto clockHz = static_cast<std::uint64_t>(
        std::llround(kDefaultModeline.pixelClockMHz * 1'000'000.0));
    const auto numerator = static_cast<std::uint64_t>(48'000) *
        kDefaultModeline.hTotal * kDefaultModeline.vTotal;
    const auto expected = static_cast<std::size_t>(videoFrames * numerator / clockHz);
    QCOMPARE(audioFrames, expected);
}

void CoreTest::audioCadenceInterlacedGoldens()
{
    const Modeline ntsc{
        "320x480i", 6.700, 320, 336, 367, 426, 480, 488, 493, 525, true};
    AudioFrameCadence ntscCadence(ntsc);
    const std::array<std::size_t, 8> ntscExpected{
        801, 801, 801, 801, 801, 801, 801, 802};
    for (const auto expected : ntscExpected) {
        QCOMPARE(ntscCadence.next(), expected);
    }

    const Modeline pal{
        "720x576i", 13.875, 720, 741, 806, 888, 576, 581, 586, 625, true};
    AudioFrameCadence palCadence(pal);
    for (int i = 0; i < 16; ++i) {
        QCOMPARE(palCadence.next(), std::size_t{960});
    }
}

void CoreTest::interlacedFieldExtraction()
{
    const Modeline mode{
        "test", 1.0, 4, 5, 6, 8, 4, 5, 6, 8, true};
    std::vector<std::uint8_t> frame(mode.fullFrameBytes());
    for (std::size_t row = 0; row < mode.vActive; ++row) {
        std::fill_n(frame.begin() + static_cast<std::ptrdiff_t>(row * mode.hActive * 3),
                    mode.hActive * 3,
                    static_cast<std::uint8_t>(row));
    }
    std::vector<std::uint8_t> field(mode.payloadBytes());
    QVERIFY(extractInterlacedField(
        frame, mode, FieldParity::Field0OddSourceLines, field));
    QCOMPARE(field[0], std::uint8_t{1});
    QCOMPARE(field[mode.hActive * 3], std::uint8_t{3});
    QVERIFY(extractInterlacedField(
        frame, mode, FieldParity::Field1EvenSourceLines, field));
    QCOMPARE(field[0], std::uint8_t{0});
    QCOMPARE(field[mode.hActive * 3], std::uint8_t{2});
}

void CoreTest::modelineCatalogValidation()
{
    std::istringstream input(
        "6.700 320 336 367 426 240 244 247 262 0 [320x240p]\n"
        "12.587 320 336 368 400 480 490 496 525 1 [320x480i]\n"
        "6.700 800 810 820 830 240 244 247 262 0 [too-wide]\n");
    const auto result = ModelineCatalog::parse(input);
    QCOMPARE(result.modelines.size(), std::size_t{2});
    QCOMPARE(result.modelines[0].name, std::string("320x240p"));
    QVERIFY(result.modelines[1].interlaced);
    QCOMPARE(result.warnings.size(), std::size_t{1});
}

void CoreTest::bundledModelinesLoad()
{
    const auto path = std::filesystem::path(MISTERCAST_SOURCE_DIR) /
        "resources/modelines.dat";
    const auto result = ModelineCatalog::load(path);
    QCOMPARE(result.warnings.size(), std::size_t{0});
    QCOMPARE(result.modelines.size(), std::size_t{10});
    QCOMPARE(result.modelines.front().name, std::string("256x240  NTSC (60Hz)"));
    QCOMPARE(result.modelines.back().name, std::string("720x576i PAL (50Hz)"));
}

void CoreTest::protocolCodecGoldens()
{
    QCOMPARE(
        GroovyProtocolCodec::makeInitCommand(true),
        (GroovyProtocolCodec::InitCommand{2, 1, 3, 2, 0}));
    QCOMPARE(
        GroovyProtocolCodec::makeInitCommand(false),
        (GroovyProtocolCodec::InitCommand{2, 1, 0, 0, 0}));

    const Modeline mode{
        "320x480i", 6.700, 320, 336, 367, 426, 480, 488, 493, 525, true};
    const GroovyProtocolCodec::ModeSwitchCommand expectedModeSwitch{
        0x03, 0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x1a, 0x40,
        0x40, 0x01, 0x50, 0x01, 0x6f, 0x01, 0xaa, 0x01,
        0xe0, 0x01, 0xe8, 0x01, 0xed, 0x01, 0x0d, 0x02, 0x01};
    QCOMPARE(
        GroovyProtocolCodec::makeModeSwitchCommand(mode), expectedModeSwitch);

    const GroovyProtocolCodec::BlitCommand expectedBlit{
        0x07, 0x04, 0x03, 0x02, 0x01, 0x01,
        0x60, 0x50, 0x40, 0x30, 0x20, 0x10};
    QCOMPARE(
        GroovyProtocolCodec::makeBlitCommand(
            0x01020304,
            FieldParity::Field1EvenSourceLines,
            0x5060,
            0x10203040),
        expectedBlit);
    QCOMPARE(
        GroovyProtocolCodec::makeAudioCommand(0x1234),
        (GroovyProtocolCodec::AudioCommand{0x04, 0x34, 0x12}));

    const GroovyProtocolCodec::StatusPacket packet{
        0x04, 0x03, 0x02, 0x01, 0x34, 0x12,
        0x44, 0x33, 0x22, 0x11, 0x78, 0x56, 0xec};
    const auto status = GroovyProtocolCodec::decodeStatus(packet);
    QCOMPARE(status.frameEcho, std::uint32_t{0x01020304});
    QCOMPARE(status.vCountEcho, std::uint16_t{0x1234});
    QCOMPARE(status.frame, std::uint32_t{0x11223344});
    QCOMPARE(status.vCount, std::uint16_t{0x5678});
    QVERIFY(status.vramSynced);
    QVERIFY(status.vgaFrameskip);
    QVERIFY(status.vgaField);
    QVERIFY(status.audio);
    QVERIFY(status.vramQueue);
}

void CoreTest::streamTimingPolicyGoldens()
{
    using namespace std::chrono_literals;
    const Modeline interlaced{
        "320x480i", 6.700, 320, 336, 367, 426, 480, 488, 493, 525, true};
    QCOMPARE(
        calculateDeliverySyncLine(interlaced, 100'000, 9, 10ms, 2ms),
        std::uint16_t{262});
    QCOMPARE(
        calculateDeliverySyncLine(interlaced, 100'000, 10, 10ms, 2ms),
        std::uint16_t{100});
    QCOMPARE(
        calculateDeliverySyncLine(interlaced, 100'000, 10, 100ms, 2ms),
        std::uint16_t{1});
    const FpgaStatus status{
        .frame = 100,
        .frameEcho = 101,
        .vCount = 80,
        .vCountEcho = 100,
    };
    QCOMPARE(calculatePacingLineCorrection(interlaced, status), std::int64_t{5});

    auto progressive = interlaced;
    progressive.vTotal = 262;
    progressive.interlaced = false;
    QCOMPARE(calculatePacingLineCorrection(progressive, status), std::int64_t{10});

    auto clamped = status;
    clamped.frameEcho = 200;
    QCOMPARE(
        calculatePacingLineCorrection(progressive, clamped),
        std::int64_t{65});
}

void CoreTest::adaptiveDeliveryMarginRecoversOnPressure()
{
    const Modeline mode{
        "320x480i", 6.700, 320, 336, 367, 426, 480, 488, 493, 525, true};
    AdaptiveDeliveryMargin margin(mode);
    QCOMPARE(margin.minimumLines(), std::uint16_t{262});

    const FpgaStatus healthy{
        .vramSynced = true,
        .vgaFrameskip = false,
        .vramQueue = true,
    };
    for (std::uint32_t i = 0; i < AdaptiveDeliveryMargin::kHealthySamplesPerStep; ++i) {
        margin.observe(healthy);
    }
    QCOMPARE(margin.minimumLines(), std::uint16_t{258});

    auto pressure = healthy;
    pressure.vramQueue = false;
    margin.observe(pressure);
    QCOMPARE(margin.minimumLines(), std::uint16_t{262});

    const auto conservative = calculateDeliverySyncLine(
        mode, 1'000, 10, std::chrono::milliseconds(1), std::chrono::nanoseconds(0));
    const auto adapted = calculateDeliverySyncLine(
        mode,
        1'000,
        10,
        std::chrono::milliseconds(1),
        std::chrono::nanoseconds(0),
        1'472,
        196);
    QVERIFY(adapted > conservative);

    margin.reset(mode);
    const auto steps = (std::uint16_t{262} - std::uint16_t{196} +
                        AdaptiveDeliveryMargin::kLinesPerStep - 1) /
        AdaptiveDeliveryMargin::kLinesPerStep;
    for (std::uint32_t i = 0;
         i < steps * AdaptiveDeliveryMargin::kHealthySamplesPerStep;
         ++i) {
        margin.observe(healthy);
    }
    QCOMPARE(margin.minimumLines(), std::uint16_t{196});

    pressure = healthy;
    pressure.vramSynced = false;
    margin.observe(pressure);
    QCOMPARE(margin.minimumLines(), std::uint16_t{262});

    for (std::uint32_t i = 0; i < AdaptiveDeliveryMargin::kHealthySamplesPerStep; ++i) {
        margin.observe(healthy);
    }
    pressure = healthy;
    pressure.vgaFrameskip = true;
    margin.observe(pressure);
    QCOMPARE(margin.minimumLines(), std::uint16_t{262});
}

void CoreTest::interlacedFrameSequence()
{
    QVERIFY(!frameSequenceAfter(100, 100));
    QVERIFY(frameSequenceAfter(101, 100));
    QVERIFY(!frameSequenceAfter(99, 100));
    QVERIFY(frameSequenceAfter(0, UINT32_MAX));

    QCOMPARE(synchronizeFrameNumber(100, 99), std::uint32_t{100});
    QCOMPARE(synchronizeFrameNumber(100, 100), std::uint32_t{100});
    QCOMPARE(synchronizeFrameNumber(100, 101), std::uint32_t{102});
    QCOMPARE(synchronizeFrameNumber(0, UINT32_MAX), std::uint32_t{0});
    QCOMPARE(synchronizeFrameNumber(UINT32_MAX, 0), std::uint32_t{1});

    QCOMPARE(static_cast<std::uint8_t>(interlacedFieldForFrame(100, 100, false)),
             std::uint8_t{1});
    QCOMPARE(static_cast<std::uint8_t>(interlacedFieldForFrame(101, 100, false)),
             std::uint8_t{0});
    QCOMPARE(static_cast<std::uint8_t>(interlacedFieldForFrame(102, 100, false)),
             std::uint8_t{1});
    QCOMPARE(static_cast<std::uint8_t>(interlacedFieldForFrame(100, 100, true)),
             std::uint8_t{0});
}

void CoreTest::mailboxNonblockingReuse()
{
    FrameMailbox mailbox;
    std::array<std::uint8_t, kMaximumFrameBytes> destination{};
    FrameCursor cursor;
    FrameMetadata metadata;
    const FrameSpec required{6, 2, 1, ModeEpoch{7}};
    QCOMPARE(mailbox.tryReadLatest(destination, cursor, metadata, required),
             FrameReadResult::NoChange);

    const std::array<std::uint8_t, 6> frame{1, 2, 3, 4, 5, 6};
    mailbox.publish(frame, 2, 1, ModeEpoch{7});
    QCOMPARE(mailbox.tryReadLatest(destination, cursor, metadata, required),
             FrameReadResult::Copied);
    QCOMPARE(cursor.sequence, std::uint64_t{1});
    QCOMPARE(metadata.bytes, frame.size());
    QCOMPARE(metadata.width, std::uint16_t{2});
    QCOMPARE(metadata.height, std::uint16_t{1});
    QCOMPARE(metadata.modeEpoch.value, std::uint64_t{7});
    QVERIFY(std::equal(frame.begin(), frame.end(), destination.begin()));
    QCOMPARE(mailbox.tryReadLatest(destination, cursor, metadata, required),
             FrameReadResult::NoChange);

    const auto acceptedMetadata = metadata;
    const auto acceptedBytes = destination;
    const std::array<std::uint8_t, 3> incompatible{9, 9, 9};
    mailbox.publish(incompatible, 1, 1, ModeEpoch{8});
    QCOMPARE(mailbox.tryReadLatest(destination, cursor, metadata, required),
             FrameReadResult::Incompatible);
    QCOMPARE(cursor.sequence, std::uint64_t{2});
    QCOMPARE(metadata.bytes, acceptedMetadata.bytes);
    QCOMPARE(metadata.modeEpoch.value, acceptedMetadata.modeEpoch.value);
    QVERIFY(destination == acceptedBytes);

    mailbox.invalidate();
    FrameCursor invalidatedCursor;
    FrameMetadata invalidatedMetadata;
    QCOMPARE(
        mailbox.tryReadLatest(
            destination, invalidatedCursor, invalidatedMetadata, required),
        FrameReadResult::Incompatible);
    QCOMPARE(invalidatedMetadata.bytes, std::size_t{0});

    const std::array<std::uint8_t, 6> replacement{6, 5, 4, 3, 2, 1};
    mailbox.publish(replacement, 2, 1, ModeEpoch{7});
    QCOMPARE(mailbox.tryReadLatest(destination, cursor, metadata, required),
             FrameReadResult::Copied);
    QCOMPARE(cursor.sequence, std::uint64_t{4});
    QVERIFY(std::equal(replacement.begin(), replacement.end(), destination.begin()));
}

void CoreTest::stereoRingReserveAndAlignment()
{
    StereoSampleRing ring;
    std::array<std::int16_t, 20> input{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::int16_t>(i);
    }
    ring.write(input);

    std::array<std::int16_t, 5> output{};
    QCOMPARE(ring.read(output, 2), std::size_t{4});
    QCOMPARE(output[0], std::int16_t{12});
    QCOMPARE(output[3], std::int16_t{15});
    const auto diagnostics = ring.diagnostics();
    QCOMPARE(diagnostics.staleSamplesDiscarded, std::uint64_t{12});
    QCOMPARE(diagnostics.bufferedSamples, std::size_t{4});
}

void CoreTest::stereoRingOverflow()
{
    StereoSampleRing ring;
    std::vector<std::int16_t> input(StereoSampleRing::kCapacitySamples + 2);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::int16_t>(i);
    }
    ring.write(input);
    QCOMPARE(ring.diagnostics().overflowSamples, std::uint64_t{2});

    std::array<std::int16_t, 2> output{};
    QCOMPARE(ring.read(output, StereoSampleRing::kCapacitySamples / 2), std::size_t{2});
    QCOMPARE(output[0], std::int16_t{2});
    QCOMPARE(output[1], std::int16_t{3});
}

void CoreTest::adaptiveAudioReserveRespondsToUnderflow()
{
    AdaptiveJitterReserve reserve;
    QCOMPARE(reserve.frames(), std::size_t{256});
    reserve.observeRead(false);
    QCOMPARE(reserve.frames(), std::size_t{384});
    reserve.observeRead(false);
    reserve.observeRead(false);
    QCOMPARE(reserve.frames(), std::size_t{512});

    reserve.reset();
    for (std::uint32_t i = 0; i < AdaptiveJitterReserve::kSuccessfulReadsPerStep; ++i) {
        reserve.observeRead(true);
    }
    QCOMPARE(reserve.frames(), std::size_t{128});
    for (std::uint32_t i = 0; i < AdaptiveJitterReserve::kSuccessfulReadsPerStep; ++i) {
        reserve.observeRead(true);
    }
    QCOMPARE(reserve.frames(), std::size_t{128});
}

void CoreTest::stereoRingWaitsForProducer()
{
    StereoSampleRing ring;
    const std::array<std::int16_t, 4> input{10, 20, 30, 40};
    std::jthread producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ring.write(input);
    });

    QVERIFY(ring.waitForSamples(4, std::chrono::milliseconds(100), {}));
    std::array<std::int16_t, 4> output{};
    QCOMPARE(ring.read(output, 0), output.size());
    QCOMPARE(output, input);
}

QTEST_APPLESS_MAIN(CoreTest)

#include "FrameProcessorTest.moc"
