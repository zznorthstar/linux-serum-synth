# Handoff — 2026-09-21

## What exists

- Initial Git repository and source/docs are in place. Supplied executables, screenshot, presets and extracted factory content are Git-ignored.
- The complete 2.1.5 NSIS installer was listed and selected content roots extracted with `7z` to `.local-serum-content/` without running Windows code. It contains 7,026 indexed files / 1,392,485,649 uncompressed bytes. `7z` could not open the separate updater as an archive; it was not executed.
- [Official manual](https://www.xferrecords.com/manual/serum-2/docs) and [What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf) were reviewed. The available manual describes 2.0.18, while the supplied installer is 2.1.5. The gap needs fixture-based verification.
- `zygzxg/serum.py` decodes/encodes the XferJson/Zstandard/CBOR container with size checks. No Serum DSP/Windows code is used. `zygzxg/assets.py` indexes private local content and resolves exact category-relative references. `zygzxg/corpus.py` aggregates observed parameter paths and enums.
- All 626 factory `.SerumPreset` files decoded, with 515 distinct explicit parameter paths observed. The corpus contains 83 voice-filter IDs and all 13 effect plus three splitter classes. The supplied `SO_IS_bass_press.SerumPreset` decodes and all four external wavetable/noise references resolve against the local index.
- Four focused unit tests pass for unknown-field round-trip preservation, malformed-frame rejection, asset resolution and multisample child references.

## What does not exist

No independent patch model, parameter/default/range map, semantic importer, audio engine, GUI, Linux plugin, native preset save/reload, host validation or compatibility render exists. Do not interpret “626 presets decode” as “626 presets load into a synth.” The current CLI reports semantic coverage as **unmeasured**. The legacy `.fxp` is identified as a VST chunk (`CcnK`/`FPCh`) but not parsed.

## Immediate next engineering work

1. Complete the semantic registry from the official manual plus isolated 2.1.5 fixtures. In particular, enumerate 49 modulation source IDs/sub-IDs, defaults/ranges/scales, serialized filter ID to user-visible type/`Var` meaning, FX splitter nesting, sample/clip/arp event structures and embedded media. Update the compatibility matrix and add synthetic fixtures before starting UI work.
2. Define a versioned, typed ZYG patch model independent of Serum serialization. Include routing/modulation graphs, mode-specific oscillator data, clip/arp sequences, FX racks/splits, asset references, provenance and an opaque unknown-data sidecar.
3. Build a lossless Serum-to-ZYG semantic adapter that reports decoded/recognized/mapped/unknown/missing separately. Use the factory corpus and supplied user preset as private integration fixtures.
4. Establish a native C++ plugin/audio core with VST3 first; add CLAP if feasible. Verify basic MIDI, state, tempo and REAPER load before expanding DSP. Implement one tested subsystem at a time, updating `docs/DSP_DECISIONS.md` and coverage status.

## Commands

```sh
PYTHONPATH=. .venv/bin/python -m unittest discover -s tests -v
PYTHONPATH=. .venv/bin/python -m zygzxg.cli index-assets .local-serum-content --output .local-serum-content/zygzxg-index.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli scan-corpus .local-serum-content/Presets --output docs/SERUM2_CORPUS_SCHEMA.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli inspect-preset SO_IS_bass_press.SerumPreset --asset-index .local-serum-content/zygzxg-index.json
```

The `.venv` currently has `cbor2` and `zstandard`; recreate it with `pip install -e .` as needed. `reaper`, `cmake`, `ninja`, `g++`, `wine` and `7z` are available on this machine, but no host/plugin test has been run.
