#pragma once
// Interactive pixel-art widgets. Each widget that edits the patch is "Bound":
// it reads its value from an immutable Patch in pull() and writes changes back
// through UiContext::edit, which coalesces edits into one patch publish per tick.
#include "PixelGfx.h"
#include "Anim.h"
#include "../PluginProcessor.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace zyg::ui {

using Getter = std::function<double(const Patch&)>;
using Setter = std::function<void(Patch&, double)>;
using BoolGetter = std::function<bool(const Patch&)>;
using BoolSetter = std::function<void(Patch&, bool)>;

// ----------------------------------------------------------------- context
class UiContext {
public:
    explicit UiContext(ZygProcessor& p) : proc(p) { patch = proc.getPatch(); }
    ZygProcessor& proc;
    std::shared_ptr<const Patch> patch;

    // Reloads the newest published patch; true when it changed since last call. A change that
    // did not come from flush()/undo (e.g. a preset load) starts a fresh undo history.
    bool refreshPatch();
    unsigned version() const noexcept { return version_; }
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void undo();
    void redo();
    // Queue an edit. `key` de-duplicates: a later edit with the same key replaces the earlier one.
    void edit(const void* key, std::function<void(Patch&)> fn);
    void editNow(std::function<void(Patch&)> fn);
    // Applies queued edits in one publish. Returns true if anything was applied.
    bool flush();

    // Hooks installed by the editor for cross-component actions.
    std::function<void(const juce::String&)> toast;                 // status line message
    std::function<void()> openBrowser;                              // preset browser overlay (all sources)
    // Preset browser with a preset source (0 all, 1 factory, 2 user) and optional type filter ("BASS");
    // source -1 restores the previous browser session.
    std::function<void(int source, const juce::String& type)> openPresetBrowser;
    // Loads a .SerumPreset / .zygpreset on a background thread (latest request wins); notes keep sounding.
    std::function<void(const juce::File&)> loadPresetFile;
    std::function<void(const juce::String&)> busy;                  // toast with spinner
    // Generic file browser: title, root, wildcards, currently loaded file, pick callback.
    std::function<void(const juce::String&, juce::File, const juce::String&, juce::File, std::function<void(const juce::File&)>)> browse;
    std::function<void(int osc)> openWavetableBrowser;              // wavetable picker
    std::function<void(const juce::String& title, const juce::StringArray& lines)> openText;
    std::function<void(int page)> gotoPage;                         // 0 OSC 1 MIX 2 FX 3 MATRIX 4 GLOBAL
    std::function<void(int tab)> selectModTab;                      // bottom mod tab
    int uiScale = 1;                                                // 100% / 200% / 300%
    int selectedFilter = 0;                                         // OSC page filter tab
    int selectedEnv = 0, selectedLfo = 0, selectedMap = 0;          // bottom modulation section
    int selectedFxRack = 0;                                         // 0 main 1 bus1 2 bus2
    // Preset browser session: survives closing the browser so reopening restores it and the
    // top-bar prev/next arrows walk the same filtered list.
    struct PresetBrowse { bool valid = false; int source = 0, format = 0; juce::String type, search; std::vector<juce::File> list; };
    PresetBrowse presetBrowse;
private:
    struct Pending { const void* key; std::function<void(Patch&)> fn; };
    void setPatch(std::shared_ptr<const Patch> p) { patch = std::move(p); ++version_; }
    std::vector<Pending> pending_;
    std::vector<std::shared_ptr<const Patch>> undo_, redo_;
    juce::uint32 lastUndoPush_ = 0;
    unsigned version_ = 1;
};

// Implemented by the editor so pages can change the UI scale.
class UiScaleTarget { public: virtual ~UiScaleTarget() = default; virtual void setUiScale(int s) = 0; };

class Bound {
public:
    virtual ~Bound() = default;
    virtual void pull(const Patch& patch) = 0;
};

// Normalises a modulation route's destination to (kind, instance, parameter).
struct TargetId { ModTarget kind = ModTarget::unknown; int inst = 0, param = 0;
    bool operator==(const TargetId&) const = default; };
