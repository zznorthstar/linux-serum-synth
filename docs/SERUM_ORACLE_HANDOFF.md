# Serum 2 oracle handoff (for the Windows session with real Serum 2)

Written 2026-09-27. Read `AGENTS.md`, `HANDOFF.md`, `docs/DSP_DECISIONS.md` and `docs/SERUM2_COMPATIBILITY_MATRIX.md` first.

## The reported problem

The user played the imported presets in ZYG-ZXG (REAPER, Linux, VST3) and compared them with the same presets in real Serum 2: **they sound nothing alike**. The expectation is not bit-identical DSP (ZYG is original DSP), but **similar behaviour and the same character**: a reese should be a reese, a growl a growl, envelopes/LFO movement and levels in the same ballpark. Today they come across as completely different sounds.

This Linux session did **not** investigate the cause (by request). Nothing below is a diagnosis; it is a map of where the unverified assumptions are, so the oracle session can measure instead of guess.

## What exists to compare against

- 11 user presets in the repo root (`*.SerumPreset`, git-ignored): `BS_NCS3_bass_growl_gordon`, `DS_AC2_bass_reese_trend`, `DS_GTH3_bass_poison_bridge`, `DS_S2OT_bass_808_perfect`, `DS_TD2_bass_elephant`, `MO_HA_BS_Amped`, `SO_IS_bass_press`, `TSP_OVERVIEW_YAANO_Synth_stutter_candy`, `TSP_S2DG_Bass_uht_wob`, `VOX_MDTH2_Bass_sugar_org`, `VOX_MDTH_bass_synth_tk_live`. All import with no missing assets and render finite audio offline; the importer reports them as essentially fully typed. **Typed is not the same as sounding right.**
- The 626 factory presets in `.local-serum-content/Presets` (private, git-ignored).
- `zygzxg_render preset.SerumPreset <content root> --note 48 --seconds 3 --hold 2 --out x.wav [--nofx --nomod --nowarp --nofilter --osc N --fxoff K --info --profile]` renders one note through the native engine offline (build with `-DZYG_BUILD_TOOLS=ON`). The switches isolate stages, which is the fastest way to find the stage that diverges.

## Suggested method

1. For each preset, bounce the same MIDI note (48 and 60, velocity 127, 2 s hold, 1 s release, 48 kHz) from Serum 2 and from ZYG (`zygzxg_render`, or the plug-in in a DAW). Level-match nothing; the level difference is itself data.
2. Bisect by stage on both sides: in Serum, disable FX / matrix / warps / filter and bounce again; in ZYG use `--nofx --nomod --nowarp --nofilter --osc N`. The first stage where the two diverge is the one to fix.
3. Prefer **controlled single-feature presets** saved from Serum (one oscillator, one warp at 25/50/75/100 %, one filter type at a few cutoff/res values, one envelope, one LFO route with a known amount, one FX at default) over full presets. Commit only their *measurements* and derived constants, never Xfer assets (see `AGENTS.md`).
4. Record every measured transfer function in `docs/DSP_DECISIONS.md`, update `compatibility/serum2_support.json`, and add a regression test that pins the measured curve.

## Where the unverified assumptions are (most likely to change the character first)

Taken from `docs/DSP_DECISIONS.md` ("Unverified assumptions") and the code; none of these has been checked against Serum.

- **Parameter scaling / ranges.** Every modulatable parameter maps a normalized value through ZYG range tables (`oscParamRange`, `filterParamRange`, FX tables in `src/FxParams.cpp`). If Serum's stored value means something else (e.g. cutoff in Hz vs normalized, detune in cents vs 0..1, warp amount curves), the preset loads "fully" but every knob sits somewhere else.
- **Modulation amounts.** A route of amount 100 moves the destination by its full ZYG knob travel (`SynthEngine.cpp` `applyRoute`); Serum's amount scale, curve (`curveOut`), bipolar handling and aux-source scaling are unverified. Heavily modulated bass presets (all 11 are bass/synth) will diverge most here.
- **Oscillator levels, unison and gain staging.** Unison detune range default (2 semitones), detune modes, blend, stack, stereo spread, and output gain per voice/unison count; the safety limiter (0.8 → 1.0 ceiling) is on by default.
- **Warps.** Bend/asym/PWM/sync/FM/PM/RM/AM transfer functions are ZYG designs (e.g. `kPD_OSC` phase distortion in `BS_NCS3_bass_growl_gordon`, FM (B) in `DS_AC2_bass_reese_trend`). Warp depth curves and FM index scaling are guesses.
- **Filters.** ~80 Serum filter IDs map to native families; cutoff/resonance/drive curves, key tracking and the MG ladder/formant/comb characters are original. Filter routing balance is −100..100 by ZYG's rule.
- **Envelopes and LFOs.** Curve bend (k = 4.4·b), tempo-synced envelope times (`Env BeatSync` is read as seconds — flagged in the import notes of `DS_GTH3_bass_poison_bridge`), LFO rate/tempo-sync semantics, rise/delay/smooth.
- **FX.** "All FX defaults, ranges, levels and curves are provisional ZYG choices" (distortion modes, OTT-style multiband comp, reverb, hyperD, bode…). Bass presets often end in distortion + multiband comp, so this chain dominates the character.
- **Embedded tables.** `embeddedWTData` frames are used as-is; the neighbouring `numFrames` field is often larger than the data (e.g. 4096 floats vs `numFrames` 18432) and its meaning is unknown — Serum may interpolate/expand these tables.
- **Voice behaviour.** Velocity only affects amplitude through routes (`velocityAmpDepth` default 0); `kParamVoiceStealRestart` semantics inferred from its name; `kParamS1Compatibility` ignored.

## Out of scope for the oracle session

UI work, the Linux host tests and packaging. Keep the four-layer split (decoder → adapter → independent `Patch` → DSP): fixes belong in the semantic adapter (`src/SerumImporter.cpp`) or in native DSP, never as Serum strings in the engine.
