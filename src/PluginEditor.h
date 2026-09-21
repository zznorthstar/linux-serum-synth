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
    ZygProcessor& processor;
    juce::TextButton selectAssets {"Set Serum content folder"};
    juce::TextButton loadPreset {"Load .SerumPreset"};
    juce::TextButton diagnostics {"Copy diagnostics"};
    juce::Label title, subtitle, status, content;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygEditor)
};
