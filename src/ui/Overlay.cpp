#include "Overlay.h"
#include "BinaryData.h"
#include <cmath>

namespace zyg::ui {

// ================================================================== Toast
Toast::Toast() {
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

int Toast::preferredWidth() const { return textWidth(text_) + (busy_ ? 34 : 26) + 8; }

void Toast::show(const juce::String& text, bool busy, int millis) {
    text_ = text; busy_ = busy;
    setSize(preferredWidth(), 22);
    if (auto* p = getParentComponent()) setTopLeftPosition((p->getWidth() - getWidth()) / 2, p->getHeight() - 68 - 34);
    holdMs_ = busy ? 20000 : millis;
    shownAt_ = Animator::seconds();
    setVisible(true);
    rise_.snap(0.0f); rise_.to(1.0f);
    fade_.to(1.0f);
    startTimerHz(30);
    repaint();
}

void Toast::timerCallback() {
    if ((Animator::seconds() - shownAt_) * 1000.0 > holdMs_) fade_.to(0.0f);
    if (fade_.v() <= 0.0f && fade_.target() <= 0.0f) { stopTimer(); setVisible(false); return; }
    repaint();
}

void Toast::paint(juce::Graphics& g) {
    const float a = fade_.v();
    if (a <= 0.01f) return;
    const int dy = int(std::round((1.0f - rise_.v()) * 8.0f));
    auto r = getLocalBounds().translated(0, dy).withTrimmedBottom(dy > 0 ? 0 : 0);
    g.setOpacity(a);
    g.beginTransparencyLayer(a);
    g.setColour(pal::edgeDark); g.fillRect(r.translated(2, 2));
    bevelBox(g, r, pal::panelHi, pal::violet, pal::edgeDark);
    g.setColour(pal::violetHot); g.fillRect(r.getX() + 1, r.getY() + 1, 2, r.getHeight() - 2);
    int x = r.getX() + 8;
    if (busy_) { drawSpinner(g, x + 5, r.getCentreY(), 4, pal::acid, Animator::seconds() * 1.4); x += 16; }
    drawTextIn(g, text_, {x, r.getY(), r.getRight() - x - 6, r.getHeight()}, pal::textHi);
    g.endTransparencyLayer();
}

// ============================================================ ModalLayer
ModalLayer::ModalLayer() {
    setVisible(false);
    setWantsKeyboardFocus(true);
    fade_.onChange = [this](float v) {
        layoutPanel();
        if (closing_ && v <= 0.0f) {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<ModalLayer>(this)] {
                if (!safe) return;
                safe->panel_.reset(); safe->closing_ = false; safe->passThrough_ = {}; safe->setVisible(false);
            });
        }
    };
}

void ModalLayer::open(std::unique_ptr<juce::Component> panel, int w, int h) {
    panel_ = std::move(panel);
    pw_ = w; ph_ = h; closing_ = false;
    if (auto* mp = dynamic_cast<ModalPanel*>(panel_.get())) mp->requestClose = [this] { close(); };
    addAndMakeVisible(*panel_);
    setVisible(true);
    toFront(false);
    fade_.snap(0.0f);
    layoutPanel();
    fade_.to(1.0f);
    panel_->grabKeyboardFocus();
}

void ModalLayer::close() { if (!panel_ || closing_) return; closing_ = true; fade_.to(0.0f); }

void ModalLayer::layoutPanel() {
    if (!panel_) return;
    const int areaH = passThrough_.isEmpty() ? getHeight() : passThrough_.getY();
    const int w = std::min(pw_, getWidth() - 24), h = std::min(ph_, areaH - 24);
    const int dy = int(std::round((1.0f - fade_.v()) * 18.0f));
    panel_->setBounds((getWidth() - w) / 2, (areaH - h) / 2 + dy, w, h);
    panel_->setAlpha(std::min(1.0f, fade_.v() * 1.4f));
}

void ModalLayer::paint(juce::Graphics& g) {
    const float a = fade_.v();
    g.setColour(pal::voidBg.withAlpha(0.72f * a));
    g.fillRect(getLocalBounds().withBottom(passThrough_.isEmpty() ? getHeight() : passThrough_.getY()));
}

void ModalLayer::mouseDown(const juce::MouseEvent& e) {
    if (panel_ && !panel_->getBounds().contains(e.getPosition())) close();
}

bool ModalLayer::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return true;   // swallow keys while a modal is open
}

