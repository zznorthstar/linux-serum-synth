#pragma once
// GLOBAL page: tuning, portamento, voicing and quality, time scaling, the per-voice panel and interface settings.
#include "Panels.h"

namespace zyg::ui {

class GlobalPage final : public Panel {
public:
    explicit GlobalPage(UiContext& c);
    void refresh(const Patch& p) override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    struct Section { juce::String title; juce::Rectangle<int> area; };
    std::vector<Section> sections_;
    std::vector<juce::Component*> comps_;
    std::vector<int> compSection_;
    std::vector<juce::Rectangle<int>> compRect_;
    void put(juce::Component* c, int section, juce::Rectangle<int> r) { comps_.push_back(c); compSection_.push_back(section); compRect_.push_back(r); }
    Chooser *priority_ = nullptr, *oversample_ = nullptr, *content_ = nullptr;
    std::array<PixelButton*, 3> scaleBtns_ {};
    juce::String contentPath_;
    int voiceRows_ = 8;
};

}
