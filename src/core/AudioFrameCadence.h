#pragma once

#include "core/Modeline.h"

#include <cstddef>

namespace mistercast {

class AudioFrameCadence {
public:
    explicit AudioFrameCadence(const Modeline& modeline)
        : audioFramesPerOutput_(48'000.0 / modeline.cadenceHz())
    {
    }

    std::size_t next()
    {
        accumulator_ += audioFramesPerOutput_;
        const auto frames = static_cast<std::size_t>(accumulator_);
        accumulator_ -= static_cast<double>(frames);
        return frames;
    }

private:
    double audioFramesPerOutput_{};
    double accumulator_{};
};

} // namespace mistercast
