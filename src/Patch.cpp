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

std::string oscModeToString(OscMode mode) {
    switch (mode) {
        case OscMode::wavetable: return "wavetable"; case OscMode::sample: return "sample";
        case OscMode::multisample: return "multisample"; case OscMode::granular: return "granular";
        case OscMode::spectral: return "spectral"; case OscMode::sub: return "sub";
        case OscMode::noise: return "noise"; default: return "unknown";
    }
}
OscMode oscModeFromString(const std::string& name) {
    if (name == "wavetable") return OscMode::wavetable;
    if (name == "sample") return OscMode::sample;
    if (name == "multisample") return OscMode::multisample;
    if (name == "granular") return OscMode::granular;
    if (name == "spectral") return OscMode::spectral;
    if (name == "sub") return OscMode::sub;
    if (name == "noise") return OscMode::noise;
    return OscMode::unknown;
}
std::string routeTargetToString(RouteTarget target) {
    switch (target) {
        case RouteTarget::filter: return "filter"; case RouteTarget::main: return "main";
        case RouteTarget::direct: return "direct"; case RouteTarget::none: return "none";
        default: return "unknown";
    }
}
RouteTarget routeTargetFromString(const std::string& name) {
    if (name == "filter") return RouteTarget::filter;
    if (name == "main") return RouteTarget::main;
    if (name == "direct") return RouteTarget::direct;
    if (name == "none") return RouteTarget::none;
    return RouteTarget::unknown;
}

namespace {
Json oscillatorToJson(const Oscillator& osc) {
    return Json{
        {"enabled", osc.enabled}, {"mode", oscModeToString(osc.mode)}, {"asset", osc.asset},
        {"octave", osc.octave}, {"semitone", osc.semitone}, {"unison", osc.unison},
        {"fine", osc.fine}, {"volume", osc.volume}, {"pan", osc.pan}, {"detune", osc.detune},
        {"tablePosition", osc.tablePosition}, {"initialPhase", osc.initialPhase}, {"randomPhase", osc.randomPhase},
        {"warpOne", osc.warpOne}, {"warpTwo", osc.warpTwo},
        {"warpOneAmount", osc.warpOneAmount}, {"warpTwoAmount", osc.warpTwoAmount},
        {"frameSize", osc.frameSize},
    };
}
void oscillatorFromJson(const Json& j, Oscillator& osc) {
    if (!j.is_object()) return;
    if (j.contains("enabled")) osc.enabled = j.at("enabled").get<bool>();
    if (j.contains("mode")) osc.mode = oscModeFromString(j.at("mode").get<std::string>());
    if (j.contains("asset")) osc.asset = j.at("asset").get<std::string>();
    if (j.contains("octave")) osc.octave = j.at("octave").get<int>();
    if (j.contains("semitone")) osc.semitone = j.at("semitone").get<int>();
    if (j.contains("unison")) osc.unison = j.at("unison").get<int>();
    if (j.contains("fine")) osc.fine = j.at("fine").get<double>();
    if (j.contains("volume")) osc.volume = j.at("volume").get<double>();
    if (j.contains("pan")) osc.pan = j.at("pan").get<double>();
    if (j.contains("detune")) osc.detune = j.at("detune").get<double>();
    if (j.contains("tablePosition")) osc.tablePosition = j.at("tablePosition").get<double>();
    if (j.contains("initialPhase")) osc.initialPhase = j.at("initialPhase").get<double>();
    if (j.contains("randomPhase")) osc.randomPhase = j.at("randomPhase").get<double>();
    if (j.contains("warpOne")) osc.warpOne = j.at("warpOne").get<std::string>();
    if (j.contains("warpTwo")) osc.warpTwo = j.at("warpTwo").get<std::string>();
    if (j.contains("warpOneAmount")) osc.warpOneAmount = j.at("warpOneAmount").get<double>();
    if (j.contains("warpTwoAmount")) osc.warpTwoAmount = j.at("warpTwoAmount").get<double>();
    if (j.contains("frameSize")) osc.frameSize = j.at("frameSize").get<unsigned>();
}
Json filterToJson(const Filter& f) {
    return Json{{"enabled", f.enabled}, {"type", f.type}, {"cutoff", f.cutoff},
                {"resonance", f.resonance}, {"drive", f.drive}, {"var", f.var}, {"wet", f.wet}};
}
void filterFromJson(const Json& j, Filter& f) {
    if (!j.is_object()) return;
    if (j.contains("enabled")) f.enabled = j.at("enabled").get<bool>();
    if (j.contains("type")) f.type = j.at("type").get<std::string>();
    if (j.contains("cutoff")) f.cutoff = j.at("cutoff").get<double>();
    if (j.contains("resonance")) f.resonance = j.at("resonance").get<double>();
    if (j.contains("drive")) f.drive = j.at("drive").get<double>();
    if (j.contains("var")) f.var = j.at("var").get<double>();
    if (j.contains("wet")) f.wet = j.at("wet").get<double>();
}
Json routeToJson(const Route& r) {
    return Json{{"target", routeTargetToString(r.target)}, {"filterBalance", r.filterBalance},
                {"fxBus1Level", r.fxBus1Level}, {"fxBus2Level", r.fxBus2Level}};
}
void routeFromJson(const Json& j, Route& r) {
    if (!j.is_object()) return;
    if (j.contains("target")) r.target = routeTargetFromString(j.at("target").get<std::string>());
    if (j.contains("filterBalance")) r.filterBalance = j.at("filterBalance").get<double>();
    if (j.contains("fxBus1Level")) r.fxBus1Level = j.at("fxBus1Level").get<double>();
    if (j.contains("fxBus2Level")) r.fxBus2Level = j.at("fxBus2Level").get<double>();
}
Json envelopeToJson(const Envelope& e) {
    return Json{{"attack", e.attack}, {"hold", e.hold}, {"decay", e.decay},
                {"sustain", e.sustain}, {"release", e.release}, {"curve", e.curve}};
}
void envelopeFromJson(const Json& j, Envelope& e) {
    if (!j.is_object()) return;
    if (j.contains("attack")) e.attack = j.at("attack").get<double>();
    if (j.contains("hold")) e.hold = j.at("hold").get<double>();
    if (j.contains("decay")) e.decay = j.at("decay").get<double>();
    if (j.contains("sustain")) e.sustain = j.at("sustain").get<double>();
    if (j.contains("release")) e.release = j.at("release").get<double>();
    if (j.contains("curve") && j.at("curve").is_array() && j.at("curve").size() == 3)
        for (int i = 0; i < 3; ++i) e.curve[std::size_t(i)] = j.at("curve").at(std::size_t(i)).get<double>();
}
}

