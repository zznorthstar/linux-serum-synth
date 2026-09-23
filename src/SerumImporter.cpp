#include "SerumImporter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <zstd.h>

namespace zyg {
namespace {
constexpr std::size_t maxFile = 128u * 1024u * 1024u;
constexpr std::size_t maxState = 256u * 1024u * 1024u;

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b.at(at)) | (std::uint32_t(b.at(at + 1)) << 8) |
           (std::uint32_t(b.at(at + 2)) << 16) | (std::uint32_t(b.at(at + 3)) << 24);
}
const Json& obj(const Json& j, const std::string& key) {
    static const Json empty = Json::object();
    if (!j.is_object()) return empty;
    const auto it = j.find(key);
    return it != j.end() && it->is_object() ? *it : empty;
}
const Json& params(const Json& j) { return obj(j, "plainParams"); }
double number(const Json& j, const char* key, double fallback) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_number() && std::isfinite(it->get<double>())
         ? it->get<double>() : fallback;
}
std::string string(const Json& j, const char* key, std::string fallback = {}) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
bool active(const Json& j, const char* key, bool fallback) {
    return number(j, key, fallback ? 1.0 : 0.0) != 0.0;
}
void diagnostic(Patch& patch, std::string path, std::string status, std::string detail) {
    patch.diagnostics.push_back({std::move(path), std::move(status), std::move(detail)});
}
void countParameters(Patch& patch, const Json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "plainParams" && it->is_object())
                patch.explicitParameters += static_cast<unsigned>(it->size());
            else countParameters(patch, *it);
        }
    } else if (j.is_array()) for (const auto& item : j) countParameters(patch, item);
}
// Every explicit field is retained in the patch's provenance tree. 'mappedParameters'
// counts only typed fields and must never be presented as full DSP coverage.
void countMapped(Patch& patch, const Json& p, const std::string& path,
                 std::initializer_list<const char*> names) {
    if (!p.is_object()) return;
    for (const auto* name : names) if (p.contains(name)) {
        ++patch.mappedParameters;
        patch.typedParameterPaths.push_back(path + "." + name);
    }
}
void unmappedDiagnostics(Patch& patch, const Json& node, const std::string& path) {
    if (node.is_array()) {
        for (std::size_t i = 0; i < node.size(); ++i)
            unmappedDiagnostics(patch, node[i], path + "[" + std::to_string(i) + "]");
    } else if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "plainParams" && it->is_object()) {
                for (auto p = it->begin(); p != it->end(); ++p) {
                    const auto key = path + "." + p.key();
                    if (std::find(patch.typedParameterPaths.begin(), patch.typedParameterPaths.end(), key)
                        == patch.typedParameterPaths.end())
                        diagnostic(patch, key, "unmapped_parameter", "retained in Serum provenance only");
                }
            } else {
                unmappedDiagnostics(patch, *it, path.empty() ? it.key() : path + "." + it.key());
            }
        }
    }
}
bool firstSliceRenders(const std::string& path) {
    if (path == "Global0.kParamMasterVolume" || path == "Global0.kParamMonoToggle" ||
        path == "Global0.kParamMono" || path == "Global0.kParamPoly")
        return true;
    if (path.starts_with("Env0."))
        return path == "Env0.kParamAttack" || path == "Env0.kParamHold" || path == "Env0.kParamDecay" ||
               path == "Env0.kParamSustain" || path == "Env0.kParamRelease";
    if (path == "VoiceFilter0.kParamEnable" || path == "VoiceFilter0.kParamFreq" ||
        path == "VoiceFilter0.kParamReso" || path == "VoiceFilter0.kParamDrive" ||
        path == "VoiceFilter0.kParamWet") return true;
    for (int i = 0; i < 3; ++i) {
        const auto osc = "Oscillator" + std::to_string(i) + ".";
        if (path == osc + "kParamEnable" || path == osc + "kParamType" ||
            path == osc + "kParamOctave" || path == osc + "kParamCoarse" ||
            path == osc + "kParamFine" || path == osc + "kParamVolume" || path == osc + "kParamPan" ||
            path == osc + "kParamUnison" || path == osc + "kParamDetune" ||
            path == osc + "WTOsc" + std::to_string(i) + ".kParamTablePos" ||
            path == osc + "WTOsc" + std::to_string(i) + ".kParamInitialPhase" ||
            path == "RoutingSlot" + std::to_string(i) + ".kParamRoutingDest") return true;
    }
    return false;
}
OscMode oscillatorMode(const std::string& s) {
    if (s == "kOsc_Wavetable" || s.empty()) return OscMode::wavetable;
    if (s == "kOsc_Sample") return OscMode::sample;
    if (s == "kOsc_MultiSample") return OscMode::multisample;
    if (s == "kOsc_Granular") return OscMode::granular;
    if (s == "kOsc_Spectral") return OscMode::spectral;
    return OscMode::unknown;
}
RouteTarget routing(const std::string& s) {
    if (s == "kRoutingDestFilter") return RouteTarget::filter;
    if (s == "kRoutingDestMaster") return RouteTarget::main;
    if (s == "kRoutingDestDirect") return RouteTarget::direct;
    if (s == "kRoutingDestNone") return RouteTarget::none;
    return RouteTarget::unknown;
}
std::string sourcePath(const Json& mode, OscMode type) {
    switch (type) {
        case OscMode::wavetable: return string(mode, "relativePathToWT");
        case OscMode::multisample: return string(mode, "sfzPathRelative");
        case OscMode::noise: return string(mode, "relativePathToNoiseSample");
        case OscMode::sub: return {};
        default: return string(mode, "samplePathRelative");
    }
}
std::string assetCategory(OscMode type) {
    switch (type) {
        case OscMode::wavetable: return "Tables";
        case OscMode::multisample: return "Multisamples";
        case OscMode::noise: return "Samples/Factory Non-Tonal/Noises";
        default: return "Samples";
    }
}
std::filesystem::path safeAssetPath(const std::filesystem::path& root,
                                    const std::string& category, const std::string& relative) {
    if (root.empty()) return {};
    const auto rel = std::filesystem::path(relative).lexically_normal();
    if (rel.is_absolute() || relative.empty()) return {};
    for (const auto& element : rel) if (element == "..") return {};
    return root / category / rel;
}
void loadWavetable(Patch& patch, Oscillator& osc, int index, const std::filesystem::path& root) {
    if (osc.asset.empty()) {
        diagnostic(patch, "Oscillator" + std::to_string(index), "missing_asset", "no wavetable reference");
        return;
    }
    const auto path = safeAssetPath(root, "Tables", osc.asset);
    if (path.empty() || !std::filesystem::is_regular_file(path)) {
        diagnostic(patch, "Oscillator" + std::to_string(index), "missing_asset", osc.asset);
        return;
    }
    std::string error;
    if (!loadWavetableFromFile(osc, path, error))
        diagnostic(patch, "Oscillator" + std::to_string(index), "unsupported_asset", error + ": " + osc.asset);
}
}

