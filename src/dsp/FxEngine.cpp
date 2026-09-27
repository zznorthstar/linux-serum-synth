#include "FxEngine.h"
#include "Filters.h"
#include "Warp.h"
#include "Oversampling.h"
#include <cstring>

namespace zyg {
std::shared_ptr<const ConvIr> buildConvIr(const SampleData& ir, double maxSeconds) {
    if (ir.frames() == 0) return nullptr;
    auto out = std::make_shared<ConvIr>();
    out->sampleRate = ir.sampleRate;
    out->channels = ir.stereo() ? 2 : 1;
    const std::size_t frames = std::min<std::size_t>(ir.frames(), std::size_t(maxSeconds * ir.sampleRate));
    out->partitions = int((frames + ConvIr::block - 1) / ConvIr::block);
    out->re.assign(std::size_t(out->channels) * std::size_t(out->partitions) * ConvIr::fftSize, 0.0f);
    out->im.assign(out->re.size(), 0.0f);
    dsp::Fft fft; fft.init(ConvIr::fftSize);
    std::vector<float> br(ConvIr::fftSize), bi(ConvIr::fftSize);
    for (int c = 0; c < out->channels; ++c) {
        const auto& src = c == 0 ? ir.left : ir.right;
        // Unit-energy impulse response so the convolution preserves programme level.
        double energy = 0.0;
        for (std::size_t i = 0; i < frames; ++i) energy += double(src[i]) * double(src[i]);
        const float norm = energy > 1.0e-12 ? float(1.0 / std::sqrt(energy)) : 1.0f;
        for (int p = 0; p < out->partitions; ++p) {
            std::fill(br.begin(), br.end(), 0.0f); std::fill(bi.begin(), bi.end(), 0.0f);
            for (int i = 0; i < ConvIr::block; ++i) {
                const std::size_t at = std::size_t(p) * ConvIr::block + std::size_t(i);
                if (at < frames) br[std::size_t(i)] = src[at] * norm;
            }
            fft.forward(br.data(), bi.data());
            const auto o = out->offset(c, p);
            std::copy(br.begin(), br.end(), out->re.begin() + std::ptrdiff_t(o));
            std::copy(bi.begin(), bi.end(), out->im.begin() + std::ptrdiff_t(o));
        }
    }
    return out;
}
}

namespace zyg::dsp {
namespace {
constexpr int Q = fxParamSlots;
inline double pct(double v) noexcept { return clampd(v / 100.0, 0.0, 1.0); }
inline double lfoSine(double phase) noexcept { return std::sin(tau * phase); }

// Small feedback allpass built on a delay line.
struct DelayAllpass {
    DelayLine line; double g = 0.6, len = 100.0;
    void allocate(std::size_t n) { line.allocate(n); }
    double process(double x) noexcept {
        const double d = line.read(len);
        const double v = x - g * d; line.push(v); return d + g * v;
    }
    void clear() noexcept { line.clear(); }
};

// ---------------------------------------------------------------- Bode
class BodeUnit final : public FxUnit {
    static constexpr int firLen = 65, half = 32;
public:
    void prepare(double sr) override {
        sr_ = sr;
        for (int i = 0; i < firLen; ++i) {
            const int k = i - half;
            double h = 0.0;
            if (k % 2 != 0) h = 2.0 / (pi * k);
            const double w = 0.42 - 0.5 * std::cos(tau * i / (firLen - 1)) + 0.08 * std::cos(2.0 * tau * i / (firLen - 1));
            h_[std::size_t(i)] = h * w;
        }
        for (int c = 0; c < 2; ++c) { dl_[c].allocate(std::size_t(2.1 * sr) + 16); }
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) { hist_[c].fill(0.0); dl_[c].clear(); ap_[c].fill({}); }
        w_ = 0; phase_ = 0.0;
    }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double shiftHz = p[fx::bShift] / 100.0 * p[fx::bRange];
        const double fb = pct(p[fx::bFeedback]) * 0.95;
        const double base = clampd(p[fx::bDelayTime], 0.001, 2.0) * sr_;
        const double bal = clampd(p[fx::bDelayBalance] / 200.0, -0.5, 0.5);
        const double t = (clampd(p[fx::bOutputMix], -100, 100) + 100.0) / 200.0;
        const double width = clampd(p[fx::bOutputWidth] / 50.0, 0.0, 2.0);
        const double wet = pct(p[fx::bWet]);
        const double blur = pct(p[fx::bBlur]) * 0.7;
        double* io[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            double dry[2], out[2], tail[2];
            for (int c = 0; c < 2; ++c) {
                double x = io[c][i];
                if (p[fx::bMonoInput] > 0.5) x = 0.5 * (l[i] + r[i]);
                const double d = dl_[c].read(std::max(2.0, base * (1.0 + (c == 0 ? bal : -bal) * 2.0)));
                double fbs = d;
                for (auto& a : ap_[c]) { const double v = fbs - blur * a.z; const double y = a.z + blur * v; a.z = v; fbs = y; }
                const double in = x + fb * fbs;
                hist_[c][std::size_t(w_)] = in;
                dry[c] = hist_[c][std::size_t((w_ - half + firLen) % firLen)];
                double im = 0.0;
                for (int k = 0; k < firLen; ++k) im += h_[std::size_t(k)] * hist_[c][std::size_t((w_ - k + firLen) % firLen)];
                out[c] = dry[c] * std::cos(phase_) - im * std::sin(phase_);
                dl_[c].push(std::tanh(out[c]));
                tail[c] = d;
            }
            w_ = (w_ + 1) % firLen;
            phase_ += tau * shiftHz / sr_; if (phase_ > tau) phase_ -= tau; else if (phase_ < -tau) phase_ += tau;
            double wl = (1.0 - t) * out[0] + t * tail[0], wr = (1.0 - t) * out[1] + t * tail[1];
            const double mid = 0.5 * (wl + wr), side = 0.5 * (wl - wr) * width;
            wl = mid + side; wr = mid - side;
            l[i] = dry[0] * (1.0 - wet) + wl * wet; r[i] = dry[1] * (1.0 - wet) + wr * wet;
        }
    }
private:
    struct Ap { double z = 0.0; };
    double sr_ = 44100.0, phase_ = 0.0;
    std::array<double, firLen> h_ {};
    std::array<double, firLen> hist_[2] {};
    DelayLine dl_[2];
    std::array<Ap, 4> ap_[2] {};
    int w_ = 0;
};

// ---------------------------------------------------------------- Chorus
class ChorusUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; for (auto& d : dl_) d.allocate(std::size_t(0.08 * sr) + 16); reset(); }
    void reset() noexcept override { for (auto& d : dl_) d.clear(); for (auto& s : lp_) s.reset(); phase_ = 0.0; fb_[0] = fb_[1] = 0.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double rate = clampd(p[fx::cRate], 0.0, 20.0), depth = p[fx::cDepth] * 0.0004; // seconds
        const double base1 = (1.0 + p[fx::cDelay]) * 0.001, base2 = (1.0 + p[fx::cDelay2]) * 0.001;
        const double fb = pct(p[fx::cFeedback]) * 0.9, wet = pct(p[fx::cWet]);
        const bool hp = p[fx::cFiltMode] > 0.5;
        for (auto& s : lp_) s.set(clampd(p[fx::cFilt], 50.0, 20000.0), sr_);
        double* io[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            const double ph = phase_;
            phase_ = wrap01(phase_ + rate / sr_);
            for (int c = 0; c < 2; ++c) {
                const double dry = io[c][i];
                const double off = c == 0 ? 0.0 : 0.5;
                const double a = dl_[c].read((base1 + depth * lfoSine(ph + off)) * sr_);
                const double b = dl_[c].read((base2 + depth * lfoSine(ph + off + 0.25)) * sr_);
                double w = 0.5 * (a + b);
                w = hp ? lp_[c].high(w) : lp_[c].low(w);
                dl_[c].push(dry + fb * fb_[c]);
                fb_[c] = w;
                io[c][i] = dry * (1.0 - 0.5 * wet) + w * wet;
            }
        }
    }
