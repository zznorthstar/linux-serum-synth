#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

namespace {
const juce::Colour bg {0xff08060d}, chassis {0xff100b1b}, panel {0xff181126};
const juce::Colour raised {0xff231838}, sunken {0xff0c0914};
const juce::Colour shadow {0xff0a0710}, line {0xff31224d}, highlight {0xff4d3678};
const juce::Colour cyan {0xff39ff14}, acidHot {0xffd6ff85}, text {0xfff0fff2}, dim {0xffa6b4a8};
const juce::Colour muted {0xff59635a}, purple {0xff8a1fdf}, purpleHot {0xffd84cff};
void label(juce::Label& l, const juce::String& s, juce::Colour colour = dim) {
    l.setText(s, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, colour);
    l.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 10.0f, juce::Font::plain)));
}
void panelBox(juce::Graphics& g, juce::Rectangle<int> r) {
    g.setColour(panel); g.fillRect(r);
    g.setColour(highlight); g.drawHorizontalLine(r.getY(), float(r.getX()), float(r.getRight()));
    g.drawVerticalLine(r.getX(), float(r.getY()), float(r.getBottom()));
    g.setColour(shadow); g.drawHorizontalLine(r.getBottom(), float(r.getX()), float(r.getRight()));
    g.drawVerticalLine(r.getRight(), float(r.getY()), float(r.getBottom()));
}
void sectionTitle(juce::Graphics& g, const juce::String& s, int x, int y, int w) {
    g.setColour(raised); g.fillRect(x, y, w, 18);
    g.setColour(cyan); g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 10.0f, juce::Font::bold)));
    g.drawText(s, x + 6, y, w - 12, 18, juce::Justification::centredLeft, false);
}
}

ZygPixelLookAndFeel::ZygPixelLookAndFeel() {
    knob32 = juce::ImageFileFormat::loadFrom(BinaryData::knob_32x32_128frames_png,
        BinaryData::knob_32x32_128frames_pngSize);
    knob48 = juce::ImageFileFormat::loadFrom(BinaryData::knob_48x48_128frames_png,
        BinaryData::knob_48x48_128frames_pngSize);
    knob64 = juce::ImageFileFormat::loadFrom(BinaryData::knob_64x64_128frames_png,
        BinaryData::knob_64x64_128frames_pngSize);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::textColourOffId, dim);
    setColour(juce::TextButton::textColourOnId, acidHot);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, sunken);
    setColour(juce::ComboBox::outlineColourId, highlight);
    setColour(juce::ComboBox::arrowColourId, cyan);
    setColour(juce::Slider::textBoxTextColourId, acidHot);
    setColour(juce::Slider::textBoxBackgroundColourId, sunken);
    setColour(juce::Slider::textBoxOutlineColourId, line);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, purple);
    setColour(juce::PopupMenu::highlightedTextColourId, acidHot);
}

void ZygPixelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float, float, juce::Slider&) {
    const int available = juce::jmin(width, height);
    const juce::Image* strip = available >= 58 ? &knob64 : available >= 42 ? &knob48 : &knob32;
    if (!strip->isValid()) return;
    const int size = strip->getWidth();
    const int frame = juce::jlimit(0, 127, juce::roundToInt(sliderPos * 127.0f));
    const int dx = x + (width - size) / 2, dy = y + (height - size) / 2;
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImage(*strip, dx, dy, size, size, 0, frame * size, size, size, false);
}

void ZygPixelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
        const juce::Colour&, bool highlighted, bool down) {
    auto r = b.getLocalBounds();
    const auto fill = b.getToggleState() || down ? purple : highlighted ? raised.brighter(0.12f) : raised;
    g.setColour(fill); g.fillRect(r);
    g.setColour(b.getToggleState() ? purpleHot : highlight); g.drawHorizontalLine(0, 0.0f, float(r.getWidth()));
    g.drawVerticalLine(0, 0.0f, float(r.getHeight()));
    g.setColour(shadow); g.drawHorizontalLine(r.getHeight() - 1, 0.0f, float(r.getWidth()));
    g.drawVerticalLine(r.getWidth() - 1, 0.0f, float(r.getHeight()));
}

void ZygPixelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
        bool highlighted, bool down) {
    drawButtonBackground(g, b, {}, highlighted, down);
    g.setColour(b.getToggleState() ? cyan : muted);
    g.fillRect(6, b.getHeight() / 2 - 3, 6, 6);
    g.setColour(b.getToggleState() ? acidHot : dim);
    g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 9.0f, juce::Font::bold)));
    g.drawText(b.getButtonText(), 17, 0, b.getWidth() - 20, b.getHeight(), juce::Justification::centredLeft);
}

void ZygPixelLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
        int, int, int, int, juce::ComboBox&) {
    g.setColour(sunken); g.fillRect(0, 0, width, height);
    g.setColour(highlight); g.drawRect(0, 0, width, height, 1);
    g.setColour(cyan);
    juce::Path p; p.addTriangle(float(width - 13), float(height / 2 - 2),
        float(width - 5), float(height / 2 - 2), float(width - 9), float(height / 2 + 3));
    g.fillPath(p);
}

void ZygPixelLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float, float, juce::Slider::SliderStyle style, juce::Slider& slider) {
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar ||
        style == juce::Slider::LinearBarVertical) {
        const int cy = y + height / 2;
        g.setColour(sunken); g.fillRect(x, cy - 3, width, 6);
        g.setColour(line); g.drawRect(x, cy - 3, width, 6);
        const int origin = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
            ? x + juce::roundToInt(float(width) * float(slider.valueToProportionOfLength(0.0))) : x + 1;
        const int thumb = juce::roundToInt(sliderPos);
        g.setColour(cyan); g.fillRect(juce::jmin(origin, thumb), cy - 2,
            juce::jmax(1, std::abs(thumb - origin)), 4);
        g.setColour(acidHot); g.fillRect(juce::roundToInt(sliderPos) - 2, cy - 6, 4, 12);
        return;
    }
    const int cx = x + width / 2;
    g.setColour(sunken); g.fillRect(cx - 3, y, 6, height);
    g.setColour(line); g.drawRect(cx - 3, y, 6, height);
    g.setColour(acidHot); g.fillRect(cx - 6, juce::roundToInt(sliderPos) - 2, 12, 4);
}

void ZygPixelLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(panel); g.setColour(highlight); g.drawRect(0, 0, width, height);
}

juce::Font ZygPixelLookAndFeel::getTextButtonFont(juce::TextButton&, int height) {
    return juce::Font(juce::FontOptions("DejaVu Sans Mono", juce::jmin(9.0f, float(height) * 0.52f), juce::Font::bold));
}

juce::Font ZygPixelLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return juce::Font(juce::FontOptions("DejaVu Sans Mono", 9.0f, juce::Font::plain));
}

