#include "MixPage.h"
#include "../FxParams.h"

namespace zyg::ui {
namespace {
const juce::Colour oscBody = mix(pal::panel, pal::acidShadow, 0.16f);
const juce::Colour oscHeader = mix(pal::raised, pal::acidShadow, 0.55f);
const juce::Colour filterBody = mix(pal::panel, pal::violetShadow, 0.30f);
const juce::Colour filterHeader = mix(pal::raised, pal::violetShadow, 0.65f);
const juce::Colour busBody = mix(pal::panel, pal::chassis, 0.35f);
}

MixStrip::MixStrip(UiContext& c, Kind kind, int index, juce::String name) : Panel(c), kind_(kind), index_(index), name_(std::move(name)) {
    const int ri = routeIndex();
    if (kind_ == Kind::osc) {
        enable_ = &make<Toggle>(ctx, "", Toggle::Style::led, [this](const Patch& p) { return p.oscillators[std::size_t(index_)].enabled; },
            [this](Patch& p, bool v) { auto& o = p.oscillators[std::size_t(index_)]; o.enabled = v;
                if (index_ == 4) o.mode = OscMode::sub; if (index_ == 3) o.mode = OscMode::noise;
                if (v && p.routes[std::size_t(index_)].target == RouteTarget::unknown) p.routes[std::size_t(index_)].target = RouteTarget::main; });
        dest_ = &make<Chooser>(); dest_->setCentredText(true); dest_->setTextColour(pal::acidHot);
        dest_->buildMenu = [this](juce::PopupMenu& m) { buildRouteMenu(ctx, m, index_); };
    } else if (kind_ == Kind::filter) {
        enable_ = &make<Toggle>(ctx, "", Toggle::Style::led, [this](const Patch& p) { return p.filters[std::size_t(index_)].enabled; },
            [this](Patch& p, bool v) { p.filters[std::size_t(index_)].enabled = v; });
        enable_->setOnColour(pal::violetHot);
        dest_ = &make<Chooser>(); dest_->setCentredText(true); dest_->setTextColour(pal::acidHot);
        dest_->buildMenu = [this, ri](juce::PopupMenu& m) {
            struct It { const char* n; RouteTarget t; };
            const It items[] = {{index_ == 0 ? "FILTER 2" : "FILTER 1", RouteTarget::filter}, {"MAIN", RouteTarget::main}, {"DIRECT", RouteTarget::direct}, {"OFF", RouteTarget::none}};
            for (const auto& it : items)
                m.addItem(it.n, true, ctx.patch->routes[std::size_t(ri)].target == it.t,
                    [this, ri, it] { ctx.editNow([ri, it](Patch& p) { p.routes[std::size_t(ri)].target = it.t; }); });
        };
        miniFilter_ = &make<FilterDisplay>(ctx, index_, true);
    } else if (kind_ == Kind::bus) {
        dest_ = &make<Chooser>(); dest_->setCentredText(true); dest_->setTextColour(pal::acidHot);
        dest_->buildMenu = [this](juce::PopupMenu& m) {
            const int cur = index_ == 1 ? ctx.patch->globals.fxBus1Dest : ctx.patch->globals.fxBus2Dest;
            m.addItem("MAIN", true, cur != 2, [this] { ctx.editNow([i = index_](Patch& p) { (i == 1 ? p.globals.fxBus1Dest : p.globals.fxBus2Dest) = 1; }); });
            if (index_ == 1) m.addItem("BUS 2", true, cur == 2, [this] { ctx.editNow([](Patch& p) { p.globals.fxBus1Dest = 2; }); });
        };
    }
    if (kind_ == Kind::osc || kind_ == Kind::filter) {
        if (kind_ == Kind::osc) {
            balance_ = &make<Knob>(ctx, KnobSpec{"F1 . F2", -100, 100, 0, true, false, 1.0, 0, [](double v) { return std::abs(v) < 0.5 ? juce::String("BOTH") : (v < 0 ? "F1 " : "F2 ") + formatNumber(std::abs(v), 0) + "%"; }},
                [ri](const Patch& p) { return p.routes[std::size_t(ri)].filterBalance; }, [ri](Patch& p, double v) { p.routes[std::size_t(ri)].filterBalance = v; }, 28);
            balance_->setTarget({ModTarget::routingParam, ri, int(RoutingParam::filterBalance)});
        }
        auto pct = [](double v) { return formatNumber(v, 0) + "%"; };
        send1_ = &make<Knob>(ctx, KnobSpec{"1", 0, 100, 0, false, false, 1.0, 0, pct},
            [ri](const Patch& p) { return p.routes[std::size_t(ri)].fxBus1Level; }, [ri](Patch& p, double v) { p.routes[std::size_t(ri)].fxBus1Level = v; }, 22);
        send2_ = &make<Knob>(ctx, KnobSpec{"2", 0, 100, 0, false, false, 1.0, 0, pct},
            [ri](const Patch& p) { return p.routes[std::size_t(ri)].fxBus2Level; }, [ri](Patch& p, double v) { p.routes[std::size_t(ri)].fxBus2Level = v; }, 22);
        send1_->setTarget({ModTarget::routingParam, ri, int(RoutingParam::fxBus1Level)});
        send2_->setTarget({ModTarget::routingParam, ri, int(RoutingParam::fxBus2Level)});
    }
    if (kind_ == Kind::osc) {
        pan_ = &make<Knob>(ctx, KnobSpec{"PAN", -1, 1, 0, true, false, 1.0, 0, fmt::pan},
            [this](const Patch& p) { return p.oscillators[std::size_t(index_)].pan; }, [this](Patch& p, double v) { p.oscillators[std::size_t(index_)].pan = v; }, 24);
        pan_->setTarget({ModTarget::oscParam, index_, int(OscParam::pan)});
    }
    // faders: normalised 0..1 travel
    Getter get; Setter set; double top = 1.0, def = 0.75;
    switch (kind_) {
        case Kind::osc: get = [this](const Patch& p) { return p.oscillators[std::size_t(index_)].volume; };
            set = [this](Patch& p, double v) { p.oscillators[std::size_t(index_)].volume = v; }; break;
        case Kind::filter: top = 2.0; def = 0.5;
            get = [this](const Patch& p) { return p.filters[std::size_t(index_)].level; };
            set = [this](Patch& p, double v) { p.filters[std::size_t(index_)].level = v; }; break;
        case Kind::bus: top = 2.0; def = 0.5;
            get = [this](const Patch& p) { return (index_ == 1 ? p.globals.fxBus1Vol : p.globals.fxBus2Vol) / 2.0; };
            set = [this](Patch& p, double v) { (index_ == 1 ? p.globals.fxBus1Vol : p.globals.fxBus2Vol) = v * 2.0; }; break;
        case Kind::main: def = 0.7;
            get = [](const Patch& p) { return p.masterVolume; }; set = [](Patch& p, double v) { p.masterVolume = v; }; break;
        case Kind::direct: top = 2.0; def = 0.5;
            get = [](const Patch& p) { return p.globals.directVol / 2.0; }; set = [](Patch& p, double v) { p.globals.directVol = v * 2.0; }; break;
    }
    fader_ = &make<Fader>(ctx, std::move(get), std::move(set), def);
    fader_->setTop(top);
    switch (kind_) {
        case Kind::osc: fader_->setTarget({ModTarget::oscParam, index_, int(OscParam::volume)}); break;
        case Kind::filter: fader_->setTarget({ModTarget::filterParam, index_, int(FilterParam::level)}); break;
        case Kind::bus: fader_->setTarget({ModTarget::globalParam, 0, int(index_ == 1 ? GlobalParam::fxBus1Vol : GlobalParam::fxBus2Vol)}); break;
        case Kind::main: fader_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::masterVolume)}); break;
        case Kind::direct: fader_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::directVol)}); break;
    }
    fader_->setMetersShown(kind_ == Kind::main);
}

