#include "KeyboardDock.h"
#include "ModCatalog.h"
#include <cmath>

namespace zyg::ui {
namespace {
constexpr int keyH = 44, blackW = 9, blackH = 27;
bool isBlack(int note) { const int n = note % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
int whiteIndex(int note) {   // white keys between lowNote and note
    int c = 0;
    for (int n = PianoKeys::lowNote; n < note; ++n) if (!isBlack(n)) ++c;
    return c;
}
struct Scale { const char* name; std::vector<int> steps; };
const std::vector<Scale>& scales() {
    static const std::vector<Scale> s = {
        {"OFF", {}}, {"CHROMATIC", {0,1,2,3,4,5,6,7,8,9,10,11}}, {"MAJOR", {0,2,4,5,7,9,11}}, {"MINOR", {0,2,3,5,7,8,10}},
        {"HARMONIC MINOR", {0,2,3,5,7,8,11}}, {"MELODIC MINOR", {0,2,3,5,7,9,11}}, {"DORIAN", {0,2,3,5,7,9,10}},
        {"PHRYGIAN", {0,1,3,5,7,8,10}}, {"LYDIAN", {0,2,4,6,7,9,11}}, {"MIXOLYDIAN", {0,2,4,5,7,9,10}},
        {"LOCRIAN", {0,1,3,5,6,8,10}}, {"MAJOR PENTATONIC", {0,2,4,7,9}}, {"MINOR PENTATONIC", {0,3,5,7,10}},
        {"BLUES", {0,3,5,6,7,10}}, {"WHOLE TONE", {0,2,4,6,8,10}}, {"DIMINISHED", {0,2,3,5,6,8,9,11}}};
    return s;
}
int maskOf(const Scale& s) { int m = 0; for (int i : s.steps) m |= 1 << i; return m; }
const char* keyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
}

// ================================================================ PianoKeys
void PianoKeys::keyRect(int note, juce::Rectangle<int>& r, bool& black) const {
    black = isBlack(note);
    const int keyW = keyW_;
    if (!black) r = {whiteIndex(note) * keyW, 0, keyW, keyH};
    else r = {whiteIndex(note) * keyW - keyW * 3 / 10, 0, keyW * 3 / 5, blackH};
}

int PianoKeys::noteAt(juce::Point<int> p, float* velocity) const {
    // black keys first (they sit on top)
    for (int n = lowNote; n < highNote; ++n) {
        juce::Rectangle<int> r; bool black;
        keyRect(n, r, black);
        if (black && r.contains(p)) { if (velocity) *velocity = juce::jmap(float(p.y) / float(blackH), 0.35f, 1.0f); return n; }
    }
    for (int n = lowNote; n < highNote; ++n) {
        juce::Rectangle<int> r; bool black;
        keyRect(n, r, black);
        if (!black && r.contains(p)) { if (velocity) *velocity = juce::jmap(float(p.y) / float(keyH), 0.35f, 1.0f); return n; }
    }
    return -1;
}

void PianoKeys::refreshHeld() {
    std::uint64_t lo = 0, hi = 0;
    for (int n = 0; n < 128; ++n) if (ctx_.proc.isNoteHeld(n)) (n < 64 ? lo : hi) |= std::uint64_t(1) << (n & 63);
    bool dirty = lo != held_[0] || hi != held_[1];
    held_[0] = lo; held_[1] = hi;
    for (int n = lowNote; n < highNote; ++n) {       // keys light instantly and fade out after release
        const bool on = (((n < 64 ? lo : hi) >> (n & 63)) & 1u) || n == down_;
        float& g = glow_[std::size_t(n)];
        const float target = on ? 1.0f : 0.0f;
        const float next = on ? 1.0f : std::max(0.0f, g - 0.09f);
        if (next != g) { g = next; dirty = true; }
        (void)target;
    }
    if (dirty) repaint();
}

void PianoKeys::mouseDown(const juce::MouseEvent& e) {
    float vel = 0.8f;
    const int n = noteAt(e.getPosition(), &vel);
    if (n < 0) return;
    down_ = n; ctx_.proc.uiNote(n, true, vel); repaint();
}
void PianoKeys::mouseDrag(const juce::MouseEvent& e) {
    float vel = 0.8f;
    const int n = noteAt(e.getPosition(), &vel);
    if (n == down_ || n < 0) return;
    if (down_ >= 0) ctx_.proc.uiNote(down_, false);
    down_ = n; ctx_.proc.uiNote(n, true, vel); repaint();
}
void PianoKeys::mouseUp(const juce::MouseEvent&) {
    if (down_ >= 0) ctx_.proc.uiNote(down_, false);
    down_ = -1; repaint();
}

void PianoKeys::paint(juce::Graphics& g) {
    const auto& patch = *ctx_.patch;
    const int scaleMask = patch.globals.pitchQuantizerMask, key = patch.globals.pitchQuantizerKey;
    auto held = [&](int n) { return ((n < 64 ? held_[0] : held_[1]) >> (n & 63)) & 1u; };
    auto inScale = [&](int n) { return scaleMask == 0 || ((scaleMask >> (((n - key) % 12 + 12) % 12)) & 1); };
    for (int n = lowNote; n < highNote; ++n) {
        juce::Rectangle<int> r; bool black; keyRect(n, r, black);
        if (black) continue;
        const bool on = held(n) || n == down_;
        const float gl = glow_[std::size_t(n)];
        g.setColour(mix(inScale(n) ? juce::Colour(0xffe6efe4) : juce::Colour(0xff8f9a90), pal::acid, std::max(gl, on ? 1.0f : 0.0f)));
        g.fillRect(r.reduced(1, 0).withTrimmedBottom(1));
        vLine(g, r.getRight() - 1, 0, keyH, pal::edgeDark);
        hLine(g, r.getX(), keyH - 2, r.getWidth(), on ? pal::acidDim : juce::Colour(0xffb9c4b8));
        hLine(g, r.getX(), keyH - 1, r.getWidth(), pal::edgeDark);
        if (n % 12 == 0) drawText(g, "C" + juce::String(n / 12 - 2), r.getX() + 2, keyH - 9, on ? pal::acidShadow : pal::textMuted);
    }
    for (int n = lowNote; n < highNote; ++n) {
        juce::Rectangle<int> r; bool black; keyRect(n, r, black);
        if (!black) continue;
        const bool on = held(n) || n == down_;
        g.setColour(pal::edgeDark); g.fillRect(r.expanded(1, 0).withHeight(blackH + 1));
        const float gl = std::max(glow_[std::size_t(n)], on ? 1.0f : 0.0f);
        g.setColour(mix(inScale(n) ? juce::Colour(0xff1d1830) : juce::Colour(0xff0e0b18), pal::acid, gl)); g.fillRect(r.withTrimmedBottom(2));
        g.setColour(on ? pal::acidHot : pal::edgeLight); g.fillRect(r.getX() + 1, 0, 1, blackH - 3);
        g.setColour(on ? pal::acidDim : pal::edgeMid); g.fillRect(r.getX() + 1, blackH - 4, r.getWidth() - 2, 2);
    }
}

// ==================================================================== Wheel
void Wheel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    wellBox(g, r, pal::sunken);
    const auto in = r.reduced(2);
    // ridged drum: ridge rows drift with the value so it looks like it turns
    const int shift = int(std::round(value_ * 24.0f));
    for (int y = in.getY(); y < in.getBottom(); ++y) {
        const int k = (y + shift) % 4;
        g.setColour(k == 0 ? pal::edgeLight : k == 1 ? pal::raisedHi : k == 2 ? pal::raised : pal::panel);
        g.fillRect(in.getX(), y, in.getWidth(), 1);
    }
    // position marker
    const int my = in.getBottom() - 2 - int(std::round(value_ * float(in.getHeight() - 4)));
    g.setColour(pal::acid); g.fillRect(in.getX(), my, in.getWidth(), 2);
    g.setColour(pal::acidHot); g.fillRect(in.getX(), my, in.getWidth(), 1);
}

// ============================================================== KeyboardDock
KeyboardDock::KeyboardDock(UiContext& c) : Panel(c) {
    bend_ = &make<Wheel>([this](float v) { ctx.proc.uiPitchBend(v); }, true, true);
    mod_ = &make<Wheel>([this](float v) { ctx.proc.uiController(1, v); }, false, false);
    bendUp_ = &make<Spinner>(ctx, "", -24, 24, 1, [](const Patch& p) { return p.globals.bendUp; }, [](Patch& p, double v) { p.globals.bendUp = v; },
        [](double v) { return juce::String(int(v)); });
    bendDown_ = &make<Spinner>(ctx, "", -24, 24, 1, [](const Patch& p) { return p.globals.bendDown; }, [](Patch& p, double v) { p.globals.bendDown = v; },
        [](double v) { return juce::String(int(v)); });
    for (auto* s : {bendUp_, bendDown_}) { s->showArrows(true); s->setCentred(true); }
    bendUp_->setDefault(2); bendDown_->setDefault(-2);

    clip_ = &make<PixelButton>("CLIP"); clip_->setBold(true); clip_->setFlat(true);
    clipOn_ = &make<PixelButton>(); clipOn_->setIcon(Icon::power);
    arp_ = &make<PixelButton>("ARP"); arp_->setBold(true); arp_->setFlat(true);
    arpOn_ = &make<PixelButton>(); arpOn_->setIcon(Icon::power);
    clip_->onClick = [this] { if (onEditor) onEditor(0); };
    arp_->onClick = [this] { if (onEditor) onEditor(1); };
    clipOn_->onClick = [this] { ctx.editNow([](Patch& p) { p.clipSettings.enabled = !p.clipSettings.enabled; }); };
    arpOn_->onClick = [this] { ctx.editNow([](Patch& p) { p.arpSettings.enabled = !p.arpSettings.enabled; }); };
    clipOn_->setAccent(pal::acid); arpOn_->setAccent(pal::violetHot);

    clipPreview_ = &make<Canvas>(); arpPreview_ = &make<Canvas>();
    clipPreview_->painter = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        wellBox(g, r, pal::sunken);
        const auto& cs = ctx.patch->clipSettings;
        const ClipDef* def = nullptr;
        for (const auto& c : cs.clips) if (!c.notes.empty()) { def = &c; break; }
        if (!def) { drawTextIn(g, "EMPTY", r, pal::textMuted, juce::Justification::centred); return; }
        int lo = 127, hi = 0; for (const auto& n : def->notes) { lo = std::min(lo, n.note); hi = std::max(hi, n.note); }
        for (const auto& n : def->notes) {
            const int x = r.getX() + 2 + int(n.time / std::max(0.25, def->lengthBeats) * (r.getWidth() - 4));
            const int w = std::max(1, int(n.length / std::max(0.25, def->lengthBeats) * (r.getWidth() - 4)));
            const int y = r.getBottom() - 4 - (hi > lo ? (n.note - lo) * (r.getHeight() - 8) / (hi - lo) : (r.getHeight() - 8) / 2);
            g.setColour(clipEnabled_ ? pal::acid : pal::textMuted); g.fillRect(x, y, w, 2);
        }
    };
    arpPreview_->painter = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        wellBox(g, r, pal::sunken);
        const auto& a = ctx.patch->arpSettings.active();
        drawText(g, juce::String(a.shape).toUpperCase(), r.getX() + 4, r.getY() + 4, arpEnabled_ ? pal::violetHot : pal::textMuted, 1, true);
        drawText(g, juce::String(a.rate >= 1.0 ? formatNumber(a.rate, 2) + " BEAT" : "1/" + formatNumber(4.0 / std::max(a.rate, 1.0e-3), 0)), r.getX() + 4, r.getY() + 14, pal::textBody);
        int x = r.getX() + 4;
        for (int i = 0; i < 8 && x < r.getRight() - 4; ++i, x += 9) {
            const int h = 3 + ((i * 5 + a.repeats * 3) % 9);
            g.setColour(arpEnabled_ ? pal::violet : pal::edgeMid); g.fillRect(x, r.getBottom() - 3 - h, 6, h);
        }
    };
    clipPreview_->down = [this](const juce::MouseEvent&) { if (onEditor) onEditor(0); };
    arpPreview_->down = [this](const juce::MouseEvent&) { if (onEditor) onEditor(1); };

    transpose_ = &make<Spinner>(ctx, "TRANSPOSE:", -24, 24, 1, [](const Patch& p) { return p.globals.transpose; }, [](Patch& p, double v) { p.globals.transpose = v; },
        [](double v) { return (v > 0 ? "+" : "") + juce::String(int(v)); });
    transpose_->showArrows(true); transpose_->setLabelWidth(46);
    transpose_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::transpose)});
    key_ = &make<Chooser>();
    key_->buildMenu = [this](juce::PopupMenu& m) {
        for (int k = 0; k < 12; ++k) m.addItem(keyNames[k], true, ctx.patch->globals.pitchQuantizerKey == k,
            [this, k] { ctx.editNow([k](Patch& p) { p.globals.pitchQuantizerKey = k; }); });
    };
    scale_ = &make<Chooser>();
    scale_->buildMenu = [this](juce::PopupMenu& m) {
        const int cur = ctx.patch->globals.pitchQuantizerMask;
        for (const auto& s : scales()) { const int mask = maskOf(s);
            m.addItem(s.name, true, cur == mask, [this, mask] { ctx.editNow([mask](Patch& p) { p.globals.pitchQuantizerMask = mask; }); }); }
    };
    swing_ = &make<Spinner>(ctx, "SWING:", 0, 100, 1, [](const Patch& p) { return p.globals.swing; }, [](Patch& p, double v) { p.globals.swing = v; },
        [](double v) { return std::abs(v - 50.0) < 0.5 ? juce::String("OFF") : juce::String(int(v)) + "%"; });
    swing_->setDefault(50); swing_->setLabelWidth(36);
    swing_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::swing)});
    // performance sources as Serum-style drag handles (drop onto any control to modulate it)
    struct P { const char* label; ModSource kind; };
    static const P perf[3] = {{"MW", ModSource::modWheel}, {"PB", ModSource::pitchBend}, {"AT", ModSource::channelPressure}};
    for (int i = 0; i < 3; ++i) {
        auto* h = &make<Canvas>();
        h->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        h->painter = [this, i](juce::Graphics& g, juce::Rectangle<int> r) {
            const int n = ctx.patch ? countRoutes(*ctx.patch, perf[i].kind, 0) : 0;
            g.setColour(n ? pal::violetShadow : pal::panel); g.fillRect(r);
            zyg::ui::frame(g, r, n ? pal::violetHot : pal::edgeMid);
            drawIcon(g, Icon::grip, r.getX() + 3, r.getCentreY() - 2, n ? pal::violetHot : pal::textMuted);
            drawTextIn(g, perf[i].label, r.withTrimmedLeft(8), n ? pal::textHi : pal::textBody, juce::Justification::centred);
        };
        h->drag = [this, h, i](const juce::MouseEvent& e) { if (e.getDistanceFromDragStart() > 4) startModDrag(*h, perf[i].kind, 0, ctx.uiScale); };
        perf_[std::size_t(i)] = h;
    }
    panic_ = &make<PixelButton>("PANIC"); panic_->setBold(true);
    panic_->onClick = [this] { ctx.proc.uiAllNotesOff(); };

    keys_ = &make<PianoKeys>(ctx);

    always_ = &make<Toggle>(ctx, "ALWAYS", Toggle::Style::check, [](const Patch& p) { return p.globals.portaAlways; }, [](Patch& p, bool v) { p.globals.portaAlways = v; });
    scaled_ = &make<Toggle>(ctx, "SCALED", Toggle::Style::check, [](const Patch& p) { return p.globals.portaScaled; }, [](Patch& p, bool v) { p.globals.portaScaled = v; });
    porta_ = &make<Knob>(ctx, KnobSpec{"PORTA", 0, 3, 0, false, false, 0.5, 0, fmt::seconds},
        [](const Patch& p) { return p.globals.portamentoTime; }, [](Patch& p, double v) { p.globals.portamentoTime = v; }, 26);
    porta_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::portamentoTime)});
    curve_ = &make<Canvas>();
    curve_->setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    curve_->painter = [this](juce::Graphics& g, juce::Rectangle<int> r) {
        wellBox(g, r, pal::sunken);
        const auto in = r.reduced(4);
        const double bend = (curveValue_ - 50.0) / 50.0;
        std::vector<juce::Point<int>> pts;
        for (int x = 0; x < in.getWidth(); ++x) {
            const double t = double(x) / double(in.getWidth() - 1);
            const double k = bend * 3.0;
            const double v = std::abs(k) < 1e-3 ? t : (std::exp(k * t) - 1.0) / (std::exp(k) - 1.0);
            pts.push_back({in.getX() + x, in.getBottom() - 1 - int(std::round(v * (in.getHeight() - 1)))});
        }
        pixelPolyline(g, pts, pal::acid);
    };
    curve_->down = [this](const juce::MouseEvent&) { curveDragStart_ = curveValue_; };
    curve_->drag = [this](const juce::MouseEvent& e) {
        curveValue_ = std::clamp(curveDragStart_ - double(e.getDistanceFromDragStartY()) * 1.5, 0.0, 100.0);
        ctx.edit(curve_, [v = curveValue_](Patch& p) { p.globals.portamentoCurve = v; });
        curve_->repaint();
    };
    curve_->up = [this](const juce::MouseEvent&) { ctx.flush(); };
    curve_->dbl = [this](const juce::MouseEvent&) { curveValue_ = 50.0; ctx.editNow([](Patch& p) { p.globals.portamentoCurve = 50.0; }); curve_->repaint(); };
}