bool loadWavetableFromFile(Oscillator& osc, const std::filesystem::path& absolutePath, std::string& error) {
    if (!std::filesystem::is_regular_file(absolutePath)) { error = "file not found"; return false; }
    if (std::filesystem::file_size(absolutePath) > maxFile) { error = "file exceeds size limit"; return false; }
    // The current vertical slice accepts mono PCM/IEEE-float RIFF WAVE. This
    // loader must only run off the audio thread (patch preparation or a UI
    // file-picker callback), never in processBlock.
    std::ifstream in(absolutePath, std::ios::binary);
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
    if (bytes.size() < 44 || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "RIFF"
        || std::string(reinterpret_cast<const char*>(bytes.data() + 8), 4) != "WAVE") {
        error = "not RIFF WAVE"; return false;
    }
    auto le16 = [&](std::size_t p) { return std::uint16_t(bytes[p] | (bytes[p+1] << 8)); };
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::size_t dataAt = 0, dataSize = 0;
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const std::uint32_t size = le32(bytes, at + 4);
        const std::size_t next = at + 8u + size;
        if (next > bytes.size()) break;
        const std::string id(reinterpret_cast<const char*>(bytes.data() + at), 4);
        if (id == "fmt " && size >= 16) {
            format = le16(at + 8); channels = le16(at + 10); bits = le16(at + 22);
        } else if (id == "data") { dataAt = at + 8; dataSize = size; }
        at = next + (size & 1u);
    }
    if (!dataAt || !channels || !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) ||
                                   (format == 3 && bits == 32))) {
        error = "unsupported WAVE encoding"; return false;
    }
    const std::size_t stride = channels * (bits / 8u);
    const std::size_t frames = dataSize / stride;
    if (frames < osc.frameSize || frames > 16u * 1024u * 1024u) {
        error = "unsupported wavetable frame size"; return false;
    }
    std::vector<float> audio(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const auto* sample = bytes.data() + dataAt + i * stride;
        float v = 0.0f;
        if (format == 3 && bits == 32) {
            const auto raw = std::uint32_t(sample[0]) | (std::uint32_t(sample[1]) << 8)
                           | (std::uint32_t(sample[2]) << 16) | (std::uint32_t(sample[3]) << 24);
            std::memcpy(&v, &raw, sizeof(v));
        } else if (bits == 16) {
            const auto raw = std::int16_t(sample[0] | (sample[1] << 8));
            v = raw / 32768.0f;
        } else if (bits == 24) {
            int raw = int(sample[0]) | (int(sample[1]) << 8) | (int(sample[2]) << 16);
            if (raw & 0x800000) raw |= ~0xffffff;
            v = raw / 8388608.0f;
        } else {
            const auto raw = std::uint32_t(sample[0]) | (std::uint32_t(sample[1]) << 8)
                           | (std::uint32_t(sample[2]) << 16) | (std::uint32_t(sample[3]) << 24);
            v = std::int32_t(raw) / 2147483648.0f;
        }
        audio[i] = std::isfinite(v) ? v : 0.0f;
    }
    osc.audio = std::move(audio);
    return true;
}

