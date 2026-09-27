#pragma once
// Bottom half of the OSC/MIX/FX/MATRIX/GLOBAL pages: macros, envelope, LFO and
// velocity/note mapping editors, and the voicing panel.
#include "Panels.h"
#include "Retro.h"

namespace zyg::ui {

class MacroPanel final : public Panel {
public:
    int rowH() const { return std::max(44, (getHeight() - 20) / 4); }
    explicit MacroPanel(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    std::array<Knob*, 8> knobs_ {};
    std::array<Canvas*, 8> handles_ {};   // number/badge area: drag to modulate
    std::array<int, 8> counts_ {};
    std::array<juce::String, 8> names_;
};

class EnvGraph;
class EnvPanel final : public Panel {
public:
    // Controls are laid out for a 188 px body; extra height goes to the graph.
    int extraH() const { return std::max(0, getHeight() - 188); }
    explicit EnvPanel(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    EnvGraph* graph_ = nullptr;
    std::array<Knob*, 5> knobs_ {};
    Envelope env_;
};

class LfoGraph;
class LfoPanel final : public Panel {
public:
    // Controls are laid out for a 188 px body; extra height goes to the graph.
    int extraH() const { return std::max(0, getHeight() - 188); }
    explicit LfoPanel(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    LfoGraph* graph_ = nullptr;
    Chooser *shape_ = nullptr, *direction_ = nullptr;
    std::array<PixelButton*, 3> modes_ {};
    Toggle *mono_ = nullptr, *sync_ = nullptr, *anchor_ = nullptr, *trip_ = nullptr, *dot_ = nullptr;
    Knob *rateHz_ = nullptr, *rateSync_ = nullptr;
    std::array<Knob*, 4> knobs_ {};
    LfoDefinition def_;
};

class MapPanel final : public Panel {
public:
    // Controls are laid out for a 188 px body; extra height goes to the graph.
    int extraH() const { return std::max(0, getHeight() - 188); }
    explicit MapPanel(UiContext& c);
    void refresh(const Patch& p) override;
    void frame() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    RetroScreen screen_;
    Toggle *mono_ = nullptr, *legato_ = nullptr;
    Spinner* poly_ = nullptr;
    int routes_ = 0, active_ = 0, poly_n_ = 8;
};

class ModSection final : public Panel {
public:
    explicit ModSection(UiContext& c);
    void refresh(const Patch& p) override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void select(int tab);      // 0-3 ENV, 4-13 LFO, 14 VELO, 15 NOTE
private:
    MacroPanel* macros_ = nullptr;
    EnvPanel* env_ = nullptr;
    LfoPanel* lfo_ = nullptr;
    MapPanel* map_ = nullptr;
    std::array<TabButton*, 16> tabs_ {};
};

}
