#pragma once

#include "core/Modeline.h"

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace mistercast {

struct ModelineLoadResult {
    std::vector<Modeline> modelines;
    std::vector<std::string> warnings;
};

class ModelineCatalog {
public:
    static ModelineLoadResult parse(std::istream& input);
    static ModelineLoadResult load(const std::filesystem::path& path);
    static bool validate(const Modeline& modeline, std::string& reason);
};

} // namespace mistercast
