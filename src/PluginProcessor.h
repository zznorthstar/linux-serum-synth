#pragma once
#include <JuceHeader.h>
#include "SerumImporter.h"
#include "SynthEngine.h"
#include <atomic>
#include <memory>
#include <functional>

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

    // Native patch and editor control operations. File preparation and patch
    // cloning run off the audio thread before the immutable handoff.
    juce::String getPresetName() const;
    void newBlankPatch();
    bool saveNativePreset(const juce::File& file);
    bool loadNativePreset(const juce::File& file);
    zyg::Patch getPatchSnapshot() const;
    bool editPatch(const std::function<void(zyg::Patch&)>& edit);
    float getOutputPeak() const noexcept { return outputPeak.load(std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept { return activeVoices.load(std::memory_order_relaxed); }
    unsigned getMidiNoteCount() const noexcept { return midiNotes.load(std::memory_order_relaxed); }
    void setAuditionHeld(bool held) noexcept { auditionHeld.store(held, std::memory_order_relaxed); }

    struct OscAInfo { bool enabled = false; juce::String mode, asset; double tablePosition = 0.0; };
    OscAInfo getOscAInfo() const;
    void setOscATablePosition(double position);
    bool setOscAWavetableFile(const juce::File& file);
    bool setOscWavetableFile(int index, const juce::File& file);
    bool setNoiseSampleFile(const juce::File& file);
private:
    bool publish(zyg::Patch&& patch);
    zyg::Patch currentPatchCopy() const;
    void setStatus(juce::String text);
    zyg::SynthEngine engine;
    std::array<std::unique_ptr<zyg::Patch>, 3> slots;
    std::atomic<int> activeIndex {0}, requestedIndex {0};
    mutable juce::CriticalSection controlLock;
    juce::String assetRoot, status;
    juce::MemoryBlock savedPreset;
    std::atomic<float> outputPeak {0.0f};
    std::atomic<int> activeVoices {0};
    std::atomic<unsigned> midiNotes {0};
    std::atomic<bool> auditionHeld {false};
    bool auditionVoiceActive = false; // audio thread only
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygProcessor)
};
