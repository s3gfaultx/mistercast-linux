#pragma once

#include <cstddef>
#include <cstdint>

namespace mistercast {

struct ModeEpoch {
    std::uint64_t value{};

    friend bool operator==(ModeEpoch, ModeEpoch) = default;
};

struct FrameCursor {
    std::uint64_t sequence{};
};

struct FrameSpec {
    std::size_t bytes{};
    std::uint16_t width{};
    std::uint16_t height{};
    ModeEpoch modeEpoch{};
};

enum class FieldParity : std::uint8_t {
    Field0OddSourceLines = 0,
    Field1EvenSourceLines = 1,
};

} // namespace mistercast