// ============================================================= ModalPanel
void ModalPanel::paint(juce::Graphics& g) {
    const auto r = getLocalBounds();
    bevelBox(g, r, pal::panel, pal::violet, pal::edgeDark);
    frame(g, r.reduced(1), pal::edgeMid);
    g.setColour(pal::raised); g.fillRect(2, 2, r.getWidth() - 4, 19);
    hLine(g, 2, 21, r.getWidth() - 4, pal::edgeDark);
    drawTextIn(g, title_, {10, 2, r.getWidth() - 60, 19}, pal::violetHot, juce::Justification::centredLeft, 1, true);
    if (busy_) drawSpinner(g, r.getWidth() - 34, 11, 4, pal::acid, Animator::seconds() * 1.4);
    drawIconCentred(g, Icon::close, {r.getWidth() - 20, 2, 18, 19}, pal::textBody);
}

void ModalPanel::mouseDown(const juce::MouseEvent& e) {
    if (e.x >= getWidth() - 22 && e.y < 22 && requestClose) requestClose();
}

// =============================================================== TextPanel
TextPanel::TextPanel(juce::String title, juce::StringArray lines) : ModalPanel(std::move(title)), lines_(std::move(lines)) {}

void TextPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    const float maxScroll = std::max(0.0f, float(lines_.size()) * 11.0f - float(body().getHeight()));
    scroll_ = juce::jlimit(0.0f, maxScroll, scroll_ - w.deltaY * 120.0f);
    repaint();
}

void TextPanel::paint(juce::Graphics& g) {
    ModalPanel::paint(g);
    const auto b = body();
    wellBox(g, b, pal::sunken);
    g.saveState();
    g.reduceClipRegion(b.reduced(1));
    int y = b.getY() + 4 - int(scroll_);
    for (const auto& line : lines_) {
        if (y > b.getBottom()) break;
        if (y > b.getY() - 10) {
            const auto first = line.upToFirstOccurrenceOf(" ", false, false);
            const auto col = first == "MISSING_ASSET" ? pal::crimson : first == "DSP_ACTIVE" ? pal::acid
                : first.startsWith("NOT_RENDERED") || first.startsWith("UNMAPPED") || first.startsWith("UNSUPPORTED") ? pal::amber : pal::textBody;
            drawText(g, fitText(line, b.getWidth() - 14), b.getX() + 6, y, col);
        }
        y += 11;
    }
    g.restoreState();
    const float total = float(lines_.size()) * 11.0f;
    if (total > float(b.getHeight())) {
        const int th = std::max(10, int(float(b.getHeight()) * float(b.getHeight()) / total));
        const int ty = b.getY() + int(scroll_ / total * float(b.getHeight()));
        g.setColour(pal::edgeLight); g.fillRect(b.getRight() - 4, ty, 3, th);
    }
}

// ============================================================== AboutPanel
AboutPanel::AboutPanel() : ModalPanel("ABOUT") {
    logo_ = juce::ImageFileFormat::loadFrom(BinaryData::logo_46_png, BinaryData::logo_46_pngSize);
    startTimerHz(24);
}

void AboutPanel::paint(juce::Graphics& g) {
    ModalPanel::paint(g);
    const auto b = body();
    const double t = Animator::seconds();
    if (logo_.isValid()) {
        const int bob = int(std::round(std::sin(t * 2.0) * 2.0));
        const int w = logo_.getWidth() * 3, h = logo_.getHeight() * 3;
        const int x = b.getCentreX() - w / 2, y = b.getY() + 10 + bob;
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(logo_, x, y, w, h, 0, 0, logo_.getWidth(), logo_.getHeight(), false);
        for (int i = 0; i < 9; ++i) {   // twinkling sparkles
            const double ph = std::fmod(t * 0.9 + double(i) * 0.37, 1.0);
            const float a = float(std::sin(ph * 3.14159));
            const int sx = x + (i * 53 + 17) % w, sy = y + (i * 37 + 9) % h;
            g.setColour(pal::acidHot.withAlpha(a));
            g.fillRect(sx, sy, 1, 3); g.fillRect(sx - 1, sy + 1, 3, 1);
        }
    }
    const int ty = b.getY() + 10 + 46 * 3 + 12;
    drawTextIn(g, "ZYG-ZXG", {b.getX(), ty, b.getWidth(), 12}, pal::textHi, juce::Justification::centred, 2, true);
    drawTextIn(g, "LINUX-NATIVE WAVETABLE SYNTH - OPENS SERUM 2 PRESETS", {b.getX(), ty + 20, b.getWidth(), 8}, pal::acid, juce::Justification::centred);
    drawTextIn(g, "VERSION 0.1.0 BETA  -  VST3 + CLAP  -  GPL-3.0", {b.getX(), ty + 32, b.getWidth(), 8}, pal::textBody, juce::Justification::centred);
    drawTextIn(g, "SERUM IS A TRADEMARK OF XFER RECORDS. NO XFER CODE OR ASSETS ARE INCLUDED.", {b.getX(), ty + 48, b.getWidth(), 8}, pal::textMuted, juce::Justification::centred);
}