private:
    double sr_ = 44100.0, phase_ = 0.0, fb_[2] {};
    DelayLine dl_[2]; OnePole lp_[2];
};

// ---------------------------------------------------------------- Flanger
class FlangerUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; for (auto& d : dl_) d.allocate(std::size_t(0.03 * sr) + 16); reset(); }
    void reset() noexcept override { for (auto& d : dl_) d.clear(); phase_ = 0.0; fb_[0] = fb_[1] = 0.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double rate = clampd(p[fx::lRate], 0.0, 20.0), depth = pct(p[fx::lDepth]);
        const double fb = clampd(p[fx::lFeedback] / 100.0, -0.95, 0.95), wet = pct(p[fx::lWet]);
        const double offset = clampd(p[fx::lWidth], 0.0, 360.0) / 360.0;
        double* io[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            const double ph = phase_; phase_ = wrap01(phase_ + rate / sr_);
            for (int c = 0; c < 2; ++c) {
                const double dry = io[c][i];
                const double lfo = 0.5 + 0.5 * lfoSine(ph + (c ? offset : 0.0));
                const double delay = (0.0004 + depth * 0.0065 * lfo) * sr_;
                const double tap = dl_[c].read(delay);
                dl_[c].push(std::tanh(dry + fb * fb_[c]));
                fb_[c] = tap;
                io[c][i] = lerp(dry, 0.5 * (dry + tap), wet);
            }
        }
    }
private:
    double sr_ = 44100.0, phase_ = 0.0, fb_[2] {};
    DelayLine dl_[2];
};

// ---------------------------------------------------------------- Phaser
class PhaserUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override { for (auto& c : ap_) for (auto& a : c) a.reset(); phase_ = 0.0; fb_[0] = fb_[1] = 0.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const int poles = std::clamp(int(std::lround(p[fx::aNumPoles])), 1, 18);
        const double rate = clampd(p[fx::aRate], 0.0, 40.0), depth = pct(p[fx::aDepth]);
        const double spread = std::exp2(0.6 * clampd(p[fx::aDepth2], 0.0, 1.0));
        const double fb = clampd(p[fx::aFeedback] / 100.0, 0.0, 0.97), wet = pct(p[fx::aWet]);
        const double offset = clampd(p[fx::aWidth], 0.0, 360.0) / 360.0;
        double* io[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            const double ph = phase_; phase_ = wrap01(phase_ + rate / sr_);
            if ((i & 7) == 0) {
                for (int c = 0; c < 2; ++c) {
                    const double lfo = lfoSine(ph + (c ? offset : 0.0));
                    const double fc = clampd(p[fx::aFreq] * std::exp2(2.5 * depth * lfo), 20.0, sr_ * 0.45);
                    for (int k = 0; k < poles; ++k) ap_[c][std::size_t(k)].set(clampd(fc * std::pow(spread, k), 20.0, sr_ * 0.45), sr_);
                }
            }
            for (int c = 0; c < 2; ++c) {
                const double dry = io[c][i];
                double s = dry + fb * fb_[c];
                for (int k = 0; k < poles; ++k) s = ap_[c][std::size_t(k)].process(s);
                fb_[c] = std::tanh(s);
                io[c][i] = lerp(dry, 0.5 * (dry + s), wet);
            }
        }
    }
private:
    double sr_ = 44100.0, phase_ = 0.0, fb_[2] {};
    std::array<Allpass1, 18> ap_[2];
};

// ---------------------------------------------------------------- HyperD
class HyperDUnit final : public FxUnit {
public:
    void prepare(double sr) override {
        sr_ = sr; for (auto& d : hyper_) d.allocate(std::size_t(0.06 * sr) + 16);
        for (auto& d : dim_) d.allocate(std::size_t(0.05 * sr) + 16);
        reset();
    }
    void reset() noexcept override { for (auto& d : hyper_) d.clear(); for (auto& d : dim_) d.clear(); ph_ = 0.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const int voices = std::clamp(int(std::lround(p[fx::hUnison])), 0, 7);
        const double rate = 0.05 + pct(p[fx::hRate]) * 4.0, excursion = pct(p[fx::hDetune]) * 0.006;
        const double wet = pct(p[fx::hWet]), dimW = pct(p[fx::hDimEWet]), dimS = pct(p[fx::hDimESize]);
        static constexpr double ratios[7] = {1.0, 1.31, 0.77, 1.73, 0.59, 2.11, 1.13};
        for (int i = 0; i < n; ++i) {
            const double dl = l[i], dr = r[i];
            hyper_[0].push(dl); hyper_[1].push(dr);
            double hl = dl, hr = dr;
            if (voices > 0) {
                double sl = 0.0, sr2 = 0.0;
                for (int v = 0; v < voices; ++v) {
                    const double ph = ph_ * ratios[v] + double(v) / 7.0;
                    const double del = (0.012 + excursion * (0.5 + 0.5 * lfoSine(ph))) * sr_;
                    const double pan = voices > 1 ? double(v) / (voices - 1) : 0.5;
                    const double a = hyper_[0].read(del), b = hyper_[1].read(del * (1.0 + 0.07 * v));
                    sl += (a * (1.0 - pan) + b * 0.3); sr2 += (b * pan + a * 0.3);
                }
                const double norm = 1.0 / std::sqrt(double(voices) + 1.0);
                hl = lerp(dl, (dl + sl) * norm, wet); hr = lerp(dr, (dr + sr2) * norm, wet);
            }
            ph_ = wrap01(ph_ + rate / sr_);
            dim_[0].push(hl); dim_[1].push(hr);
            const double mod = 0.5 + 0.5 * lfoSine(ph_ * 0.6);
            const double d1 = (0.005 + 0.009 * dimS + 0.0008 * mod) * sr_;
            const double a = dim_[0].read(d1), b = dim_[1].read(d1 * 1.21);
            const double xl = hl + 0.6 * dimS * (b - 0.5 * a), xr = hr + 0.6 * dimS * (a - 0.5 * b);
            l[i] = lerp(hl, xl, dimW); r[i] = lerp(hr, xr, dimW);
        }
    }
private:
    double sr_ = 44100.0, ph_ = 0.0;
    DelayLine hyper_[2], dim_[2];
};

// ---------------------------------------------------------------- Delay
class DelayUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; for (auto& d : dl_) d.allocate(std::size_t(2.6 * sr) + 16); reset(); }
    void reset() noexcept override { for (auto& d : dl_) d.clear(); for (auto& s : bp_) s.reset(); sm_[0] = sm_[1] = -1.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        double tL = clampd(p[fx::dTimeL] * p[fx::dOffsetL], 0.001, 2.5), tR = clampd(p[fx::dTimeR] * p[fx::dOffsetR], 0.001, 2.5);
        if (p[fx::dLink] > 0.5) tR = tL;
        const double fb = pct(p[fx::dFeedback]), wet = pct(p[fx::dWet]);
        const bool pingPong = p[fx::dMode] > 1.5;
        const double bw = clampd(p[fx::dBW], 0.75, 8.25);
        const double q = 1.0 / (2.0 * std::sinh(0.34657359 * bw));
        for (auto& s : bp_) s.set(clampd(p[fx::dFreq], 40.0, 18000.0), std::max(q, 0.1), sr_);
        if (sm_[0] < 0.0) { sm_[0] = tL * sr_; sm_[1] = tR * sr_; }
        const double a = 1.0 - std::exp(-1.0 / (0.05 * sr_));
        for (int i = 0; i < n; ++i) {
            sm_[0] += a * (tL * sr_ - sm_[0]); sm_[1] += a * (tR * sr_ - sm_[1]);
            const double dl = dl_[0].read(std::max(2.0, sm_[0])), dr = dl_[1].read(std::max(2.0, sm_[1]));
            const double fl = bp_[0].process(dl).band * bp_[0].k, fr = bp_[1].process(dr).band * bp_[1].k;
            const double inL = l[i], inR = r[i];
            if (pingPong) {
                const double mono = 0.5 * (inL + inR);
                dl_[0].push(std::tanh(mono + fb * fr)); dl_[1].push(std::tanh(fb * fl));
            } else {
                dl_[0].push(std::tanh(inL + fb * fl)); dl_[1].push(std::tanh(inR + fb * fr));
            }
            l[i] = inL * (1.0 - 0.5 * wet) + wet * dl; r[i] = inR * (1.0 - 0.5 * wet) + wet * dr;
        }
    }
