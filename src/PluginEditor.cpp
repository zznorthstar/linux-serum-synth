#include "PluginEditor.h"
#include "ui/ModCatalog.h"
#include "ui/TopBar.h"
#include "ui/OscPage.h"
#include "ui/MixPage.h"
#include "ui/FxPage.h"
#include "ui/MatrixPage.h"
#include "ui/GlobalPage.h"
#include "ui/ModSection.h"
#include "ui/KeyboardDock.h"
#include "ui/Overlay.h"
#include "ui/SeqEditors.h"
#include "ui/AssetBrowse.h"
#include "ui/PresetBrowser.h"
#include "ui/UserLibrary.h"
#include "ui/Retro.h"

using namespace zyg::ui;

namespace {
constexpr int kTopH = 52, kDockH = 68, kGap = 4;
// Serum-like proportions: the page (oscillators/filter) gets ~54 % of the height left after the
// top bar and keyboard dock, the modulation section the rest. Both have floors for the 600 px minimum.
constexpr double kPageShare = 0.54;
constexpr int kMinPageH = 290, kMinModH = 208;

juce::File prefsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ZYG-ZXG").getChildFile("ui.txt");
}
struct Prefs { int scale = 1, w = ZygEditor::defaultWidth, h = ZygEditor::defaultHeight; bool gpu = true, retro = true; };
Prefs loadPrefs() {
    Prefs p;
    const auto f = prefsFile();
    if (f.existsAsFile()) {
        juce::StringArray t; t.addTokens(f.loadFileAsString().trim(), " ", "");
        if (t.size() >= 3) { p.scale = juce::jlimit(1, 3, t[0].getIntValue()); p.w = juce::jlimit(1000, 4096, t[1].getIntValue()); p.h = juce::jlimit(600, 2400, t[2].getIntValue()); }
        if (t.size() >= 4) p.gpu = t[3].getIntValue() != 0;
        if (t.size() >= 5) p.retro = t[4].getIntValue() != 0;
    }
    return p;
}
}

void ZygEditor::Content::paint(juce::Graphics& g) { g.fillAll(pal::chassis); }

struct ZygEditor::PresetLoader : juce::Thread {
    PresetLoader(ZygProcessor& p, std::function<void(juce::File, bool, juce::String)> d)
        : juce::Thread("zyg-preset-load"), proc(p), done(std::move(d)) { startThread(); }
    ~PresetLoader() override { signalThreadShouldExit(); notify(); stopThread(4000); }
    void request(const juce::File& f) { { const juce::ScopedLock l(lock); pending = f; } notify(); }
    void run() override {
        while (!threadShouldExit()) {
            juce::File f;
            { const juce::ScopedLock l(lock); f = pending; pending = juce::File(); }
            if (f == juce::File()) { wait(-1); continue; }
            bool ok = false;
            for (int attempt = 0; attempt < 25 && !threadShouldExit(); ++attempt) {   // a slot frees once the audio thread takes the last patch
                ok = f.hasFileExtension("zygpreset") ? proc.loadNativePreset(f) : proc.loadPreset(f);
                if (ok || !proc.getStatus().startsWith("Could not hand off")) break;
                { const juce::ScopedLock l(lock); if (pending != juce::File()) break; }   // a newer request supersedes this one
                wait(20);
            }
            if (!threadShouldExit()) done(f, ok, proc.getStatus());
        }
    }
    ZygProcessor& proc;
    std::function<void(juce::File, bool, juce::String)> done;
    juce::CriticalSection lock;
    juce::File pending;
};

