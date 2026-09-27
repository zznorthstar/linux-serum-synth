#include "MatrixPage.h"
#include <cmath>

namespace zyg::ui {
namespace {
constexpr int rowH = 24, headH = 16;

ModulationRoute& ensureRoute(Patch& p, int ri) {
    while (int(p.modulation.size()) <= ri) { ModulationRoute r; r.slot = int(p.modulation.size()); p.modulation.push_back(std::move(r)); }
    return p.modulation[std::size_t(ri)];
}
void drawCurveGlyph(juce::Graphics& g, juce::Rectangle<int> r, double bend, juce::Colour c) {
    std::vector<juce::Point<int>> pts;
    const double k = -bend / 100.0 * 2.2;
    for (int x = 0; x < r.getWidth(); ++x) {
        const double t = double(x) / double(std::max(1, r.getWidth() - 1));
        const double v = std::abs(k) < 1e-3 ? t : (std::exp(k * t) - 1.0) / (std::exp(k) - 1.0);
        pts.push_back({r.getX() + x, r.getBottom() - 1 - int(std::round(v * (r.getHeight() - 1)))});
    }
    pixelPolyline(g, pts, c);
}
}

// ================================================================ CurveButton
void CurveButton::commit(double v) {
    v = juce::jlimit(-100.0, 100.0, v);
    if (v == value_) return;
    value_ = v;
    ctx_.edit(this, [set = set_, v](Patch& p) { set(p, v); });
    repaint();
}

void CurveButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { commit(0); ctx_.flush(); return; }
    juce::PopupMenu m;
    struct C { const char* n; double v; };
    for (auto c : {C{"EASE IN 2", -100}, C{"EASE IN", -50}, C{"LINEAR", 0}, C{"EASE OUT", 50}, C{"EASE OUT 2", 100}})
        m.addItem(c.n, true, std::abs(value_ - c.v) < 1, [this, c] { commit(c.v); ctx_.flush(); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withStandardItemHeight(13));
}

void CurveButton::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const float h = hoverF_.v();
    wellBox(g, r, mix(pal::sunken, pal::raised, h));
    drawCurveGlyph(g, r.reduced(5), value_, dim_ ? pal::textMuted : mix(pal::textBody, pal::acid, std::abs(float(value_)) > 1 ? 1.0f : h));
}

// ================================================================== MatrixRow
MatrixRow::MatrixRow(UiContext& c, int routeIndex) : Panel(c), ri_(routeIndex) {
    const int ri = routeIndex;
    auto get = [ri](const Patch& p, auto fn) { return ri < int(p.modulation.size()) ? fn(p.modulation[std::size_t(ri)]) : 0.0; };
    source_ = &make<Chooser>(); source_->setTextColour(pal::violetHot); source_->setBold(true);
    source_->setPlaceholderStyle(false);
    curveIn_ = &make<CurveButton>(ctx, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.curveIn; }); },
        [ri](Patch& p, double v) { ensureRoute(p, ri).curveIn = v; });
    amount_ = &make<HSlider>(ctx, -100.0, 100.0, 0.0, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.amount; }); },
        [ri](Patch& p, double v) { ensureRoute(p, ri).amount = v; });
    bipolar_ = &make<Toggle>(ctx, "", Toggle::Style::block, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.bipolar ? 1.0 : 0.0; }) > 0.5; },
        [ri](Patch& p, bool v) { ensureRoute(p, ri).bipolar = v; });
    bipolar_->setIcon(Icon::fold);
    dest_ = &make<Chooser>(); dest_->setTextColour(pal::acidHot);
    aux_ = &make<Chooser>(); aux_->setTextColour(pal::amber);
    invert_ = &make<Toggle>(ctx, "", Toggle::Style::block, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.auxInverted ? 1.0 : 0.0; }) > 0.5; },
        [ri](Patch& p, bool v) { ensureRoute(p, ri).auxInverted = v; });
    invert_->setIcon(Icon::arrowUp);
    curveOut_ = &make<CurveButton>(ctx, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.curveOut; }); },
        [ri](Patch& p, double v) { ensureRoute(p, ri).curveOut = v; });
    smooth_ = &make<HSlider>(ctx, 0.0, 100.0, 0.0, [get](const Patch& p) { return get(p, [](const ModulationRoute& r) { return r.smoothRise; }); },
        [ri](Patch& p, double v) { auto& r = ensureRoute(p, ri); r.smoothRise = v; r.smoothFall = v; });
    smooth_->setColours(pal::amber, pal::amber);
    bypassBtn_ = &make<PixelButton>(); bypassBtn_->setIcon(Icon::bypass); bypassBtn_->setFlat(true); bypassBtn_->setAccent(pal::crimson);
    bypassBtn_->onClick = [this] { if (exists()) editRoute([](ModulationRoute& r) { r.bypass = !r.bypass; }); };
    del_ = &make<PixelButton>(); del_->setIcon(Icon::close); del_->setFlat(true); del_->setAccent(pal::crimson);
    del_->onClick = [this] { if (exists()) ctx.editNow([ri = ri_](Patch& p) { if (ri < int(p.modulation.size())) p.modulation.erase(p.modulation.begin() + ri); }); };
    source_->customOpen = [this] { showSourceMenu(false); };
    aux_->customOpen = [this] { showSourceMenu(true); };
    dest_->customOpen = [this] { showDestMenu(); };
}

