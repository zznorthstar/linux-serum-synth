#include "Widgets.h"
#include "ModCatalog.h"
#include <cmath>

namespace zyg::ui {

// ---------------------------------------------------------------- context
bool UiContext::refreshPatch() {
    auto p = proc.getPatch();
    if (p == patch) return false;
    setPatch(std::move(p));
    undo_.clear(); redo_.clear();      // external change: previous states no longer follow from this one
    return true;
}

void UiContext::edit(const void* key, std::function<void(Patch&)> fn) {
    if (key)
        for (auto& e : pending_) if (e.key == key) { e.fn = std::move(fn); return; }
    pending_.push_back({key, std::move(fn)});
}

void UiContext::editNow(std::function<void(Patch&)> fn) {
    pending_.push_back({nullptr, std::move(fn)});
    flush();
}

bool UiContext::flush() {
    if (pending_.empty()) return false;
    auto queue = std::move(pending_);
    pending_.clear();
    const auto now = juce::Time::getMillisecondCounter();
    if (patch && now - lastUndoPush_ > 600) {      // one undo step per burst of edits
        undo_.push_back(patch);
        if (undo_.size() > 100) undo_.erase(undo_.begin());
    }
    lastUndoPush_ = now;
    redo_.clear();
    proc.editPatch([&queue](Patch& p) { for (auto& e : queue) e.fn(p); });
    setPatch(proc.getPatch());
    return true;
}

void UiContext::undo() {
    if (undo_.empty()) return;
    auto target = undo_.back(); undo_.pop_back();
    redo_.push_back(patch);
    proc.editPatch([&target](Patch& p) { p = *target; });
    setPatch(proc.getPatch());
    lastUndoPush_ = 0;
}

void UiContext::redo() {
    if (redo_.empty()) return;
    auto target = redo_.back(); redo_.pop_back();
    undo_.push_back(patch);
    proc.editPatch([&target](Patch& p) { p = *target; });
    setPatch(proc.getPatch());
    lastUndoPush_ = 0;
}

TargetId normalizedTarget(const ModulationRoute& r) noexcept {
    TargetId t {r.targetKind, r.targetIndex, r.targetParam};
    switch (r.targetKind) {
        case ModTarget::wavetablePosition: t.kind = ModTarget::oscParam; t.param = int(OscParam::tablePos); break;
        case ModTarget::warpOneAmount: t.kind = ModTarget::oscParam; t.param = int(OscParam::warp1); break;
        case ModTarget::warpTwoAmount: t.kind = ModTarget::oscParam; t.param = int(OscParam::warp2); break;
        case ModTarget::filterCutoff: t.kind = ModTarget::filterParam; t.param = int(FilterParam::freq); break;
        default: break;
    }
    return t;
}

bool modulationSpan(const Patch& p, TargetId id, float& lowDelta, float& highDelta) {
    lowDelta = highDelta = 0.0f;
    bool any = false;
    for (const auto& r : p.modulation) {
        if (r.bypass || normalizedTarget(r) != id) continue;
        const float a = float(r.amount / 100.0);
        const bool bi = r.bipolar || modSourceIsBipolar(r.sourceKind);
        any = true;
        if (bi) { lowDelta -= std::abs(a); highDelta += std::abs(a); }
        else if (a < 0) lowDelta += a; else highDelta += a;
    }
    return any;
}

// ------------------------------------------------------------- look & feel
PixelLookAndFeel::PixelLookAndFeel() {
    setColour(juce::PopupMenu::backgroundColourId, pal::panel);
    setColour(juce::PopupMenu::textColourId, pal::textHi);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, pal::violet);
    setColour(juce::PopupMenu::highlightedTextColourId, pal::acidHot);
    setColour(juce::ScrollBar::thumbColourId, pal::edgeLight);
    setColour(juce::TextEditor::backgroundColourId, pal::sunken);
    setColour(juce::TextEditor::textColourId, pal::lcd);
    setColour(juce::TextEditor::highlightColourId, pal::violet);
    setColour(juce::CaretComponent::caretColourId, pal::acid);
}

void PixelLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(pal::panel);
    frame(g, {0, 0, width, height}, pal::edgeLight);
    frame(g, {1, 1, width - 2, height - 2}, pal::edgeDark);
}

void PixelLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int,
                                                 int& idealWidth, int& idealHeight) {
    idealHeight = isSeparator ? 5 : 13;
    idealWidth = isSeparator ? 20 : textWidth(text.upToFirstOccurrenceOf("\t", false, false)) + 26;
}

void PixelLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
        bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
        const juce::String& shortcut, const juce::Drawable*, const juce::Colour*) {
    if (isSeparator) {
        hLine(g, area.getX() + 3, area.getCentreY(), area.getWidth() - 6, pal::edgeMid);
        return;
    }
    const bool hot = isHighlighted && isActive;
    if (hot) { g.setColour(pal::violet); g.fillRect(area); }
    const auto colour = !isActive ? pal::textMuted : hot ? pal::acidHot : isTicked ? pal::acid : pal::textHi;
    if (isTicked) {
        g.setColour(hot ? pal::acidHot : pal::acid);
        g.fillRect(area.getX() + 4, area.getCentreY() - 1, 3, 3);
    }
    drawTextIn(g, text, area.withTrimmedLeft(11).withTrimmedRight(hasSubMenu ? 10 : 4), colour);
    if (shortcut.isNotEmpty()) drawTextIn(g, shortcut, area.withTrimmedRight(4), pal::textMuted, juce::Justification::centredRight);
    if (hasSubMenu) drawIconCentred(g, Icon::arrowRight, juce::Rectangle<int>(area).removeFromRight(10), colour);
}

void PixelLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name) {
    g.setColour(pal::raised); g.fillRect(area);
    drawTextIn(g, name, area.withTrimmedLeft(5), pal::violetHot, juce::Justification::centredLeft);
}

