#include "OscPage.h"
#include "AssetBrowse.h"

namespace zyg::ui {
namespace {
const juce::Colour oscBody = mix(pal::panel, pal::acidShadow, 0.16f);
const juce::Colour oscHeader = mix(pal::raised, pal::acidShadow, 0.55f);
const juce::Colour filterBody = mix(pal::panel, pal::violetShadow, 0.30f);
const juce::Colour filterHeader = mix(pal::raised, pal::violetShadow, 0.65f);

void drawHeader(juce::Graphics& g, juce::Rectangle<int> r, bool on, juce::Colour onFill) {
    g.setColour(on ? onFill : pal::raised); g.fillRect(r);
    hLine(g, r.getX(), r.getBottom() - 1, r.getWidth(), pal::edgeDark);
    hLine(g, r.getX(), r.getY(), r.getWidth(), on ? pal::edgeLight : pal::edgeMid);
}

juce::String signedInt(double v) { return (v > 0 ? "+" : "") + juce::String(int(std::round(v))); }

// Vertical rhythm shared by every OSC-page module so header, selector rows, footer rows and the two
// knob rows line up across SUB / OSC A-C / NOISE / FILTER (as in Serum). All values are relative
// to the module height `h`; the display takes whatever is left in the middle.
constexpr int kHeadH = 21, kRowA = 24, kRowB = 42, kRowH = 15, kDispTop = 59, kKnobD = 28, kCellH = 46;
inline int footY(int h) { return h - 121; }    // phase / route row
inline int knobRow(int h, int row) { return row ? h - 50 : h - 101; }
}

// ================================================================== OscModule
OscModule::OscModule(UiContext& c, int index) : Panel(c), idx_(index) {
    const int i = index;
    enable_ = &make<Toggle>(ctx, "", Toggle::Style::led,
        [i](const Patch& p) { return p.oscillators[std::size_t(i)].enabled; },
        [i](Patch& p, bool v) { p.oscillators[std::size_t(i)].enabled = v; if (v && p.routes[std::size_t(i)].target == RouteTarget::unknown) p.routes[std::size_t(i)].target = RouteTarget::main; });

    type_ = &make<Chooser>();
    type_->setBold(true); type_->setTextColour(pal::textHi);
    type_->buildMenu = [this](juce::PopupMenu& m) {
        struct M { OscMode mode; const char* name; };
        static const M modes[] = {{OscMode::wavetable, "WAVETABLE"}, {OscMode::sample, "SAMPLE"}, {OscMode::multisample, "MULTISAMPLE"},
                                  {OscMode::granular, "GRANULAR"}, {OscMode::spectral, "SPECTRAL"}};
        for (const auto& mo : modes)
            m.addItem(mo.name, true, mode_ == mo.mode, [this, mo] {
                ctx.editNow([i = idx_, mo](Patch& p) { auto& o = p.oscillators[std::size_t(i)]; o.mode = mo.mode; });
            });
    };

    route_ = &make<Chooser>();
    route_->setCentredText(true); route_->setTextColour(pal::acidHot);
    route_->buildMenu = [this](juce::PopupMenu& m) { buildRouteMenu(ctx, m, idx_); };

    browse_ = &make<PixelButton>();
    browse_->setIcon(Icon::folder);
    browse_->onClick = [this] { if (ctx.openWavetableBrowser) ctx.openWavetableBrowser(idx_); };

    table_ = &make<Chooser>();
    table_->setArrows(true);
    table_->buildMenu = [this](juce::PopupMenu& m) {
        m.addItem("BROWSE CONTENT LIBRARY...", [this] { if (ctx.openWavetableBrowser) ctx.openWavetableBrowser(idx_); });
        m.addItem("LOAD FILE...", [this] {
            auto chooser = std::make_shared<juce::FileChooser>("Load wavetable or sample", juce::File{}, "*.wav;*.flac;*.aif;*.aiff");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) ctx.proc.setOscSampleFile(idx_, fc.getResult()); });
        });
    };
    table_->onStep = [this](int dir) {
        const auto f = neighbour(oscAssetFile(*ctx.patch, idx_), dir);
        if (f.existsAsFile()) ctx.proc.setOscSampleFile(idx_, f);
    };

    menu_ = &make<PixelButton>("M");
    menu_->setBold(true);
    menu_->onClick = [this] {
        juce::PopupMenu m;
        const auto& o = ctx.patch->oscillators[std::size_t(idx_)];
        juce::PopupMenu det, stack, pitch;
        struct D { DetuneMode m; const char* n; };
        for (auto d : {D{DetuneMode::linear, "LINEAR"}, D{DetuneMode::exponential, "EXPONENTIAL"}, D{DetuneMode::inverse, "INVERSE"},
                       D{DetuneMode::random, "RANDOM"}, D{DetuneMode::super, "SUPER"}})
            det.addItem(d.n, true, o.detuneMode == d.m, [this, d] { ctx.editNow([i = idx_, d](Patch& p) { p.oscillators[std::size_t(i)].detuneMode = d.m; }); });
        struct S { UnisonStack s; const char* n; };
        for (auto d : {S{UnisonStack::none, "NONE"}, S{UnisonStack::center12, "CENTER +12"}, S{UnisonStack::octave1, "OCTAVE 1"},
                       S{UnisonStack::octave2, "OCTAVE 2"}, S{UnisonStack::octave3, "OCTAVE 3"}, S{UnisonStack::octaveFifth1, "OCT+5TH 1"},
                       S{UnisonStack::octaveFifth2, "OCT+5TH 2"}, S{UnisonStack::octaveFifth3, "OCT+5TH 3"}})
            stack.addItem(d.n, true, o.unisonStack == d.s, [this, d] { ctx.editNow([i = idx_, d](Patch& p) { p.oscillators[std::size_t(i)].unisonStack = d.s; }); });
        struct P { OscPitchMode m; const char* n; };
        for (auto d : {P{OscPitchMode::semitones, "SEMITONES"}, P{OscPitchMode::harmonics, "HARMONICS"}, P{OscPitchMode::ratio, "RATIO"}})
            pitch.addItem(d.n, true, o.pitchMode == d.m, [this, d] { ctx.editNow([i = idx_, d](Patch& p) { p.oscillators[std::size_t(i)].pitchMode = d.m; }); });
        m.addSubMenu("DETUNE CURVE", det);
        m.addSubMenu("UNISON STACK", stack);
        m.addSubMenu("PITCH MODE", pitch);
        m.addItem("PHASE PER VOICE", true, o.perVoicePhase, [this] { ctx.editNow([i = idx_](Patch& p) { auto& q = p.oscillators[std::size_t(i)]; q.perVoicePhase = !q.perVoicePhase; }); });
        m.addItem("KEY TRACKING", true, o.pitchTrack, [this] { ctx.editNow([i = idx_](Patch& p) { auto& q = p.oscillators[std::size_t(i)]; q.pitchTrack = !q.pitchTrack; }); });
        m.addSeparator();
        m.addItem("INIT OSCILLATOR", [this] {
            ctx.editNow([i = idx_](Patch& p) {
                auto& q = p.oscillators[std::size_t(i)];
                Oscillator d; d.enabled = q.enabled; d.mode = q.mode; d.asset = q.asset; d.audio = q.audio; d.wavetableMipmaps = q.wavetableMipmaps;
                d.wavetableMipLevels = q.wavetableMipLevels; d.frameSize = q.frameSize; d.sample = q.sample; d.userSelectedAsset = q.userSelectedAsset;
                q = std::move(d);
            });
        });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_).withStandardItemHeight(13));
    };

    // pitch row
    oct_ = &make<Spinner>(ctx, "OCT", -4, 4, 1, [i](const Patch& p) { return double(p.oscillators[std::size_t(i)].octave); },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].octave = int(v); }, signedInt);
    sem_ = &make<Spinner>(ctx, "SEM", -12, 12, 1, [i](const Patch& p) { return p.oscillators[std::size_t(i)].pitch; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].pitch = v; }, signedInt);
    fin_ = &make<Spinner>(ctx, "FIN", -100, 100, 1, [i](const Patch& p) { return p.oscillators[std::size_t(i)].fine; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].fine = v; }, signedInt);
    crs_ = &make<Spinner>(ctx, "CRS", -64, 64, 0.01, [i](const Patch& p) { return p.oscillators[std::size_t(i)].semitone; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].semitone = v; },
        [](double v) { return (v > 0 ? "+" : "") + juce::String(v, 2); });

    display_ = &make<OscDisplay>(ctx, idx_);

    phase_ = &make<Spinner>(ctx, "PH", 0, 360, 1, [i](const Patch& p) { return p.oscillators[std::size_t(i)].initialPhase; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].initialPhase = v; }, fmt::degrees);
    rand_ = &make<Spinner>(ctx, "RAND", 0, 100, 1, [i](const Patch& p) { return p.oscillators[std::size_t(i)].randomPhase; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].randomPhase = v; }, [](double v) { return juce::String(int(v)); });

    detuneMode_ = &make<Chooser>();
    detuneMode_->buildMenu = [this](juce::PopupMenu& m) {
        struct D { DetuneMode m; const char* n; };
        for (auto d : {D{DetuneMode::linear, "LINEAR"}, D{DetuneMode::exponential, "EXPONENTIAL"}, D{DetuneMode::inverse, "INVERSE"},
                       D{DetuneMode::random, "RANDOM"}, D{DetuneMode::super, "SUPER"}})
            m.addItem(d.n, true, ctx.patch->oscillators[std::size_t(idx_)].detuneMode == d.m,
                      [this, d] { ctx.editNow([i = idx_, d](Patch& p) { p.oscillators[std::size_t(i)].detuneMode = d.m; }); });
    };

    // pitch / phase fields are modulation destinations too
    oct_->setTarget({ModTarget::oscParam, i, int(OscParam::octave)}); sem_->setTarget({ModTarget::oscParam, i, int(OscParam::pitch)});
    fin_->setTarget({ModTarget::oscParam, i, int(OscParam::fine)}); crs_->setTarget({ModTarget::oscParam, i, int(OscParam::coarse)});
    phase_->setTarget({ModTarget::oscParam, i, int(OscParam::initialPhase)}); rand_->setTarget({ModTarget::oscParam, i, int(OscParam::randomPhase)});

    unison_ = &make<Spinner>(ctx, "", 1, 16, 1, [i](const Patch& p) { return double(p.oscillators[std::size_t(i)].unison); },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].unison = int(v); }, [](double v) { return juce::String(int(v)); });
    unison_->setIcon(Icon::gear); unison_->setCaption("UNISON"); unison_->showArrows(true);

    // ------- wavetable set
    wtPos_ = &oscKnob(i, OscParam::tablePos, "WT POS", kKnobD, [this](double v) {
        const int frames = frameCount(ctx.patch->oscillators[std::size_t(idx_)]);
        return juce::String(frames > 1 ? int(std::round(v / 256.0 * (frames - 1))) + 1 : 1);
    });
    detune_ = &oscKnob(i, OscParam::detune, "DETUNE", kKnobD, fmt::percent01);
    blend_ = &oscKnob(i, OscParam::blend, "BLEND", kKnobD, [](double v) { return juce::String(int(v)); });
    pan_ = &oscKnob(i, OscParam::pan, "PAN", kKnobD, fmt::pan);
    level_ = &oscKnob(i, OscParam::volume, "LEVEL", kKnobD, [](double v) { return fmt::percent01(v); });
    warpKnob1_ = &oscKnob(i, OscParam::warp1, "1", kKnobD - 2, fmt::percent01);
    warpKnob2_ = &oscKnob(i, OscParam::warp2, "2", kKnobD - 2, fmt::percent01);
    warp1_ = &make<Chooser>(); warp2_ = &make<Chooser>();
    for (int s = 0; s < 2; ++s) {
        auto* w = s ? warp2_ : warp1_;
        w->setArrows(true); w->setTextColour(pal::amber); w->setCentredText(true);
        w->buildMenu = [this, s](juce::PopupMenu& m) { buildWarpMenu(ctx, m, idx_, s); };
        w->onStep = [this, s](int dir) {
            // step through the simple list order used by the menu
            static const WarpMode order[] = {WarpMode::off, WarpMode::bendPositive, WarpMode::bendNegative, WarpMode::bendBoth,
                WarpMode::asymPositive, WarpMode::asymNegative, WarpMode::asymBoth, WarpMode::pwm, WarpMode::flip, WarpMode::sync,
                WarpMode::hardClip, WarpMode::softClip, WarpMode::sineFold, WarpMode::linearFold, WarpMode::sineShaper};
            const auto cur = ctx.patch->oscillators[std::size_t(idx_)].warpDefinitions[std::size_t(s)].mode;
            int at = 0;
            for (int k = 0; k < int(std::size(order)); ++k) if (order[k] == cur) at = k;
            const auto next = order[(at + dir + int(std::size(order))) % int(std::size(order))];
            ctx.editNow([i = idx_, s, next](Patch& p) {
                auto& o = p.oscillators[std::size_t(i)];
                o.warpDefinitions[std::size_t(s)].mode = next; o.warpDefinitions[std::size_t(s)].sourceIndex = -1;
                (s ? o.warpTwo : o.warpOne).clear();
            });
        };
    }
    wtSet_ = {wtPos_};

    // ------- sample set
    auto* start = &oscKnob(i, OscParam::start, "START", kKnobD, fmt::percent);
    auto* end = &oscKnob(i, OscParam::end, "END", kKnobD, fmt::percent);
    auto* randStart = &make<Knob>(ctx, KnobSpec{"RND START", 0, 100, 0, false, false, 1.0, 0, fmt::percent},
        [i](const Patch& p) { return p.oscillators[std::size_t(i)].randomStart; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].randomStart = v; }, kKnobD);
    auto* velTrack = &make<Knob>(ctx, KnobSpec{"VEL TRK", 0, 100, 0, false, false, 1.0, 0, fmt::percent},
        [i](const Patch& p) { return p.oscillators[std::size_t(i)].velTrack; },
        [i](Patch& p, double v) { p.oscillators[std::size_t(i)].velTrack = v; }, kKnobD);
    loop_ = &make<Toggle>(ctx, "LOOP", Toggle::Style::check, [i](const Patch& p) { return p.oscillators[std::size_t(i)].looping; },
        [i](Patch& p, bool v) { p.oscillators[std::size_t(i)].looping = v; });
    reverse_ = &make<Toggle>(ctx, "REVERSE", Toggle::Style::check, [i](const Patch& p) { return p.oscillators[std::size_t(i)].reverse; },
        [i](Patch& p, bool v) { p.oscillators[std::size_t(i)].reverse = v; });
    sampleSet_ = {start, end, randStart};
    multiSet_ = {velTrack, &oscKnob(i, OscParam::envAttack, "ATTACK", kKnobD, fmt::seconds), &oscKnob(i, OscParam::envDecay, "DECAY", kKnobD, fmt::seconds),
                 &oscKnob(i, OscParam::envRelease, "RELEASE", kKnobD, fmt::seconds)};
    // ------- granular set
    granSet_ = {&oscKnob(i, OscParam::position, "POS", kKnobD, fmt::percent), &oscKnob(i, OscParam::scanRate, "SCAN", kKnobD, fmt::percent),
                &oscKnob(i, OscParam::density, "DENS", kKnobD, [](double v) { return formatNumber(v, 1) + "/S"; }),
                &oscKnob(i, OscParam::grainLength, "LENGTH", kKnobD, fmt::seconds),
                &oscKnob(i, OscParam::randomOffset, "OFFSET", kKnobD, fmt::percent), &oscKnob(i, OscParam::randomPitch, "PITCH", kKnobD, [](double v) { return formatNumber(v, 1) + " ST"; }),
                &oscKnob(i, OscParam::randomGain, "R GAIN", kKnobD, fmt::percent), &oscKnob(i, OscParam::randomPan, "R PAN", kKnobD, fmt::percent)};
    // ------- spectral set
    specSet_ = {&oscKnob(i, OscParam::position, "POS", kKnobD, fmt::percent), &oscKnob(i, OscParam::timbreShift, "TIMBRE", kKnobD, fmt::signedPct),
                &oscKnob(i, OscParam::freqLo, "FREQ LO", kKnobD, fmt::hz), &oscKnob(i, OscParam::freqHi, "FREQ HI", kKnobD, fmt::hz),
                &oscKnob(i, OscParam::specShift, "SHIFT", kKnobD, fmt::signedPct), &oscKnob(i, OscParam::specWet, "MIX", kKnobD, fmt::percent)};
}

