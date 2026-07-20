#pragma once

#include "core/Modeline.h"
#include "core/StreamTypes.h"

#include <cstdint>
#include <span>

namespace mistercast {

bool extractInterlacedField(
    std::span<const std::uint8_t> fullFrame,
    const Modeline& modeline,
    FieldParity field,
    std::span<std::uint8_t> destination);

} // namespace mistercast