void PixelLookAndFeel::drawPopupMenuUpDownArrow(juce::Graphics& g, int width, int height, bool up) {
    g.fillAll(pal::panel);
    drawIconCentred(g, up ? Icon::caretUp : Icon::caretDown, {0, 0, width, height}, pal::acid);
}

void PixelLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
        bool vertical, int thumbStart, int thumbSize, bool over, bool down) {
    g.setColour(pal::sunken); g.fillRect(x, y, width, height);
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int>(x + 1, y + thumbStart, width - 2, thumbSize)
                                          : juce::Rectangle<int>(x + thumbStart, y + 1, thumbSize, height - 2);
    bevelBox(g, thumb, down ? pal::violet : over ? pal::edgeLight : pal::raisedHi);
}

void PixelLookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool over, bool dragging) {
    const auto c = dragging ? pal::acid : over ? pal::violetHot : pal::edgeLight;
    for (int i = 0; i < 3; ++i)          // three diagonal pixel rows in the bottom-right corner
        for (int k = 0; k <= i; ++k) { g.setColour(c); g.fillRect(w - 3 - k * 3, h - 3 - (i - k) * 3, 2, 2); }
}

void PixelLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    wellBox(g, {0, 0, w, h});
}

// ------------------------------------------------------------------ helpers
juce::String formatNumber(double v, int maxDecimals) {
    if (std::abs(v) < 0.0005) return "0";
    juce::String s(v, maxDecimals);
    if (s.contains(".")) s = s.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    if (s == "-0") s = "0";
    return s;
}

int NumericEntry::key(const juce::KeyPress& k) {
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::tabKey) { active = false; return 1; }
    if (k == juce::KeyPress::escapeKey) { active = false; return -1; }
    if (k == juce::KeyPress::backspaceKey) { text = fresh ? juce::String() : text.dropLastCharacters(1); fresh = false; return 0; }
    const auto c = k.getTextCharacter();
    if (c >= 32 && c < 127) {
        if (fresh) text.clear();
        fresh = false;
        if (text.length() < 14) text += juce::String::charToString(c);
    }
    return 0;
}

static double parseNumber(const juce::String& s, double fallback) {
    const auto cleaned = s.retainCharacters("0123456789.-+");
    if (cleaned.isEmpty() || !cleaned.containsAnyOf("0123456789")) return fallback;
    double v = cleaned.getDoubleValue();
    const auto lower = s.toLowerCase();
    if (lower.contains("k") && !lower.contains("db")) v *= 1000.0;
    return v;
}

void drawLcd(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour colour,
             juce::Justification j) {
    wellBox(g, r);
    drawTextIn(g, text, r.reduced(3, 1), colour, j);
}

void drawModuleFrame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill) {
    bevelBox(g, r, fill, pal::edgeMid, pal::edgeDark);
}

void drawCaption(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, juce::Colour colour) {
    const int w = textWidth(text, 1, true);
    const int cx = r.getCentreX(), y = r.getCentreY();
    drawText(g, text, cx - w / 2, y - 2, colour, 1, true);
    hLine(g, r.getX() + 2, y, std::max(0, cx - w / 2 - 6 - r.getX()), pal::edgeMid);
    hLine(g, cx + w / 2 + 5, y, std::max(0, r.getRight() - 2 - (cx + w / 2 + 5)), pal::edgeMid);
}

// -------------------------------------------------------------------- Knob
Knob::Knob(UiContext& c, KnobSpec spec, Getter get, Setter set, int diameter)
    : ModDest(c, *this), ctx_(c), spec_(std::move(spec)), get_(std::move(get)), set_(std::move(set)), diameter_(diameter) {
    value_ = spec_.def;
    style_.bipolar = spec_.bipolar;
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(false);
}

double Knob::toNorm(double v) const {
    v = juce::jlimit(spec_.lo, spec_.hi, v);
    double t;
    if (spec_.logScale && spec_.lo > 0.0) t = std::log(v / spec_.lo) / std::log(spec_.hi / spec_.lo);
    else {
        t = (v - spec_.lo) / (spec_.hi - spec_.lo);
        if (spec_.skew != 1.0) t = std::pow(t, spec_.skew);
    }
    return juce::jlimit(0.0, 1.0, t);
}

double Knob::fromNorm(double t) const {
    t = juce::jlimit(0.0, 1.0, t);
    double v;
    if (spec_.logScale && spec_.lo > 0.0) v = spec_.lo * std::pow(spec_.hi / spec_.lo, t);
    else v = spec_.lo + (spec_.hi - spec_.lo) * (spec_.skew != 1.0 ? std::pow(t, 1.0 / spec_.skew) : t);
    if (spec_.step > 0) v = spec_.lo + std::round((v - spec_.lo) / spec_.step) * spec_.step;
    return juce::jlimit(spec_.lo, spec_.hi, v);
}

juce::String Knob::valueText() const { return spec_.format ? spec_.format(value_) : formatNumber(value_); }

void Knob::commit(double v) {
    v = juce::jlimit(spec_.lo, spec_.hi, v);
    if (v == value_) return;
    value_ = v;
    ctx_.edit(this, [set = set_, v](Patch& p) { set(p, v); });
    repaint();
}

void Knob::pull(const Patch& p) {
    if (dragging_ || entry_.active) return;
    const double v = get_(p);
    float lo = 0.0f, hi = 0.0f;
    float ml = 0.0f, mh = 0.0f;
    if (hasTarget_ && modulationSpan(p, target_, lo, hi)) {
        const float t = float(toNorm(v));
        ml = juce::jlimit(0.0f, 1.0f, t + lo); mh = juce::jlimit(0.0f, 1.0f, t + hi);
    }
    if (v != value_ || ml != modLo_ || mh != modHi_) { value_ = v; modLo_ = ml; modHi_ = mh; repaint(); }
}