ZygEditor::ZygEditor(ZygProcessor& p) : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&pixelLook);
    logo = juce::ImageFileFormat::loadFrom(BinaryData::the_logo_png, BinaryData::the_logo_pngSize);
    setResizable(false, false);
    setSize(1000, 600);
    label(title, "ZYG-ZXG", cyan);
    title.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 15.0f, juce::Font::bold)));
    label(presetName, "ZYG init", text);
    presetName.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono", 12.0f, juce::Font::bold)));
    label(status, "", dim); label(oscName, "", text);
    label(unsupported, "", dim); label(midiIndicator, "MIDI", dim);
    label(voiceCount, "Voices 0", text); label(meterText, "Output -inf dB", text);
    label(tableLabel, "WT POSITION", cyan); label(masterLabel, "MASTER", cyan);
    label(cutoffLabel, "CUTOFF", cyan);label(resonanceLabel,"RESONANCE",cyan);
    label(driveLabel,"DRIVE",cyan); label(mixLabel, "MIX", cyan);
    label(lfoLabel, "LFO 1 | native sine", cyan);
    label(matrixLabel, "MATRIX | imported routes remain visible", cyan);
    label(amountLabel, "AMOUNT", cyan);
    oscSelect.addItem("OSC A",1);oscSelect.addItem("OSC B",2);oscSelect.addItem("OSC C",3);
    oscSelect.setSelectedId(1,juce::dontSendNotification);
    oscSelect.onChange=[this]{timerCallback();};
    const char* oscNames[] = {"OCTAVE", "SEMITONE", "FINE", "LEVEL", "PAN", "PHASE", "RANDOM", "UNISON", "DETUNE"};
    const double lo[] = {-4,-12,-100,0,-1,0,0,1,0};
    const double hi[] = {4,12,100,1,1,360,100,16,1};
    const double st[] = {1,1,0.1,0.001,0.01,1,0.1,1,0.001};
    for (int i=0;i<9;++i) {
        label(oscLabels[std::size_t(i)], oscNames[i], cyan);
        configureSlider(oscSliders[std::size_t(i)], lo[i], hi[i], st[i]);
        oscSliders[std::size_t(i)].onValueChange = [this,i] {
            if (syncing) return;
            const auto v = oscSliders[std::size_t(i)].getValue();
            const int index=oscSelect.getSelectedId()-1;
            processor.editPatch([i,v,index](zyg::Patch& patch) {
                auto& o=patch.oscillators[std::size_t(index)];
                switch(i) {
                    case 0:o.octave=int(v);break; case 1:o.semitone=int(v);break;
                    case 2:o.fine=v;break; case 3:o.volume=v;break;
                    case 4:o.pan=v;break; case 5:o.initialPhase=v;break;
                    case 6:o.randomPhase=v;break; case 7:o.unison=int(v);break; case 8:o.detune=v;break;
                }
            });
        };
    }
    const char* envNames[] = {"ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
    for (int i=0;i<5;++i) {
        label(envLabels[std::size_t(i)], envNames[i], cyan);
        configureSlider(envSliders[std::size_t(i)], 0, i==3?1.0:10.0, 0.001);
        envSliders[std::size_t(i)].onValueChange = [this,i] {
            if (syncing) return;
            const auto v=envSliders[std::size_t(i)].getValue();
            processor.editPatch([i,v](zyg::Patch& patch) {
                auto& e=patch.envelopes[0];
                switch(i) {case 0:e.attack=v;break;case 1:e.hold=v;break;
                    case 2:e.decay=v;break;case 3:e.sustain=v;break;case 4:e.release=v;break;}
            });
            repaint();
        };
    }
    for(int i=0;i<4;++i) {
        label(macroLabels[std::size_t(i)],"MACRO "+juce::String(i+1),cyan);
        configureSlider(macroSliders[std::size_t(i)],0,1,0.001);
        macroSliders[std::size_t(i)].onValueChange=[this,i]{
            if(syncing)return;
            processor.editPatch([i,v=macroSliders[std::size_t(i)].getValue()](zyg::Patch& p){p.macroValues[std::size_t(i)]=v;});
        };
    }
    configureSlider(tablePosition,0,256,0.1);
    tablePosition.onValueChange=[this] {if(!syncing) processor.editPatch([index=oscSelect.getSelectedId()-1,v=tablePosition.getValue()](zyg::Patch& p){p.oscillators[std::size_t(index)].tablePosition=v;});};
    configureSlider(master,0,1,0.001);
    master.onValueChange=[this] {if(!syncing) processor.editPatch([v=master.getValue()](zyg::Patch& p){p.masterVolume=v;});};
    configureSlider(cutoff,0,1,0.001);
    cutoff.onValueChange=[this] {if(!syncing) processor.editPatch([v=cutoff.getValue()](zyg::Patch& p){p.filters[0].cutoff=v;});};
    configureSlider(resonance,0,100,0.1);
    resonance.onValueChange=[this]{if(!syncing)processor.editPatch([v=resonance.getValue()](zyg::Patch& p){p.filters[0].resonance=v;});};
    configureSlider(filterDrive,0,100,0.1);
    filterDrive.onValueChange=[this]{if(!syncing)processor.editPatch([v=filterDrive.getValue()](zyg::Patch& p){p.filters[0].drive=v;});};
    configureSlider(filterMix,0,100,0.1);
    filterMix.onValueChange=[this] {if(!syncing) processor.editPatch([v=filterMix.getValue()](zyg::Patch& p){p.filters[0].wet=v;});};
    configureSlider(lfoRate,0.01,40,0.01);
    lfoRate.onValueChange=[this] {if(!syncing) processor.editPatch([v=lfoRate.getValue()](zyg::Patch& p){p.lfoOneRateHz=v;});};
    nativeLfo.onClick=[this]{processor.editPatch([](zyg::Patch& p){p.lfoOneSine=true;p.lfoOneRateHz=1.0;});};
    configureSlider(matrixAmount,-100,100,0.1);
    matrixAmount.setSliderStyle(juce::Slider::LinearHorizontal);
    matrixAmount.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,18);
    matrixAmount.onValueChange=[this] {if(!syncing) editRoute([v=matrixAmount.getValue()](zyg::ModulationRoute& m){m.amount=v;});};
    configureSlider(subLevel,0,1,0.001);configureSlider(noiseLevel,0,1,0.001);
    subEnabled.onClick=[this]{processor.editPatch([v=subEnabled.getToggleState()](zyg::Patch& p){
        p.oscillators[4].mode=zyg::OscMode::sub;p.oscillators[4].enabled=v;
        if(v && p.routes[4].target==zyg::RouteTarget::unknown)p.routes[4].target=zyg::RouteTarget::main;});};
    noiseEnabled.onClick=[this]{processor.editPatch([v=noiseEnabled.getToggleState()](zyg::Patch& p){
        p.oscillators[3].mode=zyg::OscMode::noise;p.oscillators[3].enabled=v;
        if(v && p.routes[3].target==zyg::RouteTarget::unknown)p.routes[3].target=zyg::RouteTarget::main;});};
    subLevel.onValueChange=[this]{if(!syncing)processor.editPatch([v=subLevel.getValue()](zyg::Patch& p){p.oscillators[4].volume=v;});};
    noiseLevel.onValueChange=[this]{if(!syncing)processor.editPatch([v=noiseLevel.getValue()](zyg::Patch& p){p.oscillators[3].volume=v;});};
    oscEnabled.onClick=[this] {processor.editPatch([index=oscSelect.getSelectedId()-1,v=oscEnabled.getToggleState()](zyg::Patch& p){p.oscillators[std::size_t(index)].enabled=v;});};
    filterEnabled.onClick=[this] {processor.editPatch([v=filterEnabled.getToggleState()](zyg::Patch& p){p.filters[0].enabled=v;});};
    oscRoute.addItem("FILTER 1",1); oscRoute.addItem("MAIN",2); oscRoute.addItem("DIRECT",3); oscRoute.addItem("OFF",4);
    oscRoute.onChange=[this] {if(!syncing) processor.editPatch([index=oscSelect.getSelectedId()-1,v=oscRoute.getSelectedId()](zyg::Patch& p){
        p.routes[std::size_t(index)].target=v==1?zyg::RouteTarget::filter:v==2?zyg::RouteTarget::main:v==3?zyg::RouteTarget::direct:zyg::RouteTarget::none;});};
    matrixSource.addItem("LFO 1",1);
    for(int i=0;i<8;++i)matrixSource.addItem("Macro "+juce::String(i+1),i+2);
    matrixSource.onChange=[this] {if(!syncing) editRoute([id=matrixSource.getSelectedId()](zyg::ModulationRoute& m){
        m.source=id==1?6:25+id-2;m.auxiliary=0;m.sourceName=id==1?"LFO 1":"Macro "+std::to_string(id-1);
        m.sourceKind=id==1?zyg::ModSource::lfo:zyg::ModSource::macro;
        m.sourceIndex=id==1?0:id-2;});};
    matrixDestination.addItem("OSC A WT position",1); matrixDestination.addItem("FILTER 1 cutoff",2);
    matrixDestination.onChange=[this] {if(!syncing) editRoute([v=matrixDestination.getSelectedId()](zyg::ModulationRoute& m){
        m.destinationModule=v==2?"VoiceFilter":"WTOsc";
        m.destinationInstance=0; m.destinationParameter=v==2?"kParamFreq":"kParamTablePos";
        m.destinationParameterId=-1;
        m.targetKind=v==2?zyg::ModTarget::filterCutoff:zyg::ModTarget::wavetablePosition;
        m.targetIndex=0;});};
    routeList.onChange=[this] {syncRoute(processor.getPatchSnapshot());};
    addRoute.onClick=[this] {
        processor.editPatch([](zyg::Patch& p){
            zyg::ModulationRoute m; m.slot=int(p.modulation.size());m.source=6;m.sourceName="LFO 1";
            m.sourceKind=zyg::ModSource::lfo;m.sourceIndex=0;
            m.destinationModule="WTOsc";m.destinationParameter="kParamTablePos";
            m.targetKind=zyg::ModTarget::wavetablePosition;m.targetIndex=0;m.amount=50;
            p.modulation.push_back(std::move(m));
        });
        routeList.clear(juce::dontSendNotification);
        timerCallback();
        routeList.setSelectedId(routeList.getNumItems(),juce::sendNotification);
    };
    auto filePick=[this](const juce::String& caption,const juce::String& glob,bool save,
                         std::function<void(const juce::File&)> done) {
        chooser=std::make_unique<juce::FileChooser>(caption,juce::File{},glob);
        chooser->launchAsync(save?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting)
                                 :(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),
            [safe=juce::Component::SafePointer<ZygEditor>(this),done](const juce::FileChooser& fc) {
                if(safe && fc.getResult()!=juce::File{}) done(fc.getResult());
            });
    };
    init.onClick=[this]{processor.newBlankPatch();};
    loadSerum.onClick=[this,filePick]{filePick("Load Serum 2 preset","*.SerumPreset",false,
        [this](const juce::File& f){processor.loadPreset(f);});};
    loadNative.onClick=[this,filePick]{filePick("Open ZYG patch","*.zygpreset",false,
        [this](const juce::File& f){processor.loadNativePreset(f);});};
    saveNative.onClick=[this,filePick]{filePick("Save ZYG patch","*.zygpreset",true,
        [this](const juce::File& f){processor.saveNativePreset(f.hasFileExtension(".zygpreset")?f:f.withFileExtension(".zygpreset"));});};
    loadWavetable.onClick=[this,filePick]{filePick("Load mono wavetable","*.wav",false,
        [this](const juce::File& f){processor.setOscWavetableFile(oscSelect.getSelectedId()-1,f);});};
    browseNoise.onClick=[this,filePick]{filePick("Load noise sample","*.wav;*.flac",false,
        [this](const juce::File& f){processor.setNoiseSampleFile(f);});};
    selectAssets.onClick=[this]{
        chooser=std::make_unique<juce::FileChooser>("Select your legally owned Serum content folder");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,
            [safe=juce::Component::SafePointer<ZygEditor>(this)](const juce::FileChooser& fc){
                if(safe && fc.getResult().isDirectory()) safe->processor.setAssetRoot(fc.getResult());});
    };
    diagnostics.onClick=[this]{juce::SystemClipboard::copyTextToClipboard(processor.getDiagnosticsReport());};
    audition.onClick=[this]{processor.setAuditionHeld(audition.getToggleState());};
    synthTab.onClick=[this]{showPage(false);};
    fxTab.onClick=[this]{showPage(true);};
    fxReadout.setMultiLine(true);fxReadout.setReadOnly(true);
    fxReadout.setColour(juce::TextEditor::backgroundColourId,panel);
    fxReadout.setColour(juce::TextEditor::textColourId,text);
    for(juce::Component* c : std::array<juce::Component*,38>{&title,&presetName,&status,&oscName,&unsupported,
        &midiIndicator,&voiceCount,&meterText,&init,&loadSerum,&loadNative,&saveNative,&selectAssets,&diagnostics,
        &loadWavetable,&addRoute,&oscEnabled,&filterEnabled,&oscRoute,&routeList,&matrixSource,&matrixDestination,
        &tablePosition,&master,&cutoff,&resonance,&filterDrive,&filterMix,&lfoRate,&matrixAmount,
        &tableLabel,&masterLabel,&cutoffLabel,&resonanceLabel,&driveLabel,&mixLabel,&lfoLabel,&matrixLabel}) addAndMakeVisible(c);
    addAndMakeVisible(amountLabel);
    addAndMakeVisible(audition);
    addAndMakeVisible(oscSelect);
    addAndMakeVisible(synthTab);addAndMakeVisible(fxTab);addAndMakeVisible(fxReadout);
    addAndMakeVisible(subEnabled);addAndMakeVisible(noiseEnabled);
    addAndMakeVisible(subLevel);addAndMakeVisible(noiseLevel);addAndMakeVisible(browseNoise);
    addAndMakeVisible(nativeLfo);
    for(auto& c:oscSliders)addAndMakeVisible(c);
    for(auto& c:envSliders)addAndMakeVisible(c);
    for(auto& c:macroSliders)addAndMakeVisible(c);
    for(auto& c:oscLabels)addAndMakeVisible(c);
    for(auto& c:envLabels)addAndMakeVisible(c);
    for(auto& c:macroLabels)addAndMakeVisible(c);
    startTimerHz(8);
    showPage(false);
    timerCallback();
}
ZygEditor::~ZygEditor() {
    processor.setAuditionHeld(false);
    setLookAndFeel(nullptr);
}
void ZygEditor::showPage(bool fx) {
    fxPage=fx;
    for(juce::Component* c : std::array<juce::Component*,23>{&oscName,&unsupported,&loadWavetable,
        &addRoute,&oscEnabled,&filterEnabled,&oscSelect,&oscRoute,&routeList,&matrixSource,
        &matrixDestination,&tablePosition,&cutoff,&resonance,&filterDrive,&filterMix,&lfoRate,
        &matrixAmount,&tableLabel,&cutoffLabel,&resonanceLabel,&driveLabel,&mixLabel}) c->setVisible(!fx);
    for(auto& c:oscSliders)c.setVisible(!fx);
    for(auto& c:envSliders)c.setVisible(!fx);
    for(auto& c:oscLabels)c.setVisible(!fx);
    for(auto& c:envLabels)c.setVisible(!fx);
    for(auto& c:macroSliders)c.setVisible(!fx);
    for(auto& c:macroLabels)c.setVisible(!fx);
    lfoLabel.setVisible(!fx);matrixLabel.setVisible(!fx);amountLabel.setVisible(!fx);
    nativeLfo.setVisible(!fx);
    subEnabled.setVisible(!fx);noiseEnabled.setVisible(!fx);
    subLevel.setVisible(!fx);noiseLevel.setVisible(!fx);browseNoise.setVisible(!fx);
    fxReadout.setVisible(fx);
    synthTab.setEnabled(fx);fxTab.setEnabled(!fx);
    repaint();
}
void ZygEditor::configureSlider(juce::Slider& s,double min,double max,double step) {
    s.setRange(min,max,step);s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,58,16);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
        juce::MathConstants<float>::pi * 2.75f, true);
    s.setColour(juce::Slider::textBoxTextColourId,text);
}
void ZygEditor::editRoute(const std::function<void(zyg::ModulationRoute&)>& edit) {
    const int idx=routeList.getSelectedId()-1;
    if(idx<0)return;
    processor.editPatch([idx,&edit](zyg::Patch& p){if(idx<int(p.modulation.size()))edit(p.modulation[std::size_t(idx)]);});
}
void ZygEditor::syncRoute(const zyg::Patch& p) {
    const int idx=routeList.getSelectedId()-1;
    if(idx<0 || idx>=int(p.modulation.size()))return;
    const auto& m=p.modulation[std::size_t(idx)];
    const bool sourceSupported = (p.lfoOneSine && m.sourceKind==zyg::ModSource::lfo && m.sourceIndex==0) ||
        (m.sourceKind==zyg::ModSource::macro && m.sourceIndex>=0 && m.sourceIndex<8);
    const bool supported = sourceSupported && m.auxiliary==0 && !m.bypass &&
        ((m.targetKind==zyg::ModTarget::wavetablePosition && m.targetIndex==0) ||
         (m.targetKind==zyg::ModTarget::filterCutoff && m.targetIndex==0));
    matrixLabel.setText(supported?"MATRIX | selected route active":"MATRIX | selected route preserved, DSP pending",juce::dontSendNotification);
    syncing=true;
    matrixAmount.setValue(m.amount,juce::dontSendNotification);
    matrixSource.setSelectedId(m.sourceKind==zyg::ModSource::lfo&&m.sourceIndex==0?1:
        m.sourceKind==zyg::ModSource::macro&&m.sourceIndex>=0&&m.sourceIndex<8?m.sourceIndex+2:0,juce::dontSendNotification);
    int dest=0;
    if(m.targetKind==zyg::ModTarget::wavetablePosition&&m.targetIndex==0)dest=1;
    if(m.targetKind==zyg::ModTarget::filterCutoff&&m.targetIndex==0)dest=2;
    matrixDestination.setSelectedId(dest,juce::dontSendNotification);
    syncing=false;
}
void ZygEditor::timerCallback() {
    const auto p=processor.getPatchSnapshot();
    syncing=true;
    presetName.setText(juce::String(p.name),juce::dontSendNotification);
    status.setText(processor.getStatus(),juce::dontSendNotification);
    const int oscIndex=std::clamp(oscSelect.getSelectedId()-1,0,2);
    const auto& o=p.oscillators[std::size_t(oscIndex)];
    oscName.setText(juce::String(o.asset.empty()?"Built-in sine":o.asset)+"  |  "+juce::String(zyg::oscModeToString(o.mode)),juce::dontSendNotification);
    oscEnabled.setToggleState(o.enabled,juce::dontSendNotification);
    oscEnabled.setButtonText("OSC " + juce::String::charToString(juce::juce_wchar('A'+oscIndex)));
    filterEnabled.setToggleState(p.filters[0].enabled,juce::dontSendNotification);
    subEnabled.setToggleState(p.oscillators[4].enabled,juce::dontSendNotification);
    noiseEnabled.setToggleState(p.oscillators[3].enabled,juce::dontSendNotification);
    if(!subLevel.isMouseButtonDown())subLevel.setValue(p.oscillators[4].volume,juce::dontSendNotification);
    if(!noiseLevel.isMouseButtonDown())noiseLevel.setValue(p.oscillators[3].volume,juce::dontSendNotification);
    const auto route=p.routes[std::size_t(oscIndex)].target;
    oscRoute.setSelectedId(route==zyg::RouteTarget::filter?1:route==zyg::RouteTarget::main?2:
        route==zyg::RouteTarget::direct?3:4,juce::dontSendNotification);
    const double ov[]={double(o.octave),double(o.semitone),o.fine,o.volume,o.pan,o.initialPhase,o.randomPhase,double(o.unison),o.detune};
    for(int i=0;i<9;++i)if(!oscSliders[std::size_t(i)].isMouseButtonDown())oscSliders[std::size_t(i)].setValue(ov[i],juce::dontSendNotification);
    const auto& e=p.envelopes[0]; const double ev[]={e.attack,e.hold,e.decay,e.sustain,e.release};
    for(int i=0;i<5;++i)if(!envSliders[std::size_t(i)].isMouseButtonDown())envSliders[std::size_t(i)].setValue(ev[i],juce::dontSendNotification);
    if(!tablePosition.isMouseButtonDown())tablePosition.setValue(o.tablePosition,juce::dontSendNotification);
    if(!master.isMouseButtonDown())master.setValue(p.masterVolume,juce::dontSendNotification);
    if(!cutoff.isMouseButtonDown())cutoff.setValue(p.filters[0].cutoff,juce::dontSendNotification);
    if(!resonance.isMouseButtonDown())resonance.setValue(p.filters[0].resonance,juce::dontSendNotification);
    if(!filterDrive.isMouseButtonDown())filterDrive.setValue(p.filters[0].drive,juce::dontSendNotification);
    if(!filterMix.isMouseButtonDown())filterMix.setValue(p.filters[0].wet,juce::dontSendNotification);
    if(!lfoRate.isMouseButtonDown())lfoRate.setValue(p.lfoOneRateHz,juce::dontSendNotification);
    for(int i=0;i<4;++i)if(!macroSliders[std::size_t(i)].isMouseButtonDown())
        macroSliders[std::size_t(i)].setValue(p.macroValues[std::size_t(i)],juce::dontSendNotification);
    lfoRate.setEnabled(p.lfoOneSine);
    nativeLfo.setEnabled(!p.lfoOneSine);
    nativeLfo.setButtonText(p.lfoOneSine?"Sine active":"Use sine");
    lfoLabel.setText(p.lfoOneSine?"LFO 1 | sine rate (Hz)":"LFO 1 | imported shape unsupported",juce::dontSendNotification);
    unsupported.setText(p.fx.empty()?"FX: none":"FX: "+juce::String(p.fx.size())+" imported instances | DSP pending",juce::dontSendNotification);
    juce::String fxText="FX RACK | preservation view\n\nImported effect instances are retained in patch and host state. Effects DSP and editing are not implemented yet.\n\n";
    if(p.fx.empty()) fxText += "No FX instances in this patch.";
    for(const auto& effect:p.fx)
        fxText += "Rack "+juce::String(effect.rack)+"  Slot "+juce::String(effect.position)+"  "+juce::String(effect.type)+"  [DSP pending]\n";
    if(fxReadout.getText()!=fxText)fxReadout.setText(fxText,false);
    if(routeList.getNumItems()!=int(p.modulation.size())) {
        const int old=routeList.getSelectedId();routeList.clear(juce::dontSendNotification);
        for(std::size_t i=0;i<p.modulation.size();++i){const auto& m=p.modulation[i];
            routeList.addItem(juce::String(m.slot)+"  "+juce::String(m.sourceName)+" -> "+juce::String(m.destinationModule)+" "+juce::String(m.destinationParameter),int(i+1));}
        routeList.setSelectedId(old>0&&old<=routeList.getNumItems()?old:(routeList.getNumItems()?1:0),juce::dontSendNotification);
    }
    syncing=false;
    if(!matrixAmount.isMouseButtonDown())syncRoute(p);
    const auto count=processor.getMidiNoteCount();
    if(count!=lastMidiCount){lastMidiCount=count;midiLightTicks=8;}
    else if(midiLightTicks>0)--midiLightTicks;
    midiIndicator.setColour(juce::Label::textColourId,midiLightTicks?juce::Colours::lime:dim);
    voiceCount.setText("Voices "+juce::String(processor.getActiveVoiceCount()),juce::dontSendNotification);
    const float peak=processor.getOutputPeak();
    meterText.setText(peak>0?"Output "+juce::String(20.0*std::log10(double(peak)),1)+" dB":"Output -inf dB",juce::dontSendNotification);
    repaint();
    const auto snapshotPath=juce::SystemStats::getEnvironmentVariable("ZYG_UI_SNAPSHOT", {});
    if(!snapshotWritten && snapshotPath.isNotEmpty() && isShowing()) {
        snapshotWritten=true;
        auto image=createComponentSnapshot(getLocalBounds(),true,1.0f);
        if(auto stream=juce::File(snapshotPath).createOutputStream())
            juce::PNGImageFormat().writeImageToStream(image,*stream);
    }
}
void ZygEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);
    g.setColour(chassis);g.fillRect(0,0,getWidth(),84);
    g.setColour(highlight);g.drawHorizontalLine(83,0.0f,float(getWidth()));
    if (logo.isValid()) {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImageWithin(logo, 8, 5, 70, 70, juce::RectanglePlacement::centred, false);
    }
    if(fxPage) {
        panelBox(g,{10,90,980,465});
        sectionTitle(g,"FX RACK // imported ordering and state",10,90,980);
    }
    else {
        panelBox(g,{10,90,610,246}); sectionTitle(g,"OSCILLATOR",10,90,610);
        panelBox(g,{630,90,360,150}); sectionTitle(g,"FILTER 1 // ZDF SVF LOWPASS",630,90,360);
        panelBox(g,{630,248,360,158}); sectionTitle(g,"ENVELOPE 1",630,248,360);
        panelBox(g,{10,344,610,150}); sectionTitle(g,"MODULATION MATRIX",10,344,610);
        panelBox(g,{630,414,360,80}); sectionTitle(g,"LFO 1",630,414,360);
        panelBox(g,{10,502,980,88});
    }
    if(fxPage)return;
    const auto p=processor.getPatchSnapshot();
    const auto& osc=p.oscillators[std::size_t(std::clamp(oscSelect.getSelectedId()-1,0,2))];
    const auto& audio=osc.audio;
    juce::Rectangle<float> r(24,135,180,82);
    g.setColour(sunken);g.fillRect(r);
    g.setColour(line);g.drawRect(r,1.0f);
    g.setColour(juce::Colour(0xff1e152e));
    for(int x=1;x<4;++x)g.drawVerticalLine(int(r.getX()+x*r.getWidth()/4),r.getY(),r.getBottom());
    for(int y=1;y<4;++y)g.drawHorizontalLine(int(r.getY()+y*r.getHeight()/4),r.getX(),r.getRight());
    if(audio.size()>=osc.frameSize && osc.frameSize>1) {
        juce::Path wave;const auto size=osc.frameSize; const unsigned width=176;
        for(unsigned x=0;x<width;++x){const auto idx=std::size_t(x)*size/width;
            const float y=r.getCentreY()-std::clamp(audio[idx],-1.0f,1.0f)*(r.getHeight()*0.42f);
            if(x==0)wave.startNewSubPath(r.getX()+2,y);else wave.lineTo(r.getX()+2+x,y);}
        g.setColour(cyan);g.strokePath(wave,juce::PathStrokeType(1.4f));
    } else {
        juce::Path wave;
        for(int x=0;x<176;++x){const float y=r.getCentreY()-std::sin(float(x)*juce::MathConstants<float>::twoPi/176.0f)*28.0f;
            if(x==0)wave.startNewSubPath(r.getX()+2,y);else wave.lineTo(r.getX()+2+x,y);}
        g.setColour(cyan);g.strokePath(wave,juce::PathStrokeType(1.0f));
    }
    g.setColour(dim);g.setFont(juce::Font(juce::FontOptions("DejaVu Sans Mono",8.0f,juce::Font::plain)));
    g.drawText("LIVE WAVETABLE FRAME",28,138,150,12,juce::Justification::centredLeft);

    // The filter graph is calculated from the same normalized cutoff and resonance used by DSP.
    juce::Rectangle<float> fr(640,116,178,82);g.setColour(sunken);g.fillRect(fr);g.setColour(line);g.drawRect(fr,1.0f);
    juce::Path filterPath; const float fc=std::clamp(float(p.filters[0].cutoff),0.0f,1.0f);
    const float resonanceLift=std::clamp(float(p.filters[0].resonance/100.0),0.0f,1.0f)*22.0f;
    for(int x=0;x<174;++x){const float nx=float(x)/173.0f;const float d=(nx-fc)*18.0f;
        const float low=1.0f/(1.0f+std::exp(d));
        const float bump=resonanceLift*std::exp(-100.0f*(nx-fc)*(nx-fc));
        const float y=fr.getBottom()-5.0f-low*62.0f-bump;
        if(x==0)filterPath.startNewSubPath(fr.getX()+2,y);else filterPath.lineTo(fr.getX()+2+x,y);}
    g.setColour(cyan);g.strokePath(filterPath,juce::PathStrokeType(1.0f));

    const auto& e=p.envelopes[0];
    juce::Rectangle<float> er(640,272,340,64);g.setColour(sunken);g.fillRect(er);g.setColour(line);g.drawRect(er,1.0f);
    for(int x=1;x<8;++x){g.setColour(juce::Colour(0xff1e152e));g.drawVerticalLine(int(er.getX()+x*er.getWidth()/8),er.getY(),er.getBottom());}
    const float a=std::clamp(float(e.attack)*17.0f,3.0f,70.0f),h=std::clamp(float(e.hold)*12.0f,0.0f,35.0f),
        d=std::clamp(float(e.decay)*16.0f,4.0f,70.0f), sy=er.getBottom()-4.0f-(er.getHeight()-9.0f)*std::clamp(float(e.sustain),0.0f,1.0f);
    juce::Path env;env.startNewSubPath(er.getX()+3,er.getBottom()-4);env.lineTo(er.getX()+3+a,er.getY()+5);
    env.lineTo(er.getX()+3+a+h,er.getY()+5);env.lineTo(er.getX()+3+a+h+d,sy);
    env.lineTo(er.getRight()-55,sy);env.lineTo(er.getRight()-3,er.getBottom()-4);
    g.setColour(cyan.withAlpha(0.18f));juce::Path fill=env;fill.lineTo(er.getRight()-3,er.getBottom()-3);fill.lineTo(er.getX()+3,er.getBottom()-3);fill.closeSubPath();g.fillPath(fill);
    g.setColour(cyan);g.strokePath(env,juce::PathStrokeType(1.0f));

    juce::Rectangle<float> lr(640,438,180,46);g.setColour(sunken);g.fillRect(lr);g.setColour(line);g.drawRect(lr,1.0f);
    juce::Path lp;
    for(int x=0;x<176;++x){const float phase=float(x)/175.0f*juce::MathConstants<float>::twoPi;
        const float y=lr.getCentreY()-std::sin(phase)*16.0f;
        if(x==0)lp.startNewSubPath(lr.getX()+2,y);else lp.lineTo(lr.getX()+2+x,y);}
    g.setColour(p.lfoOneSine?purpleHot:muted);g.strokePath(lp,juce::PathStrokeType(1.0f));

    const float peak=processor.getOutputPeak();
    g.setColour(sunken);g.fillRect(825,555,150,8);g.setColour(line);g.drawRect(825,555,150,8);
    g.setColour(peak>0.9f?juce::Colour(0xffff2a55):cyan);g.fillRect(826,556,int(148*std::clamp(peak,0.0f,1.0f)),6);
}
void ZygEditor::resized() {
    title.setBounds(82,5,110,20);presetName.setBounds(195,5,440,20);
    init.setBounds(82,30,48,22);loadSerum.setBounds(134,30,100,22);
    loadNative.setBounds(238,30,78,22);saveNative.setBounds(320,30,78,22);
    selectAssets.setBounds(402,30,68,22);diagnostics.setBounds(474,30,84,22);
    synthTab.setBounds(82,56,62,22);fxTab.setBounds(148,56,42,22);
    status.setBounds(196,55,430,22);
    masterLabel.setBounds(818,4,62,14);master.setBounds(870,3,54,70);
    midiIndicator.setBounds(632,54,48,22);voiceCount.setBounds(681,54,80,22);meterText.setBounds(760,54,115,22);
    audition.setBounds(925,30,67,42);

    oscEnabled.setBounds(20,112,82,18);oscSelect.setBounds(106,112,80,18);
    oscName.setBounds(194,112,303,18);oscRoute.setBounds(500,112,108,18);
    loadWavetable.setBounds(24,224,180,22);
    tableLabel.setBounds(213,132,72,13);tablePosition.setBounds(220,144,58,68);
    for(int i=0;i<9;++i){const int slot=i+1,col=slot%5,row=slot/5;
        const int x=213+col*78,y=132+row*102;
        oscLabels[std::size_t(i)].setBounds(x,y,72,13);
        oscSliders[std::size_t(i)].setBounds(x+5,y+13,58,70);}

    filterEnabled.setBounds(884,91,98,17);oscRoute.toFront(false);
    cutoffLabel.setBounds(827,115,72,13);cutoff.setBounds(834,128,54,56);
    resonanceLabel.setBounds(900,115,82,13);resonance.setBounds(910,128,54,56);
    driveLabel.setBounds(827,181,58,13);filterDrive.setBounds(834,194,54,42);
    mixLabel.setBounds(903,181,58,13);filterMix.setBounds(910,194,54,42);

    for(int i=0;i<5;++i){const int x=640+i*68;
        envLabels[std::size_t(i)].setBounds(x,339,64,12);
        envSliders[std::size_t(i)].setBounds(x+6,351,52,50);}

    lfoLabel.setBounds(785,416,196,18);lfoRate.setBounds(830,437,56,52);
    nativeLfo.setBounds(895,444,84,26);

    matrixLabel.setBounds(20,446,585,17);addRoute.setBounds(525,347,84,18);
    routeList.setBounds(20,372,589,22);
    matrixSource.setBounds(20,402,150,22);matrixDestination.setBounds(177,402,194,22);
    amountLabel.setBounds(382,400,72,14);matrixAmount.setBounds(377,416,232,24);
    unsupported.setBounds(20,470,585,16);

    subEnabled.setBounds(20,510,70,20);subLevel.setBounds(94,508,52,68);
    noiseEnabled.setBounds(154,510,76,20);noiseLevel.setBounds(234,508,52,68);
    browseNoise.setBounds(292,510,104,22);
    for(int i=0;i<4;++i){const int x=410+i*74;
        macroLabels[std::size_t(i)].setBounds(x,507,70,12);
        macroSliders[std::size_t(i)].setBounds(x+8,519,52,58);}
    fxReadout.setBounds(20,116,960,428);
}
