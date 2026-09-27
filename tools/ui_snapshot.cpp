// Offline UI snapshot tool: renders the editor or a widget gallery to PNG.
//   zygzxg_ui_snapshot --gallery out.png
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Widgets.h"
#include "ui/ModCatalog.h"
#include "ui/FxTree.h"
#include "ui/UserLibrary.h"
#include "ui/PresetBrowser.h"

using namespace zyg::ui;

namespace {
struct Gallery : juce::Component {
    Gallery(UiContext& c) : ctx(c) {
        setSize(640, 300);
        for (int i = 0; i < 6; ++i) {
            KnobSpec s; s.label = "KNOB " + juce::String(i); s.bipolar = i % 2;
            auto k = std::make_unique<Knob>(ctx, s, [](const zyg::Patch&) { return 0.5; }, [](zyg::Patch&, double) {}, 16 + i * 4);
            k->setBounds(10 + i * 50, 150, 44, 50);
            addAndMakeVisible(*k); knobs.push_back(std::move(k));
        }
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(pal::chassis);
        drawText(g, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789", 8, 8, pal::textHi);
        drawText(g, "WT POS  UNISON  DETUNE  BLEND  CUTOFF 1.2 KHZ  -12.5 DB  100% 45.0 HZ  ()[]<>=+-/:.,'*#?!", 8, 18, pal::lcd);
        drawText(g, "BOLD LABEL SAMPLE ABC XYZ", 8, 28, pal::textHi, 1, true);
        drawText(g, "SCALE 2X", 8, 40, pal::acidHot, 2);
        int x = 8;
        for (int i = 0; i < int(Icon::count); ++i) {
            drawIcon(g, Icon(i), x, 60, pal::textBody);
            x += iconSize(Icon(i)).x + 5;
        }
        int wx = 8;
        for (auto w : {WaveIcon::sine, WaveIcon::rounded, WaveIcon::saw, WaveIcon::square, WaveIcon::triangle, WaveIcon::pulse, WaveIcon::noise}) {
            drawWaveIcon(g, w, {wx, 78, 24, 16}, pal::acid); wx += 30;
        }
        bevelBox(g, {8, 100, 60, 14}, pal::raised); drawTextIn(g, "BUTTON", {8, 100, 60, 14}, pal::textBody, juce::Justification::centred, 1, true);
        drawLcd(g, {74, 100, 60, 14}, "1.25 KHZ");
        wellBox(g, {140, 100, 60, 14}); drawModuleFrame(g, {206, 100, 60, 14});
        drawCaption(g, {272, 100, 100, 14}, "GLOBAL");
        for (int i = 0; i < 8; ++i) {
            KnobStyle st; st.bipolar = i > 5;
            blit(g, knobImage(24, float(i) / 7.0f, st, i == 3 ? 0.3f : 0.0f, i == 3 ? 0.6f : 0.0f), 8 + i * 30, 120);
        }
    }
    UiContext& ctx;
    std::vector<std::unique_ptr<Knob>> knobs;
};
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(argv[i]);
    if (args.contains("--selftest-resample")) {   // capture the plugin's own output into an oscillator
        int failed = 0;
        auto check = [&](bool c, const char* what) { if (!c) { std::printf("FAIL %s\n", what); ++failed; } };
        ZygProcessor p;
        p.setPlayConfigDetails(0, 2, 48000.0, 512); p.prepareToPlay(48000.0, 512);
        check(p.armResample(1, 1.0), "arm");
        check(p.getResampleState() == ZygProcessor::ResampleState::armed, "armed state");
        juce::AudioBuffer<float> buf(2, 512);
        double energy = 0.0;
        for (int b = 0; b < 120 && p.getResampleState() != ZygProcessor::ResampleState::idle; ++b) {
            juce::MidiBuffer midi;
            if (b == 3) midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 100);
            p.processBlock(buf, midi);
            for (int i = 0; i < 512; ++i) energy += double(buf.getSample(0, i)) * buf.getSample(0, i);
            if (b == 3) check(p.getResampleState() == ZygProcessor::ResampleState::recording, "recording after note-on");
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        }
        for (int i = 0; i < 40 && p.getResampleState() != ZygProcessor::ResampleState::idle; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        check(p.getResampleState() == ZygProcessor::ResampleState::idle, "finished");
        const auto patch = p.getPatch();
        const auto& o = patch->oscillators[1];
        check(o.mode == zyg::OscMode::sample && o.sample && o.baseNote == 64 && o.pitchTrack, "osc B is a pitch-tracked sample rooted on the played note");
        check(o.sample && o.sample->frames() == 24000 && o.sample->rootNote == 64, "one beat at 120 bpm / 48 kHz");
        double e = 0; if (o.sample) for (float v : o.sample->left) e += double(v) * v;
        check(e > 1.0, "captured audio is not silent");
        check(juce::File(juce::String(o.asset)).existsAsFile(), "wav written");
        if (juce::File(juce::String(o.asset)).existsAsFile()) juce::File(juce::String(o.asset)).deleteFile();
        check(p.takeResampleMessage().startsWith("RESAMPLED"), "user message");
        std::printf(failed ? "resample test FAILED (energy %.3f)\n" : "resample test passed (energy %.3f)\n", energy);
        return failed ? 1 : 0;
    }
    if (args.contains("--write-showcase")) {   // ZYG-native example patch (no Serum/Xfer data): presets/
        using namespace zyg;
        Patch p;
        p.name = "ZYG Showcase - Acid Morph"; p.author = "ZYG-ZXG"; p.masterVolume = 0.7; p.polyphony = 8;
        auto additiveTable = [](Oscillator& o, int frames, auto&& amp) {   // band-limited frames by additive synthesis
            o.frameSize = 2048; o.audio.assign(std::size_t(frames) * 2048u, 0.0f);
            for (int f = 0; f < frames; ++f) {
                const double t = frames > 1 ? double(f) / double(frames - 1) : 0.0;
                float* fr = o.audio.data() + std::size_t(f) * 2048u; float peak = 1.0e-6f;
                for (int i = 0; i < 2048; ++i) {
                    double v = 0.0;
                    for (int h = 1; h <= 48; ++h) v += amp(h, t) * std::sin(6.283185307179586 * h * i / 2048.0);
                    fr[i] = float(v); peak = std::max(peak, std::abs(fr[i]));
                }
                for (int i = 0; i < 2048; ++i) fr[i] /= peak;
            }
            o.tableName = "ZYG Additive";
        };
        auto& a = p.oscillators[0];   // saw -> vowel-ish formant sweep
        a.enabled = true; a.mode = OscMode::wavetable; a.unison = 5; a.detune = 0.22; a.tablePosition = 96; a.volume = 0.72;
        additiveTable(a, 64, [](int h, double t) { const double centre = 2.0 + 22.0 * t; return (1.0 / h) * (0.35 + std::exp(-std::pow((h - centre) / 3.5, 2))); });
        a.warpDefinitions[0].mode = WarpMode::bendPositive; a.warpOneAmount = 0.28;
        auto& b = p.oscillators[1];   // pulse-width sweep
        b.enabled = true; b.mode = OscMode::wavetable; b.octave = -1; b.volume = 0.45; b.tablePosition = 40;
        additiveTable(b, 32, [](int h, double t) { const double w = 0.5 - 0.42 * t; return std::sin(3.141592653589793 * h * w) / h; });
        auto& sub = p.oscillators[4]; sub.enabled = true; sub.mode = OscMode::sub; sub.octave = -1; sub.volume = 0.35;
        for (int i : {0, 1, 4}) { p.routes[std::size_t(i)].target = RouteTarget::filter; p.routes[std::size_t(i)].filterBalance = -100.0; }
        auto& f = p.filters[0]; f.enabled = true; f.type = "L24"; f.response = FilterResponse::low24; f.cutoff = 0.52; f.resonance = 48; f.drive = 22;
        p.envelopes[0] = {}; p.envelopes[0].attack = 0.006; p.envelopes[0].decay = 0.9; p.envelopes[0].sustain = 0.65; p.envelopes[0].release = 0.45;
        p.envelopes[1] = {}; p.envelopes[1].attack = 0.002; p.envelopes[1].decay = 0.42; p.envelopes[1].sustain = 0.12; p.envelopes[1].release = 0.3;
        p.lfoOneSine = false;
        p.lfoDefinitions[0].shape = LfoShape::sine; p.lfoDefinitions[0].rateHz = 0.35;
        p.lfoDefinitions[1].shape = LfoShape::lorenz; p.lfoDefinitions[1].rateHz = 0.8;
        auto route = [&](ModSource s, int si, ModTarget t, int ti, int prm, double amt) {
            ModulationRoute r; r.slot = int(p.modulation.size()); r.sourceKind = s; r.sourceIndex = si;
            r.targetKind = t; r.targetIndex = ti; r.targetParam = prm; r.amount = amt; r.bipolar = modSourceIsBipolar(s);
            p.modulation.push_back(r);
        };
        route(ModSource::lfo, 0, ModTarget::oscParam, 0, int(OscParam::tablePos), 45);
        route(ModSource::envelope, 1, ModTarget::filterParam, 0, int(FilterParam::freq), 38);
        route(ModSource::lfo, 1, ModTarget::oscParam, 1, int(OscParam::tablePos), 30);
        route(ModSource::velocity, 0, ModTarget::filterParam, 0, int(FilterParam::drive), 25);
        route(ModSource::macro, 0, ModTarget::oscParam, 0, int(OscParam::warp1), 60);
        zyg::ui::addFxModule(p, 0, FxType::distortion);
        zyg::ui::addFxModule(p, 0, FxType::chorus);
        zyg::ui::addFxModule(p, 0, FxType::delay);
        zyg::ui::addFxModule(p, 0, FxType::reverb);
        const juce::File out(args[args.indexOf("--write-showcase") + 1]);
        out.getParentDirectory().createDirectory();
        const bool ok = out.replaceWithText(juce::String::fromUTF8(zyg::patchToJson(p).dump(1).c_str()));
        std::printf(ok ? "wrote %s\n" : "could not write %s\n", out.getFullPathName().toRawUTF8());
        return ok ? 0 : 1;
    }
    if (args.contains("--selftest-library")) {   // user library layout, preset typing, equal Serum/ZYG listing
        using namespace zyg::ui;
        int failed = 0;
        auto check = [&](bool c, const juce::String& what) { if (!c) { std::printf("FAIL %s\n", what.toRawUTF8()); ++failed; } };
        const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("zyg-lib-test-" + juce::String(juce::Random().nextInt()));
        const auto previous = library::userRoot();
        const bool hadSetting = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ZYG-ZXG/library.txt").existsAsFile();
        library::setUserRoot(tmp);
        check(library::presetsDir().getChildFile("PUT YOUR PRESETS HERE.txt").existsAsFile(), "presets readme");
        check(library::wavetablesDir().getChildFile("PUT YOUR WAVETABLES HERE.txt").existsAsFile(), "wavetables readme");
        check(tmp.getChildFile("README.txt").existsAsFile(), "root readme");
        library::presetsDir().getChildFile("PUT YOUR PRESETS HERE.txt").replaceWithText("mine");
        library::ensureUserFolders();
        check(library::presetsDir().getChildFile("PUT YOUR PRESETS HERE.txt").loadFileAsString() == "mine", "readme is never overwritten");
        library::presetsDir().getChildFile("Pack").createDirectory();
        library::presetsDir().getChildFile("Pack/DS_AC2_bass_reese_trend.SerumPreset").replaceWithText("x");
        library::presetsDir().getChildFile("My Lead.zygpreset").replaceWithText("{}");
        const auto entries = PresetBrowserPanel::scan({{"USER", library::presetsDir(), true}});
        check(entries.size() == 2, "both Serum and ZYG presets are listed as user presets");
        for (const auto& e : entries) check(e.user, "entry marked user");
        const auto root = library::presetsDir();
        check(library::presetTypeOf(root.getChildFile("Factory/Bass/Reese/BA - Kitchen Sink.SerumPreset"), root) == "BASS", "BA prefix");
        check(library::presetTypeOf(root.getChildFile("x/LD - Something.SerumPreset"), root) == "LEAD", "LD prefix");
        check(library::presetTypeOf(root.getChildFile("x/PD - Air.SerumPreset"), root) == "PAD", "PD prefix");
        check(library::presetTypeOf(root.getChildFile("Pack/DS_AC2_bass_reese_trend.SerumPreset"), root) == "BASS", "keyword bass");
        check(library::presetTypeOf(root.getChildFile("Pack/MO_HA_BS_Amped.SerumPreset"), root) == "BASS", "BS token");
        check(library::presetTypeOf(root.getChildFile("Keys/Warm.zygpreset"), root) == "KEYS", "folder keys");
        check(library::presetTypeOf(root.getChildFile("Pack/Untitled 3.zygpreset"), root) == "OTHER", "unknown -> OTHER");
        if (hadSetting) library::setUserRoot(previous);
        else juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ZYG-ZXG/library.txt").deleteFile();
        tmp.deleteRecursively();
        std::printf(failed ? "library self-test FAILED\n" : "library self-test passed\n");
        return failed ? 1 : 0;
    }
    if (args.contains("--selftest-routes")) {   // drag-and-drop route creation helper
        using namespace zyg;
        int failed = 0;
        auto check = [&](bool c, const char* what) { if (!c) { std::printf("FAIL %s\n", what); ++failed; } };
        Patch p;
        const ui::TargetId cutoff {ModTarget::filterParam, 0, int(FilterParam::freq)};
        bool created = false;
        const int a = ui::addModulationRoute(p, ModSource::lfo, 0, cutoff, created);
        check(a == 0 && created, "first route created in slot 0");
        check(p.modulation[0].sourceKind == ModSource::lfo && p.modulation[0].amount == 50.0 && p.modulation[0].bipolar, "lfo route is bipolar amount 50");
        check(ui::normalizedTarget(p.modulation[0]) == cutoff, "target preserved");
        const int b = ui::addModulationRoute(p, ModSource::lfo, 0, cutoff, created);
        check(b == 0 && !created && p.modulation.size() == 1, "same source+target reuses the route");
        const int c = ui::addModulationRoute(p, ModSource::envelope, 1, cutoff, created);
        check(c == 1 && created && !p.modulation[1].bipolar, "second source gets a new unipolar route");
        p.modulation[0] = ModulationRoute{};
        check(ui::addModulationRoute(p, ModSource::macro, 2, {ModTarget::fxParam, 0, 3}, created) == 0 && created, "empty slot is reused");
        ModSource k; int i;
        check(ui::parseDragPayload(ui::makeDragPayload(ModSource::lfo, 7), k, i) && k == ModSource::lfo && i == 7, "payload round trip");
        check(!ui::parseDragPayload("junk", k, i), "junk payload rejected");
        while (p.modulation.size() < 64) p.modulation.push_back(ModulationRoute{}), p.modulation.back().sourceKind = ModSource::lfo, p.modulation.back().targetKind = ModTarget::globalParam;
        for (auto& r : p.modulation) if (r.sourceKind == ModSource::unknown) { r.sourceKind = ModSource::lfo; r.targetKind = ModTarget::globalParam; }
        check(ui::addModulationRoute(p, ModSource::note, 0, cutoff, created) == -1, "full matrix reports -1");
        std::printf(failed ? "route tests FAILED\n" : "route tests passed\n");
        return failed ? 1 : 0;
    }
    ZygProcessor proc;
    UiContext ctx(proc);
    if (args.contains("--gallery")) {
        Gallery gallery(ctx);
        const int scale = 2;
        auto img = gallery.createComponentSnapshot(gallery.getLocalBounds(), true, float(scale));
        juce::File out(args[args.indexOf("--gallery") + 1]);
        out.deleteFile();
        if (auto s = out.createOutputStream()) juce::PNGImageFormat().writeImageToStream(img, *s);
        return 0;
    }
    // --editor out.png [--page N] [--preset file] [--scale N]
    if (args.contains("--editor")) {
        if (args.contains("--preset")) {
            const juce::File f(args[args.indexOf("--preset") + 1]);
            if (f.hasFileExtension("zygpreset")) proc.loadNativePreset(f); else proc.loadPreset(f);
        }
        if (args.contains("--addfx"))   // e.g. --addfx pump,stutter  (ZYG extension effects, main rack)
            for (const auto& name : juce::StringArray::fromTokens(args[args.indexOf("--addfx") + 1], ",", ""))
                proc.editPatch([&](zyg::Patch& p) { zyg::ui::addFxModule(p, 0, name == "pump" ? zyg::FxType::pump : zyg::FxType::stutter); });
        std::unique_ptr<ZygEditor> editor(static_cast<ZygEditor*>(proc.createEditor()));
        const int scale = args.contains("--scale") ? args[args.indexOf("--scale") + 1].getIntValue() : 1;
        if (args.contains("--size")) {
            const auto wh = args[args.indexOf("--size") + 1];
            editor->setSize(wh.upToFirstOccurrenceOf("x", false, false).getIntValue(), wh.fromFirstOccurrenceOf("x", false, false).getIntValue());
        }
        if (args.contains("--page")) editor->showPage(args[args.indexOf("--page") + 1].getIntValue());
        if (args.contains("--modal")) editor->openModalByName(args[args.indexOf("--modal") + 1]);
        const int live = args.contains("--live") ? args[args.indexOf("--live") + 1].getIntValue() : 0;
        const int waitMs = args.contains("--wait") ? args[args.indexOf("--wait") + 1].getIntValue() : 0;
        if (live > 0) {
            // put the editor on screen so timers/animations run, and drive the audio engine by hand
            proc.prepareToPlay(48000.0, 512);
            editor->addToDesktop(0); editor->setVisible(true);
            if (args.contains("--play")) proc.uiNote(60, true, 0.9f);
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
            for (int t = 0; t < live; t += 20) {
                proc.processBlock(buf, midi);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            }
        } else if (waitMs > 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(waitMs);
        auto img = editor->snapshot(scale, live == 0);
        juce::File out(args[args.indexOf("--editor") + 1]);
        out.deleteFile();
        if (auto s = out.createOutputStream()) juce::PNGImageFormat().writeImageToStream(img, *s);
        return 0;
    }
    return 1;
}
