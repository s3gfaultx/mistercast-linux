#include "stream/GroovyProtocolCodec.h"

#include <cstring>
#include <type_traits>

namespace mistercast::GroovyProtocolCodec {
namespace {

template <typename Value, std::size_t Size>
void write(std::array<std::uint8_t, Size>& packet, std::size_t offset, Value value)
{
    static_assert(std::is_trivially_copyable_v<Value>);
    std::memcpy(packet.data() + offset, &value, sizeof(value));
}

template <typename Value>
[[nodiscard]] Value read(const std::uint8_t* data)
{
    static_assert(std::is_trivially_copyable_v<Value>);
    Value value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

} // namespace

InitCommand makeInitCommand(bool audioEnabled)
{
    return {
        2,
        1,
        static_cast<std::uint8_t>(audioEnabled ? 3 : 0),
        static_cast<std::uint8_t>(audioEnabled ? 2 : 0),
        0,
    };
}

ModeSwitchCommand makeModeSwitchCommand(const Modeline& modeline)
{
    ModeSwitchCommand packet{};
    packet[0] = 3;

    write(packet, 1, modeline.pixelClockMHz);
    write(packet, 9, modeline.hActive);
    write(packet, 11, modeline.hBegin);
    write(packet, 13, modeline.hEnd);
    write(packet, 15, modeline.hTotal);
    write(packet, 17, modeline.vActive);
    write(packet, 19, modeline.vBegin);
    write(packet, 21, modeline.vEnd);
    write(packet, 23, modeline.vTotal);

    packet[25] = modeline.interlaced ? 1 : 0;

    return packet;
}

BlitCommand makeBlitCommand(
    std::uint32_t frameNumber,
    FieldParity field,
    std::uint16_t syncLine,
    std::uint32_t compressedSize)
{
    BlitCommand packet{};
    packet[0] = 7;

    write(packet, 1, frameNumber);

    packet[5] = static_cast<std::uint8_t>(field);

    write(packet, 6, syncLine);
    write(packet, 8, compressedSize);

    return packet;
}

AudioCommand makeAudioCommand(std::uint16_t payloadBytes)
{
    AudioCommand packet{};
    packet[0] = 4;
    
    write(packet, 1, payloadBytes);

    return packet;
}

FpgaStatus decodeStatus(
    std::span<const std::uint8_t, kStatusPacketSize> packet)
{
    FpgaStatus status;

    status.frameEcho = read<std::uint32_t>(packet.data());
    status.vCountEcho = read<std::uint16_t>(packet.data() + 4);
    status.frame = read<std::uint32_t>(packet.data() + 6);
    status.vCount = read<std::uint16_t>(packet.data() + 10);

    const auto bits = packet[12];

    status.vramSynced = (bits & (1u << 2)) != 0;
    status.vgaFrameskip = (bits & (1u << 3)) != 0;
    status.vgaField = (bits & (1u << 5)) != 0;
    status.audio = (bits & (1u << 6)) != 0;
    status.vramQueue = (bits & (1u << 7)) != 0;
    
    return status;
}

} // namespace mistercast::GroovyProtocolCodec
