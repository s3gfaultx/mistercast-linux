#include "core/FrameProcessor.h"

#include "core/Modeline.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace mistercast {

std::uint32_t FrameProcessor::bytesPerPixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Bgr:
    case PixelFormat::Rgb:
        return 3;
    case PixelFormat::Bgrx:
    case PixelFormat::Bgra:
    case PixelFormat::Rgbx:
    case PixelFormat::Rgba:
        return 4;
    }
    return 0;
}

bool FrameProcessor::convertToBgr(
    const SourceFrame& source,
    std::span<std::uint8_t> destination,
    std::uint16_t outputWidth,
    std::uint16_t outputHeight,
    const CropSettings& crop)
{
    const auto bpp = bytesPerPixel(source.format);
    const std::size_t outputBytes = static_cast<std::size_t>(outputWidth) * outputHeight * 3;
    
    if (source.data == nullptr || source.width == 0 || source.height == 0 || bpp == 0 ||
        outputWidth == 0 || outputHeight == 0 || destination.size() < outputBytes) {
        return false;
    }

    if (outputWidth > kMaximumActiveWidth || outputHeight > kMaximumActiveHeight) {
        return false;
    }

    const std::int64_t minimumStride = static_cast<std::int64_t>(source.width) * bpp;
    const std::int64_t stride = source.stride == 0 ? minimumStride : source.stride;
    const std::int64_t absoluteStride = stride < 0 ? -stride : stride;

    if (absoluteStride < minimumStride) {
        return false;
    }

    const bool quarterTurn = crop.rotation == Rotation::Clockwise90 ||
        crop.rotation == Rotation::CounterClockwise90;
    const std::uint32_t aspectWidth = quarterTurn ? 3 : 4;
    const std::uint32_t aspectHeight = quarterTurn ? 4 : 3;

    std::uint32_t cropWidth = source.width;
    std::uint32_t cropHeight = source.height;

    if (static_cast<std::uint64_t>(source.width) * aspectHeight >
        static_cast<std::uint64_t>(source.height) * aspectWidth) {
        cropWidth = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(source.height) * aspectWidth / aspectHeight);
    } else {
        cropHeight = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(source.width) * aspectHeight / aspectWidth);
    }

    const auto horizontalSpace = static_cast<std::int32_t>(source.width - cropWidth);
    const auto verticalSpace = static_cast<std::int32_t>(source.height - cropHeight);

    std::int32_t cropX = 0;
    
    switch (crop.horizontal) {
    case HorizontalAlignment::Left:
        break;
    case HorizontalAlignment::Center:
        cropX = horizontalSpace / 2;
        break;
    case HorizontalAlignment::Right:
        cropX = horizontalSpace;
        break;
    }

    std::int32_t cropY = 0;
    
    switch (crop.vertical) {
    case VerticalAlignment::Top:
        break;
    case VerticalAlignment::Center:
        cropY = verticalSpace / 2;
        break;
    case VerticalAlignment::Bottom:
        cropY = verticalSpace;
        break;
    }

    cropX = std::clamp(cropX + crop.offsetX, 0, horizontalSpace);
    cropY = std::clamp(cropY + crop.offsetY, 0, verticalSpace);

    const bool sourceIsRgb = source.format == PixelFormat::Rgb ||
        source.format == PixelFormat::Rgbx || source.format == PixelFormat::Rgba;

    if (crop.rotation == Rotation::None && source.format == PixelFormat::Bgr &&
        cropWidth == outputWidth && cropHeight == outputHeight) {
        const auto rowBytes = static_cast<std::size_t>(outputWidth) * 3;
        for (std::uint32_t y = 0; y < outputHeight; ++y) {
            const auto* sourceRow = source.data +
                static_cast<std::int64_t>(cropY + static_cast<std::int32_t>(y)) * stride +
                static_cast<std::size_t>(cropX) * 3;
            auto* destinationRow = destination.data() + static_cast<std::size_t>(y) * rowBytes;
            std::copy_n(sourceRow, rowBytes, destinationRow);
        }
        return true;
    }

    std::array<std::uint32_t, kMaximumActiveWidth> sourceCoordinateByX{};
    std::array<std::uint32_t, kMaximumActiveHeight> sourceCoordinateByY{};

    for (std::uint32_t x = 0; x < outputWidth; ++x) {
        switch (crop.rotation) {
        case Rotation::None:
            sourceCoordinateByX[x] = cropX + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(x) * cropWidth / outputWidth);
            break;
        case Rotation::Clockwise90:
            sourceCoordinateByX[x] = cropY + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(outputWidth - x - 1) * cropHeight /
                outputWidth);
            break;
        case Rotation::CounterClockwise90:
            sourceCoordinateByX[x] = cropY + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(x) * cropHeight / outputWidth);
            break;
        case Rotation::Flip180:
            sourceCoordinateByX[x] = cropX + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(outputWidth - x - 1) * cropWidth /
                outputWidth);
            break;
        }
    }

    for (std::uint32_t y = 0; y < outputHeight; ++y) {
        switch (crop.rotation) {
        case Rotation::None:
            sourceCoordinateByY[y] = cropY + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) * cropHeight / outputHeight);
            break;
        case Rotation::Clockwise90:
            sourceCoordinateByY[y] = cropX + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) * cropWidth / outputHeight);
            break;
        case Rotation::CounterClockwise90:
            sourceCoordinateByY[y] = cropX + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(outputHeight - y - 1) * cropWidth /
                outputHeight);
            break;
        case Rotation::Flip180:
            sourceCoordinateByY[y] = cropY + static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(outputHeight - y - 1) * cropHeight /
                outputHeight);
            break;
        }
    }

    for (std::uint32_t y = 0; y < outputHeight; ++y) {
        auto* destinationRow = destination.data() +
            static_cast<std::size_t>(y) * outputWidth * 3;

        for (std::uint32_t x = 0; x < outputWidth; ++x) {
            const bool quarterTurn = crop.rotation == Rotation::Clockwise90 ||
                crop.rotation == Rotation::CounterClockwise90;
            const auto sourceX = quarterTurn
                ? sourceCoordinateByY[y]
                : sourceCoordinateByX[x];
            const auto sourceY = quarterTurn
                ? sourceCoordinateByX[x]
                : sourceCoordinateByY[y];
            const auto* sourceRow = source.data + static_cast<std::int64_t>(sourceY) * stride;
            const auto* pixel = sourceRow + static_cast<std::size_t>(sourceX) * bpp;
            auto* output = destinationRow + static_cast<std::size_t>(x) * 3;

            if (sourceIsRgb) {
                output[0] = pixel[2];
                output[1] = pixel[1];
                output[2] = pixel[0];
            } else {
                output[0] = pixel[0];
                output[1] = pixel[1];
                output[2] = pixel[2];
            }
        }
    }

    return true;
}

