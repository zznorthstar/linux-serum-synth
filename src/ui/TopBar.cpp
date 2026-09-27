#include "TopBar.h"
#include "AssetBrowse.h"
#include "UserLibrary.h"
#include "BinaryData.h"

namespace zyg::ui {

TopBar::TopBar(UiContext& c) : Panel(c) {
    logo_ = juce::ImageFileFormat::loadFrom(BinaryData::logo_46_png, BinaryData::logo_46_pngSize);
    static const char* names[5] = {"OSC", "MIX", "FX", "MATRIX", "GLOBAL"};
    for (int i = 0; i < 5; ++i) {
        auto& t = make<TabButton>();
        t.setText(names[i]); t.setBig(true);
        t.setAccent(i == 2 ? pal::amber : i == 3 ? pal::violetHot : pal::acid);
        t.onClick = [this, i] { setPage(i); if (onPage) onPage(i); };
        tabs_[std::size_t(i)] = &t;
    }
    tabs_[0]->setSelected(true);

    save_ = &make<PixelButton>(); save_->setIcon(Icon::disk); save_->setFlat(true);
    save_->onClick = [this] { saveAs(); };
    name_ = &make<Chooser>();
    name_->setBold(true); name_->setTextColour(pal::lcd);
    name_->buildMenu = [this](juce::PopupMenu& m) { addPresetItems(m); m.addSeparator(); addLibraryItems(m); };
    logoHit_ = &make<Canvas>();
    logoHit_->setMouseCursor(juce::MouseCursor::PointingHandCursor);
    logoHit_->down = [this](const juce::MouseEvent&) { if (onAbout) onAbout(); };
    auto step = [this](int dir) {
        // continue through the preset browser's list (same source/type/search) when the current
        // preset came from it; otherwise fall back to the neighbouring file on disk
        const juce::File current(juce::String(ctx.patch->sourcePath));
        const auto& list = ctx.presetBrowse.list;
        const int n = int(list.size());
        for (int i = 0; i < n; ++i)
            if (list[std::size_t(i)] == current) { loadFile(list[std::size_t(((i + dir) % n + n) % n)]); return; }
        const auto f = presetNeighbour(lastFile_.existsAsFile() ? lastFile_ : current, dir);
        if (f.existsAsFile()) loadFile(f);
    };
    prev_ = &make<PixelButton>(); prev_->setIcon(Icon::arrowLeft); prev_->onClick = [step] { step(-1); };
    next_ = &make<PixelButton>(); next_->setIcon(Icon::arrowRight); next_->onClick = [step] { step(1); };
    browser_ = &make<PixelButton>(); browser_->setIcon(Icon::browser);
    browser_->onClick = [this] { if (ctx.openBrowser) ctx.openBrowser(); };
    menu_ = &make<PixelButton>("MENU"); menu_->setBold(true);
    menu_->onClick = [this] { showMenu(); };
    undo_ = &make<PixelButton>(); undo_->setIcon(Icon::undo); undo_->onClick = [this] { ctx.undo(); };
    redo_ = &make<PixelButton>(); redo_->setIcon(Icon::redo); redo_->onClick = [this] { ctx.redo(); };
    notes_ = &make<PixelButton>(); notes_->setFlat(true);
    notes_->onClick = [this] {
        juce::StringArray lines;
        for (const auto& d : ctx.patch->diagnostics)
            lines.add(juce::String(d.status).toUpperCase() + "  " + juce::String(d.path) + "  " + juce::String(d.detail));
        if (lines.isEmpty()) lines.add("NO DIAGNOSTICS FOR THIS PATCH");
        if (ctx.openText) ctx.openText("IMPORT DIAGNOSTICS", lines);
    };
    master_ = &make<Knob>(ctx, KnobSpec{"MAIN", 0, 1, 0.7, false, false, 1.0, 0, [](double v) { return fmt::percent01(v); }},
        [](const Patch& p) { return p.masterVolume; }, [](Patch& p, double v) { p.masterVolume = v; }, 28);
    master_->setTarget({ModTarget::globalParam, 0, int(GlobalParam::masterVolume)});
}

void TopBar::setPage(int page) {
    for (int i = 0; i < 5; ++i) tabs_[std::size_t(i)]->setSelected(i == page);
}

void TopBar::chooseFile(const juce::String& caption, const juce::String& wildcard, bool save, std::function<void(const juce::File&)> done) {
    chooser_ = std::make_shared<juce::FileChooser>(caption, lastFile_.exists() ? lastFile_.getParentDirectory() : juce::File{}, wildcard);
    const int flags = save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                           : (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles);
    chooser_->launchAsync(flags, [this, done](const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File{}) done(fc.getResult());
    });
}

void TopBar::loadFile(const juce::File& f) {
    lastFile_ = f;
    if (ctx.loadPresetFile) ctx.loadPresetFile(f);
    else if (f.hasFileExtension("zygpreset")) ctx.proc.loadNativePreset(f); else ctx.proc.loadPreset(f);
}