ZygEditor::ZygEditor(ZygProcessor& p) : AudioProcessorEditor(&p), proc_(p), ctx_(p) {
    setLookAndFeel(&look_);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(content_);
    content_.setLookAndFeel(&look_);

    topOwned_ = std::make_unique<TopBar>(ctx_);
    top_ = topOwned_.get();
    content_.addAndMakeVisible(*top_);
    top_->onPage = [this](int pg) { showPage(pg); };
    top_->onScale = [this](int s) { setScale(s); };

    pages_.push_back(std::make_unique<OscPage>(ctx_));
    pages_.push_back(std::make_unique<MixPage>(ctx_));
    pages_.push_back(std::make_unique<FxPage>(ctx_));
    pages_.push_back(std::make_unique<MatrixPage>(ctx_));
    pages_.push_back(std::make_unique<GlobalPage>(ctx_));
    for (auto& pg : pages_) content_.addAndMakeVisible(*pg);

    modSection_ = std::make_unique<ModSection>(ctx_);
    content_.addAndMakeVisible(*modSection_);
    dock_ = std::make_unique<KeyboardDock>(ctx_);
    content_.addAndMakeVisible(*dock_);
    toast_ = std::make_unique<Toast>();
    content_.addAndMakeVisible(*toast_);
    modal_ = std::make_unique<ModalLayer>();
    content_.addAndMakeVisible(*modal_);
    modal_->setVisible(false);   // addAndMakeVisible would leave the full-size overlay swallowing every click
    toast_->setVisible(false);
    toast_->setInterceptsMouseClicks(false, false);

    ctx_.gotoPage = [this](int pg) { showPage(pg); };
    ctx_.toast = [this](const juce::String& s) { toast_->show(s); };
    ctx_.busy = [this](const juce::String& s) { toast_->show(s, true); };
    ctx_.openText = [this](const juce::String& title, const juce::StringArray& lines) {
        modal_->open(std::make_unique<TextPanel>(title, lines), 640, 380);
    };
    ctx_.browse = [this](const juce::String& title, juce::File root, const juce::String& wildcards, juce::File current,
                         std::function<void(const juce::File&)> pick) {
        modal_->open(std::make_unique<FileBrowserPanel>(title, std::move(root), wildcards, std::move(current), std::move(pick)), 720, 420);
    };
    loader_ = std::make_unique<PresetLoader>(proc_, [safe = juce::Component::SafePointer<ZygEditor>(this)](juce::File f, bool ok, juce::String status) {
        juce::MessageManager::callAsync([safe, f, ok, status] {
            if (safe) safe->ctx_.toast(ok ? "LOADED " + f.getFileNameWithoutExtension().toUpperCase() : status);
        });
    });
    ctx_.loadPresetFile = [this](const juce::File& f) { loader_->request(f); };
    ctx_.openPresetBrowser = [this](int source, const juce::String& type) {
        PresetBrowserPanel::Options o;
        o.restore = source < 0;
        o.source = PresetBrowserPanel::Source(juce::jlimit(0, 2, source)); o.type = type;
        modal_->open(std::make_unique<PresetBrowserPanel>(ctx_, o), 920, 560);
        modal_->setPassThrough(dock_->getBounds());   // the on-screen keyboard stays playable while browsing
    };
    ctx_.openBrowser = [this] { ctx_.openPresetBrowser(-1, {}); };
    ctx_.openWavetableBrowser = [this](int osc) { openBrowserFor(osc); };
    dock_->onEditor = [this](int which) { openEditor(which); };
    top_->onAbout = [this] { modal_->open(std::make_unique<AboutPanel>(), 420, 300); };
    top_->onRetro = [this](bool on) {
        zyg::ui::retro::setEnabled(on); top_->retroEnabled = on; content_.repaint();
        ctx_.toast(on ? "RETRO SCREENS ON" : "RETRO SCREENS OFF");
    };
    top_->onGpu = [this](bool on) { setGpu(on); ctx_.toast(on ? "GPU RENDERING ON" : "GPU RENDERING OFF (SOFTWARE)"); };

    library::ensureUserFolders();   // ~/Documents/ZYG-ZXG with "PUT YOUR ... HERE" readmes, like Serum's folder
    const auto prefs = loadPrefs();
    scale_ = prefs.scale;
    ctx_.uiScale = scale_;
    top_->currentScale = scale_;
    setResizable(true, true);
    setResizeLimits(minLogicalWidth * scale_, minLogicalHeight * scale_, 4096, 2400);
    setSize(defaultWidth * scale_, defaultHeight * scale_);   // always open at 1280x720 logical; only the scale is remembered
    showPage(0);
    refreshNow();
    startTimerHz(60);
    // ZYGZXG_NO_GPU=1 forces the software renderer (e.g. for a host/driver that misbehaves with GL).
    setGpu(prefs.gpu && juce::SystemStats::getEnvironmentVariable("ZYGZXG_NO_GPU", {}).isEmpty());
    const bool retroOn = prefs.retro && juce::SystemStats::getEnvironmentVariable("ZYGZXG_NO_RETRO", {}).isEmpty();
    zyg::ui::retro::setEnabled(retroOn); top_->retroEnabled = retroOn;
}

