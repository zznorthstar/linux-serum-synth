#include "Patch.h"
#include <sstream>

namespace zyg {
std::string sourceName(int id) {
    if (id == 1) return "Mod Wheel";
    if (id >= 2 && id <= 5) return "ENV " + std::to_string(id - 1);
    if (id >= 6 && id <= 15) return "LFO " + std::to_string(id - 5);
    if (id >= 25 && id <= 32) return "Macro " + std::to_string(id - 24);
    switch (id) {
        case 16: return "Velocity"; case 17: return "Note";
        case 18: return "Channel pressure"; case 19: return "Poly pressure";
        case 20: return "Noise audio"; case 21: return "Random 1";
        case 22: return "Random 2"; case 23: return "Alternate 1";
        case 24: return "Alternate 2"; case 33: return "Pitch Bend";
        case 34: return "MPE X"; case 35: return "MPE Y"; case 36: return "MPE Z";
        case 37: return "Release Velocity"; case 38: return "Fixed";
        case 49: return "OSC A audio"; case 50: return "OSC B audio";
        case 51: return "OSC C audio"; case 52: return "SUB audio";
        case 53: return "Filter 1 audio"; case 54: return "Filter 2 audio";
        case 55: return "Active Voices"; case 56: return "Voice Mod 1";
        case 57: return "Voice Mod 2"; case 58: return "Voice Index";
        case 59: return "Discrete Random";
        default: return "Unresolved Serum source " + std::to_string(id);
    }
}

std::string statusSummary(const Patch& patch) {
    unsigned missing = 0, unsupported = 0;
    for (const auto& d : patch.diagnostics) {
        missing += d.status == "missing_asset";
        unsupported += d.status == "not_rendered" || d.status == "unknown" ||
                       d.status == "unmapped_parameter" || d.status == "not_rendered_parameter";
    }
    std::ostringstream out;
    out << patch.name << " | Serum " << patch.serumVersion << " | "
        << patch.mappedParameters << '/' << patch.explicitParameters << " explicit fields mapped"
        << " | " << missing << " missing assets | " << unsupported << " not rendered/unknown";
    return out.str();
}
}