void TopBar::addPresetItems(juce::PopupMenu& m) {
    auto open = [this](int source, juce::String type) { return [this, source, type] { if (ctx.openPresetBrowser) ctx.openPresetBrowser(source, type); }; };
    m.addItem("BROWSE ALL PRESETS...", open(0, {}));
    m.addItem("USER PRESETS...", open(2, {}));
    m.addItem("FACTORY PRESETS...", open(1, {}));
    juce::PopupMenu byType;
    for (const auto& t : library::presetTypes()) byType.addItem(t, open(0, t));
    m.addSubMenu("BROWSE BY TYPE", byType);
    m.addSeparator();
    m.addItem("INIT PATCH", [this] { ctx.proc.newBlankPatch(); });
    m.addItem("LOAD PRESET FILE...", [this] {
        chooseFile("Load preset (Serum 2 or ZYG)", library::presetWildcards(), false, [this](const juce::File& f) { loadFile(f); });
    });
    m.addItem("SAVE PRESET...", [this] { saveAs(); });
}

void TopBar::addLibraryItems(juce::PopupMenu& m) {
    juce::PopupMenu lib;
    lib.addItem("OPEN USER FOLDER", [] { library::ensureUserFolders(); library::userRoot().startAsProcess(); });
    lib.addItem("CHANGE LOCATION...", [this] {
        chooser_ = std::make_shared<juce::FileChooser>("Choose the ZYG-ZXG user library folder", library::userRoot());
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                if (!fc.getResult().isDirectory()) return;
                library::setUserRoot(fc.getResult());
                if (ctx.toast) ctx.toast("USER LIBRARY: " + library::userRoot().getFullPathName().toUpperCase());
            });
    });
    lib.addItem("RESET TO DOCUMENTS/ZYG-ZXG", library::userRoot() != library::defaultUserRoot(), false, [this] {
        library::setUserRoot(library::defaultUserRoot());
        if (ctx.toast) ctx.toast("USER LIBRARY: " + library::userRoot().getFullPathName().toUpperCase());
    });
    lib.addSeparator();
    lib.addItem(library::userRoot().getFullPathName().toUpperCase(), false, false, [] {});
    m.addSubMenu("USER LIBRARY FOLDER", lib);
}

void TopBar::saveAs() {
    library::ensureUserFolders();
    auto name = juce::File::createLegalFileName(ctx.patch->name.empty() ? juce::String("New patch") : juce::String(ctx.patch->name));
    const auto start = library::presetsDir().getChildFile(name + ".zygpreset");
    chooser_ = std::make_shared<juce::FileChooser>("Save preset", start, "*.zygpreset");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            if (!f.hasFileExtension(".zygpreset")) f = f.withFileExtension(".zygpreset");
            const bool ok = ctx.proc.saveNativePreset(f);
            lastFile_ = f;
            if (ctx.toast) ctx.toast(ok ? "SAVED " + f.getFileNameWithoutExtension().toUpperCase() : ctx.proc.getStatus());
        });
}

void TopBar::showMenu() {
    juce::PopupMenu m;
    addPresetItems(m);
    m.addSeparator();
    addLibraryItems(m);
    m.addItem("SET CONTENT FOLDER...", [this] {
        chooser_ = std::make_shared<juce::FileChooser>("Select your legally owned Serum content folder");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) { if (fc.getResult().isDirectory()) ctx.proc.setAssetRoot(fc.getResult()); });
    });
    m.addItem("IMPORT DIAGNOSTICS", [this] { notes_->onClick(); });
    m.addItem("COPY DIAGNOSTICS", [this] { juce::SystemClipboard::copyTextToClipboard(ctx.proc.getDiagnosticsReport()); });
    m.addSeparator();
    juce::PopupMenu scale;
    for (int s : {1, 2, 3}) scale.addItem(juce::String(s) + "X  (" + juce::String(1280 * s) + " X " + juce::String(720 * s) + ")", true, currentScale == s,
                                            [this, s] { if (onScale) onScale(s); });
    m.addSubMenu("UI SCALE", scale);
    m.addItem("GPU RENDERING (OPENGL)", true, gpuEnabled, [this] { if (onGpu) onGpu(!gpuEnabled); });
    m.addItem("RETRO SCREENS (LCD / CRT)", true, retroEnabled, [this] { if (onRetro) onRetro(!retroEnabled); });
    juce::PopupMenu rs;
    if (ctx.proc.getResampleState() != ZygProcessor::ResampleState::idle)
        rs.addItem("CANCEL RESAMPLE", [this] { ctx.proc.cancelResample(); });
    else
        for (int o = 0; o < 3; ++o) {
            juce::PopupMenu len;
            for (auto [label, beats] : {std::pair<const char*, double>{"1 BEAT", 1.0}, {"1 BAR", 4.0}, {"2 BARS", 8.0}, {"4 BARS", 16.0}})
                len.addItem(label, [this, o, beats = beats] {
                    if (ctx.proc.armResample(o, beats) && ctx.toast) ctx.toast("RESAMPLE ARMED - PLAY A NOTE");
                });
            rs.addSubMenu(juce::String("INTO OSC ") + char('A' + o), len);
        }
    m.addSubMenu("RESAMPLE OUTPUT", rs);
    m.addItem("ALL NOTES OFF", [this] { ctx.proc.uiAllNotesOff(); });
    m.addItem("ABOUT ZYG-ZXG", [this] { if (onAbout) onAbout(); });
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menu_).withStandardItemHeight(13));
}

