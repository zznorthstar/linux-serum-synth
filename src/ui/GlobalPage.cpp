#include "GlobalPage.h"
#include "AssetBrowse.h"

namespace zyg::ui {

GlobalPage::GlobalPage(UiContext& c) : Panel(c) {
    sections_ = {{"TUNING & PITCH", {}}, {"PORTAMENTO", {}}, {"VOICING", {}}, {"QUALITY & MPE", {}}, {"TIME & LEVEL", {}}, {"VOICE PANEL", {}}, {"INTERFACE", {}}, {"PATCH INFO", {}}};
    auto spinner = [&](int sec, juce::Rectangle<int> r, const juce::String& label, double lo, double hi, double step, Getter g, Setter s,
                       std::function<juce::String(double)> f, double def, GlobalParam gp = GlobalParam::count) {
        auto& sp = make<Spinner>(ctx, label, lo, hi, step, std::move(g), std::move(s), std::move(f));
        sp.showArrows(true); sp.setLabelWidth(84); sp.setDefault(def);
        if (gp != GlobalParam::count) sp.setTarget({ModTarget::globalParam, 0, int(gp)});
        put(&sp, sec, r);
    };
    auto toggle = [&](int sec, juce::Rectangle<int> r, const juce::String& label, BoolGetter g, BoolSetter s) {
        auto& t = make<Toggle>(ctx, label, Toggle::Style::check, std::move(g), std::move(s));
        put(&t, sec, r);
    };
    auto knobAt = [&](int sec, juce::Rectangle<int> r, const juce::String& label, double lo, double hi, double def, Getter g, Setter s,
                      std::function<juce::String(double)> f, double skew = 1.0, GlobalParam gp = GlobalParam::count) {
        KnobSpec spec; spec.label = label; spec.lo = lo; spec.hi = hi; spec.def = def; spec.skew = skew; spec.format = std::move(f);
        auto& k = make<Knob>(ctx, spec, std::move(g), std::move(s), 28);
        if (gp != GlobalParam::count) k.setTarget({ModTarget::globalParam, 0, int(gp)});
        put(&k, sec, r);
    };
    // TUNING & PITCH
    spinner(0, {8, 24, 250, 15}, "MASTER TUNING", 400, 480, 0.1, [](const Patch& p) { return p.globals.masterTuning; }, [](Patch& p, double v) { p.globals.masterTuning = v; },
        [](double v) { return formatNumber(v, 1) + " HZ"; }, 440, GlobalParam::masterTuning);
    spinner(0, {8, 42, 250, 15}, "TRANSPOSE", -24, 24, 1, [](const Patch& p) { return p.globals.transpose; }, [](Patch& p, double v) { p.globals.transpose = v; },
        [](double v) { return (v > 0 ? "+" : "") + juce::String(int(v)) + " ST"; }, 0, GlobalParam::transpose);
    spinner(0, {8, 60, 250, 15}, "BEND UP", 0, 24, 1, [](const Patch& p) { return p.globals.bendUp; }, [](Patch& p, double v) { p.globals.bendUp = v; },
        [](double v) { return juce::String(int(v)) + " ST"; }, 2);
    spinner(0, {8, 78, 250, 15}, "BEND DOWN", -24, 0, 1, [](const Patch& p) { return p.globals.bendDown; }, [](Patch& p, double v) { p.globals.bendDown = v; },
        [](double v) { return juce::String(int(v)) + " ST"; }, -2);
    priority_ = &make<Chooser>(); priority_->setTextColour(pal::lcd);
    priority_->buildMenu = [this](juce::PopupMenu& m) {
        struct P { VoicePriority v; const char* n; };
        for (auto pr : {P{VoicePriority::latest, "LATEST NOTE"}, P{VoicePriority::low, "LOWEST NOTE"}, P{VoicePriority::high, "HIGHEST NOTE"}})
            m.addItem(pr.n, true, ctx.patch->globals.priority == pr.v, [this, pr] { ctx.editNow([pr](Patch& p) { p.globals.priority = pr.v; }); });
    };
    put(priority_, 0, {94, 96, 164, 15});
    // PORTAMENTO
    knobAt(1, {8, 22, 60, 46}, "TIME", 0, 3, 0, [](const Patch& p) { return p.globals.portamentoTime; }, [](Patch& p, double v) { p.globals.portamentoTime = v; }, fmt::seconds, 0.5, GlobalParam::portamentoTime);
    knobAt(1, {72, 22, 60, 46}, "CURVE", 0, 100, 50, [](const Patch& p) { return p.globals.portamentoCurve; }, [](Patch& p, double v) { p.globals.portamentoCurve = v; },
           [](double v) { return formatNumber(v, 0) + "%"; });
    toggle(1, {140, 26, 100, 11}, "ALWAYS", [](const Patch& p) { return p.globals.portaAlways; }, [](Patch& p, bool v) { p.globals.portaAlways = v; });
    toggle(1, {140, 44, 100, 11}, "SCALED", [](const Patch& p) { return p.globals.portaScaled; }, [](Patch& p, bool v) { p.globals.portaScaled = v; });
    // VOICING
    toggle(2, {8, 24, 120, 11}, "MONO", [](const Patch& p) { return p.mono; }, [](Patch& p, bool v) { p.mono = v; });
    toggle(2, {8, 42, 120, 11}, "LEGATO", [](const Patch& p) { return p.globals.legato; }, [](Patch& p, bool v) { p.globals.legato = v; });
    toggle(2, {8, 60, 120, 11}, "NOTE LATCH", [](const Patch& p) { return p.globals.noteLatch; }, [](Patch& p, bool v) { p.globals.noteLatch = v; });
    toggle(2, {8, 78, 150, 11}, "LIMIT SAME NOTE POLY", [](const Patch& p) { return p.globals.limitSameNote; }, [](Patch& p, bool v) { p.globals.limitSameNote = v; });
    spinner(2, {8, 96, 210, 15}, "POLYPHONY", 1, 64, 1, [](const Patch& p) { return double(p.polyphony); }, [](Patch& p, double v) { p.polyphony = int(v); },
        [](double v) { return juce::String(int(v)) + " VOICES"; }, 16);
    // QUALITY & MPE
    oversample_ = &make<Chooser>(); oversample_->setTextColour(pal::lcd);
    oversample_->buildMenu = [this](juce::PopupMenu& m) {
        static const char* n[] = {"1X (OFF)", "2X", "4X", "8X"};
        for (int i = 0; i < 4; ++i) m.addItem(n[i], true, ctx.patch->globals.oversampling == i, [this, i] { ctx.editNow([i](Patch& p) { p.globals.oversampling = i; }); });
    };
    put(oversample_, 3, {94, 24, 124, 15});
    toggle(3, {8, 46, 130, 11}, "MPE ENABLED", [](const Patch& p) { return p.globals.mpeEnabled; }, [](Patch& p, bool v) { p.globals.mpeEnabled = v; });
    spinner(3, {8, 64, 210, 15}, "MPE BEND RANGE", 1, 96, 1, [](const Patch& p) { return p.globals.mpePitchBendRange; }, [](Patch& p, double v) { p.globals.mpePitchBendRange = v; },
        [](double v) { return juce::String(int(v)) + " ST"; }, 48);
    toggle(3, {8, 84, 170, 11}, "MIDI OUT: CLIP PLAYER", [](const Patch& p) { return p.globals.midiOutClipPlayer; }, [](Patch& p, bool v) { p.globals.midiOutClipPlayer = v; });
    // TIME & LEVEL
    knobAt(4, {8, 22, 60, 46}, "ENV TIME", 0, 100, 50, [](const Patch& p) { return p.globals.envTimeScale; }, [](Patch& p, double v) { p.globals.envTimeScale = v; },
           [](double v) { return formatNumber(v, 0); }, 1.0, GlobalParam::envTimeScale);
    knobAt(4, {72, 22, 60, 46}, "LFO TIME", 0, 100, 50, [](const Patch& p) { return p.globals.lfoTimeScale; }, [](Patch& p, double v) { p.globals.lfoTimeScale = v; },
           [](double v) { return formatNumber(v, 0); }, 1.0, GlobalParam::lfoTimeScale);
    knobAt(4, {136, 22, 60, 46}, "VOICE AMP", 0, 2, 1, [](const Patch& p) { return p.globals.voiceAmp; }, [](Patch& p, double v) { p.globals.voiceAmp = v; },
           [](double v) { return fmt::db(v); }, 1.0, GlobalParam::voiceAmp);
    knobAt(4, {200, 22, 60, 46}, "VEL > AMP", 0, 1, 0, [](const Patch& p) { return p.globals.velocityAmpDepth; }, [](Patch& p, double v) { p.globals.velocityAmpDepth = v; },
           fmt::percent01);
    // VOICE PANEL (per-voice offsets)
    toggle(5, {8, 22, 120, 11}, "PANEL ACTIVE", [](const Patch& p) { return p.voicePanel.active; }, [](Patch& p, bool v) { p.voicePanel.active = v; });
    for (int v = 0; v < 8; ++v) {
        auto addSlider = [&](int col, std::array<double, 8> VoicePanel::*arr) {
            auto& s = make<HSlider>(ctx, -100.0, 100.0, 0.0, [v, arr](const Patch& p) { return (p.voicePanel.*arr)[std::size_t(v)]; },
                [v, arr](Patch& p, double x) { (p.voicePanel.*arr)[std::size_t(v)] = x; });
            put(&s, 5, {34 + col * 1000, 52 + v * 15, 1, 12});   // x refined in resized()
        };
        addSlider(0, &VoicePanel::detune); addSlider(1, &VoicePanel::pan); addSlider(2, &VoicePanel::envTime); addSlider(3, &VoicePanel::cutoff);
    }
    // INTERFACE
    for (int i = 0; i < 3; ++i) {
        auto& b = make<PixelButton>(juce::String(i + 1) + "00%"); b.setBold(true);
        b.onClick = [this, i] {
            // the editor owns the window size; ask it through the top-level component
            if (auto* top = findParentComponentOfClass<juce::AudioProcessorEditor>()) {
                if (auto* fn = dynamic_cast<UiScaleTarget*>(top)) fn->setUiScale(i + 1);
            }
        };
        scaleBtns_[std::size_t(i)] = &b;
        put(&b, 6, {8 + i * 62, 24, 58, 18});
    }
    content_ = &make<Chooser>(); content_->setTextColour(pal::textBody); content_->setBold(false);
    content_->buildMenu = [this](juce::PopupMenu& m) {
        m.addItem("CHANGE CONTENT FOLDER...", [this] {
            auto chooser = std::make_shared<juce::FileChooser>("Select your legally owned Serum content folder");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this, chooser](const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) ctx.proc.setAssetRoot(fc.getResult()); });
        });
        m.addItem("COPY PATH", [this] { juce::SystemClipboard::copyTextToClipboard(contentPath_); });
    };
    put(content_, 6, {8, 62, 300, 15});
    auto& diag = make<PixelButton>("IMPORT DIAGNOSTICS"); diag.setBold(true);
    diag.onClick = [this] {
        juce::StringArray lines;
        for (const auto& d : ctx.patch->diagnostics) lines.add(juce::String(d.status).toUpperCase() + "  " + juce::String(d.path) + "  " + juce::String(d.detail));
        if (lines.isEmpty()) lines.add("NO DIAGNOSTICS FOR THIS PATCH");
        if (ctx.openText) ctx.openText("IMPORT DIAGNOSTICS", lines);
    };
    put(&diag, 6, {8, 84, 140, 18});
}

