#include "SeqEditors.h"
#include <cmath>

namespace zyg::ui {
namespace {
constexpr int keysW = 36, velH = 38;
bool blackKey(int n) { const int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
juce::String noteName(int n) {
    static const char* nm[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(nm[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2);
}
double snapBeat(double t) { return std::round(t * 4.0) / 4.0; }
}

// =================================================================== RollEditor
void RollEditor::setData(std::vector<RollNote> notes, double lengthBeats) {
    notes_ = std::move(notes);
    length_ = std::max(0.25, lengthBeats);
    if (!arp_ && !notes_.empty() && drag_ < 0) {
        int lo = 127, hi = 0; for (const auto& n : notes_) { lo = std::min(lo, n.note); hi = std::max(hi, n.note); }
        if (lo < low_ || hi >= low_ + rows_) low_ = std::clamp(lo - 4, 0, 127 - rows_);
    }
    repaint();
}

juce::Rectangle<int> RollEditor::grid() const { return getLocalBounds().withTrimmedLeft(keysW).withTrimmedBottom(velH + 4); }
juce::Rectangle<int> RollEditor::velLane() const { return getLocalBounds().withTrimmedLeft(keysW).withTop(getHeight() - velH); }
int RollEditor::noteAtY(int y) const {
    const auto g = grid();
    const double rh = double(g.getHeight()) / double(rowCount());
    const int row = int(std::floor(double(g.getBottom() - y) / rh));
    return lowNote() + std::clamp(row, 0, rowCount() - 1);
}
double RollEditor::timeAtX(int x, bool snap) const {
    const auto g = grid();
    const double t = std::clamp(double(x - g.getX()) / double(g.getWidth()) * length_, 0.0, length_);
    return snap ? snapBeat(t) : t;
}
juce::Rectangle<int> RollEditor::noteRect(const RollNote& n) const {
    const auto g = grid();
    const double rh = double(g.getHeight()) / double(rowCount());
    const int x = g.getX() + int(std::round(n.time / length_ * g.getWidth()));
    const int w = std::max(4, int(std::round(n.length / length_ * g.getWidth())));
    const int y = g.getBottom() - int(std::round(double(n.note - lowNote() + 1) * rh));
    return {x, y + 1, w, std::max(3, int(rh) - 1)};
}
int RollEditor::hit(juce::Point<int> p) const {
    for (int i = int(notes_.size()) - 1; i >= 0; --i) if (noteRect(notes_[std::size_t(i)]).expanded(0, 0).contains(p)) return i;
    return -1;
}

void RollEditor::paint(juce::Graphics& g) {
    const auto gr = grid();
    wellBox(g, gr, pal::sunken);
    const double rh = double(gr.getHeight()) / double(rowCount());
    for (int r = 0; r < rowCount(); ++r) {
        const int note = lowNote() + r;
        const int y = gr.getBottom() - int(std::round(double(r + 1) * rh));
        const bool bk = !arp_ && blackKey(note);
        g.setColour(bk ? pal::voidBg : mix(pal::sunken, pal::panel, (r & 1) ? 0.55f : 0.25f));
        g.fillRect(gr.getX() + 1, y, gr.getWidth() - 2, int(std::ceil(rh)));
        // keyboard strip
        const juce::Rectangle<int> key(0, y, keysW - 2, int(std::ceil(rh)));
        g.setColour(arp_ ? pal::raised : (bk ? pal::edgeDark : juce::Colour(0xffdfe8dd))); g.fillRect(key);
        if (arp_ || note % 12 == 0) drawTextIn(g, arp_ ? "N" + juce::String(r + 1) : noteName(note), key.reduced(2, 0), arp_ ? pal::textBody : pal::textMuted);
    }
    // beat grid
    const int beats = int(std::ceil(length_));
    for (int b = 0; b <= beats * 4; ++b) {
        const double t = double(b) / 4.0;
        if (t > length_ + 1e-6) break;
        const int x = gr.getX() + int(std::round(t / length_ * gr.getWidth()));
        const bool bar = b % 4 == 0;
        for (int y = gr.getY() + 1; y < gr.getBottom() - 1; y += bar ? 1 : 3) px(g, x, y, bar ? pal::edgeMid : pal::edgeDark);
        if (bar) drawText(g, juce::String(b / 4 + 1), x + 2, gr.getY() + 2, pal::textMuted);
    }
    // notes
    for (std::size_t i = 0; i < notes_.size(); ++i) {
        const auto& n = notes_[i];
        auto r = noteRect(n);
        const auto base = arp_ ? pal::violet : pal::acidDim;
        const auto hot = arp_ ? pal::violetHot : pal::acid;
        g.setColour(pal::edgeDark); g.fillRect(r.expanded(1));
        g.setColour(mix(base, hot, float(n.vel))); g.fillRect(r);
        g.setColour(pal::acidHot.withAlpha(0.7f)); g.fillRect(r.getX(), r.getY(), r.getWidth(), 1);
        g.setColour(pal::edgeDark); g.fillRect(r.getRight() - 2, r.getY(), 1, r.getHeight());
        if (int(i) == drag_) frame(g, r.expanded(1), pal::acidHot);
        if (r.getWidth() > 20) drawText(g, arp_ ? "N" + juce::String(n.note + 1) : noteName(n.note), r.getX() + 2, r.getY() + std::max(0, (r.getHeight() - 5) / 2), pal::sunken);
    }
    // velocity lane
    const auto vl = velLane();
    wellBox(g, vl, pal::sunken);
    drawText(g, "VEL", 6, vl.getY() + 4, pal::textMuted);
    for (const auto& n : notes_) {
        const int x = noteRect(n).getX();
        const int h = int(std::round(n.vel * double(vl.getHeight() - 6)));
        g.setColour(arp_ ? pal::violetHot : pal::acid); g.fillRect(x, vl.getBottom() - 3 - h, 3, h);
        g.setColour(pal::acidHot); g.fillRect(x - 1, vl.getBottom() - 4 - h, 5, 2);
    }
    if (notes_.empty()) drawTextIn(g, "DOUBLE-CLICK TO ADD A " + juce::String(arp_ ? "STEP" : "NOTE"), gr, pal::textMuted, juce::Justification::centred);
}

void RollEditor::mouseDown(const juce::MouseEvent& e) {
    if (velLane().contains(e.getPosition())) {
        int best = -1, bd = 12;
        for (std::size_t i = 0; i < notes_.size(); ++i) { const int d = std::abs(noteRect(notes_[i]).getX() + 1 - e.x); if (d < bd) { bd = d; best = int(i); } }
        velDrag_ = best;
        if (best >= 0) mouseDrag(e);
        return;
    }
    const int h = hit(e.getPosition());
    if (h < 0) return;
    if (e.mods.isPopupMenu()) { notes_.erase(notes_.begin() + h); changed(); return; }
    drag_ = h;
    const auto r = noteRect(notes_[std::size_t(h)]);
    resize_ = e.x >= r.getRight() - 4;
    grabDt_ = timeAtX(e.x, false) - notes_[std::size_t(h)].time;
    grabNote_ = noteAtY(e.y) - notes_[std::size_t(h)].note;
    lastLen_ = notes_[std::size_t(h)].length;
    repaint();
}

void RollEditor::mouseDrag(const juce::MouseEvent& e) {
    if (velDrag_ >= 0) {
        const auto vl = velLane();
        notes_[std::size_t(velDrag_)].vel = std::clamp(1.0 - double(e.y - vl.getY() - 3) / double(vl.getHeight() - 6), 0.05, 1.0);
        changed(); return;
    }
    if (drag_ < 0) return;
    auto& n = notes_[std::size_t(drag_)];
    const bool snap = !e.mods.isAltDown();
    if (resize_) {
        n.length = std::max(snap ? 0.25 : 0.03, (snap ? snapBeat(timeAtX(e.x, false)) : timeAtX(e.x, false)) - n.time);
        n.length = std::min(n.length, length_ - n.time);
        lastLen_ = n.length;
    } else {
        const double t = timeAtX(e.x, false) - grabDt_;
        n.time = std::clamp(snap ? snapBeat(t) : t, 0.0, std::max(0.0, length_ - n.length));
        n.note = std::clamp(noteAtY(e.y) - grabNote_, lowNote(), lowNote() + rowCount() - 1);
    }
    changed();
}

void RollEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!grid().contains(e.getPosition()) || hit(e.getPosition()) >= 0) return;
    RollNote n; n.time = std::min(timeAtX(e.x, true), std::max(0.0, length_ - 0.25)); n.length = std::min(lastLen_, length_ - n.time);
    n.note = noteAtY(e.y); n.vel = 0.8;
    notes_.push_back(n);
    std::sort(notes_.begin(), notes_.end(), [](const RollNote& a, const RollNote& b) { return a.time < b.time; });
    changed();
}

void RollEditor::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (arp_) return;
    low_ = std::clamp(low_ + (w.deltaY > 0 ? 3 : -3), 0, 127 - rows_);
    repaint();
}

// ==================================================================== contents
namespace {
class ClipContent final : public Panel {
public:
    explicit ClipContent(UiContext& c) : Panel(c) {
        for (int i = 0; i < 12; ++i) {
            auto& t = make<TabButton>(); t.setText(juce::String(i + 1)); t.setLedShown(false); t.setAccent(pal::acid);
            t.onClick = [this, i] { sel_ = i; if (ctx.patch) refresh(*ctx.patch); };
            tabs_[std::size_t(i)] = &t;
        }
        enable_ = &make<Toggle>(ctx, "CLIP PLAYER ON", Toggle::Style::led, [](const Patch& p) { return p.clipSettings.enabled; }, [](Patch& p, bool v) { p.clipSettings.enabled = v; });
        length_ = &make<Spinner>(ctx, "LENGTH", 1, 64, 1, [this](const Patch& p) { return p.clipSettings.clips[std::size_t(sel_)].lengthBeats; },
            [this](Patch& p, double v) { p.clipSettings.clips[std::size_t(sel_)].lengthBeats = v; }, [](double v) { return juce::String(int(v)) + " BEATS"; });
        trans_ = &make<Spinner>(ctx, "TRANS", -24, 24, 1, [this](const Patch& p) { return double(p.clipSettings.clips[std::size_t(sel_)].transpose); },
            [this](Patch& p, double v) { p.clipSettings.clips[std::size_t(sel_)].transpose = int(v); }, [](double v) { return (v > 0 ? "+" : "") + juce::String(int(v)); });
        rate_ = &make<Spinner>(ctx, "RATE", 0.25, 4.0, 0.25, [this](const Patch& p) { return p.clipSettings.clips[std::size_t(sel_)].rate; },
            [this](Patch& p, double v) { p.clipSettings.clips[std::size_t(sel_)].rate = v; }, [](double v) { return formatNumber(v, 2) + "X"; });
        for (auto* s : {length_, trans_, rate_}) { s->showArrows(true); s->setLabelWidth(40); }
        mode_ = &make<Chooser>(); mode_->setTextColour(pal::lcd);
        mode_->buildMenu = [this](juce::PopupMenu& m) {
            for (auto* n : {"OneShot", "Pendulum", "Random", "Static"})
                m.addItem(juce::String(n).toUpperCase(), true, ctx.patch->clipSettings.clips[std::size_t(sel_)].playbackMode == n,
                          [this, n] { ctx.editNow([s = sel_, n](Patch& p) { p.clipSettings.clips[std::size_t(s)].playbackMode = n; }); });
        };
        span_ = &make<Chooser>(); span_->setTextColour(pal::lcd);
        span_->buildMenu = [this](juce::PopupMenu& m) {
            for (auto* n : {"Mono", "Offset", "Poly"})
                m.addItem(juce::String(n).toUpperCase(), true, ctx.patch->clipSettings.clips[std::size_t(sel_)].spanMode == n,
                          [this, n] { ctx.editNow([s = sel_, n](Patch& p) { p.clipSettings.clips[std::size_t(s)].spanMode = n; }); });
        };
        retrig_ = &make<Toggle>(ctx, "RETRIG", Toggle::Style::check, [this](const Patch& p) { return p.clipSettings.clips[std::size_t(sel_)].retrigger; },
            [this](Patch& p, bool v) { p.clipSettings.clips[std::size_t(sel_)].retrigger = v; });
        gate_ = &make<Toggle>(ctx, "NOTE GATE", Toggle::Style::check, [this](const Patch& p) { return p.clipSettings.clips[std::size_t(sel_)].noteGate; },
            [this](Patch& p, bool v) { p.clipSettings.clips[std::size_t(sel_)].noteGate = v; });
        clear_ = &make<PixelButton>("CLEAR CLIP"); clear_->setBold(true); clear_->setAccent(pal::crimson);
        clear_->onClick = [this] { ctx.editNow([s = sel_](Patch& p) { p.clipSettings.clips[std::size_t(s)].notes.clear(); }); };
        roll_ = &make<RollEditor>(false);
        roll_->onChange = [this](const std::vector<RollNote>& v) {
            ctx.edit(roll_, [s = sel_, v](Patch& p) {
                auto& c = p.clipSettings.clips[std::size_t(s)]; c.notes.clear();
                for (const auto& n : v) c.notes.push_back({n.time, n.length, n.note, n.vel});
            });
        };
    }
    void refresh(const Patch& p) override {
        Panel::refresh(p);
        for (int i = 0; i < 12; ++i) { tabs_[std::size_t(i)]->setSelected(i == sel_); tabs_[std::size_t(i)]->setBadge(0); }
        const auto& c = p.clipSettings.clips[std::size_t(sel_)];
        mode_->setText(juce::String(c.playbackMode).toUpperCase()); span_->setText(juce::String(c.spanMode).toUpperCase());
        if (!roll_->isEditing()) {
            std::vector<RollNote> v; for (const auto& n : c.notes) v.push_back({n.time, n.length, n.note, n.velocity});
            roll_->setData(std::move(v), c.lengthBeats);
        }
        repaint();
    }
    void resized() override {
        const int w = getWidth(), h = getHeight();
        for (int i = 0; i < 12; ++i) tabs_[std::size_t(i)]->setBounds(i * w / 12, h - 22, w / 12, 20);
        int y = 4; const int lw = 168;
        enable_->setBounds(6, y, lw - 8, 11); y += 22;
        length_->setBounds(6, y, lw - 8, 15); y += 20; trans_->setBounds(6, y, lw - 8, 15); y += 20; rate_->setBounds(6, y, lw - 8, 15); y += 26;
        mode_->setBounds(6, y, lw - 8, 15); y += 20; span_->setBounds(6, y, lw - 8, 15); y += 24;
        retrig_->setBounds(6, y, lw - 8, 11); y += 16; gate_->setBounds(6, y, lw - 8, 11); y += 26;
        clear_->setBounds(6, y, lw - 8, 18);
        roll_->setBounds(lw + 4, 2, w - lw - 8, h - 28);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(pal::chassis); g.fillRect(getLocalBounds());
        drawText(g, "PLAYBACK", 6, 96 - 0, pal::textMuted);
        drawText(g, "CLIP", 6, getHeight() - 32, pal::textMuted);
    }
private:
    int sel_ = 0;
    std::array<TabButton*, 12> tabs_ {};
    Toggle *enable_, *retrig_, *gate_;
    Spinner *length_, *trans_, *rate_;
    Chooser *mode_, *span_;
    PixelButton* clear_;
    RollEditor* roll_;
};

class ArpContent final : public Panel {
public:
    explicit ArpContent(UiContext& c) : Panel(c) {
        for (int i = 0; i < 12; ++i) {
            auto& t = make<TabButton>(); t.setText(juce::String(i + 1)); t.setLedShown(false); t.setAccent(pal::violetHot);
            t.onClick = [this, i] { ctx.editNow([i](Patch& p) { p.arpSettings.activeClip = i; }); };
            tabs_[std::size_t(i)] = &t;
        }
        enable_ = &make<Toggle>(ctx, "ARP ON", Toggle::Style::led, [](const Patch& p) { return p.arpSettings.enabled; }, [](Patch& p, bool v) { p.arpSettings.enabled = v; });
        enable_->setOnColour(pal::violetHot);
        shape_ = &make<Chooser>(); shape_->setTextColour(pal::violetHot); shape_->setArrows(true);
        static const char* shapes[] = {"Up", "Down", "UpDown", "DownUp", "UpAndDown", "DownAndUp", "Converge", "ConvAndDiv", "Rand", "RandNoDup", "RandOnce", "RandDrift", "ThumbUD", "Played", "Pattern"};
        shape_->buildMenu = [this](juce::PopupMenu& m) {
            for (auto* n : shapes) m.addItem(juce::String(n).toUpperCase(), true, cur().shape == n, [this, n] { edit([n](ArpClipDef& d) { d.shape = n; }); });
        };
        shape_->onStep = [this](int dir) {
            int at = 0; for (int i = 0; i < 15; ++i) if (cur().shape == shapes[i]) at = i;
            const auto n = shapes[(at + dir + 15) % 15]; edit([n](ArpClipDef& d) { d.shape = n; });
        };
        rate_ = &make<Chooser>(); rate_->setTextColour(pal::lcd); rate_->setArrows(true);
        static const double rates[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
        static const char* rateNames[] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR"};
        rate_->buildMenu = [this](juce::PopupMenu& m) {
            for (int i = 0; i < 7; ++i) m.addItem(rateNames[i], true, std::abs(cur().rate - rates[i]) < 1e-6, [this, i] { edit([i](ArpClipDef& d) { d.rate = rates[i]; }); });
        };
        rate_->onStep = [this](int dir) {
            int at = 2; for (int i = 0; i < 7; ++i) if (std::abs(cur().rate - rates[i]) < 1e-6) at = i;
            const int n = std::clamp(at + dir, 0, 6); edit([n](ArpClipDef& d) { d.rate = rates[n]; });
        };
        dotted_ = &make<Toggle>(ctx, "DOTTED", Toggle::Style::check, [this](const Patch& p) { return p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].dotted; },
            [](Patch& p, bool v) { p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].dotted = v; });
        triplet_ = &make<Toggle>(ctx, "TRIPLET", Toggle::Style::check, [](const Patch& p) { return p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].triplet; },
            [](Patch& p, bool v) { p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].triplet = v; });
        retrig_ = &make<Toggle>(ctx, "RETRIG", Toggle::Style::check, [](const Patch& p) { return p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].retrigger; },
            [](Patch& p, bool v) { p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)].retrigger = v; });
        auto k = [&](const char* label, double lo, double hi, double def, std::function<double(const ArpClipDef&)> get, std::function<void(ArpClipDef&, double)> set, int step, std::function<juce::String(double)> f) {
            KnobSpec spec; spec.label = label; spec.lo = lo; spec.hi = hi; spec.def = def; spec.step = step; spec.format = std::move(f);
            auto* kn = &make<Knob>(ctx, spec, [get](const Patch& p) { return get(p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)]); },
                [set](Patch& p, double v) { set(p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)], v); }, 28);
            knobs_.push_back(kn);
        };
        auto pct = [](double v) { return formatNumber(v, 0) + "%"; };
        k("GATE", 0, 200, 100, [](const ArpClipDef& d) { return d.gate; }, [](ArpClipDef& d, double v) { d.gate = v; }, 0, pct);
        k("CHANCE", 0, 100, 100, [](const ArpClipDef& d) { return d.chance; }, [](ArpClipDef& d, double v) { d.chance = v; }, 0, pct);
        k("REPEATS", 1, 16, 1, [](const ArpClipDef& d) { return double(d.repeats); }, [](ArpClipDef& d, double v) { d.repeats = int(v); }, 1, [](double v) { return juce::String(int(v)); });
        k("OCTAVES", 1, 3, 1, [](const ArpClipDef& d) { return double(d.transposeRange); }, [](ArpClipDef& d, double v) { d.transposeRange = int(v); }, 1, [](double v) { return juce::String(int(v)); });
        k("TRANSPOSE", -24, 24, 0, [](const ArpClipDef& d) { return d.transpose; }, [](ArpClipDef& d, double v) { d.transpose = v; }, 1, [](double v) { return (v > 0 ? "+" : "") + juce::String(int(v)); });
        k("LENGTH", 1, 32, 4, [](const ArpClipDef& d) { return d.lengthBeats; }, [](ArpClipDef& d, double v) { d.lengthBeats = v; }, 1, [](double v) { return juce::String(int(v)) + " BEATS"; });
        roll_ = &make<RollEditor>(true);
        roll_->onChange = [this](const std::vector<RollNote>& v) {
            ctx.edit(roll_, [v](Patch& p) {
                auto& c = p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)]; c.steps.clear();
                for (const auto& n : v) c.steps.push_back({n.time, n.length, n.note, n.vel});
            });
        };
    }
    void refresh(const Patch& p) override {
        Panel::refresh(p);
        const int a = p.arpSettings.activeClip;
        for (int i = 0; i < 12; ++i) { tabs_[std::size_t(i)]->setSelected(i == a); tabs_[std::size_t(i)]->setBadge(0); }
        const auto& c = p.arpSettings.clips[std::size_t(a)];
        shape_->setText(juce::String(c.shape).toUpperCase());
        static const double rates[] = {0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0};
        static const char* rateNames[] = {"1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR"};
        juce::String rn = formatNumber(c.rate, 3) + " BEATS";
        for (int i = 0; i < 7; ++i) if (std::abs(c.rate - rates[i]) < 1e-6) rn = rateNames[i];
        rate_->setText(rn);
        if (!roll_->isEditing()) {
            std::vector<RollNote> v; for (const auto& s : c.steps) v.push_back({s.time, s.length, std::clamp(s.note, 0, 7), s.velocity});
            roll_->setData(std::move(v), std::max(0.25, c.lengthBeats));
        }
        patternMode_ = c.shape == "Pattern";
        repaint();
    }
    void resized() override {
        const int w = getWidth(), h = getHeight();
        for (int i = 0; i < 12; ++i) tabs_[std::size_t(i)]->setBounds(i * w / 12, h - 22, w / 12, 20);
        const int lw = 190;
        enable_->setBounds(6, 4, lw - 8, 11);
        shape_->setBounds(6, 30, lw - 8, 15); rate_->setBounds(6, 62, lw - 8, 15);
        dotted_->setBounds(6, 84, 80, 11); triplet_->setBounds(96, 84, 80, 11); retrig_->setBounds(6, 102, 80, 11);
        for (std::size_t i = 0; i < knobs_.size(); ++i) knobs_[i]->setBounds(4 + int(i % 3) * 62, 124 + int(i / 3) * 52, 62, 48);
        roll_->setBounds(lw + 4, 2, w - lw - 8, h - 28);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(pal::chassis); g.fillRect(getLocalBounds());
        drawText(g, "SHAPE", 6, 20, pal::textMuted); drawText(g, "RATE", 6, 52, pal::textMuted);
        drawTextIn(g, patternMode_ ? "PATTERN STEPS (N1-N8 = HELD NOTE INDEX)" : "PATTERN STEPS - USED WHEN SHAPE IS PATTERN", {getWidth() - 340, 4, 336, 10}, patternMode_ ? pal::violetHot : pal::textMuted, juce::Justification::centredRight);
    }
