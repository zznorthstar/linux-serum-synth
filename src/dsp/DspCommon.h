#pragma once
// Shared small DSP utilities. Everything here is allocation-free on the audio
// thread (buffers are sized in prepare()) and denormal-safe.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace zyg::dsp {
inline constexpr double pi = 3.14159265358979323846;
inline constexpr double tau = 6.28318530717958647692;

inline double clampd(double v, double lo, double hi) noexcept { return std::min(hi, std::max(lo, v)); }
inline double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }
inline double dbToGain(double db) noexcept { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g) noexcept { return 20.0 * std::log10(std::max(g, 1.0e-9)); }
inline double flush(double v) noexcept {
    // Cheap denormal guard for feedback state.
    return (std::abs(v) < 1.0e-18 || !std::isfinite(v)) ? 0.0 : v;
}
inline double softClip(double x) noexcept { return std::tanh(x); }
// Rational tanh approximation (max error ~2%), for per-sample saturation in filters.
inline double fastTanh(double x) noexcept {
    if (x > 3.0) return 1.0;
    if (x < -3.0) return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}
inline double semitonesToRatio(double s) noexcept { return std::exp2(s / 12.0); }

// Deterministic xorshift32 generator. Returns [-1, 1) or [0, 1).
struct Rng {
    std::uint32_t s = 0x9e3779b9u;
    explicit Rng(std::uint32_t seed = 0x9e3779b9u) : s(seed ? seed : 1u) {}
    std::uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double bipolar() noexcept { return double(next()) / 2147483648.0 - 1.0; }
    double unipolar() noexcept { return double(next()) / 4294967296.0; }
};

// One-pole low-pass smoother.
struct Smoother {
    double value = 0.0, coeff = 1.0;
    void setTime(double seconds, double sampleRate) noexcept {
        coeff = seconds <= 0.0 ? 1.0 : 1.0 - std::exp(-1.0 / (seconds * sampleRate));
    }
    double step(double target) noexcept { value += coeff * (target - value); return value; }
};