void Knob::paint(juce::Graphics& g) {
    const int d = diameter_;
    KnobStyle st = style_; st.bipolar = spec_.bipolar; st.dim = dim_;
    const int kx = (getWidth() - d) / 2;
    if (hoverF_.v() > 0.02f || dragging_) {   // soft hover ring
        const float a = dragging_ ? 1.0f : hoverF_.v();
        const float R = float(d) * 0.5f + 1.5f;
        g.setColour(pal::violetHot.withAlpha(0.65f * a));
        for (int i = 0; i < 24; ++i) {
            const float t = float(i) / 24.0f * 6.2831853f;
            g.fillRect(int(std::floor(float(kx) + float(d) * 0.5f + std::sin(t) * R)), int(std::floor(float(d) * 0.5f - std::cos(t) * R)), 1, 1);
        }
    }
    if (hasTarget_ && (dropHover_ || modDragActive())) {   // valid drop target highlight
        const float R = float(d) * 0.5f + 2.5f;
        g.setColour((dropHover_ ? pal::acidHot : pal::violetHot).withAlpha(dropHover_ ? 1.0f : 0.55f));
        for (int i = 0; i < 32; ++i) {
            const float t = float(i) / 32.0f * 6.2831853f;
            g.fillRect(int(std::floor(float(kx) + float(d) * 0.5f + std::sin(t) * R)), int(std::floor(float(d) * 0.5f - std::cos(t) * R)), 1, 1);
        }
    }
    blit(g, knobImage(d, float(toNorm(value_)), st, modLo_, modHi_), kx, 0);
    if (!labelShown_) return;
    const juce::Rectangle<int> lr(0, d + 2, getWidth(), fontHeight);
    if (entry_.active) {
        const auto s = entry_.text + "_";
        wellBox(g, lr.expanded(0, 2).withWidth(getWidth()));
        drawTextIn(g, s, lr.reduced(2, 0), pal::lcd, juce::Justification::centred);
    } else if (dragging_ || hover_) drawTextIn(g, valueText(), lr, pal::lcd, juce::Justification::centred);
    else drawTextIn(g, spec_.label, lr, dim_ ? pal::textMuted : pal::textBody, juce::Justification::centred);
}

void Knob::mouseDown(const juce::MouseEvent& e) {
    if (entry_.active) return;
    if (e.mods.isPopupMenu()) { showModMenu([this] { commit(spec_.def); ctx_.flush(); }); return; }
    if (e.mods.isAltDown() || e.mods.isCtrlDown()) { commit(spec_.def); ctx_.flush(); return; }
    dragging_ = true;
    dragStartT_ = toNorm(value_);
    repaint();
}

void Knob::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const double sens = e.mods.isShiftDown() ? 1.0 / 1600.0 : 1.0 / 160.0;
    const double delta = double(e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) * sens;
    commit(fromNorm(dragStartT_ + delta));
}

// ------------------------------------------------------------ ModDest
bool ModDest::isInterestedInDragSource(const SourceDetails& d) {
    ModSource k; int i;
    if (!hasTarget_ || !parseDragPayload(d.description, k, i)) return false;
    return !(k == ModSource::macro && target_.kind == ModTarget::macroValue && target_.inst == i);   // no self-modulation
}

void ModDest::itemDropped(const SourceDetails& d) {
    dropHover_ = false; owner_.repaint();
    ModSource k; int i;
    if (!parseDragPayload(d.description, k, i)) return;
    const TargetId t = target_;
    bool created = false; int slot = -1;
    mctx_.editNow([&](Patch& p) { slot = addModulationRoute(p, k, i, t, created); });
    const auto label = sourceLabel(k, i) + " -> " + (mctx_.patch ? targetLabel(*mctx_.patch, t) : juce::String());
    if (mctx_.toast) mctx_.toast(slot < 0 ? juce::String("MATRIX FULL") : created ? label : "ALREADY ROUTED: " + label);
}

bool ModDest::modSpan(const Patch& p, float& lo, float& hi) const {
    lo = hi = 0.0f;
    return hasTarget_ && modulationSpan(p, target_, lo, hi);
}

void ModDest::paintDropFrame(juce::Graphics& g, juce::Rectangle<int> r) const {
    if (!hasTarget_ || !(dropHover_ || modDragActive())) return;
    const auto c = (dropHover_ ? pal::acidHot : pal::violetHot).withAlpha(dropHover_ ? 1.0f : 0.6f);
    for (int x = r.getX(); x < r.getRight(); x += 2) { px(g, x, r.getY(), c); px(g, x, r.getBottom() - 1, c); }
    for (int y = r.getY(); y < r.getBottom(); y += 2) { px(g, r.getX(), y, c); px(g, r.getRight() - 1, y, c); }
}