TargetId normalizedTarget(const ModulationRoute& r) noexcept;
// Normalised modulation swing (0..1 of knob travel) applied to a target by all active routes.
bool modulationSpan(const Patch& p, TargetId id, float& lowDelta, float& highDelta);

// -------------------------------------------------------------------- look
class PixelLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    PixelLookAndFeel();
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
        bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
        const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
        int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 2; }
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String&) override;
    void drawPopupMenuUpDownArrow(juce::Graphics&, int width, int height, bool isScrollUpArrow) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
        int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 6; }
    juce::Font getPopupMenuFont() override { return juce::Font(juce::FontOptions(8.0f)); }
    void drawTextEditorOutline(juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void fillTextEditorBackground(juce::Graphics&, int, int, juce::TextEditor&) override;
};

// -------------------------------------------------------------- numeric entry
// Keyboard text entry shared by knobs and spinners; drawn with the pixel font.
struct NumericEntry {
    bool active = false, fresh = false;   // fresh: the shown value is "selected"; the first key replaces it
    juce::String text;
    void begin(const juce::String& initial) { active = true; fresh = true; text = initial; }
    // Returns 1 on commit, -1 on cancel, 0 when still editing.
    int key(const juce::KeyPress& k);
};

// -------------------------------------------------------------------- knob
struct KnobSpec {
    juce::String label;
    double lo = 0.0, hi = 1.0, def = 0.0;
    bool bipolar = false;
    bool logScale = false;      // needs lo > 0
    double skew = 1.0;          // t = ((v-lo)/(hi-lo))^skew ; <1 spreads the low end
    int step = 0;               // >0: snap to integers stepping by `step`
    std::function<juce::String(double)> format;
    std::function<bool(const juce::String&, double&)> parse;
};
juce::String formatNumber(double v, int maxDecimals = 2);

// Common modulation-destination behaviour for every continuous control (knob, spinner, fader):
// accepts dragged sources, offers a right-click MOD SOURCE menu over the full source catalog,
// lists/edits the routes already hitting it, and reports the modulation span for drawing.
class ModDest : public juce::DragAndDropTarget {
public:
    ModDest(UiContext& c, juce::Component& owner) : mctx_(c), owner_(owner) {}
    void setTarget(TargetId id) { target_ = id; hasTarget_ = true; }
    bool hasTarget() const noexcept { return hasTarget_; }
    TargetId target() const noexcept { return target_; }
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override { dropHover_ = true; owner_.repaint(); }
    void itemDragExit(const SourceDetails&) override { dropHover_ = false; owner_.repaint(); }
    void itemDropped(const SourceDetails&) override;
    // Right-click menu: MOD SOURCE (add a route from any source), existing routes, RESET VALUE.
    void showModMenu(std::function<void()> reset);
protected:
    // Normalised modulation deltas (fractions of the parameter range) of all routes on this target.
    bool modSpan(const Patch& p, float& lo, float& hi) const;
    // Dotted highlight while a source is dragged (bright when hovering this control).
    void paintDropFrame(juce::Graphics& g, juce::Rectangle<int> r) const;
    bool dropHover_ = false;
    UiContext& mctx_;
    juce::Component& owner_;
    TargetId target_; bool hasTarget_ = false;
};

class Knob : public juce::Component, public Bound, public ModDest {
public:
    Knob(UiContext& c, KnobSpec spec, Getter get, Setter set, int diameter = 24);
    void pull(const Patch& p) override;
    void setDiameter(int d) { diameter_ = d; repaint(); }
    void setArcColour(juce::Colour c) { style_.arc = c; repaint(); }
    void setLabelShown(bool s) { labelShown_ = s; repaint(); }
    void setDimmed(bool d) { if (dim_ != d) { dim_ = d; repaint(); } }
    double value() const noexcept { return value_; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; hoverF_.to(1.0f); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; hoverF_.to(0.0f); repaint(); }
    bool keyPressed(const juce::KeyPress&) override;
    void focusLost(FocusChangeType) override;
private:
    Fade hoverF_ {this, 18.0f};
    double toNorm(double v) const;
    double fromNorm(double t) const;
    void commit(double v);
    juce::String valueText() const;
    UiContext& ctx_;
    KnobSpec spec_;
    Getter get_; Setter set_;
    int diameter_;
    double value_ = 0.0;
    bool dragging_ = false, hover_ = false, labelShown_ = true, dim_ = false;
    double dragStartT_ = 0.0;
    KnobStyle style_;
    NumericEntry entry_;
    float modLo_ = 0.0f, modHi_ = 0.0f;
};