// ========================================================= FileBrowserPanel
struct FileBrowserPanel::Scanner : juce::Thread {
    Scanner(juce::File r, juce::String w, std::function<void(std::vector<Entry>)> done, std::vector<std::pair<juce::String, juce::File>> extra)
        : juce::Thread("zyg-scan"), root(std::move(r)), wildcards(std::move(w)), done_(std::move(done)), extras(std::move(extra)) {}
    void run() override {
        std::vector<Entry> out;
        std::vector<std::pair<juce::String, juce::File>> all {{juce::String(), root}};
        for (const auto& e : extras) all.push_back(e);
        for (const auto& [label, dir] : all) {
            if (!dir.isDirectory()) continue;
            for (const auto& f : dir.findChildFiles(juce::File::findFiles, true, wildcards)) {
                if (threadShouldExit()) return;
                Entry e; e.file = f;
                e.name = f.getFileNameWithoutExtension().replaceCharacters("_", " ");
                e.folder = f.getParentDirectory().getRelativePathFrom(dir).replaceCharacter('\\', '/');
                if (e.folder == ".") e.folder = "";
                if (label.isNotEmpty()) e.folder = label + (e.folder.isNotEmpty() ? "/" + e.folder : juce::String());
                out.push_back(std::move(e));
            }
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            const int c = a.folder.compareNatural(b.folder);
            return c != 0 ? c < 0 : a.name.compareNatural(b.name) < 0;
        });
        if (!threadShouldExit()) done_(std::move(out));
    }
    juce::File root; juce::String wildcards;
    std::function<void(std::vector<Entry>)> done_;
    std::vector<std::pair<juce::String, juce::File>> extras;
};

FileBrowserPanel::FileBrowserPanel(juce::String title, juce::File root, juce::String wildcards, juce::File current,
                                   std::function<void(const juce::File&)> onPick,
                                   std::vector<std::pair<juce::String, juce::File>> extraRoots)
    : ModalPanel(std::move(title)), root_(std::move(root)), current_(std::move(current)), wildcards_(std::move(wildcards)),
      onPick_(std::move(onPick)) {
    setWantsKeyboardFocus(true);
    setBusy(true);
    startTimerHz(24);
    auto alive = alive_;
    juce::Component::SafePointer<FileBrowserPanel> safe(this);
    scanner_ = std::make_unique<Scanner>(root_, wildcards_, [alive, safe](std::vector<Entry> entries) {
        auto shared = std::make_shared<std::vector<Entry>>(std::move(entries));
        juce::MessageManager::callAsync([alive, safe, shared] {
            if (alive->load() && safe) safe->applyResults(std::move(*shared));
        });
    }, std::move(extraRoots));
    scanner_->startThread();
}

FileBrowserPanel::~FileBrowserPanel() {
    alive_->store(false);
    if (scanner_) scanner_->stopThread(3000);
}

void FileBrowserPanel::timerCallback() {
    if (scanning_) repaint(); else stopTimer();
}

void FileBrowserPanel::applyResults(std::vector<Entry> entries) {
    entries_ = std::move(entries);
    std::vector<juce::String> names;
    for (const auto& e : entries_) if (std::find(names.begin(), names.end(), e.folder) == names.end()) names.push_back(e.folder);
    std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) { return a.compareNatural(b) < 0; });
    folders_.assign(1, "ALL");
    folderCounts_.assign(1, int(entries_.size()));
    for (const auto& n : names) {
        folders_.push_back(n.isEmpty() ? "/" : n);
        folderCounts_.push_back(int(std::count_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.folder == n; })));
    }
    folder_ = 0;
    if (current_.existsAsFile()) {
        const auto rel = current_.getParentDirectory().getRelativePathFrom(root_).replaceCharacter('\\', '/');
        for (std::size_t i = 1; i < folders_.size(); ++i) if (folders_[i] == rel) folder_ = int(i);
    }
    scanning_ = false; setBusy(false);
    rebuildFilter();
    if (current_.existsAsFile())
        for (std::size_t i = 0; i < visible_.size(); ++i)
            if (entries_[std::size_t(visible_[i])].file == current_) { selected_ = int(i); ensureVisible(); }
    repaint();
}