void KeyboardDock::refresh(const Patch& p) {
    Panel::refresh(p);
    clipEnabled_ = p.clipSettings.enabled; arpEnabled_ = p.arpSettings.enabled;
    clipOn_->setToggled(clipEnabled_); arpOn_->setToggled(arpEnabled_);
    clip_->setToggled(clipEnabled_); arp_->setToggled(arpEnabled_);
    for (auto* h : perf_) h->repaint();
    arp_->setAccent(pal::violetHot);
    key_->setText(keyNames[std::clamp(p.globals.pitchQuantizerKey, 0, 11)]);
    key_->setDimmed(p.globals.pitchQuantizerMask == 0);
    juce::String name = "CUSTOM";
    for (const auto& s : scales()) if (maskOf(s) == p.globals.pitchQuantizerMask) { name = s.name; break; }
    scale_->setText(name);
    if (curveValue_ != p.globals.portamentoCurve) { curveValue_ = p.globals.portamentoCurve; curve_->repaint(); }
    clipPreview_->repaint(); arpPreview_->repaint();
    keys_->repaint();
}

void KeyboardDock::frame() {
    keys_->refreshHeld();
    Panel::frame();
}

void KeyboardDock::resized() {
    const int w = getWidth();
    const int rightW = 127, pianoX = 330;
    const int avail = w - rightW - pianoX - 4;
    const int keyW = std::max(15, avail / 36);
    keys_->setKeyWidth(keyW);
    const int pw = keyW * 36;
    bend_->setBounds(6, 6, 20, 56);
    bendUp_->setBounds(30, 10, 34, 15); bendDown_->setBounds(30, 40, 34, 15);
    mod_->setBounds(72, 6, 20, 56);
    for (int i = 0; i < 3; ++i) perf_[std::size_t(i)]->setBounds(98, 5 + i * 20, 36, 18);
    clipOn_->setBounds(140, 3, 18, 19); clip_->setBounds(160, 3, 74, 19);
    arpOn_->setBounds(238, 3, 18, 19); arp_->setBounds(258, 3, 66, 19);
    clipPreview_->setBounds(140, 24, 94, 42); arpPreview_->setBounds(238, 24, 86, 42);
    transpose_->setBounds(pianoX, 3, 96, 15);
    key_->setBounds(pianoX + 102, 3, 50, 15);
    scale_->setBounds(pianoX + 156, 3, 130, 15);
    swing_->setBounds(pianoX + 292, 3, 90, 15);
    panic_->setBounds(pianoX + pw - 50, 3, 50, 15);
    keys_->setBounds(pianoX, 22, pw, keyH);
    const int rx = w - rightW;
    always_->setBounds(rx + 7, 5, 56, 11); scaled_->setBounds(rx + 67, 5, 56, 11);
    porta_->setBounds(rx + 5, 18, 50, 48);
    curve_->setBounds(rx + 73, 22, 44, 38);
}

void KeyboardDock::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    g.setColour(pal::chassis); g.fillRect(r);
    hLine(g, 0, 0, r.getWidth(), pal::edgeMid);
    drawText(g, "CURVE", r.getWidth() - 127 + 79, 61, pal::textMuted);
    vLine(g, 138, 2, r.getHeight() - 4, pal::edgeDark);
    vLine(g, 326, 2, r.getHeight() - 4, pal::edgeDark);
    vLine(g, r.getWidth() - 127 - 2, 2, r.getHeight() - 4, pal::edgeDark);
}

}
