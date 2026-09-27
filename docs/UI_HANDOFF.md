# UI handoff (for the Claude working on the DSP / plugin)

Written 2026-09-26 by the Claude that built the UI. Read `DESIGN.md` and the top of `HANDOFF.md` first.

## What exists (all uncommitted, in `src/ui/` + `src/PluginEditor.*`)
A complete pixel-art, Serum-2-layout editor: resizable window (default 1280x720, min 1000x600, integer UI scale 1-3x), five pages (OSC, MIX, FX, MATRIX, GLOBAL), bottom section (macros, 4 ENV, 10 LFO, velocity/note, voicing), keyboard dock, CLIP/ARP editors, preset/wavetable/sample/impulse browser, toasts/modals, and animated displays that react to notes and output level.
Every control edits the patch through `UiContext::edit(key, fn)` (coalesced, one `ZygProcessor::editPatch` per 60 Hz tick, with undo/redo). Widgets re-read values from an immutable `shared_ptr<const Patch>` (`ZygProcessor::getPatch()`), so the UI never touches audio-thread state.

Processor changes I made (keep them when merging): `getPatch()`; slots are `shared_ptr`; UI note/pitch-bend/CC FIFO (`uiNote`, `uiPitchBend`, `uiController`, `uiAllNotesOff`); held-note bitset (`isNoteHeld`, `anyNoteHeld`); L/R peaks; `setOscSampleFile`; `setFxImpulseFile`.

Dev tools: `zygzxg_ui_snapshot` renders any page/modal to PNG (`--editor out.png --preset f --size 1280x720 --page N --modal browser|clip|arp|about|diag|toast --live 900 --play`, needs `DISPLAY`). `zygzxg_ui_model_tests` (in `ctest`) covers the FX rack model.

## Status of verification
Built (VST3+CLAP), `ctest` passes, every page snapshotted with real presets. **Never run inside REAPER**: pointer/keyboard focus, host resize, popup menus in the host window, and drag feel are untested.

## Do this after the DSP is finished
1. Rebuild, run `ctest`, install VST3/CLAP, open in REAPER and exercise every page by hand. Fix whatever breaks (keyboard focus for text entry/browser search first: `EDITOR_WANTS_KEYBOARD_FOCUS` is FALSE in CMake).
2. Re-check everything the UI assumes about the engine; where the DSP changed, update the UI, not the other way round:
   - value ranges/units: `oscParamRange` etc. in `Patch.cpp`, FX tables in `FxParams.cpp` (the FX page builds knobs from them), filter ids in `dsp/Filters.cpp` (menu list is duplicated in `src/ui/PanelsSupport.cpp`), `OscMode`/warp enums (`buildWarpMenu`), LFO shapes (`ModSection.cpp`: an LFO with `shape == unknown` is inactive), arp shapes/clip modes (`SeqEditors.cpp`), oversampling names (`GlobalPage.cpp`), distortion/reverb mode names (`FxPage.cpp`).
   - approximations that must be made exact if the DSP defines them: `filterMagnitudeDb` (filter response curves), envelope curve drawing (uses `dsp::envelopeCurveShape`), LFO preview (uses `dsp::advanceLfo`), FX viz curves in `FxViz.cpp`.
   - fields the UI does not expose yet: velocity/note mapping curves (display only), macro names/renaming, OSC key zones, env BeatSync, effect-chain presets, drag-reordering of FX/matrix rows.
3. Add tests for anything you change in `src/ui/` (model logic in `tests/ui_model_tests.cpp`).

