#pragma once
// Per-voice oscillator generators: wavetable, SUB, NOISE, sample, multisample,
// granular and spectral. Each oscillator produces a stereo sample (unison
// spread/pan applied inside) plus a raw mono value used as an audio-rate
// modulation source. Nothing here allocates on the audio thread; heavy state is
// sized in OscVoiceState::allocate().
#include "DspCommon.h"
#include "Modulators.h"
#include "Oversampling.h"
#include "Spectral.h"
#include "Warp.h"
#include "Patch.h"
#include <memory>

namespace zyg::dsp {
inline constexpr std::size_t oscParamCount = std::size_t(OscParam::count);
using OscValues = std::array<double, oscParamCount>;
inline double& at(OscValues& v, OscParam p) noexcept { return v[std::size_t(p)]; }
inline double at(const OscValues& v, OscParam p) noexcept { return v[std::size_t(p)]; }
// Stored (unmodulated) value of `param` for an oscillator, in the units the patch stores.
double oscBaseValue(const Oscillator& osc, OscParam param) noexcept;

struct UnisonVoice { double semis = 0, ratio = 1, gain = 1, gL = 1, gR = 1, s = 0, wtOff = 0, warp1 = 0, warp2 = 0; };

struct OscGrain { bool on = false; double pos = 0, inc = 0, w = 0, winInc = 0, gL = 1, gR = 1; };

struct OscVoiceState {
    static constexpr int maxUnison = 16, maxGrains = 64;
    std::array<double, maxUnison> phase {}, pos {};
    std::array<signed char, maxUnison> dir {};
    std::array<bool, maxUnison> finished {};
    std::array<UnisonVoice, maxUnison> uni {};
    int unisonCount = 1;
    std::array<OversampleState, 4> os {};   // [ch] first stage, [2 + ch] second stage (4x mode)
    std::array<std::array<double, 24>, 2> align {};
    std::size_t alignW = 0;
    std::array<WarpState, 2> warpState {};
    double lastOut = 0.0;
    double baseFreq = 440.0, baseSemis = 0.0;
    // multisample: region 0 uses pos/dir (with unison); layered regions 1..3 are single voices
    std::array<int, 4> regionIdx {};
    int regionCount = 0;
    double sliceStart = -1.0, sliceEnd = -1.0;   // frames; negative when slicing is off or out of range
    bool sliceSilent = false;
    std::array<double, 4> regPos {};
    std::array<signed char, 4> regDir {};
    std::array<bool, 4> regDone {};
    std::array<SpectralParams, 4> specParams {};
    EnvelopeGen sampleEnv;
    // noise
    Rng rng;
    std::array<double, 9> rows {};
    std::uint32_t counter = 0;
    double brown = 0.0, click = 0.0, noisePos = 0.0;
    OnePole colorLp, colorHp;
    bool noiseDone = false;
    // granular
    std::array<OscGrain, maxGrains> grains {};
    std::array<double, maxUnison> grainTimer {};
    double timeline = 0.0;
    std::array<float, 1025> window {};
    double windowKey[3] = {-1.0e9, 0.0, 0.0};
    int windowShape = -1;
    // spectral
    std::array<std::unique_ptr<SpectralVoice>, 4> spectral;
    double elapsed = 0.0;
    void allocate(SpectralShared* shared);
    void reset() noexcept;
};

struct OscInputs {
    const Oscillator* osc = nullptr;
    const OscValues* v = nullptr;
    int index = 0;
    int note = 60;
    double semis = 0.0;         // semitones from A4 for this note: pitch bend, glide, transpose included
    double velocity = 1.0;
    double sampleRate = 44100.0, bpm = 120.0, masterTuning = 440.0;
    const std::array<double, 7>* mods = nullptr; // last raw outputs of oscillators A,B,C,NOISE,SUB then filters 1, 2
    bool released = false;
    int oversampleLevel = 0;    // 0: 2x for warped oscillators, >=1: 4x
    bool importedPatch = false; // preset came from an import (unresolved assets stay silent)
    std::uint64_t serial = 0;   // per-note counter used to seed random phase/start
};
struct OscOut { double l = 0.0, r = 0.0, raw = 0.0; bool oversampled = false; };

inline bool oscUsesOversampling(const Oscillator& osc) noexcept {
    if (osc.mode != OscMode::wavetable && osc.mode != OscMode::sub) return false;
    for (const auto& w : osc.warpDefinitions) if (warpNeedsOversampling(w.mode)) return true;
    return false;
}

void oscNoteOn(OscVoiceState& s, const OscInputs& in) noexcept;
void oscNoteOff(OscVoiceState& s, const OscInputs& in) noexcept;
void oscBlockUpdate(OscVoiceState& s, const OscInputs& in) noexcept;
OscOut oscRender(OscVoiceState& s, const OscInputs& in) noexcept;
}
