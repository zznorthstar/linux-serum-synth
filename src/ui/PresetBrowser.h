#pragma once
// Preset browser: factory content + the user's library (Serum and ZYG presets alike), with search,
// source / type / format filters. It stays open while you click or arrow through presets so each
// one can be auditioned while notes keep playing; Enter or double-click keeps a preset and closes.
#include "Overlay.h"
#include <atomic>

namespace zyg::ui {

class PresetBrowserPanel final : public ModalPanel, private juce::Timer {
public:
    enum class Source { all, factory, user };
    struct Options { Source source = Source::all; juce::String type; bool restore = false; };
    PresetBrowserPanel(UiContext& c, Options opts);
    ~PresetBrowserPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; hoverChip_ = -1; repaint(); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void parentHierarchyChanged() override { if (isShowing()) grabKeyboardFocus(); }

    struct Entry { juce::File file; juce::String name, folder, type; bool user = false, native = false; };
    // Scans `roots` (label, directory, isUser) for presets; exposed for tests.
    static std::vector<Entry> scan(const std::vector<std::tuple<juce::String, juce::File, bool>>& roots, const std::atomic<bool>* cancel = nullptr);
private:
    struct Scanner;
    struct Chip { juce::Rectangle<int> r; int group, value; juce::String label; };
    void timerCallback() override;
    void applyResults(std::vector<Entry> entries);
    void rebuildFilter();
    void audition(int visibleIndex);
    void keepAndClose(int visibleIndex);
    void layoutChips();
    juce::Rectangle<int> searchArea() const;
    juce::Rectangle<int> typeArea() const;
    juce::Rectangle<int> listArea() const;
    juce::Rectangle<int> footerArea() const;
    int rowAt(juce::Point<int>) const;
    int typeRowAt(juce::Point<int>) const;
    void ensureVisible();

    UiContext& ctx_;
    Options opts_;
    int format_ = 0;                      // 0 all, 1 Serum, 2 ZYG
    juce::String search_;
    std::vector<Entry> entries_;
    std::vector<int> visible_;
    juce::StringArray types_;             // "ALL" + types present
    std::vector<int> typeCounts_;
    int type_ = 0, hover_ = -1, hoverChip_ = -1, selected_ = 0;
    float scroll_ = 0.0f;
    bool scanning_ = true;
    juce::File loaded_;
    std::vector<Chip> chips_;
    juce::Rectangle<int> folderBtn_, doneBtn_;
    std::unique_ptr<Scanner> scanner_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    static constexpr int kRowH = 14;
};

}