Json patchToJson(const Patch& patch) {
    Json oscillators = Json::array();
    for (const auto& osc : patch.oscillators) oscillators.push_back(oscillatorToJson(osc));
    Json filters = Json::array();
    for (const auto& f : patch.filters) filters.push_back(filterToJson(f));
    Json routes = Json::array();
    for (const auto& r : patch.routes) routes.push_back(routeToJson(r));
    Json envelopes = Json::array();
    for (const auto& e : patch.envelopes) envelopes.push_back(envelopeToJson(e));
    return Json{
        {"zygPresetFormat", 1},
        {"name", patch.name}, {"author", patch.author},
        {"masterVolume", patch.masterVolume}, {"mono", patch.mono}, {"polyphony", patch.polyphony},
        {"oscillators", oscillators}, {"filters", filters}, {"routes", routes}, {"envelopes", envelopes},
    };
}
Patch patchFromJson(const Json& json) {
    Patch patch;
    if (!json.is_object()) return patch;
    patch.name = json.value("name", std::string{"Untitled"});
    patch.author = json.value("author", std::string{});
    patch.masterVolume = json.value("masterVolume", 0.7);
    patch.mono = json.value("mono", false);
    patch.polyphony = json.value("polyphony", 16);
    if (json.contains("oscillators") && json.at("oscillators").is_array()) {
        const auto& arr = json.at("oscillators");
        for (std::size_t i = 0; i < patch.oscillators.size() && i < arr.size(); ++i)
            oscillatorFromJson(arr.at(i), patch.oscillators[i]);
    }
    if (json.contains("filters") && json.at("filters").is_array()) {
        const auto& arr = json.at("filters");
        for (std::size_t i = 0; i < patch.filters.size() && i < arr.size(); ++i)
            filterFromJson(arr.at(i), patch.filters[i]);
    }
    if (json.contains("routes") && json.at("routes").is_array()) {
        const auto& arr = json.at("routes");
        for (std::size_t i = 0; i < patch.routes.size() && i < arr.size(); ++i)
            routeFromJson(arr.at(i), patch.routes[i]);
    }
    if (json.contains("envelopes") && json.at("envelopes").is_array()) {
        const auto& arr = json.at("envelopes");
        for (std::size_t i = 0; i < patch.envelopes.size() && i < arr.size(); ++i)
            envelopeFromJson(arr.at(i), patch.envelopes[i]);
    }
    return patch;
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
