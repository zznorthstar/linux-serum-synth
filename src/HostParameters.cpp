#include "HostParameters.h"
#include <algorithm>
#include <cmath>

const std::array<const char*, zyg::hostControlCount> HostParameters::ids {
    "filter1CutoffOffset", "filter2CutoffOffset", "oscAPositionOffset", "oscBPositionOffset", "oscCPositionOffset",
    "oscAFrameOffset", "oscBFrameOffset", "oscCFrameOffset"
};

juce::AudioProcessorValueTreeState::ParameterLayout HostParameters::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const char* names[] = {"Filter 1 cutoff offset", "Filter 2 cutoff offset", "OSC A WT position offset",
        "OSC B WT position offset", "OSC C WT position offset", "OSC A frame offset", "OSC B frame offset", "OSC C frame offset"};
    for (std::size_t i = 0; i < zyg::hostControlCount; ++i) {
        const bool frame = i >= std::size_t(zyg::HostControl::frameA);
        const float limit = frame ? 255.0f : 1.0f;
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[i], 1}, names[i],
            juce::NormalisableRange<float>{-limit, limit, frame ? 1.0f : 0.0f}, 0.0f));
    }
    return result;
}

HostParameters::HostParameters(juce::AudioProcessor& owner) : state(owner, nullptr, "ZYGHostParameters", layout()) {
    for (std::size_t i = 0; i < raw_.size(); ++i) raw_[i] = state.getRawParameterValue(ids[i]);
}

void HostParameters::prepare(double sampleRate) noexcept {
    for (std::size_t i = 0; i < smooth_.size(); ++i) {
        smooth_[i].reset(sampleRate, 0.01);
        const float value = raw_[i]->load(std::memory_order_relaxed);
        smooth_[i].setCurrentAndTargetValue(std::isfinite(value) ? value : 0.0f);
    }
}

void HostParameters::beginBlock() noexcept {
    const bool restored = restored_.exchange(false, std::memory_order_acquire);
    for (std::size_t i = 0; i < smooth_.size(); ++i) {
        const float value = raw_[i]->load(std::memory_order_relaxed);
        const float target = std::isfinite(value) ? value : 0.0f;
        if (restored) smooth_[i].setCurrentAndTargetValue(target);
        else smooth_[i].setTargetValue(target);
    }
}

void HostParameters::render(zyg::SynthEngine& engine, float* left, float* right, int first, int count) noexcept {
    bool transparent = true;
    for (const auto& value : smooth_)
        transparent = transparent && !value.isSmoothing() && value.getCurrentValue() == 0.0f;
    if (transparent) {
        engine.clearHostControls();
        engine.render(left, right, first, count);
        return;
    }
    for (int done = 0; done < count; done += capacity) {
        const int n = std::min(capacity, count - done);
        zyg::HostControlStreams streams;
        for (std::size_t k = 0; k < smooth_.size(); ++k) {
            for (int sample = 0; sample < n; ++sample) blocks_[k][std::size_t(sample)] = smooth_[k].getNextValue();
            streams[k] = blocks_[k].data();
        }
        engine.setHostControlBlock(streams, first + done, n);
        engine.render(left, right, first + done, n);
    }
    engine.clearHostControls(); // never leave a borrowed stream bound after render
}

void HostParameters::resetState() {
    for (const auto* id : ids) {
        auto* parameter = state.getParameter(id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
    }
    restored_.store(true, std::memory_order_release);
}

void HostParameters::restoreState(const juce::ValueTree& snapshot) {
    state.replaceState(snapshot);
    restored_.store(true, std::memory_order_release);
}
