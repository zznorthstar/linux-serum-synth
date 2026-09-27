#include "FxPage.h"
#include "AssetBrowse.h"
#include <cmath>

namespace zyg::ui {
namespace {
using zyg::fx::sFreq;

bool isToggleName(const juce::String& n) {
    static const char* names[] = {"monoInput", "swapAB", "beatSync", "retrig", "filterMode", "multiband", "link", "lfMono",
                                  "polarityL", "polarityR", "pad", "minPhase", "active", "reverse"};
    for (auto* x : names) if (n == x) return true;
    return false;
}
juce::String labelFor(const juce::String& n) {
    static const std::pair<const char*, const char*> over[] = {
        {"delayBalance", "BALANCE"}, {"outputMix", "OUT MIX"}, {"outputWidth", "OUT WIDTH"}, {"delayTime", "TIME"}, {"feedback", "FEEDBACK"},
        {"ratioBelow", "RATIO LOW"}, {"bandwidth", "BANDWIDTH"}, {"frequency", "FREQ"}, {"numStages", "STAGES"}, {"numPoles", "POLES"},
        {"vintageScale", "VINT A"}, {"vintageScaleB", "VINT B"}, {"preDelay", "PREDELAY"}, {"predelay", "PREDELAY"}, {"dimSize", "DIM SIZE"},
        {"dimWet", "DIM MIX"}, {"xoverLow", "X-LOW"}, {"xoverHi", "X-HIGH"}, {"threshUD0", "THR LOW"}, {"threshUD1", "THR MID"},
        {"threshUD2", "THR HIGH"}, {"gain0", "GAIN LOW"}, {"gain1", "GAIN MID"}, {"gain2", "GAIN HIGH"}, {"ratio0", "RATIO LOW"},
        {"ratio1", "RATIO MID"}, {"ratio2", "RATIO HIGH"}, {"lfXover", "LF XOVER"}, {"ipTrim", "TRIM"}, {"freqB", "FREQ B"}, {"freqC", "FREQ C"},
        {"beats", "LENGTH"}, {"thresh", "THRESH"}, {"falloff", "FALLOFF"}, {"freq1", "FREQ 1"}, {"freq2", "FREQ 2"}, {"gain1", "GAIN 1"}, {"reso1", "RESO 1"}, {"reso2", "RESO 2"}, {"depth2", "DEPTH 2"}, {"delay2", "DELAY 2"}};
    for (const auto& o : over) if (n == o.first) return o.second;
    juce::String out;
    for (int i = 0; i < n.length(); ++i) {
        const auto c = n[i];
        if (i > 0 && juce::CharacterFunctions::isUpperCase(c) && !juce::CharacterFunctions::isUpperCase(n[i - 1])) out += " ";
        out += juce::String::charToString(c);
    }
    return out.toUpperCase();
}

std::function<juce::String(double)> formatFor(FxType t, const juce::String& n, double lo, double hi) {
    auto ms = [](double v) { return formatNumber(v, v < 10 ? 1 : 0) + " MS"; };
    if (t == FxType::pump || t == FxType::stutter) {   // ZYG extensions
        if (n == "beats") return [](double v) {
            const double d = 4.0 / std::max(v, 1.0e-3);
            if (v >= 4.0) return formatNumber(v / 4.0, 2) + " BAR";
            if (std::abs(d - std::round(d)) < 0.06 * d) return "1/" + juce::String(int(std::round(d)));
            return formatNumber(v, 2) + " BEAT";
        };
        if (n == "attack" || n == "release" || n == "smooth") return ms;
        if (n == "thresh") return [](double v) { return formatNumber(v, 0) + " DB"; };
        if (n == "pitch") return [](double v) { return (v > 0 ? "+" : "") + formatNumber(v, 1) + " ST"; };
        if (n == "hold" || n == "falloff" || n == "chance" || n == "gate" || n == "depth") return [](double v) { return formatNumber(v, 0) + "%"; };
        if (n == "shape") return [](double v) { return (v > 0 ? "+" : "") + formatNumber(v, 0); };
    }
    if (n == "wet") return [](double v) { return formatNumber(v, 0) + "%"; };
    if ((t == FxType::filter || t == FxType::distortion) && (n == "freq" || n == "frequency")) return fmt::cutoffHz;
    if (n == "freq" && t == FxType::reverb) return [](double v) { return formatNumber(v, 0) + "%"; };
    if (n == "freq" || n == "frequency" || n == "freq1" || n == "freq2" || n == "xoverLow" || n == "xoverHi" || n == "hpf" || n == "lpf" ||
        n == "lfXover" || n == "filter" || n == "range" || n == "freq2") return fmt::hz;
    if (t == FxType::comp && (n == "attack" || n == "release")) return ms;
    if (t == FxType::comp && n == "ratio") return [](double v) { return v >= 100.0 ? juce::String("INF") : formatNumber(v, 1) + ":1"; };
    if (t == FxType::comp && n == "makeup") return [](double v) { return formatNumber(20.0 * std::log10(std::max(1.0, v)), 1) + " DB"; };
    if ((t == FxType::chorus && (n == "delay" || n == "delay2")) || (t == FxType::reverb && n == "delay")) return ms;
    if (n == "delayTime" || n == "timeL" || n == "timeR" || n == "preDelay" || n == "predelay" || n == "attack" || n == "decay") return fmt::seconds;
    if (n == "gain" || n == "gain0" || n == "gain1" || n == "gain2" || n == "ipTrim" || (t == FxType::eq && n.startsWith("gain")))
        return [](double v) { return (v > 0 ? "+" : "") + formatNumber(v, 1) + " DB"; };
    if (n == "rate" && (t == FxType::chorus || t == FxType::flanger || t == FxType::phaser)) return fmt::hz;
    if (n == "width" && (t == FxType::flanger || t == FxType::phaser)) return fmt::degrees;
    if (n == "numStages" || n == "numPoles" || n == "unison" || n == "count1" || n == "count2" || n == "count3") return [](double v) { return juce::String(int(std::round(v))); };
    if (hi == 100.0 || (n == "width" && hi == 800.0)) return [](double v) { return formatNumber(v, 0) + "%"; };
    return fmt::plain;
}

const char* distortionNames[17] = {"ASYMMETRIC", "DIODE 1", "DIODE 2", "DOWNSAMPLE", "HARD CLIP", "LINEAR FOLD", "OVERDRIVE", "RECTIFY", "SINE FOLD",
                                   "SINE SHAPER", "SOFT CLIP", "SOFT SAT", "STOMP BOX", "TAPE SAT", "X-SHAPER", "X-SHAPER ASYM", "ZERO SQUARE"};
const char* reverbNames[4] = {"BASIC", "VINTAGE", "SPACE", "ABYSS"};
}

// ============================================================ FxModulePanel
FxModulePanel::FxModulePanel(UiContext& c, int fxIndex, int depth) : Panel(c), idx_(fxIndex), depth_(depth) {
    const auto& m = ctx.patch->fx[std::size_t(idx_)];
    type_ = m.fxType;
    const auto col = fxColour(type_);
    const bool splitter = isSplitter(type_);
    enable_ = &make<Toggle>(ctx, "ON", Toggle::Style::led, [i = idx_](const Patch& p) { return i < int(p.fx.size()) && p.fx[std::size_t(i)].enabled; },
        [i = idx_](Patch& p, bool v) { if (i < int(p.fx.size())) p.fx[std::size_t(i)].enabled = v; });
    enable_->setOnColour(col);
    viz_ = &make<FxViz>(ctx, idx_);

    const auto table = fxParamTable(type_);
    int wetSlot = -1;
    for (std::size_t s = 0; s < table.size(); ++s) if (juce::String(table[s].name) == "wet") wetSlot = int(s);
    auto addKnob = [&](int slot) {
        const auto& info = table[std::size_t(slot)];
        KnobSpec spec; spec.label = labelFor(info.name); spec.lo = info.lo; spec.hi = info.hi; spec.def = info.def;
        spec.bipolar = info.lo < 0.0 && info.hi > 0.0;
        spec.logScale = fxParamIsLog(type_, std::size_t(slot)) && info.lo > 0.0;
        if (juce::String(info.name) == "ratio" && type_ == FxType::comp) { spec.logScale = true; spec.lo = 1.0; spec.hi = 1000.0; }
        if (juce::String(info.name) == "numStages" || juce::String(info.name) == "numPoles" || juce::String(info.name) == "unison") spec.step = 1;
        spec.format = formatFor(type_, info.name, info.lo, info.hi);
        auto* k = &make<Knob>(ctx, spec,
            [i = idx_, slot](const Patch& p) { return i < int(p.fx.size()) ? p.fx[std::size_t(i)].p[std::size_t(slot)] : 0.0; },
            [i = idx_, slot](Patch& p, double v) { if (i < int(p.fx.size())) { p.fx[std::size_t(i)].p[std::size_t(slot)] = v; p.fx[std::size_t(i)].set[std::size_t(slot)] = true; } }, 26);
        k->setTarget({ModTarget::fxParam, idx_, slot});
        k->setArcColour(col);
        return k;
    };
    auto addToggle = [&](int slot, const juce::String& label) {
        auto* t = &make<Toggle>(ctx, label, Toggle::Style::check,
            [i = idx_, slot](const Patch& p) { return i < int(p.fx.size()) && p.fx[std::size_t(i)].p[std::size_t(slot)] > 0.5; },
            [i = idx_, slot](Patch& p, bool v) { if (i < int(p.fx.size())) { p.fx[std::size_t(i)].p[std::size_t(slot)] = v ? 1.0 : 0.0; p.fx[std::size_t(i)].set[std::size_t(slot)] = true; } });
        t->setOnColour(col);
        strip_.push_back(t); stripWidths_.push_back(textWidth(label) + 16);
    };
    auto addEnum = [&](int slot, std::vector<juce::String> names, int first) {
        auto* ch = &make<Chooser>();
        ch->setTextColour(col); ch->setCentredText(true);
        ch->buildMenu = [this, slot, names, first](juce::PopupMenu& menu) {
            const int cur = int(std::round(ctx.patch->fx[std::size_t(idx_)].p[std::size_t(slot)]));
            for (std::size_t k = 0; k < names.size(); ++k)
                menu.addItem(names[k], true, cur == first + int(k), [this, slot, first, k] {
                    ctx.editNow([i = idx_, slot, v = first + int(k)](Patch& p) { if (i < int(p.fx.size())) { p.fx[std::size_t(i)].p[std::size_t(slot)] = v; p.fx[std::size_t(i)].set[std::size_t(slot)] = true; } });
                });
        };
        ch->setText(names[0]);
        enumChoosers_.push_back({ch, slot, first, names});
        strip_.push_back(ch); stripWidths_.push_back(92);
    };

    for (int s = 0; s < int(table.size()); ++s) {
        const juce::String name = table[std::size_t(s)].name;
        if (s == wetSlot) continue;
        if (splitter && name.startsWith("count")) continue;
        if (type_ == FxType::comp) {
            const bool mb = m.p[std::size_t(fx::pMultiband)] > 0.5;
            static const char* singleOnly[] = {"ratio", "ratioBelow", "makeup", "thresh"};
            static const char* multiOnly[] = {"xoverLow", "xoverHi", "gain0", "gain1", "gain2", "ratio0", "ratio1", "ratio2", "threshUD0", "threshUD1", "threshUD2"};
            bool hide = name.startsWith("ratioBelow") && name != "ratioBelow" ? true : false;
            for (auto* x : singleOnly) if (name == x && mb) hide = true;
            bool isMulti = false; for (auto* x : multiOnly) if (name == x) isMulti = true;
            if (isMulti && !mb) hide = true;
            if (hide) continue;
        }
        if (type_ == FxType::pump && name == "trigger") { addEnum(s, {"TEMPO", "NOTE", "SIDECHAIN", "FOLLOW"}, 0); continue; }
        if (type_ == FxType::stutter && name == "mode") { addEnum(s, {"GATE", "AUTO"}, 0); continue; }
        if (type_ == FxType::delay && name == "mode") { addEnum(s, {"NORMAL", "PING-PONG"}, 1); continue; }
        if (type_ == FxType::distortion && name == "prePost") { addEnum(s, {"PRE-FILTER", "POST-FILTER"}, 1); continue; }
        if (type_ == FxType::eq && name == "type1") { addEnum(s, {"PEAK", "LOW SHELF"}, 1); continue; }
        if (type_ == FxType::eq && name == "type2") { addEnum(s, {"PEAK", "HIGH SHELF"}, 1); continue; }
        if (isToggleName(name)) { addToggle(s, labelFor(name)); continue; }
        knobs_.push_back(addKnob(s));
    }
    if (wetSlot >= 0) { wet_ = addKnob(wetSlot); wet_->setDiameter(30); }
    if (!splitter) {
        out_ = &make<Knob>(ctx, KnobSpec{"OUT", 0, 1, 0.5, false, false, 1.0, 0, [](double v) { return fmt::db(v * 2.0); }},
            [i = idx_](const Patch& p) { return i < int(p.fx.size()) ? p.fx[std::size_t(i)].p[std::size_t(fxLevelSlot)] : 0.5; },
            [i = idx_](Patch& p, double v) { if (i < int(p.fx.size())) { p.fx[std::size_t(i)].p[std::size_t(fxLevelSlot)] = v; p.fx[std::size_t(i)].set[std::size_t(fxLevelSlot)] = true; } }, 20);
        out_->setTarget({ModTarget::fxParam, idx_, fxLevelSlot});
    }
    // type-specific extras (stored outside the slot table)
    auto extra = [&](std::function<void(juce::PopupMenu&)> menuFn, std::function<juce::String(const FxModule&)> textFn, int width) {
        auto* ch = &make<Chooser>(); ch->setTextColour(col); ch->setCentredText(true); ch->buildMenu = std::move(menuFn);
        extras_.push_back({ch, std::move(textFn)});
        strip_.push_back(ch); stripWidths_.push_back(width);
    };
    if (type_ == FxType::distortion)
        extra([this](juce::PopupMenu& menu) {
            for (int k = 0; k < 17; ++k) menu.addItem(distortionNames[k], true, ctx.patch->fx[std::size_t(idx_)].modeVariant == k,
                [this, k] { ctx.editNow([i = idx_, k](Patch& p) { if (i < int(p.fx.size())) p.fx[std::size_t(i)].modeVariant = k; }); });
        }, [](const FxModule& fm) { return juce::String(distortionNames[std::clamp(fm.modeVariant, 0, 16)]); }, 104);
    if (type_ == FxType::reverb)
        extra([this](juce::PopupMenu& menu) {
            for (int k = 0; k < 4; ++k) menu.addItem(reverbNames[k], true, ctx.patch->fx[std::size_t(idx_)].modeVariant == k,
                [this, k] { ctx.editNow([i = idx_, k](Patch& p) { if (i < int(p.fx.size())) p.fx[std::size_t(i)].modeVariant = k; }); });
        }, [](const FxModule& fm) { return juce::String(reverbNames[std::clamp(fm.modeVariant, 0, 3)]); }, 84);
    if (type_ == FxType::filter)
        extra([this](juce::PopupMenu& menu) {
            juce::PopupMenu group; juce::String groupName;
            auto flush = [&] { if (groupName.isNotEmpty()) menu.addSubMenu(groupName, group); group = juce::PopupMenu(); };
            const auto& cur = ctx.patch->fx[std::size_t(idx_)];
            for (const auto& choice : filterChoices()) {
                if (!choice.serumId) { flush(); groupName = choice.name; continue; }
                group.addItem(choice.name, true, cur.filterResponse == choice.response && cur.filterVariant == choice.variant, [this, choice] {
                    ctx.editNow([i = idx_, choice](Patch& p) { if (i < int(p.fx.size())) { p.fx[std::size_t(i)].filterResponse = choice.response; p.fx[std::size_t(i)].filterVariant = choice.variant; } });
                });
            }
            flush();
        }, [](const FxModule& fm) {
            for (const auto& c : filterChoices()) if (c.serumId && c.response == fm.filterResponse && c.variant == fm.filterVariant) return juce::String(c.name);
            return juce::String("LOW 12");
        }, 120);
    if (type_ == FxType::conv)
        extra([this](juce::PopupMenu& menu) {
            menu.addItem("BROWSE IMPULSES...", [this] {
                if (ctx.browse) ctx.browse("IMPULSE BROWSER", contentRoot(*ctx.patch).getChildFile("Impulses"), "*.wav;*.flac;*.aif;*.aiff", juce::File(juce::String(ctx.patch->fx[std::size_t(idx_)].impulsePath)),
                    [this](const juce::File& f) { ctx.proc.setFxImpulseFile(idx_, f); ctx.refreshPatch(); });
            });
        }, [](const FxModule& fm) { return fm.impulsePath.empty() ? juce::String("BUILT-IN ROOM") : prettyAssetName(fm.impulsePath); }, 140);
    if (type_ == FxType::filter) {}   // filter x/y controls stay as knobs
}

int FxModulePanel::heightForWidth(int w) const {
    const int titleW = 96, vizW = std::clamp(w * 16 / 100, 120, 190);
    const int rightW = isSplitter(type_) ? (wet_ ? 60 : 0) : 100;
    const int ctlW = std::max(56, w - titleW - vizW - rightW - 20);
    const int cols = std::max(1, ctlW / 56);
    const int rows = knobs_.empty() ? 0 : (int(knobs_.size()) + cols - 1) / cols;
    int stripRows = 0;
    if (!strip_.empty()) {
        int x = 0; stripRows = 1;
        for (int wdt : stripWidths_) { if (x + wdt > ctlW && x > 0) { ++stripRows; x = 0; } x += wdt + 6; }
    }
    return std::max(74, 10 + rows * 46 + stripRows * 19 + 6);
}

void FxModulePanel::refresh(const Patch& p) {
    if (idx_ >= int(p.fx.size())) return;
    const auto& m = p.fx[std::size_t(idx_)];
    enabled_ = m.enabled;
    Panel::refresh(p);
    enable_->setLabel(enabled_ ? "ON" : "OFF");
    for (auto& e : enumChoosers_) {
        const int cur = int(std::round(m.p[std::size_t(e.slot)])) - e.first;
        e.c->setText(e.names[std::size_t(std::clamp(cur, 0, int(e.names.size()) - 1))]);
    }
    for (auto& e : extras_) e.c->setText(e.text(m));
    for (auto* k : knobs_) k->setDimmed(!enabled_);
    if (wet_) wet_->setDimmed(!enabled_);
    if (out_) out_->setDimmed(!enabled_);
    repaint();
}

void FxModulePanel::resized() {
    const int w = getWidth(), h = getHeight();
    const int titleW = 96, vizW = std::clamp(w * 16 / 100, 120, 190);
    const bool splitter = isSplitter(type_);
    const int rightW = splitter ? (wet_ ? 60 : 0) : 100;
    enable_->setBounds(8, h - 20, 36, 10);
    viz_->setBounds(titleW + 2, 6, vizW, h - 12);
    const int ctlX = titleW + vizW + 10, ctlW = std::max(56, w - ctlX - rightW - 10);
    const int cols = std::max(1, ctlW / 56);
    for (std::size_t i = 0; i < knobs_.size(); ++i)
        knobs_[i]->setBounds(ctlX + int(i % std::size_t(cols)) * 56, 6 + int(i / std::size_t(cols)) * 46, 56, 46);
    const int rows = knobs_.empty() ? 0 : (int(knobs_.size()) + cols - 1) / cols;
    int x = 0, y = 10 + rows * 46;
    for (std::size_t i = 0; i < strip_.size(); ++i) {
        if (x + stripWidths_[i] > ctlW && x > 0) { x = 0; y += 19; }
        strip_[i]->setBounds(ctlX + x, y, stripWidths_[i], 14);
        x += stripWidths_[i] + 6;
    }
    if (wet_) wet_->setBounds(w - rightW + 4 - (splitter ? 4 : 0), 8, 54, 50);
    if (out_) out_->setBounds(w - 46, 12, 42, 42);
}

void FxModulePanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const auto col = fxColour(type_);
    const float sel = selFade_.v();
    drawModuleFrame(g, r, mix(pal::panel, pal::chassis, enabled_ ? 0.0f : 0.5f));
    if (sel > 0.02f) zyg::ui::frame(g, r, pal::violetHot.withAlpha(sel));
    // title block: coloured rails above/below the effect name
    const auto tc = enabled_ ? col : pal::textMuted;
    g.setColour(tc.withAlpha(0.9f)); g.fillRect(6, 10, 84, 2); g.fillRect(6, r.getHeight() - 26, 84, 2);
    // effect identity as a glyph (the name stays in the rack list and menus)
    drawFxGlyph(g, type_, {6, 13, 84, r.getHeight() - 26 - 13 - 1}, tc, enabled_ ? mix(tc, pal::panel, 0.55f) : pal::edgeMid);
    vLine(g, 94, 6, r.getHeight() - 12, pal::edgeDark);
    if (!isSplitter(type_)) {
        vLine(g, r.getWidth() - 104, 6, r.getHeight() - 12, pal::edgeDark);
        if (wet_) drawTextIn(g, "MIX", {r.getWidth() - 100, 0, 54, 8}, pal::textMuted, juce::Justification::centred);
    }
}

