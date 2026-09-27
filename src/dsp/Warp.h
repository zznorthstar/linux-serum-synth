#pragma once
// Oscillator warp transfer functions (phase remaps, distortions, AM/RM/FM
// helpers). All are ZYG designs organised by the categories in Serum's manual.
#include "DspCommon.h"
#include "Patch.h"

namespace zyg::dsp {

struct WarpState {
    double lp = 0.0, hp = 0.0;   // per-oscillator warp filter state
    double lastOut = 0.0;        // for self phase modulation
};

inline bool isPhaseWarp(WarpMode m) noexcept {
    switch (m) {
        case WarpMode::bendPositive: case WarpMode::bendNegative: case WarpMode::bendBoth:
        case WarpMode::asymPositive: case WarpMode::asymNegative: case WarpMode::asymBoth:
        case WarpMode::pwm: case WarpMode::sync: case WarpMode::remap: return true;
        default: return false;
    }
}
inline bool isModulatorWarp(WarpMode m) noexcept {
    switch (m) {
        case WarpMode::frequencyMod: case WarpMode::frequencyModX: case WarpMode::frequencyModPhase:
        case WarpMode::phaseMod: case WarpMode::ringMod: case WarpMode::amplitudeMod: return true;
        default: return false;
    }
}
inline bool isSpectralWarp(WarpMode m) noexcept {
    return m >= WarpMode::addHarmonics && m <= WarpMode::mask;
}
// Warps that make a wavetable oscillator need the 2x oversampled path.
inline bool warpNeedsOversampling(WarpMode m) noexcept {
    return m != WarpMode::off && m != WarpMode::unknown && !isSpectralWarp(m);
}

inline double warpPhase(double phase, WarpMode mode, double amount, double variant) noexcept {
    const double p = phase - std::floor(phase);
    const double a = clampd(amount, 0.0, 1.0);
    auto symmetricBend = [&](double bend) noexcept {
        const double exponent = bend >= 0.0 ? 1.0 + 7.0 * bend : 1.0 / (1.0 - 7.0 * bend);
        return p < 0.5 ? 0.5 * std::pow(2.0 * p, exponent) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - p), exponent);
    };
    auto asymmetricBend = [&](double bend) noexcept {
        const double exponent = bend >= 0.0 ? 1.0 + 7.0 * bend : 1.0 / (1.0 - 7.0 * bend);
        return std::pow(p, exponent);
    };
    switch (mode) {
        case WarpMode::bendPositive: return symmetricBend(a);
        case WarpMode::bendNegative: return symmetricBend(-a);
        case WarpMode::bendBoth: return symmetricBend(2.0 * a - 1.0);
        case WarpMode::asymPositive: return asymmetricBend(a);
        case WarpMode::asymNegative: return asymmetricBend(-a);
        case WarpMode::asymBoth: return asymmetricBend(2.0 * a - 1.0);
        case WarpMode::pwm: {
            const double split = clampd(0.5 + 0.45 * a, 0.05, 0.95);
            return p < split ? 0.5 * p / split : 0.5 + 0.5 * (p - split) / (1.0 - split);
        }
        case WarpMode::sync: return wrap01(p * (1.0 + 7.0 * a));
        case WarpMode::remap: {
            switch (int(variant)) {
                case 1: return p - a * std::sin(tau * p) / tau * 0.98;
                case 2: { const double n = 8.0; return lerp(p, (std::floor(p * n) + 0.5) / n, a); }
                case 3: return lerp(p, 1.0 - std::abs(2.0 * p - 1.0), a);
                default: return lerp(p, wrap01(p * 3.0), a);
            }
        }
        default: return p;
    }
}
// Multiplier applied to the oscillator frequency for alias control after warp.
inline double warpFrequencyFactor(WarpMode mode, double amount, double variant) noexcept {
    const double a = clampd(amount, 0.0, 1.0);
    if (mode == WarpMode::sync) return 1.0 + 7.0 * a;
    if (mode == WarpMode::remap && int(variant) == 4) return 1.0 + 2.0 * a;
    if (mode == WarpMode::bendPositive || mode == WarpMode::bendNegative || mode == WarpMode::bendBoth
        || mode == WarpMode::asymPositive || mode == WarpMode::asymNegative || mode == WarpMode::asymBoth)
        return 1.0 + 3.0 * a;
    return 1.0;
}

inline double foldLinear(double input) noexcept {
    double value = std::fmod(input + 1.0, 4.0);
    if (value < 0.0) value += 4.0;
    return value <= 2.0 ? value - 1.0 : 3.0 - value;
}