void ModDest::showModMenu(std::function<void()> reset) {
    if (!mctx_.patch) return;
    juce::PopupMenu m;
    auto& ctx = mctx_;
    const TargetId t = target_;
    auto safe = juce::Component::SafePointer<juce::Component>(&owner_);
    if (hasTarget_) {
        // MOD SOURCE: every source in the catalog, grouped; ticked when already routed here
        juce::PopupMenu src; juce::PopupMenu group; juce::String groupName;
        auto flush = [&] { if (groupName.isNotEmpty()) src.addSubMenu(groupName, group); group = juce::PopupMenu(); };
        for (const auto& e : sourceCatalog()) {
            if (groupName != e.group) { flush(); groupName = e.group; }
            if (e.kind == ModSource::macro && t.kind == ModTarget::macroValue && t.inst == e.index) continue;
            bool routed = false;
            for (const auto& r : ctx.patch->modulation)
                if (r.targetKind != ModTarget::unknown && normalizedTarget(r) == t && r.sourceKind == e.kind && r.sourceIndex == e.index) routed = true;
            const auto kind = e.kind; const int idx = e.index;
            group.addItem(e.label, true, routed, [&ctx, t, kind, idx, safe] {
                if (!safe) return;
                bool created = false; int slot = -1;
                ctx.editNow([&](Patch& p) { slot = addModulationRoute(p, kind, idx, t, created); });
                const auto label = sourceLabel(kind, idx) + " -> " + (ctx.patch ? targetLabel(*ctx.patch, t) : juce::String());
                if (ctx.toast) ctx.toast(slot < 0 ? juce::String("MATRIX FULL") : created ? label : "ALREADY ROUTED: " + label);
            });
        }
        flush();
        m.addSubMenu("MOD SOURCE", src);
        // routes already on this destination
        bool any = false;
        for (std::size_t s = 0; s < ctx.patch->modulation.size(); ++s) {
            const auto& r = ctx.patch->modulation[s];
            if (r.targetKind == ModTarget::unknown || normalizedTarget(r) != t) continue;
            if (!any) { m.addSeparator(); m.addItem("MODULATION ON THIS CONTROL", false, false, [] {}); any = true; }
            juce::PopupMenu sub;
            const int slot = int(s);
            auto act = [&ctx, slot](std::function<void(ModulationRoute&)> fn) {
                return [&ctx, slot, fn] { ctx.editNow([slot, fn](Patch& p) { if (slot < int(p.modulation.size())) fn(p.modulation[std::size_t(slot)]); }); };
            };
            for (double a : {25.0, 50.0, 75.0, 100.0, -25.0, -50.0, -100.0})
                sub.addItem("AMOUNT " + juce::String(int(a)) + "%", true, std::abs(r.amount - a) < 0.5, act([a](ModulationRoute& q) { q.amount = a; }));
            sub.addSeparator();
            sub.addItem("BIPOLAR", true, r.bipolar, act([](ModulationRoute& q) { q.bipolar = !q.bipolar; }));
            sub.addItem(r.bypass ? "ENABLE" : "BYPASS", act([](ModulationRoute& q) { q.bypass = !q.bypass; }));
            sub.addItem("REMOVE", act([](ModulationRoute& q) { const int keep = q.slot; q = ModulationRoute{}; q.slot = keep; }));
            m.addSubMenu(sourceLabel(r.sourceKind, r.sourceIndex) + "  " + juce::String(int(std::round(r.amount))) + "%" + (r.bypass ? "  (OFF)" : ""), sub);
        }
        if (any && ctx.gotoPage) m.addItem("OPEN IN MATRIX", [&ctx] { ctx.gotoPage(3); });
        m.addSeparator();
    }
    m.addItem("RESET VALUE", [reset, safe] { if (safe && reset) reset(); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&owner_).withStandardItemHeight(15));
}

void Knob::mouseUp(const juce::MouseEvent&) {
    dragging_ = false;
    ctx_.flush();
    repaint();
}

void Knob::mouseDoubleClick(const juce::MouseEvent&) {
    if (entry_.active) return;
    dragging_ = false;
    entry_.begin(valueText());
    setWantsKeyboardFocus(true);
    grabKeyboardFocus();
    repaint();
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const double notch = e.mods.isShiftDown() ? 0.004 : 0.03;
    if (spec_.step > 0) {
        const double dir = w.deltaY > 0 ? 1.0 : w.deltaY < 0 ? -1.0 : 0.0;
        commit(value_ + dir * spec_.step);
    } else commit(fromNorm(toNorm(value_) + double(w.deltaY) * notch * 10.0 / 3.0));
    ctx_.flush();
}

bool Knob::keyPressed(const juce::KeyPress& k) {
    if (!entry_.active) return false;
    const int r = entry_.key(k);
    if (r == 1) {
        setWantsKeyboardFocus(false);
        double v = spec_.parse ? value_ : parseNumber(entry_.text, value_);
        if (spec_.parse) spec_.parse(entry_.text, v);
        commit(spec_.step > 0 ? spec_.lo + std::round((v - spec_.lo) / spec_.step) * spec_.step : v);
        ctx_.flush();
    } else if (r == -1) setWantsKeyboardFocus(false);
    repaint();
    return true;
}

void Knob::focusLost(FocusChangeType) {
    if (entry_.active) { entry_.active = false; setWantsKeyboardFocus(false); repaint(); }
}

// ----------------------------------------------------------------- Spinner
Spinner::Spinner(UiContext& c, juce::String label, double lo, double hi, double step, Getter get, Setter set,
                 std::function<juce::String(double)> format)
    : ModDest(c, *this), ctx_(c), label_(std::move(label)), lo_(lo), hi_(hi), step_(step), get_(std::move(get)), set_(std::move(set)),
      format_(std::move(format)) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void Spinner::pull(const Patch& p) {
    if (dragging_ || entry_.active) return;
    const double v = get_(p);
    float lo = 0.0f, hi = 0.0f, ml = 0.0f, mh = 0.0f;
    if (modSpan(p, lo, hi)) {
        const float t = float(juce::jlimit(0.0, 1.0, (v - lo_) / std::max(1.0e-9, hi_ - lo_)));
        ml = juce::jlimit(0.0f, 1.0f, t + lo); mh = juce::jlimit(0.0f, 1.0f, t + hi);
        if (mh <= ml) mh = ml + 0.001f;
    }
    if (v != value_ || ml != modLo_ || mh != modHi_) { value_ = v; modLo_ = ml; modHi_ = mh; repaint(); }
}

void Spinner::commit(double v) {
    v = juce::jlimit(lo_, hi_, v);
    if (step_ > 0) v = lo_ + std::round((v - lo_) / step_) * step_;
    v = juce::jlimit(lo_, hi_, v);
    if (v == value_) return;
    value_ = v;
    ctx_.edit(this, [set = set_, v](Patch& p) { set(p, v); });
    repaint();
}