void MatrixRow::editRoute(std::function<void(ModulationRoute&)> fn) {
    ctx.editNow([ri = ri_, fn = std::move(fn)](Patch& p) { fn(ensureRoute(p, ri)); });
}

void MatrixRow::showSourceMenu(bool aux) {
    juce::PopupMenu m;
    std::map<juce::String, juce::PopupMenu> groups;
    std::vector<juce::String> order;
    const auto& r = ctx.patch->modulation;
    const ModSource curKind = exists() ? (aux ? r[std::size_t(ri_)].auxKind : r[std::size_t(ri_)].sourceKind) : ModSource::unknown;
    const int curIdx = exists() ? (aux ? r[std::size_t(ri_)].auxIndex : r[std::size_t(ri_)].sourceIndex) : 0;
    if (aux) m.addItem("NONE", true, curKind == ModSource::unknown, [this] { editRoute([](ModulationRoute& q) { setRouteAux(q, ModSource::unknown, 0); }); });
    for (const auto& e : sourceCatalog()) {
        if (groups.find(e.group) == groups.end()) order.push_back(e.group);
        groups[e.group].addItem(e.label, true, e.kind == curKind && e.index == curIdx, [this, e, aux] {
            editRoute([e, aux](ModulationRoute& q) { if (aux) setRouteAux(q, e.kind, e.index); else setRouteSource(q, e.kind, e.index); });
        });
    }
    for (const auto& g : order) m.addSubMenu(g, groups[g]);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(aux ? aux_ : source_).withStandardItemHeight(13));
}

void MatrixRow::showDestMenu() {
    juce::PopupMenu m;
    std::map<juce::String, juce::PopupMenu> groups;
    std::vector<juce::String> order;
    TargetId cur; if (exists()) cur = normalizedTarget(ctx.patch->modulation[std::size_t(ri_)]);
    for (const auto& d : destinationCatalog(*ctx.patch)) {
        if (groups.find(d.group) == groups.end()) order.push_back(d.group);
        groups[d.group].addItem(d.label, true, d.kind == cur.kind && d.inst == cur.inst && d.param == cur.param,
            [this, d] { editRoute([d](ModulationRoute& q) { setRouteDestination(q, d); }); });
    }
    // group into top-level families so the menu stays short
    auto family = [](const juce::String& g) -> juce::String {
        if (g.startsWith("OSC") || g == "NOISE" || g == "SUB") return "OSCILLATORS";
        if (g.startsWith("FILTER")) return "FILTERS";
        if (g.startsWith("ENV")) return "ENVELOPES";
        if (g.startsWith("LFO")) return "LFOS";
        if (g.startsWith("FX")) return "EFFECTS";
        return g;
    };
    std::map<juce::String, juce::PopupMenu> families;
    std::vector<juce::String> famOrder;
    for (const auto& g : order) {
        const auto f = family(g);
        if (families.find(f) == families.end()) famOrder.push_back(f);
        if (f == g) { // leaf-level group: add its items directly as a submenu
            families[f] = groups[g];
        } else families[f].addSubMenu(g, groups[g]);
    }
    for (const auto& f : famOrder) m.addSubMenu(f, families[f]);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(dest_).withStandardItemHeight(13));
}

void MatrixRow::refresh(const Patch& p) {
    const bool ex = ri_ < int(p.modulation.size());
    active_ = ex;
    activeF_.to(ex ? 1.0f : 0.0f);
    Panel::refresh(p);
    if (ex) {
        const auto& r = p.modulation[std::size_t(ri_)];
        bypass_ = r.bypass;
        amountValue_ = r.amount;
        source_->setText(r.sourceKind == ModSource::unknown ? juce::String(r.sourceName).toUpperCase() : sourceLabel(r.sourceKind, r.sourceIndex));
        const auto d = destinationLabel(p, r);
        dest_->setText(d.isEmpty() ? "--" : d);
        aux_->setText(r.auxKind == ModSource::unknown ? "--" : sourceLabel(r.auxKind, r.auxIndex));
        source_->setDimmed(r.bypass); dest_->setDimmed(r.bypass); aux_->setDimmed(r.bypass || r.auxKind == ModSource::unknown);
        amount_->setDimmed(r.bypass); smooth_->setDimmed(r.bypass); curveIn_->setDimmed(r.bypass); curveOut_->setDimmed(r.bypass);
        bypassBtn_->setToggled(r.bypass);
    } else {
        bypass_ = false; amountValue_ = 0.0;
        source_->setText("--"); dest_->setText("--"); aux_->setText("--");
        source_->setDimmed(true); dest_->setDimmed(true); aux_->setDimmed(true);
        amount_->setDimmed(true); smooth_->setDimmed(true); curveIn_->setDimmed(true); curveOut_->setDimmed(true);
        bypassBtn_->setToggled(false);
    }
    repaint();
}

