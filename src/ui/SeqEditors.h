#pragma once
// CLIP and ARP editors (modal): a piano-roll style note/step editor with per-clip settings.
#include "Overlay.h"
#include "Panels.h"

namespace zyg::ui {

struct RollNote { double time = 0.0, length = 0.25; int note = 60; double vel = 0.8; };

class RollEditor final : public juce::Component {
public:
    explicit RollEditor(bool arp) : arp_(arp) { setWantsKeyboardFocus(false); }
    void setData(std::vector<RollNote> notes, double lengthBeats);
    const std::vector<RollNote>& notes() const { return notes_; }
    bool isEditing() const { return drag_ >= 0; }
    std::function<void(const std::vector<RollNote>&)> onChange;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { drag_ = -1; velDrag_ = -1; repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    juce::Rectangle<int> grid() const;
    juce::Rectangle<int> velLane() const;
    juce::Rectangle<int> noteRect(const RollNote&) const;
    int rowCount() const { return arp_ ? 8 : rows_; }
    int lowNote() const { return arp_ ? 0 : low_; }
    int noteAtY(int y) const;
    double timeAtX(int x, bool snap) const;
    int hit(juce::Point<int>) const;
    void changed() { if (onChange) onChange(notes_); repaint(); }
    bool arp_;
    std::vector<RollNote> notes_;
    double length_ = 4.0, lastLen_ = 0.25;
    int low_ = 48, rows_ = 24;
    int drag_ = -1, velDrag_ = -1;
    bool resize_ = false;
    double grabDt_ = 0.0;
    int grabNote_ = 0;
};

class SeqPanel final : public ModalPanel, private juce::Timer {
public:
    SeqPanel(UiContext& c, bool arp);
    ~SeqPanel() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
private:
    void timerCallback() override;
    UiContext& ctx_;
    std::unique_ptr<Panel> content_;
    unsigned seen_ = 0;
};

}
