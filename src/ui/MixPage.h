#pragma once
// MIX page: routing strips for every source, both filters, the FX buses, MAIN and DIRECT.
#include "Displays.h"

namespace zyg::ui {

class MixStrip final : public Panel {
public:
    enum class Kind { osc, filter, bus, main, direct };
    MixStrip(UiContext& c, Kind kind, int index, juce::String name);
    void refresh(const Patch& p) override;
    void frame() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    int routeIndex() const { return kind_ == Kind::osc ? index_ : 5 + index_; }
    Kind kind_;
    int index_;
    juce::String name_;
    bool enabled_ = true;
    juce::StringArray fxNames_;
    Toggle* enable_ = nullptr;
    Chooser* dest_ = nullptr;
    Knob *balance_ = nullptr, *send1_ = nullptr, *send2_ = nullptr, *pan_ = nullptr;
    FilterDisplay* miniFilter_ = nullptr;
    Fader* fader_ = nullptr;
    float pulse_ = 0.0f;
};

class MixPage final : public Panel {
public:
    explicit MixPage(UiContext& c);
    void resized() override;
private:
    std::vector<MixStrip*> strips_;
};

}
