#pragma once
#include "Patch.h"
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
private:
    struct Voice {
        bool active = false;
        int channel = 0, note = 0;
        std::uint64_t age = 0;
        float velocity = 0;
        enum Stage { attack, hold, decay, sustain, release } stage = attack;
        double amp = 0, releaseStep = 0, stageSeconds = 0;
        std::array<std::array<double, 16>, 5> phase {};
        double filterL = 0, filterR = 0;
    };
    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices {};
    const Patch* patch_ = nullptr;
    double sampleRate_ = 44100;
    std::uint64_t clock_ = 0;
    double envelope(Voice& voice) noexcept;
    float oscillatorSample(Voice& voice, int index, const Oscillator& osc) noexcept;
};
}
