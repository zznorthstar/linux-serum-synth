#pragma once
// Tiny animation layer: Fade values that ease toward a target on a shared 60 Hz
// timer. The timer only runs while at least one Fade is moving.
#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace zyg::ui {

class Fade;

class Animator final : private juce::Timer {
public:
    static Animator& get() { static Animator a; return a; }
    void add(Fade* f) {
        if (std::find(fades_.begin(), fades_.end(), f) == fades_.end()) fades_.push_back(f);
        if (!isTimerRunning()) { last_ = juce::Time::getMillisecondCounterHiRes(); startTimerHz(60); }
    }
    void finishAll();
    void remove(Fade* f) { fades_.erase(std::remove(fades_.begin(), fades_.end(), f), fades_.end()); }
    // Seconds since start, for continuous animations (shimmer, sweeps).
    static double seconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
private:
    void timerCallback() override;
    std::vector<Fade*> fades_;
    double last_ = 0.0;
};

class Fade {
public:
    explicit Fade(juce::Component* owner = nullptr, float speed = 16.0f, float initial = 0.0f)
        : owner_(owner), speed_(speed), v_(initial), target_(initial) {}
    ~Fade() { Animator::get().remove(this); }
    Fade(const Fade&) = delete;
    Fade& operator=(const Fade&) = delete;
    void to(float t) {
        if (t == target_) return;
        target_ = t;
        Animator::get().add(this);
    }
    void snap(float t) { v_ = target_ = t; }
    float v() const noexcept { return v_; }
    float target() const noexcept { return target_; }
    void setSpeed(float s) { speed_ = s; }
    std::function<void(float)> onChange;
    void finish() { v_ = target_; if (owner_) owner_->repaint(); if (onChange) onChange(v_); }
    // Returns true while still moving.
    bool step(double dt) {
        const float k = 1.0f - std::exp(-speed_ * float(dt));
        v_ += (target_ - v_) * k;
        if (std::abs(target_ - v_) < 0.004f) v_ = target_;
        if (owner_) owner_->repaint();
        if (onChange) onChange(v_);
        return v_ != target_;
    }
private:
    juce::Component* owner_;
    float speed_, v_, target_;
};

inline void Animator::finishAll() {
    auto copy = fades_;
    for (auto* f : copy) f->finish();
    fades_.clear();
    stopTimer();
}

inline void Animator::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = std::min(0.05, (now - last_) * 0.001);
    last_ = now;
    auto copy = fades_;
    for (auto* f : copy) {
        if (std::find(fades_.begin(), fades_.end(), f) == fades_.end()) continue;
        if (!f->step(dt)) remove(f);
    }
    if (fades_.empty()) stopTimer();
}

// Component that repaints itself ~30 times a second while it is actually on screen.
class AnimatedView : public juce::Component, protected juce::Timer {
public:
    explicit AnimatedView(int hz = 30) : hz_(hz) {}
protected:
    void visibilityChanged() override { update(); }
    void parentHierarchyChanged() override { update(); }
    void timerCallback() override {
        const double now = Animator::seconds();
        const double dt = std::min(0.1, now - last_);
        last_ = now;
        tick(dt);
        repaint();   // steady frame rate; static retro screens are cheap (RetroScreen reuses its last image)
    }
    virtual void tick(double) {}
    // True while something on the display moves (notes, modulation, easing, dragging). Informational.
    virtual bool busy() const { return true; }
    static double time() { return Animator::seconds(); }
private:
    void update() {
        if (isShowing() && !isTimerRunning()) { last_ = Animator::seconds(); startTimerHz(hz_); }
        else if (!isShowing() && isTimerRunning()) stopTimer();
    }
    int hz_;
    double last_ = 0.0;
};

}
