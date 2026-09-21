# ZYG-ZXG agent instructions

ZYG-ZXG is a native Linux synthesizer for personal use. Serum 2 presets are an interoperability input, not the architecture or sound target. Do not claim semantic compatibility from successful CBOR decoding or from making sound with only part of a preset.

Read `HANDOFF.md` and `docs/SERUM2_COMPATIBILITY_MATRIX.md` before changing the synth architecture. Keep the matrix and handoff accurate when features land. `docs/SERUM2_CORPUS_SCHEMA.json` is generated from the local 2.1.5 factory corpus and is an observed vocabulary, not the full feature registry.

The user supplied a full 2.1.5 NSIS installer, an updater EXE, one `.SerumPreset`, one legacy `.fxp`, and a screenshot. There is **no assumed DLL**. The Windows executables are reference material only. Do not make the final instrument depend on them or on Wine. The locally extracted factory content is in `.local-serum-content/` and is Git-ignored. Do not commit, redistribute, embed, or package Xfer assets or user presets. User-owned local content can be indexed at run time.

Keep four layers separate: Serum container decoder → versioned Serum semantic adapter → independent ZYG patch model → native DSP/GUI/plugin. Preserve unknown state exactly in an opaque provenance sidecar, report it, and add a semantic mapping when understood. Never drop a known modulation route, routing edge, clip, arp event, asset reference, or FX instance silently. Missing assets and unsupported semantics are different failures.

Audio processing must avoid allocations, locks, logging, disk/network access and dynamic initialization. Use sample-accurate event timing where supported by the host, stable voice allocation and deterministic state restore. Cross-thread changes need safe handoff. Research important DSP choices and record them in `docs/DSP_DECISIONS.md`.

Use the official Serum 2 manual and What's New document as primary sources. Verify 2.1.5 differences against controlled presets or a legal black-box oracle when needed. `serum2gen` is MIT licensed and may be studied; `serum-preset-packager` has no detected license file and is reference knowledge only. Record any copied code and license. Do not copy proprietary Xfer implementation code.

Before claiming a feature complete, add meaningful parser/mapping/render/host tests as appropriate, update coverage, and document limitations. REAPER on Linux is the main host target; VST3 is required and CLAP is desirable. Do not equate a standalone test with host validation.
