#pragma once
// Base class for editor sections plus formatting helpers shared by all pages.
#include "Widgets.h"
#include "ParamAccess.h"
#include "../dsp/Filters.h"

namespace zyg::ui {

// -------------------------------------------------------------- formatters
namespace fmt {
inline juce::String percent01(double v) { return formatNumber(v * 100.0, 1) + "%"; }
inline juce::String percent(double v) { return formatNumber(v, 1) + "%"; }
inline juce::String plain(double v) { return formatNumber(v, 2); }
inline juce::String semis(double v) { return (v > 0 ? "+" : "") + formatNumber(v, 2) + " ST"; }
inline juce::String cents(double v) { return (v > 0 ? "+" : "") + formatNumber(v, 1) + " CT"; }
inline juce::String degrees(double v) { return formatNumber(v, 0) + juce::String::charToString(juce::juce_wchar(0xb0)); }
inline juce::String hz(double v) { return v >= 1000.0 ? formatNumber(v / 1000.0, 2) + " KHZ" : formatNumber(v, v < 10 ? 2 : 1) + " HZ"; }
inline juce::String seconds(double v) {
    if (v < 0.0005) return "0 MS";
    return v < 1.0 ? formatNumber(v * 1000.0, v < 0.01 ? 1 : 0) + " MS" : formatNumber(v, 2) + " S";
}
inline juce::String db(double linear) {
    return linear <= 1.0e-4 ? "-INF DB" : formatNumber(20.0 * std::log10(linear), 1) + " DB";
}
inline juce::String pan(double v) { return std::abs(v) < 0.005 ? "C" : formatNumber(std::abs(v) * 100.0, 0) + (v < 0 ? " L" : " R"); }
inline juce::String signedPct(double v) { return (v > 0 ? "+" : "") + formatNumber(v, 1) + "%"; }
inline juce::String cutoffHz(double norm) { return hz(20.0 * std::pow(1000.0, std::clamp(norm, 0.0, 1.0))); }
}

// ------------------------------------------------------------------- Panel
class Panel : public juce::Component {
public:
    explicit Panel(UiContext& c) : ctx(c) {
        entry_.onChange = [this](float v) {
            setAlpha(v);
            setTransform(v >= 1.0f ? juce::AffineTransform() : juce::AffineTransform::translation(0.0f, (1.0f - v) * 10.0f));
        };
    }
    // Page entrance animation (fade + slide) and its instant completion for snapshots.
    void enter(float from = 0.0f) { entry_.snap(from); setAlpha(from); entry_.to(1.0f); }
    void settle() { entry_.snap(1.0f); setAlpha(1.0f); setTransform(juce::AffineTransform()); }
    // Pull every bound widget from the patch. Called when the patch changes.
    virtual void refresh(const Patch& p) {
        for (auto* b : bound_) b->pull(p);
        for (auto* c : panels_) c->refresh(p);
    }
    // ~30 Hz animation tick (meters, cursors).
    virtual void frame() { for (auto* c : panels_) c->frame(); }
    UiContext& ctx;

    template <class T, class... A> T& make(A&&... args) {
        auto p = std::make_unique<T>(std::forward<A>(args)...);
        T& ref = *p;
        addAndMakeVisible(ref);
        if (auto* b = dynamic_cast<Bound*>(&ref)) bound_.push_back(b);
        if (auto* pn = dynamic_cast<Panel*>(&ref)) panels_.push_back(pn);
        owned_.push_back(std::move(p));
        return ref;
    }

    Knob& knob(const juce::String& label, double lo, double hi, double def, Getter g, Setter s, int diameter = 24,
               std::function<juce::String(double)> format = fmt::plain, bool bipolar = false, int step = 0) {
        KnobSpec spec; spec.label = label; spec.lo = lo; spec.hi = hi; spec.def = def; spec.bipolar = bipolar;
        spec.step = step; spec.format = std::move(format);
        return make<Knob>(ctx, std::move(spec), std::move(g), std::move(s), diameter);
    }
    // Knob bound to an oscillator parameter using the engine's range table.
    Knob& oscKnob(int osc, OscParam p, const juce::String& label, int diameter = 24,
                  std::function<juce::String(double)> format = fmt::plain, bool bipolar = false) {
        const auto rg = oscParamRange(p);
        auto& k = knob(label, rg.lo, rg.hi, getOsc(Oscillator{}, p),
            [osc, p](const Patch& pt) { return getOsc(pt.oscillators[std::size_t(osc)], p); },
            [osc, p](Patch& pt, double v) { setOsc(pt.oscillators[std::size_t(osc)], p, v); },
            diameter, std::move(format), bipolar || (rg.lo < 0.0 && rg.hi > 0.0));
        k.setTarget({ModTarget::oscParam, osc, int(p)});
        return k;
    }
    Knob& filterKnob(int f, FilterParam p, const juce::String& label, int diameter = 24,
                     std::function<juce::String(double)> format = fmt::plain, bool bipolar = false) {
        const auto rg = filterParamRange(p);
        auto& k = knob(label, rg.lo, rg.hi, getFilter(Filter{}, p),
            [f, p](const Patch& pt) { return getFilter(pt.filters[std::size_t(f)], p); },
            [f, p](Patch& pt, double v) { setFilter(pt.filters[std::size_t(f)], p, v); },
            diameter, std::move(format), bipolar);
        k.setTarget({ModTarget::filterParam, f, int(p)});
        return k;
    }

    static juce::Rectangle<int> cell(int x, int y, int w, int h) { return {x, y, w, h}; }
    void clearChildren() {
        removeAllChildren();
        bound_.clear(); panels_.clear(); owned_.clear();
    }

protected:
    Fade entry_ {nullptr, 13.0f, 1.0f};
    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<Bound*> bound_;
    std::vector<Panel*> panels_;
};

// ----------------------------------------------------------- filter helpers
struct FilterChoice { FilterResponse response; int variant; const char* name; const char* serumId; };
// Menu of selectable filter types (groups separated by empty names).
const std::vector<FilterChoice>& filterChoices();
juce::String filterDisplayName(const Filter& f);
// Magnitude of the filter's transfer function in dB at `hz` (display approximation).
double filterMagnitudeDb(const Filter& f, double hz);

// Signal routing of an oscillator source (route index 0-2 = OSC A-C, 3 = NOISE, 4 = SUB).
juce::String routeLabel(const Route& r);
void buildRouteMenu(UiContext& ctx, juce::PopupMenu& menu, int routeIndex);
// Warp mode naming and the selectable menu for oscillator `osc` warp slot `slot` (0/1).
juce::String warpName(const WarpDefinition& w);
void buildWarpMenu(UiContext& ctx, juce::PopupMenu& menu, int osc, int slot);
juce::String oscName(int oscIndex);   // A, B, C, NOISE, SUB

// Number of active matrix routes fed by a source (ENV n / LFO n / macro n / velocity / note).
int countRoutes(const Patch& p, ModSource kind, int index);

// Wavetable frame count of an oscillator.
inline int frameCount(const Oscillator& o) {
    return o.frameSize > 0 ? int(o.audio.size() / o.frameSize) : 0;
}
juce::String prettyAssetName(const std::string& asset);

}