// ===================================================================== RackView
class RackView final : public Panel {
public:
    explicit RackView(UiContext& c) : Panel(c) {}
    void setRack(int rack) { rack_ = rack; signature_.clear(); scroll_ = 0; }
    void refresh(const Patch& p) override {
        juce::String sig;
        const auto rows = flattenRack(p, rack_);
        for (const auto& row : rows) {
            if (row.bandHeader) continue;
            const auto& m = p.fx[std::size_t(row.index)];
            sig << row.index << ":" << int(m.fxType) << ":" << row.depth << ":" << (m.fxType == FxType::comp ? int(m.p[std::size_t(fx::pMultiband)] > 0.5) : 0) << ";";
        }
        if (sig != signature_) { signature_ = sig; rebuild(p, rows); }
        Panel::refresh(p);
    }
    void selectFx(int idx, bool scrollTo) {
        selected_ = idx;
        for (auto* pn : modules_) pn->setSelected(pn->fxIndex() == idx);
        if (scrollTo) for (auto* pn : modules_) if (pn->fxIndex() == idx) {
            const int top = pn->getY() + scroll_, bottom = top + pn->getHeight();
            if (top < scroll_) scroll_ = top; else if (bottom > scroll_ + getHeight()) scroll_ = bottom - getHeight();
            layoutModules();
        }
    }
    std::function<void(int)> onSelect;
    void resized() override { layoutModules(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scrollBy(int(-w.deltaY * 110.0f)); }
    void mouseDown(const juce::MouseEvent& e) override { if (e.x >= getWidth() - 8) dragScroll(e.y); }
    void mouseDrag(const juce::MouseEvent& e) override { if (e.getMouseDownX() >= getWidth() - 8) dragScroll(e.y); }
    void paint(juce::Graphics& g) override {
        g.setColour(pal::chassis); g.fillRect(getLocalBounds());
        if (modules_.empty()) {
            drawTextIn(g, "EMPTY RACK - USE + FX TO ADD AN EFFECT", getLocalBounds(), pal::textMuted, juce::Justification::centred);
            return;
        }
        if (total_ > getHeight()) {   // scrollbar
            const int th = std::max(16, getHeight() * getHeight() / total_);
            const int ty = scroll_ * (getHeight() - th) / std::max(1, total_ - getHeight());
            g.setColour(pal::sunken); g.fillRect(getWidth() - 6, 0, 6, getHeight());
            bevelBox(g, {getWidth() - 6, ty, 6, th}, pal::raisedHi);
        }
    }
    void paintOverChildren(juce::Graphics& g) override {
        // nested band rails
        for (auto* pn : modules_) if (depthOf(pn) > 0) {
            g.setColour(pal::violet); g.fillRect(pn->getX() - 6, pn->getY(), 2, pn->getHeight());
        }
    }
private:
    int depthOf(FxModulePanel* pn) const { auto it = depths_.find(pn); return it == depths_.end() ? 0 : it->second; }
    void scrollBy(int d) { scroll_ = std::clamp(scroll_ + d, 0, std::max(0, total_ - getHeight())); layoutModules(); }
    void dragScroll(int y) { scroll_ = std::clamp(y * std::max(1, total_ - getHeight()) / std::max(1, getHeight()), 0, std::max(0, total_ - getHeight())); layoutModules(); }
    void rebuild(const Patch& p, const std::vector<FxRow>& rows) {
        clearChildren(); modules_.clear(); depths_.clear();
        for (const auto& row : rows) {
            if (row.bandHeader) continue;
            auto& pn = make<FxModulePanel>(ctx, row.index, row.depth / 2);
            pn.onSelect = [this](int i) { if (onSelect) onSelect(i); };
            modules_.push_back(&pn); depths_[&pn] = row.depth / 2;
            pn.setSelected(row.index == selected_);
        }
        (void)p;
        layoutModules();
        if (isVisible() && !modules_.empty()) {   // cascade entrance
            int i = 0;
            for (auto* pn : modules_) { pn->enter(); ++i; }
        }
    }
    void layoutModules() {
        const int w = getWidth() - 12;
        int y = -scroll_ + 2;
        total_ = 2;
        for (auto* pn : modules_) {
            const int indent = depthOf(pn) * 16;
            const int h = pn->heightForWidth(w - indent);
            pn->setBounds(4 + indent, y, w - indent, h);
            y += h + 3; total_ += h + 3;
        }
        repaint();
    }
    int rack_ = 0, scroll_ = 0, total_ = 0, selected_ = -1;
    juce::String signature_;
    std::vector<FxModulePanel*> modules_;
    std::map<FxModulePanel*, int> depths_;
};

// ====================================================================== FxList
class FxList final : public juce::Component {
public:
    FxList(UiContext& c, FxPage& page) : ctx_(c), page_(page) {}
    void refresh(const Patch& p) { rows_ = flattenRack(p, ctx_.selectedFxRack); patch_ = ctx_.patch; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent& e) override { const int r = rowAt(e.y); if (r != hover_) { hover_ = r; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        scroll_ = std::clamp(scroll_ - int(w.deltaY * 60.0f), 0, std::max(0, int(rows_.size()) * 15 - getHeight() + 4)); repaint();
    }
    int selected = -1;
    std::function<void(int)> onSelect;
    std::function<void(int, int)> onAdd;   // splitter, band
private:
    int rowAt(int y) const { const int r = (y + scroll_ - 2) / 15; return r >= 0 && r < int(rows_.size()) ? r : -1; }
    UiContext& ctx_;
    FxPage& page_;
    std::vector<FxRow> rows_;
    std::shared_ptr<const Patch> patch_;
    int hover_ = -1, scroll_ = 0;
};

void FxList::paint(juce::Graphics& g) {
    wellBox(g, getLocalBounds(), pal::sunken);
    g.saveState(); g.reduceClipRegion(getLocalBounds().reduced(1));
    const int w = getWidth();
    if (rows_.empty()) drawTextIn(g, "NO EFFECTS IN THIS RACK", getLocalBounds(), pal::textMuted, juce::Justification::centred);
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];
        const int y = 2 + int(i) * 15 - scroll_;
        if (y > getHeight() || y < -15) continue;
        const int x0 = 4 + row.depth * 8;
        const bool hov = int(i) == hover_;
        const auto& m = patch_->fx[std::size_t(row.index)];
        if (row.bandHeader) {
            drawIconCentred(g, Icon::plus, {x0, y, 9, 14}, hov ? pal::acid : pal::textMuted);
            drawTextIn(g, bandName(m.fxType, row.band), {x0 + 12, y, w - x0 - 20, 14}, hov ? pal::textBody : pal::textMuted);
            continue;
        }
        const bool sel = row.index == selected;
        if (sel || hov) { g.setColour(sel ? pal::violetShadow : pal::raised); g.fillRect(1, y, w - 2, 14); }
        g.setColour(m.enabled ? fxColour(m.fxType) : pal::textMuted); g.fillRect(x0 - 2, y + 2, 2, 10);
        drawTextIn(g, fxDisplayName(m.fxType), {x0 + 3, y, w - x0 - 60, 14}, m.enabled ? (sel ? pal::textHi : pal::textBody) : pal::textMuted, juce::Justification::centredLeft, 1, sel);
        if (hov || sel) {
            drawIconCentred(g, Icon::bypass, {w - 60, y, 10, 14}, m.enabled ? pal::acid : pal::crimson);
            drawIconCentred(g, Icon::arrowUp, {w - 48, y, 10, 14}, pal::textBody);
            drawIconCentred(g, Icon::arrowDown, {w - 36, y, 10, 14}, pal::textBody);
            drawIconCentred(g, Icon::close, {w - 22, y, 12, 14}, pal::crimson);
        } else if (!m.enabled) drawIconCentred(g, Icon::bypass, {w - 60, y, 10, 14}, pal::crimsonDim);
    }
    g.restoreState();
}

