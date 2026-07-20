#include "core/FrameFields.h"

#include <algorithm>

namespace mistercast {

bool extractInterlacedField(
    std::span<const std::uint8_t> fullFrame,
    const Modeline& modeline,
    FieldParity field,
    std::span<std::uint8_t> destination)
{
    if (!modeline.interlaced ||
        fullFrame.size() < modeline.fullFrameBytes() ||
        destination.size() < modeline.payloadBytes()) {
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(modeline.hActive) * 3;

    // Preserve the original renderer's phase: field 0 uses odd display lines.
    const std::size_t firstRow = field == FieldParity::Field0OddSourceLines ? 1 : 0;
    
    for (std::size_t y = 0; y < modeline.payloadHeight(); ++y) {
        const auto sourceOffset = (y * 2 + firstRow) * rowBytes;
        const auto destinationOffset = y * rowBytes;
        std::copy_n(
            fullFrame.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
            rowBytes,
            destination.begin() + static_cast<std::ptrdiff_t>(destinationOffset));
    }
    
    return true;
}

} // namespace mistercast