SerumDocument decodeSerum(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 25 || bytes.size() > maxFile ||
        std::string(reinterpret_cast<const char*>(bytes.data()), 9) != std::string("XferJson\0", 9))
        throw std::runtime_error("not a bounded XferJson Serum preset");
    const std::uint32_t metaSize = le32(bytes, 9);
    const std::size_t stateHeader = 17u + metaSize;
    if (stateHeader + 8u > bytes.size()) throw std::runtime_error("invalid metadata length");
    SerumDocument doc;
    doc.metadata = Json::parse(bytes.begin() + 17, bytes.begin() + stateHeader);
    const auto stateSize = le32(bytes, stateHeader);
    if (stateSize > maxState) throw std::runtime_error("Serum state exceeds safety limit");
    std::vector<std::uint8_t> decoded(stateSize);
    const auto got = ZSTD_decompress(decoded.data(), decoded.size(), bytes.data() + stateHeader + 8,
                                     bytes.size() - stateHeader - 8);
    if (ZSTD_isError(got) || got != stateSize) throw std::runtime_error("invalid Zstandard Serum state");
    doc.state = Json::from_cbor(decoded);
    if (!doc.state.is_object() || !doc.metadata.is_object()) throw std::runtime_error("invalid Serum object tree");
    doc.bytes = bytes;
    return doc;
}

Patch loadSerumFile(const std::filesystem::path& file, const std::filesystem::path& assetRoot) {
    if (!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file) > maxFile)
        throw std::runtime_error("preset does not exist or exceeds safety limit");
    std::ifstream in(file, std::ios::binary);
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in), {});
    return importSerum(decodeSerum(bytes), assetRoot, file.string());
}

void mapLegacySerumModulationRoutes(Patch& patch) {
    for (auto& route : patch.modulation) {
        if (route.sourceKind == ModSource::unknown) {
            if (route.source >= 6 && route.source <= 15) {
                route.sourceKind = ModSource::lfo; route.sourceIndex = route.source - 6;
            } else if (route.source >= 25 && route.source <= 32) {
                route.sourceKind = ModSource::macro; route.sourceIndex = route.source - 25;
            }
        }
        if (route.targetKind == ModTarget::unknown) {
            if (route.destinationModule == "WTOsc" && route.destinationParameter == "kParamTablePos") {
                route.targetKind = ModTarget::wavetablePosition;
                route.targetIndex = route.destinationInstance;
            } else if (route.destinationModule == "VoiceFilter" && route.destinationParameter == "kParamFreq") {
                route.targetKind = ModTarget::filterCutoff;
                route.targetIndex = route.destinationInstance;
            }
        }
    }
}

