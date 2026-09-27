#include "FxTree.h"
#include <algorithm>
#include <functional>

namespace zyg::ui {

bool isSplitter(FxType t) noexcept { return t == FxType::split || t == FxType::split3 || t == FxType::splitMS; }
int bandCount(FxType t) noexcept { return t == FxType::split3 ? 3 : (t == FxType::split || t == FxType::splitMS) ? 2 : 0; }

const char* fxDisplayName(FxType t) noexcept {
    switch (t) {
        case FxType::bode: return "BODE"; case FxType::chorus: return "CHORUS"; case FxType::comp: return "COMPRESSOR";
        case FxType::conv: return "CONVOLVE"; case FxType::delay: return "DELAY"; case FxType::distortion: return "DISTORTION";
        case FxType::eq: return "EQUALIZER"; case FxType::filter: return "FILTER"; case FxType::flanger: return "FLANGER";
        case FxType::hyperD: return "HYPER/DIM"; case FxType::phaser: return "PHASER"; case FxType::reverb: return "REVERB";
        case FxType::utils: return "UTILITY"; case FxType::split: return "SPLITTER L/H"; case FxType::split3: return "SPLITTER L/M/H";
        case FxType::splitMS: return "SPLITTER M/S"; case FxType::pump: return "PUMP"; case FxType::stutter: return "STUTTER"; default: return "UNKNOWN";
    }
}
const char* fxSerumName(FxType t) noexcept {
    switch (t) {
        case FxType::bode: return "FXBode"; case FxType::chorus: return "FXChorus"; case FxType::comp: return "FXComp";
        case FxType::conv: return "FXConv"; case FxType::delay: return "FXDelay"; case FxType::distortion: return "FXDistortion";
        case FxType::eq: return "FXEQ"; case FxType::filter: return "FXFilter"; case FxType::flanger: return "FXFlanger";
        case FxType::hyperD: return "FXHyperD"; case FxType::phaser: return "FXPhaser"; case FxType::reverb: return "FXReverb";
        case FxType::utils: return "FXUtils"; case FxType::split: return "FXSplit"; case FxType::split3: return "FXSplit3";
        case FxType::splitMS: return "FXSplitMS"; case FxType::pump: return "ZYGPump"; case FxType::stutter: return "ZYGStutter"; default: return "FXUnknown";
    }
}
const char* bandName(FxType t, int band) noexcept {
    if (t == FxType::split3) { static const char* n[] = {"LOWS", "MIDS", "HIGHS"}; return n[band % 3]; }
    if (t == FxType::split) { static const char* n[] = {"LOWS", "HIGHS"}; return n[band % 2]; }
    static const char* n[] = {"MID", "SIDE"}; return n[band % 2];
}

namespace {
int countOf(const Patch& p, int idx, int band) {
    const auto& m = p.fx[std::size_t(idx)];
    const auto slot = std::size_t(fx::sCount1 + band);
    return std::clamp(int(std::lround(m.p[slot])), 0, 8);
}

std::vector<FxNode> parseSeq(const Patch& p, const std::vector<int>& list, std::size_t& cursor, std::size_t end) {
    std::vector<FxNode> out;
    while (cursor < end && cursor < list.size()) {
        FxNode n; n.index = list[cursor++];
        const auto type = p.fx[std::size_t(n.index)].fxType;
        for (int b = 0; b < bandCount(type); ++b) {
            const std::size_t bandEnd = std::min(end, cursor + std::size_t(countOf(p, n.index, b)));
            n.bands[b] = parseSeq(p, list, cursor, bandEnd);
            cursor = std::max(cursor, bandEnd);
        }
        out.push_back(std::move(n));
    }
    return out;
}

int flatSize(const std::vector<FxNode>& v) {
    int n = 0;
    for (const auto& x : v) { n += 1; for (const auto& b : x.bands) n += flatSize(b); }
    return n;
}

void flatten(Patch& p, const std::vector<FxNode>& v, int rack, int& pos) {
    for (const auto& n : v) {
        auto& m = p.fx[std::size_t(n.index)];
        m.rack = rack; m.position = pos++;
        for (int b = 0; b < bandCount(m.fxType); ++b) {
            const auto slot = std::size_t(fx::sCount1 + b);
            m.p[slot] = double(flatSize(n.bands[b])); m.set[slot] = true;
            flatten(p, n.bands[b], rack, pos);
        }
    }
}

// locate the vector that contains `index` (top level or inside a band); returns nullptr if absent
std::vector<FxNode>* findList(std::vector<FxNode>& v, int index, std::size_t& at) {
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i].index == index) { at = i; return &v; }
        for (auto& b : v[i].bands) if (auto* r = findList(b, index, at)) return r;
    }
    return nullptr;
}
FxNode* findNode(std::vector<FxNode>& v, int index) {
    for (auto& n : v) {
        if (n.index == index) return &n;
        for (auto& b : n.bands) if (auto* r = findNode(b, index)) return r;
    }
    return nullptr;
}
void collect(const FxNode& n, std::vector<int>& out) {
    out.push_back(n.index);
    for (const auto& b : n.bands) for (const auto& c : b) collect(c, out);
}
void remap(std::vector<FxNode>& v, const std::vector<int>& removedSorted) {
    for (auto& n : v) {
        n.index -= int(std::count_if(removedSorted.begin(), removedSorted.end(), [&](int r) { return r < n.index; }));
        for (auto& b : n.bands) remap(b, removedSorted);
    }
}
}

