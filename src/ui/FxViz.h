#pragma once
// Animated per-effect displays: they draw the effect's actual parameters (decay tail, delay taps,
// transfer curve, response curve...) and react to played notes and output level.
#include "Panels.h"
#include "../FxParams.h"
#include "Retro.h"

namespace zyg::ui {

juce::Colour fxColour(FxType t);
// Procedural pixel-art mark identifying an effect type (used instead of the name in the rack).
void drawFxGlyph(juce::Graphics& g, FxType t, juce::Rectangle<int> r, juce::Colour ink, juce::Colour dim);

class FxViz final : public AnimatedView, public Bound {
public:
    FxViz(UiContext& c, int fxIndex);
    void pull(const Patch& p) override;
    void paint(juce::Graphics& g) override {
        paintRetro(screen_, g, getLocalBounds(), RetroStyle::crt(valid_ ? fxColour(m_.fxType) : pal::textMuted), [this](juce::Graphics& c) { paintContent(c); });
    }
    void paintContent(juce::Graphics&);
    void setIndex(int i) { index_ = i; }
protected:
    void tick(double dt) override;
    bool busy() const override { return level_ > 1.0e-3f || kick_ > 0.0f || isMouseButtonDown(); }
private:
    RetroScreen screen_;
    double v(int slot) const { return m_.p[std::size_t(slot)]; }
    void paintReverb(juce::Graphics&, juce::Rectangle<int>);
    void paintDelay(juce::Graphics&, juce::Rectangle<int>);
    void paintModulation(juce::Graphics&, juce::Rectangle<int>);
    void paintDistortion(juce::Graphics&, juce::Rectangle<int>);
    void paintComp(juce::Graphics&, juce::Rectangle<int>);
    void paintEq(juce::Graphics&, juce::Rectangle<int>);
    void paintFilter(juce::Graphics&, juce::Rectangle<int>);
    void paintUtils(juce::Graphics&, juce::Rectangle<int>);
    void paintSplit(juce::Graphics&, juce::Rectangle<int>);
    void paintBode(juce::Graphics&, juce::Rectangle<int>);
    void paintPump(juce::Graphics&, juce::Rectangle<int>);
    void paintStutter(juce::Graphics&, juce::Rectangle<int>);
    UiContext& ctx_;
    int index_;
    FxModule m_;
    bool valid_ = false;
    float level_ = 0.0f, kick_ = 0.0f;
    double age_ = 0.0, phase_ = 0.0;
    unsigned lastNotes_ = 0;
};

}
