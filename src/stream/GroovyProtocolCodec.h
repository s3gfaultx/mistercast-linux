#pragma once

#include "core/Modeline.h"
#include "core/StreamTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mistercast {

struct FpgaStatus {
    std::uint32_t frame{};
    std::uint32_t frameEcho{};
    std::uint16_t vCount{};
    std::uint16_t vCountEcho{};
    bool vramSynced{};
    bool vgaFrameskip{};
    bool vgaField{};
    bool audio{};
    bool vramQueue{};
};

namespace GroovyProtocolCodec {

inline constexpr std::size_t kStatusPacketSize = 13;

using InitCommand = std::array<std::uint8_t, 5>;
using ModeSwitchCommand = std::array<std::uint8_t, 26>;
using BlitCommand = std::array<std::uint8_t, 12>;
using AudioCommand = std::array<std::uint8_t, 3>;
using StatusPacket = std::array<std::uint8_t, kStatusPacketSize>;

[[nodiscard]] InitCommand makeInitCommand(bool audioEnabled);
[[nodiscard]] ModeSwitchCommand makeModeSwitchCommand(const Modeline& modeline);
[[nodiscard]] BlitCommand makeBlitCommand(
    std::uint32_t frameNumber,
    FieldParity field,
    std::uint16_t syncLine,
    std::uint32_t compressedSize);
[[nodiscard]] AudioCommand makeAudioCommand(std::uint16_t payloadBytes);
[[nodiscard]] FpgaStatus decodeStatus(
    std::span<const std::uint8_t, kStatusPacketSize> packet);

} // namespace GroovyProtocolCodec
} // namespace mistercast