private:
    double sr_ = 44100.0, sm_[2] {};
    DelayLine dl_[2]; Svf bp_[2];
};

// ---------------------------------------------------------------- Reverb
class ReverbUnit final : public FxUnit {
    static constexpr int lines = 8;
public:
    void prepare(double sr) override {
        sr_ = sr;
        for (auto& d : dl_) d.allocate(std::size_t(0.5 * sr) + 64);
        for (auto& d : pre_) d.allocate(std::size_t(2.9 * sr) + 16);
        const double len[4] = {0.0051, 0.0077, 0.0113, 0.0139};
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) { diff_[c][k].allocate(std::size_t(0.03 * sr) + 16); diff_[c][k].len = len[k] * sr * (c ? 1.07 : 1.0); }
        reset();
    }
    void reset() noexcept override {
        for (auto& d : dl_) d.clear(); for (auto& d : pre_) d.clear();
        for (auto& c : diff_) for (auto& d : c) d.clear();
        for (auto& s : damp_) s.reset(); for (auto& s : hp_) s.reset(); phase_ = 0.0;
    }
    void process(const FxModule& m, const double* p, double* l, double* r, int n) noexcept override {
        // mode: kHall 0, kVintage 1, kSpace 2, kAbyss 3
        const int type = std::clamp(m.modeVariant, 0, 3);
        static constexpr double baseMs[lines] = {29.7, 37.1, 41.1, 43.7, 53.3, 61.7, 71.3, 79.9};
        static constexpr double typeScale[4] = {1.0, 0.6, 1.6, 2.2};
        static constexpr double t60Max[4] = {10.0, 5.0, 25.0, 60.0};
        static constexpr double dampBias[4] = {1.0, 0.8, 1.2, 0.35};
        double sizeScale = 0.35 + pct(p[fx::rSize]) * 1.65;
        if (type == 1) sizeScale *= 0.5 + pct(p[fx::rVintageScale]);
        const double scale = sizeScale * typeScale[type];
        const double t60 = 0.2 + std::pow(pct(p[fx::rFeedback]), 1.5) * (t60Max[type] - 0.2);
        const double dampHz = clampd(500.0 * std::pow(40.0, pct(p[fx::rFreq])) * dampBias[type], 300.0, 20000.0);
        const double hpHz = 20.0 * std::pow(30.0, pct(p[fx::rFreqB]));
        const double diffG = 0.3 + 0.4 * pct(p[fx::rFreqC]) + (type == 1 ? 0.15 * pct(p[fx::rVintageScaleB]) : 0.0);
        const double modDepth = (0.0004 + pct(p[fx::rMode]) * 0.0015) * (type == 1 ? 2.0 : 1.0) * sr_;
        const double width = pct(p[fx::rWidth]), wet = pct(p[fx::rWet]);
        const double preSamples = clampd(p[fx::rPreDelay] + p[fx::rDelay] * 0.001, 0.0, 2.7) * sr_;
        for (int k = 0; k < lines; ++k) {
            len_[k] = baseMs[k] * 0.001 * scale * sr_;
            gain_[k] = std::pow(10.0, -3.0 * (len_[k] / sr_) / t60);
            damp_[k].set(dampHz, sr_); hp_[k].set(hpHz, sr_);
        }
        for (int c = 0; c < 2; ++c) for (auto& a : diff_[c]) a.g = clampd(diffG, 0.1, 0.75);
        for (int i = 0; i < n; ++i) {
            const double dryL = l[i], dryR = r[i];
            pre_[0].push(dryL); pre_[1].push(dryR);
            double inL = preSamples > 1.0 ? pre_[0].read(preSamples) : dryL;
            double inR = preSamples > 1.0 ? pre_[1].read(preSamples) : dryR;
            for (auto& a : diff_[0]) inL = a.process(inL);
            for (auto& a : diff_[1]) inR = a.process(inR);
            phase_ = wrap01(phase_ + 0.37 / sr_);
            double x[lines];
            for (int k = 0; k < lines; ++k) {
                const double mod = modDepth * lfoSine(phase_ * (1.0 + 0.13 * k) + 0.125 * k);
                double v = dl_[k].read(std::max(4.0, len_[k] + mod));
                v = damp_[k].low(v); v = hp_[k].high(v);
                x[k] = v;
            }
            // Householder-style orthogonal mix
            double sum = 0.0; for (int k = 0; k < lines; ++k) sum += x[k];
            const double mix = sum * (2.0 / lines);
            double outL = 0.0, outR = 0.0;
            for (int k = 0; k < lines; ++k) {
                const double inj = (k < 4 ? inL : inR) * (k & 1 ? -0.5 : 0.5);
                dl_[k].push(flush(std::tanh((x[k] - mix) * gain_[k] + inj)));
                if (k & 1) outR += x[k] * ((k & 2) ? -1.0 : 1.0); else outL += x[k] * ((k & 2) ? -1.0 : 1.0);
            }
            outL *= 0.35; outR *= 0.35;
            const double mid = 0.5 * (outL + outR), side = 0.5 * (outL - outR) * (0.3 + 0.7 * width) / 1.0;
            outL = mid + side; outR = mid - side;
            l[i] = dryL * (1.0 - 0.5 * wet) + outL * wet * 1.4; r[i] = dryR * (1.0 - 0.5 * wet) + outR * wet * 1.4;
        }
    }
private:
    double sr_ = 44100.0, phase_ = 0.0;
    DelayLine dl_[lines], pre_[2];
    DelayAllpass diff_[2][4];
    OnePole damp_[lines], hp_[lines];
    double len_[lines] {}, gain_[lines] {};
};