## Status update 2026-09-26 (DSP/plugin Claude)
- Run inside REAPER (X11, VST3 and CLAP, synthetic XTest input). Found and fixed a blocker: `ModalLayer` was made visible by `addAndMakeVisible` and swallowed every click. Now hidden at construction; the toast no longer intercepts clicks. Also set `EDITOR_WANTS_KEYBOARD_FOCUS TRUE`; numeric entry (double-click a knob, type, Return) and the browser search now work in the host. Typing into a numeric field replaces the shown value (it used to append to it).
- Drag-and-drop modulation is implemented and verified in REAPER: ENV/LFO/VELO/NOTE tabs and the macro number/badge area start a drag (`startModDrag`), every `Knob` with a target is a `DragAndDropTarget` (`Knob::itemDropped` -> `addModulationRoute`, reuse of an existing source+target route, default amount 50, toast "LFO 1 -> REVERB WIDTH"), valid targets ring while dragging, badges update. Right-click on a modulated knob opens a per-route menu (amount presets, bypass, remove, reset). Route helper is tested by `ui_route_tests` (`zygzxg_ui_snapshot --selftest-routes`). HSlider/Fader are not drop targets yet; the arc handle is not draggable (use the matrix or the right-click menu).
- Still open from the list below: velocity/note curve editing, macro renaming, key zones, chain presets, drag reordering, exposing every new DSP parameter (spectral/granular controls, arp/clip parameters beyond what is drawn).

## Required feature (implemented, kept for reference): Serum-style drag-and-drop modulation
Goal: press on a modulation source (ENV 1-4 tab, LFO 1-10 tab, macro knob, VELO/NOTE tab) and drag it onto any modulatable knob/slider; on drop a matrix route is created (`sourceKind/sourceIndex` -> `targetKind/targetIndex/targetParam`) with a default amount, and the target shows a modulation ring. Dragging an existing ring/handle on a target adjusts its amount; right-click or alt-drag removes it.

What already helps:
- Every modulatable `Knob` already knows its destination: `Knob::setTarget(TargetId{kind, inst, param})` (set for OSC, filter, env, LFO, macro, FX, global and routing knobs) and draws a violet arc from all routes that hit it (`modulationSpan`).
- Sources are identified by `ModSource` + index (`sourceCatalog()` / `setRouteSource()` in `ModCatalog.*`); routes are created in `MatrixRow` via `ensureRoute` and `setRouteDestination`.

Suggested implementation:
1. Make the source tabs/macro knobs `juce::DragAndDropContainer` sources: the editor (`ZygEditor`) inherits `juce::DragAndDropContainer`; a source starts a drag after a few pixels of movement with a small pixel "chip" image (source name) as the drag image.
2. Make `Knob` (and `HSlider`) `juce::DragAndDropTarget`: `isInterestedInDragSource` only when the knob has a target; `itemDragEnter/Exit` fade a highlight ring (use `Fade`); `itemDropped` calls `ctx.editNow` to add a `ModulationRoute` (reuse an existing route with the same source+target instead of duplicating; default amount 50, bipolar per `modSourceIsBipolar`).
3. Show it: after drop, the knob's mod arc appears (already automatic), the source tab badge count increments, and a toast reports "LFO 1 -> FILTER 1 CUTOFF". While dragging, highlight all valid targets on the current page.
4. Amount editing on the target: drag the mod arc handle (add a small hit-zone to `Knob`) or use a popup listing that target's routes with sliders; keep the MATRIX page as the precise editor.
5. Cover FX knobs (`ModTarget::fxParam`, index = position in `Patch::fx`), routing knobs and macros too; remember `removeFxModule` already re-points fx routes.
6. Test: unit-test the route-creation helper; verify in REAPER that LFO 1 -> filter cutoff audibly modulates.

## Housekeeping
- `ui-design-kit/` PNG mock-ups are unused at run time (only `assets/ui/logo_46.png` is embedded); `generate_placeholders.py` can be ignored or removed.
- Keep `DESIGN.md`, `HANDOFF.md` and `docs/TESTING.md` accurate when behaviour changes.

## Status update 2026-09-27
- Legibility/proportion pass, 3D wavetable mesh, spectrum analyzer and OpenGL renderer landed; see `DESIGN.md` bullets 1-5 and the top of `HANDOFF.md`. When adding UI: use the regular face for anything a user reads, `drawSmall` only for secondary marks; keep OSC-page modules on the shared `kRow*/footY/knobRow` rhythm.
