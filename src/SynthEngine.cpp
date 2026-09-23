#include "SynthEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace zyg {
namespace {
constexpr double tau = 6.2831853071795864769;
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
double envelopeCurve(double progress, double curve) noexcept {
    const double x = clamp(progress, 0.0, 1.0);
    const double bend = clamp((curve - 50.0) / 50.0, -1.0, 1.0);
    if (std::abs(bend) < 1.0e-6) return x;
    const double exponent = std::exp2(std::abs(bend) * 4.0);
    return bend > 0.0 ? 1.0 - std::pow(1.0 - x, exponent) : std::pow(x, exponent);
}
float tableAtLevel(const Oscillator& osc, unsigned level, double phase, double position) noexcept {
    const auto size = osc.frameSize;
    const auto frames = osc.audio.size() / size;
    if (!frames || size < 2) return 0.0f;
    const float* samples = osc.audio.data();
    if (level > 0 && level <= osc.wavetableMipLevels && osc.wavetableMipmaps
        && osc.wavetableMipmaps->size() >= std::size_t(level) * osc.audio.size())
        samples = osc.wavetableMipmaps->data() + std::size_t(level - 1) * osc.audio.size();
    const double scaled = clamp(position / 256.0, 0.0, 1.0) * double(frames - 1);
    const std::size_t frame = std::size_t(scaled);
    const double mix = scaled - double(frame);
    const double p = phase * size;
    const auto i = std::size_t(p) % size;
    const double fraction = p - std::floor(p);
    const auto a = frame * size;
    const auto b = std::min(frame + 1, frames - 1) * size;
    auto cubic = [&](std::size_t base) noexcept {
        const double y0 = samples[base + (i + size - 1) % size];
        const double y1 = samples[base + i];
        const double y2 = samples[base + (i + 1) % size];
        const double y3 = samples[base + (i + 2) % size];
        return y1 + 0.5 * fraction * (y2 - y0 + fraction *
            (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + fraction *
            (3.0 * (y1 - y2) + y3 - y0)));
    };
    const double x = cubic(a);
    const double y = cubic(b);
    return float(x + mix * (y - x));
}
float tableAt(const Oscillator& osc, double phase, double position, double frequency,
              double sampleRate) noexcept {
    if (osc.wavetableMipLevels == 0 || frequency <= 0.0)
        return tableAtLevel(osc, 0, phase, position);
    const double mipPosition = clamp(std::log2(double(osc.frameSize) * frequency / sampleRate),
                                     0.0, double(osc.wavetableMipLevels));
    const auto lower = unsigned(std::floor(mipPosition));
    const auto upper = std::min(lower + 1, osc.wavetableMipLevels);
    const double mix = mipPosition - lower;
    const double a = tableAtLevel(osc, lower, phase, position);
    if (upper == lower) return float(a);
    return float(a + mix * (tableAtLevel(osc, upper, phase, position) - a));
}
}
void SynthEngine::prepare(double sampleRate) noexcept {
    sampleRate_ = std::max(8000.0, sampleRate);
    allNotesOff();
}
void SynthEngine::setPatch(const Patch* patch) noexcept {
    patch_ = patch;
}
int SynthEngine::activeVoiceCount() const noexcept {
    int count = 0;
    for (const auto& voice : voices) count += voice.active;
    return count;
}
void SynthEngine::allNotesOff() noexcept { for (auto& v : voices) v = Voice{}; }
void SynthEngine::noteOn(int channel, int note, float velocity) noexcept {
    if (!patch_ || note < 0 || note > 127) return;
    if (patch_->mono) allNotesOff();
    Voice* target = nullptr;
    const int limit = std::clamp(patch_->polyphony, 1, maxVoices);
    for (int i = 0; i < limit; ++i) if (!voices[i].active) { target = &voices[i]; break; }
    if (!target) {
        target = &*std::min_element(voices.begin(), voices.begin() + limit, [](const Voice& a, const Voice& b) {
            return a.age < b.age;
        });
    }
    *target = Voice{};
    target->active = true;
    target->channel = channel;
    target->note = note;
    target->velocity = std::clamp(velocity, 0.0f, 1.0f);
    target->age = ++clock_;
    target->noiseState = 0x9e3779b9u ^ std::uint32_t(clock_ * 2654435761u) ^ std::uint32_t(note * 334214467u);
    for (std::size_t i = 0; i < target->chaosState.size(); ++i) {
        target->chaosState[i] = {0.1 + 0.003 * double(i), 0.0, 0.0};
        target->lfoRandomState[i] = target->noiseState ^ std::uint32_t(0x85ebca6bu * (i + 1));
    }
    target->smoothedCutoff = clamp(patch_->filters[0].cutoff, 0.0, 1.0);
    for (std::size_t i = 0; i < patch_->oscillators.size(); ++i) {
        const auto& osc = patch_->oscillators[i];
        const int count = std::clamp(osc.unison, 1, 16);
        const double randomRange = clamp(osc.randomPhase / 100.0, 0.0, 1.0);
        for (int u = 0; u < count; ++u) {
            target->noiseState ^= target->noiseState << 13;
            target->noiseState ^= target->noiseState >> 17;
            target->noiseState ^= target->noiseState << 5;
            const double randomOffset = double(target->noiseState) / 4294967296.0 * randomRange;
            target->phase[i][u] = std::fmod(osc.initialPhase / 360.0 + double(u) / count + randomOffset, 1.0);
        }
    }
}
void SynthEngine::noteOff(int channel, int note) noexcept {
    for (auto& voice : voices) {
        if (voice.active && voice.channel == channel && voice.note == note && voice.stage != Voice::release) {
            voice.stage = Voice::release;
            voice.stageSeconds = 0.0;
            voice.releaseStartAmp = voice.amp;
            voice.releaseDuration = patch_ ? std::max(0.0, patch_->envelopes[0].release) : 0.075;
        }
    }
}
double SynthEngine::envelope(Voice& voice) noexcept {
    const auto& env = patch_->envelopes[0];
    const auto step = 1.0 / sampleRate_;
    voice.stageSeconds += step;
    switch (voice.stage) {
        case Voice::attack:
            voice.amp = env.attack <= step ? 1.0
                : envelopeCurve(voice.stageSeconds / env.attack, env.curve[0]);
            if (voice.stageSeconds >= env.attack) { voice.stage = Voice::hold; voice.stageSeconds = 0; }
            break;
        case Voice::hold:
            voice.amp = 1.0;
            if (voice.stageSeconds >= env.hold) { voice.stage = Voice::decay; voice.stageSeconds = 0; }
            break;
        case Voice::decay:
            voice.amp = env.decay <= step ? env.sustain :
                1.0 + (clamp(env.sustain, 0, 1) - 1.0)
                    * envelopeCurve(voice.stageSeconds / env.decay, env.curve[1]);
            if (voice.stageSeconds >= env.decay) { voice.stage = Voice::sustain; voice.stageSeconds = 0; }
            break;
        case Voice::sustain: voice.amp = clamp(env.sustain, 0, 1); break;
        case Voice::release:
            voice.amp = voice.releaseDuration <= step ? 0.0 : voice.releaseStartAmp
                * (1.0 - envelopeCurve(voice.stageSeconds / voice.releaseDuration, env.curve[2]));
            if (voice.stageSeconds >= voice.releaseDuration || voice.amp <= 1e-7) {
                voice.active = false; voice.amp = 0;
            }
            break;
    }
    return voice.amp;
}
double SynthEngine::lfoValue(Voice& voice, int index) noexcept {
    if (!patch_ || index < 0 || index >= 10) return 0.0;
    const auto slot = std::size_t(index);
    if (index == 0 && patch_->lfoOneSine) {
        const double value = std::sin(tau * voice.lfoPhase[slot]);
        voice.lfoPhase[slot] += clamp(patch_->lfoOneRateHz, 0.01, 1000.0) / sampleRate_;
        voice.lfoPhase[slot] -= std::floor(voice.lfoPhase[slot]);
        return value;
    }
    const auto& definition = patch_->lfoDefinitions[slot];
    if (definition.tempoSync) return 0.0;
    const double rate = clamp(definition.rateHz, 0.01, 100.0);
    if (definition.shape == LfoShape::sine) {
        const double value = std::sin(tau * voice.lfoPhase[slot]);
        voice.lfoPhase[slot] += rate / sampleRate_;
        voice.lfoPhase[slot] -= std::floor(voice.lfoPhase[slot]);
        return value;
    }
    if (definition.shape == LfoShape::randomHold) {
        const double oldPhase = voice.lfoPhase[slot];
        voice.lfoPhase[slot] += rate / sampleRate_;
        voice.lfoPhase[slot] -= std::floor(voice.lfoPhase[slot]);
        if (voice.lfoPhase[slot] < oldPhase) {
            auto& state = voice.lfoRandomState[slot];
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            voice.randomHold[slot] = double(state) / 2147483648.0 - 1.0;
        }
        return voice.randomHold[slot];
    }
    auto& state = voice.chaosState[slot];
    const double step = rate / sampleRate_;
    const double x = state[0], y = state[1], z = state[2];
    if (definition.shape == LfoShape::lorenz) {
        state[0] += step * 10.0 * (y - x);
        state[1] += step * (x * (28.0 - z) - y);
        state[2] += step * (x * y - (8.0 / 3.0) * z);
        if (!std::isfinite(state[0] + state[1] + state[2])) state = {0.1, 0.0, 0.0};
        return std::tanh(state[0] / 15.0);
    }
    if (definition.shape == LfoShape::rossler) {
        state[0] += step * (-y - z);
        state[1] += step * (x + 0.2 * y);
        state[2] += step * (0.2 + z * (x - 5.7));
        if (!std::isfinite(state[0] + state[1] + state[2])) state = {0.1, 0.0, 0.0};
        return std::tanh(state[0] / 8.0);
    }
    return 0.0;
}
float SynthEngine::oscillatorSample(Voice& voice, int index, const Oscillator& osc, double position) noexcept {
    if (!osc.enabled) return 0.0f;
    if (osc.mode == OscMode::noise) {
        if (osc.audio.empty()) {
            if (!osc.asset.empty() || !patch_->originalPreset.empty()) return 0.0f;
            auto& state = voice.noiseState;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return (float(double(state) / 2147483648.0 - 1.0) * float(osc.volume));
        }
        const auto size = osc.audio.size();
        const auto at = std::size_t(voice.noisePosition) % size;
        const auto next = (at + 1) % size;
        const double fraction = voice.noisePosition - std::floor(voice.noisePosition);
        const float out = float(osc.audio[at] + fraction * (osc.audio[next] - osc.audio[at]));
        voice.noisePosition += clamp(osc.sampleRate, 8000.0, 192000.0) / sampleRate_;
        if (voice.noisePosition >= double(size)) voice.noisePosition -= double(size);
        return out * float(osc.volume);
    }
    if (osc.mode != OscMode::sub && osc.audio.empty()) return 0.0f;
    const double semitone = 12.0 * osc.octave + osc.semitone + osc.fine / 100.0;
    const double base = 440.0 * std::exp2((voice.note - 69.0 + semitone) / 12.0);
    float result = 0.0f;
    const int count = std::clamp(osc.unison, 1, 16);
    for (int u = 0; u < count; ++u) {
        const double spread = count > 1 ? (2.0 * u / (count - 1) - 1.0) : 0.0;
        const double detuned = base * std::exp2(spread * osc.detune / 120.0);
        double& phase = voice.phase[index][u];
        result += osc.mode == OscMode::sub ? float(std::sin(tau * phase))
                                           : tableAt(osc, phase, position, detuned, sampleRate_);
        phase += detuned / sampleRate_;
        phase -= std::floor(phase);
    }
    return result * float(osc.volume / std::sqrt(double(count)));
}
void SynthEngine::render(float* left, float* right, int firstSample, int count) noexcept {
    if (!patch_) return;
    const auto& filter = patch_->filters[0];
    const double cutoffSmooth = 1.0 - std::exp(-1.0 / (0.005 * sampleRate_));
    const double q = 0.707 + 9.293 * clamp(filter.resonance / 100.0, 0.0, 1.0);
    const double k = 1.0 / q;
    const double driveGain = 1.0 + 0.05 * clamp(filter.drive, 0.0, 100.0);
    const double driveNorm = filter.drive > 0.0 ? 1.0 / std::tanh(driveGain) : 1.0;
    const auto end = firstSample + count;
    for (int n = firstSample; n < end; ++n) {
        double l = 0, r = 0;
        for (auto& voice : voices) {
            if (!voice.active) continue;
            const double amp = envelope(voice) * voice.velocity;
            std::array<double, 10> lfoValues {};
            std::array<bool, 10> lfoEvaluated {};
            double tableOffset = 0.0, cutoffOffset = 0.0;
            for (const auto& mod : patch_->modulation) {
                if (mod.bypass || mod.auxiliary != 0) continue;
                double value = 0.0;
                if (mod.sourceKind == ModSource::lfo && mod.sourceIndex >= 0 && mod.sourceIndex < 10) {
                    const auto source = std::size_t(mod.sourceIndex);
                    const auto& definition = patch_->lfoDefinitions[source];
                    const bool supported = (mod.sourceIndex == 0 && patch_->lfoOneSine)
                        || (definition.shape != LfoShape::unknown && !definition.tempoSync);
                    if (!supported) continue;
                    if (!lfoEvaluated[source]) {
                        lfoValues[source] = lfoValue(voice, mod.sourceIndex);
                        lfoEvaluated[source] = true;
                    }
                    value = mod.bipolar ? lfoValues[source] : (lfoValues[source] + 1.0) * 0.5;
                }
                else if (mod.sourceKind == ModSource::macro && mod.sourceIndex >= 0 && mod.sourceIndex < 8)
                    value = patch_->macroValues[std::size_t(mod.sourceIndex)];
                else continue;
                if (mod.targetKind == ModTarget::wavetablePosition && mod.targetIndex == 0)
                    tableOffset += mod.amount * 2.56 * value;
                if (mod.targetKind == ModTarget::filterCutoff && mod.targetIndex == 0)
                    cutoffOffset += mod.amount * 0.01 * value;
            }
            double wetL = 0, wetR = 0, directL = 0, directR = 0;
            for (int i = 0; i < 5; ++i) {
                const auto& osc = patch_->oscillators[i];
                const auto sample = oscillatorSample(voice, i, osc,
                    osc.tablePosition + (i == 0 ? tableOffset : 0.0));
                if (patch_->routes[i].target == RouteTarget::none) continue;
                const double pan = clamp(osc.pan, -1.0, 1.0);
                const double sl = sample * std::sqrt(1.0 - pan);
                const double sr = sample * std::sqrt(1.0 + pan);
                if (patch_->routes[i].target == RouteTarget::filter) { wetL += sl; wetR += sr; }
                else { directL += sl; directR += sr; }
            }
            if (filter.enabled) {
                const double targetCutoff = clamp(filter.cutoff + cutoffOffset, 0.0, 1.0);
                voice.smoothedCutoff += cutoffSmooth * (targetCutoff - voice.smoothedCutoff);
                const double cutoff = 20.0 * std::pow(1000.0, voice.smoothedCutoff);
                const double g = std::tan(3.14159265358979323846 * std::min(cutoff, sampleRate_ * 0.45) / sampleRate_);
                const double a1 = 1.0 / (1.0 + g * (g + k));
                const double a2 = g * a1, a3 = g * a2;
                auto lowpass = [&](double in, double& band, double& low) noexcept {
                    if (filter.drive > 0.0) in = std::tanh(in * driveGain) * driveNorm;
                    const double v3 = in - low;
                    const double v1 = a1 * band + a2 * v3;
                    const double v2 = low + a2 * band + a3 * v3;
                    band = 2.0 * v1 - band;
                    low = 2.0 * v2 - low;
                    if (std::abs(band) < 1e-20) band = 0.0;
                    if (std::abs(low) < 1e-20) low = 0.0;
                    return v2;
                };
                const double filteredL = lowpass(wetL, voice.filterBandL, voice.filterLowL);
                const double filteredR = lowpass(wetR, voice.filterBandR, voice.filterLowR);
                const double mix = clamp(filter.wet / 100.0, 0.0, 1.0);
                wetL += mix * (filteredL - wetL);
                wetR += mix * (filteredR - wetR);
            }
            const double gain = amp * patch_->masterVolume * 0.25;
            l += (wetL + directL) * gain;
            r += (wetR + directR) * gain;
        }
        left[n] += float(l); right[n] += float(r);
    }
}
}
