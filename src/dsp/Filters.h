#pragma once
// ZYG voice/FX filter family. Every response is an original design guided by the
// category names Serum documents (normal SVF, MG/EMS/Acid ladders, multi-mode
// morphs, comb, flange, phaser, formant, ...); exact Serum transfer curves are
// not claimed. All state is preallocated in prepare().
#include "DspCommon.h"
#include "Patch.h"

namespace zyg::dsp {

struct FilterParams {
    FilterResponse type = FilterResponse::low12;
    int variant = 0;
    double cutoffHzL = 1000.0, cutoffHzR = 1000.0;
    double reso = 0.1;    // 0..1
    double drive = 0.0;   // 0..1
    double var = 0.0;     // 0..1
    double x = 0.5, y = 0.5;
};

// Maps a Serum filter identifier to a native response + variant. Kept here so
// the adapter and the native UI share one table.
bool filterFromSerumId(const std::string& id, FilterResponse& response, int& variant) noexcept;
std::string filterSerumIdName(FilterResponse response, int variant);
// Human-readable native name for a response family member.
const char* filterFamilyName(FilterResponse response) noexcept;

class FilterCore {
public:
    void prepare(double sampleRate);   // allocates; not for the audio thread
    void reset() noexcept;
    void update(const FilterParams& p) noexcept;
    void process(double& l, double& r) noexcept;
    bool usesDelayMemory() const noexcept;
private:
    struct Chan {
        std::array<Svf, 4> svf;
        std::array<OnePole, 3> pole;
        std::array<Allpass1, 16> ap;
        std::array<double, 4> ladder {};
        std::array<double, 4> ladderAux {};
        std::array<Svf, 3> formant;
        DelayLine delayA, delayB;
        std::array<DelayLine, 4> diffuse;
        double loopState = 0.0, loopState2 = 0.0, hold = 0.0, phase = 0.0;
        std::array<double, 4> fdn {};
        double last = 0.0;
        Rng rng;
    };
    double tick(Chan& c, int channel, double in) noexcept;
    std::array<Chan, 2> ch_;
    FilterParams p_;
    double sr_ = 44100.0;
    // derived per update
    std::array<double, 2> hz_ {}, g1_ {};
    double k_ = 1.0, q_ = 1.0, feedback_ = 0.0, driveGain_ = 1.0, driveNorm_ = 1.0;
    int stages_ = 1;
};
}