void OscModule::refresh(const Patch& p) {
    const auto& o = p.oscillators[std::size_t(idx_)];
    const auto m = o.mode == OscMode::unknown ? OscMode::wavetable : o.mode;
    const bool modeChanged = m != mode_;
    mode_ = m; enabled_ = o.enabled;
    Panel::refresh(p);
    const auto& r = p.routes[std::size_t(idx_)];
    route_->setText(routeLabel(r));
    static const char* modeNames[] = {"WAVETABLE", "SAMPLE", "MULTISAMPLE", "GRANULAR", "SPECTRAL", "SUB", "NOISE", "WAVETABLE"};
    type_->setText(modeNames[std::size_t(m)]);
    const auto name = prettyAssetName(o.asset);
    table_->setText(name.isNotEmpty() ? name
                    : m != OscMode::wavetable ? juce::String("- NO SAMPLE -")
                    : o.tableName.empty() ? juce::String("DEFAULT SINE") : juce::String(o.tableName).toUpperCase() + " (EMBEDDED)");
    table_->setDimmed(!o.enabled);
    detuneMode_->setText([&] {
        switch (o.detuneMode) { case DetuneMode::linear: return "LINEAR"; case DetuneMode::exponential: return "EXPONENTIAL";
            case DetuneMode::inverse: return "INVERSE"; case DetuneMode::random: return "RANDOM"; default: return "SUPER"; }
    }());
    warp1_->setText(warpName(o.warpDefinitions[0]));
    warp2_->setText(warpName(o.warpDefinitions[1]));
    const bool warpable = m == OscMode::wavetable;
    warp1_->setDimmed(!o.enabled); warp2_->setDimmed(!o.enabled);
    // dim everything that is a Knob/Spinner when disabled
    auto dimAll = [&](bool d) {
        for (auto& c : owned_) {
            if (auto* k = dynamic_cast<Knob*>(c.get())) k->setDimmed(d);
            else if (auto* s = dynamic_cast<Spinner*>(c.get())) s->setDimmed(d);
        }
    };
    dimAll(!o.enabled);
    if (modeChanged) resized();
    // visibility per mode
    const bool wt = m == OscMode::wavetable;
    for (auto* k : {wtPos_, detune_, blend_, warpKnob1_, warpKnob2_}) k->setVisible(wt);
    warp1_->setVisible(warpable); warp2_->setVisible(warpable);
    const bool sample = m == OscMode::sample, multi = m == OscMode::multisample, gran = m == OscMode::granular, spec = m == OscMode::spectral;
    for (auto* k : sampleSet_) k->setVisible(sample);
    for (auto* k : multiSet_) k->setVisible(multi);
    for (std::size_t k = 0; k < granSet_.size(); ++k) granSet_[k]->setVisible(gran && k < 6);
    for (auto* k : specSet_) k->setVisible(spec);
    loop_->setVisible(sample); reverse_->setVisible(sample);
    // detune/blend stay relevant to sample-like modes but sit in different cells
    detune_->setVisible(true); blend_->setVisible(wt || sample || multi);
    if (spec || gran) blend_->setVisible(false);
    phase_->setVisible(wt || m == OscMode::spectral); rand_->setVisible(wt || m == OscMode::spectral);
    detuneMode_->setVisible(true);
    repaint();
}