// Trapezoidal (TPT) state-variable filter, one channel.
struct Svf {
    double ic1 = 0.0, ic2 = 0.0;
    double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
    struct Out { double low, band, high; };
    void set(double cutoffHz, double q, double sampleRate) noexcept {
        g = std::tan(pi * clampd(cutoffHz, 5.0, sampleRate * 0.49) / sampleRate);
        k = 1.0 / std::max(q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    Out process(double in) noexcept {
        const double v3 = in - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush(2.0 * v1 - ic1); ic2 = flush(2.0 * v2 - ic2);
        return {v2, v1, in - k * v1 - v2};
    }
    void reset() noexcept { ic1 = ic2 = 0.0; }
};

// One-pole TPT low-pass; high-pass is in - low.
struct OnePole {
    double s = 0.0, g = 0.0;
    void set(double cutoffHz, double sampleRate) noexcept {
        const double t = std::tan(pi * clampd(cutoffHz, 1.0, sampleRate * 0.49) / sampleRate);
        g = t / (1.0 + t);
    }
    double low(double in) noexcept {
        const double v = (in - s) * g; const double y = v + s; s = flush(y + v); return y;
    }
    double high(double in) noexcept { return in - low(in); }
    void reset() noexcept { s = 0.0; }
};

// First-order allpass with a settable break frequency.
struct Allpass1 {
    double s = 0.0, a = 0.0;
    void set(double hz, double sampleRate) noexcept {
        const double t = std::tan(pi * clampd(hz, 1.0, sampleRate * 0.49) / sampleRate);
        a = (t - 1.0) / (t + 1.0);
    }
    double process(double in) noexcept {
        const double y = a * in + s; s = flush(in - a * y); return y;
    }
    void reset() noexcept { s = 0.0; }
};

// RBJ biquad, transposed direct form II.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void setLowpass(double f, double q, double sr) noexcept {
        const double w = tau * clampd(f, 5.0, sr * 0.49) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass(double f, double q, double sr) noexcept {
        const double w = tau * clampd(f, 5.0, sr * 0.49) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setBandpass(double f, double q, double sr) noexcept {
        const double w = tau * clampd(f, 5.0, sr * 0.49) / sr, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        norm(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void setPeaking(double f, double q, double gainDb, double sr) noexcept {
        const double A = std::pow(10.0, gainDb / 40.0), w = tau * clampd(f, 5.0, sr * 0.49) / sr;
        const double c = std::cos(w), al = std::sin(w) / (2.0 * q);
        norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf(double f, double gainDb, double sr) noexcept {
        const double A = std::pow(10.0, gainDb / 40.0), w = tau * clampd(f, 5.0, sr * 0.49) / sr;
        const double c = std::cos(w), al = std::sin(w) / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * al;
        norm(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void setHighShelf(double f, double gainDb, double sr) noexcept {
        const double A = std::pow(10.0, gainDb / 40.0), w = tau * clampd(f, 5.0, sr * 0.49) / sr;
        const double c = std::cos(w), al = std::sin(w) / 2.0 * std::sqrt(2.0), sq = 2.0 * std::sqrt(A) * al;
        norm(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    double process(double x) noexcept {
        const double y = b0 * x + z1;
        z1 = flush(b1 * x - a1 * y + z2); z2 = flush(b2 * x - a2 * y);
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0; }
private:
    void norm(double B0, double B1, double B2, double A0, double A1, double A2) noexcept {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

// Linkwitz-Riley 4th-order crossover section (low/high outputs sum to allpass).
struct Crossover {
    Biquad lp1, lp2, hp1, hp2;
    void set(double f, double sr) noexcept {
        const double q = 0.70710678118654752;
        lp1.setLowpass(f, q, sr); lp2.setLowpass(f, q, sr);
        hp1.setHighpass(f, q, sr); hp2.setHighpass(f, q, sr);
    }
    void process(double in, double& low, double& high) noexcept {
        low = lp2.process(lp1.process(in)); high = hp2.process(hp1.process(in));
    }
    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
};

// Circular delay line with cubic (Catmull-Rom) fractional read. The buffer is
// sized by allocate(), which must be called off the audio thread.
class DelayLine {
public:
    void allocate(std::size_t maxSamples) {
        std::size_t n = 8;
        while (n < maxSamples + 4) n <<= 1;
        buf_.assign(n, 0.0f); mask_ = n - 1; w_ = 0;
    }
    void clear() noexcept { std::fill(buf_.begin(), buf_.end(), 0.0f); w_ = 0; }
    void push(double x) noexcept { buf_[w_] = float(x); w_ = (w_ + 1) & mask_; }
    // Delay in samples (>= 1).
    double read(double delay) const noexcept {
        const double d = std::max(delay, 1.0);
        const double pos = double(w_) - d - 1.0;
        const double fl = std::floor(pos); const double f = pos - fl;
        const auto i = std::size_t(std::int64_t(fl)) & mask_;
        const double y0 = buf_[(i - 1) & mask_], y1 = buf_[i], y2 = buf_[(i + 1) & mask_], y3 = buf_[(i + 2) & mask_];
        return y1 + 0.5 * f * (y2 - y0 + f * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + f * (3.0 * (y1 - y2) + y3 - y0)));
    }
    double readLinear(double delay) const noexcept {
        const double d = std::max(delay, 1.0);
        const double pos = double(w_) - d;
        const double fl = std::floor(pos); const double f = pos - fl;
        const auto i = std::size_t(std::int64_t(fl)) & mask_;
        return buf_[i] + f * (buf_[(i + 1) & mask_] - buf_[i]);
    }
    std::size_t capacity() const noexcept { return buf_.size(); }
    bool allocated() const noexcept { return !buf_.empty(); }
private:
    std::vector<float> buf_;
    std::size_t mask_ = 0, w_ = 0;
};

// Sine LFO / phase helpers.
inline double wrap01(double p) noexcept { return p - std::floor(p); }

// Tempo helpers: beats per second at `bpm`.
inline double beatsPerSecond(double bpm) noexcept { return std::max(bpm, 1.0) / 60.0; }
}