void FxList::mouseDown(const juce::MouseEvent& e) {
    const int r = rowAt(e.y);
    if (r < 0) return;
    const auto row = rows_[std::size_t(r)];
    if (row.bandHeader) { if (onAdd) onAdd(row.index, row.band); return; }
    const int w = getWidth();
    const int idx = row.index;
    if (e.x >= w - 62 && e.x < w - 50) { ctx_.editNow([idx](Patch& p) { if (idx < int(p.fx.size())) p.fx[std::size_t(idx)].enabled = !p.fx[std::size_t(idx)].enabled; }); return; }
    if (e.x >= w - 50 && e.x < w - 38) { ctx_.editNow([idx](Patch& p) { moveFxModule(p, idx, -1); }); return; }
    if (e.x >= w - 38 && e.x < w - 26) { ctx_.editNow([idx](Patch& p) { moveFxModule(p, idx, 1); }); return; }
    if (e.x >= w - 26) { ctx_.editNow([idx](Patch& p) { removeFxModule(p, idx); }); if (onSelect) onSelect(-1); return; }
    if (onSelect) onSelect(idx);
}

// ====================================================================== FxPage
FxPage::FxPage(UiContext& c) : Panel(c) {
    static const char* names[3] = {"MAIN", "BUS 1", "BUS 2"};
    for (int i = 0; i < 3; ++i) {
        auto* t = &make<TabButton>();
        t->setText(names[i]); t->setAccent(pal::amber); t->setLedShown(false);
        t->onClick = [this, i] {
            ctx.selectedFxRack = i; selectedFx_ = -1;
            rack_->setRack(i);
            if (ctx.patch) refresh(*ctx.patch);
        };
        tabs_[std::size_t(i)] = t;
    }
    add_ = &make<PixelButton>("+ FX"); add_->setBold(true); add_->setAccent(pal::acid);
    add_->onClick = [this] { showAddMenu(-1, 0); };
    clear_ = &make<PixelButton>("CLEAR"); clear_->setBold(true);
    clear_->onClick = [this] {
        ctx.editNow([rack = ctx.selectedFxRack](Patch& p) {
            std::vector<int> idx;
            for (std::size_t i = 0; i < p.fx.size(); ++i) if (p.fx[i].rack == rack) idx.push_back(int(i));
            for (auto it = idx.rbegin(); it != idx.rend(); ++it) removeFxModule(p, *it);
        });
    };
    list_ = &make<FxList>(ctx, *this);
    list_->onSelect = [this](int i) { selectedFx_ = i; list_->selected = i; rack_->selectFx(i, true); list_->repaint(); };
    list_->onAdd = [this](int split, int band) { showAddMenu(split, band); };
    rack_ = &make<RackView>(ctx);
    rack_->onSelect = [this](int i) { selectedFx_ = i; list_->selected = i; rack_->selectFx(i, false); list_->repaint(); };
}