// Normalised drive shapers used for both warp distortions and FX distortion.
inline double shapeDiode1(double x, double d) noexcept {
    const double n = 1.0 - std::exp(-d);
    return x >= 0.0 ? (1.0 - std::exp(-d * x)) / n : -(1.0 - std::exp(0.5 * d * x)) * 0.5 / n;
}
inline double shapeDiode2(double x, double d) noexcept {
    const double n = 1.0 - std::exp(-d);
    return (x >= 0.0 ? 1.0 : -1.0) * (1.0 - std::exp(-d * std::abs(x))) / n;
}
inline double shapeSoftSat(double x, double d) noexcept { const double z = clampd(x * d, -1.0, 1.0); return 1.5 * z - 0.5 * z * z * z; }
inline double shapeTape(double x, double d) noexcept { return std::tanh(d * x + 0.12 * d * x * x) / std::tanh(d); }
inline double shapeTube(double x, double d) noexcept { return (std::tanh(d * x + 0.3) - std::tanh(0.3)) / (std::tanh(d + 0.3) - std::tanh(0.3)); }
inline double shapeStomp(double x, double d) noexcept {
    const double z = x * d; return clampd(z * 0.9 + 0.1 * std::tanh(z * 3.0), -0.8, 0.8) / 0.8;
}
inline double shapeZeroSquare(double x, double a) noexcept {
    const double s = std::tanh(x * 60.0); return lerp(x, s, a);
}

inline double warpAmplitude(double sample, double lookupPhase, WarpMode mode, double amount,
                            double modulator, WarpState& st, double sampleRate, double var = 0.5) noexcept {
    const double a = clampd(amount, 0.0, 1.0);
    if (a <= 1.0e-9 && mode != WarpMode::filterLow && mode != WarpMode::filterHigh) return sample;
    switch (mode) {
        case WarpMode::ringMod: return sample * ((1.0 - a) + a * modulator);
        case WarpMode::amplitudeMod: return sample * ((1.0 - a) + a * 0.5 * (modulator + 1.0));
        case WarpMode::flip: return lookupPhase >= 1.0 - a ? -sample : sample;
        case WarpMode::hardClip: return clampd(sample * (1.0 + 12.0 * a), -1.0, 1.0);
        case WarpMode::softClip: { const double d = 1.0 + 8.0 * a; return std::tanh(sample * d) / std::tanh(d); }
        case WarpMode::sineFold: { const double s = std::sin(sample * (1.0 + 4.0 * a) * 1.5707963267948966); return sample + a * (s - sample); }
        case WarpMode::linearFold: { const double s = foldLinear(sample * (1.0 + 8.0 * a)); return sample + a * (s - sample); }
        case WarpMode::sineShaper: { const double s = std::sin(sample * pi * (0.5 + 2.5 * a)); return sample + a * (s - sample); }
        case WarpMode::asymmetricClip: {
            const double pos = std::tanh(sample * (1.0 + 10.0 * a)), neg = std::tanh(sample * (1.0 + 3.0 * a));
            return sample >= 0.0 ? pos : neg;
        }
        case WarpMode::rectify: return sample + a * (std::abs(sample) - sample);
        case WarpMode::diode1: return shapeDiode1(sample, 1.0 + 7.0 * a);
        case WarpMode::diode2: return shapeDiode2(sample, 1.0 + 7.0 * a);
        case WarpMode::softSat: return shapeSoftSat(sample, 1.0 + 3.0 * a);
        case WarpMode::tapeSat: return shapeTape(sample, 1.0 + 5.0 * a);
        case WarpMode::tube: return shapeTube(sample, 1.0 + 6.0 * a);
        case WarpMode::stompBox: return shapeStomp(sample, 1.0 + 9.0 * a);
        case WarpMode::zeroSquare: return shapeZeroSquare(sample, a);
        case WarpMode::quantize: {
            const double levels = 2.0 + (1.0 - a) * 254.0;
            return std::round(sample * levels * 0.5) / (levels * 0.5);
        }
        case WarpMode::filterLow: case WarpMode::filterHigh: {
            const double hz = 20.0 * std::pow(1000.0, mode == WarpMode::filterLow ? 1.0 - a : a);
            const double g = std::tan(pi * std::min(hz, sampleRate * 0.45) / sampleRate);
            const double gg = g / (1.0 + g);
            const double v = (sample - st.lp) * gg; const double low = v + st.lp; st.lp = flush(low + v);
            return mode == WarpMode::filterLow ? low : sample - low;
        }
        default: return sample;
    }
}
}