// ---------------------------------------------------------------- Compressor
class CompUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override {
        for (auto& e : lin_) e = 0.0; for (auto& x : x1_) x.reset(); for (auto& x : x2_) x.reset();
        inMs_ = outMs_ = 1.0e-9; agc_ = 1.0;
    }
    static double gainReductionDb(double levelDb, double thrDb, double ratio, double expand, double knee) noexcept {
        const double over = levelDb - thrDb;
        double gr = 0.0;
        if (over > knee * 0.5) gr = over * (1.0 - 1.0 / std::max(ratio, 1.0));
        else if (over > -knee * 0.5) { const double t = over + knee * 0.5; gr = (1.0 - 1.0 / std::max(ratio, 1.0)) * t * t / (2.0 * knee); }
        if (expand > 0.0 && over < -knee * 0.5) gr += std::min(60.0, (-over - knee * 0.5) * expand);
        return gr;
    }
    // Multiband leveller: downward compression above the band threshold, upward compression below it.
    static double levelDb(double levelDbIn, double thr, double down, double up) noexcept {
        if (levelDbIn > thr) return -(levelDbIn - thr) * (1.0 - 1.0 / down);
        if (levelDbIn > -70.0) return std::min(18.0, (thr - levelDbIn) * (1.0 - 1.0 / up));
        return 0.0;
    }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double at = std::exp(-1.0 / (std::max(p[fx::pAttack], 0.05) * 0.001 * sr_));
        const double rl = std::exp(-1.0 / (std::max(p[fx::pRelease], 0.1) * 0.001 * sr_));
        const double wet = pct(p[fx::pWet]);
        const bool multi = p[fx::pMultiband] > 0.5;
        for (int c = 0; c < 2; ++c) { x1_[c].set(p[fx::pXoverLow], sr_); x2_[c].set(p[fx::pXoverHi], sr_); }
        const double thr1 = -60.0 * clampd(p[fx::pThresh], 0.0, 1.0); // 0 = 0 dBFS (brickwall at full scale), 1 = -60 dB
        const double ratio1 = clampd(p[fx::pRatio], 1.0, 1000.0);
        const double exp1 = clampd(1.0 / clampd(p[fx::pRatioBelow], 0.02, 1.0) - 1.0, 0.0, 30.0);
        double bandThr[3], bandRatio[3], bandExp[3], bandGain[3];
        for (int b = 0; b < 3; ++b) {
            bandThr[b] = -60.0 + 60.0 * clampd(p[fx::pThreshUD0 + b] / 200.0, 0.0, 1.0);
            bandRatio[b] = 1.0 / (1.0 - clampd(p[fx::pRatio0 + b], 0.0, 0.995));
            bandExp[b] = 1.0 / (1.0 - clampd(p[fx::pRatioBelow0 + b], 0.0, 0.995));
            bandGain[b] = dbToGain(p[fx::pGain0 + b]);
        }
        // User makeup can only recover what the thresholds remove, so a preset cannot exceed 0 dBFS through it.
        double capDb = 0.0;
        if (!multi) capDb = std::max(0.0, -thr1 * (1.0 - 1.0 / std::min(ratio1, 100.0)));
        else for (int b = 0; b < 3; ++b) capDb += std::max(0.0, -bandThr[b] * (1.0 - 1.0 / std::min(bandRatio[b], 100.0))) / 3.0;
        const double makeup = dbToGain(std::min(gainToDb(clampd(p[fx::pMakeup], 1.0, 31.0)), std::min(capDb, 12.0)));
        // Slow loudness match: the compressor shapes dynamics, it does not change programme level.
        const double avg = std::exp(-1.0 / (0.4 * sr_));
        for (int i = 0; i < n; ++i) {
            const double dl = l[i], dr = r[i];
            double ol, orr;
            if (!multi) {
                const double mag = std::max(std::abs(dl), std::abs(dr));
                lin_[0] = mag > lin_[0] ? at * lin_[0] + (1.0 - at) * mag : rl * lin_[0] + (1.0 - rl) * mag;
                const double g = dbToGain(-gainReductionDb(gainToDb(lin_[0]), thr1, ratio1, exp1, 6.0));
                ol = dl * g; orr = dr * g;
            } else {
                double lo[2], mid[2], hi[2], out[2] = {0.0, 0.0};
                const double in[2] = {dl, dr};
                for (int c = 0; c < 2; ++c) { double rest; x1_[c].process(in[c], lo[c], rest); x2_[c].process(rest, mid[c], hi[c]); }
                for (int b = 0; b < 3; ++b) {
                    const double* band = b == 0 ? lo : (b == 1 ? mid : hi);
                    const double mag = std::max(std::abs(band[0]), std::abs(band[1]));
                    lin_[b] = mag > lin_[b] ? at * lin_[b] + (1.0 - at) * mag : rl * lin_[b] + (1.0 - rl) * mag;
                    const double g = dbToGain(levelDb(gainToDb(lin_[b]), bandThr[b], bandRatio[b], bandExp[b])) * bandGain[b];
                    out[0] += band[0] * g; out[1] += band[1] * g;
                }
                ol = out[0]; orr = out[1];
            }
            inMs_ = avg * inMs_ + (1.0 - avg) * 0.5 * (dl * dl + dr * dr);
            outMs_ = avg * outMs_ + (1.0 - avg) * 0.5 * (ol * ol + orr * orr);
            const double want = clampd(std::sqrt((inMs_ + 1.0e-12) / (outMs_ + 1.0e-12)), 0.2, 3.0);
            agc_ += (1.0 - std::exp(-1.0 / (0.05 * sr_))) * (want - agc_);
            ol *= agc_ * makeup; orr *= agc_ * makeup;
            l[i] = lerp(dl, ol, wet); r[i] = lerp(dr, orr, wet);
        }
    }
private:
    double sr_ = 44100.0, lin_[3] {}, inMs_ = 1.0e-9, outMs_ = 1.0e-9, agc_ = 1.0;
    Crossover x1_[2], x2_[2];
};

// ---------------------------------------------------------------- Convolution
class ConvUnit final : public FxUnit {
    static constexpr int B = ConvIr::block, F = ConvIr::fftSize, maxParts = 640;
public:
    void prepare(double sr) override {
        sr_ = sr; fft_.init(F);
        for (int c = 0; c < 2; ++c) {
            fdlRe_[c].assign(std::size_t(maxParts) * F, 0.0f); fdlIm_[c].assign(std::size_t(maxParts) * F, 0.0f);
            in_[c].assign(B, 0.0f); out_[c].assign(B, 0.0f); tail_[c].assign(B, 0.0f);
            dry_[c].assign(B, 0.0f); pre_[c].allocate(std::size_t(0.5 * sr) + 16);
        }
        acc_.assign(F, 0.0f); accI_.assign(F, 0.0f); tmpR_.assign(F, 0.0f); tmpI_.assign(F, 0.0f);
        gains_.assign(maxParts, 1.0f);
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < 2; ++c) {
            std::fill(fdlRe_[c].begin(), fdlRe_[c].end(), 0.0f); std::fill(fdlIm_[c].begin(), fdlIm_[c].end(), 0.0f);
            std::fill(in_[c].begin(), in_[c].end(), 0.0f); std::fill(out_[c].begin(), out_[c].end(), 0.0f);
            std::fill(tail_[c].begin(), tail_[c].end(), 0.0f); std::fill(dry_[c].begin(), dry_[c].end(), 0.0f);
            pre_[c].clear();
        }
        pos_ = 0; head_ = 0; lp_[0].reset(); lp_[1].reset(); tone_[0].reset(); tone_[1].reset();
    }
    void process(const FxModule& m, const double* p, double* l, double* r, int n) noexcept override {
        const ConvIr* ir = m.convIr.get();
        if (!ir || ir->partitions == 0) return; // missing impulse: dry passthrough
        const int parts = std::min(ir->partitions, maxParts);
        const double sizeFrac = clampd(p[fx::vSize] / 100.0, 0.1, 1.0);
        const int used = std::max(1, int(std::lround(parts * sizeFrac)));
        const double decay = std::max(p[fx::vDecay], 0.02), attack = std::max(p[fx::vAttack], 0.0);
        for (int k = 0; k < used; ++k) {
            const double t = double(k) * B / ir->sampleRate;
            double g = std::pow(10.0, -3.0 * t / decay);
            if (attack > 0.0) g *= std::min(1.0, t / attack);
            if (k > used - 4) g *= double(used - k) / 4.0; // fade the truncated end
            gains_[std::size_t(k)] = float(g);
        }
        const double trim = dbToGain(p[fx::vIpTrim]), wet = pct(p[fx::vWet]);
        const double predelay = clampd(p[fx::vPredelay], 0.0, 0.45) * sr_;
        const bool useLp = p[fx::vDamping] < 99.5, useTone = std::abs(p[fx::vTone]) > 0.5;
        for (int c = 0; c < 2; ++c) {
            lp_[c].set(clampd(500.0 * std::pow(40.0, pct(p[fx::vDamping])), 500.0, 20000.0), sr_);
            tone_[c].setHighShelf(1000.0, clampd(p[fx::vTone] * 0.12, -12.0, 12.0), sr_);
        }
        double* io[2] = {l, r};
        for (int i = 0; i < n; ++i) {
            double wetSample[2], drySample[2];
            for (int c = 0; c < 2; ++c) {
                const double x = io[c][i];
                pre_[c].push(x * trim);
                const double d = predelay > 1.0 ? pre_[c].read(predelay) : x * trim;
                in_[c][std::size_t(pos_)] = float(d);
                // Wet and dry are both one block late so the impulse response starts on the beat.
                double w = out_[c][std::size_t(pos_)];
                if (useTone) w = tone_[c].process(w);
                wetSample[c] = useLp ? lp_[c].low(w) : w;
                drySample[c] = prevDry_[c][std::size_t(pos_)];
                dry_[c][std::size_t(pos_)] = float(x);
            }
            l[i] = drySample[0] * (1.0 - wet) + wetSample[0] * wet;
            r[i] = drySample[1] * (1.0 - wet) + wetSample[1] * wet;
            if (++pos_ == B) { runBlock(*ir, used); pos_ = 0; }
        }
    }
