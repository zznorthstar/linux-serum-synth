#include "ModSection.h"
#include "ModCatalog.h"
#include "../dsp/Modulators.h"
#include "Retro.h"
#include <cmath>

namespace zyg::ui {
namespace {
const juce::Colour graphWell = pal::sunken;
const juce::Colour graphFill = mix(pal::acidShadow, pal::sunken, 0.45f);
const juce::Colour lfoLine = pal::violetHot;

LfoDefinition effectiveLfo(const Patch& p, int i) {
    LfoDefinition d = p.lfoDefinitions[std::size_t(i)];
    if (i == 0 && p.lfoOneSine) { d = LfoDefinition{}; d.shape = LfoShape::sine; d.rateHz = p.lfoOneRateHz; }
    return d;
}
// Legacy LFO 1 (sine driven by lfoOneRateHz) becomes a normal definition before it is edited.
void modernizeLfo(Patch& p) {
    if (!p.lfoOneSine) return;
    p.lfoDefinitions[0] = effectiveLfo(p, 0);
    p.lfoOneSine = false;
}
void editLfo(UiContext& ctx, const void* key, int index, std::function<void(LfoDefinition&)> fn) {
    ctx.edit(key, [index, fn = std::move(fn)](Patch& p) {
        if (index == 0) modernizeLfo(p);
        fn(p.lfoDefinitions[std::size_t(index)]);
    });
}
const char* shapeName(LfoShape s) {
    switch (s) {
        case LfoShape::sine: return "SINE"; case LfoShape::lorenz: return "LORENZ"; case LfoShape::rossler: return "ROSSLER";
        case LfoShape::randomHold: return "S&H"; case LfoShape::path: return "PATH"; case LfoShape::curve: return "CURVE";
        default: return "OFF";
    }
}
constexpr double kDivisions[10] = {32, 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 0.0625};
const char* kDivisionNames[10] = {"8 BAR", "4 BAR", "2 BAR", "1 BAR", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
// syncBeats = division * modifier (1, 2/3 triplet, 3/2 dotted)
double modifierOf(double beats) {
    const double l = std::log2(std::max(beats, 1.0e-6));
    const double f = l - std::floor(l);
    if (std::abs(f - 0.415) < 0.04) return 2.0 / 3.0;
    if (std::abs(f - 0.585) < 0.04) return 1.5;
    return 1.0;
}
int divisionIndex(double beats) {
    const double base = beats / modifierOf(beats);
    int best = 5; double bd = 1e9;
    for (int i = 0; i < 10; ++i) { const double d = std::abs(std::log2(base / kDivisions[i])); if (d < bd) { bd = d; best = i; } }
    return best;
}
}

// =============================================================== MacroPanel
MacroPanel::MacroPanel(UiContext& c) : Panel(c) {
    for (int i = 0; i < 8; ++i) {
        auto* k = &make<Knob>(ctx, KnobSpec{"MACRO " + juce::String(i + 1), 0, 1, 0, false, false, 1.0, 0, [](double v) { return fmt::percent01(v); }},
            [i](const Patch& p) { return p.macroValues[std::size_t(i)]; },
            [i](Patch& p, double v) { p.macroValues[std::size_t(i)] = v; }, 22);
        k->setTarget({ModTarget::macroValue, i, 0});
        k->setLabelShown(false);
        knobs_[std::size_t(i)] = k;
        auto* h = &make<Canvas>();
        h->setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        h->drag = [this, h, i](const juce::MouseEvent& e) { if (e.getDistanceFromDragStart() > 4) startModDrag(*h, ModSource::macro, i, ctx.uiScale); };
        handles_[std::size_t(i)] = h;
    }
}

void MacroPanel::refresh(const Patch& p) {
    Panel::refresh(p);
    for (int i = 0; i < 8; ++i) {
        counts_[std::size_t(i)] = countRoutes(p, ModSource::macro, i);
        const auto& j = p.macros[std::size_t(i)];
        names_[std::size_t(i)] = j.is_object() && j.contains("name") && j["name"].is_string()
            ? juce::String(j["name"].get<std::string>()) : "MACRO " + juce::String(i + 1);
    }
    repaint();
}

void MacroPanel::resized() {
    for (int i = 0; i < 8; ++i) {
        const int col = i / 4, row = i % 4;
        const int rh = rowH(), cy = 20 + row * rh + (rh - 44) / 2;
        knobs_[std::size_t(i)]->setBounds(1 + col * 68 + 4, cy + 1, 30, 30);
        handles_[std::size_t(i)]->setBounds(1 + col * 68 + 37, 20 + row * rh + 2, 29, rh - 4);
    }
}

void MacroPanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, pal::panel);
    g.setColour(pal::raised); g.fillRect(1, 1, r.getWidth() - 2, 17);
    hLine(g, 1, 18, r.getWidth() - 2, pal::edgeDark);
    drawTextIn(g, "MACROS", {1, 1, r.getWidth() - 2, 17}, pal::textHi, juce::Justification::centred, 1, true);
    for (int i = 0; i < 8; ++i) {
        const int col = i / 4, row = i % 4;
        const int rh = rowH();
        const juce::Rectangle<int> cell(1 + col * 68, 20 + row * rh, 67, rh - 1);
        hLine(g, cell.getX(), cell.getBottom() - 1, cell.getWidth(), pal::edgeDark);
        vLine(g, cell.getRight() - 1, cell.getY(), cell.getHeight(), pal::edgeDark);
        const int cy = cell.getY() + (rh - 44) / 2;
        drawText(g, juce::String(i + 1), cell.getRight() - 9, cy + 3, pal::textMuted);
        const auto badge = juce::Rectangle<int>(cell.getRight() - 16, cy + 14, 13, 13);
        const bool used = counts_[std::size_t(i)] > 0;
        g.setColour(used ? pal::violetShadow : pal::sunken); g.fillRect(badge.reduced(1, 0)); g.fillRect(badge.reduced(0, 1));
        g.setColour(used ? pal::violetHot : pal::edgeMid);
        g.fillRect(badge.getX() + 1, badge.getY(), badge.getWidth() - 2, 1); g.fillRect(badge.getX() + 1, badge.getBottom() - 1, badge.getWidth() - 2, 1);
        g.fillRect(badge.getX(), badge.getY() + 1, 1, badge.getHeight() - 2); g.fillRect(badge.getRight() - 1, badge.getY() + 1, 1, badge.getHeight() - 2);
        drawTextIn(g, juce::String(counts_[std::size_t(i)]), badge, used ? pal::textHi : pal::textMuted, juce::Justification::centred);
        drawTextIn(g, names_[std::size_t(i)], {cell.getX() + 2, cy + 33, cell.getWidth() - 4, 9}, pal::textBody, juce::Justification::centred);
    }
}