void OscModule::layoutSet(const std::vector<juce::Component*>& slots) {
    for (std::size_t s = 0; s < slots.size(); ++s)
        if (slots[s]) slots[s]->setBounds(slot(int(s) % 4, int(s) / 4));
}
juce::Rectangle<int> OscModule::slot(int col, int row) const {
    const int cw = std::max(40, (getWidth() - 48) / 4);
    return {2 + col * cw, knobRow(getHeight(), row), cw, kCellH};
}

void OscModule::resized() {
    const int w = getWidth(), h = getHeight();
    const int fy = footY(h);
    enable_->setBounds(5, 6, 9, 9);
    type_->setBounds(58, 3, 92, kRowH);
    menu_->setBounds(154, 3, 18, kRowH);
    route_->setBounds(w - 52, 3, 48, kRowH);
    browse_->setBounds(2, kRowA, 16, 16);
    table_->setBounds(20, kRowA, w - 22, 16);
    const int q = (w - 4) / 4;
    oct_->setBounds(2, kRowB, q - 2, kRowH); sem_->setBounds(2 + q, kRowB, q - 2, kRowH);
    fin_->setBounds(2 + 2 * q, kRowB, q - 2, kRowH); crs_->setBounds(2 + 3 * q, kRowB, w - 4 - 3 * q, kRowH);
    display_->setBounds(2, kDispTop, w - 4, std::max(20, fy - 2 - kDispTop));
    phase_->setBounds(2, fy, 60, kRowH); rand_->setBounds(64, fy, 66, kRowH);
    detuneMode_->setBounds(w - 100, fy, 98, kRowH);
    pan_->setBounds(w - 46, knobRow(h, 0), 44, kCellH);
    level_->setBounds(w - 46, knobRow(h, 1), 44, kCellH);
    auto place = [](juce::Component* c, juce::Rectangle<int> r) { if (c) c->setBounds(r); };
    auto cellSpin = [&](int col, int row) { auto r = slot(col, row); return juce::Rectangle<int>(r.getX() + (r.getWidth() - 44) / 2, r.getY() + 3, 44, 24); };
    if (mode_ == OscMode::wavetable) {
        place(wtPos_, slot(0, 0)); place(unison_, cellSpin(1, 0)); place(detune_, slot(2, 0)); place(blend_, slot(3, 0));
        place(warpKnob1_, slot(0, 1)); place(warpKnob2_, slot(3, 1));
        const auto a = slot(1, 1);
        const int ww = slot(2, 1).getRight() - a.getX() - 4;
        warp1_->setBounds(a.getX() + 2, a.getY() + 1, ww, 14); warp2_->setBounds(a.getX() + 2, a.getY() + 16, ww, 14);
    } else if (mode_ == OscMode::sample) {
        place(sampleSet_[0], slot(0, 0)); place(sampleSet_[1], slot(1, 0)); place(unison_, cellSpin(2, 0)); place(detune_, slot(3, 0));
        place(blend_, slot(0, 1)); place(sampleSet_[2], slot(1, 1));
        const auto a = slot(2, 1);
        loop_->setBounds(a.getX() + 4, a.getY() + 6, 60, 12); reverse_->setBounds(a.getX() + 4, a.getY() + 22, 70, 12);
    } else if (mode_ == OscMode::multisample) {
        place(unison_, cellSpin(0, 0)); place(detune_, slot(1, 0)); place(blend_, slot(2, 0)); place(multiSet_[0], slot(3, 0));
        for (int k = 1; k < 4; ++k) place(multiSet_[std::size_t(k)], slot(k - 1, 1));
    } else if (mode_ == OscMode::granular) {
        for (int k = 0; k < 4; ++k) place(granSet_[std::size_t(k)], slot(k, 0));
        place(granSet_[4], slot(0, 1)); place(granSet_[5], slot(1, 1));
        place(unison_, cellSpin(2, 1)); place(detune_, slot(3, 1));
        granSet_[6]->setVisible(false); granSet_[7]->setVisible(false);
    } else {
        for (int k = 0; k < 4; ++k) place(specSet_[std::size_t(k)], slot(k, 0));
        place(specSet_[4], slot(0, 1)); place(specSet_[5], slot(1, 1)); place(unison_, cellSpin(2, 1)); place(detune_, slot(3, 1));
    }
}

