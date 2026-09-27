#include "SerumImporter.h"
#include "FxParams.h"
#include "Wavetable.h"
#include "Assets.h"
#include "dsp/Filters.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
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
    return it != j.end() && it->is_number() && std::isfinite(it->get<double>()) ? it->get<double>() : fallback;
}
std::string string(const Json& j, const char* key, std::string fallback = {}) {
    if (!j.is_object()) return fallback;
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}
void diagnostic(Patch& patch, std::string path, std::string status, std::string detail) {
    patch.diagnostics.push_back({std::move(path), std::move(status), std::move(detail)});
}
void countParameters(Patch& patch, const Json& j) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "plainParams" && it->is_object()) patch.explicitParameters += static_cast<unsigned>(it->size());
            else countParameters(patch, *it);
        }
    } else if (j.is_array()) for (const auto& item : j) countParameters(patch, item);
}
void unmappedDiagnostics(Patch& patch, const std::unordered_set<std::string>& typed, const Json& node, const std::string& path) {
    if (node.is_array()) {
        for (std::size_t i = 0; i < node.size(); ++i) unmappedDiagnostics(patch, typed, node[i], path + "[" + std::to_string(i) + "]");
    } else if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "plainParams" && it->is_object()) {
                for (auto p = it->begin(); p != it->end(); ++p) {
                    const auto key = path + "." + p.key();
                    if (!typed.count(key)) diagnostic(patch, key, "unmapped_parameter", "retained in Serum provenance only");
                }
            } else unmappedDiagnostics(patch, typed, *it, path.empty() ? it.key() : path + "." + it.key());
        }
    }
}

// Reads one module's plainParams and records every typed field for accounting.
struct Tracker {
    Patch& patch;
    std::unordered_set<std::string> seen;
    void typed(const std::string& path) {
        if (seen.insert(path).second) { ++patch.mappedParameters; patch.typedParameterPaths.push_back(path); }
    }
};
struct Mod {
    const Json& p; std::string path; Tracker& t;
    bool has(const char* k) const { return p.is_object() && p.contains(k); }
    double num(const char* k, double def) const {
        if (!has(k)) return def;
        t.typed(path + "." + k);
        return number(p, k, def);
    }
    std::string str(const char* k, const std::string& def = {}) const {
        if (!has(k)) return def;
        t.typed(path + "." + k);
        return string(p, k, def);
    }
    bool flag(const char* k, bool def) const { return num(k, def ? 1.0 : 0.0) != 0.0; }
    void mark(const char* k) const { if (has(k)) t.typed(path + "." + k); }
};

// ---------------------------------------------------------------- enum tables
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
SubShape subShape(const std::string& id) {
    if (id == "kPulse") return SubShape::pulse;
    if (id == "kRoundRect") return SubShape::roundedRectangle;
    if (id == "kSaw") return SubShape::saw;
    if (id == "kSquare") return SubShape::square;
    if (id == "kTriangle") return SubShape::triangle;
    return SubShape::unknown;
}
int otherOscillator(int target, bool second) {
    std::array<int, 2> sources {};
    int at = 0;
    for (int i = 0; i < 3; ++i) if (i != target) sources[std::size_t(at++)] = i;
    return sources[second ? 1u : 0u];
}
WarpDefinition warpDefinition(const std::string& id, int target) {
    WarpDefinition result;
    if (id.empty()) return result;
    struct Entry { const char* id; WarpMode mode; };
    static constexpr Entry direct[] = {
        {"kBendPos", WarpMode::bendPositive}, {"kBendNeg", WarpMode::bendNegative}, {"kBendPosNeg", WarpMode::bendBoth},
        {"kASYMPos", WarpMode::asymPositive}, {"kASYMNeg", WarpMode::asymNegative}, {"kASYMPosNeg", WarpMode::asymBoth},
        {"kPWM", WarpMode::pwm}, {"kFlip", WarpMode::flip}, {"kSync", WarpMode::sync}, {"kQuantize", WarpMode::quantize},
        {"kEvenOdd", WarpMode::evenOdd}, {"kSelfPD", WarpMode::selfPhase},
        {"kDistHardClip", WarpMode::hardClip}, {"kDistSoftClip", WarpMode::softClip}, {"kDistSinFold", WarpMode::sineFold},
        {"kDistLinFold", WarpMode::linearFold}, {"kDistSineShaper", WarpMode::sineShaper}, {"kDistAsym", WarpMode::asymmetricClip},
        {"kDistRectify", WarpMode::rectify}, {"kDistDiode1", WarpMode::diode1}, {"kDistDiode2", WarpMode::diode2},
        {"kDistSoftSat", WarpMode::softSat}, {"kDistTapeSat", WarpMode::tapeSat}, {"kDistTube", WarpMode::tube},
        {"kDistStompBox", WarpMode::stompBox}, {"kDistZeroSquare", WarpMode::zeroSquare},
        {"kFilterLPF", WarpMode::filterLow}, {"kFilterHPF", WarpMode::filterHigh},
        {"kAddharmonics", WarpMode::addHarmonics}, {"kAddsubharmonics", WarpMode::addSubharmonics},
        {"kDetune", WarpMode::spectralDetune}, {"kGate", WarpMode::gate}, {"kMirror", WarpMode::mirror},
        {"kPeakHarmDown", WarpMode::peakHarmonicDown}, {"kPeakHarmUp", WarpMode::peakHarmonicUp},
        {"kPeakOctaveDown", WarpMode::peakOctaveDown}, {"kPeakOctaveUp", WarpMode::peakOctaveUp},
        {"kShepardFilter", WarpMode::shepardFilter}, {"kShepardNarrow", WarpMode::shepardNarrow},
        {"kSmear", WarpMode::smear}, {"kSpectralComb", WarpMode::spectralComb},
        {"kSpectralPhaseTwist", WarpMode::phaseTwist}, {"kSpectralPitchShift", WarpMode::spectralPitchShift},
        {"kSpectralShift", WarpMode::spectralShift}, {"kSpread", WarpMode::spread},
    };
    for (const auto& e : direct) if (id == e.id) { result.mode = e.mode; return result; }
    if (id.rfind("kRemap_", 0) == 0) { result.mode = WarpMode::remap; result.variant = std::atoi(id.c_str() + 7); return result; }
    struct Prefix { const char* prefix; WarpMode mode; };
    static constexpr Prefix prefixes[] = {
        {"kFM_", WarpMode::frequencyMod}, {"kFMX_", WarpMode::frequencyModX}, {"kFMP_", WarpMode::frequencyModPhase},
        {"kPD_", WarpMode::phaseMod}, {"kRM_", WarpMode::ringMod}, {"kAM_", WarpMode::amplitudeMod},
        {"kMask_", WarpMode::mask}, {"kVocode_", WarpMode::vocode},
    };
    for (const auto& pr : prefixes) {
        const std::string p(pr.prefix);
        if (id.rfind(p, 0) != 0) continue;
        const auto suffix = id.substr(p.size());
        if (suffix == "OSC") result.sourceIndex = otherOscillator(target, false);
        else if (suffix == "OSC2") result.sourceIndex = otherOscillator(target, true);
        else if (suffix == "SUB") result.sourceIndex = 4;
        else if (suffix == "NOISE") result.sourceIndex = 3;
        else if (suffix == "FILT1") result.sourceIndex = 5;
        else if (suffix == "FILT2") result.sourceIndex = 6;
        else { result.mode = WarpMode::unknown; return result; }
        result.mode = pr.mode;
        return result;
    }
    result.mode = WarpMode::unknown; // kDLM and any future identifier
    return result;
}
DetuneMode detuneMode(const std::string& s) {
    if (s == "kDetuneExp") return DetuneMode::exponential;
    if (s == "kDetuneInv") return DetuneMode::inverse;
    if (s == "kDetuneRandom") return DetuneMode::random;
    if (s == "kDetuneSuper") return DetuneMode::super;
    return DetuneMode::linear;
}
UnisonStack unisonStack(const std::string& s) {
    if (s == "kCenter12") return UnisonStack::center12;
    if (s == "kOctave1") return UnisonStack::octave1;
    if (s == "kOctave2") return UnisonStack::octave2;
    if (s == "kOctave3") return UnisonStack::octave3;
    if (s == "kOctaveFifth1") return UnisonStack::octaveFifth1;
    if (s == "kOctaveFifth2") return UnisonStack::octaveFifth2;
    if (s == "kOctaveFifth3") return UnisonStack::octaveFifth3;
    return UnisonStack::none;
}
LoopMode loopMode(const std::string& s) {
    if (s == "kReverse") return LoopMode::reverse;
    if (s == "kPingPong") return LoopMode::pingPong;
    if (s == "kTailed") return LoopMode::tailed;
    return LoopMode::forward;
}
WindowShape windowShape(const std::string& s) {
    if (s == "kWindowBlackmanHarris") return WindowShape::blackmanHarris;
    if (s == "kWindowExpDec") return WindowShape::expDecay;
    if (s == "kWindowTriangle") return WindowShape::triangle;
    if (s == "kWindowTukey") return WindowShape::tukey;
    return WindowShape::gaussian;
}
NoiseType noiseType(const std::string& s) {
    if (s == "Pink") return NoiseType::pink;
    if (s == "Brown") return NoiseType::brown;
    if (s == "Geiger") return NoiseType::geiger;
    return NoiseType::white;
}
int distortionMode(const std::string& s) {
    static const char* names[] = {"kAsym", "kDiode1", "kDiode2", "kDownsample", "kHardClip", "kLinFold", "kOverdrive", "kRectify",
                                  "kSinFold", "kSineShaper", "kSoftClip", "kSoftSat", "kStompBox", "kTapeSat", "kXShaper",
                                  "kXShaperAsym", "kZeroSquare"};
    for (int i = 0; i < 17; ++i) if (s == names[i]) return i;
    return 6;
}
int reverbType(const std::string& s) {
    if (s == "kVintage") return 1; if (s == "kSpace") return 2; if (s == "kAbyss") return 3; return 0;
}