private:
    void runBlock(const ConvIr& ir, int used) noexcept {
        const int channels = ir.channels;
        head_ = (head_ + maxParts - 1) % maxParts;
        for (int c = 0; c < 2; ++c) {
            std::fill(tmpR_.begin(), tmpR_.end(), 0.0f); std::fill(tmpI_.begin(), tmpI_.end(), 0.0f);
            std::copy(in_[c].begin(), in_[c].end(), tmpR_.begin());
            fft_.forward(tmpR_.data(), tmpI_.data());
            std::copy(tmpR_.begin(), tmpR_.end(), fdlRe_[c].begin() + std::ptrdiff_t(std::size_t(head_) * F));
            std::copy(tmpI_.begin(), tmpI_.end(), fdlIm_[c].begin() + std::ptrdiff_t(std::size_t(head_) * F));
        }
        for (int c = 0; c < 2; ++c) {
            std::fill(acc_.begin(), acc_.end(), 0.0f); std::fill(accI_.begin(), accI_.end(), 0.0f);
            const int irc = channels == 2 ? c : 0;
            for (int k = 0; k < used; ++k) {
                const int slot = (head_ + k) % maxParts;
                const float* xr = fdlRe_[c].data() + std::size_t(slot) * F;
                const float* xi = fdlIm_[c].data() + std::size_t(slot) * F;
                const auto o = ir.offset(irc, k);
                const float* hr = ir.re.data() + o; const float* hi = ir.im.data() + o;
                const float g = gains_[std::size_t(k)];
                for (int b = 0; b < F; ++b) {
                    acc_[std::size_t(b)] += g * (xr[b] * hr[b] - xi[b] * hi[b]);
                    accI_[std::size_t(b)] += g * (xr[b] * hi[b] + xi[b] * hr[b]);
                }
            }
            fft_.inverse(acc_.data(), accI_.data());
            for (int i = 0; i < B; ++i) {
                out_[c][std::size_t(i)] = acc_[std::size_t(i)] + tail_[c][std::size_t(i)];
                tail_[c][std::size_t(i)] = acc_[std::size_t(i + B)];
                prevDry_[c][std::size_t(i)] = dry_[c][std::size_t(i)];
            }
        }
    }
    double sr_ = 44100.0;
    Fft fft_;
    std::vector<float> fdlRe_[2], fdlIm_[2], in_[2], out_[2], tail_[2], dry_[2], acc_, accI_, tmpR_, tmpI_, gains_;
    std::array<float, B> prevDry_[2] {};
    DelayLine pre_[2];
    OnePole lp_[2]; Biquad tone_[2];
    int pos_ = 0, head_ = 0;
};

// ---------------------------------------------------------------- Distortion
class DistortionUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override {
        for (auto& s : os_) s = OversampleState{}; for (auto& s : svf_) s.reset();
        for (auto& d : dryHist_) d.fill(0.0); prev_[0] = prev_[1] = 0.0; dryW_ = 0; hold_[0] = hold_[1] = 0.0; cnt_ = 0.0;
    }
    static double shape(int mode, double x, double drive, int stages, double& aux) noexcept {
        const double g = std::pow(10.0, drive * 1.8); // 0..36 dB pre-gain
        const double d = 1.0 + 9.0 * drive;
        (void) aux;
        switch (mode) {
            case 0: { const double a = std::tanh(x * g), b = std::tanh(x * g * 0.35); return x >= 0 ? a : b; } // kAsym
            case 1: return shapeDiode1(x * g, 1.0 + 6.0 * drive);
            case 2: return shapeDiode2(x * g, 1.0 + 6.0 * drive);
            case 4: return clampd(x * g, -1.0, 1.0);
            case 5: return foldLinear(x * g);
            case 6: return std::tanh(x * g + 0.05) - std::tanh(0.05);
            case 7: { const double v = x * g; return std::tanh(std::abs(v) * 1.2) * (v >= 0 ? 1.0 : 0.6); }
            case 8: return std::sin(x * g * 1.5707963267948966);
            case 9: return std::sin(x * (1.0 + 5.0 * drive) * pi);
            case 10: return std::tanh(x * g);
            case 11: return shapeSoftSat(x, d);
            case 12: return shapeStomp(x, d * 1.5);
            case 13: return shapeTape(x, 1.0 + 6.0 * drive);
            case 14: { // xshaper: iterated sine folding, one fold per stage
                double v = x * (1.0 + 3.0 * drive);
                for (int s = 0; s < std::clamp(stages, 1, 16); ++s) v = std::sin(v * 1.5707963267948966 * 1.6);
                return v;
            }
            case 15: { // asymmetric xshaper
                double v = x * (1.0 + 3.0 * drive) + 0.2 * drive;
                for (int s = 0; s < std::clamp(stages, 1, 16); ++s) v = std::sin(v * 1.5707963267948966 * 1.6);
                return v;
            }
            case 16: return shapeZeroSquare(x * (1.0 + 4.0 * drive), drive);
            default: return x;
        }
    }
    void process(const FxModule& m, const double* p, double* l, double* r, int n) noexcept override {
        const int mode = m.modeVariant;
        const double drive = pct(p[fx::xDrive]), wet = pct(p[fx::xWet]);
        const int stages = int(std::lround(p[fx::xNumStages]));
        const double hz = 20.0 * std::pow(1000.0, clampd(p[fx::xFreq], 0.0, 1.0));
        const double q = 1.0 / (2.0 * std::sinh(0.34657359 * clampd(p[fx::xBW], 0.075, 7.6)));
        const double lp = pct(p[fx::xLPHP]);
        const bool pre = p[fx::xPrePost] < 1.5;
        for (auto& s : svf_) s.set(hz, std::max(q, 0.3), sr_);
        const bool filtered = !(lp > 0.999 && hz > 15000.0);
        double* io[2] = {l, r};
        double aux = 0.0;
        const double downFactor = 1.0 + drive * 40.0;
        const double comp = 1.0 / std::sqrt(std::pow(10.0, drive * 1.8)); // half loudness compensation for drive
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < 2; ++c) {
                const double dry = io[c][i];
                dryHist_[c][std::size_t(dryW_ & 15)] = dry;
                double x = dry;
                if (filtered && pre) { const auto o = svf_[c].process(x); x = lp * o.low + (1.0 - lp) * o.high; }
                double y;
                if (mode == 3) { // kDownsample: sample-rate and bit reduction
                    const int factor = std::max(1, int(downFactor));
                    if (dryW_ % factor == 0) hold_[c] = x;
                    const double lv = std::exp2(15.0 - 11.0 * drive);
                    y = std::round(hold_[c] * lv) / lv;
                } else {
                    const double up1 = 0.5 * (prev_[c] + x);
                    const double a = shape(mode, up1, drive, stages, aux), b = shape(mode, x, drive, stages, aux);
                    y = downsample2x(os_[c], a, b) * comp;
                    prev_[c] = x;
                }
                if (filtered && !pre) { const auto o = svf_[c].process(y); y = lp * o.low + (1.0 - lp) * o.high; }
                const double dryAligned = dryHist_[c][std::size_t((dryW_ - 15) & 15)];
                io[c][i] = dryAligned * (1.0 - wet) + y * wet;
            }
            ++dryW_;
        }
    }
private:
    double sr_ = 44100.0, prev_[2] {}, hold_[2] {}, cnt_ = 0.0;
    OversampleState os_[2]; Svf svf_[2];
    std::array<double, 16> dryHist_[2] {};
    long long dryW_ = 0;
};

// ---------------------------------------------------------------- EQ
class EqUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override { for (auto& c : b_) for (auto& q : c) q.reset(); }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        for (int c = 0; c < 2; ++c) {
            const double q1 = 0.3 + 6.0 * pct(p[fx::eReso1]), q2 = 0.3 + 6.0 * pct(p[fx::eReso2]);
            if (p[fx::eType1] < 1.5) b_[c][0].setPeaking(p[fx::eFreq1], q1, p[fx::eGain1], sr_); else b_[c][0].setLowShelf(p[fx::eFreq1], p[fx::eGain1], sr_);
            if (p[fx::eType2] < 1.5) b_[c][1].setPeaking(p[fx::eFreq2], q2, p[fx::eGain2], sr_); else b_[c][1].setHighShelf(p[fx::eFreq2], p[fx::eGain2], sr_);
        }
        for (int i = 0; i < n; ++i) {
            l[i] = b_[0][1].process(b_[0][0].process(l[i]));
            r[i] = b_[1][1].process(b_[1][0].process(r[i]));
        }
    }
