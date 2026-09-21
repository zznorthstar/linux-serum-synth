#include "PluginEditor.h"

ZygEditor::ZygEditor(ZygProcessor& p) : AudioProcessorEditor(&p), processor(p) {
    setSize(680, 260);
    title.setText("ZYG-ZXG", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colour(0xff9df7f4));
    subtitle.setText("Linux Serum Synth  |  VST3 proof of pipeline", juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);
    status.setColour(juce::Label::textColourId, juce::Colours::white);
    status.setJustificationType(juce::Justification::topLeft);
    content.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);
    for (juce::Component* c : std::array<juce::Component*, 7>{&title, &subtitle, &status, &content, &selectAssets, &loadPreset, &diagnostics})
        addAndMakeVisible(c);
    selectAssets.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Choose your legally obtained Serum content folder");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [safe = juce::Component::SafePointer<ZygEditor>(this)](const juce::FileChooser& fc) {
            if (safe && fc.getResult().isDirectory()) safe->processor.setAssetRoot(fc.getResult());
        });
    };
    loadPreset.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load Serum 2 preset", juce::File{}, "*.SerumPreset");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<ZygEditor>(this)](const juce::FileChooser& fc) {
            if (safe && fc.getResult().existsAsFile()) safe->processor.loadPreset(fc.getResult());
        });
    };
    diagnostics.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(processor.getDiagnosticsReport()); };
    startTimerHz(4);
}
void ZygEditor::timerCallback() {
    status.setText(processor.getStatus(), juce::dontSendNotification);
    content.setText("Content: " + processor.getAssetRoot(), juce::dontSendNotification);
}
void ZygEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff12202d));
    g.setColour(juce::Colour(0xff295d78));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(12.0f), 16.0f);
    g.setColour(juce::Colour(0xff182f46));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 13.0f);
    g.setColour(juce::Colour(0xff5ee6ef));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 13.0f, 1.5f);
}
void ZygEditor::resized() {
    title.setBounds(32, 26, 300, 43);
    subtitle.setBounds(33, 68, 610, 22);
    selectAssets.setBounds(32, 106, 205, 32);
    loadPreset.setBounds(248, 106, 180, 32);
    diagnostics.setBounds(438, 106, 170, 32);
    content.setBounds(32, 149, 610, 22);
    status.setBounds(32, 178, 610, 62);
}