void MatrixRow::resized() {
    const int w = getWidth();
    const int fixed = 12 + 100 + 24 + 22 + 140 + 92 + 20 + 24 + 90 + 18 + 18 + 11 * 3;
    const int amountW = std::max(80, w - fixed - 44);
    int x = 14;
    auto put = [&](juce::Component* c, int width) { c->setBounds(x, 3, width, rowH - 6); x += width + 3; };
    put(source_, 100); put(curveIn_, 24); put(amount_, amountW); x += 40; put(bipolar_, 22);
    put(dest_, 140); put(aux_, 92); put(invert_, 20); put(curveOut_, 24); put(smooth_, 90);
    put(bypassBtn_, 18); put(del_, 18);
}

void MatrixRow::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const float a = activeF_.v();
    g.setColour(mix(pal::sunken, pal::panel, a)); g.fillRect(r);
    hLine(g, 0, r.getBottom() - 1, r.getWidth(), pal::edgeDark);
    drawIconCentred(g, Icon::grip, {2, 0, 8, rowH}, pal::textMuted);
    // amount read-out beside the slider
    const int x = amount_->getRight() + 2;
    drawTextIn(g, active_ ? (amountValue_ > 0 ? "+" : "") + formatNumber(amountValue_, 1) + "%" : "", {x, 0, 40, rowH}, bypass_ ? pal::textMuted : amountValue_ < 0 ? pal::crimson : pal::lcd, juce::Justification::centred);
    // routing arrow between source and destination
    drawIconCentred(g, Icon::arrowRight, {bipolar_->getRight() - 1, 0, 6, rowH}, pal::textMuted);
}

// ================================================================== MatrixPage
MatrixPage::MatrixPage(UiContext& c) : Panel(c) {
    clear_ = &make<PixelButton>("CLEAR ALL"); clear_->setBold(true); clear_->setAccent(pal::crimson);
    clear_->onClick = [this] { ctx.editNow([](Patch& p) { p.modulation.clear(); }); };
}

void MatrixPage::refresh(const Patch& p) {
    const int want = std::max(int(p.modulation.size()) + 1, std::max(8, (getHeight() - headH) / rowH));
    if (want != count_) {
        while (int(rows_.size()) > want) rows_.pop_back();
        while (int(rows_.size()) < want) {
            rows_.push_back(std::make_unique<MatrixRow>(ctx, int(rows_.size())));
            addAndMakeVisible(*rows_.back());
            rows_.back()->enter();
        }
        count_ = want;
        layoutRows();
    }
    for (auto& r : rows_) r->refresh(p);
    clear_->setVisible(!p.modulation.empty());
}

void MatrixPage::layoutRows() {
    int y = headH - scroll_;
    total_ = headH;
    for (auto& r : rows_) { r->setBounds(0, y, getWidth() - 8, rowH); y += rowH; total_ += rowH; }
    repaint();
}

void MatrixPage::resized() {
    layoutRows();
    clear_->setBounds(getWidth() - 80, 1, 76, 14);
    if (ctx.patch) count_ = -1;   // re-evaluate visible row count for the new height
    if (ctx.patch) refresh(*ctx.patch);
}

void MatrixPage::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    scroll_ = std::clamp(scroll_ - int(w.deltaY * 90.0f), 0, std::max(0, total_ - getHeight()));
    layoutRows();
}

void MatrixPage::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    g.setColour(pal::chassis); g.fillRect(r);
    g.setColour(pal::raised); g.fillRect(0, 0, r.getWidth(), headH);
    hLine(g, 0, headH - 1, r.getWidth(), pal::edgeDark);
    const int w = r.getWidth();
    const int fixed = 12 + 100 + 24 + 22 + 140 + 92 + 20 + 24 + 90 + 18 + 18 + 11 * 3;
    const int amountW = std::max(80, w - 8 - fixed - 44);
    int x = 14;
    auto head = [&](const char* t, int width, bool centre = false) {
        drawTextIn(g, t, {x, 0, width, headH}, pal::textBody, centre ? juce::Justification::centred : juce::Justification::centredLeft, 1, true); x += width + 3; };
    head("SOURCE", 100); head("CRV", 24, true); head("AMOUNT", amountW + 40); head("POL", 22, true); head("DESTINATION", 140);
    head("AUX SOURCE", 92); head("INV", 20, true); head("CRV", 24, true); head("SMOOTH", 90);
    if (total_ > r.getHeight()) {
        const int th = std::max(16, r.getHeight() * r.getHeight() / total_);
        const int ty = headH + scroll_ * (r.getHeight() - headH - th) / std::max(1, total_ - r.getHeight());
        g.setColour(pal::sunken); g.fillRect(w - 6, headH, 6, r.getHeight() - headH);
        bevelBox(g, {w - 6, ty, 6, th}, pal::raisedHi);
    }
}

}