private:
    double sr_ = 44100.0; Biquad b_[2][2];
};

// ---------------------------------------------------------------- Filter
class FilterFxUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; core_.prepare(sr); }
    void reset() noexcept override { core_.reset(); }
    void process(const FxModule& m, const double* p, double* l, double* r, int n) noexcept override {
        FilterParams fp;
        fp.type = m.filterResponse; fp.variant = m.filterVariant;
        const double hz = 20.0 * std::pow(1000.0, clampd(p[fx::fFreq], 0.0, 1.0));
        const double st = (clampd(p[fx::fStereo], 0.0, 100.0) - 50.0) / 50.0;
        fp.cutoffHzL = hz * std::exp2(-0.5 * st); fp.cutoffHzR = hz * std::exp2(0.5 * st);
        fp.reso = pct(p[fx::fReso]); fp.drive = pct(p[fx::fDrive]); fp.var = pct(p[fx::fVar]);
        fp.x = clampd(p[fx::fX], 0.0, 1.0); fp.y = clampd(p[fx::fY], 0.0, 1.0);
        core_.update(fp);
        const double wet = pct(p[fx::fWet]);
        const double pad = p[fx::fPad] > 0.5 ? 0.5 : 1.0;
        for (int i = 0; i < n; ++i) {
            double a = l[i] * pad, b = r[i] * pad;
            const double da = a, db = b;
            core_.process(a, b);
            l[i] = lerp(da, a, wet); r[i] = lerp(db, b, wet);
        }
    }
private:
    double sr_ = 44100.0; FilterCore core_;
};

// ---------------------------------------------------------------- Utils
class UtilsUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override { for (auto& b : hp_) b.reset(); for (auto& b : lp_) b.reset(); for (auto& x : xo_) x.reset(); }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double width = clampd(p[fx::uWidth] / 100.0, 0.0, 8.0), bal = clampd(p[fx::uBalance] / 100.0, -1.0, 1.0);
        const double gl = std::min(1.0, 1.0 - bal), gr = std::min(1.0, 1.0 + bal), wet = pct(p[fx::uWet]);
        const double pl = p[fx::uPolarityL] > 0.5 ? -1.0 : 1.0, pr = p[fx::uPolarityR] > 0.5 ? -1.0 : 1.0;
        const bool lfMono = p[fx::uLFMono] > 0.5;
        for (int c = 0; c < 2; ++c) {
            hp_[c].setHighpass(p[fx::uHPF], 0.707, sr_); lp_[c].setLowpass(clampd(p[fx::uLPF], 50.0, 20000.0), 0.707, sr_);
            xo_[c].set(clampd(p[fx::uLFXover], 20.0, 400.0), sr_);
        }
        const bool useHp = p[fx::uHPF] > 20.5, useLp = p[fx::uLPF] < 19900.0;
        for (int i = 0; i < n; ++i) {
            const double dl = l[i], dr = r[i];
            double a = dl * pl, b = dr * pr;
            if (useHp) { a = hp_[0].process(a); b = hp_[1].process(b); }
            if (useLp) { a = lp_[0].process(a); b = lp_[1].process(b); }
            if (lfMono) {
                double la, ha, lb, hb; xo_[0].process(a, la, ha); xo_[1].process(b, lb, hb);
                const double m = 0.5 * (la + lb); a = m + ha; b = m + hb;
            }
            const double mid = 0.5 * (a + b), side = 0.5 * (a - b) * width;
            a = (mid + side) * gl; b = (mid - side) * gr;
            l[i] = lerp(dl, a, wet); r[i] = lerp(dr, b, wet);
        }
    }
private:
    double sr_ = 44100.0; Biquad hp_[2], lp_[2]; Crossover xo_[2];
};

// ------------------------------------------------------------------ Pump
// ZYG extension: tempo-locked, note-triggered or sidechain-driven volume ducking
// (the "kick pump" used in electro, complextro and hard techno).
class PumpUnit final : public FxUnit {
public:
    void prepare(double sr) override { sr_ = sr; reset(); }
    void reset() noexcept override { gain_ = 1.0; since_ = 1.0e9; env_ = 0.0; hold_ = 0; armed_ = true; lastSerial_ = ctx ? ctx->noteSerial : 0; follow_ = 0.0; }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double depth = pct(p[fx::uDepth]), beats = std::max(p[fx::uBeats], 0.03), wet = pct(p[fx::uWetMix]);
        const double shape = p[fx::uShape], hold = p[fx::uHold];
        const int mode = int(std::lround(clampd(p[fx::uTrigger], 0.0, 3.0)));
        const double bps = beatsPerSecond(ctx ? ctx->bpm : 120.0), perSample = bps / sr_;
        const double aDown = 1.0 - std::exp(-1.0 / (std::max(p[fx::uAttack], 0.1) * 0.001 * sr_)), aUp = 1.0 - std::exp(-1.0 / (0.0005 * sr_));
        const double thrLin = dbToGain(clampd(p[fx::uThresh], -60.0, 0.0));
        const double relC = 1.0 - std::exp(-1.0 / (std::max(p[fx::uRelease], 1.0) * 0.001 * sr_));
        const double aEnv = 1.0 - std::exp(-1.0 / (0.0003 * sr_));
        const bool haveSc = ctx && ctx->scL;
        if (ctx && ctx->noteSerial != lastSerial_) { lastSerial_ = ctx->noteSerial; if (mode == fx::pumpNote) since_ = 0.0; }
        for (int i = 0; i < n; ++i) {
            double duck = 0.0;   // 0 = open, 1 = fully ducked
            if (mode == fx::pumpTempo) {
                const double t = wrap01((ctx ? ctx->beat : 0.0) / beats + double(i) * perSample / beats);
                duck = fx::pumpDuck(t, hold, shape);
            } else if (mode == fx::pumpNote) {
                since_ += perSample;
                duck = since_ < beats ? fx::pumpDuck(since_ / beats, hold, shape) : 0.0;
            } else if (haveSc) {
                const double x = std::max(std::abs(ctx->scL[i]), std::abs(ctx->scR[i]));
                env_ += (x > env_ ? aEnv : relC) * (x - env_);
                if (mode == fx::pumpSidechain) {
                    if (hold_ > 0) --hold_;
                    if (env_ > thrLin && armed_ && hold_ == 0) { since_ = 0.0; armed_ = false; hold_ = int(0.04 * sr_); }
                    if (env_ < thrLin * 0.5) armed_ = true;
                    since_ += perSample;
                    duck = since_ < beats ? fx::pumpDuck(since_ / beats, hold, shape) : 0.0;
                } else {   // follow: classic sidechain compression by level
                    duck = clampd((env_ / thrLin - 1.0) / 3.0 + (env_ > thrLin ? 0.25 : 0.0), 0.0, 1.0);
                }
            }
            const double target = 1.0 - depth * duck;
            gain_ += (target < gain_ ? aDown : aUp) * (target - gain_);
            const double g = lerp(1.0, gain_, wet);
            l[i] *= g; r[i] *= g;
        }
    }
private:
    double sr_ = 44100.0, gain_ = 1.0, since_ = 1.0e9, env_ = 0.0, follow_ = 0.0;
    int hold_ = 0; bool armed_ = true; unsigned lastSerial_ = 0;
};