void Spinner::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    if (caption_.isNotEmpty()) {
        drawTextIn(g, caption_, r.removeFromBottom(fontHeight + 2).withTrimmedTop(2), dim_ ? pal::textMuted : pal::textBody,
                   juce::Justification::centred);
    }
    wellBox(g, r);
    int x = 3;
    if (hasIcon_) { drawIconCentred(g, icon_, {x, 0, iconSize(icon_).x, r.getHeight()}, dim_ ? pal::textMuted : pal::textBody); x += iconSize(icon_).x + 4; }
    if (label_.isNotEmpty()) {
        drawTextIn(g, label_, {x, 0, textWidth(label_) + 1, r.getHeight()}, pal::textMuted);
        x += std::max(labelWidth_, textWidth(label_) + 4);
    }
    const int right = r.getRight() - (arrows_ ? 9 : 3);
    const auto text = entry_.active ? entry_.text + "_" : (format_ ? format_(value_) : formatNumber(value_, 1));
    drawTextIn(g, text, {x, 0, right - x, r.getHeight()}, entry_.active ? pal::acidHot : dim_ ? pal::textMuted : valueColour_,
               centred_ ? juce::Justification::centred : juce::Justification::centredLeft);
    if (arrows_) {
        vLine(g, right + 1, 1, r.getHeight() - 2, pal::edgeMid);
        drawIconCentred(g, Icon::caretUp, {right + 2, 1, 7, r.getHeight() / 2}, pal::textBody);
        drawIconCentred(g, Icon::caretDown, {right + 2, r.getHeight() / 2, 7, r.getHeight() / 2}, pal::textBody);
    }
    if (modHi_ > modLo_) {   // modulation range under the field
        const int w = r.getWidth() - 4, x0 = 2 + int(std::round(modLo_ * float(w))), x1 = 2 + int(std::round(modHi_ * float(w)));
        g.setColour(pal::violetHot); g.fillRect(x0, r.getBottom() - 2, std::max(2, x1 - x0), 1);
    }
    paintDropFrame(g, r);
}

void Spinner::mouseDown(const juce::MouseEvent& e) {
    if (entry_.active) return;
    if (e.mods.isPopupMenu()) { showModMenu([this] { commit(def_); ctx_.flush(); }); return; }
    if (e.mods.isAltDown()) { commit(def_); ctx_.flush(); return; }
    if (arrows_ && e.x >= getWidth() - 9) {
        commit(value_ + (e.y < getHeight() / 2 ? step_ : -step_));
        ctx_.flush();
        return;
    }
    dragging_ = true;
    dragStart_ = value_;
}

void Spinner::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const double px = e.mods.isShiftDown() ? 40.0 : 5.0;
    commit(dragStart_ - std::round(double(e.getDistanceFromDragStartY()) / px) * step_);
    if (step_ <= 0) commit(dragStart_ - double(e.getDistanceFromDragStartY()) / px * ((hi_ - lo_) / 100.0));
}

void Spinner::mouseDoubleClick(const juce::MouseEvent&) {
    dragging_ = false;
    entry_.begin(format_ ? format_(value_) : formatNumber(value_, 2));
    setWantsKeyboardFocus(true);
    grabKeyboardFocus();
    repaint();
}

void Spinner::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    const double dir = w.deltaY > 0 ? 1.0 : w.deltaY < 0 ? -1.0 : 0.0;
    commit(value_ + dir * (step_ > 0 ? step_ : (hi_ - lo_) / 100.0));
    ctx_.flush();
}

bool Spinner::keyPressed(const juce::KeyPress& k) {
    if (!entry_.active) return false;
    const int r = entry_.key(k);
    if (r == 1) {
        setWantsKeyboardFocus(false);
        commit(parseNumber(entry_.text, value_));
        ctx_.flush();
    } else if (r == -1) setWantsKeyboardFocus(false);
    repaint();
    return true;
}

void Spinner::focusLost(FocusChangeType) {
    if (entry_.active) { entry_.active = false; setWantsKeyboardFocus(false); repaint(); }
}

// ------------------------------------------------------------------ Toggle
Toggle::Toggle(UiContext& c, juce::String label, Style style, BoolGetter get, BoolSetter set)
    : ctx_(c), label_(std::move(label)), style_(style), get_(std::move(get)), set_(std::move(set)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void Toggle::pull(const Patch& p) { if (get_) setOn(get_(p)); }

void Toggle::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    on_state_ = !on_state_;
    onF_.to(on_state_ ? 1.0f : 0.0f);   // the later pull() sees no change, so the fade must start here
    if (set_) ctx_.edit(this, [set = set_, v = on_state_](Patch& p) { set(p, v); });
    ctx_.flush();
    if (onToggle) onToggle(on_state_);
    repaint();
}

void Toggle::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const float on = onF_.v(), hov = hoverF_.v();
    const auto textCol = mix(mix(pal::textMuted, pal::textBody, hov), pal::textHi, on);
    switch (style_) {
        case Style::led: {
            const juce::Rectangle<int> box(1, (r.getHeight() - 8) / 2, 8, 8);
            if (on > 0.05f) { g.setColour(on_.withAlpha(0.28f * on)); g.fillRect(box.expanded(1)); }
            g.setColour(pal::edgeDark); g.fillRect(box);
            g.setColour(mix(pal::sunken, on_, on)); g.fillRect(box.reduced(1));
            if (on < 0.98f) frame(g, box, mix(pal::edgeMid, pal::edgeLight, hov));
            if (on > 0.05f) {
                g.setColour(pal::acidHot.withAlpha(on));
                g.fillRect(box.getX() + 1, box.getY() + 1, 1, 1);
                g.setColour(mix(on_, pal::acidHot, 0.5f).withAlpha(on));
                g.fillRect(box.getX() + 2, box.getY() + 1, 1, 1); g.fillRect(box.getX() + 1, box.getY() + 2, 1, 1);
            }
            if (label_.isNotEmpty()) drawTextIn(g, label_, r.withTrimmedLeft(12), textCol);
            break;
        }
        case Style::check: {
            const juce::Rectangle<int> box(0, (r.getHeight() - 7) / 2, 7, 7);
            wellBox(g, box, pal::sunken);
            if (hov > 0.05f && on < 0.5f) frame(g, box, pal::edgeLight.withAlpha(hov));
            if (on > 0.05f) {
                g.setColour(on_.withAlpha(on));
                for (auto p : std::initializer_list<juce::Point<int>>{{1, 3}, {2, 4}, {3, 3}, {4, 2}, {5, 1}})
                    g.fillRect(box.getX() + p.x, box.getY() + p.y, 1, 1);
            }
            if (label_.isNotEmpty()) drawTextIn(g, label_, r.withTrimmedLeft(10), textCol);
            break;
        }
        case Style::block: {
            const auto fill = mix(mix(pal::raised, pal::raisedHi, hov), pal::violetShadow, on);
            bevelBox(g, r, fill, mix(pal::edgeLight, pal::violet, on));
            const auto col = mix(mix(pal::textBody, pal::textHi, hov), pal::acidHot, on);
            if (hasIcon_) drawIconCentred(g, icon_, r, col);
            else drawTextIn(g, label_, r, isEnabled() ? col : pal::textMuted, juce::Justification::centred, 1, true);
            break;
        }
    }
}