void OscModule::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, enabled_ ? oscBody : mix(pal::panel, pal::chassis, 0.5f));
    drawHeader(g, {1, 1, r.getWidth() - 2, 20}, enabled_, oscHeader);
    drawTextIn(g, "OSC " + oscName(idx_), {19, 1, 38, 20}, enabled_ ? pal::textHi : pal::textMuted, juce::Justification::centredLeft, 1, true);
    const int h = r.getHeight(), cw = std::max(40, (r.getWidth() - 48) / 4);
    if (mode_ == OscMode::wavetable) drawTextIn(g, "WARP", {2 + cw + 2, knobRow(h, 1) + kKnobD + 2, 2 * cw - 4, fontHeight}, pal::textBody, juce::Justification::centred);
    vLine(g, r.getWidth() - 48, knobRow(h, 0) - 2, h - knobRow(h, 0) - 4, pal::edgeMid);
}

// ================================================================== SubModule
SubModule::SubModule(UiContext& c) : Panel(c) {
    enable_ = &make<Toggle>(ctx, "", Toggle::Style::led, [](const Patch& p) { return p.oscillators[4].enabled; },
        [](Patch& p, bool v) { auto& o = p.oscillators[4]; o.enabled = v; o.mode = OscMode::sub;
            if (v && p.routes[4].target == RouteTarget::unknown) p.routes[4].target = RouteTarget::main; });
    oct_ = &make<Spinner>(ctx, "OCT", -4, 4, 1, [](const Patch& p) { return double(p.oscillators[4].octave); },
        [](Patch& p, double v) { p.oscillators[4].octave = int(v); }, signedInt);
    crs_ = &make<Spinner>(ctx, "CRS", -64, 64, 1, [](const Patch& p) { return p.oscillators[4].semitone; },
        [](Patch& p, double v) { p.oscillators[4].semitone = v; }, signedInt);
    oct_->setTarget({ModTarget::oscParam, 4, int(OscParam::octave)}); crs_->setTarget({ModTarget::oscParam, 4, int(OscParam::coarse)});
    route_ = &make<Chooser>(); route_->setCentredText(true); route_->setTextColour(pal::acidHot);
    route_->buildMenu = [this](juce::PopupMenu& m) { buildRouteMenu(ctx, m, 4); };
    display_ = &make<OscDisplay>(ctx, 4);
    struct S { SubShape s; WaveIcon w; };
    static const S shapes[6] = {{SubShape::unknown, WaveIcon::sine}, {SubShape::roundedRectangle, WaveIcon::rounded},
        {SubShape::triangle, WaveIcon::triangle}, {SubShape::saw, WaveIcon::saw}, {SubShape::square, WaveIcon::square},
        {SubShape::pulse, WaveIcon::pulse}};
    for (int i = 0; i < 6; ++i) {
        auto* b = &make<PixelButton>();
        b->setWave(shapes[i].w);
        b->onClick = [this, i] { ctx.editNow([i](Patch& p) { auto& o = p.oscillators[4]; o.mode = OscMode::sub; o.subShape = shapes[i].s; }); };
        shapes_[std::size_t(i)] = b;
    }
    pan_ = &make<Knob>(ctx, KnobSpec{"PAN", -1, 1, 0, true, false, 1.0, 0, fmt::pan},
        [](const Patch& p) { return p.oscillators[4].pan; }, [](Patch& p, double v) { p.oscillators[4].pan = v; }, kKnobD);
    pan_->setTarget({ModTarget::oscParam, 4, int(OscParam::pan)});
    level_ = &make<Knob>(ctx, KnobSpec{"LEVEL", 0, 1, 0.75, false, false, 1.0, 0, fmt::percent01},
        [](const Patch& p) { return p.oscillators[4].volume; }, [](Patch& p, double v) { p.oscillators[4].volume = v; }, kKnobD);
    level_->setTarget({ModTarget::oscParam, 4, int(OscParam::volume)});
}

