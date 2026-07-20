#pragma once

#include "core/Modeline.h"
#include "core/StreamTypes.h"

#include <string>

namespace mistercast {

struct StreamConfig {
    std::string host;
    bool audioEnabled{};
    Modeline modeline;
    ModeEpoch modeEpoch;
};

} // namespace mistercast
