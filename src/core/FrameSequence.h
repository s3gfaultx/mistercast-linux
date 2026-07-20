#pragma once

#include "core/StreamTypes.h"

#include <cstdint>

namespace mistercast {

inline bool frameSequenceAfter(std::uint32_t candidate, std::uint32_t reference)
{
    const std::uint32_t difference = candidate - reference;
    return difference != 0 && difference < 0x8000'0000u;
}

inline std::uint32_t synchronizeFrameNumber(
    std::uint32_t nextFrame,
    std::uint32_t displayedFrame)
{
    return frameSequenceAfter(displayedFrame, nextFrame)
        ? displayedFrame + 1
        : nextFrame;
}

inline FieldParity interlacedFieldForFrame(
    std::uint32_t frameNumber,
    std::uint32_t displayedFrame,
    bool displayedField)
{
    const bool fieldOne =
        (!displayedField) ^ (((frameNumber - displayedFrame) & 1u) != 0);
    return fieldOne
        ? FieldParity::Field1EvenSourceLines
        : FieldParity::Field0OddSourceLines;
}

} // namespace mistercast
