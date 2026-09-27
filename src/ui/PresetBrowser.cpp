#include "PresetBrowser.h"
#include "AssetBrowse.h"
#include "UserLibrary.h"

namespace zyg::ui {

// ------------------------------------------------------------------ scanning
std::vector<PresetBrowserPanel::Entry> PresetBrowserPanel::scan(const std::vector<std::tuple<juce::String, juce::File, bool>>& roots,
                                                              const std::atomic<bool>* cancel) {
    std::vector<Entry> out;
    for (const auto& [label, root, user] : roots) {
        if (!root.isDirectory()) continue;
        for (const auto& f : root.findChildFiles(juce::File::findFiles, true, library::presetWildcards())) {
            if (cancel && cancel->load()) return {};
            Entry e; e.file = f; e.user = user; e.native = f.hasFileExtension("zygpreset");
            e.name = f.getFileNameWithoutExtension().replaceCharacters("_", " ");
            auto rel = f.getParentDirectory().getRelativePathFrom(root).replaceCharacter('\\', '/');
            if (rel == ".") rel = {};
            // factory content already starts with its own top folder ("Factory/Bass/..."); user presets get a USER prefix
            e.folder = rel.startsWithIgnoreCase(label) ? rel.toUpperCase() : label + (rel.isNotEmpty() ? "/" + rel.toUpperCase() : juce::String());
            e.type = library::presetTypeOf(f, root);
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.user != b.user) return a.user;                    // user presets first
        return a.name.compareNatural(b.name) < 0;
    });
    return out;
}

struct PresetBrowserPanel::Scanner : juce::Thread {
    Scanner(std::vector<std::tuple<juce::String, juce::File, bool>> r, std::function<void(std::vector<Entry>)> done)
        : juce::Thread("zyg-preset-scan"), roots(std::move(r)), done_(std::move(done)) {}
    void run() override {
        auto out = scan(roots, &cancel);
        if (!threadShouldExit() && !cancel.load()) done_(std::move(out));
    }
    std::vector<std::tuple<juce::String, juce::File, bool>> roots;
    std::function<void(std::vector<Entry>)> done_;
    std::atomic<bool> cancel {false};
};

PresetBrowserPanel::PresetBrowserPanel(UiContext& c, Options opts)
    : ModalPanel("PRESET BROWSER"), ctx_(c), opts_(std::move(opts)) {
    setWantsKeyboardFocus(true);
    setBusy(true);
    loaded_ = juce::File(juce::String(ctx_.patch->sourcePath));
    const auto& st = ctx_.presetBrowse;
    if (opts_.restore && st.valid) { opts_.source = Source(st.source); opts_.type = st.type; format_ = st.format; search_ = st.search; }
    library::ensureUserFolders();
    std::vector<std::tuple<juce::String, juce::File, bool>> roots;
    roots.emplace_back("USER", library::presetsDir(), true);
    roots.emplace_back("FACTORY", contentRoot(*ctx_.patch).getChildFile("Presets"), false);
    auto alive = alive_;
    juce::Component::SafePointer<PresetBrowserPanel> safe(this);
    scanner_ = std::make_unique<Scanner>(std::move(roots), [alive, safe](std::vector<Entry> entries) {
        auto shared = std::make_shared<std::vector<Entry>>(std::move(entries));
        juce::MessageManager::callAsync([alive, safe, shared] { if (alive->load() && safe) safe->applyResults(std::move(*shared)); });
    });
    scanner_->startThread();
    startTimerHz(24);
}

PresetBrowserPanel::~PresetBrowserPanel() {
    alive_->store(false);
    if (scanner_) { scanner_->cancel.store(true); scanner_->stopThread(3000); }
}

void PresetBrowserPanel::timerCallback() {
    // follow loads finished by the async loader (and keep the spinner moving while scanning)
    const juce::File now(juce::String(ctx_.patch->sourcePath));
    if (now != loaded_) { loaded_ = now; repaint(); }
    if (scanning_) repaint();
}