// ================================================================= EnvGraph
class EnvGraph final : public AnimatedView, public Bound {
public:
    explicit EnvGraph(UiContext& c) : AnimatedView(30), ctx_(c) {}
    void pull(const Patch& p) override { if (drag_ < 0) { env_ = p.envelopes[std::size_t(ctx_.selectedEnv)]; repaint(); } }
    void paint(juce::Graphics& g) override { paintRetro(screen_, g, getLocalBounds(), RetroStyle::envelopeLcd(), [this](juce::Graphics& c) { paintContent(c); }); }
    void paintContent(juce::Graphics& g);
    RetroScreen screen_;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { drag_ = -1; ctx_.flush(); repaint(); }
    void mouseMove(const juce::MouseEvent& e) override { const int h = hit(e.getPosition()); if (h != hover_) { hover_ = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int h = hit(e.getPosition());
        if (h >= 4 && h <= 6) { env_.curve[std::size_t(h - 4)] = h == 4 ? 50.0 : 66.6; commit(); }
    }
protected:
    void tick(double dt) override;
    bool busy() const override { return stage_ != Stage::idle || drag_ >= 0 || ctx_.proc.anyNoteHeld(); }
private:
    struct Geo { double scale; int x0, top, bottom, xA, xH, xD, xS, xR, ySus; };
    Geo geometry(double forcedScale = -1.0) const;
    juce::Point<int> curveHandle(const Geo& g, int which) const;
    int hit(juce::Point<int> pt) const;
    void commit() {
        const int sel = ctx_.selectedEnv; const auto e = env_;
        ctx_.edit(this, [sel, e](Patch& p) { auto& d = p.envelopes[std::size_t(sel)];
            d.attack = e.attack; d.hold = e.hold; d.decay = e.decay; d.sustain = e.sustain; d.release = e.release; d.curve = e.curve; });
        repaint();
    }
    UiContext& ctx_;
    Envelope env_;
    int drag_ = -1, hover_ = -1;
    double dragScale_ = 0.0;
    // playhead preview: follows the held keys through this envelope's stages
    enum class Stage { idle, held, released } stage_ = Stage::idle;
    double tHeld_ = 0.0, tRel_ = 0.0;
    bool wasHeld_ = false;
};

EnvGraph::Geo EnvGraph::geometry(double forced) const {
    const auto r = getLocalBounds().reduced(3);
    Geo g {};
    g.x0 = r.getX() + 1; g.top = r.getY() + 7; g.bottom = r.getBottom() - 9;
    const double a = env_.attack, h = env_.hold, d = env_.decay, rel = env_.release;
    const double active = a + h + d + rel;
    const double sus = 0.25 * active + 0.02;
    const double W = double(r.getWidth() - 4);
    g.scale = forced > 0.0 ? forced : W / std::max(1.0e-3, active + sus);
    g.xA = g.x0 + int(std::round(a * g.scale));
    g.xH = g.xA + int(std::round(h * g.scale));
    g.xD = g.xH + int(std::round(d * g.scale));
    g.xS = g.xD + int(std::round((forced > 0.0 ? 0.25 * (active) + 0.02 : sus) * g.scale));
    g.xR = g.xS + int(std::round(rel * g.scale));
    g.ySus = g.bottom - int(std::round(std::clamp(env_.sustain, 0.0, 1.0) * double(g.bottom - g.top)));
    return g;
}

juce::Point<int> EnvGraph::curveHandle(const Geo& g, int which) const {
    using dsp::envelopeCurveShape;
    if (which == 0) return {(g.x0 + g.xA) / 2, g.bottom - int(std::round(envelopeCurveShape(0.5, env_.curve[0]) * (g.bottom - g.top)))};
    if (which == 1) return {(g.xH + g.xD) / 2, g.top + int(std::round(envelopeCurveShape(0.5, env_.curve[1]) * (g.ySus - g.top)))};
    const double v = 1.0 - envelopeCurveShape(0.5, env_.curve[2]);
    return {(g.xS + g.xR) / 2, g.bottom - int(std::round(v * double(g.bottom - g.ySus)))};
}

int EnvGraph::hit(juce::Point<int> pt) const {
    const auto g = geometry(drag_ >= 0 ? dragScale_ : -1.0);
    const juce::Point<int> pts[4] = {{g.xA, g.top}, {g.xH, g.top}, {g.xD, g.ySus}, {g.xR, g.bottom}};
    for (int i = 0; i < 4; ++i) if (pt.getDistanceFrom(pts[i]) <= 6) return i;
    for (int i = 0; i < 3; ++i) if (pt.getDistanceFrom(curveHandle(g, i)) <= 5) return 4 + i;
    return -1;
}

void EnvGraph::mouseDown(const juce::MouseEvent& e) {
    drag_ = hit(e.getPosition());
    if (drag_ >= 0) dragScale_ = geometry().scale;
}

void EnvGraph::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ < 0) return;
    const auto g = geometry(dragScale_);
    const double s = dragScale_;
    auto secs = [&](int px0, int px1) { return std::max(0.0, double(px1 - px0) / s); };
    constexpr double minT = 0.0005;
    switch (drag_) {
        case 0: env_.attack = std::max(minT, secs(g.x0, e.x)); break;
        case 1: env_.hold = secs(g.xA, e.x); break;
        case 2:
            env_.decay = std::max(minT, secs(g.xH, e.x));
            env_.sustain = std::clamp(double(g.bottom - e.y) / double(g.bottom - g.top), 0.0, 1.0);
            break;
        case 3: env_.release = std::max(minT, secs(g.xS, e.x)); break;
        default: {
            const int k = drag_ - 4;
            double sh;   // envelopeCurveShape(0.5, curve) implied by the handle height
            if (k == 0) sh = double(g.bottom - e.y) / double(g.bottom - g.top);
            else if (k == 1) sh = double(e.y - g.top) / double(std::max(1, g.ySus - g.top));
            else sh = 1.0 - double(g.bottom - e.y) / double(std::max(1, g.bottom - g.ySus));
            sh = std::clamp(sh, 0.02, 0.98);
            const double bend = sh > 0.5 ? std::log2(std::log(1.0 - sh) / std::log(0.5)) / 4.0
                                         : -std::log2(std::log(sh) / std::log(0.5)) / 4.0;
            env_.curve[std::size_t(k)] = std::clamp(50.0 + bend * 50.0, 0.0, 100.0);
        }
    }
    commit();
}