void MixStrip::refresh(const Patch& p) {
    Panel::refresh(p);
    fxNames_.clear();
    if (kind_ == Kind::bus || kind_ == Kind::main) {
        const int rack = kind_ == Kind::main ? 0 : index_;
        std::vector<const FxModule*> mods;
        for (const auto& m : p.fx) if (m.rack == rack) mods.push_back(&m);
        std::sort(mods.begin(), mods.end(), [](auto* a, auto* b) { return a->position < b->position; });
        for (auto* m : mods) fxNames_.add(juce::String(zyg::fxTypeName(m->fxType)).toUpperCase() + (m->enabled ? "" : " (OFF)"));
    }
    juce::String d;
    if (kind_ == Kind::osc || kind_ == Kind::filter) {
        const auto& r = p.routes[std::size_t(routeIndex())];
        if (kind_ == Kind::filter && r.target == RouteTarget::filter) d = index_ == 0 ? "FILTER 2" : "FILTER 1";
        else d = routeLabel(r);
        if (kind_ == Kind::osc) { enabled_ = p.oscillators[std::size_t(index_)].enabled; }
        else enabled_ = p.filters[std::size_t(index_)].enabled;
        const bool toFilter = r.target == RouteTarget::filter;
        if (balance_) balance_->setDimmed(!toFilter || !enabled_);
        if (send1_) { send1_->setDimmed(!enabled_); send2_->setDimmed(!enabled_); }
        if (pan_) pan_->setDimmed(!enabled_);
    } else if (kind_ == Kind::bus) {
        d = (index_ == 1 ? p.globals.fxBus1Dest : p.globals.fxBus2Dest) == 2 ? "BUS 2" : "MAIN";
    }
    if (dest_) dest_->setText(d);
    if (dest_) dest_->setDimmed(!enabled_);
    fader_->setDimmed(!enabled_);
    repaint();
}

