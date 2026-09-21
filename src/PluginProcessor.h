#pragma once
#include <JuceHeader.h>
#include "SerumImporter.h"
#include "SynthEngine.h"
#include <atomic>
#include <memory>

class ZygProcessor final : public juce::AudioProcessor {
public:
    ZygProcessor();
    ~ZygProcessor() override = default;
    const juce::String getName() const override { return "ZYG-ZXG"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Current patch"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadPreset(const juce::File& file);
    bool setAssetRoot(const juce::File& root);
    juce::String getAssetRoot() const;
    juce::String getStatus() const;
    juce::String getDiagnosticsReport() const;
private:
    bool publish(zyg::Patch&& patch);
    void setStatus(juce::String text);
    zyg::SynthEngine engine;
    std::array<std::unique_ptr<zyg::Patch>, 3> slots;
    std::atomic<int> activeIndex {0}, requestedIndex {0};
    mutable juce::CriticalSection controlLock;
    juce::String assetRoot, status;
    juce::MemoryBlock savedPreset;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygProcessor)
};