void PresetBrowserPanel::applyResults(std::vector<Entry> entries) {
    entries_ = std::move(entries);
    scanning_ = false; setBusy(false);
    rebuildFilter();
    for (std::size_t i = 0; i < visible_.size(); ++i)
        if (entries_[std::size_t(visible_[i])].file == loaded_) { selected_ = int(i); ensureVisible(); }
    repaint();
}

void PresetBrowserPanel::rebuildFilter() {
    // type list reflects the source/format/search filters so counts stay honest
    juce::StringArray tokens; tokens.addTokens(search_.toLowerCase(), " ", ""); tokens.removeEmptyStrings();
    auto passesBase = [&](const Entry& e) {
        if (opts_.source == Source::factory && e.user) return false;
        if (opts_.source == Source::user && !e.user) return false;
        if (format_ == 1 && e.native) return false;
        if (format_ == 2 && !e.native) return false;
        const auto hay = (e.folder + " " + e.name + " " + e.type).toLowerCase();
        for (const auto& t : tokens) if (!hay.contains(t)) return false;
        return true;
    };
    const auto previousType = type_ > 0 && type_ < types_.size() ? types_[type_] : opts_.type;
    types_.clearQuick(); typeCounts_.clear();
    types_.add("ALL"); typeCounts_.push_back(0);
    for (const auto& t : library::presetTypes()) { types_.add(t); typeCounts_.push_back(0); }
    for (const auto& e : entries_) if (passesBase(e)) { ++typeCounts_[0]; ++typeCounts_[std::size_t(types_.indexOf(e.type))]; }
    for (int i = types_.size(); --i >= 1;) if (typeCounts_[std::size_t(i)] == 0 && types_[i] != previousType) { types_.remove(i); typeCounts_.erase(typeCounts_.begin() + i); }
    type_ = std::max(0, types_.indexOf(previousType));
    opts_.type = type_ > 0 ? types_[type_] : juce::String();

    visible_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        if (!passesBase(e)) continue;
        if (type_ > 0 && e.type != types_[type_]) continue;
        visible_.push_back(int(i));
    }
    selected_ = juce::jlimit(0, std::max(0, int(visible_.size()) - 1), selected_);
    for (std::size_t i = 0; i < visible_.size(); ++i)   // keep the loaded preset selected when filters change
        if (entries_[std::size_t(visible_[i])].file == loaded_) selected_ = int(i);
    scroll_ = 0.0f;
    ensureVisible();
    layoutChips();
    // publish the session: the top-bar arrows step through exactly this list
    if (!scanning_) {
        auto& st = ctx_.presetBrowse;
        st.valid = true; st.source = int(opts_.source); st.format = format_; st.type = opts_.type; st.search = search_;
        st.list.clear();
        for (int i : visible_) st.list.push_back(entries_[std::size_t(i)].file);
    }
}

// -------------------------------------------------------------------- layout
juce::Rectangle<int> PresetBrowserPanel::searchArea() const { return body().withHeight(17).withTrimmedRight(250); }
juce::Rectangle<int> PresetBrowserPanel::footerArea() const { return body().withTop(body().getBottom() - 17); }
juce::Rectangle<int> PresetBrowserPanel::typeArea() const {
    auto b = body().withTrimmedTop(22).withTrimmedBottom(21);
    return b.removeFromLeft(118);
}
juce::Rectangle<int> PresetBrowserPanel::listArea() const {
    auto b = body().withTrimmedTop(22).withTrimmedBottom(21);
    b.removeFromLeft(122);
    return b;
}

void PresetBrowserPanel::layoutChips() {
    chips_.clear();
    const auto b = body();
    int x = b.getRight() - 246;
    auto add = [&](int group, int value, const juce::String& label) {
        const int w = textWidth(label) + 12;
        chips_.push_back({{x, b.getY(), w, 17}, group, value, label});
        x += w + 2;
    };
    add(0, 0, "ALL"); add(0, 1, "FACTORY"); add(0, 2, "USER");
    x += 6;
    add(1, 0, "ANY"); add(1, 1, "S2"); add(1, 2, "ZYG");
    const auto f = footerArea();
    doneBtn_ = {f.getRight() - 60, f.getY(), 60, 17};
    folderBtn_ = {doneBtn_.getX() - 128, f.getY(), 124, 17};
}

