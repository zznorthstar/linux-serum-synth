#pragma once
// Structural editing of the FX rack: modules of a rack form a tree because a splitter owns
// the modules that follow it (per band). Edits keep `position` and the splitter counts consistent
// and re-point modulation routes when modules are removed.
#include "../Patch.h"
#include "../FxParams.h"
#include <vector>

namespace zyg::ui {

struct FxNode {
    int index = -1;                         // index into Patch::fx
    std::vector<FxNode> bands[3];
};

bool isSplitter(FxType t) noexcept;
int bandCount(FxType t) noexcept;            // 0 for ordinary modules, 2 or 3 for splitters
const char* fxDisplayName(FxType t) noexcept;
const char* fxSerumName(FxType t) noexcept;  // provenance class name, e.g. "FXDelay"
const char* bandName(FxType t, int band) noexcept;

std::vector<FxNode> parseRack(const Patch& p, int rack);
// Flattens the tree, assigns positions and refreshes splitter module counts.
void writeRack(Patch& p, int rack, const std::vector<FxNode>& tree);

// path = index of the owning splitter node inside the rack (or -1 for top level) and band.
void addFxModule(Patch& p, int rack, FxType type, int splitterIndex = -1, int band = 0);
void removeFxModule(Patch& p, int index);
void moveFxModule(Patch& p, int index, int direction);      // -1 up, +1 down among siblings
// Depth-first flattened order with nesting information (for lists).
struct FxRow { int index; int depth; int band; int owner; bool bandHeader; int bandOfOwner; };
std::vector<FxRow> flattenRack(const Patch& p, int rack);

}
