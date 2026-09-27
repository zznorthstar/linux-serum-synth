#pragma once
// Names and menus for modulation sources and destinations (matrix, macros).
#include "Panels.h"

namespace zyg::ui {

struct SourceEntry { const char* group; juce::String label; ModSource kind; int index; };
const std::vector<SourceEntry>& sourceCatalog();
juce::String sourceLabel(ModSource kind, int index);
void setRouteSource(ModulationRoute& r, ModSource kind, int index);
void setRouteAux(ModulationRoute& r, ModSource kind, int index);

struct DestEntry { juce::String group, label; ModTarget kind; int inst, param; };
// Everything a route can point at for `patch` (FX destinations depend on the rack contents).
std::vector<DestEntry> destinationCatalog(const Patch& patch);
juce::String destinationLabel(const Patch& patch, const ModulationRoute& r);
void setRouteDestination(ModulationRoute& r, const DestEntry& d);


// Serum-style drag-and-drop: creates (or finds) the matrix route source -> target.
// Returns the slot index; `created` says whether a new route was added. -1 when the matrix is full.
int addModulationRoute(Patch& p, ModSource kind, int index, const TargetId& target, bool& created, double amount = 50.0);
// Drag payload helpers ("zygmod:<kind>:<index>").
juce::var makeDragPayload(ModSource kind, int index);
bool parseDragPayload(const juce::var& v, ModSource& kind, int& index);
juce::String targetLabel(const Patch& p, const TargetId& t);
// Begins a drag of a modulation source from `src` (needs an ancestor DragAndDropContainer).
void startModDrag(juce::Component& src, ModSource kind, int index, int uiScale);
// True while a modulation source is being dragged (targets light up).
bool modDragActive() noexcept;
void setModDragActive(bool b) noexcept;

}
