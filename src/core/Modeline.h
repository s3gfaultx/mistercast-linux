#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mistercast {

inline constexpr std::uint16_t kMaximumActiveWidth = 720;
inline constexpr std::uint16_t kMaximumActiveHeight = 576;
inline constexpr std::size_t kMaximumFrameBytes =
    static_cast<std::size_t>(kMaximumActiveWidth) * kMaximumActiveHeight * 3;

struct Modeline {
    std::string name;
    double pixelClockMHz{};
    std::uint16_t hActive{};
    std::uint16_t hBegin{};
    std::uint16_t hEnd{};
    std::uint16_t hTotal{};
    std::uint16_t vActive{};
    std::uint16_t vBegin{};
    std::uint16_t vEnd{};
    std::uint16_t vTotal{};
    bool interlaced{};

    [[nodiscard]] std::uint16_t payloadHeight() const
    {
        return interlaced ? vActive / 2 : vActive;
    }

    [[nodiscard]] std::size_t fullFrameBytes() const
    {
        return static_cast<std::size_t>(hActive) * vActive * 3;
    }

    [[nodiscard]] std::size_t payloadBytes() const
    {
        return static_cast<std::size_t>(hActive) * payloadHeight() * 3;
    }

    [[nodiscard]] double cadenceHz() const
    {
        if (hTotal == 0 || vTotal == 0) {
            return 0.0;
        }
        const double base = pixelClockMHz * 1'000'000.0 /
            (static_cast<double>(hTotal) * vTotal);
        return interlaced ? base * 2.0 : base;
    }
};

inline const Modeline kDefaultModeline{
    "320x240 NTSC (60Hz)",
    6.700,
    320,
    336,
    367,
    426,
    240,
    244,
    247,
    262,
    false,
};

} // namespace mistercast
