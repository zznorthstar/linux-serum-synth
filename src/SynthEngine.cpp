#include "SynthEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace zyg {
namespace {
constexpr double tau = 6.2831853071795864769;
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
float tableAt(const Oscillator& osc, double phase, double position) noexcept {
    const auto size = osc.frameSize;
    const auto frames = osc.audio.size() / size;
    if (!frames || size < 2) return 0.0f;
    const double scaled = clamp(position / 256.0, 0.0, 1.0) * double(frames - 1);
    const std::size_t frame = std::size_t(scaled);
    const double mix = scaled - double(frame);
    const double p = phase * size;
    const auto i = std::size_t(p) % size;
    const auto next = (i + 1) % size;
    const double fraction = p - std::floor(p);
    const auto a = frame * size;
    const auto b = std::min(frame + 1, frames - 1) * size;
    const double x = osc.audio[a + i] + fraction * (osc.audio[a + next] - osc.audio[a + i]);
    const double y = osc.audio[b + i] + fraction * (osc.audio[b + next] - osc.audio[b + i]);
    return float(x + mix * (y - x));
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
            const double seconds = patch_ ? std::max(0.001, patch_->envelopes[0].release) : 0.075;
            voice.releaseStep = voice.amp / (seconds * sampleRate_);
        }
    }
}
double SynthEngine::envelope(Voice& voice) noexcept {
    const auto& env = patch_->envelopes[0];
    const auto step = 1.0 / sampleRate_;
    voice.stageSeconds += step;
    switch (voice.stage) {
        case Voice::attack:
            voice.amp = env.attack <= step ? 1.0 : clamp(voice.stageSeconds / env.attack, 0, 1);
            if (voice.stageSeconds >= env.attack) { voice.stage = Voice::hold; voice.stageSeconds = 0; }
            break;
        case Voice::hold:
            voice.amp = 1.0;
            if (voice.stageSeconds >= env.hold) { voice.stage = Voice::decay; voice.stageSeconds = 0; }
            break;
        case Voice::decay:
            voice.amp = env.decay <= step ? env.sustain :
                1.0 + (clamp(env.sustain, 0, 1) - 1.0) * clamp(voice.stageSeconds / env.decay, 0, 1);
            if (voice.stageSeconds >= env.decay) { voice.stage = Voice::sustain; voice.stageSeconds = 0; }
            break;
        case Voice::sustain: voice.amp = clamp(env.sustain, 0, 1); break;
        case Voice::release:
            voice.amp = std::max(0.0, voice.amp - voice.releaseStep);
            if (voice.amp <= 1e-7) { voice.active = false; voice.amp = 0; }
            break;
    }
    return voice.amp;
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
        result += osc.mode == OscMode::sub ? float(std::sin(tau * phase)) : tableAt(osc, phase, position);
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
            const double lfo = patch_->lfoOneSine ? std::sin(tau * voice.lfoPhase) : 0.0;
            voice.lfoPhase += clamp(patch_->lfoOneRateHz, 0.01, 40.0) / sampleRate_;
            voice.lfoPhase -= std::floor(voice.lfoPhase);
            double tableOffset = 0.0, cutoffOffset = 0.0;
            for (const auto& mod : patch_->modulation) {
                if (mod.bypass || mod.auxiliary != 0) continue;
                double value = 0.0;
                if (mod.sourceKind == ModSource::lfo && mod.sourceIndex == 0 && patch_->lfoOneSine)
                    value = mod.bipolar ? lfo : (lfo + 1.0) * 0.5;
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