void EnvGraph::tick(double dt) {
    const bool held = ctx_.proc.anyNoteHeld();
    if (held && !wasHeld_) { stage_ = Stage::held; tHeld_ = 0.0; }
    if (!held && wasHeld_ && stage_ == Stage::held) { stage_ = Stage::released; tRel_ = 0.0; }
    wasHeld_ = held;
    if (stage_ == Stage::held) tHeld_ += dt;
    else if (stage_ == Stage::released) { tRel_ += dt; if (tRel_ > env_.release + 0.15) stage_ = Stage::idle; }
}

void EnvGraph::paintContent(juce::Graphics& g) {
    using dsp::envelopeCurveShape;
    const auto r = getLocalBounds();
    wellBox(g, r, graphWell);
    const auto geo = geometry(drag_ >= 0 ? dragScale_ : -1.0);
    // time grid
    static const double steps[] = {0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20};
    double step = 20;
    for (double s : steps) if (s * geo.scale >= 38.0) { step = s; break; }
    for (double t = step; geo.x0 + t * geo.scale < r.getRight() - 4; t += step) {
        const int x = geo.x0 + int(std::round(t * geo.scale));
        for (int y = r.getY() + 2; y < r.getBottom() - 2; y += 3) px(g, x, y, pal::edgeMid);
        drawText(g, (t < 1.0 ? formatNumber(t * 1000.0, 0) + "MS" : formatNumber(t, 1) + "S"), x + 2, geo.bottom + 1, pal::textMuted);
    }
    for (int i = 1; i < 4; ++i) { const int y = geo.top + (geo.bottom - geo.top) * i / 4; for (int x = r.getX() + 2; x < r.getRight() - 2; x += 4) px(g, x, y, pal::edgeDark); }
    // curve
    auto level = [&](int x) -> double {
        if (x <= geo.xA) { const double t = geo.xA > geo.x0 ? double(x - geo.x0) / double(geo.xA - geo.x0) : 1.0; return envelopeCurveShape(t, env_.curve[0]); }
        if (x <= geo.xH) return 1.0;
        if (x <= geo.xD) { const double t = geo.xD > geo.xH ? double(x - geo.xH) / double(geo.xD - geo.xH) : 1.0; return 1.0 + (env_.sustain - 1.0) * envelopeCurveShape(t, env_.curve[1]); }
        if (x <= geo.xS) return env_.sustain;
        const double t = geo.xR > geo.xS ? double(x - geo.xS) / double(geo.xR - geo.xS) : 1.0;
        return env_.sustain * (1.0 - envelopeCurveShape(t, env_.curve[2]));
    };
    const int maxX = std::min(geo.xR, r.getRight() - 4);
    // fill fades from the curve toward the floor so the graph reads as a lit surface, not a flat slab
    const juce::ColourGradient fillGrad(mix(pal::acidShadow, pal::acidDim, 0.25f).withAlpha(0.95f), 0.0f, float(geo.top),
                                        graphFill.withAlpha(0.35f), 0.0f, float(geo.bottom), false);
    int prev = geo.bottom;
    for (int x = geo.x0; x <= maxX; ++x) {
        const int y = geo.bottom - int(std::round(std::clamp(level(x), 0.0, 1.0) * double(geo.bottom - geo.top)));
        g.setGradientFill(fillGrad); g.fillRect(x, y + 1, 1, std::max(0, geo.bottom - y));
        for (int d = 1; d <= 3 && y + d <= geo.bottom; ++d) if (((x + d) & 1) == 0) px(g, x, y + d, mix(pal::acidShadow, pal::acidDim, 0.35f));
        g.setColour(pal::acid);
        if (x == geo.x0) g.fillRect(x, y, 1, 1); else g.fillRect(x, std::min(y, prev), 1, std::abs(y - prev) + 1);
        prev = y;
    }
    // playhead of a virtual voice following the held keys
    if (stage_ != Stage::idle && drag_ < 0) {
        int hx;
        if (stage_ == Stage::held) {
            const double body = env_.attack + env_.hold + env_.decay;
            hx = tHeld_ <= body ? geo.x0 + int(std::round(tHeld_ * geo.scale)) : std::min(geo.xS, geo.xD + int(std::round((tHeld_ - body) * geo.scale)));
        } else hx = std::min(geo.xR, geo.xS + int(std::round(tRel_ * geo.scale)));
        hx = std::clamp(hx, geo.x0, geo.xR);
        const int hy = geo.bottom - int(std::round(std::clamp(level(hx), 0.0, 1.0) * double(geo.bottom - geo.top)));
        vLine(g, hx, r.getY() + 2, r.getHeight() - 4, pal::acidHot.withAlpha(0.28f));
        g.setColour(pal::acid.withAlpha(0.35f)); g.fillRect(hx - 3, hy - 3, 7, 7);
        g.setColour(pal::acidHot); g.fillRect(hx - 1, hy - 1, 3, 3);
    }
    // handles
    const juce::Point<int> pts[4] = {{geo.xA, geo.top}, {geo.xH, geo.top}, {geo.xD, geo.ySus}, {geo.xR, geo.bottom}};
    for (int i = 0; i < 4; ++i) {
        const bool on = hover_ == i || drag_ == i;
        const auto c = on ? pal::acidHot : pal::acid;
        g.setColour(pal::edgeDark); g.fillRect(pts[i].x - 3, pts[i].y - 3, 7, 7);
        g.setColour(c); g.fillRect(pts[i].x - 2, pts[i].y - 2, 5, 5);
        g.setColour(pal::sunken); g.fillRect(pts[i].x - 1, pts[i].y - 1, 3, 3);
    }
    for (int i = 0; i < 3; ++i) {
        const auto h = curveHandle(geo, i);
        const bool on = hover_ == 4 + i || drag_ == 4 + i;
        g.setColour(on ? pal::acidHot : pal::acidDim); g.fillRect(h.x - 1, h.y - 1, 3, 3);
    }
}