bool FrameProcessor::resizeBgr(
    std::span<const std::uint8_t> source,
    std::uint16_t sourceWidth,
    std::uint16_t sourceHeight,
    std::span<std::uint8_t> destination,
    std::uint16_t outputWidth,
    std::uint16_t outputHeight)
{
    const auto sourceBytes =
        static_cast<std::size_t>(sourceWidth) * sourceHeight * 3;
    const auto outputBytes =
        static_cast<std::size_t>(outputWidth) * outputHeight * 3;
        
    if (sourceWidth == 0 || sourceHeight == 0 || outputWidth == 0 ||
        outputHeight == 0 || source.size() < sourceBytes ||
        destination.size() < outputBytes) {
        return false;
    }

    for (std::uint32_t y = 0; y < outputHeight; ++y) {
        const auto sourceY = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(y) * sourceHeight / outputHeight);

        for (std::uint32_t x = 0; x < outputWidth; ++x) {
            const auto sourceX = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(x) * sourceWidth / outputWidth);
            const auto sourceOffset =
                (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 3;
            const auto destinationOffset =
                (static_cast<std::size_t>(y) * outputWidth + x) * 3;

            std::copy_n(
                source.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
                3,
                destination.begin() +
                    static_cast<std::ptrdiff_t>(destinationOffset));
        }
    }
    return true;
}

} // namespace mistercast