CurvePoints curveFromJson(const Json& j, bool closed) {
    CurvePoints c; c.closed = closed;
    if (!j.is_object() || !j.contains("xVals") || !j.contains("yVals")) return c;
    const auto& xs = j["xVals"]; const auto& ys = j["yVals"];
    if (!xs.is_array() || !ys.is_array()) return c;
    const std::size_t n = std::min(xs.size(), ys.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!xs[i].is_number() || !ys[i].is_number()) continue;
        // Serum stores y with 0 at the top of the editor; the native value is 1 - y.
        c.x.push_back(std::clamp(xs[i].get<double>(), 0.0, 1.0)); c.y.push_back(1.0 - std::clamp(ys[i].get<double>(), 0.0, 1.0));
        double bend = 0.5;
        if (j.contains("curveVals") && j["curveVals"].is_array() && i < j["curveVals"].size() && j["curveVals"][i].is_number())
            bend = std::clamp(j["curveVals"][i].get<double>(), 0.0, 1.0);
        c.bend.push_back(bend);
    }
    return c;
}

std::uint64_t fnv1a(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto b : bytes) { h ^= b; h *= 1099511628211ull; }
    return h ? h : 1;
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
        std::uint32_t size = le32(bytes, at + 4);
        const std::string id(reinterpret_cast<const char*>(bytes.data() + at), 4);
        if (id == "data") {
            // Some authoring tools leave the data length larger than the file; use what is present.
            dataAt = at + 8; dataSize = std::min<std::size_t>(size, bytes.size() - dataAt); break;
        }
        const std::size_t next = at + 8u + size;
        if (next > bytes.size()) break;
        if (id == "fmt " && size >= 16) {
            format = le16(at + 8); channels = le16(at + 10); bits = le16(at + 22);
        }
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
    prepareWavetableMipmaps(osc);
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
    return importSerum(decodeSerum(bytes), assetRoot.empty() ? defaultContentRoot() : assetRoot, file.string());
}