// ================================================================== EnvPanel
EnvPanel::EnvPanel(UiContext& c) : Panel(c) {
    graph_ = &make<EnvGraph>(ctx);
    static const EnvParam params[5] = {EnvParam::attack, EnvParam::hold, EnvParam::decay, EnvParam::sustain, EnvParam::release};
    static const char* labels[5] = {"ATK", "HOLD", "DEC", "SUS", "REL"};
    for (int i = 0; i < 5; ++i) {
        const auto ep = params[i];
        KnobSpec spec; spec.label = labels[i];
        if (ep == EnvParam::sustain) { spec.lo = 0.0; spec.hi = 1.0; spec.def = 1.0; spec.format = [](double v) { return v <= 1.0e-4 ? juce::String("-INF DB") : fmt::db(v); }; }
        else { spec.lo = 0.0005; spec.hi = 32.0; spec.logScale = true; spec.def = ep == EnvParam::hold ? 0.0005 : ep == EnvParam::attack ? 0.0005 : ep == EnvParam::decay ? 1.0 : 0.075; spec.format = fmt::seconds; }
        auto* k = &make<Knob>(ctx, spec,
            [this, ep](const Patch& p) { return std::max(ep == EnvParam::sustain ? 0.0 : 0.0005, getEnv(p.envelopes[std::size_t(ctx.selectedEnv)], ep)); },
            [this, ep](Patch& p, double v) { setEnv(p.envelopes[std::size_t(ctx.selectedEnv)], ep, v <= 0.0006 && ep != EnvParam::sustain ? 0.0 : v); }, 26);
        knobs_[std::size_t(i)] = k;
    }
}

void EnvPanel::refresh(const Patch& p) {
    static const EnvParam params[5] = {EnvParam::attack, EnvParam::hold, EnvParam::decay, EnvParam::sustain, EnvParam::release};
    for (int i = 0; i < 5; ++i) knobs_[std::size_t(i)]->setTarget({ModTarget::envParam, ctx.selectedEnv, int(params[i])});
    env_ = p.envelopes[std::size_t(ctx.selectedEnv)];
    Panel::refresh(p);
    repaint();
}

void EnvPanel::resized() {
    const int w = getWidth();
    const int ex = extraH();
    graph_->setBounds(18, 2, w - 22, 116 + ex);
    const int cw = (w - 12) / 5;
    for (int i = 0; i < 5; ++i) knobs_[std::size_t(i)]->setBounds(6 + i * cw, 140 + ex, cw, 44);
}

void EnvPanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, pal::panel);
    // left tool strip (lock / arrows / zoom glyphs are decorative here: the graph auto-fits)
    drawIconCentred(g, Icon::lock, {2, 8, 14, 10}, pal::textMuted);
    drawIconCentred(g, Icon::caretUp, {2, 26, 14, 8}, pal::textMuted);
    drawIconCentred(g, Icon::search, {2, 52, 14, 10}, pal::textMuted);
    drawIconCentred(g, Icon::caretDown, {2, 78, 14, 8}, pal::textMuted);
    // value read-outs above the knobs
    const juce::String vals[5] = {fmt::seconds(env_.attack), fmt::seconds(env_.hold), fmt::seconds(env_.decay),
                                  env_.sustain <= 1.0e-4 ? "-INF DB" : fmt::db(env_.sustain), fmt::seconds(env_.release)};
    const int ex = extraH();
    for (int i = 0; i < 5; ++i) drawTextIn(g, vals[i], {6 + i * ((r.getWidth() - 12) / 5), 121 + ex, (r.getWidth() - 12) / 5, 12}, pal::lcd, juce::Justification::centred);
    hLine(g, 3, 135 + ex, r.getWidth() - 6, pal::edgeDark);
}

// ================================================================== LfoGraph
class LfoGraph final : public AnimatedView, public Bound {
public:
    explicit LfoGraph(UiContext& c) : AnimatedView(30), ctx_(c) {}
    void pull(const Patch& p) override { if (drag_ < 0) { def_ = effectiveLfo(p, ctx_.selectedLfo); rebuild(); repaint(); } }
    void paint(juce::Graphics& g) override { paintRetro(screen_, g, getLocalBounds(), RetroStyle::violetCrt(), [this](juce::Graphics& c) { paintContent(c); }); }
    void paintContent(juce::Graphics& g);
    RetroScreen screen_;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { drag_ = -1; ctx_.flush(); repaint(); }
    void mouseMove(const juce::MouseEvent& e) override { const int h = pointAt(e.getPosition()); if (h != hover_) { hover_ = h; repaint(); } }
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override;
protected:
    void tick(double dt) override {
        if (ctx_.proc.getActiveVoiceCount() > 0 && def_.shape != LfoShape::unknown) {
            const double hz = def_.tempoSync ? 2.0 / std::max(0.03, def_.syncBeats) : def_.rateHz;
            phase_ = std::fmod(phase_ + dt * std::clamp(hz, 0.15, 3.0), 1.0);   // speed is capped so it stays readable
            playing_ = true;
        } else playing_ = false;
    }
    bool busy() const override { return playing_ || drag_ >= 0; }
private:
    double phase_ = 0.0; bool playing_ = false;
    bool editable() const { return def_.shape == LfoShape::curve || def_.shape == LfoShape::path; }
    juce::Rectangle<int> area() const { return getLocalBounds().reduced(3); }
    juce::Point<int> toPx(double x, double y) const {
        const auto a = area();
        return {a.getX() + int(std::round(x * (a.getWidth() - 1))), a.getY() + int(std::round((1.0 - y) * (a.getHeight() - 1)))};
    }
    int pointAt(juce::Point<int> pt) const {
        if (!editable()) return -1;
        for (std::size_t i = 0; i < def_.path.x.size(); ++i)
            if (pt.getDistanceFrom(toPx(def_.path.x[i], def_.path.y[i])) <= 6) return int(i);
        return -1;
    }
    void rebuild();
    void commit() {
        const int sel = ctx_.selectedLfo; const auto path = def_.path; const auto shape = def_.shape;
        editLfo(ctx_, this, sel, [path, shape](LfoDefinition& d) { d.path = path; d.shape = shape; });
        repaint();
    }
    UiContext& ctx_;
    LfoDefinition def_;
    std::vector<float> wave_;
    int drag_ = -1, hover_ = -1;
};

