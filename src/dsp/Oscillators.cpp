#include "Oscillators.h"

namespace zyg::dsp {
namespace {
double polyBlep(double phase, double increment) noexcept {
    const double dt = clampd(std::abs(increment), 1.0e-9, 0.5);
    if (phase < dt) { const double x = phase / dt; return x + x - x * x - 1.0; }
    if (phase > 1.0 - dt) { const double x = (phase - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
}
double subWave(SubShape shape, double phase, double increment) noexcept {
    const double p = phase - std::floor(phase);
    switch (shape) {
        case SubShape::saw: return 2.0 * p - 1.0 - polyBlep(p, increment);
        case SubShape::square: case SubShape::pulse: {
            const double duty = shape == SubShape::pulse ? 0.25 : 0.5;
            double value = p < duty ? 1.0 : -1.0;
            value += polyBlep(p, increment);
            double falling = p - duty; if (falling < 0.0) falling += 1.0;
            return value - polyBlep(falling, increment);
        }
        case SubShape::triangle: return 1.0 - 4.0 * std::abs(p - 0.5);
        case SubShape::roundedRectangle: return std::tanh(2.5 * std::sin(tau * p)) / std::tanh(2.5);
        default: return std::sin(tau * p);
    }
}

double hermiteAt(const float* d, long n, double pos, bool wrap) noexcept {
    const long i = long(std::floor(pos));
    const double t = pos - double(i);
    auto g = [&](long k) noexcept -> double {
        if (wrap) { k %= n; if (k < 0) k += n; return d[k]; }
        if (k < 0 || k >= n) return 0.0;
        return d[k];
    };
    const double y0 = g(i - 1), y1 = g(i), y2 = g(i + 1), y3 = g(i + 2);
    return y1 + 0.5 * t * (y2 - y0 + t * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + t * (3.0 * (y1 - y2) + y3 - y0)));
}

float tableAtLevel(const Oscillator& osc, unsigned level, double phase, double position) noexcept {
    const auto size = osc.frameSize;
    const auto frames = osc.audio.size() / size;
    if (!frames || size < 2) return 0.0f;
    const float* samples = osc.audio.data();
    if (level > 0 && level <= osc.wavetableMipLevels && osc.wavetableMipmaps
        && osc.wavetableMipmaps->size() >= std::size_t(level) * osc.audio.size())
        samples = osc.wavetableMipmaps->data() + std::size_t(level - 1) * osc.audio.size();
    const double scaled = clampd(position / 256.0, 0.0, 1.0) * double(frames - 1);
    const std::size_t frame = std::size_t(scaled);
    const double mix = scaled - double(frame);
    const double p = phase * size;
    const auto i = std::size_t(p) % size;
    const double fraction = p - std::floor(p);
    const auto a = frame * size;
    const auto b = std::min(frame + 1, frames - 1) * size;
    auto cubic = [&](std::size_t base) noexcept {
        const double y0 = samples[base + (i + size - 1) % size], y1 = samples[base + i];
        const double y2 = samples[base + (i + 1) % size], y3 = samples[base + (i + 2) % size];
        return y1 + 0.5 * fraction * (y2 - y0 + fraction * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3
            + fraction * (3.0 * (y1 - y2) + y3 - y0)));
    };
    const double x = cubic(a), y = cubic(b);
    return float(x + mix * (y - x));
}
float tableAt(const Oscillator& osc, double phase, double position, double frequency, double sr) noexcept {
    if (osc.wavetableMipLevels == 0 || frequency <= 0.0) return tableAtLevel(osc, 0, phase, position);
    const double mipPosition = clampd(std::log2(double(osc.frameSize) * frequency / sr), 0.0, double(osc.wavetableMipLevels));
    const auto lower = unsigned(std::floor(mipPosition));
    const auto upper = std::min(lower + 1, osc.wavetableMipLevels);
    const double mix = mipPosition - lower;
    const double a = tableAtLevel(osc, lower, phase, position);
    if (upper == lower) return float(a);
    return float(a + mix * (tableAtLevel(osc, upper, phase, position) - a));
}

double windowValue(WindowShape shape, double x, double param, double skew) noexcept {
    x = clampd(x, 0.0, 1.0);
    if (std::abs(skew) > 1.0e-6) x = std::pow(x, std::exp2(clampd(skew, -100.0, 100.0) / 100.0 * 1.5));
    const double a = clampd(param / 100.0, 0.0, 1.0);
    switch (shape) {
        case WindowShape::blackmanHarris:
            return 0.35875 - 0.48829 * std::cos(tau * x) + 0.14128 * std::cos(2.0 * tau * x) - 0.01168 * std::cos(3.0 * tau * x);
        case WindowShape::expDecay: return std::min(x / 0.02, 1.0) * std::exp(-(2.0 + 6.0 * a) * x);
        case WindowShape::gaussian: { const double sg = 0.1 + 0.3 * a; const double d = (x - 0.5) / sg; return std::exp(-0.5 * d * d); }
        case WindowShape::triangle: return 1.0 - std::abs(2.0 * x - 1.0);
        case WindowShape::tukey: {
            const double alpha = std::max(a, 0.02);
            if (x < alpha * 0.5) return 0.5 * (1.0 - std::cos(tau * x / alpha));
            if (x > 1.0 - alpha * 0.5) return 0.5 * (1.0 - std::cos(tau * (1.0 - x) / alpha));
            return 1.0;
        }
    }
    return 1.0;
}

double stackSemis(UnisonStack stack, int u, int n) noexcept {
    switch (stack) {
        case UnisonStack::center12: return (n > 1 && u == n / 2) ? 12.0 : 0.0;
        case UnisonStack::octave1: return 12.0 * (u % 2);
        case UnisonStack::octave2: return 12.0 * (u % 3);
        case UnisonStack::octave3: return 12.0 * (u % 4);
        case UnisonStack::octaveFifth1: { static constexpr double t[2] = {0, 7}; return t[u % 2]; }
        case UnisonStack::octaveFifth2: { static constexpr double t[3] = {0, 7, 12}; return t[u % 3]; }
        case UnisonStack::octaveFifth3: { static constexpr double t[4] = {0, 7, 12, 19}; return t[u % 4]; }
        default: return 0.0;
    }
}
double superCurve(double s) noexcept {
    static constexpr double t[7] = {-1.0, -0.5716, -0.1774, 0.0, 0.1810, 0.5650, 0.9766};
    const double pos = clampd((s + 1.0) * 3.0, 0.0, 6.0);
    const int i = std::min(5, int(pos));
    return lerp(t[i], t[i + 1], pos - i);
}

double pitchOffsetSemis(const OscInputs& in) noexcept {
    const auto& v = *in.v;
    const double pitchKnob = in.osc->pitchMode == OscPitchMode::semitones ? at(v, OscParam::pitch) : 0.0;
    return 12.0 * at(v, OscParam::octave) + at(v, OscParam::coarse) + pitchKnob + at(v, OscParam::fine) / 100.0;
}
}

double oscBaseValue(const Oscillator& o, OscParam p) noexcept {
    switch (p) {
        case OscParam::volume: return o.volume; case OscParam::pan: return o.pan;
        case OscParam::coarse: return o.semitone; case OscParam::fine: return o.fine;
        case OscParam::octave: return o.octave; case OscParam::detune: return o.detune;
        case OscParam::blend: return o.blend; case OscParam::unisonStereo: return o.unisonStereo;
        case OscParam::unisonWarp: return o.unisonWarp; case OscParam::unisonWarp2: return o.unisonWarp2;
        case OscParam::unisonWTPos: return o.unisonWTPos; case OscParam::tablePos: return o.tablePosition;
        case OscParam::warp1: return o.warpOneAmount; case OscParam::warp2: return o.warpTwoAmount;
        case OscParam::warpVar1: return o.warpDefinitions[0].var; case OscParam::warpVar2: return o.warpDefinitions[1].var;
        case OscParam::initialPhase: return o.initialPhase; case OscParam::randomPhase: return o.randomPhase;
        case OscParam::start: return o.start; case OscParam::end: return o.end;
        case OscParam::position: return o.position; case OscParam::scanRate: return o.scanRate;
        case OscParam::loopStart: return o.loopStart; case OscParam::loopEnd: return o.loopEnd;
        case OscParam::pitch: return o.pitch; case OscParam::pitchRatio: return o.pitchRatio;
        case OscParam::hzOffset: return o.hzOffset; case OscParam::grainLength: return o.grainLength;
        case OscParam::density: return o.density; case OscParam::randomOffset: return o.randomOffset;
        case OscParam::randomPitch: return o.randomPitch; case OscParam::randomPan: return o.randomPan;
        case OscParam::randomGain: return o.randomGain; case OscParam::randomDir: return o.randomDir;
        case OscParam::randomGrainLength: return o.randomGrainLength; case OscParam::windowParam: return o.windowParam;
        case OscParam::windowSkew: return o.windowSkew; case OscParam::timbreShift: return o.timbreShift;
        case OscParam::envAttack: return o.sampleEnv.attack; case OscParam::envDecay: return o.sampleEnv.decay;
        case OscParam::envSustain: return o.sampleEnv.sustain; case OscParam::envRelease: return o.sampleEnv.release;
        case OscParam::color: return o.noiseColor; case OscParam::freqLo: return o.freqLo;
        case OscParam::freqHi: return o.freqHi; case OscParam::specShift: return o.specFilterShift;
        case OscParam::specWet: return o.specFilterWet; case OscParam::randomWarp: return o.randomWarp;
        case OscParam::randomWarp2: return o.randomWarp2;
        default: return 0.0;
    }
}

void OscVoiceState::allocate(SpectralShared* shared) {
    if (shared) for (auto& s : spectral) { if (!s) s = std::make_unique<SpectralVoice>(); if (!s->allocated()) s->allocate(shared); }
}
void OscVoiceState::reset() noexcept {
    phase.fill(0.0); pos.fill(0.0); dir.fill(1); finished.fill(false); uni = {}; unisonCount = 1;
    for (auto& o : os) o = OversampleState{};
    for (auto& a : align) a.fill(0.0);
    alignW = 0; warpState = {}; lastOut = 0.0; regionCount = 0; regPos.fill(0.0); regDir.fill(1); regDone.fill(false);
    sliceStart = sliceEnd = -1.0; sliceSilent = false;
    sampleEnv.reset(); rows.fill(0.0); counter = 0; brown = 0.0; click = 0.0; noisePos = 0.0; noiseDone = false;
    colorLp.reset(); colorHp.reset();
    for (auto& g : grains) g.on = false;
    grainTimer.fill(0.0); timeline = 0.0; elapsed = 0.0; windowShape = -1;
}

void oscBlockUpdate(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    int count = std::clamp(osc.unison, 1, OscVoiceState::maxUnison);
    if (osc.mode == OscMode::sub || osc.mode == OscMode::noise) count = 1;
    if (osc.mode == OscMode::spectral) count = std::min(count, 4);
    s.unisonCount = count;
    const double detune = clampd(at(v, OscParam::detune), 0.0, 1.0);
    const double range = std::max(0.0, osc.unisonRange);
    const double maxCents = 100.0 * range * std::pow(detune, 1.5);
    const double blend = clampd(at(v, OscParam::blend) / 100.0, 0.0, 1.0);
    const double stereo = clampd(at(v, OscParam::unisonStereo) / 100.0, -1.0, 1.0);
    double weightSq = 0.0;
    // "Centredness" is measured from the most central voice so an even stack (no exact centre) still sounds at Blend 0.
    const double minAbs = count > 1 ? ((count % 2) ? 0.0 : 1.0 / (count - 1)) : 0.0;
    for (int u = 0; u < count; ++u) {
        auto& x = s.uni[std::size_t(u)];
        const double sp = count > 1 ? 2.0 * u / (count - 1) - 1.0 : 0.0;
        double shape = sp;
        switch (osc.detuneMode) {
            case DetuneMode::exponential: shape = (sp < 0 ? -1.0 : 1.0) * sp * sp; break;
            case DetuneMode::inverse: shape = (sp < 0 ? -1.0 : 1.0) * std::sqrt(std::abs(sp)); break;
            case DetuneMode::random: {
                std::uint32_t h = std::uint32_t(u) * 2654435761u + 0x9e3779b9u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                shape = double(h) / 2147483648.0 - 1.0; break;
            }
            case DetuneMode::super: shape = superCurve(sp); break;
            default: break;
        }
        x.s = sp;
        x.semis = shape * maxCents / 100.0 + stackSemis(osc.unisonStack, u, count);
        x.ratio = std::exp2(x.semis / 12.0);
        x.gain = count > 1 ? lerp(1.0 - clampd((std::abs(sp) - minAbs) / std::max(1.0 - minAbs, 1.0e-9), 0.0, 1.0), 1.0, blend) : 1.0;
        weightSq += x.gain * x.gain;
        const double pan = clampd(sp * stereo, -1.0, 1.0);
        x.gL = std::sqrt(1.0 - pan) ; x.gR = std::sqrt(1.0 + pan);
        x.wtOff = sp * at(v, OscParam::unisonWTPos) / 100.0 * 128.0;
        x.warp1 = sp * at(v, OscParam::unisonWarp) / 100.0 * 0.5;
        x.warp2 = sp * at(v, OscParam::unisonWarp2) / 100.0 * 0.5;
    }
    const double norm = count > 1 ? 1.0 / std::sqrt(std::max(weightSq, 1.0e-9)) : 1.0;
    for (int u = 0; u < count; ++u) {
        auto& x = s.uni[std::size_t(u)];
        x.gain *= norm;
        if (count == 1) { x.gL = 1.0; x.gR = 1.0; }
    }
    // base frequency: note pitch, oscillator pitch knobs, ratio mode and Hz offset
    double semis = pitchOffsetSemis(in);
    if (osc.pitchTrack) semis += in.semis; else semis += -9.0;
    s.baseSemis = semis;
    double f = in.masterTuning * std::exp2(semis / 12.0);
    if (osc.pitchMode == OscPitchMode::ratio) { const double r = at(v, OscParam::pitchRatio); f *= r < 0.0 ? 1.0 / (1.0 - r) : (r == 0.0 ? 1.0 : r); }
    else if (osc.pitchMode == OscPitchMode::harmonics) { const double h = at(v, OscParam::pitch); f *= h < 0.0 ? 1.0 / (1.0 - h) : (h < 1.0 ? 1.0 : h); }
    f += at(v, OscParam::hzOffset);
    s.baseFreq = std::max(0.01, f);
}

void oscNoteOn(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    s.reset();
    s.rng = Rng(0x9e3779b9u ^ std::uint32_t(in.note * 334214467u) ^ std::uint32_t(in.index * 2246822519u) ^ std::uint32_t(in.serial * 2654435761u));
    oscBlockUpdate(s, in);
    const int count = s.unisonCount;
    switch (osc.mode) {
        case OscMode::wavetable: case OscMode::sub: {
            const double randomRange = clampd(at(v, OscParam::randomPhase) / 100.0, 0.0, 1.0);
            for (int u = 0; u < count; ++u) {
                const double randomOffset = s.rng.unipolar() * randomRange;
                s.phase[std::size_t(u)] = std::fmod(at(v, OscParam::initialPhase) / 360.0 + double(u) / count + randomOffset, 1.0);
            }
            break;
        }
        case OscMode::sample: case OscMode::multisample: {
            const SampleData* sd = osc.sample.get();
            if (osc.mode == OscMode::multisample) {
                s.regionCount = 0;
                for (std::size_t i = 0; i < osc.regions.size() && s.regionCount < 4; ++i) {
                    const auto& r = osc.regions[i];
                    if (r.sample && in.note >= r.loKey && in.note <= r.hiKey
                        && int(in.velocity * 127.0 + 0.5) >= r.loVel && int(in.velocity * 127.0 + 0.5) <= r.hiVel)
                        s.regionIdx[std::size_t(s.regionCount++)] = int(i);
                }
                if (s.regionCount) sd = osc.regions[std::size_t(s.regionIdx[0])].sample.get();
                if (osc.sampleEnv.override_ || osc.sampleEnv.useSfzRelease) s.sampleEnv.noteOn(0.0);
            }
            if (!sd || sd->frames() < 2) break;
            const double n = double(sd->frames());
            double startF = clampd(at(v, OscParam::start) / 100.0, 0.0, 1.0) * n;
            double endF = clampd(at(v, OscParam::end) / 100.0, 0.0, 1.0) * n;
            if (osc.mode == OscMode::sample && osc.slicingMode != 0 && !osc.sliceMarkers.empty()) {
                // Slicing: each key from the slice root triggers one slice, played once.
                const int si = in.note - osc.sliceRoot;
                if (si < 0 || std::size_t(si) >= osc.sliceMarkers.size()) s.sliceSilent = true;
                else {
                    startF = osc.sliceMarkers[std::size_t(si)] * n;
                    endF = std::size_t(si) + 1 < osc.sliceMarkers.size() ? osc.sliceMarkers[std::size_t(si) + 1] * n : n;
                    s.sliceStart = startF; s.sliceEnd = endF;
                }
            }
            for (int u = 0; u < count; ++u) {
                const bool rev = osc.reverse || osc.loopMode == LoopMode::reverse;
                const double jitter = s.rng.unipolar() * osc.randomStart / 100.0 * n;
                s.pos[std::size_t(u)] = rev ? std::max(startF, (endF > startF ? endF : n) - 1.0 - jitter) : std::min(startF + jitter, n - 2.0);
                s.dir[std::size_t(u)] = rev ? -1 : 1;
            }
            for (int k = 1; k < s.regionCount; ++k) { s.regPos[std::size_t(k)] = 0.0; s.regDir[std::size_t(k)] = 1; }
            break;
        }
        case OscMode::noise: {
            s.colorLp.reset(); s.colorHp.reset();
            const double n = double(osc.audio.size());
            if (n > 0.0) s.noisePos = clampd(at(v, OscParam::initialPhase) / 100.0 + s.rng.unipolar() * clampd(at(v, OscParam::randomPhase) / 100.0, 0.0, 1.0), 0.0, 0.999) * n;
            break;
        }
        case OscMode::granular: {
            s.timeline = clampd(at(v, OscParam::position) / 100.0, 0.0, 1.0);
            const double dens = std::max(at(v, OscParam::density), 0.5);
            for (int u = 0; u < count; ++u) {
                const double frac = osc.unisonTrigPattern == 2 ? s.rng.unipolar() : double(u) / count;
                s.grainTimer[std::size_t(u)] = frac * in.sampleRate / dens;
            }
            break;
        }
        case OscMode::spectral:
            for (int u = 0; u < count; ++u) if (s.spectral[std::size_t(u)]) s.spectral[std::size_t(u)]->reset(std::uint32_t(s.rng.next()));
            s.timeline = 0.0;
            break;
        default: break;
    }
}

void oscNoteOff(OscVoiceState& s, const OscInputs& in) noexcept {
    if (in.osc->mode == OscMode::multisample && (in.osc->sampleEnv.override_ || in.osc->sampleEnv.useSfzRelease)) s.sampleEnv.noteOff();
}

namespace {
// ---- wavetable / SUB
OscOut renderTable(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    OscOut out;
    const bool sub = osc.mode == OscMode::sub;
    if (!sub && osc.audio.empty()) return out;
    const bool os2 = oscUsesOversampling(osc);
    const int count = s.unisonCount;
    const bool mono = count == 1;
    out.oversampled = os2;
    const double amount0 = at(v, OscParam::warp1), amount1 = at(v, OscParam::warp2);
    const double var0 = at(v, OscParam::warpVar1), var1 = at(v, OscParam::warpVar2);
    std::array<double, 2> modulators {};
    for (std::size_t slot = 0; slot < 2; ++slot) {
        const int src = osc.warpDefinitions[slot].sourceIndex;
        if (src >= 0 && src < 7 && in.mods) modulators[slot] = (*in.mods)[std::size_t(src)];
    }
    auto renderSub = [&](double rateScale, double& outL, double& outR) noexcept {
        double accL = 0.0, accR = 0.0;
        for (int u = 0; u < count; ++u) {
            const auto& un = s.uni[std::size_t(u)];
            const double fu = s.baseFreq * un.ratio;
            double& phase = s.phase[std::size_t(u)];
            double freqScale = 1.0, lookup = phase;
            const std::array<double, 2> amt {clampd(amount0 + un.warp1, 0.0, 1.0), clampd(amount1 + un.warp2, 0.0, 1.0)};
            const std::array<double, 2> vr {var0, var1};
            double freqMip = 1.0;
            for (std::size_t slot = 0; slot < 2; ++slot) {
                const auto& w = osc.warpDefinitions[slot];
                const double a = amt[slot], m = modulators[slot];
                switch (w.mode) {
                    case WarpMode::frequencyMod: freqScale += 8.0 * a * m; break;
                    case WarpMode::frequencyModX: freqScale *= std::exp2(3.0 * a * m); break;
                    case WarpMode::frequencyModPhase: lookup += 2.0 * a * m; break;
                    case WarpMode::phaseMod: lookup += 0.25 * a * m; break;
                    case WarpMode::selfPhase: lookup += 0.5 * a * s.lastOut; break;
                    default:
                        if (isPhaseWarp(w.mode)) { lookup = warpPhase(lookup, w.mode, a, w.variant); freqMip *= warpFrequencyFactor(w.mode, a, w.variant); }
                        break;
                }
            }
            lookup -= std::floor(lookup);
            const double effFreq = std::abs(fu * freqScale) * freqMip;
            double sample;
            if (sub) sample = subWave(osc.subShape, lookup, effFreq / in.sampleRate);
            else sample = tableAt(osc, lookup, at(v, OscParam::tablePos) + un.wtOff, effFreq, in.sampleRate);
            for (std::size_t slot = 0; slot < 2; ++slot) {
                const auto& w = osc.warpDefinitions[slot];
                if (w.mode == WarpMode::evenOdd) {
                    const double other = sub ? subWave(osc.subShape, lookup + 0.5, effFreq / in.sampleRate)
                                             : tableAt(osc, wrap01(lookup + 0.5), at(v, OscParam::tablePos) + un.wtOff, effFreq, in.sampleRate);
                    sample += (amt[slot] - 0.5) * 2.0 * other;
                    sample *= 0.7;
                } else if (!isPhaseWarp(w.mode) && !isSpectralWarp(w.mode) && w.mode != WarpMode::frequencyMod
                           && w.mode != WarpMode::frequencyModX && w.mode != WarpMode::frequencyModPhase
                           && w.mode != WarpMode::phaseMod && w.mode != WarpMode::selfPhase && w.mode != WarpMode::off
                           && w.mode != WarpMode::unknown) {
                    sample = warpAmplitude(sample, lookup, w.mode, amt[slot], modulators[slot], s.warpState[slot], in.sampleRate * (os2 ? 2.0 : 1.0), vr[slot]);
                }
            }
            accL += sample * un.gain * un.gL; accR += sample * un.gain * un.gR;
            phase += fu * freqScale * rateScale / in.sampleRate;
            phase -= std::floor(phase);
        }
        outL = accL; outR = accR;
    };
    double l = 0.0, r = 0.0;
    const bool four = in.oversampleLevel >= 1;
    if (os2 && !four) {
        double l0, r0, l1, r1;
        renderSub(0.5, l0, r0); renderSub(0.5, l1, r1);
        l = downsample2x(s.os[0], l0, l1);
        r = mono ? l : downsample2x(s.os[1], r0, r1);
    } else if (os2) {
        double sl[4], sr4[4];
        for (int k = 0; k < 4; ++k) renderSub(0.25, sl[k], sr4[k]);
        const double la = downsample2x(s.os[0], sl[0], sl[1]), lb = downsample2x(s.os[0], sl[2], sl[3]);
        l = downsample2x(s.os[2], la, lb);
        if (mono) r = l;
        else {
            const double ra = downsample2x(s.os[1], sr4[0], sr4[1]), rb = downsample2x(s.os[1], sr4[2], sr4[3]);
            r = downsample2x(s.os[3], ra, rb);
        }
    } else {
        double a, b;
        renderSub(1.0, a, b);
        // Unwarped sources are delayed by the decimator latency so layered oscillators stay in phase.
        const std::size_t len = four ? 23 : 15;
        auto delay = [&](std::size_t ch, double x) noexcept { const double d = s.align[ch][s.alignW]; s.align[ch][s.alignW] = x; return d; };
        l = delay(0, a); r = mono ? l : delay(1, b);
        if (mono) s.align[1][s.alignW] = 0.0;
        s.alignW = (s.alignW + 1) % len;
    }
    s.lastOut = 0.5 * (l + r);
    out.l = l; out.r = r; out.raw = 0.5 * (l + r);
    return out;
}

// ---- noise
OscOut renderNoise(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    OscOut out;
    double x = 0.0;
    if (!osc.audio.empty()) {
        if (s.noiseDone) return out;
        const std::size_t size = osc.audio.size();
        const std::size_t at0 = std::size_t(s.noisePos) % size, next = (at0 + 1) % size;
        const double f = s.noisePos - std::floor(s.noisePos);
        x = osc.audio[at0] + f * (osc.audio[next] - osc.audio[at0]);
        s.noisePos += clampd(osc.sampleRate, 8000.0, 192000.0) / in.sampleRate;
        if (s.noisePos >= double(size)) {
            if (osc.oneShot) { s.noiseDone = true; s.noisePos = 0.0; } else s.noisePos -= double(size);
        }
    } else if (!osc.asset.empty() || (in.importedPatch && !osc.noiseTypeExplicit)) {
        return out; // unresolved noise sample: silent, not substituted
    } else {
        switch (osc.noiseType) {
            case NoiseType::white: x = s.rng.bipolar(); break;
            case NoiseType::pink: {
                ++s.counter;
                const unsigned tz = unsigned(__builtin_ctz(s.counter | 0x100u));
                s.rows[std::min<std::size_t>(tz, 8)] = s.rng.bipolar();
                const double w = s.rng.bipolar();
                double sum = w; for (auto row : s.rows) sum += row;
                x = sum / 5.0; break;
            }
            case NoiseType::brown: {
                s.brown += 0.02 * s.rng.bipolar(); s.brown /= 1.02; s.brown = flush(s.brown);
                x = s.brown * 7.0; break;
            }
            case NoiseType::geiger: {
                const double rate = 60.0 * std::exp2(s.baseSemis / 12.0 * 0.0);
                if (s.rng.unipolar() < rate / in.sampleRate * 8.0) s.click = (s.rng.unipolar() < 0.5 ? -1.0 : 1.0) * (0.4 + 0.6 * s.rng.unipolar());
                x = s.click; s.click *= 0.985; break;
            }
        }
    }
    const double color = clampd(at(v, OscParam::color), 0.0, 1.0);
    if (color < 0.499) { s.colorLp.set(20.0 * std::pow(1000.0, color * 2.0), in.sampleRate); x = s.colorLp.low(x); }
    else if (color > 0.501) { s.colorHp.set(20.0 * std::pow(1000.0, (color - 0.5) * 2.0), in.sampleRate); x = s.colorHp.high(x); }
    out.l = out.r = out.raw = x;
    return out;
}

// ---- sample / multisample
struct PlayResult { double l = 0.0, r = 0.0; };
inline PlayResult readSample(const SampleData& sd, double pos, bool wrap) noexcept {
    const long n = long(sd.frames());
    PlayResult p;
    p.l = hermiteAt(sd.left.data(), n, pos, wrap);
    p.r = sd.stereo() ? hermiteAt(sd.right.data(), n, pos, wrap) : p.l;
    return p;
}

// Grain playback reads with linear interpolation (many overlapping reads per sample).
inline PlayResult readGrain(const SampleData& sd, double pos) noexcept {
    const std::size_t n = sd.frames();
    double p = pos;
    const double len = double(n);
    if (p >= len) p -= len * std::floor(p / len); else if (p < 0.0) p += len * std::ceil(-p / len);
    std::size_t i = std::size_t(p); if (i >= n) i = 0;
    const std::size_t j = i + 1 < n ? i + 1 : 0;
    const double f = p - double(i);
    PlayResult r;
    r.l = sd.left[i] + f * (sd.left[j] - sd.left[i]);
    r.r = sd.stereo() ? sd.right[i] + f * (sd.right[j] - sd.right[i]) : r.l;
    return r;
}

// Advances a play head; returns false when a one-shot head has finished.
bool advanceHead(double& pos, signed char& dir, double rate, double startF, double endF, bool loop,
                 double ls, double le, LoopMode mode, bool released) noexcept {
    pos += dir * rate;
    if (loop && !(mode == LoopMode::tailed && released)) {
        const double span = le - ls;
        if (span > 8.0) {
            if (mode == LoopMode::pingPong) {
                if (pos >= le) { pos = 2.0 * le - pos; dir = -1; }
                else if (pos <= ls) { pos = 2.0 * ls - pos; dir = 1; }
            } else {
                if (pos >= le) pos -= span; else if (pos < ls) pos += span;
            }
            return true;
        }
    }
    if (dir > 0 && pos >= endF) return false;
    if (dir < 0 && pos <= startF) return false;
    return true;
}

OscOut renderSampleOsc(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    OscOut out;
    const bool multi = osc.mode == OscMode::multisample;
    const int layers = multi ? s.regionCount : 1;
    if (multi && layers == 0) return out;
    double accL = 0.0, accR = 0.0;
    const double velGain = 1.0 - clampd(osc.velTrack, 0.0, 100.0) / 100.0 * (1.0 - in.velocity);
    for (int layer = 0; layer < layers; ++layer) {
        const SampleRegion* region = multi ? &osc.regions[std::size_t(s.regionIdx[std::size_t(layer)])] : nullptr;
        const SampleData* sd = multi ? region->sample.get() : osc.sample.get();
        if (!sd || sd->frames() < 4) continue;
        const double n = double(sd->frames());
        const int uCount = layer == 0 ? s.unisonCount : 1;
        double startF = clampd(at(v, OscParam::start) / 100.0, 0.0, 1.0) * n;
        double endF = clampd(at(v, OscParam::end) / 100.0, 0.0, 1.0) * n;
        if (multi) { startF = 0.0; endF = n; }
        const bool sliced = s.sliceStart >= 0.0;
        if (sliced) { startF = s.sliceStart; endF = s.sliceEnd; }
        if (s.sliceSilent) return out;
        if (endF <= startF + 2.0) endF = n;
        bool loop = sliced ? false : (multi ? region->loop : osc.looping);
        double ls = multi ? region->loopStart : lerp(startF, endF, 0.0) + clampd(at(v, OscParam::loopStart) / 100.0, 0.0, 1.0) * n - 0.0;
        double le = multi ? region->loopEnd : clampd(at(v, OscParam::loopEnd) / 100.0, 0.0, 1.0) * n;
        ls = clampd(ls, startF, endF); le = clampd(le, startF, endF);
        if (le <= ls + 8.0) { ls = startF; le = endF; }
        const double cf = multi ? 0.0 : clampd(osc.loopCrossfade, 0.0, 100.0) / 100.0 * (le - ls) * 0.5;
        const int root = multi ? region->rootKey : sd->rootNote;
        const double tuning = multi ? region->tuneCents / 100.0 : 0.0;
        const double regionGain = multi ? dbToGain(region->volumeDb) : 1.0;
        for (int u = 0; u < uCount; ++u) {
            double* posp = layer == 0 ? &s.pos[std::size_t(u)] : &s.regPos[std::size_t(layer)];
            signed char* dirp = layer == 0 ? &s.dir[std::size_t(u)] : &s.regDir[std::size_t(layer)];
            bool done = layer == 0 ? s.finished[std::size_t(u)] : s.regDone[std::size_t(layer)];
            if (done) continue;
            const auto& un = s.uni[std::size_t(u)];
            double semis = s.baseSemis + (layer == 0 ? un.semis : 0.0) + tuning;
            const double rel = (osc.pitchTrack && !sliced) ? semis - double(root - 69) : (sliced ? semis - in.semis : semis + 9.0);
            double rate = sd->sampleRate / in.sampleRate * std::exp2(rel / 12.0);
            if (osc.pitchMode == OscPitchMode::ratio) rate = sd->sampleRate / in.sampleRate * std::max(at(v, OscParam::pitchRatio), 0.05);
            PlayResult a = readSample(*sd, *posp, false);
            if (loop && cf > 4.0 && *dirp > 0 && *posp > le - cf) {
                const double t = clampd((*posp - (le - cf)) / cf, 0.0, 1.0);
                const PlayResult b = readSample(*sd, *posp - (le - ls), false);
                const double g1 = std::cos(t * 1.5707963267948966), g2 = std::sin(t * 1.5707963267948966);
                a.l = a.l * g1 + b.l * g2; a.r = a.r * g1 + b.r * g2;
            }
            const double g = regionGain * (layer == 0 ? un.gain : 1.0);
            const double gl = layer == 0 ? un.gL : 1.0, gr = layer == 0 ? un.gR : 1.0;
            accL += a.l * g * gl; accR += a.r * g * gr;
            const bool alive = advanceHead(*posp, *dirp, rate, startF, endF, loop, ls, le, osc.loopMode, in.released);
            if (!alive) { if (layer == 0) s.finished[std::size_t(u)] = true; else s.regDone[std::size_t(layer)] = true; }
        }
    }
    double gain = velGain;
    if (multi && (osc.sampleEnv.override_ || osc.sampleEnv.useSfzRelease)) {
        EnvParams ep;
        ep.attack = 0.0; ep.hold = 0.0; ep.decay = 0.0; ep.sustain = 1.0; ep.release = osc.sampleEnv.release;
        if (osc.sampleEnv.override_) {
            ep.attack = at(v, OscParam::envAttack); ep.hold = osc.sampleEnv.hold; ep.decay = at(v, OscParam::envDecay);
            ep.sustain = at(v, OscParam::envSustain); ep.release = at(v, OscParam::envRelease);
        }
        gain *= s.sampleEnv.advance(ep, 1.0 / in.sampleRate);
    }
    accL *= gain; accR *= gain;
    if (multi) {
        // Timbre shift: a spectral tilt (brighter/darker) around 1.5 kHz.
        const double t = clampd(at(v, OscParam::timbreShift) / 100.0, -1.0, 1.0);
        if (std::abs(t) > 1.0e-4) {
            s.colorLp.set(1500.0, in.sampleRate); s.colorHp.set(1500.0, in.sampleRate);
            const double lowL = s.colorLp.low(accL), lowR = s.colorHp.low(accR);
            accL = lowL + (1.0 + 1.5 * t) * (accL - lowL); accR = lowR + (1.0 + 1.5 * t) * (accR - lowR);
        }
    }
    out.l = accL; out.r = accR; out.raw = 0.5 * (out.l + out.r);
    return out;
}

// ---- granular
OscOut renderGranular(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    OscOut out;
    const SampleData* sd = osc.sample.get();
    if (!sd || sd->frames() < 16) return out;
    const double n = double(sd->frames());
    const double dur = n / sd->sampleRate;
    const double dens = std::max(at(v, OscParam::density), 0.5);
    const double interval = in.sampleRate / std::min(dens, 800.0);
    const double baseLen = std::max(at(v, OscParam::grainLength), 0.004);
    s.timeline += at(v, OscParam::scanRate) / 100.0 / std::max(dur, 0.05) / in.sampleRate;
    if (s.timeline > 1.0) s.timeline -= std::floor(s.timeline); else if (s.timeline < 0.0) s.timeline += 1.0 - std::floor(s.timeline);
    const double normal = 1.0 / std::max(1.0, dens * baseLen * s.unisonCount * 0.5);
    {   // window lookup table, rebuilt only when its controls change
        const double wpar = at(v, OscParam::windowParam), wskew = at(v, OscParam::windowSkew);
        if (s.windowShape != int(osc.windowShape) || s.windowKey[1] != wpar || s.windowKey[2] != wskew) {
            for (int i = 0; i <= 1024; ++i) s.window[std::size_t(i)] = float(windowValue(osc.windowShape, i / 1024.0, wpar, wskew));
            s.windowShape = int(osc.windowShape); s.windowKey[1] = wpar; s.windowKey[2] = wskew; s.windowKey[0] = 0.0;
        }
    }
    for (int u = 0; u < s.unisonCount; ++u) {
        double& timer = s.grainTimer[std::size_t(u)];
        timer -= 1.0;
        if (timer > 0.0) continue;
        double step = interval;
        if (osc.unisonTrigPattern == 1) step *= std::pow(1.18, u);
        else if (osc.unisonTrigPattern == 2) step *= 0.5 + s.rng.unipolar();
        timer += step;
        OscGrain* slot = nullptr;
        for (auto& g : s.grains) if (!g.on) { slot = &g; break; }
        if (!slot) continue;
        const auto& un = s.uni[std::size_t(u)];
        const double ro = clampd(at(v, OscParam::randomOffset), 0.0, 100.0) / 100.0;
        double rs = clampd(at(v, OscParam::start) / 100.0, 0.0, 1.0), re = clampd(at(v, OscParam::end) / 100.0, 0.0, 1.0);
        if (re <= rs + 0.005) { rs = 0.0; re = 1.0; }
        double start = (rs + wrap01(s.timeline + (s.rng.bipolar()) * ro * 0.5) * (re - rs)) * n;
        const double len = baseLen * (1.0 + clampd(at(v, OscParam::randomGrainLength), 0.0, 100.0) / 100.0 * (s.rng.unipolar() * 2.0 - 1.0) * 0.8);
        const double semis = s.baseSemis + un.semis + at(v, OscParam::randomPitch) * (s.rng.bipolar());
        const double rel = osc.pitchTrack ? semis - double(sd->rootNote - 69) : semis + 9.0;
        double rate = sd->sampleRate / in.sampleRate * std::exp2(rel / 12.0);
        if (s.rng.unipolar() < clampd(at(v, OscParam::randomDir), 0.0, 100.0) / 100.0 * 0.5) rate = -rate;
        const double rg = clampd(at(v, OscParam::randomGain), 0.0, 100.0) / 100.0;
        const double gain = normal * (1.0 - rg * s.rng.unipolar());
        const double rp = clampd(at(v, OscParam::randomPan), 0.0, 100.0) / 100.0 * s.rng.bipolar();
        const double pan = clampd(un.s * clampd(osc.unisonStereo, -100.0, 100.0) / 100.0 + rp, -1.0, 1.0);
        slot->on = true; slot->pos = start; slot->inc = rate; slot->w = 0.0; slot->winInc = 1.0 / std::max(len * in.sampleRate, 8.0);
        slot->gL = gain * std::sqrt(1.0 - pan); slot->gR = gain * std::sqrt(1.0 + pan);
    }
    double l = 0.0, r = 0.0;
    for (auto& g : s.grains) {
        if (!g.on) continue;
        const double wp = clampd(g.w, 0.0, 1.0) * 1024.0;
        const int wi = std::min(1023, int(wp));
        const double w = s.window[std::size_t(wi)] + (wp - wi) * (s.window[std::size_t(wi + 1)] - s.window[std::size_t(wi)]);
        const PlayResult p = readGrain(*sd, g.pos);
        l += p.l * w * g.gL; r += p.r * w * g.gR;
        g.pos += g.inc; g.w += g.winInc;
        if (g.w >= 1.0) g.on = false;
    }
    out.l = l; out.r = r; out.raw = 0.5 * (l + r);
    return out;
}

// ---- spectral
OscOut renderSpectral(OscVoiceState& s, const OscInputs& in) noexcept {
    const auto& osc = *in.osc; const auto& v = *in.v;
    OscOut out;
    const SpectralAnalysis* an = osc.spectral.get();
    if (!an || an->frames == 0) return out;
    s.elapsed += 1.0 / in.sampleRate;
    const double dur = double(an->frames) * SpectralAnalysis::hop / an->sampleRate;
    s.timeline += at(v, OscParam::scanRate) / 100.0 / std::max(dur, 0.05) / in.sampleRate;
    double rs = clampd(at(v, OscParam::start) / 100.0, 0.0, 1.0), re = clampd(at(v, OscParam::end) / 100.0, 0.0, 1.0);
    if (re <= rs + 0.005) { rs = 0.0; re = 1.0; }
    const double tl = rs + wrap01(at(v, OscParam::position) / 100.0 + s.timeline) * (re - rs);   // position runs across the Start..End region
    double l = 0.0, r = 0.0;
    double modulator = 0.0;
    for (const auto& w : osc.warpDefinitions) if (w.sourceIndex >= 0 && w.sourceIndex < 7 && in.mods) { modulator = (*in.mods)[std::size_t(w.sourceIndex)]; break; }
    for (int u = 0; u < s.unisonCount; ++u) {
        auto& sv = s.spectral[std::size_t(u)];
        if (!sv || !sv->allocated()) continue;
        const auto& un = s.uni[std::size_t(u)];
        SpectralParams sp;
        sp.frame = tl * double(an->frames - 1);
        const double rel = osc.pitchTrack ? s.baseSemis + un.semis - double(osc.sample ? osc.sample->rootNote - 69 : -9) : s.baseSemis + un.semis + 9.0;
        sp.ratio = std::exp2(rel / 12.0);
        sp.sourceRateRatio = an->sampleRate / in.sampleRate;
        sp.freqLo = at(v, OscParam::freqLo); sp.freqHi = at(v, OscParam::freqHi);
        sp.smoothBand = osc.loHiSmooth; sp.phaseLock = osc.phaseLock;
        sp.filterShift = at(v, OscParam::specShift); sp.filterWet = at(v, OscParam::specWet);
        sp.warp = {osc.warpDefinitions[0].mode, osc.warpDefinitions[1].mode};
        sp.amount = {clampd(at(v, OscParam::warp1), 0.0, 1.0), clampd(at(v, OscParam::warp2), 0.0, 1.0)};
        sp.var = {at(v, OscParam::warpVar1), at(v, OscParam::warpVar2)};
        const double y = sv->process(*an, sp, in.sampleRate, modulator);
        l += y * un.gain * un.gL; r += y * un.gain * un.gR;
    }
    out.l = l; out.r = r; out.raw = 0.5 * (l + r);
    return out;
}
}

OscOut oscRender(OscVoiceState& s, const OscInputs& in) noexcept {
    switch (in.osc->mode) {
        case OscMode::wavetable: case OscMode::sub: return renderTable(s, in);
        case OscMode::noise: return renderNoise(s, in);
        case OscMode::sample: case OscMode::multisample: return renderSampleOsc(s, in);
        case OscMode::granular: return renderGranular(s, in);
        case OscMode::spectral: return renderSpectral(s, in);
        default: return {};
    }
}
}
