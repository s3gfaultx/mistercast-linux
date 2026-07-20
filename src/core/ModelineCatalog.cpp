#include "core/ModelineCatalog.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace mistercast {
namespace {

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");

    return value.substr(first, last - first + 1);
}

template<typename Integer>
bool parseInteger(const std::string& text, Integer& output)
{
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto result = std::from_chars(begin, end, output);

    return result.ec == std::errc{} && result.ptr == end;
}

bool parseClock(std::string text, double& output)
{
    std::replace(text.begin(), text.end(), ',', '.');
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    stream >> output;

    return stream && stream.peek() == std::char_traits<char>::eof();
}

} // namespace

ModelineLoadResult ModelineCatalog::parse(std::istream& input)
{
    ModelineLoadResult result;
    std::unordered_set<std::string> names;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(input, line)) {
        ++lineNumber;

        const std::string original = line;
        line = trim(std::move(line));
        
        if (line.empty() || line.front() == ';') {
            continue;
        }

        const auto nameStart = line.find('[');
        const auto nameEnd = line.find(']', nameStart == std::string::npos ? 0 : nameStart + 1);

        if (nameStart == std::string::npos || nameEnd == std::string::npos || nameEnd <= nameStart + 1) {
            result.warnings.push_back(
                "Line " + std::to_string(lineNumber) + ": invalid modeline name: " + original);
                
            continue;
        }

        Modeline modeline;
        modeline.name = trim(line.substr(nameStart + 1, nameEnd - nameStart - 1));
        line.erase(nameStart, nameEnd - nameStart + 1);

        std::istringstream values(line);
        values.imbue(std::locale::classic());
        std::vector<std::string> fields;
        
        for (std::string field; values >> field;) {
            fields.push_back(std::move(field));
        }

        if (fields.size() != 10) {
            result.warnings.push_back(
                "Line " + std::to_string(lineNumber) + ": expected 10 timing values: " + original);

            continue;
        }

        std::uint16_t interlace = 0;
        if (!parseClock(fields[0], modeline.pixelClockMHz) ||
            !parseInteger(fields[1], modeline.hActive) ||
            !parseInteger(fields[2], modeline.hBegin) ||
            !parseInteger(fields[3], modeline.hEnd) ||
            !parseInteger(fields[4], modeline.hTotal) ||
            !parseInteger(fields[5], modeline.vActive) ||
            !parseInteger(fields[6], modeline.vBegin) ||
            !parseInteger(fields[7], modeline.vEnd) ||
            !parseInteger(fields[8], modeline.vTotal) ||
            !parseInteger(fields[9], interlace) || interlace > 1) {
            result.warnings.push_back(
                "Line " + std::to_string(lineNumber) + ": invalid timing value: " + original);

            continue;
        }

        modeline.interlaced = interlace != 0;

        std::string reason;
        if (!validate(modeline, reason)) {
            result.warnings.push_back(
                "Line " + std::to_string(lineNumber) + ": " + reason + ": " + original);
            continue;
        }

        if (!names.insert(modeline.name).second) {
            result.warnings.push_back(
                "Line " + std::to_string(lineNumber) + ": duplicate modeline name: " + modeline.name);
            continue;
        }

        result.modelines.push_back(std::move(modeline));
    }

    return result;
}

ModelineLoadResult ModelineCatalog::load(const std::filesystem::path& path)
{
    std::ifstream input(path);

    if (!input) {
        ModelineLoadResult result;
        result.warnings.push_back("Unable to open " + path.string());
        return result;
    }

    return parse(input);
}

bool ModelineCatalog::validate(const Modeline& modeline, std::string& reason)
{
    if (modeline.name.empty()) {
        reason = "empty modeline name";
        return false;
    }

    if (!(modeline.pixelClockMHz > 0.0)) {
        reason = "pixel clock must be positive";
        return false;
    }

    if (modeline.hActive == 0 || modeline.vActive == 0 ||
        modeline.hActive > kMaximumActiveWidth || modeline.vActive > kMaximumActiveHeight) {
        reason = "active area exceeds the 720x576 Groovy_MiSTer limit";
        return false;
    }

    if (!(modeline.hActive < modeline.hBegin && modeline.hBegin <= modeline.hEnd &&
          modeline.hEnd < modeline.hTotal)) {
        reason = "horizontal timings are not ordered";
        return false;
    }

    if (!(modeline.vActive < modeline.vBegin && modeline.vBegin <= modeline.vEnd &&
          modeline.vEnd < modeline.vTotal)) {
        reason = "vertical timings are not ordered";
        return false;
    }

    if (modeline.interlaced && (modeline.vActive % 2) != 0) {
        reason = "interlaced active height must be even";
        return false;
    }

    if (modeline.fullFrameBytes() > kMaximumFrameBytes) {
        reason = "RGB frame exceeds the Groovy_MiSTer buffer";
        return false;
    }

    const double cadence = modeline.cadenceHz();

    if (cadence < 40.0 || cadence > 120.0) {
        reason = "output cadence must be between 40 and 120 Hz";
        return false;
    }
    
    return true;
}

} // namespace mistercast
