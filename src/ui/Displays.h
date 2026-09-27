#pragma once
// Read-mostly visualisations: wavetable stack, sample waveform, filter response.
#include "Panels.h"
#include "../dsp/Fft.h"
#include "Retro.h"

namespace zyg::ui {

// Oscillator display. Draws whatever the oscillator's mode has to show. Wavetables render as a
// procedural 3D mesh (x = sample position, depth = frame, height = amplitude) with the current,
// warped frame drawn live on top; a drag scrubs the table position.
class OscDisplay : public AnimatedView, public Bound {
public:
    OscDisplay(UiContext& c, int oscIndex) : AnimatedView(30), ctx_(c), idx_(oscIndex) {}
    void pull(const Patch& p) override;
    void paint(juce::Graphics& g) override { paintRetro(screen_, g, getLocalBounds(), RetroStyle::greenLcd(), [this](juce::Graphics& c) { paintContent(c); }); }
    void paintContent(juce::Graphics&);
    void mouseDown(const juce::MouseEvent& e) override { dragStartPos_ = pos_; }
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { ctx_.flush(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (ctx_.openWavetableBrowser) ctx_.openWavetableBrowser(idx_); }
protected:
    void tick(double dt) override;
    bool busy() const override {
        return modulated_ || activity_ > 0.01f || shownPos_ != pos_ || shownWarp_[0] != warp_[0] || shownWarp_[1] != warp_[1] || isMouseButtonDown();
    }
private:
    void paintWavetable(juce::Graphics&, const Oscillator&, juce::Rectangle<int>, bool live);
    void paintSample(juce::Graphics&, const Oscillator&, juce::Rectangle<int>, bool live);
    void paintNoise(juce::Graphics&, const Oscillator&, juce::Rectangle<int>, bool live);
    void paintSub(juce::Graphics&, const Oscillator&, juce::Rectangle<int>, bool live);
    UiContext& ctx_;
    int idx_;
    double pos_ = 0.0, dragStartPos_ = 0.0, shownPos_ = 0.0;
    float warp_[2] {}, shownWarp_[2] {};     // live (possibly modulated) warp amounts
    bool modulated_ = false;                 // a voice is sounding: follow the engine's modulated values
    float activity_ = 0.0f;
    double phase_ = 0.0;
    // cached mesh raster; rebuilt only when its inputs change
    juce::Image mesh_;
    std::uint64_t meshKey_ = 0;
    RetroScreen screen_;
};

// Frequency response of the selected filter.
class FilterDisplay : public AnimatedView, public Bound {
public:
    explicit FilterDisplay(UiContext& c, int fixedIndex = -1, bool mini = false) : AnimatedView(30), ctx_(c), fixed_(fixedIndex), mini_(mini) {}
    int index() const { return fixed_ >= 0 ? fixed_ : ctx_.selectedFilter; }
    void pull(const Patch& p) override;
    void paint(juce::Graphics& g) override { paintRetro(screen_, g, getLocalBounds(), RetroStyle::violetCrt(), [this](juce::Graphics& c) { paintContent(c); }); }
    void paintContent(juce::Graphics&);
    void mouseDown(const juce::MouseEvent& e) override { mouseDrag(e); }
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { ctx_.flush(); }
protected:
    void tick(double dt) override;
    bool busy() const override {
        if (activity_ > 0.01f || isMouseButtonDown() || std::abs(shown_.cutoff - filter_.cutoff) > 1.0e-4
            || std::abs(shown_.resonance - filter_.resonance) > 1.0e-3) return true;
        for (float v : specDb_) if (v > -110.0f) return true;   // spectrum still falling
        return false;
    }
private:
    UiContext& ctx_;
    Filter shown_;             // eased copy that the curve is drawn from
    float activity_ = 0.0f;
    double phase_ = 0.0;
    int fixed_;
    bool mini_;
    Filter filter_;
    bool have_ = false;
    // output spectrum (FFT of the plugin output) drawn behind the response
    void updateSpectrum(double dt);
    std::vector<float> scope_, re_, im_, window_, specDb_;
    dsp::Fft fft_;
    RetroScreen screen_;
};

}