namespace {
int oscParamFromName(const std::string& n) {
    struct E { const char* name; OscParam p; };
    static constexpr E table[] = {
        {"kParamCoarsePit", OscParam::coarse}, {"kParamDetune", OscParam::detune}, {"kParamDetuneWid", OscParam::blend},
        {"kParamFine", OscParam::fine}, {"kParamHzOffset", OscParam::hzOffset}, {"kParamLoopEnd", OscParam::loopEnd},
        {"kParamLoopStart", OscParam::loopStart}, {"kParamOctave", OscParam::octave}, {"kParamPan", OscParam::pan},
        {"kParamPitch", OscParam::pitch}, {"kParamPitchRatio", OscParam::pitchRatio}, {"kParamPosition", OscParam::position},
        {"kParamScanRate", OscParam::scanRate}, {"kParamStart", OscParam::start}, {"kParamEnd", OscParam::end},
        {"kParamUnisonStereo", OscParam::unisonStereo}, {"kParamUnisonWarp", OscParam::unisonWarp},
        {"kParamUnisonWarp2", OscParam::unisonWarp2}, {"kParamVolume", OscParam::volume},
        {"kParamTablePos", OscParam::tablePos}, {"kParamWarp", OscParam::warp1}, {"kParamWarp2", OscParam::warp2},
        {"kParamWarpVar", OscParam::warpVar1}, {"kParamWarpVar2", OscParam::warpVar2},
        {"kParamInitialPhase", OscParam::initialPhase}, {"kParamRandomPhase", OscParam::randomPhase},
        {"kParamUnisonWTPos", OscParam::unisonWTPos},
        {"kParamEnvAttack", OscParam::envAttack}, {"kParamEnvDecay", OscParam::envDecay},
        {"kParamEnvSustain", OscParam::envSustain}, {"kParamEnvRelease", OscParam::envRelease},
        {"kParamTimbreShift", OscParam::timbreShift}, {"kParamDensity", OscParam::density},
        {"kParamGrainLength", OscParam::grainLength}, {"kParamRandomDir", OscParam::randomDir},
        {"kParamRandomGain", OscParam::randomGain}, {"kParamRandomGrainLength", OscParam::randomGrainLength},
        {"kParamRandomOffset", OscParam::randomOffset}, {"kParamRandomPan", OscParam::randomPan},
        {"kParamRandomPitch", OscParam::randomPitch}, {"kParamRandomWarp", OscParam::randomWarp},
        {"kParamRandomWarp2", OscParam::randomWarp2}, {"kParamWindowParam", OscParam::windowParam},
        {"kParamWindowSkew", OscParam::windowSkew}, {"kParamFreqHi", OscParam::freqHi}, {"kParamFreqLo", OscParam::freqLo},
        {"kParamSpecFltShift", OscParam::specShift}, {"kParamSpecFltWetDry", OscParam::specWet},
        {"kParamColor", OscParam::color},
    };
    for (const auto& e : table) if (n == e.name) return int(e.p);
    return -1;
}

void mapSource(int id, ModSource& kind, int& index) {
    if (!modSourceFromId(id, kind, index)) { kind = ModSource::unknown; index = 0; }
}

// Fills the route's typed destination from Serum's module/parameter names.
void mapDestination(const Patch& patch, ModulationRoute& r) {
    const auto& m = r.destinationModule;
    const auto& n = r.destinationParameter;
    const int id = r.destinationInstance;
    r.targetKind = ModTarget::unknown; r.targetIndex = 0; r.targetParam = 0;
    auto set = [&](ModTarget k, int inst, int prm) { r.targetKind = k; r.targetIndex = inst; r.targetParam = prm; };
    if (m == "WTOsc" || m == "Oscillator" || m == "SampleOsc" || m == "MultiSampleOsc" || m == "GranularOsc"
        || m == "SpectralOsc" || m == "NoiseOsc") {
        const int p = oscParamFromName(n);
        if (p >= 0 && id >= 0 && id < 5) {
            const auto op = OscParam(p);
            if (m == "WTOsc" && op == OscParam::tablePos) set(ModTarget::wavetablePosition, id, p);
            else if (m == "WTOsc" && op == OscParam::warp1) set(ModTarget::warpOneAmount, id, p);
            else if (m == "WTOsc" && op == OscParam::warp2) set(ModTarget::warpTwoAmount, id, p);
            else set(ModTarget::oscParam, id, p);
        }
    } else if (m == "VoiceFilter") {
        static constexpr struct { const char* n; FilterParam p; } t[] = {
            {"kParamFreq", FilterParam::freq}, {"kParamReso", FilterParam::reso}, {"kParamDrive", FilterParam::drive},
            {"kParamVar", FilterParam::var}, {"kParamWet", FilterParam::wet}, {"kParamLevelOut", FilterParam::level},
            {"kParamStereo", FilterParam::stereo}, {"kParamX", FilterParam::x}, {"kParamY", FilterParam::y}};
        for (const auto& e : t) if (n == e.n && id >= 0 && id < 2) {
            if (e.p == FilterParam::freq) set(ModTarget::filterCutoff, id, int(e.p)); else set(ModTarget::filterParam, id, int(e.p));
        }
    } else if (m == "Env") {
        static constexpr struct { const char* n; EnvParam p; } t[] = {
            {"kParamAttack", EnvParam::attack}, {"kParamHold", EnvParam::hold}, {"kParamDecay", EnvParam::decay},
            {"kParamSustain", EnvParam::sustain}, {"kParamRelease", EnvParam::release}, {"kParamCurve1", EnvParam::curve1},
            {"kParamCurve2", EnvParam::curve2}, {"kParamCurve3", EnvParam::curve3}};
        for (const auto& e : t) if (n == e.n && id >= 0 && id < 4) set(ModTarget::envParam, id, int(e.p));
    } else if (m == "LFO") {
        static constexpr struct { const char* n; LfoParam p; } t[] = {
            {"kParamRate", LfoParam::rate}, {"kParamPhase", LfoParam::phase}, {"kParamRise", LfoParam::rise},
            {"kParamDelay", LfoParam::delay}, {"kParamSmooth", LfoParam::smooth}};
        for (const auto& e : t) if (n == e.n && id >= 0 && id < 10) set(ModTarget::lfoParam, id, int(e.p));
    } else if (m == "Macro") {
        if (n == "kParamValue" && id >= 0 && id < 8) set(ModTarget::macroValue, id, 0);
    } else if (m == "Global") {
        static constexpr struct { const char* n; GlobalParam p; } t[] = {
            {"kParamMasterTuning", GlobalParam::masterTuning}, {"kParamPortamentoTime", GlobalParam::portamentoTime},
            {"kParamSwing", GlobalParam::swing}, {"kParamTranspose", GlobalParam::transpose}, {"kParamVoiceAmp", GlobalParam::voiceAmp}};
        for (const auto& e : t) if (n == e.n) set(ModTarget::globalParam, 0, int(e.p));
    } else if (m == "VoicePanel") {
        if (n == "kParamGlobalScalingEnvTime") set(ModTarget::globalParam, 0, int(GlobalParam::envTimeScale));
        else if (n == "kParamGlobalScalingLfoTime") set(ModTarget::globalParam, 0, int(GlobalParam::lfoTimeScale));
    } else if (m == "RoutingSlot") {
        if (id >= 0 && id < 7) {
            if (n == "kParamFilterBalance") set(ModTarget::routingParam, id, int(RoutingParam::filterBalance));
            else if (n == "kParamFXBus1Level") set(ModTarget::routingParam, id, int(RoutingParam::fxBus1Level));
            else if (n == "kParamFXBus2Level") set(ModTarget::routingParam, id, int(RoutingParam::fxBus2Level));
        }
    } else if (m == "Arp") {
        static constexpr const char* names[] = {"kParamChance", "kParamGate", "kParamRate", "kParamTransposeRange", "kParamVeloTarget", "kParamWrapPhantomNote"};
        for (int i = 0; i < 6; ++i) if (n == names[i]) set(ModTarget::arpParam, 0, i);
    } else if (m == "LFOPointModBus") {
        if (n == "kParamValue" && id >= 0 && id < 16) set(ModTarget::lfoPointBus, id, 0);
    } else if (m == "MidiClip") {
        if (n == "kParamRate") set(ModTarget::clipParam, 0, 0);
    } else if (m.rfind("FX", 0) == 0) {
        const int rack = id / 100, pos = id % 100;
        for (std::size_t i = 0; i < patch.fx.size(); ++i) {
            const auto& fxm = patch.fx[i];
            if (fxm.rack != rack || fxm.position != pos || fxm.type != m) continue;
            if (n == "kParamLevelOut") { set(ModTarget::fxParam, int(i), fxLevelSlot); break; }
            const auto table = fxParamTable(fxm.fxType);
            for (std::size_t k = 0; k < table.size(); ++k) if (n == table[k].serumKey) { set(ModTarget::fxParam, int(i), int(k)); break; }
            break;
        }
    }
}
}

void mapLegacySerumModulationRoutes(Patch& patch) {
    for (int i = 0; i < 3; ++i) {
        auto& oscillator = patch.oscillators[std::size_t(i)];
        const auto& p = params(oscillator.modeState);
        if (oscillator.warpOne.empty()) oscillator.warpOne = string(p, "kParamWarpMenu");
        if (oscillator.warpTwo.empty()) oscillator.warpTwo = string(p, "kParamWarpMenu2");
        if (oscillator.warpDefinitions[0].mode == WarpMode::off && !oscillator.warpOne.empty())
            oscillator.warpDefinitions[0] = warpDefinition(oscillator.warpOne, i);
        if (oscillator.warpDefinitions[1].mode == WarpMode::off && !oscillator.warpTwo.empty())
            oscillator.warpDefinitions[1] = warpDefinition(oscillator.warpTwo, i);
    }
    for (auto& route : patch.modulation) {
        if (route.sourceKind == ModSource::unknown) mapSource(route.source, route.sourceKind, route.sourceIndex);
        if (route.targetKind == ModTarget::unknown) mapDestination(patch, route);
    }
}