Patch importSerum(const SerumDocument& doc, const std::filesystem::path& root, const std::string& file) {
    Patch patch;
    patch.name = string(doc.metadata, "presetName", "Unnamed Serum preset");
    patch.author = string(doc.metadata, "presetAuthor");
    patch.serumVersion = string(doc.metadata, "productVersion", "unknown");
    patch.sourcePath = file;
    patch.assetRoot = root.string();
    patch.originalPreset = doc.bytes;
    patch.unknownSerumState = doc.state; // exact semantic tree, including currently unresolved fields
    countParameters(patch, doc.state);
    const Json& global = obj(doc.state, "Global0");
    patch.global = global;
    const Json& gp = params(global);
    patch.masterVolume = number(gp, "kParamMasterVolume", 0.7);
    patch.mono = active(gp, "kParamMonoToggle", active(gp, "kParamMono", false));
    patch.polyphony = std::clamp(int(number(gp, "kParamPoly", 16)), 1, 64);
    countMapped(patch, gp, "Global0", {"kParamMasterVolume", "kParamMonoToggle", "kParamMono", "kParamPoly"});

    for (int i = 0; i < 5; ++i) {
        auto& osc = patch.oscillators[i];
        const auto key = "Oscillator" + std::to_string(i);
        const Json& o = obj(doc.state, key), &p = params(o);
        osc.additional = o;
        osc.enabled = active(p, "kParamEnable", i == 0);
        osc.modeId = string(p, "kParamType", i == 3 ? "kOsc_Noise" : i == 4 ? "kOsc_Sub" : "kOsc_Wavetable");
        osc.mode = i == 3 ? OscMode::noise : i == 4 ? OscMode::sub : oscillatorMode(osc.modeId);
        osc.octave = int(number(p, "kParamOctave", 0));
        osc.semitone = int(number(p, "kParamCoarse", 0));
        osc.fine = number(p, "kParamFine", 0.0);
        osc.volume = number(p, "kParamVolume", 0.75);
        osc.pan = number(p, "kParamPan", 0.0);
        osc.unison = std::clamp(int(number(p, "kParamUnison", 1)), 1, 16);
        osc.detune = number(p, "kParamDetune", 0.2);
        countMapped(patch, p, key, {"kParamEnable", "kParamType", "kParamOctave", "kParamCoarse",
                               "kParamFine", "kParamVolume", "kParamPan", "kParamUnison", "kParamDetune"});
        const std::string modeKey = (osc.mode == OscMode::wavetable ? "WTOsc" :
            osc.mode == OscMode::sample ? "SampleOsc" : osc.mode == OscMode::multisample ? "MultiSampleOsc" :
            osc.mode == OscMode::granular ? "GranularOsc" : osc.mode == OscMode::noise ? "NoiseOsc" :
            osc.mode == OscMode::sub ? "SubOsc" : "SpectralOsc") + std::to_string(i);
        const Json& mode = obj(o, modeKey), &mp = params(mode);
        osc.modeState = mode;
        osc.asset = sourcePath(mode, osc.mode);
        osc.tablePosition = number(mp, "kParamTablePos", 0.0);
        osc.initialPhase = number(mp, "kParamInitialPhase", 0.0);
        osc.randomPhase = number(mp, "kParamRandomPhase", 0.0);
        osc.warpOne = string(mp, "kParamWarpMode");
        osc.warpTwo = string(mp, "kParamWarpMode2");
        osc.warpOneAmount = number(mp, "kParamWarp", 0.0);
        osc.warpTwoAmount = number(mp, "kParamWarp2", 0.0);
        countMapped(patch, mp, key + "." + modeKey, {"kParamTablePos", "kParamInitialPhase", "kParamRandomPhase",
                                "kParamWarpMode", "kParamWarpMode2", "kParamWarp", "kParamWarp2"});
        if (osc.enabled && osc.mode == OscMode::wavetable) loadWavetable(patch, osc, i, root);
        else if (osc.enabled) {
            const auto path = safeAssetPath(root, assetCategory(osc.mode), osc.asset);
            if (!osc.asset.empty() && !std::filesystem::is_regular_file(path))
                diagnostic(patch, key, "missing_asset", osc.asset);
            if (osc.asset.empty() && osc.mode != OscMode::sub)
                diagnostic(patch, key, "missing_asset", "no audio asset reference");
            diagnostic(patch, key, "not_rendered", "oscillator mode " + osc.modeId);
        }
    }
    for (int i = 0; i < 2; ++i) {
        const auto key = "VoiceFilter" + std::to_string(i);
        const Json& p = params(obj(doc.state, key));
        auto& f = patch.filters[i];
        f.additional = obj(doc.state, key);
        f.enabled = active(p, "kParamEnable", false);
        f.type = string(p, "kParamType", "L12");
        f.cutoff = number(p, "kParamFreq", 1.0);
        f.resonance = number(p, "kParamReso", 10.0);
        f.drive = number(p, "kParamDrive", 0.0);
        f.var = number(p, "kParamVar", 0.0);
        f.wet = number(p, "kParamWet", 100.0);
        countMapped(patch, p, key, {"kParamEnable", "kParamType", "kParamFreq", "kParamReso",
                               "kParamDrive", "kParamVar", "kParamWet"});
        if (f.enabled && f.type != "L12") diagnostic(patch, key, "not_rendered", "filter " + f.type + " uses first-slice lowpass fallback");
    }
    for (int i = 0; i < 4; ++i) {
        const Json& p = params(obj(doc.state, "Env" + std::to_string(i)));
        auto& e = patch.envelopes[i];
        e.additional = obj(doc.state, "Env" + std::to_string(i));
        e.attack = number(p, "kParamAttack", e.attack); e.hold = number(p, "kParamHold", e.hold);
        e.decay = number(p, "kParamDecay", e.decay); e.sustain = number(p, "kParamSustain", e.sustain);
        e.release = number(p, "kParamRelease", e.release);
        e.curve = {number(p, "kParamCurve1", 50), number(p, "kParamCurve2", 66.6), number(p, "kParamCurve3", 66.6)};
        countMapped(patch, p, "Env" + std::to_string(i), {"kParamAttack", "kParamHold", "kParamDecay", "kParamSustain",
                               "kParamRelease", "kParamCurve1", "kParamCurve2", "kParamCurve3"});
        if (i > 0 && p.is_object() && !p.empty()) diagnostic(patch, "Env" + std::to_string(i), "not_rendered", "modulation envelope retained");
    }
    for (int i = 0; i < 7; ++i) {
        const Json& p = params(obj(doc.state, "RoutingSlot" + std::to_string(i)));
        auto& route = patch.routes[i];
        route.additional = obj(doc.state, "RoutingSlot" + std::to_string(i));
        route.target = routing(string(p, "kParamRoutingDest", i < 5 ? "kRoutingDestFilter" : "kRoutingDestMaster"));
        route.filterBalance = number(p, "kParamFilterBalance", 0.0);
        route.fxBus1Level = number(p, "kParamFXBus1Level", 0.0);
        route.fxBus2Level = number(p, "kParamFXBus2Level", 0.0);
        countMapped(patch, p, "RoutingSlot" + std::to_string(i), {"kParamRoutingDest", "kParamFilterBalance", "kParamFXBus1Level", "kParamFXBus2Level"});
        if (route.fxBus1Level || route.fxBus2Level) diagnostic(patch, "RoutingSlot" + std::to_string(i), "not_rendered", "FX bus send retained");
    }
    for (int i = 0; i < 64; ++i) {
        const auto key = "ModSlot" + std::to_string(i);
        const Json& m = obj(doc.state, key);
        if (!m.contains("source") || !m["source"].is_array() || m["source"].empty()) continue;
        ModulationRoute route;
        route.slot = i;
        route.source = m["source"][0].is_number_integer() ? m["source"][0].get<int>() : 0;
        route.auxiliary = m["source"].size() > 1 && m["source"][1].is_number_integer() ? m["source"][1].get<int>() : 0;
        route.sourceName = sourceName(route.source);
        if (route.source >= 6 && route.source <= 15) {
            route.sourceKind = ModSource::lfo; route.sourceIndex = route.source - 6;
        } else if (route.source >= 25 && route.source <= 32) {
            route.sourceKind = ModSource::macro; route.sourceIndex = route.source - 25;
        }
        route.destinationModule = string(m, "destModuleTypeString");
        route.destinationParameter = string(m, "destModuleParamName");
        route.destinationInstance = int(number(m, "destModuleID", 0));
        route.destinationParameterId = int(number(m, "destModuleParamID", -1));
        if (route.destinationModule == "WTOsc" && route.destinationParameter == "kParamTablePos") {
            route.targetKind = ModTarget::wavetablePosition;
            route.targetIndex = route.destinationInstance;
        } else if (route.destinationModule == "VoiceFilter" && route.destinationParameter == "kParamFreq") {
            route.targetKind = ModTarget::filterCutoff;
            route.targetIndex = route.destinationInstance;
        }
        const Json& p = params(m);
        route.amount = number(p, "kParamAmount", 0.0);
        route.bipolar = active(p, "kParamBipolar", false);
        route.bypass = active(p, "kParamBypass", false);
        route.additional = m;
        countMapped(patch, p, key, {"kParamAmount", "kParamBipolar", "kParamBypass"});
        patch.modulation.push_back(std::move(route));
        diagnostic(patch, key, "not_rendered", "modulation route retained, DSP matrix pending");
    }
    for (int i = 0; i < 10; ++i) {
        patch.lfos[i] = obj(doc.state, "LFO" + std::to_string(i));
        if (!params(patch.lfos[i]).empty()) diagnostic(patch, "LFO" + std::to_string(i), "not_rendered", "LFO retained");
    }
    patch.lfoOneSine = false;
    for (int i = 0; i < 8; ++i) {
        const auto key = "Macro" + std::to_string(i);
        patch.macros[i] = obj(doc.state, key);
        const auto& mp = params(patch.macros[i]);
        patch.macroValues[i] = std::clamp(number(mp, "kParamValue", 0.0) / 100.0, 0.0, 1.0);
        countMapped(patch, mp, key, {"kParamValue"});
    }
    for (int rack = 0; rack < 3; ++rack) {
        const Json& r = obj(doc.state, "FXRack" + std::to_string(rack));
        const Json& fx = r.contains("FX") ? r["FX"] : Json();
        if (!fx.is_array()) continue;
        int pos = 0;
        for (const auto& item : fx) {
            FxModule module;
            module.rack = rack; module.position = pos++;
            if (item.is_object()) {
                for (auto it = item.begin(); it != item.end(); ++it)
                    if (it.key().starts_with("FX") && it->is_object()) { module.type = it.key(); module.parameters = params(*it); break; }
            }
            module.additional = item;
            patch.fx.push_back(std::move(module));
            diagnostic(patch, "FXRack" + std::to_string(rack), "not_rendered", "FX module " + patch.fx.back().type + " retained");
        }
    }
    patch.arp = obj(doc.state, "Arp0");
    patch.clips = obj(doc.state, "Clip0");
    for (int i = 0; i < 12; ++i) {
        patch.arpClips[i] = obj(doc.state, "ArpClip" + std::to_string(i));
        patch.midiClips[i] = obj(doc.state, "MidiClip" + std::to_string(i));
        if (patch.arpClips[i].contains("clip"))
            diagnostic(patch, "ArpClip" + std::to_string(i), "not_rendered", "arp clip retained");
        if (patch.midiClips[i].contains("clip"))
            diagnostic(patch, "MidiClip" + std::to_string(i), "not_rendered", "MIDI clip retained");
    }
    unmappedDiagnostics(patch, doc.state, "");
    for (const auto& path : patch.typedParameterPaths)
        if (!firstSliceRenders(path))
            diagnostic(patch, path, "not_rendered_parameter", "typed in Patch, not yet used by DSP");
    // All untyped or future fields remain in unknownSerumState. This sidecar is
    // preservation, not a semantic rendering claim.
    return patch;
}
}
