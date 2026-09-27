#pragma once
// Top bar: logo, page tabs, preset field, browser/menu/undo, master knob and meter.
#include "Panels.h"

namespace zyg::ui {

class TopBar final : public Panel {
public:
    explicit TopBar(UiContext& c);
    void refresh(const Patch& p) override;
    void frame() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setPage(int page);
    std::function<void(int)> onPage;
    std::function<void(int)> onScale;   // 1, 2, 3
    std::function<void(bool)> onGpu;    // toggle the OpenGL renderer
    std::function<void(bool)> onRetro;  // toggle LCD/CRT screen emulation
    bool retroEnabled = true;
    bool gpuEnabled = false;
    std::function<void()> onAbout;
    int currentScale = 1;
private:
    void showMenu();
    void addPresetItems(juce::PopupMenu& m);     // shared by the preset dropdown and MENU
    void addLibraryItems(juce::PopupMenu& m);
    void saveAs();
    void loadFile(const juce::File& f);
    void chooseFile(const juce::String& caption, const juce::String& wildcard, bool save, std::function<void(const juce::File&)> done);
    juce::Image logo_;
    std::array<TabButton*, 5> tabs_ {};
    PixelButton *save_ = nullptr, *prev_ = nullptr, *next_ = nullptr, *browser_ = nullptr, *menu_ = nullptr,
                *undo_ = nullptr, *redo_ = nullptr, *notes_ = nullptr;
    Chooser* name_ = nullptr;
    Canvas* logoHit_ = nullptr;                  // click the logo for ABOUT
    Knob* master_ = nullptr;
    juce::String presetName_, author_, provenance_;
    int noteCount_ = 0, missing_ = 0;
    float peakL_ = 0, peakR_ = 0, holdL_ = 0, holdR_ = 0;
    unsigned lastNotes_ = 0;
    Fade midiF_ {this, 5.0f};
    juce::File lastFile_;
    std::shared_ptr<juce::FileChooser> chooser_;
};

}
