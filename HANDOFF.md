# Handoff — 2026-09-21

## What exists

- Initial Git repository and source/docs are in place. Supplied executables, screenshot, presets and extracted factory content are Git-ignored.
- The complete 2.1.5 NSIS installer was listed and all recognized content roots extracted with `7z` to `.local-serum-content/` without running Windows code. It contains 7,357 indexed files / 1,392,825,417 uncompressed bytes. `7z` could not open the separate updater as an archive; it was not executed.
- [Official manual](https://www.xferrecords.com/manual/serum-2/docs) and [What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf) were reviewed. The available manual describes 2.0.18, while the supplied installer is 2.1.5. Its 626 factory presets were authored as 2.0.11–2.0.15; the supplied user preset is 2.1.2. Later feature gaps need fixture-based verification.
- `zygzxg/serum.py` decodes/encodes the XferJson/Zstandard/CBOR container with size checks. No Serum DSP/Windows code is used. `zygzxg/assets.py` indexes private local content and resolves exact category-relative references. `zygzxg/corpus.py` aggregates observed parameter paths and enums.
- All 626 factory `.SerumPreset` files decoded and re-encoded to semantically identical CBOR state, with 515 explicit parameter paths, 799 structural paths, 49 main modulation source IDs, 83 voice-filter IDs and all 13 effect plus three splitter classes observed. All 12,013 required external references resolve against the local content index. The supplied `SO_IS_bass_press.SerumPreset` decodes and all four external wavetable/noise references resolve.
- Six focused unit tests pass for unknown-field round-trip preservation, malformed-frame rejection, asset resolution, virtual-root path handling and multisample child references.

## First native plugin checkpoint

- `CMakeLists.txt` pins JUCE 8.0.9 and nlohmann/json 3.11.3 and links system libzstd. `src/SerumImporter.cpp` natively decodes Serum containers and maps a first subset into the Serum-independent `Patch` in `src/Patch.h`; the original CBOR tree and original preset bytes remain as provenance. Explicit typed fields are counted; every remaining explicit parameter gets an `unmapped_parameter` diagnostic. The source-ID name map includes known IDs, with unresolved IDs labelled, not guessed.
- The `Patch` has oscillator mode/asset shells, filter/routing/envelope fields, ordered modulation routes, LFO/macro and arp/MIDI clip state, FX rack module identities, global/voice fields and diagnostics. This is **partial semantic mapping**, not complete behavior. Current native DSP renders local PCM/float RIFF WAVE wavetables for A/B/C, basic pitch/unison/Env 1 and one low-pass fallback. It does not render matrix routes, warp, other oscillator modes, SUB/NOISE, most routing, distinct filter types, FX, arp or clips.
- The Linux VST3 bundle builds at `build/ZYGZXG_artefacts/Release/VST3/ZYG-ZXG.vst3`. It has a minimal editor for selecting a legally obtained Serum content root, loading `.SerumPreset`, and copying compatibility diagnostics. The three-slot patch handoff avoids audio-thread file I/O/locks/allocations; host state stores original preset bytes plus asset path and re-imports on restore. CLAP, native ZYG preset format and production UI are absent.
- `ctest` passes. The native core test imports and renders `SO_IS_bass_press.SerumPreset` (embedded productVersion **2.1.2**), with 47/83 explicit fields typed and 123 diagnostic entries across unmapped/mapped-but-not-rendered/unsupported state. Every explicit field is either typed or individually flagged as unmapped. This fixture does not prove 2.1.4 compatibility.
- REAPER on this machine discovered the renamed **ZYG-ZXG** VST3, inserted it as an instrument, saved a project with a MIDI note and that real preset (loaded via developer-only environment hook), then reopened it without environment variables and produced a 2-second, 48 kHz stereo offline render (mean −34.1 dBFS, peak −28.2 dBFS). Evidence is in `docs/TESTING.md`; private render/project artifacts are under `/tmp/zygzxg-host-final-zM4kqU`.
- `compatibility/serum2_support.json` is the machine-readable dashboard. It intentionally marks most implementation categories none/partial and full semantic coverage unknown.

Do not interpret “626 presets decode” as “626 presets load into a compatible synth,” or this one REAPER render as full preset compatibility. The legacy `.fxp` remains unparsed.

## Immediate next engineering work

1. Strengthen the fixture-backed semantic map, particularly verified defaults/ranges/scales, unresolved source IDs `39–44/47–48`, modulation destination semantics, oscillator A/B/C enable defaults, and FX splitter nesting. Keep unresolved state explicit; do not wait for theoretical completion before rendering more features.
2. Add actual modulation DSP early (Env/LFO/macro/velocity → typed destinations), then dual-filter/routing behavior. The current imported user preset has 15 active routes that do not render, making it a strong next regression target.
3. Replace temporary wavetable interpolation/low-pass fallback with tested bandlimited tables and distinct filter implementations. Expand sample/multisample/granular/spectral/SUB/NOISE and asset resolution in vertical slices using private real presets plus synthetic fixtures.
4. Verify GUI-based preset loading, REAPER realtime playback, 44.1/96 kHz and buffer-size changes, project reload, automation and failure diagnostics. Build CLAP as a separate format and test independently. Only then consider binary releases.

## Commands

```sh
PYTHONPATH=. .venv/bin/python -m unittest discover -s tests -v
PYTHONPATH=. .venv/bin/python -m zygzxg.cli index-assets .local-serum-content --output .local-serum-content/zygzxg-index.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli scan-corpus .local-serum-content/Presets --output docs/SERUM2_CORPUS_SCHEMA.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli inspect-preset SO_IS_bass_press.SerumPreset --asset-index .local-serum-content/zygzxg-index.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli audit-assets .local-serum-content/Presets .local-serum-content/zygzxg-index.json
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ZYG_TEST_PRESET=SO_IS_bass_press.SerumPreset ZYG_TEST_ASSET_ROOT=.local-serum-content build/zygzxg_core_tests
```

The `.venv` currently has `cbor2` and `zstandard`; recreate it with `pip install -e .` as needed. `reaper`, `cmake`, `ninja`, `g++`, `wine` and `7z` are available on this machine, but no host/plugin test has been run.