// ------------------------------------------------------------- PixelButton
void PixelButton::mouseUp(const juce::MouseEvent& e) {
    const bool inside = getLocalBounds().contains(e.getPosition());
    const bool wasDown = down_;
    down_ = false;
    repaint();
    if (!wasDown || !inside) return;
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); }
    else if (onClick) onClick();
}

void PixelButton::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const float hov = hoverF_.v(), lit = litF_.v();
    const float press = down_ ? 1.0f : pressF_.v();
    const float strength = std::max({hov, lit, press});
    if (!flat_ || strength > 0.02f) {
        const float a = flat_ ? strength : 1.0f;
        const auto fill = mix(mix(pal::raised, pal::raisedHi, hov), pal::violetShadow, std::max(lit, press));
        bevelBox(g, r, fill.withMultipliedAlpha(a), mix(mix(pal::edgeLight, pal::violet, lit), pal::edgeMid, press).withMultipliedAlpha(a),
                 mix(pal::edgeDark, pal::edgeLight, press).withMultipliedAlpha(a));
    }
    const auto col = mix(mix(pal::textBody, pal::textHi, hov), accent_, lit);
    auto inner = r.reduced(1);
    if (press > 0.5f) inner = inner.translated(0, 1);
    if (hasWave_) drawWaveIcon(g, wave_, inner.reduced(4, 8), mix(mix(pal::textBody, pal::textHi, hov), accent_, lit));
    if (hasIcon_) drawIconCentred(g, icon_, text_.isEmpty() ? inner : inner.removeFromLeft(iconSize(icon_).x + 6), col);
    if (text_.isNotEmpty())
        drawTextIn(g, text_, hasIcon_ ? inner.withTrimmedLeft(iconSize(icon_).x + 6) : inner, col,
                   juce::Justification::centred, 1, bold_);
    if (lit > 0.02f) {
        const int w = int(std::round(float(r.getWidth() - 2) * lit));
        g.setColour(accent_); g.fillRect(r.getCentreX() - w / 2, r.getBottom() - 2, w, 1);
    }
}

// ----------------------------------------------------------------- Chooser
void Chooser::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const float hov = hoverF_.v();
    const int arrowW = arrows_ ? 17 : 0;
    auto field = r.withTrimmedRight(arrowW);
    if (!flat_) wellBox(g, field);
    else if (hov > 0.02f) { g.setColour(pal::raised.withAlpha(hov)); g.fillRect(field); }
    if (!flat_ && hov > 0.02f) frame(g, field, pal::edgeLight.withAlpha(hov * 0.9f));
    const auto col = dimmed_ ? pal::textMuted : dimText_ ? pal::textBody : textColour_;
    const auto textArea = field.withTrimmedLeft(4).withTrimmedRight(9);
    drawTextIn(g, text_, textArea, col, centred_ ? juce::Justification::centred : juce::Justification::centredLeft, 1, bold_);
    drawIconCentred(g, Icon::caretDown, field.removeFromRight(9), mix(pal::acidDim, pal::acid, hov));
    if (arrows_) {
        auto a = r.removeFromRight(arrowW);
        const auto c = mix(pal::textBody, pal::textHi, hov);
        drawIconCentred(g, Icon::arrowLeft, a.removeFromLeft(8), c);
        drawIconCentred(g, Icon::arrowRight, a.withTrimmedLeft(1), c);
    }
}

void Chooser::mouseDown(const juce::MouseEvent& e) {
    const int arrowW = arrows_ ? 17 : 0;
    if (arrows_ && e.x >= getWidth() - arrowW) {
        if (onStep) onStep(e.x < getWidth() - arrowW / 2 ? -1 : 1);
        return;
    }
    if (customOpen) { customOpen(); return; }
    if (!buildMenu) return;
    juce::PopupMenu m;
    buildMenu(m);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth() - arrowW)
                        .withStandardItemHeight(13));
}

void Chooser::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (onStep && w.deltaY != 0.0f) onStep(w.deltaY > 0 ? -1 : 1);
}

// ------------------------------------------------------------------- Fader
Fader::Fader(UiContext& c, Getter get, Setter set, double def)
    : ModDest(c, *this), ctx_(c), get_(std::move(get)), set_(std::move(set)), def_(def) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void Fader::pull(const Patch& p) {
    if (dragging_) return;
    const double v = get_(p);
    float lo = 0.0f, hi = 0.0f;
    const bool m = modSpan(p, lo, hi);
    if (v != value_ || m != modded_ || lo != modLo_ || hi != modHi_) { value_ = v; modded_ = m; modLo_ = lo; modHi_ = hi; repaint(); }
}

juce::Rectangle<int> Fader::track() const {
    const int tx = meters_ ? 11 : labels_ ? 3 : (getWidth() - 8) / 2;
    return {tx, 4, 8, getHeight() - 8};
}