void SubModule::refresh(const Patch& p) {
    const auto& o = p.oscillators[4];
    enabled_ = o.enabled;
    shape_ = o.subShape;
    Panel::refresh(p);
    route_->setText(routeLabel(p.routes[4]));
    for (auto* k : {pan_, level_}) k->setDimmed(!enabled_);
    oct_->setDimmed(!enabled_); crs_->setDimmed(!enabled_);
    static const SubShape order[6] = {SubShape::unknown, SubShape::roundedRectangle, SubShape::triangle, SubShape::saw, SubShape::square, SubShape::pulse};
    for (int i = 0; i < 6; ++i) shapes_[std::size_t(i)]->setToggled(o.subShape == order[i]);
    repaint();
}

void SubModule::resized() {
    const int w = getWidth(), h = getHeight();
    const int fy = footY(h);
    enable_->setBounds(5, 6, 9, 9);
    oct_->setBounds(2, kRowA, w - 4, kRowH); crs_->setBounds(2, kRowB, w - 4, kRowH);
    display_->setVisible(false);
    // 2 x 3 shape grid fills the display band, like Serum's SUB column
    const int gx = 3, gw = w - 6, gy = kDispTop, gh = std::max(30, fy - 3 - gy);
    for (int i = 0; i < 6; ++i) {
        const int c = i % 2, rI = i / 2;
        const int x0 = gx + c * gw / 2, x1 = gx + (c + 1) * gw / 2;
        const int y0 = gy + rI * gh / 3, y1 = gy + (rI + 1) * gh / 3;
        shapes_[std::size_t(i)]->setBounds(x0, y0, x1 - x0 - 1, y1 - y0 - 1);
    }
    route_->setBounds(2, fy, w - 4, kRowH);
    pan_->setBounds(0, knobRow(h, 0), w, kCellH);
    level_->setBounds(0, knobRow(h, 1), w, kCellH);
}