std::vector<FxNode> parseRack(const Patch& p, int rack) {
    std::vector<int> list;
    for (std::size_t i = 0; i < p.fx.size(); ++i) if (p.fx[i].rack == rack) list.push_back(int(i));
    std::stable_sort(list.begin(), list.end(), [&](int a, int b) { return p.fx[std::size_t(a)].position < p.fx[std::size_t(b)].position; });
    std::size_t cursor = 0;
    auto tree = parseSeq(p, list, cursor, list.size());
    // anything the counts did not claim is kept at top level so nothing is lost
    while (cursor < list.size()) { FxNode n; n.index = list[cursor++]; tree.push_back(std::move(n)); }
    return tree;
}

void writeRack(Patch& p, int rack, const std::vector<FxNode>& tree) {
    int pos = 0;
    flatten(p, tree, rack, pos);
}

void addFxModule(Patch& p, int rack, FxType type, int splitterIndex, int band) {
    FxModule m;
    m.type = fxSerumName(type); m.fxType = type; m.rack = rack; m.enabled = true; m.position = 1000000;
    const auto table = fxParamTable(type);
    for (std::size_t i = 0; i < table.size() && i < m.p.size(); ++i) { m.p[i] = table[i].def; m.set[i] = true; }
    m.p[fxLevelSlot] = 0.5; m.set[fxLevelSlot] = true;
    if (type == FxType::filter) { m.filterResponse = FilterResponse::low12; m.filterVariant = 0; }
    if (type == FxType::distortion) m.modeVariant = 6;
    if (type == FxType::split3) { m.p[fx::sFreq] = 210; m.p[fx::sFreq2] = 1000; }
    if (type == FxType::split) m.p[fx::sFreq] = 350;
    p.fx.push_back(std::move(m));
    const int idx = int(p.fx.size()) - 1;
    auto tree = parseRack(p, rack);
    // parseRack put the new module in the rack list at position 0; rebuild without it, then insert
    std::size_t at = 0;
    if (auto* list = findList(tree, idx, at)) list->erase(list->begin() + std::ptrdiff_t(at));
    FxNode node; node.index = idx;
    FxNode* owner = splitterIndex >= 0 ? findNode(tree, splitterIndex) : nullptr;
    if (owner && bandCount(p.fx[std::size_t(owner->index)].fxType) > 0) owner->bands[std::clamp(band, 0, 2)].push_back(std::move(node));
    else tree.push_back(std::move(node));
    writeRack(p, rack, tree);
}

void removeFxModule(Patch& p, int index) {
    if (index < 0 || index >= int(p.fx.size())) return;
    const int rack = p.fx[std::size_t(index)].rack;
    auto tree = parseRack(p, rack);
    std::size_t at = 0;
    auto* list = findList(tree, index, at);
    std::vector<int> removed;
    if (list) { collect((*list)[at], removed); list->erase(list->begin() + std::ptrdiff_t(at)); }
    else removed.push_back(index);
    std::sort(removed.begin(), removed.end());
    remap(tree, removed);
    // modulation routes: drop those aimed at removed modules, shift the rest
    std::erase_if(p.modulation, [&](const ModulationRoute& r) {
        return r.targetKind == ModTarget::fxParam && std::binary_search(removed.begin(), removed.end(), r.targetIndex);
    });
    for (auto& r : p.modulation)
        if (r.targetKind == ModTarget::fxParam)
            r.targetIndex -= int(std::count_if(removed.begin(), removed.end(), [&](int x) { return x < r.targetIndex; }));
    for (auto it = removed.rbegin(); it != removed.rend(); ++it) p.fx.erase(p.fx.begin() + *it);
    writeRack(p, rack, tree);
}

void moveFxModule(Patch& p, int index, int direction) {
    if (index < 0 || index >= int(p.fx.size())) return;
    const int rack = p.fx[std::size_t(index)].rack;
    auto tree = parseRack(p, rack);
    std::size_t at = 0;
    auto* list = findList(tree, index, at);
    if (!list) return;
    const auto to = std::ptrdiff_t(at) + direction;
    if (to < 0 || to >= std::ptrdiff_t(list->size())) return;
    std::swap((*list)[at], (*list)[std::size_t(to)]);
    writeRack(p, rack, tree);
}

std::vector<FxRow> flattenRack(const Patch& p, int rack) {
    std::vector<FxRow> rows;
    std::function<void(const std::vector<FxNode>&, int, int)> walk = [&](const std::vector<FxNode>& v, int depth, int owner) {
        for (const auto& n : v) {
            rows.push_back({n.index, depth, 0, owner, false, 0});
            const auto type = p.fx[std::size_t(n.index)].fxType;
            for (int b = 0; b < bandCount(type); ++b) {
                rows.push_back({n.index, depth + 1, b, n.index, true, b});
                walk(n.bands[b], depth + 2, n.index);
            }
        }
    };
    walk(parseRack(p, rack), 0, -1);
    return rows;
}

}