// ---------------------------------------------------------------- Stutter
// ZYG extension: tempo-sized beat-repeat. Captures the last slice of audio and loops
// it (optionally reversed, pitched, gated, decaying); Gate mode follows the `active`
// parameter (automate it from a macro/LFO/velocity), Auto mode re-captures at grid
// cells with a probability.
class StutterUnit final : public FxUnit {
public:
    void prepare(double sr) override {
        sr_ = sr;
        std::size_t n = 1024; while (n < std::size_t(sr * 2.3)) n <<= 1;
        ring_[0].assign(n, 0.0f); ring_[1].assign(n, 0.0f); mask_ = n - 1;
        loop_[0].assign(std::size_t(sr * 2.1), 0.0f); loop_[1].assign(std::size_t(sr * 2.1), 0.0f);
        reset();
    }
    void reset() noexcept override {
        std::fill(ring_[0].begin(), ring_[0].end(), 0.0f); std::fill(ring_[1].begin(), ring_[1].end(), 0.0f);
        w_ = 0; engaged_ = false; fade_ = 0.0; pos_ = 0.0; len_ = 1; cell_ = -1; want_ = false; rng_ = Rng(0x51ee7u);
        gainDecay_ = 1.0; gateEnv_ = 1.0;
    }
    void process(const FxModule&, const double* p, double* l, double* r, int n) noexcept override {
        const double bps = beatsPerSecond(ctx ? ctx->bpm : 120.0), perSample = bps / sr_;
        const double beats = std::max(p[fx::tBeats], 0.02);
        const std::size_t maxLen = loop_[0].size() - 2;
        const std::size_t sliceLen = std::size_t(clampd(beats / bps * sr_, 16.0, double(maxLen)));
        const bool autoMode = p[fx::tMode] > 0.5;
        const double chance = pct(p[fx::tChance]), rate = std::exp2(p[fx::tPitch] / 12.0), gate = pct(p[fx::tGate]);
        const double keep = 1.0 - pct(p[fx::tFalloff]) * 0.5, wet = pct(p[fx::tWetMix]);
        const bool rev = p[fx::tReverse] > 0.5;
        const double sm = 1.0 - std::exp(-1.0 / (std::max(p[fx::tSmooth], 0.5) * 0.001 * sr_));
        for (int i = 0; i < n; ++i) {
            ring_[0][w_] = float(l[i]); ring_[1][w_] = float(r[i]);
            w_ = (w_ + 1) & mask_;
            bool recapture = false;
            if (autoMode) {
                const double b = (ctx ? ctx->beat : 0.0) + double(i) * perSample;
                const long cell = long(std::floor(b / beats));
                if (cell != cell_) { cell_ = cell; want_ = rng_.unipolar() < chance; recapture = want_; }
            } else want_ = p[fx::tActive] > 0.5;
            if (want_ && (!engaged_ || recapture)) {
                len_ = sliceLen;
                for (std::size_t k = 0; k < len_; ++k) {
                    const std::size_t src = (w_ + mask_ + 1 - len_ + k) & mask_;
                    loop_[0][k] = ring_[0][src]; loop_[1][k] = ring_[1][src];
                }
                if (!engaged_) { pos_ = 0.0; gainDecay_ = 1.0; }
                engaged_ = true;
            } else if (!want_) engaged_ = false;
            fade_ += sm * ((engaged_ ? 1.0 : 0.0) - fade_);
            if (fade_ < 1.0e-4 && !engaged_) { fade_ = 0.0; continue; }
            // playback
            const double idx = rev ? double(len_) - 1.0 - pos_ : pos_;
            const std::size_t i0 = std::size_t(clampd(idx, 0.0, double(len_ - 1))), i1 = std::min(i0 + 1, len_ - 1);
            const double f = idx - std::floor(idx);
            double a = lerp(loop_[0][i0], loop_[0][i1], f), b = lerp(loop_[1][i0], loop_[1][i1], f);
            const double frac = pos_ / double(len_);
            gateEnv_ += sm * ((frac < gate ? 1.0 : 0.0) - gateEnv_);
            a *= gainDecay_ * gateEnv_; b *= gainDecay_ * gateEnv_;
            pos_ += rate;
            if (pos_ >= double(len_)) { pos_ -= double(len_); gainDecay_ *= keep; }
            const double m = fade_ * wet;
            l[i] = lerp(l[i], a, m); r[i] = lerp(r[i], b, m);
        }
    }
private:
    double sr_ = 44100.0, fade_ = 0.0, pos_ = 0.0, gainDecay_ = 1.0, gateEnv_ = 1.0;
    std::vector<float> ring_[2], loop_[2];
    std::size_t mask_ = 1023, w_ = 0, len_ = 1;
    bool engaged_ = false, want_ = false; long cell_ = -1; Rng rng_;
};
}

std::unique_ptr<FxUnit> makeFxUnit(FxType type) {
    switch (type) {
        case FxType::bode: return std::make_unique<BodeUnit>();
        case FxType::chorus: return std::make_unique<ChorusUnit>();
        case FxType::comp: return std::make_unique<CompUnit>();
        case FxType::conv: return std::make_unique<ConvUnit>();
        case FxType::delay: return std::make_unique<DelayUnit>();
        case FxType::distortion: return std::make_unique<DistortionUnit>();
        case FxType::eq: return std::make_unique<EqUnit>();
        case FxType::filter: return std::make_unique<FilterFxUnit>();
        case FxType::flanger: return std::make_unique<FlangerUnit>();
        case FxType::hyperD: return std::make_unique<HyperDUnit>();
        case FxType::phaser: return std::make_unique<PhaserUnit>();
        case FxType::reverb: return std::make_unique<ReverbUnit>();
        case FxType::utils: return std::make_unique<UtilsUnit>();
        case FxType::pump: return std::make_unique<PumpUnit>();
        case FxType::stutter: return std::make_unique<StutterUnit>();
        default: return nullptr;
    }
}

namespace {
int poolIndex(FxType t) noexcept { return int(t); }
int poolSize(FxType t) noexcept {
    switch (t) {
        case FxType::conv: return 2; case FxType::reverb: case FxType::hyperD: return 4;
        case FxType::delay: case FxType::bode: return 4; case FxType::stutter: return 3; default: return 6;
    }
}
constexpr std::uint64_t fnv(const std::string& s, std::uint64_t h = 1469598103934665603ull) noexcept {
    for (const char ch : s) { h ^= std::uint8_t(ch); h *= 1099511628211ull; }
    return h;
}
}

void FxEngine::prepare(double sampleRate) {
    sr_ = sampleRate;
    for (int t = int(FxType::bode); t <= int(FxType::stutter); ++t) {
        auto& pool = pools_[std::size_t(poolIndex(FxType(t)))];
        pool.units.clear();
        for (int i = 0; i < poolSize(FxType(t)); ++i) {
            auto u = makeFxUnit(FxType(t));
            if (u) { u->ctx = &ctx_; u->prepare(sampleRate); pool.units.push_back(std::move(u)); }
        }
    }
    offsets_.assign(std::size_t(maxModules) * fxParamSlots, 0.0);
    splitBuf_.assign(std::size_t(3) * 6 * chunk, 0.0);
    boundPreset_ = ~std::uint64_t(0); keys_ = {}; patch_ = nullptr;
}

void FxEngine::reset() noexcept {
    for (auto& pool : pools_) for (auto& u : pool.units) u->reset();
    for (auto& s : splits_) for (int c = 0; c < 2; ++c) { s.x1[c].reset(); s.x2[c].reset(); }
}

void FxEngine::clearOffsets() noexcept { std::fill(offsets_.begin(), offsets_.end(), 0.0); }