void FileBrowserPanel::rebuildFilter() {
    visible_.clear();
    juce::StringArray tokens; tokens.addTokens(search_.toLowerCase(), " ", "");
    tokens.removeEmptyStrings();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (folder_ > 0) {
            const auto want = folders_[std::size_t(folder_)] == "/" ? juce::String() : folders_[std::size_t(folder_)];
            if (e.folder != want) continue;
        }
        const auto hay = (e.folder + " " + e.name).toLowerCase();
        bool ok = true;
        for (const auto& t : tokens) if (!hay.contains(t)) { ok = false; break; }
        if (ok) visible_.push_back(int(i));
    }
    selected_ = juce::jlimit(0, std::max(0, int(visible_.size()) - 1), selected_);
    fileScroll_ = 0.0f;
}

juce::Rectangle<int> FileBrowserPanel::searchArea() const { return body().withHeight(16); }
juce::Rectangle<int> FileBrowserPanel::folderArea() const {
    auto b = body().withTrimmedTop(20).withTrimmedBottom(14);
    return b.removeFromLeft(std::clamp(b.getWidth() / 4, 130, 220));
}
juce::Rectangle<int> FileBrowserPanel::fileArea() const {
    auto b = body().withTrimmedTop(20).withTrimmedBottom(14);
    b.removeFromLeft(std::clamp(b.getWidth() / 4, 130, 220) + 4);
    return b;
}
int FileBrowserPanel::fileRowAt(juce::Point<int> p) const {
    const auto a = fileArea();
    if (!a.contains(p)) return -1;
    const int i = int((float(p.y - a.getY() - 2) + fileScroll_) / 12.0f);
    return i >= 0 && i < int(visible_.size()) ? i : -1;
}
int FileBrowserPanel::folderRowAt(juce::Point<int> p) const {
    const auto a = folderArea();
    if (!a.contains(p)) return -1;
    const int i = int((float(p.y - a.getY() - 2) + folderScroll_) / 12.0f);
    return i >= 0 && i < int(folders_.size()) ? i : -1;
}

void FileBrowserPanel::ensureVisible() {
    const auto a = fileArea();
    const float top = float(selected_) * 12.0f, bottom = top + 12.0f;
    if (top < fileScroll_) fileScroll_ = top;
    else if (bottom > fileScroll_ + float(a.getHeight() - 4)) fileScroll_ = bottom - float(a.getHeight() - 4);
}

void FileBrowserPanel::pick(int i) {
    if (i < 0 || i >= int(visible_.size())) return;
    const auto f = entries_[std::size_t(visible_[std::size_t(i)])].file;
    if (requestClose) requestClose();
    if (onPick_) onPick_(f);
}

void FileBrowserPanel::mouseDown(const juce::MouseEvent& e) {
    ModalPanel::mouseDown(e);
    if (const int f = folderRowAt(e.getPosition()); f >= 0) { folder_ = f; rebuildFilter(); repaint(); return; }
    if (const int r = fileRowAt(e.getPosition()); r >= 0) pick(r);
    grabKeyboardFocus();
}

void FileBrowserPanel::mouseMove(const juce::MouseEvent& e) {
    const int r = fileRowAt(e.getPosition());
    if (r != hover_) { hover_ = r; repaint(); }
}

void FileBrowserPanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const bool folders = folderArea().contains(e.getPosition());
    auto& sc = folders ? folderScroll_ : fileScroll_;
    const auto a = folders ? folderArea() : fileArea();
    const float total = float(folders ? folders_.size() : visible_.size()) * 12.0f;
    sc = juce::jlimit(0.0f, std::max(0.0f, total - float(a.getHeight() - 4)), sc - w.deltaY * 96.0f);
    repaint();
}

bool FileBrowserPanel::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) return false;
    if (k == juce::KeyPress::downKey) { selected_ = std::min(selected_ + 1, int(visible_.size()) - 1); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::upKey) { selected_ = std::max(selected_ - 1, 0); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::pageDownKey) { selected_ = std::min(selected_ + 12, int(visible_.size()) - 1); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::pageUpKey) { selected_ = std::max(selected_ - 12, 0); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::returnKey) { pick(selected_); return true; }
    if (k == juce::KeyPress::backspaceKey) { search_ = search_.dropLastCharacters(1); rebuildFilter(); repaint(); return true; }
    const auto c = k.getTextCharacter();
    if (c >= 32 && c < 127) { search_ += juce::String::charToString(c); rebuildFilter(); repaint(); return true; }
    return false;
}

