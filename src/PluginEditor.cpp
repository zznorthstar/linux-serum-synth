#include "PluginEditor.h"

namespace {
const juce::Colour panelFill {0xff182f46};
const juce::Colour panelBorder {0xff295d78};
const juce::Colour cyan {0xff5ee6ef};
const juce::Colour ink {0xff9df7f4};
}

ZygEditor::ZygEditor(ZygProcessor& p) : AudioProcessorEditor(&p), processor(p) {
    setSize(700, 430);
    title.setText("ZYG-ZXG", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(32.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, ink);
    subtitle.setText("Linux Serum Synth", juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);
    status.setColour(juce::Label::textColourId, juce::Colours::white);
    status.setJustificationType(juce::Justification::topLeft);
    content.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);

    sectionLabel(presetHeading, "PRESET");
    presetName.setColour(juce::Label::textColourId, juce::Colours::white);
    presetName.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));

    sectionLabel(oscAHeading, "OSC A");
    oscAInfo.setColour(juce::Label::textColourId, juce::Colours::white);
    oscAPositionLabel.setText("Table position", juce::dontSendNotification);
    oscAPositionLabel.setColour(juce::Label::textColourId, juce::Colours::lightsteelblue);

    oscATablePosition.setRange(0.0, 256.0, 0.1);
    oscATablePosition.setSliderStyle(juce::Slider::LinearHorizontal);
    oscATablePosition.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
    oscATablePosition.setColour(juce::Slider::thumbColourId, cyan);
    oscATablePosition.setColour(juce::Slider::trackColourId, panelBorder);
    oscATablePosition.onValueChange = [this] { processor.setOscATablePosition(oscATablePosition.getValue()); };

    for (juce::Component* c : std::array<juce::Component*, 16>{
             &title, &subtitle, &status, &content,
             &selectAssets, &loadPreset, &diagnostics,
             &presetHeading, &presetName, &newPatch, &savePreset,
             &oscAHeading, &oscAInfo, &oscAPositionLabel, &loadWavetable, &oscATablePosition})
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
    newPatch.onClick = [this] { processor.newBlankPatch(); };
    savePreset.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Save ZYG native preset", juce::File{}, "*.zygpreset");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe = juce::Component::SafePointer<ZygEditor>(this)](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (safe && file != juce::File{}) {
                if (!file.hasFileExtension(".zygpreset")) file = file.withFileExtension(".zygpreset");
                safe->processor.saveNativePreset(file);
            }
        });
    };
    diagnostics.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(processor.getDiagnosticsReport()); };
    loadWavetable.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Load wavetable (mono RIFF WAVE)", juce::File{}, "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<ZygEditor>(this)](const juce::FileChooser& fc) {
            if (safe && fc.getResult().existsAsFile()) safe->processor.setOscAWavetableFile(fc.getResult());
        });
    };

    startTimerHz(4);
    timerCallback();
}
void ZygEditor::sectionLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    label.setColour(juce::Label::textColourId, cyan);
}
void ZygEditor::timerCallback() {
    status.setText(processor.getStatus(), juce::dontSendNotification);
    content.setText("Content: " + processor.getAssetRoot(), juce::dontSendNotification);
    presetName.setText(processor.getPresetName(), juce::dontSendNotification);

    const auto osc = processor.getOscAInfo();
    oscAInfo.setText((osc.enabled ? juce::String("enabled") : juce::String("disabled")) + " | "
                      + osc.mode + " | " + (osc.asset.isEmpty() ? juce::String("(no wavetable)") : osc.asset),
                      juce::dontSendNotification);
    if (!oscATablePosition.isMouseButtonDown())
        oscATablePosition.setValue(osc.tablePosition, juce::dontSendNotification);
}
void ZygEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff12202d));
    g.setColour(panelBorder);
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(12.0f), 16.0f);
    g.setColour(juce::Colour(0xff0e1c29));
    g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 13.0f);
    g.setColour(cyan);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 13.0f, 1.5f);

    auto panel = [&](juce::Rectangle<int> bounds) {
        g.setColour(panelFill.withAlpha(0.55f));
        g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
        g.setColour(panelBorder);
        g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
    };
    panel({28, 100, 644, 78});
    panel({28, 190, 644, 90});
}
void ZygEditor::resized() {
    title.setBounds(32, 26, 300, 43);
    subtitle.setBounds(33, 68, 610, 20);

    presetHeading.setBounds(40, 106, 200, 18);
    selectAssets.setBounds(40, 126, 190, 28);
    loadPreset.setBounds(238, 126, 150, 28);
    newPatch.setBounds(396, 126, 70, 28);
    savePreset.setBounds(474, 126, 130, 28);
    presetName.setBounds(40, 158, 350, 18);
    diagnostics.setBounds(474, 158, 150, 18);

    oscAHeading.setBounds(40, 196, 200, 18);
    oscAInfo.setBounds(40, 216, 400, 20);
    loadWavetable.setBounds(40, 240, 190, 28);
    oscAPositionLabel.setBounds(248, 244, 100, 20);
    oscATablePosition.setBounds(346, 240, 310, 28);

    content.setBounds(32, 292, 610, 20);
    status.setBounds(32, 316, 610, 96);
}