void MixStrip::frame() {
    if (kind_ == Kind::main) fader_->setLevels(ctx.proc.getOutputPeakLeft(), ctx.proc.getOutputPeakRight());
    Panel::frame();
}

void MixStrip::resized() {
    const int w = getWidth(), h = getHeight();
    if (enable_) enable_->setBounds(5, 6, 10, 8);
    if (dest_) dest_->setBounds(3, 22, w - 6, 14);
    if (miniFilter_) miniFilter_->setBounds(3, 40, w - 6, 46);
    if (balance_) balance_->setBounds(0, 40, w, 48);
    const int sendY = 92;
    if (send1_) { send1_->setBounds(4, sendY, w / 2 - 6, 34); send2_->setBounds(w / 2 + 2, sendY, w / 2 - 6, 34); }
    const int faderTop = std::max(kind_ == Kind::osc || kind_ == Kind::filter ? 132 : 44, h - 168);
    if (pan_) pan_->setBounds(2, h - 62 - 2, w / 2 - 4, 40);
    const int fx = (kind_ == Kind::osc ? w / 2 - 4 : 6);
    fader_->setBounds(fx, faderTop, w - fx - 3, h - faderTop - 4);
    if (kind_ == Kind::main) fader_->setBounds(6, faderTop, w - 9, h - faderTop - 4);
}

void MixStrip::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const bool osc = kind_ == Kind::osc, filt = kind_ == Kind::filter;
    drawModuleFrame(g, r, osc ? (enabled_ ? oscBody : mix(pal::panel, pal::chassis, 0.5f)) : filt ? filterBody : busBody);
    g.setColour(osc ? (enabled_ ? oscHeader : pal::raised) : filt ? filterHeader : pal::raised); g.fillRect(1, 1, r.getWidth() - 2, 20);
    hLine(g, 1, 21, r.getWidth() - 2, pal::edgeDark);
    const int tx = enable_ ? 17 : 6;
    drawTextIn(g, name_, {tx, 1, r.getWidth() - tx - 2, 20}, enabled_ ? pal::textHi : pal::textMuted, juce::Justification::centredLeft, 1, true);
    if (osc || filt) {
        drawTextIn(g, "BUS", {0, 92 + 12, r.getWidth(), 8}, pal::textMuted, juce::Justification::centred);
        hLine(g, 4, 130, r.getWidth() - 8, pal::edgeDark);
    }
    if (kind_ == Kind::bus || kind_ == Kind::main) {
        int y = 44;
        for (const auto& n : fxNames_) {
            if (y > r.getBottom() - 180) { drawText(g, "...", 8, y, pal::textMuted); break; }
            g.setColour(pal::violet); g.fillRect(4, y, 2, 7);
            drawTextIn(g, n, {9, y - 1, r.getWidth() - 12, 9}, n.endsWith("(OFF)") ? pal::textMuted : pal::textBody);
            y += 12;
        }
        if (fxNames_.isEmpty()) drawTextIn(g, "NO EFFECTS", {2, 44, r.getWidth() - 4, 9}, pal::textMuted, juce::Justification::centred);
    }
    if (kind_ == Kind::direct) drawTextIn(g, "BYPASSES FX", {2, 44, r.getWidth() - 4, 9}, pal::textMuted, juce::Justification::centred);
}

MixPage::MixPage(UiContext& c) : Panel(c) {
    struct S { MixStrip::Kind k; int i; const char* n; };
    const S list[] = {{MixStrip::Kind::osc, 4, "SUB"}, {MixStrip::Kind::osc, 0, "OSC A"}, {MixStrip::Kind::osc, 1, "OSC B"},
                      {MixStrip::Kind::osc, 2, "OSC C"}, {MixStrip::Kind::osc, 3, "NOISE"}, {MixStrip::Kind::filter, 0, "FILTER 1"},
                      {MixStrip::Kind::filter, 1, "FILTER 2"}, {MixStrip::Kind::bus, 1, "BUS 1"}, {MixStrip::Kind::bus, 2, "BUS 2"},
                      {MixStrip::Kind::main, 0, "MAIN"}, {MixStrip::Kind::direct, 0, "DIRECT"}};
    for (const auto& s : list) strips_.push_back(&make<MixStrip>(ctx, s.k, s.i, s.n));
}

void MixPage::resized() {
    const int w = getWidth() - 6, n = int(strips_.size());
    for (int i = 0; i < n; ++i) {
        const int x0 = 3 + i * w / n, x1 = 3 + (i + 1) * w / n;
        strips_[std::size_t(i)]->setBounds(x0, 0, x1 - x0 - 2, getHeight());
    }
}

}