void FxEngine::bind(const Patch* patch) noexcept {
    patch_ = patch;
    unitFor_.fill(nullptr); splitIndex_.fill(-1);
    rackCount_ = {0, 0, 0};
    if (!patch) return;
    std::uint64_t id = patch->presetId ? patch->presetId : fnv(patch->name, fnv(patch->sourcePath));
    const bool newPreset = id != boundPreset_;
    boundPreset_ = id;
    std::array<int, 19> next {};
    int splitNext = 0;
    const int count = int(std::min<std::size_t>(patch->fx.size(), maxModules));
    for (int i = 0; i < count; ++i) {
        const auto& m = patch->fx[std::size_t(i)];
        const Key key{m.fxType, m.rack, m.position};
        const bool changed = newPreset || keys_[std::size_t(i)].type != key.type || keys_[std::size_t(i)].rack != key.rack
                             || keys_[std::size_t(i)].position != key.position;
        keys_[std::size_t(i)] = key;
        if (m.fxType == FxType::split || m.fxType == FxType::split3 || m.fxType == FxType::splitMS) {
            if (splitNext < int(splits_.size())) {
                splitIndex_[std::size_t(i)] = splitNext;
                if (changed) for (int c = 0; c < 2; ++c) { splits_[std::size_t(splitNext)].x1[c].reset(); splits_[std::size_t(splitNext)].x2[c].reset(); }
                ++splitNext;
            }
        } else {
            auto& pool = pools_[std::size_t(poolIndex(m.fxType))];
            const int slot = next[std::size_t(poolIndex(m.fxType))]++;
            if (slot < int(pool.units.size())) {
                unitFor_[std::size_t(i)] = pool.units[std::size_t(slot)].get();
                if (changed) unitFor_[std::size_t(i)]->reset();
            }
        }
        const int rack = std::clamp(m.rack, 0, 2);
        // insertion by position (stable, fixed capacity)
        auto& list = rackModules_[std::size_t(rack)];
        int at = rackCount_[std::size_t(rack)]++;
        list[std::size_t(at)] = i;
        while (at > 0 && patch->fx[std::size_t(list[std::size_t(at - 1)])].position > m.position) {
            std::swap(list[std::size_t(at)], list[std::size_t(at - 1)]); --at;
        }
    }
}

void FxEngine::processRack(int rack, double* l, double* r, int n) noexcept {
    if (!patch_ || rack < 0 || rack > 2 || rackCount_[std::size_t(rack)] == 0) return;
    for (int off = 0; off < n; off += chunk) {
        const int len = std::min(chunk, n - off);
        ctx_ = ctxBase_;
        ctx_.beat += double(off) / sr_ * beatsPerSecond(ctxBase_.bpm);
        if (ctx_.scL) { ctx_.scL += off; ctx_.scR += off; }
        processRange(rack, 0, rackCount_[std::size_t(rack)], l + off, r + off, len, 0);
    }
}

void FxEngine::processModule(int index, double* l, double* r, int n) noexcept {
    const auto& m = patch_->fx[std::size_t(index)];
    auto* unit = unitFor_[std::size_t(index)];
    if (!m.enabled || !unit) return;
    const auto table = fxParamTable(m.fxType);
    double eff[fxParamSlots];
    const double* off = offsets_.data() + std::size_t(index) * fxParamSlots;
    for (int k = 0; k < fxParamSlots; ++k) {
        double base, lo = -1.0e12, hi = 1.0e12;
        if (k == fxLevelSlot) { base = m.set[std::size_t(k)] ? m.p[std::size_t(k)] : 0.5; lo = 0.0; hi = 1.0; }
        else if (std::size_t(k) < table.size()) {
            base = m.set[std::size_t(k)] ? m.p[std::size_t(k)] : table[std::size_t(k)].def;
            lo = table[std::size_t(k)].lo; hi = table[std::size_t(k)].hi;
        } else base = m.p[std::size_t(k)];
        eff[k] = (k == fxLevelSlot || std::size_t(k) >= table.size())
            ? clampd(base + off[k], lo, hi)
            : applyModulation(base, off[k], ParamRange{lo, hi, fxParamIsLog(m.fxType, std::size_t(k))});
    }
    unit->process(m, eff, l, r, n);
    const double gain = clampd(eff[fxLevelSlot] * 2.0, 0.0, 2.0);
    if (std::abs(gain - 1.0) > 1.0e-6) for (int i = 0; i < n; ++i) { l[i] *= gain; r[i] *= gain; }
}

void FxEngine::processRange(int rack, int begin, int end, double* l, double* r, int n, int depth) noexcept {
    auto& list = rackModules_[std::size_t(rack)];
    int i = begin;
    while (i < end) {
        const int idx = list[std::size_t(i)];
        const auto& m = patch_->fx[std::size_t(idx)];
        const bool isSplit = m.fxType == FxType::split || m.fxType == FxType::split3 || m.fxType == FxType::splitMS;
        if (!isSplit) { processModule(idx, l, r, n); ++i; continue; }
        const int bands = m.fxType == FxType::split3 ? 3 : 2;
        int counts[3] = {0, 0, 0};
        const double* off = offsets_.data() + std::size_t(idx) * fxParamSlots;
        for (int b = 0; b < bands; ++b) {
            const auto slot = std::size_t(fx::sCount1 + b);
            counts[b] = std::clamp(int(std::lround(m.set[slot] ? m.p[slot] : 0.0)), 0, 8);
        }
        int total = 0; for (int b = 0; b < bands; ++b) total += counts[b];
        total = std::min(total, end - i - 1);
        const int splitAt = splitIndex_[std::size_t(idx)];
        if (!m.enabled || splitAt < 0 || depth >= 2) { i += 1; continue; } // branches then run in series as plain modules
        double* base = splitBuf_.data() + std::size_t(depth) * 6 * chunk;
        double* bl[3] = {base, base + 2 * chunk, base + 4 * chunk};
        double* br[3] = {base + chunk, base + 3 * chunk, base + 5 * chunk};
        auto& st = splits_[std::size_t(splitAt)];
        auto slotValue = [&](int slot, double def) {
            const auto table = fxParamTable(m.fxType);
            double v = m.set[std::size_t(slot)] ? m.p[std::size_t(slot)] : (std::size_t(slot) < table.size() ? table[std::size_t(slot)].def : def);
            const auto lo = std::size_t(slot) < table.size() ? table[std::size_t(slot)].lo : 0.0, hi = std::size_t(slot) < table.size() ? table[std::size_t(slot)].hi : 1.0;
            return applyModulation(v, off[slot], ParamRange{lo, hi, true});
        };
        if (m.fxType == FxType::splitMS) {
            for (int k = 0; k < n; ++k) { bl[0][k] = 0.5 * (l[k] + r[k]); br[0][k] = bl[0][k]; bl[1][k] = 0.5 * (l[k] - r[k]); br[1][k] = bl[1][k]; }
        } else {
            const double f1 = clampd(slotValue(fx::sFreq, 500.0), 30.0, 9000.0), f2 = clampd(slotValue(fx::sFreq2, 3000.0), 300.0, 12000.0);
            for (int c = 0; c < 2; ++c) { st.x1[c].set(f1, sr_); if (bands == 3) st.x2[c].set(std::max(f2, f1 * 1.2), sr_); }
            for (int k = 0; k < n; ++k) {
                double lo[2], hi[2];
                const double in[2] = {l[k], r[k]};
                for (int c = 0; c < 2; ++c) st.x1[c].process(in[c], lo[c], hi[c]);
                if (bands == 2) { bl[0][k] = lo[0]; br[0][k] = lo[1]; bl[1][k] = hi[0]; br[1][k] = hi[1]; }
                else {
                    double mid[2], top[2];
                    for (int c = 0; c < 2; ++c) st.x2[c].process(hi[c], mid[c], top[c]);
                    bl[0][k] = lo[0]; br[0][k] = lo[1]; bl[1][k] = mid[0]; br[1][k] = mid[1]; bl[2][k] = top[0]; br[2][k] = top[1];
                }
            }
        }
        int cursor = i + 1;
        for (int b = 0; b < bands; ++b) {
            processRange(rack, cursor, cursor + counts[b], bl[b], br[b], n, depth + 1);
            cursor += counts[b];
        }
        if (m.fxType == FxType::splitMS) {
            for (int k = 0; k < n; ++k) { l[k] = bl[0][k] + bl[1][k]; r[k] = br[0][k] - br[1][k]; }
        } else {
            for (int k = 0; k < n; ++k) {
                double sl = 0.0, sr = 0.0;
                for (int b = 0; b < bands; ++b) { sl += bl[b][k]; sr += br[b][k]; }
                l[k] = sl; r[k] = sr;
            }
        }
        i += 1 + total;
    }
}
}