void SubModule::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, enabled_ ? oscBody : mix(pal::panel, pal::chassis, 0.5f));
    drawHeader(g, {1, 1, r.getWidth() - 2, 20}, enabled_, oscHeader);
    drawTextIn(g, "SUB", {19, 1, 36, 20}, enabled_ ? pal::textHi : pal::textMuted, juce::Justification::centredLeft, 1, true);
    // sunken tray behind the shape grid
    wellBox(g, {2, kDispTop - 1, r.getWidth() - 4, std::max(30, footY(r.getHeight()) - 3 - kDispTop) + 2}, enabled_ ? pal::acidWell : pal::sunken);
}

// ================================================================= NoiseModule
NoiseModule::NoiseModule(UiContext& c) : Panel(c) {
    enable_ = &make<Toggle>(ctx, "", Toggle::Style::led, [](const Patch& p) { return p.oscillators[3].enabled; },
        [](Patch& p, bool v) { auto& o = p.oscillators[3]; o.enabled = v; o.mode = OscMode::noise;
            if (v && p.routes[3].target == RouteTarget::unknown) p.routes[3].target = RouteTarget::main; });
    type_ = &make<Chooser>(); type_->setArrows(true); type_->setTextColour(pal::textHi);
    auto stepType = [this](int dir) {
        ctx.editNow([dir](Patch& p) {
            auto& o = p.oscillators[3];
            const int n = (int(o.noiseType) + dir + 4) % 4;
            o.mode = OscMode::noise; o.noiseType = NoiseType(n); o.noiseTypeExplicit = true; o.asset.clear(); o.audio.clear(); o.userSelectedAsset = false;
        });
    };
    type_->onStep = stepType;
    type_->buildMenu = [this](juce::PopupMenu& m) {
        struct T { NoiseType t; const char* n; };
        for (auto t : {T{NoiseType::white, "WHITE"}, T{NoiseType::pink, "PINK"}, T{NoiseType::brown, "BROWN"}, T{NoiseType::geiger, "GEIGER"}})
            m.addItem(t.n, true, ctx.patch->oscillators[3].asset.empty() && ctx.patch->oscillators[3].noiseType == t.t, [this, t] {
                ctx.editNow([t](Patch& p) { auto& o = p.oscillators[3]; o.mode = OscMode::noise; o.noiseType = t.t; o.noiseTypeExplicit = true;
                    o.asset.clear(); o.audio.clear(); o.userSelectedAsset = false; });
            });
        m.addSeparator();
        m.addItem("LOAD NOISE SAMPLE...", [this] {
            auto chooser = std::make_shared<juce::FileChooser>("Load noise sample", juce::File{}, "*.wav;*.flac");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this, chooser](const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) ctx.proc.setNoiseSampleFile(fc.getResult()); });
        });
    };
    display_ = &make<OscDisplay>(ctx, 3);
    route_ = &make<Chooser>(); route_->setCentredText(true); route_->setTextColour(pal::acidHot);
    route_->buildMenu = [this](juce::PopupMenu& m) { buildRouteMenu(ctx, m, 3); };
    colour_ = &make<Knob>(ctx, KnobSpec{"COLOR", 0, 1, 0.5, true, false, 1.0, 0, fmt::percent01},
        [](const Patch& p) { return p.oscillators[3].noiseColor; }, [](Patch& p, double v) { p.oscillators[3].noiseColor = v; }, kKnobD);
    colour_->setTarget({ModTarget::oscParam, 3, int(OscParam::color)});
    pan_ = &make<Knob>(ctx, KnobSpec{"PAN", -1, 1, 0, true, false, 1.0, 0, fmt::pan},
        [](const Patch& p) { return p.oscillators[3].pan; }, [](Patch& p, double v) { p.oscillators[3].pan = v; }, kKnobD);
    pan_->setTarget({ModTarget::oscParam, 3, int(OscParam::pan)});
    level_ = &make<Knob>(ctx, KnobSpec{"LEVEL", 0, 1, 0.75, false, false, 1.0, 0, fmt::percent01},
        [](const Patch& p) { return p.oscillators[3].volume; }, [](Patch& p, double v) { p.oscillators[3].volume = v; }, kKnobD);
    level_->setTarget({ModTarget::oscParam, 3, int(OscParam::volume)});
    oneShot_ = &make<Toggle>(ctx, "1-SHOT", Toggle::Style::check, [](const Patch& p) { return p.oscillators[3].oneShot; },
        [](Patch& p, bool v) { p.oscillators[3].oneShot = v; });
}

