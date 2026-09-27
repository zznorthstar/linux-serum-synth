#pragma once
// OSC page: SUB, OSC A/B/C, NOISE and the two-filter section, laid out like Serum 2.
#include "Displays.h"

namespace zyg::ui {

class OscModule final : public Panel {
public:
    OscModule(UiContext& c, int index);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void layoutSet(const std::vector<juce::Component*>& slots);
    juce::Rectangle<int> slot(int col, int row) const;
    int idx_;
    OscMode mode_ = OscMode::unknown;
    bool enabled_ = false;
    juce::String routeText_;
    Toggle* enable_ = nullptr;
    Chooser *type_ = nullptr, *route_ = nullptr, *table_ = nullptr, *detuneMode_ = nullptr, *warp1_ = nullptr, *warp2_ = nullptr;
    PixelButton *menu_ = nullptr, *browse_ = nullptr;
    Spinner *oct_ = nullptr, *sem_ = nullptr, *fin_ = nullptr, *crs_ = nullptr, *phase_ = nullptr, *rand_ = nullptr, *unison_ = nullptr;
    OscDisplay* display_ = nullptr;
    Knob *pan_ = nullptr, *level_ = nullptr, *detune_ = nullptr, *blend_ = nullptr;
    Knob *wtPos_ = nullptr, *warpKnob1_ = nullptr, *warpKnob2_ = nullptr;
    std::vector<Knob*> knobs_, wtSet_, sampleSet_, multiSet_, granSet_, specSet_;
    std::vector<juce::Component*> extras_;    // toggles used by sample modes
    Toggle *loop_ = nullptr, *reverse_ = nullptr, *oneShotToggle_ = nullptr;
};

class SubModule final : public Panel {
public:
    explicit SubModule(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    bool enabled_ = false;
    Toggle* enable_ = nullptr;
    Spinner *oct_ = nullptr, *crs_ = nullptr;
    Chooser* route_ = nullptr;
    OscDisplay* display_ = nullptr;
    std::array<PixelButton*, 6> shapes_ {};
    Knob *pan_ = nullptr, *level_ = nullptr;
    SubShape shape_ = SubShape::unknown;
};

class NoiseModule final : public Panel {
public:
    explicit NoiseModule(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    bool enabled_ = false;
    Toggle *enable_ = nullptr, *oneShot_ = nullptr;
    Chooser *type_ = nullptr, *route_ = nullptr;
    OscDisplay* display_ = nullptr;
    Knob *pan_ = nullptr, *level_ = nullptr, *colour_ = nullptr;
};

class FilterModule final : public Panel {
public:
    explicit FilterModule(UiContext& c);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void selectFilter(int f);
    Toggle* enable_[2] {};
    TabButton* tab_[2] {};
    Chooser* type_ = nullptr;
    FilterDisplay* display_ = nullptr;
    std::array<PixelButton*, 5> sources_ {};   // S A B C N
    std::vector<Knob*> knobs_;
    Fader* level_ = nullptr;
    bool enabled_[2] {};
};

class OscPage final : public Panel {
public:
    explicit OscPage(UiContext& c);
    void resized() override;
private:
    SubModule* sub_ = nullptr;
    OscModule* osc_[3] {};
    NoiseModule* noise_ = nullptr;
    FilterModule* filter_ = nullptr;
};

}
