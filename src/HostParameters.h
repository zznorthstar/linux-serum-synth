#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SynthEngine.h"

// JUCE host automation is an additive performance layer over the immutable
// imported/native patch. Stable IDs and zero defaults keep preset calibration
// independent of a host's automation lanes. No editor code belongs here.
class HostParameters {
public:
    explicit HostParameters(juce::AudioProcessor& owner);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static const std::array<const char*, zyg::hostControlCount> ids;
    juce::AudioProcessorValueTreeState state;

    void prepare(double sampleRate) noexcept;
    void beginBlock() noexcept;
    void render(zyg::SynthEngine&, float* left, float* right, int first, int count) noexcept;
    void resetState(); // control/state thread only; never called by processBlock
    void restoreState(const juce::ValueTree&); // restore targets without an order-dependent startup ramp
private:
    static constexpr int capacity = 256;
    std::array<std::atomic<float>*, zyg::hostControlCount> raw_ {};
    std::array<juce::LinearSmoothedValue<float>, zyg::hostControlCount> smooth_;
    std::array<std::array<float, capacity>, zyg::hostControlCount> blocks_ {};
    std::atomic<bool> restored_ {false};
};