void LfoGraph::rebuild() {
    wave_.clear();
    const int w = std::max(2, area().getWidth());
    wave_.resize(std::size_t(w));
    if ((def_.shape == LfoShape::curve || def_.shape == LfoShape::path) && !def_.path.empty()) {
        for (int x = 0; x < w; ++x) {
            const double ph = double(x) / double(w - 1);
            wave_[std::size_t(x)] = float(def_.shape == LfoShape::path ? dsp::evalPath2D(def_.path, ph) : dsp::evalPath(def_.path, ph));
        }
        return;
    }
    if (def_.shape == LfoShape::unknown) { std::fill(wave_.begin(), wave_.end(), 0.0f); return; }
    dsp::LfoState st; st.reset(12345u, 0.0);
    LfoDefinition d = def_; d.mode = LfoMode::free; d.direction = 0;
    const double rate = (def_.shape == LfoShape::randomHold) ? 8.0 : (def_.shape == LfoShape::sine ? 1.0 : 3.0);
    for (int x = 0; x < w; ++x)
        wave_[std::size_t(x)] = float(dsp::advanceLfo(st, d, rate, 1.0 / double(w), 0.0, 0.0, 0.0, 0.0));
}

void LfoGraph::paintContent(juce::Graphics& g) {
    const auto r = getLocalBounds();
    wellBox(g, r, pal::violetWell);
    const auto a = area();
    for (int i = 1; i < 4; ++i) { const int x = a.getX() + a.getWidth() * i / 4; for (int y = a.getY() + 1; y < a.getBottom(); y += 3) px(g, x, y, pal::violetShadow); }
    for (int x = a.getX(); x < a.getRight(); x += 3) px(g, x, a.getCentreY(), pal::violetShadow);
    const bool off = def_.shape == LfoShape::unknown;
    if (wave_.size() >= 2 && !off) {
        int prev = 0;
        const auto fill = mix(pal::violetShadow, pal::violetWell, 0.35f);
        for (int x = 0; x < int(wave_.size()); ++x) {
            const int y = a.getY() + int(std::round((1.0f - (std::clamp(wave_[std::size_t(x)], -1.0f, 1.0f) * 0.5f + 0.5f)) * float(a.getHeight() - 1)));
            const int mid = a.getCentreY();
            g.setColour(fill); g.fillRect(a.getX() + x, std::min(y, mid), 1, std::abs(y - mid) + 1);
            g.setColour(lfoLine);
            if (x == 0) g.fillRect(a.getX(), y, 1, 1); else g.fillRect(a.getX() + x, std::min(y, prev), 1, std::abs(y - prev) + 1);
            prev = y;
        }
    }
    if (playing_ && !off && wave_.size() >= 2) {
        const int i = std::clamp(int(phase_ * double(wave_.size() - 1)), 0, int(wave_.size()) - 1);
        const int y = a.getY() + int(std::round((1.0f - (std::clamp(wave_[std::size_t(i)], -1.0f, 1.0f) * 0.5f + 0.5f)) * float(a.getHeight() - 1)));
        vLine(g, a.getX() + i, a.getY(), a.getHeight(), pal::acidHot.withAlpha(0.2f));
        g.setColour(pal::violetHot.withAlpha(0.4f)); g.fillRect(a.getX() + i - 3, y - 3, 7, 7);
        g.setColour(pal::acidHot); g.fillRect(a.getX() + i - 1, y - 1, 3, 3);
    }
    if (off) drawTextIn(g, "LFO OFF - CHOOSE A SHAPE", r, pal::textMuted, juce::Justification::centred);
    if (editable()) {
        for (std::size_t i = 0; i < def_.path.x.size(); ++i) {
            const auto p = toPx(def_.path.x[i], def_.path.y[i]);
            const bool on = hover_ == int(i) || drag_ == int(i);
            g.setColour(pal::edgeDark); g.fillRect(p.x - 3, p.y - 3, 7, 7);
            g.setColour(on ? pal::acidHot : pal::violetHot); g.fillRect(p.x - 2, p.y - 2, 5, 5);
            g.setColour(pal::sunken); g.fillRect(p.x - 1, p.y - 1, 3, 3);
        }
        drawSmall(g, "DBL-CLICK ADD  RIGHT-CLICK DELETE", a.getX() + 3, a.getBottom() - 7, pal::textMuted);
    } else if (!off && def_.shape != LfoShape::path)
        drawSmall(g, "DBL-CLICK TO DRAW AS CURVE", a.getX() + 3, a.getBottom() - 7, pal::textMuted);
}

void LfoGraph::mouseDown(const juce::MouseEvent& e) {
    const int h = pointAt(e.getPosition());
    if (h >= 0 && e.mods.isPopupMenu()) {
        if (def_.path.x.size() > 2) {
            def_.path.x.erase(def_.path.x.begin() + h); def_.path.y.erase(def_.path.y.begin() + h);
            if (std::size_t(h) < def_.path.bend.size()) def_.path.bend.erase(def_.path.bend.begin() + h);
            rebuild(); commit(); ctx_.flush();
        }
        return;
    }
    drag_ = h;
}

void LfoGraph::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ < 0 || !editable()) return;
    const auto a = area();
    double x = std::clamp(double(e.x - a.getX()) / double(a.getWidth() - 1), 0.0, 1.0);
    double y = std::clamp(1.0 - double(e.y - a.getY()) / double(a.getHeight() - 1), 0.0, 1.0);
    if (e.mods.isShiftDown()) { x = std::round(x * 32.0) / 32.0; y = std::round(y * 16.0) / 16.0; }
    const std::size_t i = std::size_t(drag_), n = def_.path.x.size();
    const double lo = i > 0 ? def_.path.x[i - 1] + 0.002 : 0.0, hi = i + 1 < n ? def_.path.x[i + 1] - 0.002 : 1.0;
    if (def_.shape == LfoShape::curve) x = std::clamp(x, lo, std::max(lo, hi));
    def_.path.x[i] = x; def_.path.y[i] = y;
    rebuild(); commit();
}