private:
    const ArpClipDef& cur() const { return ctx.patch->arpSettings.active(); }
    void edit(std::function<void(ArpClipDef&)> fn) { ctx.editNow([fn = std::move(fn)](Patch& p) { fn(p.arpSettings.clips[std::size_t(p.arpSettings.activeClip)]); }); }
    std::array<TabButton*, 12> tabs_ {};
    Toggle *enable_, *dotted_, *triplet_, *retrig_;
    Chooser *shape_, *rate_;
    std::vector<Knob*> knobs_;
    RollEditor* roll_;
    bool patternMode_ = false;
};
}

// ==================================================================== SeqPanel
SeqPanel::SeqPanel(UiContext& c, bool arp) : ModalPanel(arp ? "ARPEGGIATOR" : "MIDI CLIP PLAYER"), ctx_(c) {
    if (arp) content_ = std::make_unique<ArpContent>(c); else content_ = std::make_unique<ClipContent>(c);
    addAndMakeVisible(*content_);
    content_->refresh(*ctx_.patch);
    seen_ = ctx_.version();
    startTimerHz(20);
}
SeqPanel::~SeqPanel() { stopTimer(); }

void SeqPanel::resized() { content_->setBounds(body()); }
void SeqPanel::paint(juce::Graphics& g) { ModalPanel::paint(g); }

void SeqPanel::timerCallback() {
    if (ctx_.version() != seen_) { seen_ = ctx_.version(); content_->refresh(*ctx_.patch); }
}

}