void ZygEditor::setGpu(bool on) {
    gpu_ = on;
    top_->gpuEnabled = on;
    if (on && !gl_) {
        gl_ = std::make_unique<juce::OpenGLContext>();
        gl_->setComponentPaintingEnabled(true);
        gl_->setContinuousRepainting(false);   // components still repaint on demand (displays tick at 30-60 Hz)
        gl_->attachTo(*this);
    } else if (!on && gl_) {
        gl_->detach();
        gl_.reset();
    }
    repaint();
}

ZygEditor::~ZygEditor() {
    stopTimer();
    loader_.reset();
    if (gl_) { gl_->detach(); gl_.reset(); }
    const auto f = prefsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(juce::String(scale_) + " " + juce::String(getWidth()) + " " + juce::String(getHeight()) + " " + juce::String(gpu_ ? 1 : 0) + " " + juce::String(top_->retroEnabled ? 1 : 0));
    proc_.setAuditionHeld(false);
    modal_.reset();
    content_.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

zyg::ui::ModalLayer& ZygEditor::modal() { return *modal_; }

void ZygEditor::paint(juce::Graphics& g) { g.fillAll(pal::voidBg); }

void ZygEditor::resized() {
    logicalW_ = std::max(minLogicalWidth, getWidth() / scale_);
    logicalH_ = std::max(minLogicalHeight, getHeight() / scale_);
    content_.setTransform(juce::AffineTransform::scale(float(scale_)));
    content_.setBounds(0, 0, logicalW_, logicalH_);
    layoutContent();
}

void ZygEditor::layoutContent() {
    const int W = logicalW_, H = logicalH_;
    top_->setBounds(0, 0, W, kTopH);
    dock_->setBounds(0, H - kDockH, W, kDockH);
    const int avail = H - kTopH - 2 - kGap - kDockH;
    int pageH = std::max(kMinPageH, int(std::round(avail * kPageShare)));
    const int modH = std::max(kMinModH, avail - pageH);
    pageH = avail - modH;
    const int modY = H - kDockH - kGap - modH;
    modSection_->setBounds(0, modY, W, modH);
    const int pageY = kTopH + 2;
    for (auto& pg : pages_) pg->setBounds(0, pageY, W, modY - 2 - pageY);
    modal_->setBounds(0, 0, W, H);
    if (toast_->isVisible()) toast_->setTopLeftPosition((W - toast_->getWidth()) / 2, H - kDockH - 34);
}

void ZygEditor::setScale(int s) {
    s = juce::jlimit(1, 3, s);
    if (s == scale_) return;
    const int lw = logicalW_, lh = logicalH_;
    scale_ = s;
    ctx_.uiScale = s;
    top_->currentScale = s;
    setResizeLimits(minLogicalWidth * s, minLogicalHeight * s, 4096, 2400);
    setSize(lw * s, lh * s);
    ctx_.toast("UI SCALE " + juce::String(s * 100) + "%");
}

bool ZygEditor::keyPressed(const juce::KeyPress& k) {
    const auto ctrl = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier;
    if (k.getModifiers().testFlags(ctrl) && k.getKeyCode() == 'Z') { if (k.getModifiers().isShiftDown()) ctx_.redo(); else ctx_.undo(); return true; }
    if (k.getModifiers().testFlags(ctrl) && k.getKeyCode() == 'Y') { ctx_.redo(); return true; }
    if (!modal_->isOpen() && !k.getModifiers().isAnyModifierKeyDown() && k.getKeyCode() >= '1' && k.getKeyCode() <= '5') {
        showPage(k.getKeyCode() - '1'); return true;
    }
    return false;
}

void ZygEditor::openBrowserFor(int osc) {
    const auto& o = ctx_.patch->oscillators[std::size_t(osc)];
    const auto root = contentRoot(*ctx_.patch);
    const bool wt = o.mode == zyg::OscMode::wavetable || o.mode == zyg::OscMode::unknown;
    const auto dir = root.getChildFile(wt ? "Tables" : "Samples");
    library::ensureUserFolders();
    const auto userDir = wt ? library::wavetablesDir() : library::samplesDir();
    modal_->open(std::make_unique<FileBrowserPanel>(wt ? "WAVETABLE BROWSER - OSC " + oscName(osc) : "SAMPLE BROWSER - OSC " + oscName(osc), dir,
        "*.wav;*.flac;*.aif;*.aiff", oscAssetFile(*ctx_.patch, osc),
        [this, osc](const juce::File& f) {
            ctx_.busy("LOADING " + f.getFileNameWithoutExtension().toUpperCase() + "...");
            juce::Timer::callAfterDelay(60, [this, osc, f, safe = juce::Component::SafePointer<ZygEditor>(this)] {
                if (!safe) return;
                const bool ok = proc_.setOscSampleFile(osc, f);
                ctx_.toast(ok ? "LOADED " + f.getFileNameWithoutExtension().toUpperCase() : proc_.getStatus());
            });
        }, std::vector<std::pair<juce::String, juce::File>>{{"USER", userDir}}), 720, 420);
}

void ZygEditor::showPage(int page) {
    page = juce::jlimit(0, int(pages_.size()) - 1, page);
    const bool changed = page != page_;
    prevPage_ = page_;
    page_ = page;
    for (int i = 0; i < int(pages_.size()); ++i) pages_[std::size_t(i)]->setVisible(i == page_);
    top_->setPage(page);
    if (ctx_.patch) pages_[std::size_t(page_)]->refresh(*ctx_.patch);
    if (changed) pages_[std::size_t(page_)]->enter();   // page entrance animation
}

void ZygEditor::refreshNow() {
    ctx_.refreshPatch();
    seenVersion_ = ctx_.version();
    if (!ctx_.patch) return;
    top_->refresh(*ctx_.patch);
    pages_[std::size_t(page_)]->refresh(*ctx_.patch);
    modSection_->refresh(*ctx_.patch);
    dock_->refresh(*ctx_.patch);
}

void ZygEditor::openEditor(int which) { modal_->open(std::make_unique<SeqPanel>(ctx_, which == 1), 900, 480); }

void ZygEditor::openModalByName(const juce::String& name) {
    if (name == "browser") ctx_.openBrowser();
    else if (name == "about") modal_->open(std::make_unique<AboutPanel>(), 420, 300);
    else if (name == "diag") top_->refresh(*ctx_.patch), ctx_.openText("IMPORT DIAGNOSTICS", [&] { juce::StringArray l; for (const auto& d : ctx_.patch->diagnostics) l.add(juce::String(d.status).toUpperCase() + "  " + juce::String(d.path) + "  " + juce::String(d.detail)); return l; }());
    else if (name == "clip") openEditor(0);
    else if (name == "arp") openEditor(1);
    else if (name == "wavetable") openBrowserFor(0);
    else if (name == "toast") ctx_.busy("LOADING BASS SYNTH PRESS...");
}

juce::Image ZygEditor::snapshot(int scale, bool settle) {
    refreshNow();
    if (settle) {
        for (auto& pg : pages_) pg->settle();
        Animator::get().finishAll();
    }
    return content_.createComponentSnapshot(content_.getLocalBounds(), true, float(scale));
}

void ZygEditor::dragOperationStarted(const juce::DragAndDropTarget::SourceDetails&) {
    zyg::ui::setModDragActive(true); content_.repaint();
}
void ZygEditor::dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) {
    zyg::ui::setModDragActive(false); content_.repaint();
}