void LfoGraph::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto a = area();
    if (def_.shape == LfoShape::unknown) return;
    if (!editable()) {   // convert the current shape into an editable curve
        CurvePoints pts; pts.closed = true;
        if (def_.shape == LfoShape::sine) {
            for (int i = 0; i < 16; ++i) { const double ph = double(i) / 16.0; pts.x.push_back(ph); pts.y.push_back(0.5 + 0.5 * std::sin(6.283185307 * ph)); pts.bend.push_back(0.5); }
        } else {
            pts.x = {0.0, 0.25, 0.75}; pts.y = {0.5, 1.0, 0.0}; pts.bend = {0.5, 0.5, 0.5};
        }
        def_.path = pts; def_.shape = LfoShape::curve;
        rebuild(); commit(); ctx_.flush();
        return;
    }
    if (pointAt(e.getPosition()) >= 0) return;
    const double x = std::clamp(double(e.x - a.getX()) / double(a.getWidth() - 1), 0.0, 1.0);
    const double y = std::clamp(1.0 - double(e.y - a.getY()) / double(a.getHeight() - 1), 0.0, 1.0);
    std::size_t at = 0;
    while (at < def_.path.x.size() && def_.path.x[at] < x) ++at;
    def_.path.x.insert(def_.path.x.begin() + std::ptrdiff_t(at), x);
    def_.path.y.insert(def_.path.y.begin() + std::ptrdiff_t(at), y);
    if (def_.path.bend.size() + 1 >= def_.path.x.size()) def_.path.bend.insert(def_.path.bend.begin() + std::ptrdiff_t(std::min(at, def_.path.bend.size())), 0.5);
    rebuild(); commit(); ctx_.flush();
}

// ================================================================== LfoPanel
LfoPanel::LfoPanel(UiContext& c) : Panel(c) {
    graph_ = &make<LfoGraph>(ctx);
    shape_ = &make<Chooser>(); shape_->setTextColour(pal::violetHot); shape_->setBold(true);
    shape_->buildMenu = [this](juce::PopupMenu& m) {
        struct S { LfoShape s; const char* n; };
        for (auto s : {S{LfoShape::sine, "SINE"}, S{LfoShape::curve, "CURVE"}, S{LfoShape::path, "PATH (2D)"}, S{LfoShape::randomHold, "S&H RANDOM"},
                       S{LfoShape::lorenz, "LORENZ"}, S{LfoShape::rossler, "ROSSLER"}, S{LfoShape::unknown, "OFF"}})
            m.addItem(s.n, true, def_.shape == s.s, [this, s] {
                ctx.editNow([sel = ctx.selectedLfo, s](Patch& p) {
                    if (sel == 0) modernizeLfo(p);
                    auto& d = p.lfoDefinitions[std::size_t(sel)];
                    d.shape = s.s;
                    if ((s.s == LfoShape::curve || s.s == LfoShape::path) && d.path.empty()) {
                        d.path.closed = true; d.path.x = {0.0, 0.25, 0.75}; d.path.y = {0.5, 1.0, 0.0}; d.path.bend = {0.5, 0.5, 0.5};
                    }
                });
            });
    };
    static const char* modeNames[3] = {"FREE", "RETRIG", "ENVELOPE"};
    static const LfoMode modeVals[3] = {LfoMode::free, LfoMode::trigger, LfoMode::envelope};
    static const Icon icons[3] = {Icon::free, Icon::retrig, Icon::envelope};
    for (int i = 0; i < 3; ++i) {
        auto* b = &make<PixelButton>(modeNames[i]);
        b->setIcon(icons[i]); b->setAccent(pal::violetHot); b->setBold(true); b->setFlat(true);
        b->onClick = [this, i] { editLfo(ctx, this, ctx.selectedLfo, [i](LfoDefinition& d) { d.mode = modeVals[i]; }); ctx.flush(); };
        modes_[std::size_t(i)] = b;
    }
    mono_ = &make<Toggle>(ctx, "MONO", Toggle::Style::check, [this](const Patch& p) { return effectiveLfo(p, ctx.selectedLfo).mono; },
        [this](Patch& p, bool v) { if (ctx.selectedLfo == 0) modernizeLfo(p); p.lfoDefinitions[std::size_t(ctx.selectedLfo)].mono = v; });
    direction_ = &make<Chooser>(); direction_->setArrows(true); direction_->setTextColour(pal::violetHot); direction_->setCentredText(true);
    direction_->buildMenu = [this](juce::PopupMenu& m) {
        static const char* n[3] = {"FORWARD", "BACKWARD", "PING-PONG"};
        for (int i = 0; i < 3; ++i) m.addItem(n[i], true, def_.direction == i, [this, i] { editLfo(ctx, this, ctx.selectedLfo, [i](LfoDefinition& d) { d.direction = i; }); ctx.flush(); });
    };
    direction_->onStep = [this](int dir) { const int n = (def_.direction + dir + 3) % 3; editLfo(ctx, this, ctx.selectedLfo, [n](LfoDefinition& d) { d.direction = n; }); ctx.flush(); };
    anchor_ = &make<Toggle>(ctx, "", Toggle::Style::block, [this](const Patch& p) { return effectiveLfo(p, ctx.selectedLfo).anchored; },
        [this](Patch& p, bool v) { if (ctx.selectedLfo == 0) modernizeLfo(p); p.lfoDefinitions[std::size_t(ctx.selectedLfo)].anchored = v; });
    sync_ = &make<Toggle>(ctx, "BPM", Toggle::Style::block, [this](const Patch& p) { return effectiveLfo(p, ctx.selectedLfo).tempoSync; },
        [this](Patch& p, bool v) { if (ctx.selectedLfo == 0) modernizeLfo(p); p.lfoDefinitions[std::size_t(ctx.selectedLfo)].tempoSync = v; });
    trip_ = &make<Toggle>(ctx, "TRIP", Toggle::Style::block, [this](const Patch& p) { auto d = effectiveLfo(p, ctx.selectedLfo); return d.tempoSync && std::abs(modifierOf(d.syncBeats) - 2.0 / 3.0) < 1e-6; },
        [this](Patch& p, bool v) {
            if (ctx.selectedLfo == 0) modernizeLfo(p);
            auto& d = p.lfoDefinitions[std::size_t(ctx.selectedLfo)]; const double base = d.syncBeats / modifierOf(d.syncBeats);
            d.syncBeats = v ? base * 2.0 / 3.0 : base; });
    dot_ = &make<Toggle>(ctx, "DOT", Toggle::Style::block, [this](const Patch& p) { auto d = effectiveLfo(p, ctx.selectedLfo); return d.tempoSync && std::abs(modifierOf(d.syncBeats) - 1.5) < 1e-6; },
        [this](Patch& p, bool v) {
            if (ctx.selectedLfo == 0) modernizeLfo(p);
            auto& d = p.lfoDefinitions[std::size_t(ctx.selectedLfo)]; const double base = d.syncBeats / modifierOf(d.syncBeats);
            d.syncBeats = v ? base * 1.5 : base; });
    for (auto* t : {sync_, trip_, dot_, anchor_}) t->setCentred(true);
    anchor_->setIcon(Icon::anchor);
    rateHz_ = &make<Knob>(ctx, KnobSpec{"RATE", 0.01, 100.0, 1.0, false, true, 1.0, 0, fmt::hz},
        [this](const Patch& p) { return std::max(0.01, effectiveLfo(p, ctx.selectedLfo).rateHz); },
        [this](Patch& p, double v) { if (ctx.selectedLfo == 0) { modernizeLfo(p); } p.lfoDefinitions[std::size_t(ctx.selectedLfo)].rateHz = v; }, 26);
    rateSync_ = &make<Knob>(ctx, KnobSpec{"RATE", 0, 9, 5, false, false, 1.0, 1, [](double v) { return juce::String(kDivisionNames[std::clamp(int(std::round(v)), 0, 9)]); }},
        [this](const Patch& p) { return double(divisionIndex(effectiveLfo(p, ctx.selectedLfo).syncBeats)); },
        [this](Patch& p, double v) {
            if (ctx.selectedLfo == 0) modernizeLfo(p);
            auto& d = p.lfoDefinitions[std::size_t(ctx.selectedLfo)];
            d.syncBeats = kDivisions[std::clamp(int(std::round(v)), 0, 9)] * modifierOf(d.syncBeats); }, 26);
    struct K { LfoParam p; const char* label; double hi; double skew; std::function<juce::String(double)> f; };
    const K ks[4] = {{LfoParam::rise, "RISE", 4.0, 0.4, fmt::seconds}, {LfoParam::delay, "DELAY", 4.0, 0.4, fmt::seconds},
                     {LfoParam::smooth, "SMOOTH", 100.0, 1.0, [](double v) { return formatNumber(v, 0) + "%"; }},
                     {LfoParam::phase, "PHASE", 360.0, 1.0, fmt::degrees}};
    for (int i = 0; i < 4; ++i) {
        const auto k = ks[i];
        knobs_[std::size_t(i)] = &make<Knob>(ctx, KnobSpec{k.label, 0.0, k.hi, 0.0, false, false, k.skew, 0, k.f},
            [this, k](const Patch& p) { return getLfo(effectiveLfo(p, ctx.selectedLfo), k.p); },
            [this, k](Patch& p, double v) { if (ctx.selectedLfo == 0) modernizeLfo(p); setLfo(p.lfoDefinitions[std::size_t(ctx.selectedLfo)], k.p, v); }, 26);
    }
}

