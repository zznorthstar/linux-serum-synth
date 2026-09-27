#pragma once
// Envelope and LFO generators. Both are fixed-size, allocation-free and
// deterministic. LFO/envelope shapes are ZYG designs; Serum path points are
// evaluated with the stored point/bend data but the bend transfer is native.
#include "DspCommon.h"
#include "Patch.h"

namespace zyg::dsp {

inline double envelopeCurveShape(double progress, double curve) noexcept {
    const double x = clampd(progress, 0.0, 1.0);
    const double bend = clampd((curve - 50.0) / 50.0, -1.0, 1.0);
    if (std::abs(bend) < 1.0e-6) return x;
    const double exponent = std::exp2(std::abs(bend) * 4.0);
    return bend > 0.0 ? 1.0 - std::pow(1.0 - x, exponent) : std::pow(x, exponent);
}

struct EnvParams {
    double attack = 0.005, hold = 0.0, decay = 2.0, sustain = 1.0, release = 0.075;
    double curve1 = 50.0, curve2 = 66.6, curve3 = 66.6;
    double start = 0.0, end = 0.0;
};

class EnvelopeGen {
public:
    enum Stage : std::uint8_t { idle, attack, hold, decay, sustain, release };
    void noteOn(double startLevel = 0.0) noexcept { stage_ = attack; t_ = 0.0; value_ = startLevel; from_ = startLevel; }
    void noteOff() noexcept { if (stage_ != idle && stage_ != release) { stage_ = release; t_ = 0.0; from_ = value_; } }
    void reset() noexcept { stage_ = idle; t_ = 0.0; value_ = 0.0; from_ = 0.0; }
    bool active() const noexcept { return stage_ != idle; }
    Stage stage() const noexcept { return stage_; }
    double value() const noexcept { return value_; }
    // Advance by `dt` seconds (block-rate friendly).
    double advance(const EnvParams& p, double dt) noexcept {
        if (stage_ == idle) return value_ = 0.0;
        t_ += dt;
        const double sus = clampd(p.sustain, 0.0, 1.0);
        switch (stage_) {
            case attack:
                if (p.attack <= dt) { value_ = 1.0; stage_ = hold; t_ = 0.0; }
                else {
                    value_ = from_ + (1.0 - from_) * envelopeCurveShape(t_ / p.attack, p.curve1);
                    if (t_ >= p.attack) { stage_ = hold; t_ = 0.0; value_ = 1.0; }
                }
                break;
            case hold:
                value_ = 1.0;
                if (t_ >= p.hold) { stage_ = decay; t_ = 0.0; }
                break;
            case decay:
                if (p.decay <= dt) { value_ = sus; stage_ = sustain; t_ = 0.0; }
                else {
                    value_ = 1.0 + (sus - 1.0) * envelopeCurveShape(t_ / p.decay, p.curve2);
                    if (t_ >= p.decay) { stage_ = sustain; t_ = 0.0; value_ = sus; }
                }
                break;
            case sustain: value_ = sus; break;
            case release:
                if (p.release <= dt) { value_ = p.end; stage_ = idle; }
                else {
                    value_ = p.end + (from_ - p.end) * (1.0 - envelopeCurveShape(t_ / p.release, p.curve3));
                    if (t_ >= p.release || value_ <= p.end + 1.0e-7) { stage_ = idle; value_ = p.end; }
                }
                break;
            default: break;
        }
        return value_;
    }
private:
    Stage stage_ = idle;
    double t_ = 0.0, value_ = 0.0, from_ = 0.0;
};

// Raw-array evaluators (n points; bend has n entries) so modulated copies can live on the stack.
double evalCurveRaw(const double* x, const double* y, const double* bend, std::size_t n, double t) noexcept;
double evalPathRaw(const double* x, const double* y, const double* bend, std::size_t n, bool closed, double phase) noexcept;
double evalPath2DRaw(const double* x, const double* y, std::size_t n, bool closed, double u) noexcept;
// Evaluates a periodic (closed) or one-shot path. `phase` in [0,1); output in [-1,1].
double evalPath(const CurvePoints& path, double phase) noexcept;
// 2D polyline traversal at constant speed; returns the point's value in [-1,1].
double evalPath2D(const CurvePoints& path, double u) noexcept;
// Evaluates a matrix curve on [0,1] returning [0,1].
double evalCurve(const CurvePoints& curve, double x) noexcept;
double bendCurve(double t, double bend) noexcept;

struct LfoState {
    double phase = 0.0, elapsed = 0.0, smoothed = 0.0, held = 0.0;
    std::array<double, 3> chaos {0.1, 0.0, 0.0};
    std::uint32_t rng = 1;
    bool started = false, finished = false;
    void reset(std::uint32_t seed, double startPhase) noexcept {
        phase = startPhase; elapsed = 0.0; smoothed = 0.0; held = 0.0; chaos = {0.1, 0.0, 0.0};
        rng = seed ? seed : 1u; started = false; finished = false;
    }
};

// Cycles per second for a definition at the given tempo.
inline double lfoRateHz(const LfoDefinition& d, double bpm) noexcept {
    if (d.tempoSync) return beatsPerSecond(bpm) / std::max(d.syncBeats, 1.0e-4);
    return clampd(d.rateHz, 0.0, 1000.0);
}

// Advances one control step of `dt` seconds and returns the LFO value in [-1,1].
// `rateHz` may be modulated; `gateOn` lets envelope-mode LFOs restart.
// `busOffsets` (16 entries, may be null) modulates point properties named by d.pointMods.
double advanceLfo(LfoState& s, const LfoDefinition& d, double rateHz, double dt,
                  double phaseOffset, double smoothPercent, double rise, double delay,
                  const double* busOffsets = nullptr) noexcept;
}