int PresetBrowserPanel::rowAt(juce::Point<int> p) const {
    const auto a = listArea();
    if (!a.contains(p)) return -1;
    const int i = int((float(p.y - a.getY() - 16) + scroll_) / float(kRowH));
    return p.y >= a.getY() + 16 && i >= 0 && i < int(visible_.size()) ? i : -1;
}
int PresetBrowserPanel::typeRowAt(juce::Point<int> p) const {
    const auto a = typeArea();
    if (!a.contains(p)) return -1;
    const int i = (p.y - a.getY() - 3) / kRowH;
    return i >= 0 && i < types_.size() ? i : -1;
}

void PresetBrowserPanel::ensureVisible() {
    const auto a = listArea();
    const float view = float(a.getHeight() - 20);
    const float top = float(selected_) * float(kRowH), bottom = top + float(kRowH);
    if (top < scroll_) scroll_ = top;
    else if (bottom > scroll_ + view) scroll_ = bottom - view;
}

// ------------------------------------------------------------------- actions
void PresetBrowserPanel::audition(int i) {
    if (i < 0 || i >= int(visible_.size())) return;
    selected_ = i; ensureVisible();
    const auto f = entries_[std::size_t(visible_[std::size_t(i)])].file;
    loaded_ = f;
    if (ctx_.loadPresetFile) ctx_.loadPresetFile(f);
    repaint();
}

void PresetBrowserPanel::keepAndClose(int i) {
    if (i >= 0 && i < int(visible_.size()) && entries_[std::size_t(visible_[std::size_t(i)])].file != loaded_) audition(i);
    if (requestClose) requestClose();
}

void PresetBrowserPanel::mouseDown(const juce::MouseEvent& e) {
    ModalPanel::mouseDown(e);
    grabKeyboardFocus();
    const auto p = e.getPosition();
    for (const auto& c : chips_)
        if (c.r.contains(p)) {
            if (c.group == 0) opts_.source = Source(c.value); else format_ = c.value;
            rebuildFilter(); repaint(); return;
        }
    if (doneBtn_.contains(p)) { if (requestClose) requestClose(); return; }
    if (folderBtn_.contains(p)) { library::ensureUserFolders(); library::presetsDir().startAsProcess(); return; }
    if (const int t = typeRowAt(p); t >= 0) { type_ = t; opts_.type = t > 0 ? types_[t] : juce::String(); rebuildFilter(); repaint(); return; }
    if (const int r = rowAt(p); r >= 0) audition(r);
}

void PresetBrowserPanel::mouseDoubleClick(const juce::MouseEvent& e) {
    if (const int r = rowAt(e.getPosition()); r >= 0) keepAndClose(r);
}

void PresetBrowserPanel::mouseMove(const juce::MouseEvent& e) {
    const int r = rowAt(e.getPosition());
    int chip = -1;
    for (std::size_t i = 0; i < chips_.size(); ++i) if (chips_[i].r.contains(e.getPosition())) chip = int(i);
    if (doneBtn_.contains(e.getPosition())) chip = 100;
    if (folderBtn_.contains(e.getPosition())) chip = 101;
    if (r != hover_ || chip != hoverChip_) { hover_ = r; hoverChip_ = chip; repaint(); }
}

void PresetBrowserPanel::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    const auto a = listArea();
    const float total = float(visible_.size()) * float(kRowH);
    scroll_ = juce::jlimit(0.0f, std::max(0.0f, total - float(a.getHeight() - 20)), scroll_ - w.deltaY * 120.0f);
    repaint();
}

