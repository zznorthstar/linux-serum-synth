#pragma once
// Bottom dock: wheels, CLIP/ARP switches, transpose/key/scale/swing, piano and portamento controls.
#include "Panels.h"

namespace zyg::ui {

class PianoKeys final : public juce::Component {
public:
    explicit PianoKeys(UiContext& c) : ctx_(c) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override;
    void refreshHeld();
    void setKeyWidth(int w) { if (w != keyW_) { keyW_ = w; repaint(); } }
    static constexpr int lowNote = 36, highNote = 96;   // C1..C6 (Serum naming: C3 = MIDI 60)
private:
    int noteAt(juce::Point<int> p, float* velocity = nullptr) const;
    void keyRect(int note, juce::Rectangle<int>& r, bool& black) const;
    UiContext& ctx_;
    int down_ = -1, keyW_ = 15;
    std::uint64_t held_[2] {};
    std::array<float, 128> glow_ {};
};

class Wheel final : public juce::Component {
public:
    Wheel(std::function<void(float)> onValue, bool springs, bool bipolar) : onValue_(std::move(onValue)), springs_(springs), bipolar_(bipolar) {
        value_ = bipolar ? 0.5f : 0.0f;
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    void setValue(float v) { if (v != value_) { value_ = v; repaint(); } }
    float value() const { return value_; }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { setFromY(e.y); }
    void mouseDrag(const juce::MouseEvent& e) override { setFromY(e.y); }
    void mouseUp(const juce::MouseEvent&) override { if (springs_) { value_ = 0.5f; onValue_(0.0f); repaint(); } }
    void mouseDoubleClick(const juce::MouseEvent&) override { value_ = bipolar_ ? 0.5f : 0.0f; onValue_(bipolar_ ? 0.0f : 0.0f); repaint(); }
private:
    void setFromY(int y) {
        const float t = 1.0f - juce::jlimit(0.0f, 1.0f, float(y - 3) / float(std::max(1, getHeight() - 7)));
        value_ = t; onValue_(bipolar_ ? t * 2.0f - 1.0f : t); repaint();
    }
    std::function<void(float)> onValue_;
    bool springs_, bipolar_;
    float value_ = 0.0f;
};

class KeyboardDock final : public Panel {
public:
    explicit KeyboardDock(UiContext& c);
    void refresh(const Patch& p) override;
    void frame() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void(int)> onEditor;   // 0 = clip, 1 = arp
private:
    PianoKeys* keys_ = nullptr;
    Wheel *bend_ = nullptr, *mod_ = nullptr;
    Spinner *bendUp_ = nullptr, *bendDown_ = nullptr, *transpose_ = nullptr, *swing_ = nullptr;
    Chooser *key_ = nullptr, *scale_ = nullptr;
    PixelButton *clip_ = nullptr, *arp_ = nullptr, *clipOn_ = nullptr, *arpOn_ = nullptr, *panic_ = nullptr;
    Toggle *always_ = nullptr, *scaled_ = nullptr;
    Knob* porta_ = nullptr;
    Canvas *curve_ = nullptr, *clipPreview_ = nullptr, *arpPreview_ = nullptr;
    std::array<Canvas*, 3> perf_ {};     // drag handles: MOD WHEEL, PITCH BEND, PRESSURE
    double curveValue_ = 50.0, curveDragStart_ = 50.0;
    bool clipEnabled_ = false, arpEnabled_ = false;
};

}