// ----------------------------------------------------------------- spinner
// Compact LCD field: LABEL value, with optional up/down arrows. Drag, wheel or type.
class Spinner : public juce::Component, public Bound, public ModDest {
public:
    Spinner(UiContext& c, juce::String label, double lo, double hi, double step, Getter get, Setter set,
            std::function<juce::String(double)> format = {});
    void pull(const Patch& p) override;
    void showArrows(bool b) { arrows_ = b; repaint(); }
    void setLabelWidth(int w) { labelWidth_ = w; repaint(); }
    void setValueColour(juce::Colour c) { valueColour_ = c; }
    void setDefault(double d) { def_ = d; }
    // Caption under the field (knob-cell style) and an optional leading icon.
    void setCaption(const juce::String& c) { caption_ = c; repaint(); }
    void setIcon(Icon i) { icon_ = i; hasIcon_ = true; repaint(); }
    void setCentred(bool c) { centred_ = c; repaint(); }
    void setDimmed(bool d) { if (dim_ != d) { dim_ = d; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging_ = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusLost(FocusChangeType) override;
private:
    void commit(double v);
    UiContext& ctx_;
    juce::String label_;
    double lo_, hi_, step_, def_ = 0.0, value_ = 0.0, dragStart_ = 0.0;
    Getter get_; Setter set_;
    std::function<juce::String(double)> format_;
    bool arrows_ = false, dragging_ = false, hasIcon_ = false, centred_ = false, dim_ = false;
    int labelWidth_ = 0;
    Icon icon_ = Icon::gear;
    juce::String caption_;
    juce::Colour valueColour_ = pal::lcd;
    NumericEntry entry_;
    float modLo_ = 0.0f, modHi_ = 0.0f;   // absolute normalised span of routes on this field
};

// ------------------------------------------------------------------ toggle
class Toggle : public juce::Component, public Bound {
public:
    enum class Style { led, check, block };
    Toggle(UiContext& c, juce::String label, Style style, BoolGetter get, BoolSetter set);
    void pull(const Patch& p) override;
    void setLabel(const juce::String& s) { label_ = s; repaint(); }
    void setOnColour(juce::Colour c) { on_ = c; repaint(); }
    void setOn(bool b) { if (on_state_ != b) { on_state_ = b; onF_.to(b ? 1.0f : 0.0f); repaint(); } }
    void setIcon(Icon i) { icon_ = i; hasIcon_ = true; repaint(); }
    bool isOn() const noexcept { return on_state_; }
    void setCentred(bool c) { centred_ = c; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; hoverF_.to(1.0f); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; hoverF_.to(0.0f); repaint(); }
    std::function<void(bool)> onToggle;   // extra hook after the patch edit
private:
    UiContext& ctx_;
    juce::String label_;
    Style style_;
    BoolGetter get_; BoolSetter set_;
    Fade onF_ {this, 16.0f}, hoverF_ {this, 18.0f};
    Icon icon_ = Icon::plus; bool hasIcon_ = false;
    bool on_state_ = false, hover_ = false, centred_ = false;
    juce::Colour on_ = pal::acid;
};

// ------------------------------------------------------------------ button
class PixelButton : public juce::Component {
public:
    PixelButton() = default;
    explicit PixelButton(juce::String text) : text_(std::move(text)) {}
    void setText(const juce::String& t) { text_ = t; repaint(); }
    void setIcon(Icon i) { icon_ = i; hasIcon_ = true; repaint(); }
    void setToggled(bool t) { if (toggled_ != t) { toggled_ = t; litF_.to(t ? 1.0f : 0.0f); repaint(); } }
    bool isToggled() const noexcept { return toggled_; }
    void setAccent(juce::Colour c) { accent_ = c; repaint(); }
    void setFlat(bool f) { flat_ = f; repaint(); }
    void setBold(bool b) { bold_ = b; repaint(); }
    void setWave(WaveIcon w) { wave_ = w; hasWave_ = true; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { down_ = true; pressF_.snap(1.0f); pressF_.to(0.999f); repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; hoverF_.to(1.0f); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; down_ = false; hoverF_.to(0.0f); repaint(); }
    std::function<void()> onClick;
    std::function<void()> onRightClick;
private:
    Fade hoverF_ {this, 18.0f}, pressF_ {this, 9.0f}, litF_ {this, 14.0f};
    juce::String text_; Icon icon_ = Icon::plus; bool hasIcon_ = false;
    bool toggled_ = false, hover_ = false, down_ = false, flat_ = false, bold_ = false, hasWave_ = false;
    WaveIcon wave_ = WaveIcon::sine;
    juce::Colour accent_ = pal::acid;
};

// ---------------------------------------------------------------- chooser
// Dropdown field with optional prev/next arrows. The owner supplies the menu.
class Chooser : public juce::Component {
public:
    Chooser() = default;
    void setText(const juce::String& t) { if (text_ != t) { text_ = t; repaint(); } }
    void setPlaceholderStyle(bool b) { dimText_ = b; repaint(); }
    void setArrows(bool b) { arrows_ = b; repaint(); }
    void setTextColour(juce::Colour c) { textColour_ = c; repaint(); }
    void setBold(bool b) { bold_ = b; repaint(); }
    void setFlat(bool f) { flat_ = f; repaint(); }
    void setCentredText(bool c) { centred_ = c; repaint(); }
    void setDimmed(bool d) { dimmed_ = d; repaint(); }
    std::function<void(juce::PopupMenu&)> buildMenu;
    std::function<void(int dir)> onStep;      // -1 / +1 from the arrow buttons
    std::function<void()> customOpen;         // replaces the popup (for hierarchical menus)
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; hoverF_.to(1.0f); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; hoverF_.to(0.0f); repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    Fade hoverF_ {this, 18.0f};
    juce::String text_;
    bool arrows_ = false, hover_ = false, dimText_ = false, bold_ = false, flat_ = false, centred_ = false, dimmed_ = false;
    juce::Colour textColour_ = pal::textHi;
};

// ------------------------------------------------------------------- fader
// Vertical level fader with dB ticks. Value is normalised 0..1 (0 dB at `unity`).
class Fader : public juce::Component, public Bound, public ModDest {
public:
    Fader(UiContext& c, Getter get, Setter set, double def);
    void pull(const Patch& p) override;
    void setLevels(float l, float r) { if (l != l_ || r != r_) { l_ = l; r_ = r; repaint(); } }
    void setMetersShown(bool b) { meters_ = b; repaint(); }
    void setDimmed(bool d) { dim_ = d; repaint(); }
    void setUnity(double t) { unity_ = t; }
    void setLabelsShown(bool b) { labels_ = b; repaint(); }
    // Linear gain at the top of the travel (1 = 0 dB at the top, 2 = +6 dB); only shifts the dB scale.
    void setTop(double gain) { top_ = gain; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging_ = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void setFromY(int y);
    void commit(double v);
    juce::Rectangle<int> track() const;
    UiContext& ctx_;
    Getter get_; Setter set_;
    double value_ = 0.0, def_ = 0.0, unity_ = 0.75, top_ = 1.0;
    bool dragging_ = false, meters_ = false, dim_ = false, labels_ = true;
    float l_ = 0.0f, r_ = 0.0f;
    float modLo_ = 0.0f, modHi_ = 0.0f;   // modulation deltas in fader travel units
    bool modded_ = false;
};

// Segmented stereo peak meter, 2 columns, `levels` linear 0..1+.
void drawMeter(juce::Graphics& g, juce::Rectangle<int> r, float left, float right, bool vertical = true);

// Tab with LED, optional count badge and selected state.
class TabButton : public juce::Component {
public:
    TabButton() = default;
    void setText(const juce::String& t) { text_ = t; repaint(); }
    void setSelected(bool s) { if (selected_ != s) { selected_ = s; selF_.to(s ? 1.0f : 0.0f); repaint(); } }
    void setBadge(int n) { if (badge_ != n) { badge_ = n; repaint(); } }
    void setLed(bool on) { if (led_ != on) { led_ = on; repaint(); } }
    void setLedShown(bool s) { ledShown_ = s; }
    void setBig(bool b) { big_ = b; }
    void setAccent(juce::Colour c) { accent_ = c; repaint(); }
    void setDragHandle(bool b) { handle_ = b; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { if (onClick && !e.mods.isPopupMenu()) onClick(); else if (onRightClick) onRightClick(); }
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; hoverF_.to(1.0f); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; hoverF_.to(0.0f); repaint(); }
    std::function<void()> onClick, onRightClick;
    std::function<void()> onDragOut;   // pointer dragged away from the tab: start a modulation drag
    void mouseDrag(const juce::MouseEvent& e) override { if (onDragOut && e.getDistanceFromDragStart() > 5 && !e.mods.isPopupMenu()) onDragOut(); }
private:
    Fade hoverF_ {this, 18.0f}, selF_ {this, 13.0f};
    juce::String text_; bool selected_ = false, hover_ = false, led_ = false, ledShown_ = true, big_ = false, handle_ = false;
    int badge_ = 0;
    juce::Colour accent_ = pal::acid;
};

// Horizontal bipolar/unipolar slider used in the matrix.
class HSlider : public juce::Component, public Bound {
public:
    HSlider(UiContext& c, double lo, double hi, double def, Getter get, Setter set);
    void pull(const Patch& p) override;
    void setColours(juce::Colour pos, juce::Colour neg) { pos_ = pos; neg_ = neg; repaint(); }
    void setOverrideValue(double v) { value_ = v; repaint(); }
    void setDimmed(bool d) { dim_ = d; repaint(); }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragging_ = false; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void commit(double v);
    UiContext& ctx_;
    double lo_, hi_, def_, value_ = 0.0;
    Getter get_; Setter set_;
    bool dragging_ = false, dim_ = false;
    juce::Colour pos_ = pal::acid, neg_ = pal::crimson;
};

// Shows `text` in the LCD well; used by readouts.
void drawLcd(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
             juce::Colour colour = pal::lcd, juce::Justification j = juce::Justification::centred);

// Standard module frame: bevelled panel with optional header bar and LED.
void drawModuleFrame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill = pal::panel);
// Small section caption with hairline rules either side, e.g. "-- GLOBAL --".
void drawCaption(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text,
                 juce::Colour colour = pal::textBody);

// Lightweight `Component` that runs a paint lambda (static decoration, displays).
class Canvas : public juce::Component {
public:
    std::function<void(juce::Graphics&, juce::Rectangle<int>)> painter;
    std::function<void(const juce::MouseEvent&)> down, drag, up, dbl, move;
    std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)> wheel;
    Canvas() { setOpaque(false); }
    void paint(juce::Graphics& g) override { if (painter) painter(g, getLocalBounds()); }
    void mouseDown(const juce::MouseEvent& e) override { if (down) down(e); }
    void mouseDrag(const juce::MouseEvent& e) override { if (drag) drag(e); }
    void mouseUp(const juce::MouseEvent& e) override { if (up) up(e); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (dbl) dbl(e); }
    void mouseMove(const juce::MouseEvent& e) override { if (move) move(e); }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (wheel) wheel(e, w); }
};

}