void NoiseModule::refresh(const Patch& p) {
    const auto& o = p.oscillators[3];
    enabled_ = o.enabled;
    Panel::refresh(p);
    static const char* names[] = {"WHITE", "PINK", "BROWN", "GEIGER"};
    type_->setText(o.asset.empty() ? names[int(o.noiseType) & 3] : prettyAssetName(o.asset));
    type_->setDimmed(!enabled_);
    route_->setText(routeLabel(p.routes[3]));
    for (auto* k : {pan_, level_, colour_}) k->setDimmed(!enabled_);
    oneShot_->setVisible(!o.asset.empty());
    repaint();
}

void NoiseModule::resized() {
    const int w = getWidth(), h = getHeight();
    const int fy = footY(h);
    enable_->setBounds(5, 6, 9, 9);
    type_->setBounds(2, kRowA, w - 4, 16);
    display_->setBounds(2, kRowB, w - 4, std::max(20, fy - 2 - kRowB));
    route_->setBounds(2, fy, w - 4, kRowH);
    colour_->setBounds(0, knobRow(h, 0), w / 2, kCellH); pan_->setBounds(w / 2, knobRow(h, 0), w / 2, kCellH);
    level_->setBounds(0, knobRow(h, 1), w / 2, kCellH);
    oneShot_->setBounds(w / 2 + 2, knobRow(h, 1) + 12, w / 2 - 2, 12);
}

void NoiseModule::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, enabled_ ? oscBody : mix(pal::panel, pal::chassis, 0.5f));
    drawHeader(g, {1, 1, r.getWidth() - 2, 20}, enabled_, oscHeader);
    drawTextIn(g, "NOISE", {19, 1, 50, 20}, enabled_ ? pal::textHi : pal::textMuted, juce::Justification::centredLeft, 1, true);
}

// ================================================================ FilterModule
FilterModule::FilterModule(UiContext& c) : Panel(c) {
    for (int f = 0; f < 2; ++f) {
        enable_[f] = &make<Toggle>(ctx, "", Toggle::Style::led, [f](const Patch& p) { return p.filters[std::size_t(f)].enabled; },
            [f](Patch& p, bool v) { p.filters[std::size_t(f)].enabled = v; });
        enable_[f]->setOnColour(pal::violetHot);
        tab_[f] = &make<TabButton>();
        tab_[f]->setText("FILTER " + juce::String(f + 1)); tab_[f]->setLedShown(false); tab_[f]->setAccent(pal::violetHot);
        tab_[f]->onClick = [this, f] { selectFilter(f); };
    }
    type_ = &make<Chooser>(); type_->setArrows(true); type_->setTextColour(pal::textHi);
    type_->buildMenu = [this](juce::PopupMenu& m) {
        juce::PopupMenu group; juce::String groupName;
        auto flush = [&] { if (groupName.isNotEmpty()) m.addSubMenu(groupName, group); group = juce::PopupMenu(); };
        const auto& cur = ctx.patch->filters[std::size_t(ctx.selectedFilter)];
        for (const auto& choice : filterChoices()) {
            if (!choice.serumId) { flush(); groupName = choice.name; continue; }
            const bool ticked = cur.type == choice.serumId;
            group.addItem(choice.name, true, ticked, [this, choice] {
                ctx.editNow([f = ctx.selectedFilter, choice](Patch& p) {
                    auto& fl = p.filters[std::size_t(f)];
                    fl.type = choice.serumId; fl.response = choice.response; fl.variant = choice.variant; fl.enabled = true;
                });
            });
        }
        flush();
    };
    type_->onStep = [this](int dir) {
        const auto& list = filterChoices();
        const auto& cur = ctx.patch->filters[std::size_t(ctx.selectedFilter)];
        int at = 0;
        for (int i = 0; i < int(list.size()); ++i) if (list[std::size_t(i)].serumId && cur.type == list[std::size_t(i)].serumId) at = i;
        int next = at;
        for (int k = 1; k <= int(list.size()); ++k) {
            const int j = ((at + dir * k) % int(list.size()) + int(list.size())) % int(list.size());
            if (list[std::size_t(j)].serumId) { next = j; break; }
        }
        const auto choice = list[std::size_t(next)];
        ctx.editNow([f = ctx.selectedFilter, choice](Patch& p) {
            auto& fl = p.filters[std::size_t(f)]; fl.type = choice.serumId; fl.response = choice.response; fl.variant = choice.variant; });
    };
    display_ = &make<FilterDisplay>(ctx);

    static const char* srcLabel[5] = {"S", "A", "B", "C", "N"};
    static const int srcRoute[5] = {4, 0, 1, 2, 3};
    for (int i = 0; i < 5; ++i) {
        auto* b = &make<PixelButton>(srcLabel[i]);
        b->setBold(true); b->setAccent(pal::violetHot);
        b->onClick = [this, i] {
            ctx.editNow([f = ctx.selectedFilter, r = srcRoute[i]](Patch& p) {
                auto& rt = p.routes[std::size_t(r)];
                const bool toThis = rt.target == RouteTarget::filter && (f == 0 ? rt.filterBalance < 100.0 : rt.filterBalance > -100.0);
                if (!toThis) {
                    if (rt.target != RouteTarget::filter) { rt.target = RouteTarget::filter; rt.filterBalance = f == 0 ? -100.0 : 100.0; }
                    else rt.filterBalance = 0.0;
                    p.filters[std::size_t(f)].enabled = true;
                } else if (std::abs(rt.filterBalance) < 1.0) rt.filterBalance = f == 0 ? 100.0 : -100.0;
                else rt.target = RouteTarget::main;
            });
        };
        sources_[std::size_t(i)] = b;
    }
    auto sel = [this](FilterParam p, const juce::String& label, std::function<juce::String(double)> format, bool bipolar = false) {
        const auto rg = filterParamRange(p);
        auto* k = &make<Knob>(ctx, KnobSpec{label, rg.lo, rg.hi, getFilter(Filter{}, p), bipolar, false, 1.0, 0, std::move(format)},
            [this, p](const Patch& pt) { return getFilter(pt.filters[std::size_t(ctx.selectedFilter)], p); },
            [this, p](Patch& pt, double v) { setFilter(pt.filters[std::size_t(ctx.selectedFilter)], p, v); }, kKnobD);
        knobs_.push_back(k);
        return k;
    };
    sel(FilterParam::freq, "CUTOFF", fmt::cutoffHz);
    sel(FilterParam::reso, "RES", [](double v) { return formatNumber(v, 0) + "%"; });
    sel(FilterParam::drive, "DRIVE", [](double v) { return formatNumber(v, 0) + "%"; });
    sel(FilterParam::var, "VAR", [](double v) { return formatNumber(v, 0) + "%"; });
    sel(FilterParam::wet, "MIX", [](double v) { return formatNumber(v, 0) + "%"; });
    sel(FilterParam::stereo, "STEREO", [](double v) { return formatNumber(v - 50.0, 0); }, false);
    level_ = &make<Fader>(ctx, [this](const Patch& p) { return p.filters[std::size_t(ctx.selectedFilter)].level * 2.0 > 1.0 ? 1.0 : p.filters[std::size_t(ctx.selectedFilter)].level * 2.0; },
        [this](Patch& p, double v) { p.filters[std::size_t(ctx.selectedFilter)].level = v * 0.5; }, 0.5);
    level_->setLabelsShown(false);
    selectFilter(0);
}

