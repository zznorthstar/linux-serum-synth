#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/Panels.h"
#include <memory>

namespace zyg::ui { class TopBar; class ModSection; class KeyboardDock; class Toast; class ModalLayer; }

// Serum-2-layout pixel-art editor. The window is freely resizable (default 1280x720);
// widgets live on a logical canvas of window size / integer UI scale (100%, 200%, 300%)
// so pixel art always stays crisp.
class ZygEditor final : public juce::AudioProcessorEditor, public zyg::ui::UiScaleTarget, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit ZygEditor(ZygProcessor& processor);
    ~ZygEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int minLogicalWidth = 1000, minLogicalHeight = 600;
    static constexpr int defaultWidth = 1280, defaultHeight = 720;
    // Page indices: 0 OSC, 1 MIX, 2 FX, 3 MATRIX, 4 GLOBAL.
    void showPage(int page);
    int currentPage() const noexcept { return page_; }
    void setScale(int scale);
    void setUiScale(int s) override { setScale(s); }
    int scale() const noexcept { return scale_; }
    // Renders the current UI at `scale` x its logical size (for documentation / tests).
    juce::Image snapshot(int scale = 1, bool settle = true);
    void openEditor(int which);          // 0 clip, 1 arp
    void openModalByName(const juce::String& name);   // test hook: browser, about, diag, clip, arp
    void refreshNow();
    zyg::ui::ModalLayer& modal();
    zyg::ui::UiContext& context() { return ctx_; }
private:
    void dragOperationStarted(const juce::DragAndDropTarget::SourceDetails&) override;
    void dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) override;
    void timerCallback() override;
    void layoutContent();
    void openBrowserFor(int oscIndex);
    ZygProcessor& proc_;
    zyg::ui::UiContext ctx_;
    zyg::ui::PixelLookAndFeel look_;

    struct Content : juce::Component { void paint(juce::Graphics& g) override; } content_;
    zyg::ui::TopBar* top_ = nullptr;
    std::vector<std::unique_ptr<zyg::ui::Panel>> pages_;
    std::unique_ptr<zyg::ui::TopBar> topOwned_;
    std::unique_ptr<zyg::ui::ModSection> modSection_;
    std::unique_ptr<zyg::ui::KeyboardDock> dock_;
    std::unique_ptr<zyg::ui::Toast> toast_;
    std::unique_ptr<zyg::ui::ModalLayer> modal_;
    std::uint64_t lastPresetId_ = 0;
    std::string lastPresetName_;
    bool firstPatch_ = true;
    int page_ = 0, prevPage_ = 0;
    int scale_ = 1;
    // GPU compositing: JUCE paints every component through this OpenGL context when attached.
    std::unique_ptr<juce::OpenGLContext> gl_;
    bool gpu_ = true;
    void setGpu(bool on);
    // Loads presets off the message thread so browsing never stalls the UI; the newest request wins.
    struct PresetLoader;
    std::unique_ptr<PresetLoader> loader_;
    int logicalW_ = defaultWidth, logicalH_ = defaultHeight;
    unsigned seenVersion_ = 0;
    bool snapshotWritten_ = false;
    zyg::ui::Fade pageFade_ {nullptr, 14.0f};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZygEditor)
};