bool PresetBrowserPanel::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) return false;   // the modal layer closes
    const int n = int(visible_.size());
    if (k == juce::KeyPress::downKey) { audition(std::min(selected_ + 1, n - 1)); return true; }
    if (k == juce::KeyPress::upKey) { audition(std::max(selected_ - 1, 0)); return true; }
    if (k == juce::KeyPress::pageDownKey) { selected_ = std::min(selected_ + 16, n - 1); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::pageUpKey) { selected_ = std::max(selected_ - 16, 0); ensureVisible(); repaint(); return true; }
    if (k == juce::KeyPress::returnKey) { keepAndClose(selected_); return true; }
    if (k == juce::KeyPress::backspaceKey) { search_ = search_.dropLastCharacters(1); rebuildFilter(); repaint(); return true; }
    const auto c = k.getTextCharacter();
    if (c >= 32 && c < 127) { search_ += juce::String::charToString(c); rebuildFilter(); repaint(); return true; }
    return false;
}

// --------------------------------------------------------------------- paint
void PresetBrowserPanel::paint(juce::Graphics& g) {
    ModalPanel::paint(g);
    if (chips_.empty()) layoutChips();
    // search
    const auto s = searchArea();
    wellBox(g, s, pal::sunken);
    drawIconCentred(g, Icon::search, {s.getX() + 3, s.getY(), 12, s.getHeight()}, pal::textMuted);
    const bool caret = int(Animator::seconds() * 2.0) % 2 == 0;
    drawTextIn(g, search_.isEmpty() ? juce::String("TYPE TO SEARCH NAME, FOLDER OR TYPE...") : search_ + (caret ? "_" : " "),
               s.withTrimmedLeft(18).withTrimmedRight(70), search_.isEmpty() ? pal::textMuted : pal::lcd);
    drawTextIn(g, juce::String(visible_.size()) + " / " + juce::String(entries_.size()), s.withTrimmedRight(5), pal::textMuted, juce::Justification::centredRight);
    // source + format chips
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const auto& c = chips_[i];
        const bool on = c.group == 0 ? int(opts_.source) == c.value : format_ == c.value;
        const bool hov = hoverChip_ == int(i);
        g.setColour(on ? pal::violetShadow : hov ? pal::raisedHi : pal::raised); g.fillRect(c.r);
        zyg::ui::frame(g, c.r, on ? pal::violetHot : pal::edgeMid);
        drawTextIn(g, c.label, c.r, on ? pal::textHi : pal::textBody, juce::Justification::centred);
    }
    // type list
    const auto ta = typeArea();
    wellBox(g, ta, pal::sunken);
    for (int i = 0; i < types_.size(); ++i) {
        const int y = ta.getY() + 3 + i * kRowH;
        if (y + kRowH > ta.getBottom()) break;
        const bool sel = i == type_;
        if (sel) { g.setColour(pal::violetShadow); g.fillRect(ta.getX() + 1, y, ta.getWidth() - 2, kRowH - 1); g.setColour(pal::violetHot); g.fillRect(ta.getX() + 1, y, 2, kRowH - 1); }
        drawTextIn(g, types_[i], {ta.getX() + 7, y, ta.getWidth() - 40, kRowH - 1}, sel ? pal::textHi : pal::textBody);
        drawTextIn(g, juce::String(typeCounts_[std::size_t(i)]), {ta.getRight() - 34, y, 29, kRowH - 1}, pal::textMuted, juce::Justification::centredRight);
    }
    // preset list
    const auto a = listArea();
    wellBox(g, a, pal::sunken);
    const int nameW = a.getWidth() * 48 / 100, typeX = a.getX() + 8 + nameW, folderX = typeX + 64;
    g.setColour(pal::raised); g.fillRect(a.getX() + 1, a.getY() + 1, a.getWidth() - 2, 14);
    drawSmall(g, "NAME", a.getX() + 18, a.getY() + 5, pal::textMuted);
    drawSmall(g, "TYPE", typeX, a.getY() + 5, pal::textMuted);
    drawSmall(g, "LOCATION", folderX, a.getY() + 5, pal::textMuted);
    drawSmall(g, "FORMAT", a.getRight() - 42, a.getY() + 5, pal::textMuted);
    const auto clip = a.withTrimmedTop(16).reduced(1, 0);
    g.saveState(); g.reduceClipRegion(clip);
    if (scanning_) {
        drawSpinner(g, clip.getCentreX(), clip.getCentreY() - 6, 8, pal::acid, Animator::seconds() * 1.4);
        drawTextIn(g, "SCANNING PRESETS...", {clip.getX(), clip.getCentreY() + 6, clip.getWidth(), 10}, pal::textMuted, juce::Justification::centred);
    } else if (visible_.empty()) {
        const bool anyUser = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.user; });
        const bool noUser = opts_.source == Source::user && !anyUser;
        if (entries_.empty() || noUser) {
            drawTextIn(g, noUser ? "NO USER PRESETS YET" : "NO PRESETS FOUND", clip.withTrimmedBottom(24), pal::textBody, juce::Justification::centred);
            drawTextIn(g, "PUT .SERUMPRESET OR .ZYGPRESET FILES IN " + library::presetsDir().getFullPathName().toUpperCase(),
                       clip.withTrimmedTop(24), pal::textMuted, juce::Justification::centred);
        } else drawTextIn(g, "NO MATCHES", clip, pal::textMuted, juce::Justification::centred);
    }
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        const int y = clip.getY() + int(i) * kRowH - int(scroll_);
        if (y > clip.getBottom()) break;
        if (y < clip.getY() - kRowH) continue;
        const auto& e = entries_[std::size_t(visible_[i])];
        const bool sel = int(i) == selected_, hov = int(i) == hover_, cur = e.file == loaded_;
        if (sel || hov) { g.setColour(sel ? pal::violet : pal::raised); g.fillRect(clip.getX(), y, clip.getWidth(), kRowH - 1); }
        if (cur) drawIcon(g, Icon::play, a.getX() + 7, y + (kRowH - 5) / 2, sel ? pal::acidHot : pal::acid);
        drawTextIn(g, e.name, {a.getX() + 18, y, nameW - 12, kRowH - 1}, sel ? pal::acidHot : cur ? pal::acid : pal::textHi);
        drawTextIn(g, e.type, {typeX, y, 60, kRowH - 1}, sel ? pal::textHi : pal::textBody);
        drawTextIn(g, e.folder, {folderX, y, a.getRight() - 50 - folderX, kRowH - 1}, sel ? pal::textHi : pal::textMuted);
        const auto badge = juce::Rectangle<int>(a.getRight() - 42, y + 2, 30, kRowH - 5);
        g.setColour(e.native ? pal::acidShadow : pal::violetShadow); g.fillRect(badge);
        drawSmall(g, e.native ? "ZYG" : "S2", badge.getX() + (e.native ? 7 : 10), badge.getY() + (badge.getHeight() - 5) / 2, e.native ? pal::acid : pal::violetHot);
    }
    g.restoreState();
    const float total = float(visible_.size()) * float(kRowH);
    if (total > float(clip.getHeight())) {
        const int th = std::max(10, int(float(clip.getHeight()) * float(clip.getHeight()) / total));
        const int ty = clip.getY() + int(scroll_ / total * float(clip.getHeight()));
        g.setColour(pal::edgeLight); g.fillRect(a.getRight() - 4, ty, 3, th);
    }
    // footer
    const auto f = footerArea();
    drawTextIn(g, "CLICK / UP-DOWN: AUDITION    ENTER / DOUBLE-CLICK: KEEP + CLOSE    ESC: CLOSE", f.withTrimmedRight(200), pal::textMuted);
    for (auto [r, label, id] : {std::tuple{folderBtn_, juce::String("OPEN USER FOLDER"), 101}, std::tuple{doneBtn_, juce::String("DONE"), 100}}) {
        bevelBox(g, r, hoverChip_ == id ? pal::raisedHi : pal::raised);
        drawTextIn(g, label, r, id == 100 ? pal::acidHot : pal::textBody, juce::Justification::centred, 1, id == 100);
    }
}

}