void ZygEditor::timerCallback() {
    if (const auto msg = proc_.takeResampleMessage(); msg.isNotEmpty()) ctx_.toast(msg);
    ctx_.flush();
    ctx_.refreshPatch();
    if (ctx_.version() != seenVersion_) {
        seenVersion_ = ctx_.version();
        if (ctx_.patch->presetId != lastPresetId_ || ctx_.patch->name != lastPresetName_) {   // a different patch: re-animate the page
            lastPresetId_ = ctx_.patch->presetId; lastPresetName_ = ctx_.patch->name;
            if (!firstPatch_) { pages_[std::size_t(page_)]->enter(0.25f); modSection_->enter(0.35f); }
            firstPatch_ = false;
        }
        top_->refresh(*ctx_.patch);
        pages_[std::size_t(page_)]->refresh(*ctx_.patch);
        modSection_->refresh(*ctx_.patch);
        dock_->refresh(*ctx_.patch);
    }
    top_->frame();
    pages_[std::size_t(page_)]->frame();
    modSection_->frame();
    dock_->frame();

    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("ZYG_UI_SNAPSHOT", {});
    if (!snapshotWritten_ && snapshotPath.isNotEmpty() && isShowing()) {
        snapshotWritten_ = true;
        auto image = createComponentSnapshot(getLocalBounds(), true, 1.0f);
        if (auto stream = juce::File(snapshotPath).createOutputStream())
            juce::PNGImageFormat().writeImageToStream(image, *stream);
    }
}