Patch importSerum(const SerumDocument& doc, const std::filesystem::path& root, const std::string& file) {
    Patch patch;
    Tracker t{patch, {}};
    patch.name = string(doc.metadata, "presetName", "Unnamed Serum preset");
    patch.author = string(doc.metadata, "presetAuthor");
    patch.serumVersion = string(doc.metadata, "productVersion", "unknown");
    patch.sourcePath = file;
    patch.assetRoot = root.string();
    patch.originalPreset = doc.bytes;
    patch.presetId = doc.bytes.empty() ? 0 : fnv1a(doc.bytes);
    patch.unknownSerumState = doc.state; // exact semantic tree, including currently unresolved fields
    countParameters(patch, doc.state);

    // ------------------------------------------------------------------ global
    const Json& global = obj(doc.state, "Global0");
    patch.global = global;
    {
        Mod g{params(global), "Global0", t};
        auto& gs = patch.globals;
        patch.masterVolume = g.num("kParamMasterVolume", 0.7);
        patch.mono = g.flag("kParamMonoToggle", g.flag("kParamMono", false));
        patch.polyphony = std::clamp(int(g.num("kParamPolyCount", g.num("kParamPoly", 16))), 1, 64);
        gs.directVol = g.num("kParamDirectVol", 1.0);
        gs.fxBus1Vol = g.num("kParamFXBus1Vol", 1.0); gs.fxBus2Vol = g.num("kParamFXBus2Vol", 1.0);
        gs.fxBus1Dest = int(g.num("kParamFXBus1Dest", 0.0)); gs.fxBus2Dest = int(g.num("kParamFXBus2Dest", 0.0));
        gs.portamentoTime = g.num("kParamPortamentoTime", 0.0); gs.portamentoCurve = g.num("kParamPortamentoCurve", 50.0);
        gs.portaAlways = g.flag("kParamPortaAlways", false); gs.portaScaled = g.flag("kParamPortaScaled", false);
        gs.legato = g.flag("kParamLegato", false);
        gs.bendUp = g.num("kParamBendRangeUp", 2.0); gs.bendDown = g.num("kParamBendRangeDn", -2.0);
        const auto priority = g.str("kParamVoicePriority");
        gs.priority = priority == "Low" ? VoicePriority::low : priority == "High" ? VoicePriority::high : VoicePriority::latest;
        gs.masterTuning = g.num("kParamGlobalTuning", 440.0);
        gs.transpose = g.num("kParamTranspose", 0.0); gs.swing = g.num("kParamSwing", 50.0);
        gs.oversampling = int(g.num("kParamOversampling", 0.0));
        gs.noteLatch = g.flag("kParamNoteLatch", false); gs.limitSameNote = g.flag("kParamLimitSameNotePolyphony", false);
        gs.voiceAmp = g.num("kParamVoiceAmp", 1.0); gs.modWheel = g.num("kParamModWheel", 0.0) / 100.0;
        gs.midiOutClipPlayer = g.str("kParamMidiOut") == "ClipPlayer";
        g.mark("kParamSwingDiv"); g.mark("kParamProgram"); g.mark("kParamUseUltraOnRender");
        gs.noteCurveApplyTuning = g.flag("kParamNoteCurveApplyTuning", false);
        gs.pitchExpressionApplyTuning = g.flag("kParamPitchExpressionApplyTuning", false);
        if (gs.noteCurveApplyTuning || gs.pitchExpressionApplyTuning)
            diagnostic(patch, "Global0", "not_rendered_parameter", "apply-tuning switch is on but ZYG has no microtuning tables; equal temperament is used");
        gs.serum1Compatibility = g.flag("kParamS1Compatibility", false);
        if (gs.serum1Compatibility)
            diagnostic(patch, "Global0.kParamS1Compatibility", "not_rendered_parameter", "Serum 1 compatibility mode: legacy behaviour differences are undocumented; rendered with native behaviour");
        gs.mpeEnabled = number(doc.state, "mpeEnabled", 0.0) != 0.0;
        gs.mpePitchBendRange = number(doc.state, "mpePitchBendRange", 48.0);
    }
    {
        Mod q{params(obj(doc.state, "PitchQuantizer0")), "PitchQuantizer0", t};
        patch.globals.pitchQuantizerKey = int(q.num("kParamKey", 0.0));
        patch.globals.pitchQuantizerScale = int(q.num("kParamScale", 0.0));
        if (patch.globals.pitchQuantizerScale) diagnostic(patch, "PitchQuantizer0", "not_rendered", "quantizer scale ids are unverified; state retained");
    }
    {
        Mod v{params(obj(doc.state, "VoicePanel0")), "VoicePanel0", t};
        auto& vp = patch.voicePanel;
        for (int n = 0; n < 8; ++n) {
            const std::string s = "kParamVoice" + std::to_string(n + 1);
            vp.detune[std::size_t(n)] = v.num((s + "Detune").c_str(), 0.0);
            vp.pan[std::size_t(n)] = v.num((s + "Pan").c_str(), 0.0);
            vp.envTime[std::size_t(n)] = v.num((s + "EnvTime").c_str(), 0.0);
            vp.cutoff[std::size_t(n)] = v.num((s + "FilterCutoff").c_str(), 0.0);
            vp.mod1[std::size_t(n)] = v.num((s + "Mod1").c_str(), 0.0);
            vp.mod2[std::size_t(n)] = v.num((s + "Mod2").c_str(), 0.0);
        }
        vp.randomDetune = v.num("kParamGlobalRandomOscDetune", 0.0) * (v.flag("kParamGlobalRandomOscDetune10x", false) ? 10.0 : 1.0);
        vp.randomPan = v.num("kParamGlobalRandomOscPan", 0.0); vp.randomEnvTime = v.num("kParamGlobalRandomEnvTime", 0.0);
        vp.randomCutoff = v.num("kParamGlobalRandomFilterCutoff", 0.0);
        vp.scalingEnvTime = v.num("kParamGlobalScalingEnvTime", 50.0); vp.scalingLfoTime = v.num("kParamGlobalScalingLfoTime", 50.0);
        patch.globals.envTimeScale = vp.scalingEnvTime; patch.globals.lfoTimeScale = vp.scalingLfoTime;
        vp.voiceCount = std::clamp(int(v.num("kParamVoiceCount", 8.0)), 1, 8);
        v.mark("kParamGlobalScalingLfoTimeSnap"); v.mark("kParamOscA"); v.mark("kParamOscB"); v.mark("kParamOscC");
        v.mark("kParamOscN"); v.mark("kParamOscS");
    }

    // ------------------------------------------------------------- oscillators
    for (int i = 0; i < 5; ++i) {
        auto& osc = patch.oscillators[std::size_t(i)];
        const auto key = "Oscillator" + std::to_string(i);
        const Json& o = obj(doc.state, key);
        Mod m{params(o), key, t};
        osc.additional = o;
        osc.enabled = m.flag("kParamEnable", i == 0);
        osc.modeId = m.str("kParamType", i == 3 ? "kOsc_Noise" : i == 4 ? "kOsc_Sub" : "kOsc_Wavetable");
        osc.mode = i == 3 ? OscMode::noise : i == 4 ? OscMode::sub : oscillatorMode(osc.modeId);
        osc.octave = int(m.num("kParamOctave", 0.0)); osc.semitone = m.num("kParamCoarsePit", m.num("kParamCoarse", 0.0));
        osc.pitch = m.num("kParamPitch", 0.0); osc.fine = m.num("kParamFine", 0.0);
        osc.volume = m.num("kParamVolume", 0.75);
        osc.pan = std::clamp(m.num("kParamPan", 0.0) / 50.0, -1.0, 1.0);
        osc.unison = std::clamp(int(std::lround(m.num("kParamUnison", 1.0))), 1, 16);
        osc.detune = m.num("kParamDetune", 0.2); osc.blend = m.num("kParamDetuneWid", 75.0);
        osc.unisonStereo = m.num("kParamUnisonStereo", 100.0); osc.unisonRange = m.num("kParamUnisonRange", 2.0);
        osc.detuneMode = detuneMode(m.str("kParamDetuneMode")); osc.unisonStack = unisonStack(m.str("kParamUnisonStack"));
        osc.unisonWarp = m.num("kParamUnisonWarp", 0.0); osc.unisonWarp2 = m.num("kParamUnisonWarp2", 0.0);
        osc.hzOffset = m.num("kParamHzOffset", 0.0);
        const auto pitchMode = m.str("kParamPitchMode");
        osc.pitchMode = pitchMode == "Ratio" ? OscPitchMode::ratio : pitchMode == "Harmonics" ? OscPitchMode::harmonics : OscPitchMode::semitones;
        osc.pitchRatio = m.num("kParamPitchRatio", 1.0);
        osc.pitchTrack = m.flag("kParamPitchTrack", true);
        osc.keyZoneLo = int(m.num("kParamKeyZoneMin", 0.0)); osc.keyZoneHi = int(m.num("kParamKeyZoneMax", 127.0));
        osc.velZoneLo = int(m.num("kParamVelocityZoneMin", 0.0)); osc.velZoneHi = int(m.num("kParamVelocityZoneMax", 127.0));
        // sample-family controls (shared by sample, multisample, granular and spectral modes)
        osc.start = m.num("kParamStart", 0.0); osc.end = m.num("kParamEnd", 100.0);
        osc.loopStart = m.num("kParamLoopStart", 0.0); osc.loopEnd = m.num("kParamLoopEnd", 100.0);
        osc.looping = m.has("kParamLoopMode");
        osc.loopMode = loopMode(m.str("kParamLoopMode"));
        osc.loopCrossfade = m.num("kParamLoopCrossfade", 0.0);
        osc.reverse = m.flag("kParamReverse", false);
        osc.randomStart = m.num("kParamRandomStart", 0.0); osc.position = m.num("kParamPosition", 0.0);
        osc.scanRate = m.num("kParamScanRate", 0.0); osc.scanRange = m.num("kParamScanRange", 1.0);
        osc.scanTempoLock = m.flag("kParamScanTempoLock", false); osc.baseTempo = m.num("kParamBaseTempo", 120.0);
        const auto slicing = m.str("kParamSlicingEnabled");
        osc.slicingMode = slicing == "Auto" ? 1 : slicing == "Manual" ? 2 : 0;
        if (o.contains("sliceMarkers") && o["sliceMarkers"].is_array())
            for (const auto& mk : o["sliceMarkers"]) if (mk.is_number()) osc.sliceMarkers.push_back(std::clamp(mk.get<double>(), 0.0, 1.0));
        for (const char* k : {"kParamUnisonSpan", "kParamKeyZoneWarp", "kParamPitchRatioModMode", "kParamPitchSource",
                              "kParamPitchBendTrack", "kParamLinkLoopLength", "kParamLoopStartLink", "kParamLoopEndsAtRelease",
                              "kParamManualPositionMode", "kParamScanBPMDivide", "kParamScanBPMRate", "kParamSlicingRootNote",
                              "kParamAutoSliceThreshold"})
            m.mark(k);

        const std::string modeKey = (osc.mode == OscMode::wavetable ? "WTOsc" : osc.mode == OscMode::sample ? "SampleOsc"
            : osc.mode == OscMode::multisample ? "MultiSampleOsc" : osc.mode == OscMode::granular ? "GranularOsc"
            : osc.mode == OscMode::noise ? "NoiseOsc" : osc.mode == OscMode::sub ? "SubOsc" : "SpectralOsc") + std::to_string(i);
        const Json& mode = obj(o, modeKey);
        Mod mm{params(mode), key + "." + modeKey, t};
        osc.modeState = mode;
        osc.tablePosition = mm.num("kParamTablePos", 0.0);
        osc.initialPhase = mm.num("kParamInitialPhase", 0.0);
        osc.randomPhase = mm.num("kParamRandomPhase", 0.0);
        osc.perVoicePhase = mm.str("kParamPhaseMemory") != "kContiguous";
        osc.unisonWTPos = mm.num("kParamUnisonWTPos", 0.0);
        mm.mark("kParamXfadeMode");
        if (osc.mode == OscMode::sub) {
            osc.subShape = subShape(mm.str("kParamShape"));
            osc.contiguousPhase = mm.flag("kParamContiguousPhase", false);
        }
        if (osc.mode == OscMode::noise) {
            osc.noiseTypeExplicit = mm.has("kParamNoiseType");
            osc.noiseType = noiseType(mm.str("kParamNoiseType"));
            osc.noiseColor = mm.num("kParamColor", 0.5);
            osc.oneShot = mm.flag("kParamOneShot", false);
            osc.fine = mm.num("kParamFine", osc.fine);
        }
        if (osc.mode == OscMode::multisample) {
            osc.sampleEnv.attack = mm.num("kParamEnvAttack", 0.0); osc.sampleEnv.decay = mm.num("kParamEnvDecay", 0.0);
            osc.sampleEnv.sustain = mm.num("kParamEnvSustain", 1.0); osc.sampleEnv.release = mm.num("kParamEnvRelease", 0.05);
            osc.sampleEnv.hold = mm.num("kParamEnvHold", 0.0); osc.sampleEnv.delay = mm.num("kParamEnvDelay", 0.0);
            osc.sampleEnv.override_ = mm.flag("kParamEnvOverride", false);
            osc.velTrack = mm.num("kParamVelTrack", 0.0); osc.velTrackOverride = mm.flag("kParamVelTrackOverride", false);
            osc.timbreShift = mm.num("kParamTimbreShift", 0.0);
        }
        if (osc.mode == OscMode::granular) {
            osc.density = mm.num("kParamDensity", 20.0); osc.densityBpm = mm.str("kParamDensityMode") == "kDensityBPM";
            osc.grainLength = mm.num("kParamGrainLength", 0.1); osc.lengthBpm = mm.str("kParamLengthMode") == "kLengthBPM";
            osc.windowShape = windowShape(mm.str("kParamWindowShape"));
            osc.windowParam = mm.num("kParamWindowParam", 50.0); osc.windowSkew = mm.num("kParamWindowSkew", 0.0);
            osc.randomOffset = mm.num("kParamRandomOffset", 0.0); osc.randomDir = mm.num("kParamRandomDir", 0.0);
            osc.randomGain = mm.num("kParamRandomGain", 0.0); osc.randomPan = mm.num("kParamRandomPan", 0.0);
            osc.randomPitch = mm.num("kParamRandomPitch", 0.0); osc.randomGrainLength = mm.num("kParamRandomGrainLength", 0.0);
            osc.randomWarp = mm.num("kParamRandomWarp", 0.0); osc.randomWarp2 = mm.num("kParamRandomWarp2", 0.0);
            osc.randomWindowAmount = mm.num("kParamRandomWindowAmount", 0.0); osc.randomWindowSkew = mm.num("kParamRandomWindowSkew", 0.0);
            const auto trig = mm.str("kParamUnisonTrigPattern");
            osc.unisonTrigPattern = trig == "kExponential" ? 1 : trig == "kRandom" ? 2 : 0;
            mm.mark("kParamYAxisAssignment");
        }
        if (osc.mode == OscMode::spectral) {
            osc.freqLo = mm.num("kParamFreqLo", 20.0); osc.freqHi = mm.num("kParamFreqHi", 20000.0);
            osc.loHiSmooth = mm.flag("kParamLoHiIsSmooth", false); osc.phaseLock = mm.flag("kParamPhaseLock", false);
            osc.keepTransients = mm.flag("kParamTransients", false);
            osc.specFilterShift = mm.num("kParamSpecFltShift", 0.0); osc.specFilterWet = mm.num("kParamSpecFltWetDry", 0.0);
        }
        osc.warpOne = mm.str("kParamWarpMenu", mm.str("kParamWarpMode"));
        osc.warpTwo = mm.str("kParamWarpMenu2", mm.str("kParamWarpMode2"));
        osc.warpOneAmount = mm.num("kParamWarp", 0.0); osc.warpTwoAmount = mm.num("kParamWarp2", 0.0);
        osc.warpDefinitions[0] = warpDefinition(osc.warpOne, i); osc.warpDefinitions[1] = warpDefinition(osc.warpTwo, i);
        osc.warpDefinitions[0].var = mm.num("kParamWarpVar", 0.5); osc.warpDefinitions[1].var = mm.num("kParamWarpVar2", 0.5);
        for (int slot = 0; slot < 2; ++slot) {
            const auto& id = slot == 0 ? osc.warpOne : osc.warpTwo;
            if (!id.empty() && osc.warpDefinitions[std::size_t(slot)].mode == WarpMode::unknown)
                diagnostic(patch, key + "." + modeKey + ".Warp" + std::to_string(slot + 1), "not_rendered", "warp " + id + " has no native DSP");
        }
        // asset references
        if (osc.mode == OscMode::wavetable) {
            osc.asset = string(mode, "relativePathToWT");
            // Serum's initial table carries no path in the preset.
            if (osc.asset.empty() && !mode.contains("embeddedWTData")) osc.asset = "S2 Tables/Default Shapes.wav";
        }
        else if (osc.mode == OscMode::noise) osc.asset = string(mode, "relativePathToNoiseSample");
        else if (osc.mode == OscMode::multisample) {
            osc.asset = string(mode, "sfzPathRelative"); osc.embeddedSfz = string(mode, "embedded_sfz");
        } else if (osc.mode == OscMode::sample || osc.mode == OscMode::granular || osc.mode == OscMode::spectral) {
            osc.asset = string(mode, "samplePathRelative");
            osc.baseNote = int(number(mode, "baseNote", 60.0));
        }
        if (osc.mode == OscMode::wavetable && mode.contains("embeddedWTData") && mode["embeddedWTData"].is_array()) {
            // Tables authored in Serum's editor travel inside the preset as raw frames.
            const auto& raw = mode["embeddedWTData"];
            const std::size_t frames = raw.size() / osc.frameSize;
            if (frames >= 1) {
                osc.audio.resize(frames * osc.frameSize);
                for (std::size_t k = 0; k < osc.audio.size(); ++k) {
                    const double v = raw[k].is_number() ? raw[k].get<double>() : 0.0;
                    osc.audio[k] = std::isfinite(v) ? float(v) : 0.0f;
                }
                prepareWavetableMipmaps(osc);
                osc.asset.clear();
                osc.tableName = string(mode, "tableDisplayName");
                if (osc.tableName.empty()) osc.tableName = "Embedded";
            }
        }
        if (osc.enabled && osc.mode == OscMode::wavetable && !osc.asset.empty() && !root.empty()) {
            std::string error;
            const auto path = resolveSerumAsset(root, "Tables", osc.asset);
            if (path.empty()) diagnostic(patch, "Oscillator" + std::to_string(i), "missing_asset", osc.asset);
            else if (!loadWavetableFromFile(osc, path, error))
                diagnostic(patch, "Oscillator" + std::to_string(i), "unsupported_asset", error + ": " + osc.asset);
        } else if (osc.enabled && osc.mode == OscMode::wavetable && osc.asset.empty() && osc.audio.empty())
            diagnostic(patch, "Oscillator" + std::to_string(i), "missing_asset", "no wavetable reference");
        else if (osc.enabled && root.empty() && !osc.asset.empty() && osc.mode != OscMode::sub)
            diagnostic(patch, "Oscillator" + std::to_string(i), "missing_asset", "content root not set: " + osc.asset);
        if (osc.enabled && osc.mode != OscMode::wavetable && osc.mode != OscMode::sub && osc.mode != OscMode::noise
            && osc.asset.empty() && osc.embeddedSfz.empty())
            diagnostic(patch, "Oscillator" + std::to_string(i), "missing_asset", "no audio asset reference");
    }
    // Serum's default NOISE source (no sample selected) is the generator itself
    if (patch.oscillators[3].asset.empty()) patch.oscillators[3].noiseTypeExplicit = true;

    // ----------------------------------------------------------------- filters
    for (int i = 0; i < 2; ++i) {
        const auto key = "VoiceFilter" + std::to_string(i);
        const Json& fj = obj(doc.state, key);
        Mod m{params(fj), key, t};
        auto& f = patch.filters[std::size_t(i)];
        f.additional = fj;
        f.enabled = m.flag("kParamEnable", false);
        f.type = m.str("kParamType", "L12");
        FilterResponse response = FilterResponse::low12; int variant = 0;
        const bool known = dsp::filterFromSerumId(f.type, response, variant);
        f.response = known ? response : FilterResponse::low12; f.variant = variant;
        f.cutoff = m.num("kParamFreq", 1.0); f.resonance = m.num("kParamReso", 10.0); f.drive = m.num("kParamDrive", 0.0);
        f.var = m.num("kParamVar", 0.0); f.wet = m.num("kParamWet", 100.0); f.level = m.num("kParamLevelOut", 0.5);
        f.stereo = m.num("kParamStereo", 50.0); f.x = m.num("kParamX", 0.5); f.y = m.num("kParamY", 0.5);
        f.keyTrack = m.flag("kParamKeyTrack", false); f.pad = m.flag("kParamPad", false);
        if (f.enabled) diagnostic(patch, key, known ? "dsp_active" : "not_rendered",
            known ? "typed native response for filter " + f.type : "filter " + f.type + " has no native member; low-pass fallback");
    }

    // --------------------------------------------------------------- envelopes
    for (int i = 0; i < 4; ++i) {
        const auto key = "Env" + std::to_string(i);
        const Json& ej = obj(doc.state, key);
        Mod m{params(ej), key, t};
        auto& e = patch.envelopes[std::size_t(i)];
        e.additional = ej;
        e.attack = m.num("kParamAttack", e.attack); e.hold = m.num("kParamHold", e.hold);
        e.decay = m.num("kParamDecay", e.decay); e.sustain = m.num("kParamSustain", e.sustain);
        e.release = m.num("kParamRelease", e.release);
        e.curve = {m.num("kParamCurve1", 50.0), m.num("kParamCurve2", 66.6), m.num("kParamCurve3", 66.6)};
        e.start = m.num("kParamStart", 0.0); e.end = m.num("kParamEnd", 0.0);
        e.legatoInverted = m.flag("kParamLegatoInverted", false);
        e.restartOnSteal = m.flag("kParamVoiceStealRestart", true);
        if (m.flag("kParamBeatSync", false)) {
            m.mark("kParamBeatSync");
            diagnostic(patch, key, "not_rendered_parameter", "tempo-synced envelope times are read as seconds; sync semantics unverified");
        }
    }

    // ------------------------------------------------------------ routing slots
    for (int i = 0; i < 7; ++i) {
        const auto key = "RoutingSlot" + std::to_string(i);
        const Json& rj = obj(doc.state, key);
        Mod m{params(rj), key, t};
        auto& route = patch.routes[std::size_t(i)];
        route.additional = rj;
        route.target = routing(m.str("kParamRoutingDest", i < 5 ? "kRoutingDestFilter" : "kRoutingDestMaster"));
        route.filterBalance = m.num("kParamFilterBalance", -100.0);
        route.fxBus1Level = m.num("kParamFXBus1Level", 0.0); route.fxBus2Level = m.num("kParamFXBus2Level", 0.0);
        for (int e = 0; e < 4; ++e) route.viaEnv[std::size_t(e)] = m.flag(("kParamViaEnv" + std::to_string(e + 1)).c_str(), e == 0);
    }
    if (patch.routes[5].target == RouteTarget::filter && patch.routes[6].target == RouteTarget::filter)
        diagnostic(patch, "RoutingSlot5..6", "not_rendered", "two-way filter feedback is not modeled; the backward edge uses Main fallback");

    // -------------------------------------------------------------------- LFOs
    patch.lfoOneSine = false;
    for (int i = 0; i < 10; ++i) {
        const auto key = "LFO" + std::to_string(i);
        patch.lfos[std::size_t(i)] = obj(doc.state, key);
        const Json& lj = patch.lfos[std::size_t(i)];
        Mod m{params(lj), key, t};
        auto& d = patch.lfoDefinitions[std::size_t(i)];
        const auto type = m.str("kParamType");
        if (type == "Lorenz") d.shape = LfoShape::lorenz;
        else if (type == "Rossler") d.shape = LfoShape::rossler;
        else if (type == "RandomSH") d.shape = LfoShape::randomHold;
        else if (type == "Path") d.shape = LfoShape::path;
        else d.shape = LfoShape::curve; // the default drawn-curve LFO
        const auto& pd = obj(lj, "pathData");
        const auto& cd = obj(lj, "curveData");
        if (d.shape == LfoShape::path && !pd.empty())
            d.path = curveFromJson(pd, !(pd.contains("isOpen") && pd["isOpen"].is_boolean() && pd["isOpen"].get<bool>()));
        else { d.path = curveFromJson(cd, true); if (d.path.empty() && d.shape == LfoShape::path) d.shape = LfoShape::curve; }
        const auto modeName = m.str("kParamMode");
        d.mode = modeName == "Free" ? LfoMode::free : modeName == "Envelope" ? LfoMode::envelope : modeName == "OneShot" ? LfoMode::oneShot : LfoMode::trigger;
        d.rateHz = m.num("kParamRate", 1.0);
        if (m.flag("kParamRate10x", false)) d.rateHz *= 10.0;
        d.tempoSync = false; // stored rates are read as Hz; see docs/DSP_DECISIONS.md
        d.phaseDegrees = m.num("kParamPhase", 0.0); d.rise = m.num("kParamRise", 0.0); d.delay = m.num("kParamDelay", 0.0);
        d.smooth = m.num("kParamSmooth", 0.0); d.mono = m.flag("kParamMono", false); d.anchored = m.flag("kParamAnchored", false);
        d.direction = int(m.num("kParamDirection", 0.0));
        for (const char* k : {"kParamBeatSync", "kParamDotted", "kParamTriplets", "kParamGridX", "kParamGridY", "kParamSwing",
                              "kParamPhaseSnap", "kParamDefaultMode"}) m.mark(k);
        if (m.has("kParamBeatSync") && number(params(lj), "kParamBeatSync", 0.0) != 0.0)
            diagnostic(patch, key, "not_rendered_parameter", "tempo-synced LFO rate is read as Hz; sync mapping unverified");
        if (!params(lj).empty() || !pd.empty() || !cd.empty()) diagnostic(patch, key, "dsp_active", "native LFO generator active");
    }

    // LFO point modulation buses: each bus animates one property of one drawn point.
    if (doc.state.contains("lfoPointModAssignments") && doc.state["lfoPointModAssignments"].is_array())
        for (const auto& a : doc.state["lfoPointModAssignments"]) {
            const int lfo = int(number(a, "lfoID", -1.0)), bus = int(number(a, "busID", -1.0));
            if (lfo < 0 || lfo >= 10 || bus < 0 || bus >= 16) continue;
            patch.lfoDefinitions[std::size_t(lfo)].pointMods.push_back({int(number(a, "pointID", 0.0)), bus, int(number(a, "target", 1.0))});
        }
    for (int b = 0; b < 16; ++b) Mod{params(obj(doc.state, "LFOPointModBus" + std::to_string(b))), "LFOPointModBus" + std::to_string(b), t}.mark("kParamValue");

    // ------------------------------------------------------------------ macros
    for (int i = 0; i < 8; ++i) {
        const auto key = "Macro" + std::to_string(i);
        patch.macros[std::size_t(i)] = obj(doc.state, key);
        Mod m{params(patch.macros[std::size_t(i)]), key, t};
        patch.macroValues[std::size_t(i)] = std::clamp(m.num("kParamValue", 0.0) / 100.0, 0.0, 1.0);
    }

    // --------------------------------------------------------------------- FX
    for (int rack = 0; rack < 3; ++rack) {
        const Json& r = obj(doc.state, "FXRack" + std::to_string(rack));
        const Json& fxs = r.contains("FX") ? r["FX"] : Json();
        if (!fxs.is_array()) continue;
        int pos = 0;
        for (const auto& item : fxs) {
            FxModule module;
            module.rack = rack; module.position = pos;
            if (item.is_object()) {
                for (auto it = item.begin(); it != item.end(); ++it) {
                    if (it.key().rfind("FX", 0) == 0 && it->is_object()) {
                        module.type = it.key(); module.parameters = params(*it);
                        module.fxType = fxTypeFromSerum(module.type);
                        Mod m{params(*it), "FXRack" + std::to_string(rack) + ".FX[" + std::to_string(pos) + "]." + module.type, t};
                        const auto table = fxParamTable(module.fxType);
                        for (std::size_t k = 0; k < table.size() && k < module.p.size(); ++k)
                            if (m.has(table[k].serumKey)) { module.p[k] = m.num(table[k].serumKey, table[k].def); module.set[k] = true; }
                        if (m.has("kParamLevelOut")) { module.p[fxLevelSlot] = m.num("kParamLevelOut", 0.5); module.set[fxLevelSlot] = true; }
                        if (m.has("kParamEnable")) module.enabled = m.flag("kParamEnable", true);
                        if (module.fxType == FxType::distortion) module.modeVariant = distortionMode(m.str("kParamMode"));
                        else if (module.fxType == FxType::reverb) module.modeVariant = reverbType(m.str("kParamType"));
                        else if (module.fxType == FxType::filter) {
                            const auto id = m.str("kParamType", "L12");
                            FilterResponse response = FilterResponse::low12; int variant = 0;
                            if (!dsp::filterFromSerumId(id, response, variant))
                                diagnostic(patch, m.path, "not_rendered", "FX filter " + id + " has no native member; low-pass fallback");
                            module.filterResponse = response; module.filterVariant = variant;
                        } else if (module.fxType == FxType::conv) module.impulsePath = string(*it, "relativePathToIR");
                        // parameters that only make sense with a tempo grid
                        for (const char* k : {"kParamBeatSync", "kParamPredelayBeatSync", "kParamPreDelayBeatSync", "kParamHQ",
                                              "kParamMinPhase", "kParamCompensatedWetDry", "kParamMonoInput", "kParamSwapAB", "kParamRetrig"})
                            m.mark(k);
                        for (int d = 0; d < 3; ++d) m.mark(("kParamDeadband" + std::to_string(d)).c_str());
                        if (module.fxType == FxType::unknown) diagnostic(patch, m.path, "not_rendered", "unknown FX class " + module.type);
                    }
                }
            }
            module.additional = item;
            patch.fx.push_back(std::move(module));
            const auto& added = patch.fx.back();
            diagnostic(patch, "FXRack" + std::to_string(rack), added.fxType == FxType::unknown ? "not_rendered" : "dsp_active",
                       "FX module " + added.type + (added.fxType == FxType::unknown ? " retained" : " native DSP active"));
            ++pos;
        }
    }

    // ------------------------------------------------------- arpeggiator & clips
    patch.arp = obj(doc.state, "Arp0");
    patch.clips = obj(doc.state, "Clip0");
    {
        Mod a{params(patch.arp), "Arp0", t};
        auto& as = patch.arpSettings;
        as.enabled = a.flag("kParamEnabled", false);
        as.activeClip = std::clamp(int(a.num("kParamActiveClipID", 0.0)), 0, 11);
        as.keyZoneMin = int(a.num("kParamKeyZoneMin", 0.0)); as.keyZoneMax = int(a.num("kParamKeyZoneMax", 127.0));
        a.mark("kParamLaunchQuantize"); a.mark("kParamMidiSelectOctave");
        for (int i = 0; i < 12; ++i) {
            const auto key = "ArpClip" + std::to_string(i);
            patch.arpClips[std::size_t(i)] = obj(doc.state, key);
            const Json& cj = patch.arpClips[std::size_t(i)];
            Mod c{params(cj), key, t};
            auto& d = as.clips[std::size_t(i)];
            d.rate = c.num("kParamRate", 0.25); d.dotted = c.flag("kParamDotted", false); d.triplet = c.flag("kParamTriplets", false);
            d.shape = c.str("kParamShape", "Up"); d.repeats = std::max(1, int(c.num("kParamRepeats", 1.0)));
            d.wrapRange = c.num("kParamWrapRange", 12.0); d.wrapMode = c.str("kParamRangeWrapMode");
            d.wrapPhantomNote = int(c.num("kParamWrapPhantomNote", 60.0));
            d.gate = c.num("kParamGate", 100.0); d.chance = c.num("kParamChance", 100.0);
            d.transpose = c.num("kParamTranspose", 0.0); d.transposeRange = std::clamp(int(c.num("kParamTransposeRange", 1.0)), 1, 3);
            d.transposeShape = c.str("kParamTransposeShape");
            d.retrigger = c.flag("kParamNoteRetrig", true);
            for (const char* k : {"kParamBeatRetrig", "kParamBeatSync", "kParamFirstNoteRetrig", "kParamLaunchRetrig", "kParamOffset",
                                  "kParamPlaybackMode", "kParamRetrigRate", "kParamStepAction", "kParamThru", "kParamTransposeShift",
                                  "kParamVeloDecay", "kParamVeloEnabled", "kParamVeloRetrig", "kParamVeloTarget", "kParamWrapTranspose"})
                c.mark(k);
            const Json& clip = cj.contains("clip") ? cj["clip"] : Json();
            if (clip.is_object()) {
                d.lengthBeats = number(clip, "regionEndBeats", 4.0);
                if (clip.contains("notes") && clip["notes"].is_array())
                    for (const auto& n : clip["notes"]) {
                        ArpStep step; step.time = number(n, "timeStamp", 0.0); step.length = number(n, "length", 0.25);
                        step.note = int(number(n, "noteNum", 0.0));
                        if (n.contains("attributes") && n["attributes"].is_array() && !n["attributes"].empty() && n["attributes"][0].is_number())
                            step.velocity = n["attributes"][0].get<double>();
                        d.steps.push_back(step);
                    }
                diagnostic(patch, key, "dsp_active", "arp clip pattern active");
            }
        }
    }
    {
        const Json& cp = obj(doc.state, "ClipPlayer0");
        Mod c{params(cp), "ClipPlayer0", t};
        auto& cs = patch.clipSettings;
        cs.enabled = c.flag("kParamEnabled", false);
        cs.selectOctave = int(c.num("kParamMidiSelectOctave", -3.0));
        for (const char* k : {"kParamMetronomeEnabled", "kParamMonoClipTrigger", "kParamRecordMode", "kParamSpanKeyboardClip"}) c.mark(k);
        for (int i = 0; i < 12; ++i) {
            const auto key = "MidiClip" + std::to_string(i);
            patch.midiClips[std::size_t(i)] = obj(doc.state, key);
            const Json& mj = patch.midiClips[std::size_t(i)];
            Mod m{params(mj), key, t};
            auto& d = cs.clips[std::size_t(i)];
            d.rate = m.num("kParamRate", 0.5) > 0.0 ? 0.5 / m.num("kParamRate", 0.5) : 1.0;
            d.playbackMode = m.str("kParamPlaybackMode", "OneShot"); d.spanMode = m.str("kParamSpanKeyboardMode", "Mono");
            d.retrigger = m.flag("kParamLaunchRetrig", true); d.noteGate = m.flag("kParamNoteGate", false);
            d.transpose = int(m.num("kParamTranspose", 0.0)); d.dotted = m.flag("kParamDotted", false); d.triplet = m.flag("kParamTriplets", false);
            for (const char* k : {"kParamBeatSync", "kParamLaunchQuantize", "kParamPlaybackModeTime", "kParamVelocityTrigger"}) m.mark(k);
            const Json& clip = mj.contains("clip") ? mj["clip"] : Json();
            if (clip.is_object()) {
                d.lengthBeats = number(clip, "regionEndBeats", number(mj, "displayLength_Beats", 16.0));
                if (clip.contains("notes") && clip["notes"].is_array())
                    for (const auto& n : clip["notes"]) {
                        ClipNote cn; cn.time = number(n, "timeStamp", 0.0); cn.length = number(n, "length", 1.0);
                        cn.note = int(number(n, "noteNum", 60.0));
                        if (n.contains("attributes") && n["attributes"].is_array() && !n["attributes"].empty() && n["attributes"][0].is_number())
                            cn.velocity = n["attributes"][0].get<double>();
                        d.notes.push_back(cn);
                    }
                diagnostic(patch, key, "dsp_active", "MIDI clip playback active");
            }
        }
    }

    // ------------------------------------------------- modulation matrix (last)
    for (int i = 0; i < 64; ++i) {
        const auto key = "ModSlot" + std::to_string(i);
        const Json& mj = obj(doc.state, key);
        if (!mj.contains("source") || !mj["source"].is_array() || mj["source"].empty()) continue;
        Mod m{params(mj), key, t};
        ModulationRoute route;
        route.slot = i;
        route.source = mj["source"][0].is_number_integer() ? mj["source"][0].get<int>() : 0;
        route.auxiliary = mj["source"].size() > 1 && mj["source"][1].is_number_integer() ? mj["source"][1].get<int>() : 0;
        route.sourceName = sourceName(route.source);
        mapSource(route.source, route.sourceKind, route.sourceIndex);
        if (route.auxiliary != 0) mapSource(route.auxiliary, route.auxKind, route.auxIndex);
        route.destinationModule = string(mj, "destModuleTypeString");
        route.destinationParameter = string(mj, "destModuleParamName");
        route.destinationInstance = int(number(mj, "destModuleID", 0.0));
        route.destinationParameterId = int(number(mj, "destModuleParamID", -1.0));
        mapDestination(patch, route);
        route.amount = m.num("kParamAmount", 0.0);
        route.bipolar = m.flag("kParamBipolar", false); route.bypass = m.flag("kParamBypass", false);
        route.curveIn = m.num("kParamCurveIn", 0.0); route.curveOut = m.num("kParamCurveOut", 0.0);
        route.auxInverted = m.flag("kParamAuxInverted", false);
        route.smoothRise = m.num("kParamSmoothRise", 0.0); route.smoothFall = m.num("kParamSmoothFall", 0.0);
        route.delaySeconds = m.num("kParamDelayOffset", 0.0);
        if (mj.contains("flex") && mj["flex"].is_array()) {
            const auto& flex = mj["flex"];
            if (flex.size() > 0 && m.flag("kParamMainCurveData", false)) route.mainCurve = curveFromJson(flex[0], false);
            if (flex.size() > 1 && m.flag("kParamAuxCurveData", false)) route.auxCurve = curveFromJson(flex[1], false);
        }
        m.mark("kParamMainCurveData"); m.mark("kParamAuxCurveData");
        for (const char* k : {"kParamAuxCurve", "kParamDelayBeatSync", "kParamOut", "kParamSmoothLink"}) m.mark(k);
        route.additional = mj;
        const bool srcOk = route.sourceKind != ModSource::unknown;
        const bool dstOk = route.targetKind != ModTarget::unknown;
        const bool auxOk = route.auxiliary == 0 || route.auxKind != ModSource::unknown;
        diagnostic(patch, key, srcOk && dstOk && auxOk ? "dsp_active" : "not_rendered",
            srcOk && dstOk && auxOk ? "native modulation route active"
                : !srcOk ? "modulation source " + std::to_string(route.source) + " is unresolved"
                : !dstOk ? "modulation destination " + route.destinationModule + "." + route.destinationParameter + " has no native target"
                         : "auxiliary source is unresolved");
        patch.modulation.push_back(std::move(route));
    }

    unmappedDiagnostics(patch, t.seen, doc.state, "");
    for (const auto& d : patch.diagnostics) (void) d;
    // All untyped or future fields remain in unknownSerumState. This sidecar is
    // preservation, not a semantic rendering claim.
    return patch;
}
}
