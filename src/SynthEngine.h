#pragma once
#include "Patch.h"
#include "Oversampling.h"
#include <array>

namespace zyg {
class SynthEngine {
public:
    void prepare(double sampleRate) noexcept;
    void setPatch(const Patch* patch) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void allNotesOff() noexcept;
    void render(float* left, float* right, int firstSample, int count) noexcept;
    int activeVoiceCount() const noexcept;
private:
    struct Voice {
        bool active = false;
        int channel = 0, note = 0;
        std::uint64_t age = 0;
        float velocity = 0;
        enum Stage { attack, hold, decay, sustain, release } stage = attack;
        double amp = 0, releaseStartAmp = 0, releaseDuration = 0, stageSeconds = 0;
        std::array<double, 10> lfoPhase {};
        std::array<std::array<double, 3>, 10> chaosState {};
        std::array<double, 10> randomHold {};
        std::array<std::uint32_t, 10> lfoRandomState {};
        double noisePosition = 0;
        std::uint32_t noiseState = 0x9e3779b9u;
        std::array<std::array<double, 16>, 5> phase {};
        std::array<OversampleState, 5> warpOversampling;
        std::array<std::array<double, 15>, 5> alignmentDelay {};
        std::array<std::size_t, 5> alignmentWrite {};
        double filterBandL = 0, filterLowL = 0, filterBandR = 0, filterLowR = 0;
        double smoothedCutoff = 1.0;
    };
    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices {};
    const Patch* patch_ = nullptr;
    double sampleRate_ = 44100;
    std::uint64_t clock_ = 0;
    double envelope(Voice& voice) noexcept;
    double lfoValue(Voice& voice, int index) noexcept;
    float oscillatorSample(Voice& voice, int index, const Oscillator& osc, double position,
                           const std::array<double, 2>& warpAmounts,
                           const std::array<std::array<double, 2>, 5>& modulatorSamples) noexcept;
    double rawModulatorSample(const Voice& voice, int index, double subSampleOffset) const noexcept;
};
}
