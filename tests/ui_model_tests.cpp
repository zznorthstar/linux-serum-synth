// Tests for the editor's FX rack model (src/ui/FxTree): structure edits keep positions,
// splitter counts and modulation-route indices consistent. No JUCE needed.
#include "ui/FxTree.h"
#include <cstdlib>
#include <functional>
#include <iostream>

using namespace zyg;
using namespace zyg::ui;

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::cout << "  FAIL line " << __LINE__ << ": " #c "\n"; ++g_failed; return; } } while (0)

static std::vector<int> order(const Patch& p, int rack) {
    std::vector<int> v;
    for (const auto& r : flattenRack(p, rack)) if (!r.bandHeader) v.push_back(r.index);
    return v;
}

static void addAppendsInOrder() {
    Patch p;
    addFxModule(p, 0, FxType::delay);
    addFxModule(p, 0, FxType::reverb);
    addFxModule(p, 1, FxType::chorus);
    CHECK(p.fx.size() == 3);
    CHECK((order(p, 0) == std::vector<int>{0, 1}));
    CHECK((order(p, 1) == std::vector<int>{2}));
    CHECK(p.fx[1].position == 1 && p.fx[1].fxType == FxType::reverb && p.fx[1].enabled);
}

static void splitterBandsCountChildren() {
    Patch p;
    addFxModule(p, 0, FxType::split3);            // 0
    addFxModule(p, 0, FxType::distortion, 0, 1);  // 1 in MIDS
    addFxModule(p, 0, FxType::comp, 0, 1);        // 2 in MIDS
    addFxModule(p, 0, FxType::delay);             // 3 after the splitter
    const auto& s = p.fx[0];
    CHECK(int(s.p[fx::sCount1]) == 0 && int(s.p[fx::sCount2]) == 2 && int(s.p[fx::sCount3]) == 0);
    const auto rows = flattenRack(p, 0);
    int nested = 0; for (const auto& r : rows) if (!r.bandHeader && r.depth > 0) ++nested;
    CHECK(nested == 2);
    CHECK((order(p, 0) == std::vector<int>{0, 1, 2, 3}));
}

static void removeSplitterRemovesChildrenAndReindexes() {
    Patch p;
    addFxModule(p, 0, FxType::split);          // 0
    addFxModule(p, 0, FxType::distortion, 0, 0); // 1
    addFxModule(p, 0, FxType::reverb);         // 2
    ModulationRoute onReverb; onReverb.targetKind = ModTarget::fxParam; onReverb.targetIndex = 2; onReverb.targetParam = 0;
    ModulationRoute onChild; onChild.targetKind = ModTarget::fxParam; onChild.targetIndex = 1; onChild.targetParam = 1;
    p.modulation = {onReverb, onChild};
    removeFxModule(p, 0);
    CHECK(p.fx.size() == 1 && p.fx[0].fxType == FxType::reverb);
    CHECK(p.modulation.size() == 1);            // the route into the removed distortion is gone
    CHECK(p.modulation[0].targetIndex == 0);    // the reverb route now points at index 0
    CHECK(p.fx[0].position == 0);
}

static void moveSwapsSiblingsOnly() {
    Patch p;
    addFxModule(p, 0, FxType::delay);           // 0
    addFxModule(p, 0, FxType::split);           // 1
    addFxModule(p, 0, FxType::eq, 1, 0);        // 2 inside LOWS
    addFxModule(p, 0, FxType::reverb);          // 3
    moveFxModule(p, 3, -1);                     // reverb jumps above the splitter block as one unit
    CHECK((order(p, 0) == std::vector<int>{0, 3, 1, 2}));
    moveFxModule(p, 2, -1);                     // only child of its band: no-op
    CHECK((order(p, 0) == std::vector<int>{0, 3, 1, 2}));
    CHECK(int(p.fx[1].p[fx::sCount1]) == 1);
}

int main() {
    struct T { const char* n; void (*f)(); } tests[] = {{"add appends in order", addAppendsInOrder}, {"splitter bands count children", splitterBandsCountChildren},
        {"remove splitter removes children and reindexes routes", removeSplitterRemovesChildrenAndReindexes}, {"move swaps siblings only", moveSwapsSiblingsOnly}};
    for (auto& t : tests) { const int before = g_failed; t.f(); std::cout << (g_failed == before ? "[ ok ] " : "[FAIL] ") << t.n << '\n'; }
    std::cout << (g_failed ? "UI model tests FAILED\n" : "UI model tests passed\n");
    return g_failed ? 1 : 0;
}
