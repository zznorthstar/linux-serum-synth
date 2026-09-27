#include "Patch.h"
#include "FxParams.h"
#include "Wavetable.h"
#include <cmath>
#include <cstring>
#include <sstream>

namespace zyg {
namespace {
// Embedded wavetable frames (tables without an asset path) are stored as base64 little-endian float32.
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string encodeFrames(const std::vector<float>& v) {
    std::string bytes(v.size() * 4, '\0');
    for (std::size_t i = 0; i < v.size(); ++i) {
        std::uint32_t u; std::memcpy(&u, &v[i], 4);
        for (int b = 0; b < 4; ++b) bytes[i * 4 + std::size_t(b)] = char((u >> (8 * b)) & 0xffu);
    }
    std::string out; out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t n = (std::uint32_t(std::uint8_t(bytes[i])) << 16) |
            (i + 1 < bytes.size() ? std::uint32_t(std::uint8_t(bytes[i + 1])) << 8 : 0u) |
            (i + 2 < bytes.size() ? std::uint32_t(std::uint8_t(bytes[i + 2])) : 0u);
        out += kB64[(n >> 18) & 63]; out += kB64[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kB64[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kB64[n & 63] : '=';
    }
    return out;
}
std::vector<float> decodeFrames(const std::string& text) {
    std::string bytes; std::uint32_t acc = 0; int bits = 0;
    for (char c : text) {
        const char* at = std::strchr(kB64, c);
        if (c == '=' || !at || c == '\0') continue;
        acc = (acc << 6) | std::uint32_t(at - kB64); bits += 6;
        if (bits >= 8) { bits -= 8; bytes += char((acc >> bits) & 0xffu); }
    }
    std::vector<float> v(bytes.size() / 4);
    for (std::size_t i = 0; i < v.size(); ++i) {
        std::uint32_t u = 0;
        for (int b = 0; b < 4; ++b) u |= std::uint32_t(std::uint8_t(bytes[i * 4 + std::size_t(b)])) << (8 * b);
        float f; std::memcpy(&f, &u, 4);
        v[i] = std::isfinite(f) ? f : 0.0f;
    }
    return v;
}
}
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
using nlohmann::json;
// --- enum <-> string tables (first entry is the fallback for unknown names)
NLOHMANN_JSON_SERIALIZE_ENUM(WarpMode, {
    {WarpMode::unknown, "unknown"}, {WarpMode::off, "off"},
    {WarpMode::bendPositive, "bendPositive"}, {WarpMode::bendNegative, "bendNegative"}, {WarpMode::bendBoth, "bendBoth"},
    {WarpMode::asymPositive, "asymPositive"}, {WarpMode::asymNegative, "asymNegative"}, {WarpMode::asymBoth, "asymBoth"},
    {WarpMode::pwm, "pwm"}, {WarpMode::flip, "flip"}, {WarpMode::frequencyMod, "frequencyMod"}, {WarpMode::ringMod, "ringMod"},
    {WarpMode::amplitudeMod, "amplitudeMod"}, {WarpMode::hardClip, "hardClip"}, {WarpMode::softClip, "softClip"},
    {WarpMode::sineFold, "sineFold"}, {WarpMode::linearFold, "linearFold"}, {WarpMode::sineShaper, "sineShaper"},
    {WarpMode::asymmetricClip, "asymmetricClip"}, {WarpMode::rectify, "rectify"}, {WarpMode::phaseMod, "phaseMod"},
    {WarpMode::frequencyModX, "frequencyModX"}, {WarpMode::frequencyModPhase, "frequencyModPhase"}, {WarpMode::sync, "sync"},
    {WarpMode::remap, "remap"}, {WarpMode::quantize, "quantize"}, {WarpMode::evenOdd, "evenOdd"}, {WarpMode::selfPhase, "selfPhase"},
    {WarpMode::filterLow, "filterLow"}, {WarpMode::filterHigh, "filterHigh"}, {WarpMode::diode1, "diode1"}, {WarpMode::diode2, "diode2"},
    {WarpMode::softSat, "softSat"}, {WarpMode::tapeSat, "tapeSat"}, {WarpMode::tube, "tube"}, {WarpMode::stompBox, "stompBox"},
    {WarpMode::zeroSquare, "zeroSquare"}, {WarpMode::addHarmonics, "addHarmonics"}, {WarpMode::addSubharmonics, "addSubharmonics"},
    {WarpMode::spectralDetune, "spectralDetune"}, {WarpMode::gate, "gate"}, {WarpMode::mirror, "mirror"}, {WarpMode::smear, "smear"},
    {WarpMode::spectralComb, "spectralComb"}, {WarpMode::spectralPitchShift, "spectralPitchShift"},
    {WarpMode::spectralShift, "spectralShift"}, {WarpMode::spread, "spread"}, {WarpMode::shepardFilter, "shepardFilter"},
    {WarpMode::shepardNarrow, "shepardNarrow"}, {WarpMode::peakOctaveDown, "peakOctaveDown"}, {WarpMode::peakOctaveUp, "peakOctaveUp"},
    {WarpMode::peakHarmonicDown, "peakHarmonicDown"}, {WarpMode::peakHarmonicUp, "peakHarmonicUp"},
    {WarpMode::phaseTwist, "phaseTwist"}, {WarpMode::vocode, "vocode"}, {WarpMode::mask, "mask"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LfoShape, {{LfoShape::unknown, "unknown"}, {LfoShape::sine, "sine"}, {LfoShape::lorenz, "lorenz"},
    {LfoShape::rossler, "rossler"}, {LfoShape::randomHold, "randomHold"}, {LfoShape::path, "path"}, {LfoShape::curve, "curve"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LfoMode, {{LfoMode::free, "free"}, {LfoMode::trigger, "trigger"}, {LfoMode::envelope, "envelope"}, {LfoMode::oneShot, "oneShot"}})
NLOHMANN_JSON_SERIALIZE_ENUM(FilterResponse, {
    {FilterResponse::unknown, "unknown"}, {FilterResponse::low6, "low6"}, {FilterResponse::low12, "low12"}, {FilterResponse::low18, "low18"},
    {FilterResponse::low24, "low24"}, {FilterResponse::high6, "high6"}, {FilterResponse::high12, "high12"}, {FilterResponse::high18, "high18"},
    {FilterResponse::high24, "high24"}, {FilterResponse::band12, "band12"}, {FilterResponse::band24, "band24"},
    {FilterResponse::notch12, "notch12"}, {FilterResponse::notch24, "notch24"}, {FilterResponse::ladder6, "ladder6"},
    {FilterResponse::ladder18, "ladder18"}, {FilterResponse::ladder24, "ladder24"}, {FilterResponse::ladder12, "ladder12"},
    {FilterResponse::ladderDirty, "ladderDirty"}, {FilterResponse::ladderEms, "ladderEms"}, {FilterResponse::ladderAcid, "ladderAcid"},
    {FilterResponse::multi, "multi"}, {FilterResponse::comb, "comb"}, {FilterResponse::flange, "flange"}, {FilterResponse::phaser, "phaser"},
    {FilterResponse::formant, "formant"}, {FilterResponse::allpass, "allpass"}, {FilterResponse::diffuser, "diffuser"},
    {FilterResponse::djMixer, "djMixer"}, {FilterResponse::shelfEq, "shelfEq"}, {FilterResponse::polezero, "polezero"},
    {FilterResponse::exponential, "exponential"}, {FilterResponse::screamer, "screamer"}, {FilterResponse::waveshaper, "waveshaper"},
    {FilterResponse::ringMod, "ringMod"}, {FilterResponse::sampleHold, "sampleHold"}, {FilterResponse::addBass, "addBass"},
    {FilterResponse::zdfAnalog, "zdfAnalog"}, {FilterResponse::distComb, "distComb"}, {FilterResponse::filterReverb, "filterReverb"},
    {FilterResponse::bandReject, "bandReject"}, {FilterResponse::peaking, "peaking"}})
NLOHMANN_JSON_SERIALIZE_ENUM(SubShape, {{SubShape::unknown, "unknown"}, {SubShape::pulse, "pulse"}, {SubShape::roundedRectangle, "roundedRectangle"},
    {SubShape::saw, "saw"}, {SubShape::square, "square"}, {SubShape::triangle, "triangle"}})
NLOHMANN_JSON_SERIALIZE_ENUM(NoiseType, {{NoiseType::white, "white"}, {NoiseType::pink, "pink"}, {NoiseType::brown, "brown"}, {NoiseType::geiger, "geiger"}})
NLOHMANN_JSON_SERIALIZE_ENUM(DetuneMode, {{DetuneMode::linear, "linear"}, {DetuneMode::exponential, "exponential"}, {DetuneMode::inverse, "inverse"},
    {DetuneMode::random, "random"}, {DetuneMode::super, "super"}})
NLOHMANN_JSON_SERIALIZE_ENUM(UnisonStack, {{UnisonStack::none, "none"}, {UnisonStack::center12, "center12"}, {UnisonStack::octave1, "octave1"},
    {UnisonStack::octave2, "octave2"}, {UnisonStack::octave3, "octave3"}, {UnisonStack::octaveFifth1, "octaveFifth1"},
    {UnisonStack::octaveFifth2, "octaveFifth2"}, {UnisonStack::octaveFifth3, "octaveFifth3"}})
NLOHMANN_JSON_SERIALIZE_ENUM(LoopMode, {{LoopMode::forward, "forward"}, {LoopMode::reverse, "reverse"}, {LoopMode::pingPong, "pingPong"}, {LoopMode::tailed, "tailed"}})
NLOHMANN_JSON_SERIALIZE_ENUM(WindowShape, {{WindowShape::gaussian, "gaussian"}, {WindowShape::blackmanHarris, "blackmanHarris"},
    {WindowShape::expDecay, "expDecay"}, {WindowShape::triangle, "triangle"}, {WindowShape::tukey, "tukey"}})
NLOHMANN_JSON_SERIALIZE_ENUM(VoicePriority, {{VoicePriority::latest, "latest"}, {VoicePriority::low, "low"}, {VoicePriority::high, "high"}})
NLOHMANN_JSON_SERIALIZE_ENUM(OscPitchMode, {{OscPitchMode::semitones, "semitones"}, {OscPitchMode::harmonics, "harmonics"}, {OscPitchMode::ratio, "ratio"}})
NLOHMANN_JSON_SERIALIZE_ENUM(FxType, {{FxType::unknown, "unknown"}, {FxType::bode, "bode"}, {FxType::chorus, "chorus"}, {FxType::comp, "comp"},
    {FxType::conv, "conv"}, {FxType::delay, "delay"}, {FxType::distortion, "distortion"}, {FxType::eq, "eq"}, {FxType::filter, "filter"},
    {FxType::flanger, "flanger"}, {FxType::hyperD, "hyperD"}, {FxType::phaser, "phaser"}, {FxType::reverb, "reverb"}, {FxType::utils, "utils"},
    {FxType::split, "split"}, {FxType::split3, "split3"}, {FxType::splitMS, "splitMS"}, {FxType::pump, "pump"}, {FxType::stutter, "stutter"}})
NLOHMANN_JSON_SERIALIZE_ENUM(ModSource, {{ModSource::unknown, "unknown"}, {ModSource::envelope, "envelope"}, {ModSource::lfo, "lfo"},
    {ModSource::macro, "macro"}, {ModSource::velocity, "velocity"}, {ModSource::note, "note"}, {ModSource::modWheel, "modWheel"},
    {ModSource::channelPressure, "channelPressure"}, {ModSource::polyPressure, "polyPressure"}, {ModSource::noiseAudio, "noiseAudio"},
    {ModSource::random, "random"}, {ModSource::alternate, "alternate"}, {ModSource::pitchBend, "pitchBend"}, {ModSource::mpeX, "mpeX"},
    {ModSource::mpeY, "mpeY"}, {ModSource::mpeZ, "mpeZ"}, {ModSource::releaseVelocity, "releaseVelocity"}, {ModSource::fixed, "fixed"},
    {ModSource::oscAudio, "oscAudio"}, {ModSource::filterAudio, "filterAudio"}, {ModSource::activeVoices, "activeVoices"},
    {ModSource::voiceMod, "voiceMod"}, {ModSource::voiceIndex, "voiceIndex"}, {ModSource::discreteRandom, "discreteRandom"}, {ModSource::sidechain, "sidechain"}})
NLOHMANN_JSON_SERIALIZE_ENUM(ModTarget, {{ModTarget::unknown, "unknown"}, {ModTarget::wavetablePosition, "wavetablePosition"},
    {ModTarget::warpOneAmount, "warpOneAmount"}, {ModTarget::warpTwoAmount, "warpTwoAmount"}, {ModTarget::filterCutoff, "filterCutoff"},
    {ModTarget::oscParam, "oscParam"}, {ModTarget::filterParam, "filterParam"}, {ModTarget::envParam, "envParam"},
    {ModTarget::lfoParam, "lfoParam"}, {ModTarget::macroValue, "macroValue"}, {ModTarget::globalParam, "globalParam"},
    {ModTarget::fxParam, "fxParam"}, {ModTarget::arpParam, "arpParam"}, {ModTarget::clipParam, "clipParam"},
    {ModTarget::routingParam, "routingParam"}, {ModTarget::lfoPointBus, "lfoPointBus"}})

template <class T> void rd(const Json& j, const char* key, T& value) {
    if (!j.is_object() || !j.contains(key)) return;
    try { value = j.at(key).get<T>(); } catch (const std::exception&) {}
}

Json curveToJson(const CurvePoints& c) { return Json{{"x", c.x}, {"y", c.y}, {"bend", c.bend}, {"closed", c.closed}}; }
CurvePoints curveFromJson(const Json& j) {
    CurvePoints c;
    rd(j, "x", c.x); rd(j, "y", c.y); rd(j, "bend", c.bend); rd(j, "closed", c.closed);
    const auto n = std::min(c.x.size(), c.y.size());
    c.x.resize(n); c.y.resize(n);
    return c;
}

Json warpToJson(const WarpDefinition& w) { return Json{{"mode", w.mode}, {"sourceIndex", w.sourceIndex}, {"variant", w.variant}, {"var", w.var}}; }
void warpFromJson(const Json& j, WarpDefinition& w) {
    w.mode = WarpMode::unknown;
    rd(j, "mode", w.mode); rd(j, "sourceIndex", w.sourceIndex); rd(j, "variant", w.variant); rd(j, "var", w.var);
}

Json oscillatorToJson(const Oscillator& o) {
    Json j{
        {"enabled", o.enabled}, {"mode", oscModeToString(o.mode)}, {"modeId", o.modeId}, {"subShape", o.subShape},
        {"asset", o.asset}, {"tableName", o.tableName}, {"userSelectedAsset", o.userSelectedAsset}, {"octave", o.octave}, {"semitone", o.semitone},
        {"pitch", o.pitch}, {"unison", o.unison}, {"fine", o.fine}, {"volume", o.volume}, {"pan", o.pan}, {"detune", o.detune},
        {"tablePosition", o.tablePosition}, {"initialPhase", o.initialPhase}, {"randomPhase", o.randomPhase},
        {"warpOne", o.warpOne}, {"warpTwo", o.warpTwo}, {"warpOneAmount", o.warpOneAmount}, {"warpTwoAmount", o.warpTwoAmount},
        {"warpDefinitions", Json::array({warpToJson(o.warpDefinitions[0]), warpToJson(o.warpDefinitions[1])})},
        {"frameSize", o.frameSize}, {"sampleRate", o.sampleRate}, {"modeState", o.modeState}, {"additional", o.additional},
        {"blend", o.blend}, {"unisonStereo", o.unisonStereo}, {"unisonRange", o.unisonRange}, {"detuneMode", o.detuneMode},
        {"unisonStack", o.unisonStack}, {"unisonWTPos", o.unisonWTPos}, {"unisonWarp", o.unisonWarp}, {"unisonWarp2", o.unisonWarp2},
        {"perVoicePhase", o.perVoicePhase}, {"pitchTrack", o.pitchTrack}, {"hzOffset", o.hzOffset}, {"pitchMode", o.pitchMode},
        {"pitchRatio", o.pitchRatio}, {"keyZoneLo", o.keyZoneLo}, {"keyZoneHi", o.keyZoneHi}, {"velZoneLo", o.velZoneLo},
        {"velZoneHi", o.velZoneHi}, {"noiseType", o.noiseType}, {"noiseColor", o.noiseColor}, {"noiseTypeExplicit", o.noiseTypeExplicit},
        {"oneShot", o.oneShot}, {"contiguousPhase", o.contiguousPhase}, {"baseNote", o.baseNote}, {"embeddedSfz", o.embeddedSfz},
        {"childFiles", o.childFiles}, {"start", o.start}, {"end", o.end}, {"loopStart", o.loopStart}, {"loopEnd", o.loopEnd},
        {"loopCrossfade", o.loopCrossfade}, {"loopMode", o.loopMode}, {"looping", o.looping}, {"reverse", o.reverse},
        {"randomStart", o.randomStart}, {"position", o.position}, {"scanRate", o.scanRate}, {"scanRange", o.scanRange},
        {"scanTempoLock", o.scanTempoLock}, {"timbreShift", o.timbreShift}, {"velTrack", o.velTrack}, {"velTrackOverride", o.velTrackOverride},
        {"sampleEnv", Json{{"delay", o.sampleEnv.delay}, {"attack", o.sampleEnv.attack}, {"hold", o.sampleEnv.hold}, {"decay", o.sampleEnv.decay},
                           {"sustain", o.sampleEnv.sustain}, {"release", o.sampleEnv.release}, {"override", o.sampleEnv.override_},
                           {"useSfzRelease", o.sampleEnv.useSfzRelease}}},
        {"density", o.density}, {"densityBpm", o.densityBpm}, {"grainLength", o.grainLength}, {"lengthBpm", o.lengthBpm},
        {"windowShape", o.windowShape}, {"windowParam", o.windowParam}, {"windowSkew", o.windowSkew}, {"randomOffset", o.randomOffset},
        {"randomDir", o.randomDir}, {"randomGain", o.randomGain}, {"randomPan", o.randomPan}, {"randomPitch", o.randomPitch},
        {"randomGrainLength", o.randomGrainLength}, {"randomWarp", o.randomWarp}, {"randomWarp2", o.randomWarp2},
        {"randomWindowAmount", o.randomWindowAmount}, {"randomWindowSkew", o.randomWindowSkew},
        {"unisonTrigPattern", o.unisonTrigPattern}, {"freqLo", o.freqLo}, {"freqHi", o.freqHi}, {"loHiSmooth", o.loHiSmooth},
        {"phaseLock", o.phaseLock}, {"keepTransients", o.keepTransients}, {"specFilterShift", o.specFilterShift},
        {"specFilterWet", o.specFilterWet}, {"slicingMode", o.slicingMode}, {"baseTempo", o.baseTempo},
        {"sliceMarkers", o.sliceMarkers}, {"sliceRoot", o.sliceRoot}};
    // Tables with no asset path (Serum editor "Custom" tables) only exist inside the patch: keep their frames.
    if (o.mode == OscMode::wavetable && o.asset.empty() && !o.audio.empty()) j["embeddedFrames"] = encodeFrames(o.audio);
    return j;
}
void oscillatorFromJson(const Json& j, Oscillator& o) {
    if (!j.is_object()) return;
    rd(j, "enabled", o.enabled);
    if (j.contains("mode") && j["mode"].is_string()) o.mode = oscModeFromString(j["mode"].get<std::string>());
    rd(j, "modeId", o.modeId); rd(j, "subShape", o.subShape); rd(j, "asset", o.asset); rd(j, "tableName", o.tableName); rd(j, "userSelectedAsset", o.userSelectedAsset);
    rd(j, "octave", o.octave); rd(j, "semitone", o.semitone); rd(j, "pitch", o.pitch); rd(j, "unison", o.unison); rd(j, "fine", o.fine);
    rd(j, "volume", o.volume); rd(j, "pan", o.pan); rd(j, "detune", o.detune); rd(j, "tablePosition", o.tablePosition);
    rd(j, "initialPhase", o.initialPhase); rd(j, "randomPhase", o.randomPhase); rd(j, "warpOne", o.warpOne); rd(j, "warpTwo", o.warpTwo);
    rd(j, "warpOneAmount", o.warpOneAmount); rd(j, "warpTwoAmount", o.warpTwoAmount);
    if (j.contains("warpDefinitions") && j["warpDefinitions"].is_array())
        for (std::size_t i = 0; i < o.warpDefinitions.size() && i < j["warpDefinitions"].size(); ++i) warpFromJson(j["warpDefinitions"][i], o.warpDefinitions[i]);
    rd(j, "frameSize", o.frameSize); rd(j, "sampleRate", o.sampleRate);
    if (j.contains("embeddedFrames") && j["embeddedFrames"].is_string()) {
        o.audio = decodeFrames(j["embeddedFrames"].get<std::string>());
        if (o.frameSize == 0 || o.audio.size() % o.frameSize != 0) o.audio.clear();
        else prepareWavetableMipmaps(o);
    }
    o.modeState = j.value("modeState", Json{}); o.additional = j.value("additional", Json{});
    rd(j, "blend", o.blend); rd(j, "unisonStereo", o.unisonStereo); rd(j, "unisonRange", o.unisonRange); rd(j, "detuneMode", o.detuneMode);
    rd(j, "unisonStack", o.unisonStack); rd(j, "unisonWTPos", o.unisonWTPos); rd(j, "unisonWarp", o.unisonWarp); rd(j, "unisonWarp2", o.unisonWarp2);
    rd(j, "perVoicePhase", o.perVoicePhase); rd(j, "pitchTrack", o.pitchTrack); rd(j, "hzOffset", o.hzOffset); rd(j, "pitchMode", o.pitchMode);
    rd(j, "pitchRatio", o.pitchRatio); rd(j, "keyZoneLo", o.keyZoneLo); rd(j, "keyZoneHi", o.keyZoneHi); rd(j, "velZoneLo", o.velZoneLo);
    rd(j, "velZoneHi", o.velZoneHi); rd(j, "noiseType", o.noiseType); rd(j, "noiseColor", o.noiseColor); rd(j, "noiseTypeExplicit", o.noiseTypeExplicit);
    rd(j, "oneShot", o.oneShot); rd(j, "contiguousPhase", o.contiguousPhase); rd(j, "baseNote", o.baseNote); rd(j, "embeddedSfz", o.embeddedSfz);
    rd(j, "childFiles", o.childFiles); rd(j, "start", o.start); rd(j, "end", o.end); rd(j, "loopStart", o.loopStart); rd(j, "loopEnd", o.loopEnd);
    rd(j, "loopCrossfade", o.loopCrossfade); rd(j, "loopMode", o.loopMode); rd(j, "looping", o.looping); rd(j, "reverse", o.reverse);
    rd(j, "randomStart", o.randomStart); rd(j, "position", o.position); rd(j, "scanRate", o.scanRate); rd(j, "scanRange", o.scanRange);
    rd(j, "scanTempoLock", o.scanTempoLock); rd(j, "timbreShift", o.timbreShift); rd(j, "velTrack", o.velTrack); rd(j, "velTrackOverride", o.velTrackOverride);
    if (j.contains("sampleEnv")) {
        const auto& e = j["sampleEnv"];
        rd(e, "delay", o.sampleEnv.delay); rd(e, "attack", o.sampleEnv.attack); rd(e, "hold", o.sampleEnv.hold); rd(e, "decay", o.sampleEnv.decay);
        rd(e, "sustain", o.sampleEnv.sustain); rd(e, "release", o.sampleEnv.release); rd(e, "override", o.sampleEnv.override_);
        rd(e, "useSfzRelease", o.sampleEnv.useSfzRelease);
    }
    rd(j, "density", o.density); rd(j, "densityBpm", o.densityBpm); rd(j, "grainLength", o.grainLength); rd(j, "lengthBpm", o.lengthBpm);
    rd(j, "windowShape", o.windowShape); rd(j, "windowParam", o.windowParam); rd(j, "windowSkew", o.windowSkew); rd(j, "randomOffset", o.randomOffset);
    rd(j, "randomDir", o.randomDir); rd(j, "randomGain", o.randomGain); rd(j, "randomPan", o.randomPan); rd(j, "randomPitch", o.randomPitch);
    rd(j, "randomGrainLength", o.randomGrainLength); rd(j, "randomWarp", o.randomWarp); rd(j, "randomWarp2", o.randomWarp2);
    rd(j, "randomWindowAmount", o.randomWindowAmount); rd(j, "randomWindowSkew", o.randomWindowSkew);
    rd(j, "unisonTrigPattern", o.unisonTrigPattern); rd(j, "freqLo", o.freqLo); rd(j, "freqHi", o.freqHi); rd(j, "loHiSmooth", o.loHiSmooth);
    rd(j, "phaseLock", o.phaseLock); rd(j, "keepTransients", o.keepTransients); rd(j, "specFilterShift", o.specFilterShift);
    rd(j, "specFilterWet", o.specFilterWet); rd(j, "slicingMode", o.slicingMode); rd(j, "baseTempo", o.baseTempo);
    rd(j, "sliceMarkers", o.sliceMarkers); rd(j, "sliceRoot", o.sliceRoot);
}

Json filterToJson(const Filter& f) {
    return Json{{"enabled", f.enabled}, {"type", f.type}, {"response", f.response}, {"variant", f.variant}, {"cutoff", f.cutoff},
                {"resonance", f.resonance}, {"drive", f.drive}, {"var", f.var}, {"wet", f.wet}, {"level", f.level}, {"stereo", f.stereo},
                {"x", f.x}, {"y", f.y}, {"keyTrack", f.keyTrack}, {"pad", f.pad}, {"additional", f.additional}};
}
void filterFromJson(const Json& j, Filter& f) {
    if (!j.is_object()) return;
    rd(j, "enabled", f.enabled); rd(j, "type", f.type); rd(j, "response", f.response); rd(j, "variant", f.variant); rd(j, "cutoff", f.cutoff);
    rd(j, "resonance", f.resonance); rd(j, "drive", f.drive); rd(j, "var", f.var); rd(j, "wet", f.wet); rd(j, "level", f.level);
    rd(j, "stereo", f.stereo); rd(j, "x", f.x); rd(j, "y", f.y); rd(j, "keyTrack", f.keyTrack); rd(j, "pad", f.pad);
    f.additional = j.value("additional", Json{});
}
Json routeToJson(const Route& r) {
    return Json{{"target", routeTargetToString(r.target)}, {"filterBalance", r.filterBalance}, {"filterBalanceScale", 2},
                {"fxBus1Level", r.fxBus1Level}, {"fxBus2Level", r.fxBus2Level}, {"viaEnv", r.viaEnv}, {"additional", r.additional}};
}
void routeFromJson(const Json& j, Route& r) {
    if (!j.is_object()) return;
    if (j.contains("target") && j["target"].is_string()) r.target = routeTargetFromString(j["target"].get<std::string>());
    if (j.contains("filterBalance")) {
        rd(j, "filterBalance", r.filterBalance);
        // Earlier native state stored 0 = Filter 1 .. 100 = Filter 2; the current scale is -100..100.
        if (!j.contains("filterBalanceScale")) r.filterBalance = 2.0 * r.filterBalance - 100.0;
    }
    rd(j, "fxBus1Level", r.fxBus1Level); rd(j, "fxBus2Level", r.fxBus2Level); rd(j, "viaEnv", r.viaEnv);
    r.additional = j.value("additional", Json{});
}
Json envelopeToJson(const Envelope& e) {
    return Json{{"attack", e.attack}, {"hold", e.hold}, {"decay", e.decay}, {"sustain", e.sustain}, {"release", e.release},
                {"curve", e.curve}, {"start", e.start}, {"end", e.end}, {"legatoInverted", e.legatoInverted}, {"restartOnSteal", e.restartOnSteal}, {"additional", e.additional}};
}
void envelopeFromJson(const Json& j, Envelope& e) {
    if (!j.is_object()) return;
    rd(j, "attack", e.attack); rd(j, "hold", e.hold); rd(j, "decay", e.decay); rd(j, "sustain", e.sustain); rd(j, "release", e.release);
    rd(j, "curve", e.curve); rd(j, "start", e.start); rd(j, "end", e.end); rd(j, "legatoInverted", e.legatoInverted); rd(j, "restartOnSteal", e.restartOnSteal);
    e.additional = j.value("additional", Json{});
}
Json lfoToJson(const LfoDefinition& l) {
    Json pm = Json::array();
    for (const auto& m : l.pointMods) pm.push_back({{"point", m.point}, {"bus", m.bus}, {"target", m.target}});
    return Json{{"shape", l.shape}, {"rateHz", l.rateHz}, {"tempoSync", l.tempoSync}, {"syncBeats", l.syncBeats}, {"mode", l.mode},
                {"phaseDegrees", l.phaseDegrees}, {"rise", l.rise}, {"delay", l.delay}, {"smooth", l.smooth}, {"mono", l.mono},
                {"anchored", l.anchored}, {"direction", l.direction}, {"path", curveToJson(l.path)}, {"pointMods", pm}};
}
void lfoFromJson(const Json& j, LfoDefinition& l) {
    if (!j.is_object()) return;
    rd(j, "shape", l.shape); rd(j, "rateHz", l.rateHz); rd(j, "tempoSync", l.tempoSync); rd(j, "syncBeats", l.syncBeats); rd(j, "mode", l.mode);
    rd(j, "phaseDegrees", l.phaseDegrees); rd(j, "rise", l.rise); rd(j, "delay", l.delay); rd(j, "smooth", l.smooth); rd(j, "mono", l.mono);
    rd(j, "anchored", l.anchored); rd(j, "direction", l.direction);
    if (j.contains("path")) l.path = curveFromJson(j["path"]);
    if (j.contains("pointMods") && j["pointMods"].is_array())
        for (const auto& mj : j["pointMods"]) { LfoPointMod m; rd(mj, "point", m.point); rd(mj, "bus", m.bus); rd(mj, "target", m.target); l.pointMods.push_back(m); }
}
Json modulationToJson(const ModulationRoute& m) {
    return Json{{"slot", m.slot}, {"source", m.source}, {"auxiliary", m.auxiliary}, {"sourceKind", m.sourceKind}, {"sourceIndex", m.sourceIndex},
                {"auxKind", m.auxKind}, {"auxIndex", m.auxIndex}, {"targetKind", m.targetKind}, {"targetIndex", m.targetIndex},
                {"targetParam", m.targetParam}, {"sourceName", m.sourceName}, {"destinationModule", m.destinationModule},
                {"destinationInstance", m.destinationInstance}, {"destinationParameter", m.destinationParameter},
                {"destinationParameterId", m.destinationParameterId}, {"amount", m.amount}, {"bipolar", m.bipolar}, {"bypass", m.bypass},
                {"curveIn", m.curveIn}, {"curveOut", m.curveOut}, {"auxInverted", m.auxInverted}, {"smoothRise", m.smoothRise},
                {"smoothFall", m.smoothFall}, {"delaySeconds", m.delaySeconds}, {"mainCurve", curveToJson(m.mainCurve)},
                {"auxCurve", curveToJson(m.auxCurve)}, {"additional", m.additional}};
}
ModulationRoute modulationFromJson(const Json& j) {
    ModulationRoute m;
    rd(j, "slot", m.slot); rd(j, "source", m.source); rd(j, "auxiliary", m.auxiliary); rd(j, "sourceKind", m.sourceKind);
    rd(j, "sourceIndex", m.sourceIndex); rd(j, "auxKind", m.auxKind); rd(j, "auxIndex", m.auxIndex); rd(j, "targetKind", m.targetKind);
    rd(j, "targetIndex", m.targetIndex); rd(j, "targetParam", m.targetParam); rd(j, "sourceName", m.sourceName);
    rd(j, "destinationModule", m.destinationModule); rd(j, "destinationInstance", m.destinationInstance);
    rd(j, "destinationParameter", m.destinationParameter); rd(j, "destinationParameterId", m.destinationParameterId);
    rd(j, "amount", m.amount); rd(j, "bipolar", m.bipolar); rd(j, "bypass", m.bypass); rd(j, "curveIn", m.curveIn);
    rd(j, "curveOut", m.curveOut); rd(j, "auxInverted", m.auxInverted); rd(j, "smoothRise", m.smoothRise); rd(j, "smoothFall", m.smoothFall);
    rd(j, "delaySeconds", m.delaySeconds);
    if (j.contains("mainCurve")) m.mainCurve = curveFromJson(j["mainCurve"]);
    if (j.contains("auxCurve")) m.auxCurve = curveFromJson(j["auxCurve"]);
    m.additional = j.value("additional", Json{});
    return m;
}
Json fxToJson(const FxModule& m) {
    Json set = Json::array(); for (bool b : m.set) set.push_back(b);
    return Json{{"type", m.type}, {"fxType", m.fxType}, {"rack", m.rack}, {"position", m.position}, {"enabled", m.enabled},
                {"parameters", m.parameters}, {"additional", m.additional}, {"p", m.p}, {"set", set}, {"filterResponse", m.filterResponse},
                {"filterVariant", m.filterVariant}, {"modeVariant", m.modeVariant}, {"impulsePath", m.impulsePath}};
}
FxModule fxFromJson(const Json& j) {
    FxModule m;
    rd(j, "type", m.type); rd(j, "fxType", m.fxType); rd(j, "rack", m.rack); rd(j, "position", m.position); rd(j, "enabled", m.enabled);
    m.parameters = j.value("parameters", Json{}); m.additional = j.value("additional", Json{});
    rd(j, "p", m.p); rd(j, "filterResponse", m.filterResponse); rd(j, "filterVariant", m.filterVariant); rd(j, "modeVariant", m.modeVariant);
    rd(j, "impulsePath", m.impulsePath);
    if (j.contains("set") && j["set"].is_array())
        for (std::size_t i = 0; i < m.set.size() && i < j["set"].size(); ++i) m.set[i] = j["set"][i].is_boolean() && j["set"][i].get<bool>();
    if (m.fxType == FxType::unknown && !m.type.empty()) m.fxType = fxTypeFromSerum(m.type);
    return m;
}
Json globalsToJson(const GlobalState& g) {
    return Json{{"directVol", g.directVol}, {"fxBus1Vol", g.fxBus1Vol}, {"fxBus2Vol", g.fxBus2Vol}, {"fxBus1Dest", g.fxBus1Dest},
                {"fxBus2Dest", g.fxBus2Dest}, {"portamentoTime", g.portamentoTime}, {"portamentoCurve", g.portamentoCurve},
                {"portaAlways", g.portaAlways}, {"portaScaled", g.portaScaled}, {"legato", g.legato}, {"bendUp", g.bendUp},
                {"bendDown", g.bendDown}, {"priority", g.priority}, {"masterTuning", g.masterTuning}, {"transpose", g.transpose},
                {"swing", g.swing}, {"oversampling", g.oversampling}, {"noteLatch", g.noteLatch}, {"limitSameNote", g.limitSameNote},
                {"voiceAmp", g.voiceAmp}, {"modWheel", g.modWheel}, {"velocityAmpDepth", g.velocityAmpDepth},
                {"envTimeScale", g.envTimeScale}, {"lfoTimeScale", g.lfoTimeScale}, {"pitchQuantizerKey", g.pitchQuantizerKey},
                {"pitchQuantizerScale", g.pitchQuantizerScale}, {"pitchQuantizerMask", g.pitchQuantizerMask},
                {"midiOutClipPlayer", g.midiOutClipPlayer}, {"mpeEnabled", g.mpeEnabled}, {"mpePitchBendRange", g.mpePitchBendRange},
                {"noteCurveApplyTuning", g.noteCurveApplyTuning}, {"pitchExpressionApplyTuning", g.pitchExpressionApplyTuning},
                {"serum1Compatibility", g.serum1Compatibility}};
}
void globalsFromJson(const Json& j, GlobalState& g) {
    if (!j.is_object()) return;
    rd(j, "directVol", g.directVol); rd(j, "fxBus1Vol", g.fxBus1Vol); rd(j, "fxBus2Vol", g.fxBus2Vol); rd(j, "fxBus1Dest", g.fxBus1Dest);
    rd(j, "fxBus2Dest", g.fxBus2Dest); rd(j, "portamentoTime", g.portamentoTime); rd(j, "portamentoCurve", g.portamentoCurve);
    rd(j, "portaAlways", g.portaAlways); rd(j, "portaScaled", g.portaScaled); rd(j, "legato", g.legato); rd(j, "bendUp", g.bendUp);
    rd(j, "bendDown", g.bendDown); rd(j, "priority", g.priority); rd(j, "masterTuning", g.masterTuning); rd(j, "transpose", g.transpose);
    rd(j, "swing", g.swing); rd(j, "oversampling", g.oversampling); rd(j, "noteLatch", g.noteLatch); rd(j, "limitSameNote", g.limitSameNote);
    rd(j, "voiceAmp", g.voiceAmp); rd(j, "modWheel", g.modWheel); rd(j, "velocityAmpDepth", g.velocityAmpDepth);
    rd(j, "envTimeScale", g.envTimeScale); rd(j, "lfoTimeScale", g.lfoTimeScale); rd(j, "pitchQuantizerKey", g.pitchQuantizerKey);
    rd(j, "pitchQuantizerScale", g.pitchQuantizerScale); rd(j, "pitchQuantizerMask", g.pitchQuantizerMask);
    rd(j, "midiOutClipPlayer", g.midiOutClipPlayer); rd(j, "mpeEnabled", g.mpeEnabled); rd(j, "mpePitchBendRange", g.mpePitchBendRange);
    rd(j, "noteCurveApplyTuning", g.noteCurveApplyTuning); rd(j, "pitchExpressionApplyTuning", g.pitchExpressionApplyTuning);
    rd(j, "serum1Compatibility", g.serum1Compatibility);
}
Json voicePanelToJson(const VoicePanel& v) {
    return Json{{"detune", v.detune}, {"pan", v.pan}, {"envTime", v.envTime}, {"cutoff", v.cutoff}, {"mod1", v.mod1}, {"mod2", v.mod2},
                {"randomDetune", v.randomDetune}, {"randomPan", v.randomPan}, {"randomEnvTime", v.randomEnvTime},
                {"randomCutoff", v.randomCutoff}, {"scalingEnvTime", v.scalingEnvTime}, {"scalingLfoTime", v.scalingLfoTime},
                {"voiceCount", v.voiceCount}, {"active", v.active}};
}
void voicePanelFromJson(const Json& j, VoicePanel& v) {
    if (!j.is_object()) return;
    rd(j, "detune", v.detune); rd(j, "pan", v.pan); rd(j, "envTime", v.envTime); rd(j, "cutoff", v.cutoff); rd(j, "mod1", v.mod1);
    rd(j, "mod2", v.mod2); rd(j, "randomDetune", v.randomDetune); rd(j, "randomPan", v.randomPan); rd(j, "randomEnvTime", v.randomEnvTime);
    rd(j, "randomCutoff", v.randomCutoff); rd(j, "scalingEnvTime", v.scalingEnvTime); rd(j, "scalingLfoTime", v.scalingLfoTime);
    rd(j, "voiceCount", v.voiceCount); rd(j, "active", v.active);
}
Json arpToJson(const ArpSettings& a) {
    Json clips = Json::array();
    for (const auto& c : a.clips) {
        Json steps = Json::array();
        for (const auto& s : c.steps) steps.push_back({{"time", s.time}, {"length", s.length}, {"note", s.note}, {"velocity", s.velocity}});
        clips.push_back({{"rate", c.rate}, {"dotted", c.dotted}, {"triplet", c.triplet}, {"shape", c.shape}, {"repeats", c.repeats},
                         {"wrapRange", c.wrapRange}, {"wrapMode", c.wrapMode}, {"wrapPhantomNote", c.wrapPhantomNote}, {"gate", c.gate},
                         {"chance", c.chance}, {"transpose", c.transpose}, {"transposeRange", c.transposeRange},
                         {"transposeShape", c.transposeShape}, {"retrigger", c.retrigger}, {"lengthBeats", c.lengthBeats}, {"steps", steps}});
    }
    return Json{{"enabled", a.enabled}, {"activeClip", a.activeClip}, {"keyZoneMin", a.keyZoneMin}, {"keyZoneMax", a.keyZoneMax}, {"clips", clips}};
}
void arpFromJson(const Json& j, ArpSettings& a) {
    if (!j.is_object()) return;
    rd(j, "enabled", a.enabled); rd(j, "activeClip", a.activeClip); rd(j, "keyZoneMin", a.keyZoneMin); rd(j, "keyZoneMax", a.keyZoneMax);
    if (!j.contains("clips") || !j["clips"].is_array()) return;
    for (std::size_t i = 0; i < a.clips.size() && i < j["clips"].size(); ++i) {
        const auto& cj = j["clips"][i]; auto& c = a.clips[i];
        rd(cj, "rate", c.rate); rd(cj, "dotted", c.dotted); rd(cj, "triplet", c.triplet); rd(cj, "shape", c.shape); rd(cj, "repeats", c.repeats);
        rd(cj, "wrapRange", c.wrapRange); rd(cj, "wrapMode", c.wrapMode); rd(cj, "wrapPhantomNote", c.wrapPhantomNote); rd(cj, "gate", c.gate);
        rd(cj, "chance", c.chance); rd(cj, "transpose", c.transpose); rd(cj, "transposeRange", c.transposeRange);
        rd(cj, "transposeShape", c.transposeShape); rd(cj, "retrigger", c.retrigger); rd(cj, "lengthBeats", c.lengthBeats);
        if (cj.contains("steps") && cj["steps"].is_array())
            for (const auto& sj : cj["steps"]) { ArpStep s; rd(sj, "time", s.time); rd(sj, "length", s.length); rd(sj, "note", s.note); rd(sj, "velocity", s.velocity); c.steps.push_back(s); }
    }
}
Json clipsToJson(const ClipSettings& s) {
    Json clips = Json::array();
    for (const auto& c : s.clips) {
        Json notes = Json::array();
        for (const auto& n : c.notes) notes.push_back({{"time", n.time}, {"length", n.length}, {"note", n.note}, {"velocity", n.velocity}});
        clips.push_back({{"rate", c.rate}, {"lengthBeats", c.lengthBeats}, {"playbackMode", c.playbackMode}, {"spanMode", c.spanMode},
                         {"retrigger", c.retrigger}, {"noteGate", c.noteGate}, {"triplet", c.triplet}, {"dotted", c.dotted},
                         {"transpose", c.transpose}, {"notes", notes}});
    }
    return Json{{"enabled", s.enabled}, {"selectOctave", s.selectOctave}, {"clips", clips}};
}
void clipsFromJson(const Json& j, ClipSettings& s) {
    if (!j.is_object()) return;
    rd(j, "enabled", s.enabled); rd(j, "selectOctave", s.selectOctave);
    if (!j.contains("clips") || !j["clips"].is_array()) return;
    for (std::size_t i = 0; i < s.clips.size() && i < j["clips"].size(); ++i) {
        const auto& cj = j["clips"][i]; auto& c = s.clips[i];
        rd(cj, "rate", c.rate); rd(cj, "lengthBeats", c.lengthBeats); rd(cj, "playbackMode", c.playbackMode); rd(cj, "spanMode", c.spanMode);
        rd(cj, "retrigger", c.retrigger); rd(cj, "noteGate", c.noteGate); rd(cj, "triplet", c.triplet); rd(cj, "dotted", c.dotted);
        rd(cj, "transpose", c.transpose);
        if (cj.contains("notes") && cj["notes"].is_array())
            for (const auto& nj : cj["notes"]) { ClipNote n; rd(nj, "time", n.time); rd(nj, "length", n.length); rd(nj, "note", n.note); rd(nj, "velocity", n.velocity); c.notes.push_back(n); }
    }
}
}

Json patchToJson(const Patch& patch) {
    Json oscillators = Json::array(), filters = Json::array(), routes = Json::array(), envelopes = Json::array();
    for (const auto& o : patch.oscillators) oscillators.push_back(oscillatorToJson(o));
    for (const auto& f : patch.filters) filters.push_back(filterToJson(f));
    for (const auto& r : patch.routes) routes.push_back(routeToJson(r));
    for (const auto& e : patch.envelopes) envelopes.push_back(envelopeToJson(e));
    Json modulation = Json::array(), fx = Json::array(), lfoDefinitions = Json::array(), diagnostics = Json::array();
    for (const auto& m : patch.modulation) modulation.push_back(modulationToJson(m));
    for (const auto& m : patch.fx) fx.push_back(fxToJson(m));
    for (const auto& l : patch.lfoDefinitions) lfoDefinitions.push_back(lfoToJson(l));
    for (const auto& d : patch.diagnostics) diagnostics.push_back({{"path", d.path}, {"status", d.status}, {"detail", d.detail}});
    return Json{
        {"zygPresetFormat", 3},
        {"name", patch.name}, {"author", patch.author}, {"serumVersion", patch.serumVersion}, {"sourcePath", patch.sourcePath},
        {"assetRoot", patch.assetRoot}, {"presetId", patch.presetId},
        {"masterVolume", patch.masterVolume}, {"mono", patch.mono}, {"polyphony", patch.polyphony},
        {"lfoOneRateHz", patch.lfoOneRateHz}, {"lfoOneSine", patch.lfoOneSine},
        {"oscillators", oscillators}, {"filters", filters}, {"routes", routes}, {"envelopes", envelopes},
        {"modulation", modulation}, {"fx", fx}, {"lfos", patch.lfos}, {"lfoDefinitions", lfoDefinitions}, {"macros", patch.macros},
        {"macroValues", patch.macroValues}, {"arp", patch.arp}, {"clips", patch.clips}, {"global", patch.global},
        {"arpClips", patch.arpClips}, {"midiClips", patch.midiClips},
        {"globals", globalsToJson(patch.globals)}, {"voicePanel", voicePanelToJson(patch.voicePanel)},
        {"arpSettings", arpToJson(patch.arpSettings)}, {"clipSettings", clipsToJson(patch.clipSettings)},
        {"unknownSerumState", patch.unknownSerumState}, {"originalPreset", patch.originalPreset},
        {"diagnostics", diagnostics}, {"typedParameterPaths", patch.typedParameterPaths},
        {"explicitParameters", patch.explicitParameters}, {"mappedParameters", patch.mappedParameters},
    };
}

Patch patchFromJson(const Json& json) {
    Patch patch;
    if (!json.is_object()) return patch;
    patch.name = json.value("name", std::string{"Untitled"});
    rd(json, "author", patch.author); rd(json, "serumVersion", patch.serumVersion); rd(json, "sourcePath", patch.sourcePath);
    rd(json, "assetRoot", patch.assetRoot); rd(json, "presetId", patch.presetId);
    rd(json, "masterVolume", patch.masterVolume); rd(json, "mono", patch.mono); rd(json, "polyphony", patch.polyphony);
    rd(json, "lfoOneRateHz", patch.lfoOneRateHz); rd(json, "lfoOneSine", patch.lfoOneSine);
    auto readArray = [&](const char* key, auto& dest, auto&& convert) {
        if (json.contains(key) && json.at(key).is_array())
            for (std::size_t i = 0; i < dest.size() && i < json.at(key).size(); ++i) convert(json.at(key)[i], dest[i]);
    };
    readArray("oscillators", patch.oscillators, oscillatorFromJson);
    readArray("filters", patch.filters, filterFromJson);
    readArray("routes", patch.routes, routeFromJson);
    readArray("envelopes", patch.envelopes, envelopeFromJson);
    readArray("lfoDefinitions", patch.lfoDefinitions, lfoFromJson);
    if (json.contains("modulation") && json.at("modulation").is_array())
        for (const auto& j : json.at("modulation")) patch.modulation.push_back(modulationFromJson(j));
    if (json.contains("fx") && json.at("fx").is_array())
        for (const auto& j : json.at("fx")) patch.fx.push_back(fxFromJson(j));
    auto plain = [](const Json& src, Json& dst) { dst = src; };
    readArray("lfos", patch.lfos, plain); readArray("macros", patch.macros, plain);
    readArray("arpClips", patch.arpClips, plain); readArray("midiClips", patch.midiClips, plain);
    if (json.contains("macroValues") && json.at("macroValues").is_array())
        for (std::size_t i = 0; i < patch.macroValues.size() && i < json.at("macroValues").size(); ++i)
            patch.macroValues[i] = json.at("macroValues")[i].get<double>();
    patch.arp = json.value("arp", Json{}); patch.clips = json.value("clips", Json{}); patch.global = json.value("global", Json{});
    if (json.contains("globals")) globalsFromJson(json["globals"], patch.globals);
    if (json.contains("voicePanel")) voicePanelFromJson(json["voicePanel"], patch.voicePanel);
    if (json.contains("arpSettings")) arpFromJson(json["arpSettings"], patch.arpSettings);
    if (json.contains("clipSettings")) clipsFromJson(json["clipSettings"], patch.clipSettings);
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

ParamRange oscParamRange(OscParam p) noexcept {
    switch (p) {
        case OscParam::volume: return {0.0, 1.0}; case OscParam::pan: return {-1.0, 1.0};
        case OscParam::coarse: return {-64.0, 64.0}; case OscParam::fine: return {-100.0, 100.0};
        case OscParam::octave: return {-4.0, 4.0}; case OscParam::detune: return {0.0, 1.0};
        case OscParam::blend: return {0.0, 100.0}; case OscParam::unisonStereo: return {-100.0, 100.0};
        case OscParam::unisonWarp: case OscParam::unisonWarp2: case OscParam::unisonWTPos: return {-100.0, 100.0};
        case OscParam::tablePos: return {0.0, 256.0}; case OscParam::warp1: case OscParam::warp2: return {0.0, 1.0};
        case OscParam::warpVar1: case OscParam::warpVar2: return {0.0, 1.0};
        case OscParam::initialPhase: return {0.0, 360.0};
        case OscParam::randomPhase: case OscParam::start: case OscParam::end: case OscParam::position:
        case OscParam::loopStart: case OscParam::loopEnd: return {0.0, 100.0};
        case OscParam::scanRate: return {-200.0, 200.0}; case OscParam::pitch: return {-12.0, 12.0};
        case OscParam::pitchRatio: return {-1.0, 24.0}; case OscParam::hzOffset: return {-1000.0, 500.0};
        case OscParam::grainLength: return {0.0, 10.0, true}; case OscParam::density: return {0.0, 800.0, true};
        case OscParam::randomPitch: return {0.0, 12.0};
        case OscParam::randomOffset: case OscParam::randomPan: case OscParam::randomGain: case OscParam::randomDir:
        case OscParam::randomGrainLength: case OscParam::windowParam: case OscParam::randomWarp:
        case OscParam::randomWarp2: case OscParam::specWet: return {0.0, 100.0};
        case OscParam::windowSkew: case OscParam::timbreShift: case OscParam::specShift: return {-100.0, 100.0};
        case OscParam::envAttack: case OscParam::envDecay: case OscParam::envRelease: return {0.0, 32.0, true};
        case OscParam::envSustain: case OscParam::color: return {0.0, 1.0};
        case OscParam::freqLo: case OscParam::freqHi: return {20.0, 20000.0, true};
        default: return {0.0, 1.0};
    }
}
ParamRange filterParamRange(FilterParam p) noexcept {
    switch (p) {
        case FilterParam::freq: case FilterParam::level: case FilterParam::x: case FilterParam::y: return {0.0, 1.0};
        case FilterParam::pan: return {-100.0, 100.0};
        default: return {0.0, 100.0};
    }
}
ParamRange envParamRange(EnvParam p) noexcept {
    switch (p) {
        case EnvParam::sustain: return {0.0, 1.0};
        case EnvParam::curve1: case EnvParam::curve2: case EnvParam::curve3: return {0.0, 100.0};
        default: return {0.0, 32.0, true};
    }
}
ParamRange lfoParamRange(LfoParam p) noexcept {
    switch (p) {
        case LfoParam::rate: return {0.0, 100.0, true}; case LfoParam::phase: return {0.0, 360.0};
        case LfoParam::smooth: return {0.0, 100.0}; default: return {0.0, 4.0, true};
    }
}
ParamRange globalParamRange(GlobalParam p) noexcept {
    switch (p) {
        case GlobalParam::masterTuning: return {-24.0, 24.0}; case GlobalParam::portamentoTime: return {0.0, 3.0, true};
        case GlobalParam::swing: case GlobalParam::envTimeScale: case GlobalParam::lfoTimeScale: return {0.0, 100.0}; case GlobalParam::transpose: return {-24.0, 24.0};
        case GlobalParam::masterVolume: return {0.0, 1.0};
        default: return {0.0, 2.0};
    }
}
double applyModulation(double base, double d, const ParamRange& r) noexcept {
    if (d == 0.0) return base;
    if (!r.log) return std::min(r.hi, std::max(r.lo, base + d * (r.hi - r.lo)));
    const double floor = std::max(r.lo, r.hi * 1.0e-4);
    const double norm = base <= floor ? 0.0 : std::log(base / floor) / std::log(r.hi / floor);
    const double moved = std::min(1.0, std::max(0.0, norm + d));
    return moved <= 0.0 ? (base <= 0.0 ? base : floor * 0.0 + (d < 0.0 ? r.lo : floor)) : floor * std::pow(r.hi / floor, moved);
}
ParamRange routingParamRange(RoutingParam p) noexcept {
    return p == RoutingParam::filterBalance ? ParamRange{-100.0, 100.0} : ParamRange{0.0, 100.0};
}

bool modSourceFromId(int id, ModSource& kind, int& index) noexcept {
    auto set = [&](ModSource k, int i) { kind = k; index = i; return true; };
    if (id == 1) return set(ModSource::modWheel, 0);
    if (id >= 2 && id <= 5) return set(ModSource::envelope, id - 2);
    if (id >= 6 && id <= 15) return set(ModSource::lfo, id - 6);
    if (id == 16) return set(ModSource::velocity, 0);
    if (id == 17) return set(ModSource::note, 0);
    if (id == 18) return set(ModSource::channelPressure, 0);
    if (id == 19) return set(ModSource::polyPressure, 0);
    if (id == 20) return set(ModSource::noiseAudio, 0);
    if (id == 21 || id == 22) return set(ModSource::random, id - 21);
    // Ids 39-44 and 47-48 occur in factory presets as per-note variation of pan/fine/cutoff/position;
    // they are treated as further note-on random sources (unverified).
    if (id >= 39 && id <= 44) return set(ModSource::random, id - 37);
    if (id == 47 || id == 48) return set(ModSource::random, id - 40);
    if (id == 23 || id == 24) return set(ModSource::alternate, id - 23);
    if (id >= 25 && id <= 32) return set(ModSource::macro, id - 25);
    if (id == 33) return set(ModSource::pitchBend, 0);
    if (id >= 34 && id <= 36) return set(id == 34 ? ModSource::mpeX : id == 35 ? ModSource::mpeY : ModSource::mpeZ, 0);
    if (id == 37) return set(ModSource::releaseVelocity, 0);
    if (id == 38) return set(ModSource::fixed, 0);
    if (id >= 49 && id <= 52) return set(ModSource::oscAudio, id - 49);
    if (id == 53 || id == 54) return set(ModSource::filterAudio, id - 53);
    if (id == 55) return set(ModSource::activeVoices, 0);
    if (id == 56 || id == 57) return set(ModSource::voiceMod, id - 56);
    if (id == 58) return set(ModSource::voiceIndex, 0);
    if (id == 59) return set(ModSource::discreteRandom, 0);
    return false;
}
bool modSourceIsBipolar(ModSource k) noexcept {
    switch (k) {
        case ModSource::lfo: case ModSource::pitchBend: case ModSource::mpeX: case ModSource::mpeY:
        case ModSource::noiseAudio: case ModSource::oscAudio: case ModSource::filterAudio: return true;
        default: return false;
    }
}
}