void FileBrowserPanel::paint(juce::Graphics& g) {
    ModalPanel::paint(g);
    const auto s = searchArea();
    wellBox(g, s, pal::sunken);
    drawIconCentred(g, Icon::search, {s.getX() + 2, s.getY(), 12, s.getHeight()}, pal::textMuted);
    const bool caret = int(Animator::seconds() * 2.0) % 2 == 0;
    drawTextIn(g, search_.isEmpty() ? juce::String("TYPE TO SEARCH...") : search_ + (caret ? "_" : " "), s.withTrimmedLeft(16),
               search_.isEmpty() ? pal::textMuted : pal::lcd);
    drawTextIn(g, juce::String(visible_.size()) + " OF " + juce::String(entries_.size()), s.withTrimmedLeft(s.getWidth() - 90).withTrimmedRight(4), pal::textMuted, juce::Justification::centredRight);

    const auto fa = folderArea();
    wellBox(g, fa, pal::sunken);
    g.saveState(); g.reduceClipRegion(fa.reduced(1));
    for (std::size_t i = 0; i < folders_.size(); ++i) {
        const int y = fa.getY() + 2 + int(i) * 12 - int(folderScroll_);
        if (y > fa.getBottom() || y < fa.getY() - 12) continue;
        const bool sel = int(i) == folder_;
        if (sel) { g.setColour(pal::violetShadow); g.fillRect(fa.getX() + 1, y, fa.getWidth() - 2, 11); g.setColour(pal::violetHot); g.fillRect(fa.getX() + 1, y, 2, 11); }
        drawTextIn(g, folders_[i], {fa.getX() + 6, y, fa.getWidth() - 34, 11}, sel ? pal::textHi : pal::textBody);
        drawTextIn(g, juce::String(folderCounts_[i]), {fa.getRight() - 30, y, 26, 11}, pal::textMuted, juce::Justification::centredRight);
    }
    g.restoreState();

    const auto a = fileArea();
    wellBox(g, a, pal::sunken);
    g.saveState(); g.reduceClipRegion(a.reduced(1));
    if (scanning_) {
        drawSpinner(g, a.getCentreX(), a.getCentreY() - 6, 8, pal::acid, Animator::seconds() * 1.4);
        drawTextIn(g, "SCANNING CONTENT LIBRARY...", {a.getX(), a.getCentreY() + 6, a.getWidth(), 10}, pal::textMuted, juce::Justification::centred);
    } else if (visible_.empty()) {
        drawTextIn(g, entries_.empty() ? "NO FILES FOUND IN " + root_.getFullPathName().toUpperCase() : "NO MATCHES", a, pal::textMuted, juce::Justification::centred);
    }
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        const int y = a.getY() + 2 + int(i) * 12 - int(fileScroll_);
        if (y > a.getBottom()) break;
        if (y < a.getY() - 12) continue;
        const auto& e = entries_[std::size_t(visible_[i])];
        const bool sel = int(i) == selected_, hov = int(i) == hover_;
        if (sel || hov) { g.setColour(sel ? pal::violet : pal::raised); g.fillRect(a.getX() + 1, y, a.getWidth() - 2, 11); }
        const bool cur = e.file == current_;
        drawTextIn(g, e.name, {a.getX() + 6, y, a.getWidth() * 60 / 100, 11}, sel ? pal::acidHot : cur ? pal::acid : pal::textHi);
        if (folder_ == 0) drawTextIn(g, e.folder, {a.getX() + a.getWidth() * 62 / 100, y, a.getWidth() * 38 / 100 - 10, 11}, sel ? pal::textHi : pal::textMuted);
    }
    g.restoreState();
    const float total = float(visible_.size()) * 12.0f;
    if (total > float(a.getHeight() - 4)) {
        const int th = std::max(10, int(float(a.getHeight()) * float(a.getHeight()) / total));
        const int ty = a.getY() + int(fileScroll_ / total * float(a.getHeight()));
        g.setColour(pal::edgeLight); g.fillRect(a.getRight() - 4, ty, 3, th);
    }
    drawTextIn(g, "CLICK OR ENTER TO LOAD   UP/DOWN MOVE   ESC CLOSE", body().withTop(body().getBottom() - 12), pal::textMuted);
}

}