void LfoPanel::refresh(const Patch& p) {
    def_ = effectiveLfo(p, ctx.selectedLfo);
    static const LfoParam params[4] = {LfoParam::rise, LfoParam::delay, LfoParam::smooth, LfoParam::phase};
    for (int i = 0; i < 4; ++i) knobs_[std::size_t(i)]->setTarget({ModTarget::lfoParam, ctx.selectedLfo, int(params[i])});
    rateHz_->setTarget({ModTarget::lfoParam, ctx.selectedLfo, int(LfoParam::rate)});
    Panel::refresh(p);
    shape_->setText(shapeName(def_.shape));
    static const LfoMode modeVals[3] = {LfoMode::free, LfoMode::trigger, LfoMode::envelope};
    for (int i = 0; i < 3; ++i) modes_[std::size_t(i)]->setToggled(def_.mode == modeVals[i]);
    static const char* dirs[3] = {"FORWARD", "BACKWARD", "PING-PONG"};
    direction_->setText(dirs[std::clamp(def_.direction, 0, 2)]);
    rateHz_->setVisible(!def_.tempoSync); rateSync_->setVisible(def_.tempoSync);
    trip_->setEnabled(def_.tempoSync); dot_->setEnabled(def_.tempoSync);
    repaint();
}

void LfoPanel::resized() {
    const int w = getWidth();
    const int ex = extraH();
    shape_->setBounds(3, 3, 84, 16);
    for (int i = 0; i < 3; ++i) modes_[std::size_t(i)]->setBounds(3, 25 + i * 24, 84, 21);
    mono_->setBounds(6, 104 + ex, 60, 11);
    graph_->setBounds(92, 2, w - 96, 116 + ex);
    direction_->setBounds(w - 172, 120 + ex, 168, 15);
    anchor_->setBounds(4, 140 + ex, 26, 22);
    sync_->setBounds(34, 140 + ex, 34, 22);
    rateHz_->setBounds(72, 138 + ex, 56, 46); rateSync_->setBounds(72, 138 + ex, 56, 46);
    trip_->setBounds(132, 140 + ex, 32, 21); dot_->setBounds(132, 163 + ex, 32, 21);
    const int kw = std::max(50, (w - 170) / 4);
    for (int i = 0; i < 4; ++i) knobs_[std::size_t(i)]->setBounds(168 + i * kw, 140 + ex, kw, 44);
}

void LfoPanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, pal::panel);
    const int ex = extraH();
    vLine(g, 89, 3, 114 + ex, pal::edgeDark);
    drawText(g, "HOST", 5, 166 + ex, pal::textMuted);
    hLine(g, 3, 136 + ex, r.getWidth() - 6, pal::edgeDark);
    drawText(g, "SHAPE", 93, 124 + ex, pal::textMuted);
    drawText(g, "LFO " + juce::String(ctx.selectedLfo + 1), 124, 124 + ex, pal::violetHot, 1, true);
}

// ==================================================================== MapPanel
MapPanel::MapPanel(UiContext& c) : Panel(c) {
    mono_ = &make<Toggle>(ctx, "MONO", Toggle::Style::check, [](const Patch& p) { return p.mono; }, [](Patch& p, bool v) { p.mono = v; });
    legato_ = &make<Toggle>(ctx, "LEGATO", Toggle::Style::check, [](const Patch& p) { return p.globals.legato; }, [](Patch& p, bool v) { p.globals.legato = v; });
    poly_ = &make<Spinner>(ctx, "POLY", 1, 64, 1, [](const Patch& p) { return double(p.polyphony); },
        [](Patch& p, double v) { p.polyphony = int(v); }, [](double v) { return juce::String(int(v)); });
    poly_->showArrows(true);
}

void MapPanel::refresh(const Patch& p) {
    Panel::refresh(p);
    routes_ = ctx.selectedMap == 0 ? countRoutes(p, ModSource::velocity, 0) : countRoutes(p, ModSource::note, 0);
    poly_n_ = p.polyphony;
    repaint();
}