void Fader::commit(double v) {
    v = juce::jlimit(0.0, 1.0, v);
    if (v == value_) return;
    value_ = v;
    ctx_.edit(this, [set = set_, v](Patch& p) { set(p, v); });
    repaint();
}

void Fader::setFromY(int y) {
    const auto t = track();
    commit(1.0 - double(y - t.getY()) / double(std::max(1, t.getHeight() - 1)));
}

void Fader::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { showModMenu([this] { commit(def_); ctx_.flush(); }); return; }
    if (e.mods.isAltDown()) { commit(def_); ctx_.flush(); return; }
    dragging_ = true; setFromY(e.y);
}
void Fader::mouseDrag(const juce::MouseEvent& e) { if (dragging_) setFromY(e.y); }
void Fader::mouseDoubleClick(const juce::MouseEvent&) { commit(def_); ctx_.flush(); }
void Fader::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    commit(value_ + (w.deltaY > 0 ? 1 : -1) * (e.mods.isShiftDown() ? 0.005 : 0.03)); ctx_.flush();
}

void drawMeter(juce::Graphics& g, juce::Rectangle<int> r, float left, float right, bool vertical) {
    auto db = [](float v) { return v <= 1.0e-5f ? -96.0f : 20.0f * std::log10(v); };
    const float lv[2] {left, right};
    if (vertical) {
        const int segs = r.getHeight() / 2;
        const int colW = (r.getWidth() - 1) / 2;
        for (int c = 0; c < 2; ++c) {
            const float d = db(lv[c]);
            const int lit = juce::jlimit(0, segs, int(std::round((d + 48.0f) / 48.0f * float(segs))));
            for (int s = 0; s < segs; ++s) {
                const float pos = float(s) / float(segs);
                const auto onCol = pos > 0.9f ? pal::crimson : pos > 0.72f ? pal::amber : pal::acid;
                const auto offCol = pos > 0.9f ? pal::crimsonDim : pos > 0.72f ? pal::amberDim : pal::acidShadow;
                g.setColour(s < lit ? onCol : mix(offCol, pal::sunken, 0.45f));
                g.fillRect(r.getX() + c * (colW + 1), r.getBottom() - (s + 1) * 2, colW, 1);
            }
        }
    } else {
        const int segs = r.getWidth() / 2;
        const int rowH = (r.getHeight() - 1) / 2;
        for (int c = 0; c < 2; ++c) {
            const float d = db(lv[c]);
            const int lit = juce::jlimit(0, segs, int(std::round((d + 48.0f) / 48.0f * float(segs))));
            for (int s = 0; s < segs; ++s) {
                const float pos = float(s) / float(segs);
                const auto onCol = pos > 0.9f ? pal::crimson : pos > 0.72f ? pal::amber : pal::acid;
                const auto offCol = pos > 0.9f ? pal::crimsonDim : pos > 0.72f ? pal::amberDim : pal::acidShadow;
                g.setColour(s < lit ? onCol : mix(offCol, pal::sunken, 0.45f));
                g.fillRect(r.getX() + s * 2, r.getY() + c * (rowH + 1), 1, rowH);
            }
        }
    }
}

void Fader::paint(juce::Graphics& g) {
    const auto t = track();
    if (meters_) drawMeter(g, {1, t.getY(), 7, t.getHeight()}, l_, r_);
    wellBox(g, t, pal::sunken);
    // groove
    g.setColour(pal::edgeDark); g.fillRect(t.getCentreX() - 1, t.getY() + 2, 2, t.getHeight() - 4);
    const int bottom = t.getBottom() - 3;
    const int top = t.getY() + 2;
    const int ty = bottom - int(std::round(value_ * double(bottom - top)));
    // fill below thumb
    g.setColour(dim_ ? pal::edgeMid : pal::acidDim);
    g.fillRect(t.getCentreX() - 1, ty, 2, bottom - ty + 1);
    // ticks
    struct Tick { double db; const char* s; };
    static const Tick ticks[] = {{6, "+6"}, {0, "0"}, {-6, "-6"}, {-12, "-12"}, {-24, "-24"}};
    for (const auto& k : ticks) {
        const double gain = std::pow(10.0, k.db / 20.0) / top_;
        if (gain > 1.0001 || gain < 0.02) continue;
        const int y = bottom - int(std::round(gain * double(bottom - top)));
        hLine(g, t.getRight() + 1, y, 2, k.db == 0.0 ? pal::acidDim : pal::edgeLight);
        if (labels_) drawText(g, k.s, t.getRight() + 5, y - 2, k.db == 0.0 ? pal::textBody : pal::textMuted);
    }
    // thumb
    const juce::Rectangle<int> thumb(t.getX() - 2, ty - 3, t.getWidth() + 4, 7);
    bevelBox(g, thumb, dragging_ ? pal::violet : pal::raisedHi, pal::edgeLight);
    hLine(g, thumb.getX() + 2, thumb.getCentreY(), thumb.getWidth() - 4, dim_ ? pal::textMuted : pal::acidHot);
    if (modded_) {   // modulation range as a violet column beside the groove
        const int y0 = bottom - int(std::round(juce::jlimit(0.0, 1.0, value_ + double(modHi_)) * double(bottom - top)));
        const int y1 = bottom - int(std::round(juce::jlimit(0.0, 1.0, value_ + double(modLo_)) * double(bottom - top)));
        g.setColour(pal::violetHot); g.fillRect(t.getRight() + 1, std::min(y0, y1), 2, std::max(2, std::abs(y1 - y0)));
    }
    paintDropFrame(g, t.expanded(2, 2));
}

