#include "FxParams.h"
#include <array>
#include <string_view>

namespace zyg {
namespace {
using I = FxParamInfo;
// name, serumKey, default, lo, hi. Defaults for absent keys are provisional ZYG
// choices (see docs/DSP_DECISIONS.md); ranges come from the observed corpus.
constexpr std::array<I, 13> bode {{
    {"shift", "kParamShift", 0, -100, 100}, {"range", "kParamRange", 100, 0.1, 3100},
    {"feedback", "kParamFeedback", 0, 0, 100}, {"delayTime", "kParamDelayTime", 0.05, 0.001, 2},
    {"blur", "kParamBlur", 0, 0, 100}, {"delayBalance", "kParamDelayBalance", 0, -100, 100},
    {"outputMix", "kParamOutputMix", 0, -100, 100}, {"outputWidth", "kParamOutputWidth", 50, 0, 100},
    {"wet", "kParamWet", 50, 0, 100}, {"monoInput", "kParamMonoInput", 0, 0, 1},
    {"swapAB", "kParamSwapAB", 0, 0, 1}, {"beatSync", "kParamBeatSync", 0, 0, 1},
    {"retrig", "kParamRetrig", 0, 0, 1}}};
constexpr std::array<I, 9> chorus {{
    {"delay", "kParamDelay", 7, 0, 13}, {"delay2", "kParamDelay2", 5, 0, 11},
    {"depth", "kParamDepth", 6, 0, 26}, {"feedback", "kParamFeedback", 0, 0, 60},
    {"filter", "kParamFilt", 20000, 50, 20000}, {"filterMode", "kParamFiltMode", 0, 0, 1},
    {"rate", "kParamRate", 0.4, 0, 1.4}, {"wet", "kParamWet", 50, 0, 100},
    {"beatSync", "kParamBeatSync", 0, 0, 1}}};
constexpr std::array<I, 22> comp {{
    {"attack", "kParamAttack", 10, 0.1, 1000}, {"release", "kParamRelease", 100, 0.1, 1000},
    {"thresh", "kParamThresh", 0.5, 0, 1}, {"ratio", "kParamRatio", 2, 1, 1000000},
    {"ratioBelow", "kParamRatioBelow", 1, 0.02, 1}, {"makeup", "kParamMakeup", 1, 1, 31},
    {"wet", "kParamWet", 100, 0, 100}, {"multiband", "kParamMultiband", 0, 0, 1},
    {"xoverLow", "kParamXoverLow", 200, 50, 5000}, {"xoverHi", "kParamXoverHi", 2500, 300, 10600},
    {"gain0", "kParamGain0", 0, -24, 24}, {"gain1", "kParamGain1", 0, -24, 24}, {"gain2", "kParamGain2", 0, -24, 24},
    {"ratio0", "kParamRatio0", 0.5, 0, 1}, {"ratio1", "kParamRatio1", 0.5, 0, 1}, {"ratio2", "kParamRatio2", 0.5, 0, 1},
    {"ratioBelow0", "kParamRatioBelow0", 0.5, 0, 1}, {"ratioBelow1", "kParamRatioBelow1", 0.5, 0, 1},
    {"ratioBelow2", "kParamRatioBelow2", 0.5, 0, 1}, {"threshUD0", "kParamThreshUD0", 100, 0, 200},
    {"threshUD1", "kParamThreshUD1", 100, 0, 200}, {"threshUD2", "kParamThreshUD2", 100, 0, 200}}};
constexpr std::array<I, 9> conv {{
    {"attack", "kParamAttack", 0, 0, 0.25}, {"damping", "kParamDamping", 100, 0, 100},
    {"decay", "kParamDecay", 40, 0, 40}, {"ipTrim", "kParamIpTrim", 0, -34, 6},
    {"predelay", "kParamPredelay", 0, 0, 0.4}, {"size", "kParamSize", 100, 10, 1000},
    {"tone", "kParamTone", 0, -100, 100}, {"wet", "kParamWet", 25, 0, 100},
    {"minPhase", "kParamMinPhase", 0, 0, 1}}};
constexpr std::array<I, 11> delay {{
    {"bandwidth", "kParamBW", 3, 0.75, 8.25}, {"feedback", "kParamFeedback", 35, 0, 95},
    {"frequency", "kParamFreq", 2000, 40, 18000}, {"timeL", "kParamTimeL", 0.25, 0.001, 2},
    {"timeR", "kParamTimeR", 0.25, 0.001, 2}, {"offsetL", "kParamOffsetL", 1, 0.5, 1.5},
    {"offsetR", "kParamOffsetR", 1, 0.5, 1.5}, {"wet", "kParamWet", 25, 0, 100},
    {"mode", "kParamMode", 1, 1, 2}, {"link", "kParamLink", 0, 0, 1}, {"beatSync", "kParamBeatSync", 0, 0, 1}}};
constexpr std::array<I, 7> distortion {{
    {"bandwidth", "kParamBW", 1.5, 0.075, 7.6}, {"drive", "kParamDrive", 30, 0, 100},
    {"frequency", "kParamFreq", 1.0, 0, 1}, {"lphp", "kParamLPHP", 100, 0, 100},
    {"numStages", "kParamNumStages", 4, 2, 16}, {"prePost", "kParamPrePost", 1, 1, 2},
    {"wet", "kParamWet", 100, 0, 100}}};
constexpr std::array<I, 8> eq {{
    {"freq1", "kParamFreq1", 100, 20, 20000}, {"freq2", "kParamFreq2", 4000, 20, 20000},
    {"gain1", "kParamGain1", 0, -24, 24}, {"gain2", "kParamGain2", 0, -24, 24},
    {"reso1", "kParamReso1", 40, 0, 100}, {"reso2", "kParamReso2", 40, 0, 100},
    {"type1", "kParamType1", 1, 1, 2}, {"type2", "kParamType2", 1, 1, 2}}};
constexpr std::array<I, 9> filter {{
    {"drive", "kParamDrive", 0, 0, 100}, {"freq", "kParamFreq", 0.5, 0, 1},
    {"reso", "kParamReso", 10, 0, 100}, {"stereo", "kParamStereo", 50, 0, 100},
    {"var", "kParamVar", 0, 0, 100}, {"wet", "kParamWet", 100, 0, 100},
    {"x", "kParamX", 0.5, 0, 1}, {"y", "kParamY", 0.5, 0, 1}, {"pad", "kParamPad", 0, 0, 1}}};
constexpr std::array<I, 6> flanger {{
    {"depth", "kParamDepth", 50, 0, 100}, {"feedback", "kParamFeedback", 50, 0, 95},
    {"rate", "kParamRate", 0.4, 0, 9}, {"wet", "kParamWet", 50, 0, 100},
    {"width", "kParamWidth", 90, 0, 360}, {"beatSync", "kParamBeatSync", 0, 0, 1}}};
constexpr std::array<I, 7> hyperd {{
    {"detune", "kParamDetune", 30, 0, 100}, {"dimSize", "kParamDimESize", 50, 0, 100},
    {"dimWet", "kParamDimEWet", 50, 0, 100}, {"rate", "kParamRate", 30, 0, 100},
    {"unison", "kParamUnison", 4, 0, 7}, {"wet", "kParamWet", 50, 0, 100}, {"retrig", "kParamRetrig", 0, 0, 1}}};
constexpr std::array<I, 9> phaser {{
    {"depth", "kParamDepth", 50, 0, 100}, {"depth2", "kParamDepth2", 0.5, 0, 1},
    {"feedback", "kParamFeedback", 50, 0, 100}, {"freq", "kParamFreq", 500, 20, 18000},
    {"numPoles", "kParamNumPoles", 6, 1, 18}, {"rate", "kParamRate", 0.2, 0, 20},
    {"wet", "kParamWet", 50, 0, 100}, {"width", "kParamWidth", 90, 0, 360},
    {"beatSync", "kParamBeatSync", 0, 0, 1}}};
constexpr std::array<I, 12> reverb {{
    {"delay", "kParamDelay", 30, 0, 250}, {"feedback", "kParamFeedback", 50, 0, 100},
    {"freq", "kParamFreq", 70, 0, 100}, {"freqB", "kParamFreqB", 50, 0, 100},
    {"freqC", "kParamFreqC", 50, 0, 100}, {"mode", "kParamMode", 50, 0, 100},
    {"preDelay", "kParamPreDelay", 0.02, 0, 2.5}, {"size", "kParamSize", 50, 0, 100},
    {"vintageScale", "kParamVintageScale", 50, 0, 100}, {"vintageScaleB", "kParamVintageScaleB", 50, 0, 100},
    {"wet", "kParamWet", 25, 0, 100}, {"width", "kParamWidth", 80, 0, 100}}};
constexpr std::array<I, 9> utils {{
    {"balance", "kParamBalance", 0, -100, 100}, {"hpf", "kParamHPF", 20, 1, 400},
    {"lfMono", "kParamLFMono", 0, 0, 1}, {"lfXover", "kParamLFXover", 150, 20, 400},
    {"lpf", "kParamLPF", 20000, 50, 20000}, {"polarityL", "kParamPolarityL", 0, 0, 1},
    {"polarityR", "kParamPolarityR", 0, 0, 1}, {"wet", "kParamWet", 100, 0, 100},
    {"width", "kParamWidth", 100, 0, 800}}};
constexpr std::array<I, 5> split {{
    {"freq", "kParamFreq", 500, 30, 3600}, {"freq2", "kParamFreq2", 3000, 300, 9000},
    {"count1", "kParamModuleCount1", 0, 0, 5}, {"count2", "kParamModuleCount2", 0, 0, 5},
    {"count3", "kParamModuleCount3", 0, 0, 5}}};
// ZYG extensions. depth/wet in percent; beats in quarter notes; times in ms.
constexpr std::array<I, 9> pump {{
    {"depth", "", 80, 0, 100}, {"beats", "", 1, 0.0625, 16}, {"shape", "", 0, -100, 100},
    {"hold", "", 0, 0, 90}, {"attack", "", 2, 0.1, 100}, {"trigger", "", 0, 0, 3},
    {"thresh", "", -30, -60, 0}, {"release", "", 120, 5, 1000}, {"wet", "", 100, 0, 100}}};
constexpr std::array<I, 10> stutter {{
    {"beats", "", 0.25, 0.03125, 2}, {"active", "", 1, 0, 1}, {"mode", "", 0, 0, 1},
    {"chance", "", 100, 0, 100}, {"pitch", "", 0, -24, 24}, {"gate", "", 100, 5, 100},
    {"falloff", "", 0, 0, 90}, {"reverse", "", 0, 0, 1}, {"smooth", "", 2, 0.5, 20}, {"wet", "", 100, 0, 100}}};
}

std::span<const FxParamInfo> fxParamTable(FxType type) noexcept {
    switch (type) {
        case FxType::bode: return bode; case FxType::chorus: return chorus;
        case FxType::comp: return comp; case FxType::conv: return conv;
        case FxType::delay: return delay; case FxType::distortion: return distortion;
        case FxType::eq: return eq; case FxType::filter: return filter;
        case FxType::flanger: return flanger; case FxType::hyperD: return hyperd;
        case FxType::phaser: return phaser; case FxType::reverb: return reverb;
        case FxType::utils: return utils;
        case FxType::split: case FxType::split3: case FxType::splitMS: return split;
        case FxType::pump: return pump; case FxType::stutter: return stutter;
        default: return {};
    }
}

bool fxParamIsLog(FxType type, std::size_t slot) noexcept {
    const auto table = fxParamTable(type);
    if (slot >= table.size()) return false;
    const std::string_view n = table[slot].name;
    static constexpr std::string_view names[] = {"range", "delayTime", "filter", "attack", "release", "xoverLow", "xoverHi", "decay", "size",
        "frequency", "timeL", "timeR", "freq1", "freq2", "freq", "hpf", "lpf", "lfXover", "beats"};
    if (type == FxType::conv && n == "size") return false;
    if ((type == FxType::filter || type == FxType::distortion) && n == "freq") return false; // already a normalized knob
    if (type == FxType::reverb || type == FxType::hyperD) return false;
    for (auto name : names) if (n == name) return true;
    return false;
}

FxType fxTypeFromSerum(const std::string& name) noexcept {
    if (name == "FXBode") return FxType::bode; if (name == "FXChorus") return FxType::chorus;
    if (name == "FXComp") return FxType::comp; if (name == "FXConv") return FxType::conv;
    if (name == "FXDelay") return FxType::delay; if (name == "FXDistortion") return FxType::distortion;
    if (name == "FXEQ") return FxType::eq; if (name == "FXFilter") return FxType::filter;
    if (name == "FXFlanger") return FxType::flanger; if (name == "FXHyperD") return FxType::hyperD;
    if (name == "FXPhaser") return FxType::phaser; if (name == "FXReverb") return FxType::reverb;
    if (name == "FXUtils") return FxType::utils; if (name == "FXSplit") return FxType::split;
    if (name == "FXSplit3") return FxType::split3; if (name == "FXSplitMS") return FxType::splitMS;
    return FxType::unknown;
}

const char* fxTypeName(FxType type) noexcept {
    switch (type) {
        case FxType::bode: return "bode"; case FxType::chorus: return "chorus";
        case FxType::comp: return "comp"; case FxType::conv: return "conv";
        case FxType::delay: return "delay"; case FxType::distortion: return "distortion";
        case FxType::eq: return "eq"; case FxType::filter: return "filter";
        case FxType::flanger: return "flanger"; case FxType::hyperD: return "hyperD";
        case FxType::phaser: return "phaser"; case FxType::reverb: return "reverb";
        case FxType::utils: return "utils"; case FxType::split: return "split";
        case FxType::split3: return "split3"; case FxType::splitMS: return "splitMS";
        case FxType::pump: return "pump"; case FxType::stutter: return "stutter";
        default: return "unknown";
    }
}

void applyFxDefaults(FxModule& module) {
    const auto table = fxParamTable(module.fxType);
    for (std::size_t i = 0; i < table.size() && i < module.p.size(); ++i)
        if (!module.set[i]) module.p[i] = table[i].def;
}
}
