#pragma once
// FX page: rack tabs + module list (add / remove / bypass / reorder, splitter bands) and the
// scrolling rack of effect panels with animated displays.
#include "FxViz.h"
#include "FxTree.h"

namespace zyg::ui {

class FxModulePanel final : public Panel {
public:
    FxModulePanel(UiContext& c, int fxIndex, int depth);
    int heightForWidth(int w) const;
    void setSelected(bool s) { if (selected_ != s) { selected_ = s; selFade_.to(s ? 1.0f : 0.0f); repaint(); } }
    int fxIndex() const { return idx_; }
    void refresh(const Patch& p) override;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::function<void(int)> onSelect;
    void mouseDown(const juce::MouseEvent&) override { if (onSelect) onSelect(idx_); }
private:
    int idx_, depth_;
    FxType type_ = FxType::unknown;
    bool enabled_ = true, selected_ = false;
    Fade selFade_ {this, 14.0f};
    FxViz* viz_ = nullptr;
    Toggle* enable_ = nullptr;
    Knob *wet_ = nullptr, *out_ = nullptr;
    std::vector<Knob*> knobs_;
    std::vector<juce::Component*> strip_;
    std::vector<int> stripWidths_;
    struct EnumCh { Chooser* c; int slot; int first; std::vector<juce::String> names; };
    std::vector<EnumCh> enumChoosers_;
    struct Extra { Chooser* c; std::function<juce::String(const FxModule&)> text; };
    std::vector<Extra> extras_;
};

class FxList;
class RackView;

class FxPage final : public Panel {
public:
    explicit FxPage(UiContext& c);
    void refresh(const Patch& p) override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void showAddMenu(int splitter, int band);
    std::array<TabButton*, 3> tabs_ {};
    PixelButton *add_ = nullptr, *clear_ = nullptr;
    FxList* list_ = nullptr;
    RackView* rack_ = nullptr;
    int selectedFx_ = -1;
    friend class FxList;
};

}