void GlobalPage::refresh(const Patch& p) {
    Panel::refresh(p);
    priority_->setText(p.globals.priority == VoicePriority::low ? "LOWEST NOTE" : p.globals.priority == VoicePriority::high ? "HIGHEST NOTE" : "LATEST NOTE");
    static const char* n[] = {"1X (OFF)", "2X", "4X", "8X"};
    oversample_->setText(n[std::clamp(p.globals.oversampling, 0, 3)]);
    contentPath_ = contentRoot(p).getFullPathName();
    content_->setText(contentPath_.isEmpty() ? "NO CONTENT FOLDER SET" : contentPath_.toUpperCase());
    for (int i = 0; i < 3; ++i) scaleBtns_[std::size_t(i)]->setToggled(ctx.uiScale == i + 1);
    repaint();
}

void GlobalPage::resized() {
    const int w = getWidth(), h = getHeight();
    const int colW = (w - 12) / 3;
    const int x0 = 3, x1 = 3 + colW + 3, x2 = 3 + 2 * (colW + 3);
    sections_[0].area = {x0, 2, colW, 124};                       // tuning
    sections_[1].area = {x0, 129, colW, 76};                      // portamento
    sections_[6].area = {x0, 208, colW, std::max(96, h - 210)};   // interface
    sections_[2].area = {x1, 2, colW, 124};                       // voicing
    sections_[3].area = {x1, 129, colW, 100};                     // quality
    sections_[7].area = {x1, 232, colW, std::max(60, h - 234)};   // patch info
    sections_[4].area = {x2, 2, colW, 84};                        // time & level
    sections_[5].area = {x2, 89, colW, std::max(120, h - 91)};    // voice panel
    for (std::size_t i = 0; i < comps_.size(); ++i) {
        const auto& sec = sections_[std::size_t(compSection_[i])].area;
        auto r = compRect_[i];
        int width = std::min(r.getWidth(), sec.getWidth() - 16);
        if (compSection_[i] == 5 && r.getWidth() == 1) {   // voice-panel sliders: four equal columns
            const int inner = sec.getWidth() - 40;
            const int col = (r.getX() - 34) / 1000;
            r = {34 + col * (inner / 4), r.getY(), inner / 4 - 4, r.getHeight()};
            width = r.getWidth();
        } else if (compSection_[i] == 0 || compSection_[i] == 2 || compSection_[i] == 3) width = sec.getWidth() - 16 - (r.getX() > 40 ? r.getX() - 8 : 0);
        comps_[i]->setBounds(sec.getX() + r.getX(), sec.getY() + r.getY(), width, r.getHeight());
    }
}