void MapPanel::frame() { const int a = ctx.proc.getActiveVoiceCount(); if (a != active_) { active_ = a; repaint(0, 128, getWidth(), 12); } }

void MapPanel::resized() {
    const int w = getWidth();
    const int ex = extraH();
    mono_->setBounds(8, 148 + ex, 56, 12); legato_->setBounds(8, 166 + ex, 60, 12);
    poly_->setBounds(w - 62, 146 + ex, 58, 15);
}

void MapPanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    drawModuleFrame(g, r, pal::panel);
    const int ex = extraH();
    const juce::Rectangle<int> graph(3, 3, r.getWidth() - 6, 116 + ex);
    paintRetro(screen_, g, graph, RetroStyle::violetCrt(), [&](juce::Graphics& g) {
        const auto local = graph.withZeroOrigin();
        wellBox(g, local, pal::violetWell);
        const auto in = local.reduced(3);
        for (int i = 1; i < 4; ++i) { const int x = in.getX() + in.getWidth() * i / 4; for (int y = in.getY(); y < in.getBottom(); y += 3) px(g, x, y, pal::violetShadow); }
        for (int i = 1; i < 4; ++i) { const int y = in.getY() + in.getHeight() * i / 4; for (int x = in.getX(); x < in.getRight(); x += 3) px(g, x, y, pal::violetShadow); }
        pixelLine(g, in.getX(), in.getBottom() - 1, in.getRight() - 1, in.getY(), pal::violetHot);
        for (int x = in.getX(); x < in.getRight(); ++x) { const int y = in.getBottom() - 1 - (x - in.getX()) * (in.getHeight() - 1) / (in.getWidth() - 1); g.setColour(mix(pal::violetShadow, pal::violetWell, 0.3f)); g.fillRect(x, y + 1, 1, in.getBottom() - y - 1); }
        drawText(g, ctx.selectedMap == 0 ? "VELOCITY" : "NOTE", in.getX() + 3, in.getY() + 3, pal::violetHot, 1, true);
        drawText(g, juce::String(routes_) + (routes_ == 1 ? " ROUTE" : " ROUTES"), in.getX() + 3, in.getY() + 13, pal::textBody);
        drawSmall(g, "LINEAR MAPPING", in.getX() + 3, in.getBottom() - 8, pal::textMuted);
    });
    // voicing
    drawText(g, "VOICING", 8, 126 + ex, pal::textHi, 1, true);
    hLine(g, 3, 139 + ex, r.getWidth() - 6, pal::edgeMid);
    drawTextIn(g, juce::String(active_) + " / " + juce::String(poly_n_), {r.getWidth() - 70, 164 + ex, 66, 12}, pal::lcd, juce::Justification::centredRight);
    drawSmall(g, "VOICES", r.getWidth() - 36, 178 + ex, pal::textMuted);
}

// ================================================================= ModSection
ModSection::ModSection(UiContext& c) : Panel(c) {
    macros_ = &make<MacroPanel>(ctx);
    env_ = &make<EnvPanel>(ctx);
    lfo_ = &make<LfoPanel>(ctx);
    map_ = &make<MapPanel>(ctx);
    for (int i = 0; i < 16; ++i) {
        auto* t = &make<TabButton>();
        t->setText(i < 4 ? "ENV " + juce::String(i + 1) : i < 14 ? juce::String(i - 3) : i == 14 ? "VELO" : "NOTE");
        t->setAccent(pal::violetHot);
        t->setLedShown(false);
        t->onClick = [this, i] { select(i); };
        t->onDragOut = [this, t, i] {
            if (i < 4) startModDrag(*t, ModSource::envelope, i, ctx.uiScale);
            else if (i < 14) startModDrag(*t, ModSource::lfo, i - 4, ctx.uiScale);
            else startModDrag(*t, i == 14 ? ModSource::velocity : ModSource::note, 0, ctx.uiScale);
        };
        tabs_[std::size_t(i)] = t;
    }
    ctx.selectModTab = [this](int i) { select(i); };
}

void ModSection::select(int tab) {
    if (tab < 4) ctx.selectedEnv = tab;
    else if (tab < 14) ctx.selectedLfo = tab - 4;
    else ctx.selectedMap = tab - 14;
    if (ctx.patch) refresh(*ctx.patch);
}

void ModSection::refresh(const Patch& p) {
    Panel::refresh(p);
    for (int i = 0; i < 16; ++i) {
        auto* t = tabs_[std::size_t(i)];
        const bool sel = i < 4 ? ctx.selectedEnv == i : i < 14 ? ctx.selectedLfo == i - 4 : ctx.selectedMap == i - 14;
        t->setSelected(sel);
        int n = 0;
        if (i < 4) n = countRoutes(p, ModSource::envelope, i);
        else if (i < 14) n = countRoutes(p, ModSource::lfo, i - 4);
        else n = countRoutes(p, i == 14 ? ModSource::velocity : ModSource::note, 0);
        t->setBadge(n);
    }
}

void ModSection::resized() {
    const int w = getWidth();
    const int extra = std::max(0, w - 1000);
    const int envW = 292 + int(extra * 0.4), mapW = 125;
    const int lfoW = w - 3 - 136 - 1 - envW - 2 - mapW - 4 - 1;
    const int envX = 140, lfoX = envX + envW + 2, mapX = w - mapW - 4;
    const int h = getHeight();
    macros_->setBounds(3, 0, 136, h);
    for (int i = 0; i < 4; ++i) tabs_[std::size_t(i)]->setBounds(envX + i * envW / 4, 0, envW / 4, 18);
    for (int i = 0; i < 10; ++i) tabs_[std::size_t(4 + i)]->setBounds(lfoX + i * lfoW / 10, 0, (i + 1) * lfoW / 10 - i * lfoW / 10, 18);
    tabs_[14]->setBounds(mapX, 0, mapW / 2, 18); tabs_[15]->setBounds(mapX + mapW / 2, 0, mapW - mapW / 2, 18);
    env_->setBounds(envX, 20, envW, h - 20);
    lfo_->setBounds(lfoX, 20, lfoW, h - 20);
    map_->setBounds(mapX, 20, mapW, h - 20);
}

void ModSection::paint(juce::Graphics& g) {
    g.setColour(pal::chassis); g.fillRect(getLocalBounds());
    hLine(g, 0, 18, getWidth(), pal::edgeMid);
    hLine(g, 0, 0, getWidth(), pal::edgeMid);
}

}
