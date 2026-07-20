#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mistercast {

enum class PixelFormat {
    Bgr,
    Bgrx,
    Bgra,
    Rgb,
    Rgbx,
    Rgba,
};

struct SourceFrame {
    const std::uint8_t* data{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t stride{};
    PixelFormat format{PixelFormat::Bgrx};
};

enum class HorizontalAlignment {
    Left,
    Center,
    Right,
};

enum class VerticalAlignment {
    Top,
    Center,
    Bottom,
};

enum class Rotation {
    None,
    Clockwise90,
    CounterClockwise90,
    Flip180,
};

struct CropSettings {
    HorizontalAlignment horizontal{HorizontalAlignment::Center};
    VerticalAlignment vertical{VerticalAlignment::Center};
    std::int32_t offsetX{};
    std::int32_t offsetY{};
    Rotation rotation{Rotation::None};
};

class FrameProcessor {
public:
    static bool convertToBgr(
        const SourceFrame& source,
        std::span<std::uint8_t> destination,
        std::uint16_t outputWidth,
        std::uint16_t outputHeight,
        const CropSettings& crop = {});
    static bool resizeBgr(
        std::span<const std::uint8_t> source,
        std::uint16_t sourceWidth,
        std::uint16_t sourceHeight,
        std::span<std::uint8_t> destination,
        std::uint16_t outputWidth,
        std::uint16_t outputHeight);

private:
    static std::uint32_t bytesPerPixel(PixelFormat format);
};

} // namespace mistercast