void GlobalPage::paint(juce::Graphics& g) {
    g.setColour(pal::chassis); g.fillRect(getLocalBounds());
    for (const auto& s : sections_) {
        drawModuleFrame(g, s.area, pal::panel);
        g.setColour(pal::raised); g.fillRect(s.area.getX() + 1, s.area.getY() + 1, s.area.getWidth() - 2, 16);
        hLine(g, s.area.getX() + 1, s.area.getY() + 17, s.area.getWidth() - 2, pal::edgeDark);
        drawTextIn(g, s.title, {s.area.getX() + 8, s.area.getY() + 1, s.area.getWidth() - 16, 16}, pal::violetHot, juce::Justification::centredLeft, 1, true);
    }
    // patch info
    {
        const auto& ia = sections_[7].area;
        const auto& p = *ctx.patch;
        int fx = 0; for (const auto& m : p.fx) fx += m.fxType != FxType::unknown;
        const juce::String rows[][2] = {{"NAME", juce::String(p.name)}, {"AUTHOR", juce::String(p.author.empty() ? "-" : p.author)},
            {"SOURCE", p.originalPreset.empty() ? "ZYG NATIVE" : "SERUM IMPORT " + juce::String(p.serumVersion)},
            {"MOD ROUTES", juce::String(int(p.modulation.size()))}, {"EFFECTS", juce::String(fx)},
            {"TYPED FIELDS", juce::String(int(p.mappedParameters)) + " / " + juce::String(int(p.explicitParameters))},
            {"DIAGNOSTICS", juce::String(int(p.diagnostics.size()))}};
        int y = ia.getY() + 24;
        for (const auto& row : rows) {
            if (y + 9 > ia.getBottom()) break;
            drawText(g, row[0], ia.getX() + 8, y, pal::textMuted);
            drawTextIn(g, row[1], {ia.getX() + 92, y - 2, ia.getWidth() - 100, 9}, pal::lcd);
            y += 13;
        }
    }
    const auto& vp = sections_[5].area;
    static const char* heads[4] = {"DETUNE", "PAN", "ENV TIME", "CUTOFF"};
    const int inner = vp.getWidth() - 40;
    for (int c = 0; c < 4; ++c) drawTextIn(g, heads[c], {vp.getX() + 34 + c * (inner / 4), vp.getY() + 36, inner / 4 - 4, 8}, pal::textMuted, juce::Justification::centred);
    for (int v = 0; v < 8; ++v) drawTextIn(g, "V" + juce::String(v + 1), {vp.getX() + 6, vp.getY() + 52 + v * 15, 26, 12}, pal::textBody);
}

}
