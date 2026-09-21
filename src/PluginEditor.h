#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class ZygEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ZygEditor(ZygProcessor& processor);
    ~ZygEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void sectionLabel(juce::Label& label, const juce::String& text);
    ZygProcessor& processor;

    // PRESET
    juce::TextButton selectAssets {"Set Serum content folder"};
    juce::TextButton loadPreset {"Load .SerumPreset"};
    juce::TextButton newPatch {"New"};
    juce::TextButton savePreset {"Save .zygpreset"};
    juce::TextButton diagnostics {"Copy diagnostics"};
    juce::Label presetHeading, presetName;

    // OSC A
    juce::Label oscAHeading, oscAInfo, oscAPositionLabel;
    juce::TextButton loadWavetable {"Load wavetable (.wav)"};
    juce::Slider oscATablePosition;

    juce::Label title, subtitle, status, content;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygEditor)
};
