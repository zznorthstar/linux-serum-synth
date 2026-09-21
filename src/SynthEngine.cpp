#include "SynthEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace zyg {
namespace {
constexpr double tau = 6.2831853071795864769;
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
float tableAt(const Oscillator& osc, double phase) noexcept {
    const auto size = osc.frameSize;
    const auto frames = osc.audio.size() / size;
    if (!frames || size < 2) return 0.0f;
    const double scaled = clamp(osc.tablePosition / 256.0, 0.0, 1.0) * double(frames - 1);
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
    allNotesOff();
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
    for (std::size_t i = 0; i < patch_->oscillators.size(); ++i) {
        const auto& osc = patch_->oscillators[i];
        const int count = std::clamp(osc.unison, 1, 16);
        for (int u = 0; u < count; ++u)
            target->phase[i][u] = std::fmod(osc.initialPhase / 360.0 + double(u) / count, 1.0);
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
float SynthEngine::oscillatorSample(Voice& voice, int index, const Oscillator& osc) noexcept {
    if (!osc.enabled || osc.audio.empty()) return 0.0f;
    const double semitone = 12.0 * osc.octave + osc.semitone + osc.fine / 100.0;
    const double base = 440.0 * std::exp2((voice.note - 69.0 + semitone) / 12.0);
    float result = 0.0f;
    const int count = std::clamp(osc.unison, 1, 16);
    for (int u = 0; u < count; ++u) {
        const double spread = count > 1 ? (2.0 * u / (count - 1) - 1.0) : 0.0;
        const double detuned = base * std::exp2(spread * osc.detune / 120.0);
        double& phase = voice.phase[index][u];
        result += tableAt(osc, phase);
        phase += detuned / sampleRate_;
        phase -= std::floor(phase);
    }
    return result * float(osc.volume / std::sqrt(double(count)));
}
void SynthEngine::render(float* left, float* right, int firstSample, int count) noexcept {
    if (!patch_) return;
    const auto& filter = patch_->filters[0];
    const double cutoff = 20.0 * std::pow(1000.0, clamp(filter.cutoff, 0, 1));
    const double coeff = 1.0 - std::exp(-tau * std::min(cutoff, sampleRate_ * 0.45) / sampleRate_);
    const auto end = firstSample + count;
    for (int n = firstSample; n < end; ++n) {
        double l = 0, r = 0;
        for (auto& voice : voices) {
            if (!voice.active) continue;
            const double amp = envelope(voice) * voice.velocity;
            double wet = 0, direct = 0;
            for (int i = 0; i < 3; ++i) {
                const auto& osc = patch_->oscillators[i];
                const auto sample = oscillatorSample(voice, i, osc);
                if (patch_->routes[i].target == RouteTarget::none) continue;
                if (patch_->routes[i].target == RouteTarget::direct || patch_->routes[i].target == RouteTarget::main)
                    direct += sample;
                else wet += sample;
            }
            if (filter.enabled) {
                voice.filterL += coeff * (wet - voice.filterL);
                if (std::abs(voice.filterL) < 1e-20) voice.filterL = 0.0;
                wet = voice.filterL;
            }
            const double signal = (wet + direct) * amp * patch_->masterVolume * 0.25;
            l += signal; r += signal;
        }
        left[n] += float(l); right[n] += float(r);
    }
}
}
