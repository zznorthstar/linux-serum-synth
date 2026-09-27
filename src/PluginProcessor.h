#pragma once
#include <JuceHeader.h>
#include "SerumImporter.h"
#include "SynthEngine.h"
#include <array>
#include <atomic>
#include <memory>
#include <functional>

class ZygProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    ZygProcessor();
    ~ZygProcessor() override { stopTimer(); }
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
    // Shared, immutable view of the newest published patch. Cheap: no copy of wavetable data.
    std::shared_ptr<const zyg::Patch> getPatch() const;
    bool editPatch(const std::function<void(zyg::Patch&)>& edit);
    float getOutputPeak() const noexcept { return outputPeak.load(std::memory_order_relaxed); }
    float getOutputPeakLeft() const noexcept { return peakLeft.load(std::memory_order_relaxed); }
    float getOutputPeakRight() const noexcept { return peakRight.load(std::memory_order_relaxed); }
    // Copies the newest `n` (<= 8192) mono output samples, oldest first. Message thread only.
    void copyScope(float* dest, int n) const noexcept;
    // Live modulated oscillator values for the displays (see SynthEngine::oscDisplayState).
    bool oscDisplayState(int osc, zyg::SynthEngine::OscDisplayState& out) const noexcept { return engine.oscDisplayState(osc, out); }
    // On-screen keyboard: queued from the message thread, drained by the audio thread.
    void uiNote(int note, bool on, float velocity = 0.8f) noexcept;
    void uiAllNotesOff() noexcept;
    void uiPitchBend(float bipolar) noexcept;                 // -1..1
    void uiController(int controller, float normalised) noexcept;
    bool anyNoteHeld() const noexcept { return (heldLow.load(std::memory_order_relaxed) | heldHigh.load(std::memory_order_relaxed)) != 0; }
    bool isNoteHeld(int note) const noexcept {
        if (note < 0 || note > 127) return false;
        const auto bits = (note < 64 ? heldLow : heldHigh).load(std::memory_order_relaxed);
        return (bits >> (note & 63)) & 1u;
    }
    int getActiveVoiceCount() const noexcept { return activeVoices.load(std::memory_order_relaxed); }
    unsigned getMidiNoteCount() const noexcept { return midiNotes.load(std::memory_order_relaxed); }
    void setAuditionHeld(bool held) noexcept { auditionHeld.store(held, std::memory_order_relaxed); }

    struct OscAInfo { bool enabled = false; juce::String mode, asset; double tablePosition = 0.0; };
    OscAInfo getOscAInfo() const;
    void setOscATablePosition(double position);
    bool setOscAWavetableFile(const juce::File& file);
    bool setOscWavetableFile(int index, const juce::File& file);
    // Sample / granular / spectral source for OSC A-C (decoded off the audio thread).
    bool setOscSampleFile(int index, const juce::File& file);
    bool setNoiseSampleFile(const juce::File& file);
    // Impulse response for an FX Convolve module (index into Patch::fx).
    bool setFxImpulseFile(int fxIndex, const juce::File& file);

    // Resample: records the plugin's own output for `beats` quarter notes starting at the next note-on, saves it as a
    // WAV under ~/.local/share/ZYG-ZXG/Resampled and loads it into OSC A-C as a pitch-tracked sample.
    bool armResample(int oscIndex, double beats);
    void cancelResample();
    enum class ResampleState { idle = 0, armed = 1, recording = 2, finishing = 3 };
    ResampleState getResampleState() const noexcept { return ResampleState(rsState.load(std::memory_order_acquire)); }
    float getResampleProgress() const noexcept;
    juce::String takeResampleMessage();      // one-shot event text for the UI (empty when none)
    static juce::File resampleFolder();
private:
    void timerCallback() override;
    void finishResample();
    void captureResample(const float* l, const float* r, int n) noexcept;
    void rsNoteOn(int note, int at) noexcept { if (rsState.load(std::memory_order_acquire) == 1) { rsNote = note; rsStartAt = std::max(0, at); rsState.store(2, std::memory_order_release); } }
    juce::AudioBuffer<float> scBuf;
    std::array<std::vector<float>, 2> rsBuf;
    std::atomic<int> rsState {0};
    int rsTarget = 0, rsNote = 60, rsFrames = 0, rsStartAt = -1;
    std::atomic<int> rsWritten {0};
    std::atomic<double> lastBpm {120.0};
    juce::String rsMessage;
    bool publish(zyg::Patch&& patch);
    zyg::Patch currentPatchCopy() const;
    void setStatus(juce::String text);
    zyg::SynthEngine engine;
    std::array<std::shared_ptr<zyg::Patch>, 3> slots;
    std::atomic<int> activeIndex {0}, requestedIndex {0};
    mutable juce::CriticalSection controlLock;
    juce::String assetRoot, status;
    juce::MemoryBlock savedPreset;
    std::atomic<float> outputPeak {0.0f}, peakLeft {0.0f}, peakRight {0.0f};
    // Output scope for the editor's spectrum analyzer: a fixed ring of relaxed atomics written by the
    // audio thread (no locks/allocation); the editor copies the newest samples. Torn reads only blur a frame.
    static constexpr std::uint32_t kScopeSize = 8192;
    std::array<std::atomic<float>, kScopeSize> scope_ {};
    std::atomic<std::uint32_t> scopeWrite_ {0};
    struct UiNote { std::uint8_t kind = 0, note = 0, velocity = 0; bool on = false; float value = 0.0f; };  // kind: 0 note, 1 panic, 2 bend, 3 cc
    std::atomic<std::uint64_t> heldLow {0}, heldHigh {0};
    void markHeld(int note, bool on) noexcept;
    static constexpr int uiNoteCapacity = 128;
    juce::AbstractFifo uiNoteFifo {uiNoteCapacity};
    std::array<UiNote, uiNoteCapacity> uiNoteQueue {};
    std::atomic<int> activeVoices {0};
    std::atomic<unsigned> midiNotes {0};
    std::atomic<bool> auditionHeld {false};
    bool auditionVoiceActive = false; // audio thread only
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygProcessor)
};
