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
const char* lfoShapeName(LfoShape shape) {
    switch (shape) {
        case LfoShape::sine: return "sine";
        case LfoShape::lorenz: return "lorenz";
        case LfoShape::rossler: return "rossler";
        case LfoShape::randomHold: return "randomHold";
        default: return "unknown";
    }
}
LfoShape lfoShapeFromName(const std::string& name) {
    if (name == "sine") return LfoShape::sine;
    if (name == "lorenz") return LfoShape::lorenz;
    if (name == "rossler") return LfoShape::rossler;
    if (name == "randomHold") return LfoShape::randomHold;
    return LfoShape::unknown;
}
Json oscillatorToJson(const Oscillator& osc) {
    return Json{
        {"enabled", osc.enabled}, {"mode", oscModeToString(osc.mode)}, {"modeId", osc.modeId},
        {"asset", osc.asset}, {"userSelectedAsset", osc.userSelectedAsset},
        {"octave", osc.octave}, {"semitone", osc.semitone}, {"unison", osc.unison},
        {"fine", osc.fine}, {"volume", osc.volume}, {"pan", osc.pan}, {"detune", osc.detune},
        {"tablePosition", osc.tablePosition}, {"initialPhase", osc.initialPhase}, {"randomPhase", osc.randomPhase},
        {"warpOne", osc.warpOne}, {"warpTwo", osc.warpTwo},
        {"warpOneAmount", osc.warpOneAmount}, {"warpTwoAmount", osc.warpTwoAmount},
        {"frameSize", osc.frameSize}, {"sampleRate", osc.sampleRate},
        {"modeState", osc.modeState}, {"additional", osc.additional},
    };
}
void oscillatorFromJson(const Json& j, Oscillator& osc) {
    if (!j.is_object()) return;
    if (j.contains("enabled")) osc.enabled = j.at("enabled").get<bool>();
    if (j.contains("mode")) osc.mode = oscModeFromString(j.at("mode").get<std::string>());
    osc.modeId = j.value("modeId", std::string{});
    if (j.contains("asset")) osc.asset = j.at("asset").get<std::string>();
    osc.userSelectedAsset = j.value("userSelectedAsset", false);
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
    if (j.contains("sampleRate")) osc.sampleRate = j.at("sampleRate").get<double>();
    osc.modeState = j.value("modeState", Json{});
    osc.additional = j.value("additional", Json{});
}
Json filterToJson(const Filter& f) {
    return Json{{"enabled", f.enabled}, {"type", f.type}, {"cutoff", f.cutoff},
                {"resonance", f.resonance}, {"drive", f.drive}, {"var", f.var}, {"wet", f.wet}, {"additional", f.additional}};
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
    f.additional = j.value("additional", Json{});
}
Json routeToJson(const Route& r) {
    return Json{{"target", routeTargetToString(r.target)}, {"filterBalance", r.filterBalance},
                {"fxBus1Level", r.fxBus1Level}, {"fxBus2Level", r.fxBus2Level}, {"additional", r.additional}};
}
void routeFromJson(const Json& j, Route& r) {
    if (!j.is_object()) return;
    if (j.contains("target")) r.target = routeTargetFromString(j.at("target").get<std::string>());
    if (j.contains("filterBalance")) r.filterBalance = j.at("filterBalance").get<double>();
    if (j.contains("fxBus1Level")) r.fxBus1Level = j.at("fxBus1Level").get<double>();
    if (j.contains("fxBus2Level")) r.fxBus2Level = j.at("fxBus2Level").get<double>();
    r.additional = j.value("additional", Json{});
}
Json envelopeToJson(const Envelope& e) {
    return Json{{"attack", e.attack}, {"hold", e.hold}, {"decay", e.decay},
                {"sustain", e.sustain}, {"release", e.release}, {"curve", e.curve}, {"additional", e.additional}};
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
    e.additional = j.value("additional", Json{});
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
    Json modulation = Json::array();
    for (const auto& m : patch.modulation) modulation.push_back({
        {"slot", m.slot}, {"source", m.source}, {"auxiliary", m.auxiliary},
        {"sourceKind", m.sourceKind == ModSource::lfo ? "lfo" : m.sourceKind == ModSource::macro ? "macro" : "unknown"},
        {"sourceIndex", m.sourceIndex},
        {"targetKind", m.targetKind == ModTarget::wavetablePosition ? "wavetablePosition" :
            m.targetKind == ModTarget::filterCutoff ? "filterCutoff" : "unknown"},
        {"targetIndex", m.targetIndex},
        {"sourceName", m.sourceName}, {"destinationModule", m.destinationModule},
        {"destinationInstance", m.destinationInstance}, {"destinationParameter", m.destinationParameter},
        {"destinationParameterId", m.destinationParameterId}, {"amount", m.amount},
        {"bipolar", m.bipolar}, {"bypass", m.bypass}, {"additional", m.additional}});
    Json fx = Json::array();
    for (const auto& m : patch.fx) fx.push_back({{"type", m.type}, {"rack", m.rack},
        {"position", m.position}, {"enabled", m.enabled}, {"parameters", m.parameters}, {"additional", m.additional}});
    Json diagnostics = Json::array();
    for (const auto& d : patch.diagnostics)
        diagnostics.push_back({{"path", d.path}, {"status", d.status}, {"detail", d.detail}});
    Json lfoDefinitions = Json::array();
    for (const auto& lfo : patch.lfoDefinitions)
        lfoDefinitions.push_back({{"shape", lfoShapeName(lfo.shape)}, {"rateHz", lfo.rateHz},
                                  {"tempoSync", lfo.tempoSync}});
    return Json{
        {"zygPresetFormat", 2},
        {"name", patch.name}, {"author", patch.author},
        {"serumVersion", patch.serumVersion}, {"sourcePath", patch.sourcePath}, {"assetRoot", patch.assetRoot},
        {"masterVolume", patch.masterVolume}, {"mono", patch.mono}, {"polyphony", patch.polyphony},
        {"lfoOneRateHz", patch.lfoOneRateHz}, {"lfoOneSine", patch.lfoOneSine},
        {"oscillators", oscillators}, {"filters", filters}, {"routes", routes}, {"envelopes", envelopes},
        {"modulation", modulation}, {"fx", fx}, {"lfos", patch.lfos},
        {"lfoDefinitions", lfoDefinitions}, {"macros", patch.macros},
        {"macroValues", patch.macroValues},
        {"arp", patch.arp}, {"clips", patch.clips}, {"global", patch.global},
        {"arpClips", patch.arpClips}, {"midiClips", patch.midiClips},
        {"unknownSerumState", patch.unknownSerumState}, {"originalPreset", patch.originalPreset},
        {"diagnostics", diagnostics}, {"typedParameterPaths", patch.typedParameterPaths},
        {"explicitParameters", patch.explicitParameters}, {"mappedParameters", patch.mappedParameters},
    };
}
Patch patchFromJson(const Json& json) {
    Patch patch;
    if (!json.is_object()) return patch;
    patch.name = json.value("name", std::string{"Untitled"});
    patch.author = json.value("author", std::string{});
    patch.serumVersion = json.value("serumVersion", std::string{});
    patch.sourcePath = json.value("sourcePath", std::string{});
    patch.assetRoot = json.value("assetRoot", std::string{});
    patch.masterVolume = json.value("masterVolume", 0.7);
    patch.mono = json.value("mono", false);
    patch.polyphony = json.value("polyphony", 16);
    patch.lfoOneRateHz = json.value("lfoOneRateHz", 1.0);
    patch.lfoOneSine = json.value("lfoOneSine", true);
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
    if (json.contains("modulation") && json.at("modulation").is_array())
        for (const auto& j : json.at("modulation")) {
            ModulationRoute m;
            m.slot = j.value("slot", 0); m.source = j.value("source", 0); m.auxiliary = j.value("auxiliary", 0);
            const auto sourceKind = j.value("sourceKind", std::string{"unknown"});
            m.sourceKind = sourceKind == "lfo" ? ModSource::lfo : sourceKind == "macro" ? ModSource::macro : ModSource::unknown;
            m.sourceIndex = j.value("sourceIndex", 0);
            const auto targetKind = j.value("targetKind", std::string{"unknown"});
            m.targetKind = targetKind == "wavetablePosition" ? ModTarget::wavetablePosition :
                targetKind == "filterCutoff" ? ModTarget::filterCutoff : ModTarget::unknown;
            m.targetIndex = j.value("targetIndex", 0);
            m.sourceName = j.value("sourceName", std::string{});
            m.destinationModule = j.value("destinationModule", std::string{});
            m.destinationInstance = j.value("destinationInstance", 0);
            m.destinationParameter = j.value("destinationParameter", std::string{});
            m.destinationParameterId = j.value("destinationParameterId", -1);
            m.amount = j.value("amount", 0.0); m.bipolar = j.value("bipolar", false);
            m.bypass = j.value("bypass", false); m.additional = j.value("additional", Json{});
            patch.modulation.push_back(std::move(m));
        }
    if (json.contains("fx") && json.at("fx").is_array())
        for (const auto& j : json.at("fx")) {
            FxModule m;
            m.type = j.value("type", std::string{}); m.rack = j.value("rack", 0);
            m.position = j.value("position", 0); m.enabled = j.value("enabled", true);
            m.parameters = j.value("parameters", Json{}); m.additional = j.value("additional", Json{});
            patch.fx.push_back(std::move(m));
        }
    auto readArray = [&](const char* key, auto& dest) {
        if (json.contains(key) && json.at(key).is_array())
            for (std::size_t i = 0; i < dest.size() && i < json.at(key).size(); ++i) dest[i] = json.at(key)[i];
    };
    readArray("lfos", patch.lfos); readArray("macros", patch.macros);
    if (json.contains("lfoDefinitions") && json.at("lfoDefinitions").is_array()) {
        const auto& definitions = json.at("lfoDefinitions");
        for (std::size_t i = 0; i < patch.lfoDefinitions.size() && i < definitions.size(); ++i) {
            patch.lfoDefinitions[i].shape = lfoShapeFromName(definitions[i].value("shape", std::string{"unknown"}));
            patch.lfoDefinitions[i].rateHz = definitions[i].value("rateHz", 1.0);
            patch.lfoDefinitions[i].tempoSync = definitions[i].value("tempoSync", false);
        }
    }
    if (json.contains("macroValues") && json.at("macroValues").is_array())
        for (std::size_t i = 0; i < patch.macroValues.size() && i < json.at("macroValues").size(); ++i)
            patch.macroValues[i] = json.at("macroValues")[i].get<double>();
    readArray("arpClips", patch.arpClips); readArray("midiClips", patch.midiClips);
    patch.arp = json.value("arp", Json{}); patch.clips = json.value("clips", Json{});
    patch.global = json.value("global", Json{});
    patch.unknownSerumState = json.value("unknownSerumState", Json{});
    patch.originalPreset = json.value("originalPreset", std::vector<std::uint8_t>{});
    if (json.contains("diagnostics") && json.at("diagnostics").is_array())
        for (const auto& j : json.at("diagnostics")) patch.diagnostics.push_back({
            j.value("path", std::string{}), j.value("status", std::string{}), j.value("detail", std::string{})});
    patch.typedParameterPaths = json.value("typedParameterPaths", std::vector<std::string>{});
    patch.explicitParameters = json.value("explicitParameters", 0u);
    patch.mappedParameters = json.value("mappedParameters", 0u);
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
    out << patch.name;
    if (patch.serumVersion.empty() && patch.originalPreset.empty()) out << " | ZYG native";
    else out << " | Serum " << patch.serumVersion << " | "
        << patch.mappedParameters << '/' << patch.explicitParameters << " explicit fields mapped";
    out << " | " << missing << " missing assets | " << unsupported << " not rendered/unknown";
    return out.str();
}
}
