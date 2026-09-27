#pragma once
#include "Patch.h"
#include <memory>

namespace zyg {
// Polyphonic ZYG synthesis engine. All rendering entry points are allocation-free
// and lock-free; prepare() is the only call that allocates.
class SynthEngine {
public:
    SynthEngine();
    ~SynthEngine();
    SynthEngine(const SynthEngine&) = delete;
    SynthEngine& operator=(const SynthEngine&) = delete;

    void prepare(double sampleRate);
    void setPatch(const Patch* patch) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note, float releaseVelocity = 0.0f) noexcept;
    void allNotesOff() noexcept;
    // Adds `count` samples starting at `firstSample` into left/right.
    void render(float* left, float* right, int firstSample, int count) noexcept;
    int activeVoiceCount() const noexcept;

    // Host and controller state.
    void setTransport(double bpm, double beatPosition, bool playing) noexcept;
    // Host sidechain input for the next render() calls (indices are the same as render's). nullptr disables it.
    void setSidechain(const float* left, const float* right, int frames) noexcept;
    void setModWheel(float value) noexcept;                          // 0..1
    void setPitchBend(int channel, float value) noexcept;            // -1..1
    void setChannelPressure(int channel, float value) noexcept;      // 0..1
    void setPolyPressure(int note, float value) noexcept;            // 0..1
    void setControlChange(int channel, int controller, float value) noexcept; // value 0..1
    void setSustainPedal(bool down) noexcept;
    // Output protection (soft ceiling at 0 dBFS). On by default; developer tools may disable it to measure raw levels.
    void setSafetyLimiter(bool enabled) noexcept;

    // Modulated oscillator values of the newest sounding voice, published once per render chunk
    // with relaxed atomics so editor displays can follow modulation. Returns false while silent.
    struct OscDisplayState { float tablePos = 0.0f, warp1 = 0.0f, warp2 = 0.0f; };
    bool oscDisplayState(int osc, OscDisplayState& out) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