// --------------------------------------------------------------- TabButton
void TabButton::paint(juce::Graphics& g) {
    auto r = getLocalBounds();
    const float hov = hoverF_.v(), sel = selF_.v();
    const auto fill = mix(mix(pal::chassis, pal::panelHi, hov), pal::raised, sel);
    g.setColour(fill); g.fillRect(r);
    vLine(g, 0, 0, r.getHeight(), pal::edgeDark);
    vLine(g, r.getRight() - 1, 0, r.getHeight(), pal::edgeMid);
    hLine(g, 0, 0, r.getWidth(), pal::edgeDark);
    if (sel > 0.02f) {   // accent bar grows out from the middle
        const int w = int(std::round(float(r.getWidth()) * sel));
        hLine(g, r.getCentreX() - w / 2, 0, w, accent_);
        hLine(g, r.getCentreX() - w / 2, 1, w, mix(accent_, fill, 0.6f));
    }
    if (big_) {
        const juce::Rectangle<int> led(r.getCentreX() - 2, 9, 5, 5);
        g.setColour(pal::edgeDark); g.fillRect(led.expanded(1));
        g.setColour(led_ ? accent_ : mix(pal::edgeMid, accent_, sel)); g.fillRect(led);
        if (led_ || sel > 0.5f) px(g, led.getX(), led.getY(), pal::acidHot);
        drawTextIn(g, text_, {0, 20 - int(std::round(sel)), r.getWidth(), 24}, mix(mix(pal::textMuted, pal::textBody, hov), pal::textHi, sel),
                   juce::Justification::centred, 1, true);
        if (sel > 0.02f) { const int w = int(std::round(float(r.getWidth() - 8) * sel)); g.setColour(accent_); g.fillRect(r.getCentreX() - w / 2, r.getBottom() - 3, w, 1); }
        return;
    }
    auto inner = r.reduced(3, 0);
    if (ledShown_) {
        auto led = inner.removeFromRight(9);
        const juce::Rectangle<int> b(led.getX() + 2, led.getCentreY() - 2, 5, 5);
        g.setColour(pal::edgeDark); g.fillRect(b);
        g.setColour(led_ ? accent_ : pal::edgeMid); g.fillRect(b.reduced(1));
    }
    if (handle_) drawIconCentred(g, Icon::grip, inner.removeFromLeft(5), pal::textMuted);
    if (badge_ > 0) {
        auto bb = inner.removeFromRight(11);
        const juce::Rectangle<int> circle(bb.getX() + 1, bb.getCentreY() - 4, 9, 9);
        g.setColour(pal::violetShadow); g.fillRect(circle.reduced(1, 0)); g.fillRect(circle.reduced(0, 1));
        g.setColour(pal::violetHot); g.fillRect(circle.getX() + 1, circle.getY(), circle.getWidth() - 2, 1);
        g.fillRect(circle.getX() + 1, circle.getBottom() - 1, circle.getWidth() - 2, 1);
        g.fillRect(circle.getX(), circle.getY() + 1, 1, circle.getHeight() - 2);
        g.fillRect(circle.getRight() - 1, circle.getY() + 1, 1, circle.getHeight() - 2);
        drawTextIn(g, juce::String(badge_), circle, pal::textHi, juce::Justification::centred);
    }
    drawTextIn(g, text_, inner, mix(mix(pal::textMuted, pal::textBody, hov), pal::textHi, sel),
               juce::Justification::centred, 1, true);
}

// ----------------------------------------------------------------- HSlider
HSlider::HSlider(UiContext& c, double lo, double hi, double def, Getter get, Setter set)
    : ctx_(c), lo_(lo), hi_(hi), def_(def), get_(std::move(get)), set_(std::move(set)) {
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
}

void HSlider::pull(const Patch& p) {
    if (dragging_ || !get_) return;
    const double v = get_(p);
    if (v != value_) { value_ = v; repaint(); }
}

void HSlider::commit(double v) {
    v = juce::jlimit(lo_, hi_, v);
    if (v == value_) return;
    value_ = v;
    if (set_) ctx_.edit(this, [set = set_, v](Patch& p) { set(p, v); });
    repaint();
}

void HSlider::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const int cy = r.getCentreY();
    const int x0 = 4, x1 = r.getWidth() - 5;
    g.setColour(pal::sunken); g.fillRect(x0 - 2, cy - 1, x1 - x0 + 5, 3);
    hLine(g, x0 - 2, cy - 2, x1 - x0 + 5, pal::edgeDark);
    hLine(g, x0 - 2, cy + 2, x1 - x0 + 5, pal::edgeMid);
    const double t = (value_ - lo_) / (hi_ - lo_);
    const int tx = x0 + int(std::round(t * double(x1 - x0)));
    const bool bi = lo_ < 0.0 && hi_ > 0.0;
    const int ox = bi ? x0 + int(std::round((0.0 - lo_) / (hi_ - lo_) * double(x1 - x0))) : x0;
    if (bi) vLine(g, ox, cy - 4, 9, pal::edgeMid);
    g.setColour(dim_ ? pal::textMuted : (value_ < 0 ? neg_ : pos_));
    g.fillRect(std::min(ox, tx), cy - 1, std::max(1, std::abs(tx - ox)), 3);
    const juce::Rectangle<int> thumb(tx - 3, cy - 4, 7, 9);
    bevelBox(g, thumb, dragging_ ? pal::violet : pal::raisedHi, pal::edgeLight);
    vLine(g, thumb.getCentreX(), thumb.getY() + 2, thumb.getHeight() - 4, dim_ ? pal::textMuted : pal::acidHot);
}

void HSlider::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || e.mods.isAltDown()) { commit(def_); ctx_.flush(); return; }
    dragging_ = true;
    mouseDrag(e);
}
void HSlider::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const int x0 = 4, x1 = getWidth() - 5;
    commit(lo_ + (hi_ - lo_) * juce::jlimit(0.0, 1.0, double(e.x - x0) / double(x1 - x0)));
}
void HSlider::mouseDoubleClick(const juce::MouseEvent&) { commit(def_); ctx_.flush(); }
void HSlider::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    commit(value_ + (w.deltaY > 0 ? 1 : -1) * (hi_ - lo_) * (e.mods.isShiftDown() ? 0.002 : 0.02)); ctx_.flush();
}

}