void FilterModule::selectFilter(int f) {
    ctx.selectedFilter = f;
    static const FilterParam params[6] = {FilterParam::freq, FilterParam::reso, FilterParam::drive, FilterParam::var, FilterParam::wet, FilterParam::stereo};
    for (int i = 0; i < 6; ++i) knobs_[std::size_t(i)]->setTarget({ModTarget::filterParam, f, int(params[i])});
    level_->setTarget({ModTarget::filterParam, f, int(FilterParam::level)});
    for (int i = 0; i < 2; ++i) tab_[i]->setSelected(i == f);
    if (ctx.patch) refresh(*ctx.patch);
}

void FilterModule::refresh(const Patch& p) {
    const int f = ctx.selectedFilter;
    const auto& fl = p.filters[std::size_t(f)];
    for (int i = 0; i < 2; ++i) { enabled_[i] = p.filters[std::size_t(i)].enabled; tab_[i]->setSelected(i == f); }
    Panel::refresh(p);
    type_->setText(filterDisplayName(fl));
    type_->setDimmed(!fl.enabled);
    for (auto* k : knobs_) k->setDimmed(!fl.enabled);
    level_->setDimmed(!fl.enabled);
    static const int srcRoute[5] = {4, 0, 1, 2, 3};
    for (int i = 0; i < 5; ++i) {
        const auto& rt = p.routes[std::size_t(srcRoute[i])];
        const bool on = rt.target == RouteTarget::filter && (f == 0 ? rt.filterBalance < 100.0 : rt.filterBalance > -100.0);
        sources_[std::size_t(i)]->setToggled(on);
    }
    // knobs 4/5 tinted by variant usefulness could go here
    repaint();
}

void FilterModule::resized() {
    const int w = getWidth(), h = getHeight();
    const int fy = footY(h);
    enable_[0]->setBounds(4, 6, 9, 9); tab_[0]->setBounds(15, 1, w / 2 - 17, 20);
    enable_[1]->setBounds(w / 2 + 4, 6, 9, 9); tab_[1]->setBounds(w / 2 + 15, 1, w - w / 2 - 17, 20);
    type_->setBounds(2, kRowA, w - 4, 16);
    display_->setBounds(2, kRowB, w - 4, std::max(20, fy - 2 - kRowB));
    for (int i = 0; i < 5; ++i) sources_[std::size_t(i)]->setBounds(2 + i * 25, fy, 23, kRowH);
    const int cw = std::max(40, (w - 40) / 3);
    for (int i = 0; i < 6; ++i) knobs_[std::size_t(i)]->setBounds(2 + (i % 3) * cw, knobRow(h, i / 3), cw, kCellH);
    level_->setBounds(w - 34, knobRow(h, 0), 32, h - knobRow(h, 0) - 14);
}

void FilterModule::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, filterBody);
    const int f = ctx.selectedFilter;
    drawHeader(g, {1, 1, r.getWidth() - 2, 20}, enabled_[f], filterHeader);
    drawTextIn(g, "LEVEL", {r.getWidth() - 36, knobRow(r.getHeight(), 1) + kKnobD + 2, 36, fontHeight}, pal::textBody, juce::Justification::centred);
}

// ==================================================================== OscPage
OscPage::OscPage(UiContext& c) : Panel(c) {
    sub_ = &make<SubModule>(ctx);
    for (int i = 0; i < 3; ++i) osc_[i] = &make<OscModule>(ctx, i);
    noise_ = &make<NoiseModule>(ctx);
    filter_ = &make<FilterModule>(ctx);
}

void OscPage::resized() {
    const int w = getWidth(), h = getHeight();
    const int extra = std::max(0, w - 1000);
    // Serum-like column shares: SUB ~6.5 %, OSC ~22 % each, NOISE ~8 %, FILTER takes the rest (~17 %).
    const int subW = 64 + int(extra * 0.07), oscW = 222 + int(extra * 0.23), noiseW = 86 + int(extra * 0.05);
    sub_->setBounds(3, 0, subW, h);
    int x = 3 + subW + 2;
    for (int i = 0; i < 3; ++i) { osc_[i]->setBounds(x, 0, oscW, h); x += oscW + 2; }
    noise_->setBounds(x, 0, noiseW, h); x += noiseW + 2;
    filter_->setBounds(x, 0, w - x - 3, h);
}

}
