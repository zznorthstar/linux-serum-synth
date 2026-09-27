#pragma once
// Toast messages, the modal layer and the modal panels (file browser, text viewer, about).
#include "Widgets.h"
#include <atomic>

namespace zyg::ui {

// ------------------------------------------------------------------- toast
class Toast final : public juce::Component, private juce::Timer {
public:
    Toast();
    // Shows a message; `busy` adds an animated spinner and keeps it up until the next toast.
    void show(const juce::String& text, bool busy = false, int millis = 2600);
    void paint(juce::Graphics&) override;
    int preferredWidth() const;
private:
    void timerCallback() override;
    juce::String text_;
    bool busy_ = false;
    Fade fade_ {this, 14.0f};
    Fade rise_ {this, 12.0f};
    double shownAt_ = 0.0;
    int holdMs_ = 2600;
};

// ------------------------------------------------------------------- modal
class ModalLayer final : public juce::Component {
public:
    ModalLayer();
    void open(std::unique_ptr<juce::Component> panel, int width, int height);
    // Clicks inside `r` fall through to the editor (e.g. the keyboard dock while browsing presets);
    // the panel is centred in the area above it. Reset on close.
    void setPassThrough(juce::Rectangle<int> r) { passThrough_ = r; layoutPanel(); repaint(); }
    bool hitTest(int x, int y) override { return !passThrough_.contains(x, y); }
    void close();
    bool isOpen() const noexcept { return panel_ != nullptr && !closing_; }
    void paint(juce::Graphics&) override;
    void resized() override { layoutPanel(); }
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    void layoutPanel();
    std::unique_ptr<juce::Component> panel_;
    int pw_ = 0, ph_ = 0;
    bool closing_ = false;
    juce::Rectangle<int> passThrough_;
    Fade fade_ {this, 16.0f};
};

// Base for panels shown in the modal layer: title bar with a close button.
class ModalPanel : public juce::Component {
public:
    explicit ModalPanel(juce::String title) : title_(std::move(title)) {}
    std::function<void()> requestClose;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override;
    juce::Rectangle<int> body() const { return getLocalBounds().withTrimmedTop(22).reduced(4); }
    void setBusy(bool b) { busy_ = b; repaint(); }
    void tickBusy() { if (busy_) repaint(getWidth() - 44, 0, 22, 22); }
protected:
    juce::String title_;
    bool busy_ = false;
};

class TextPanel final : public ModalPanel {
public:
    TextPanel(juce::String title, juce::StringArray lines);
    void paint(juce::Graphics&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    juce::StringArray lines_;
    float scroll_ = 0.0f;
};

class AboutPanel final : public ModalPanel, private juce::Timer {
public:
    AboutPanel();
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    juce::Image logo_;
};

// Searchable two-pane file browser (folders | files) that scans in the background.
class FileBrowserPanel final : public ModalPanel, private juce::Timer {
public:
    // `extraRoots` (label, folder) are scanned too and shown as "<LABEL>/..." folders (e.g. the user library).
    FileBrowserPanel(juce::String title, juce::File root, juce::String wildcards, juce::File current,
                     std::function<void(const juce::File&)> onPick,
                     std::vector<std::pair<juce::String, juce::File>> extraRoots = {});
    ~FileBrowserPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void parentHierarchyChanged() override { if (isShowing()) grabKeyboardFocus(); }
private:
    struct Entry { juce::File file; juce::String name, folder; };
    struct Scanner;
    void timerCallback() override;
    void applyResults(std::vector<Entry> entries);
    void rebuildFilter();
    void pick(int visibleIndex);
    juce::Rectangle<int> folderArea() const;
    juce::Rectangle<int> fileArea() const;
    juce::Rectangle<int> searchArea() const;
    int fileRowAt(juce::Point<int>) const;
    int folderRowAt(juce::Point<int>) const;
    void ensureVisible();

    juce::File root_, current_;
    juce::String wildcards_, search_;
    std::function<void(const juce::File&)> onPick_;
    std::vector<Entry> entries_;
    std::vector<int> visible_;
    std::vector<juce::String> folders_;
    std::vector<int> folderCounts_;
    int folder_ = 0, hover_ = -1, selected_ = 0;
    float fileScroll_ = 0.0f, folderScroll_ = 0.0f;
    bool scanning_ = true;
    std::unique_ptr<Scanner> scanner_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}
