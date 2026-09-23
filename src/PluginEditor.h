#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>

class ZygPixelLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    ZygPixelLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
        bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down,
        int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
        float sliderPos, float minSliderPos, float maxSliderPos,
        juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getTextButtonFont(juce::TextButton&, int height) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
private:
    juce::Image knob32, knob48, knob64;
};

class ZygEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ZygEditor(ZygProcessor& processor);
    ~ZygEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void configureSlider(juce::Slider&, double min, double max, double step);
    void editRoute(const std::function<void(zyg::ModulationRoute&)>&);
    void syncRoute(const zyg::Patch&);
    void showPage(bool fx);
    ZygProcessor& processor;
    bool syncing = false;
    bool fxPage = false;
    bool snapshotWritten = false;
    unsigned lastMidiCount = 0;
    int midiLightTicks = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    ZygPixelLookAndFeel pixelLook;
    juce::Image logo;

    juce::Label title, presetName, status, oscName, unsupported, midiIndicator, voiceCount, meterText;
    juce::TextButton init {"INIT"}, loadSerum {"Load Serum"}, loadNative {"Open ZYG"},
        saveNative {"Save ZYG"}, selectAssets {"Content"}, diagnostics {"Diagnostics"},
        loadWavetable {"Browse wavetable"}, addRoute {"+ route"};
    juce::TextButton synthTab {"SYNTH"}, fxTab {"FX"};
    juce::TextEditor fxReadout;
    juce::ToggleButton audition {"C3 HOLD"};
    juce::ToggleButton subEnabled {"SUB"}, noiseEnabled {"NOISE"};
    juce::Slider subLevel, noiseLevel;
    juce::TextButton browseNoise {"Browse noise"}, nativeLfo {"Use sine"};
    juce::ToggleButton oscEnabled {"OSC A"}, filterEnabled {"FILTER 1"};
    juce::ComboBox oscSelect, oscRoute, routeList, matrixSource, matrixDestination;
    juce::Slider tablePosition, master, cutoff, resonance, filterDrive, filterMix, lfoRate, matrixAmount;
    std::array<juce::Slider, 9> oscSliders;
    std::array<juce::Slider, 5> envSliders;
    std::array<juce::Slider, 4> macroSliders;
    std::array<juce::Label, 9> oscLabels;
    std::array<juce::Label, 5> envLabels;
    std::array<juce::Label, 4> macroLabels;
    juce::Label tableLabel, masterLabel, cutoffLabel, resonanceLabel, driveLabel, mixLabel,
        lfoLabel, matrixLabel, amountLabel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygEditor)
};
