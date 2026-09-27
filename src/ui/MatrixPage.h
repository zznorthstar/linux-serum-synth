#pragma once
// MATRIX page: modulation routes as an editable table (source, curve, amount, polarity,
// destination, aux source, smoothing, bypass, delete).
#include "Panels.h"
#include "ModCatalog.h"

namespace zyg::ui {

class CurveButton final : public juce::Component, public Bound {
public:
    CurveButton(UiContext& c, Getter get, Setter set) : ctx_(c), get_(std::move(get)), set_(std::move(set)) {}
    void pull(const Patch& p) override { const double v = get_(p); if (v != value_) { value_ = v; repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hoverF_.to(1.0f); }
    void mouseExit(const juce::MouseEvent&) override { hoverF_.to(0.0f); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { commit(value_ + (w.deltaY > 0 ? 25 : -25)); ctx_.flush(); }
    void setDimmed(bool d) { dim_ = d; repaint(); }
private:
    void commit(double v);
    UiContext& ctx_;
    Getter get_; Setter set_;
    double value_ = 0.0;
    bool dim_ = false;
    Fade hoverF_ {this, 18.0f};
};

class MatrixRow final : public Panel {
public:
    MatrixRow(UiContext& c, int routeIndex);
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    int routeIndex() const { return ri_; }
private:
    bool exists() const { return ri_ < int(ctx.patch->modulation.size()); }
    void editRoute(std::function<void(ModulationRoute&)> fn);
    void showSourceMenu(bool aux);
    void showDestMenu();
    int ri_;
    bool active_ = false, bypass_ = false;
    Chooser *source_ = nullptr, *dest_ = nullptr, *aux_ = nullptr;
    CurveButton *curveIn_ = nullptr, *curveOut_ = nullptr;
    HSlider *amount_ = nullptr, *smooth_ = nullptr;
    Toggle *bipolar_ = nullptr, *invert_ = nullptr;
    PixelButton *bypassBtn_ = nullptr, *del_ = nullptr;
    double amountValue_ = 0.0;
    Fade activeF_ {this, 12.0f};
};

class MatrixPage final : public Panel {
public:
    explicit MatrixPage(UiContext& c);
    void refresh(const Patch& p) override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
private:
    void layoutRows();
    std::vector<std::unique_ptr<MatrixRow>> rows_;
    int count_ = -1, scroll_ = 0, total_ = 0;
    PixelButton* clear_ = nullptr;
};

}