void FxPage::showAddMenu(int splitter, int band) {
    juce::PopupMenu m;
    struct T { FxType t; }; 
    const FxType plain[] = {FxType::bode, FxType::chorus, FxType::comp, FxType::conv, FxType::delay, FxType::distortion, FxType::eq, FxType::filter,
                            FxType::flanger, FxType::hyperD, FxType::phaser, FxType::reverb, FxType::utils};
    const FxType zyg[] = {FxType::pump, FxType::stutter};
    const bool nestedTooDeep = splitter >= 0 && [&] {
        // allow at most two splitter levels (matches the engine)
        int depth = 0;
        for (const auto& row : flattenRack(*ctx.patch, ctx.selectedFxRack)) if (row.index == splitter && row.bandHeader) depth = row.depth;
        return depth >= 3;
    }();
    for (auto t : plain) m.addItem(fxDisplayName(t), [this, t, splitter, band] { ctx.editNow([rack = ctx.selectedFxRack, t, splitter, band](Patch& p) { addFxModule(p, rack, t, splitter, band); }); });
    m.addSeparator();
    for (auto t : zyg) m.addItem(juce::String(fxDisplayName(t)) + "  (ZYG)", [this, t, splitter, band] { ctx.editNow([rack = ctx.selectedFxRack, t, splitter, band](Patch& p) { addFxModule(p, rack, t, splitter, band); }); });
    m.addSeparator();
    for (auto t : {FxType::split, FxType::split3, FxType::splitMS})
        m.addItem(fxDisplayName(t), !nestedTooDeep, false, [this, t, splitter, band] { ctx.editNow([rack = ctx.selectedFxRack, t, splitter, band](Patch& p) { addFxModule(p, rack, t, splitter, band); }); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add_).withStandardItemHeight(13));
}

void FxPage::refresh(const Patch& p) {
    int counts[3] = {0, 0, 0};
    for (const auto& m : p.fx) if (m.rack >= 0 && m.rack < 3) ++counts[m.rack];
    for (int i = 0; i < 3; ++i) { tabs_[std::size_t(i)]->setSelected(i == ctx.selectedFxRack); tabs_[std::size_t(i)]->setBadge(counts[i]); }
    list_->refresh(p);
    rack_->refresh(p);
    for (auto* b : {add_, clear_}) (void)b;
}

void FxPage::resized() {
    const int w = getWidth(), h = getHeight();
    const int sideW = 216;
    for (int i = 0; i < 3; ++i) tabs_[std::size_t(i)]->setBounds(3 + i * (sideW / 3), 2, sideW / 3, 20);
    add_->setBounds(3, 26, 80, 18); clear_->setBounds(87, 26, 60, 18);
    list_->setBounds(3, 48, sideW, h - 52);
    rack_->setBounds(sideW + 8, 2, w - sideW - 11, h - 4);
}

void FxPage::paint(juce::Graphics& g) {
    g.setColour(pal::chassis); g.fillRect(getLocalBounds());
    vLine(g, 222, 2, getHeight() - 4, pal::edgeMid);
}

}