void TopBar::refresh(const Patch& p) {
    Panel::refresh(p);
    presetName_ = juce::String(p.name.empty() ? "- INIT -" : p.name);
    author_ = juce::String(p.author);
    provenance_ = p.originalPreset.empty() ? "ZYG NATIVE" : "SERUM IMPORT " + juce::String(p.serumVersion);
    noteCount_ = 0; missing_ = 0;
    for (const auto& d : p.diagnostics) {
        missing_ += d.status == "missing_asset";
        noteCount_ += d.status == "not_rendered" || d.status == "unknown" || d.status == "unmapped_parameter" || d.status == "not_rendered_parameter" || d.status == "unsupported_asset";
    }
    name_->setText(presetName_ + (ctx.canUndo() ? " *" : ""));
    notes_->setText(missing_ ? juce::String(missing_) + " MISSING" : noteCount_ ? juce::String(noteCount_) + " NOTES" : "CLEAN");
    notes_->setAccent(missing_ ? pal::crimson : noteCount_ ? pal::amber : pal::acid);
    notes_->setToggled(missing_ > 0 || noteCount_ > 0);
    undo_->setToggled(false);
    repaint();
}

void TopBar::frame() {
    const unsigned n = ctx.proc.getMidiNoteCount();
    if (n != lastNotes_) { lastNotes_ = n; midiF_.snap(1.0f); midiF_.to(0.0f); }
    const float l = ctx.proc.getOutputPeakLeft(), r = ctx.proc.getOutputPeakRight();
    holdL_ = std::max(l, holdL_ * 0.86f); holdR_ = std::max(r, holdR_ * 0.86f);
    if (std::abs(holdL_ - peakL_) > 0.002f || std::abs(holdR_ - peakR_) > 0.002f) { peakL_ = holdL_; peakR_ = holdR_; repaint(getWidth() - 40, 0, 40, getHeight()); }
    Panel::frame();
}

void TopBar::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    g.setColour(pal::chassis); g.fillRect(r);
    hLine(g, 0, r.getBottom() - 1, r.getWidth(), pal::edgeMid);
    // logo well
    wellBox(g, {0, 0, 140, r.getHeight()}, pal::voidBg);
    if (logo_.isValid()) blit(g, logo_, (140 - logo_.getWidth()) / 2, (r.getHeight() - logo_.getHeight()) / 2);
    // preset block
    const juce::Rectangle<int> block(431, 0, r.getWidth() - 170 - 431, r.getHeight());
    g.setColour(pal::voidBg); g.fillRect(block);
    hLine(g, block.getX(), 27, block.getWidth(), pal::edgeMid);
    drawText(g, "ARTIST:", block.getX() + 6, 34, pal::textMuted);
    drawText(g, author_.isEmpty() ? "-" : author_, block.getX() + 6 + 33, 34, pal::textBody);
    const int descX = block.getX() + block.getWidth() * 45 / 100;
    vLine(g, descX - 6, 30, 16, pal::edgeMid);
    drawText(g, "SOURCE:", descX, 34, pal::textMuted);
    const bool imported = provenance_.startsWith("SERUM");
    drawText(g, provenance_, descX + 33, 34, imported ? pal::violetHot : pal::acid);
    // MIDI activity lamp under the meter
    {
        const juce::Rectangle<int> lamp(getWidth() - 27, 48, 13, 2);
        g.setColour(mix(pal::edgeMid, pal::acid, midiF_.v())); g.fillRect(lamp);
    }
    // master meter
    drawMeter(g, {getWidth() - 25, 6, 9, 40}, peakL_, peakR_);
    // separators
    vLine(g, r.getWidth() - 170, 0, r.getHeight(), pal::edgeMid);
}

void TopBar::resized() {
    const int w = getWidth();
    const int right = w - 170;
    for (int i = 0; i < 5; ++i) tabs_[std::size_t(i)]->setBounds(141 + i * 58, 0, 57, getHeight() - 1);
    logoHit_->setBounds(0, 0, 140, getHeight());
    save_->setBounds(434, 4, 18, 20);
    name_->setBounds(454, 4, right - 454 - 96, 20);
    prev_->setBounds(right - 92, 4, 18, 20); next_->setBounds(right - 72, 4, 18, 20);
    notes_->setBounds(right - 50, 4, 48, 20);
    browser_->setBounds(right + 4, 3, 38, 22); menu_->setBounds(right + 44, 3, 38, 22);
    undo_->setBounds(right + 4, 27, 38, 22); redo_->setBounds(right + 44, 27, 38, 22);
    master_->setBounds(right + 92, 2, 44, 48);
}

}
