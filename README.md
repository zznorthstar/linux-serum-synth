# ZYG-ZXG

ZYG-ZXG / Linux Serum Synth is an independent, GPL-3.0, Linux-native synthesizer being built to interpret Serum 2 presets with original DSP. It is **not affiliated with Xfer Records**. This is an **early engineering build**, not a production-compatible instrument. A native VST3, independent patch model, Serum importer and first wavetable audio path now have a REAPER offline-render smoke test; CLAP is not yet available. No release binary is available yet.

The supplied Serum 2.1.2 user preset decodes and its wavetable path renders in a REAPER project. Its modulation and FX do **not** yet render. The 626 supplied factory presets were authored in Serum 2.0.11–2.0.15; decoding them is not a claim that they play correctly. See [the support dashboard](compatibility/serum2_support.json) and [compatibility matrix](docs/SERUM2_COMPATIBILITY_MATRIX.md).

The first deliverables include a [factory-corpus schema](docs/SERUM2_CORPUS_SCHEMA.json), bounded `.SerumPreset` decoder, local-asset indexer, and native C++ vertical slice. Unknown fields are retained, not silently declared compatible. The indexer points at content the user obtained; it does not bundle or copy Xfer assets.

## Build the experimental VST3

On x86_64 Linux, install CMake 3.24+, Ninja, a C++20 compiler, ALSA/X11/FreeType/OpenGL development packages and `libzstd`. Configuration fetches pinned JUCE 8.0.9 and nlohmann/json 3.11.3 from upstream. No Xfer content is downloaded or bundled.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The build output is `build/ZYGZXG_artefacts/Release/VST3/ZYG-ZXG.vst3`. Copy the **whole bundle directory** to `~/.vst3/` and rescan VST3 plugins in REAPER. The initial editor lets you select the root of your own legally obtained Serum content (`Tables`, `Samples`, etc.) and load a `.SerumPreset`. The path and original preset bytes are saved in host state. Current wavetable support accepts a subset of local WAVE files; unsupported state is reported. Do not use this build in irreplaceable projects.

## Research tools

Python 3.11+ is required for the CLI:

```sh
python3 -m venv .venv
.venv/bin/pip install -e .
.venv/bin/zygzxg index-assets /path/to/extracted-or-installed/Serum-content --output /path/to/private/index.json
.venv/bin/zygzxg inspect-preset /path/to/preset.SerumPreset --asset-index /path/to/private/index.json
.venv/bin/zygzxg resolve-asset /path/to/private/index.json wavetable 'S2 Tables/Default Shapes.wav'
```

If running without installing the script entry point, use `PYTHONPATH=. .venv/bin/python -m zygzxg.cli ...`. The content root should directly contain directories such as `Tables`, `Samples`, `Multisamples`, `Presets`, and `Impulses`. The index stores that root's absolute path and should stay private on the user's machine.

The supplied 2.1.5 full installer was safely extracted with `7z` into the ignored `.local-serum-content/`; the updater has not been executed. Windows software is not a runtime dependency. The supplied `.fxp` is a legacy VST chunk preset and is not yet parsed.

See [HANDOFF.md](HANDOFF.md) for exact status, [AGENTS.md](AGENTS.md) for persistent engineering rules, [docs/TESTING.md](docs/TESTING.md) for verification, and [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidance.
