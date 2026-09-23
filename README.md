# ZYG-ZXG

ZYG-ZXG / Linux Serum Synth is an independent, GPL-3.0, Linux-native synthesizer being built to interpret Serum 2 presets with original DSP. It is **not affiliated with Xfer Records**. This is an **early engineering build**, not a production-compatible instrument. Native VST3i and CLAP instruments build and have separate REAPER realtime MIDI/transport tests. There is no public binary release yet.

The supplied Serum 2.1.2 user preset decodes and its wavetable path renders in a REAPER project. Its modulation and FX do **not** yet render. A second user preset authored in 2.0.24 has a detailed partial [verification report](docs/preset_verification/TSP_OVERVIEW_YAANO_Synth_stutter_candy.md). The 626 supplied factory presets were authored in Serum 2.0.11–2.0.15; decoding them is not a claim that they play correctly. See [the support dashboard](compatibility/serum2_support.json) and [compatibility matrix](docs/SERUM2_COMPATIBILITY_MATRIX.md).

The first deliverables include a [factory-corpus schema](docs/SERUM2_CORPUS_SCHEMA.json), bounded `.SerumPreset` decoder, local-asset indexer, and native C++ vertical slice. Unknown fields are retained, not silently declared compatible. The indexer points at content the user obtained; it does not bundle or copy Xfer assets.

## Install on Linux

When an alpha release archive is available, extract it and copy the entire `ZYG-ZXG.vst3` bundle to `~/.vst3/` and `ZYG-ZXG.clap` to `~/.clap/`. Restart or rescan REAPER, insert **VST3i: ZYG-ZXG** or **CLAP: ZYG-ZXG** on an instrument track, arm it and enable monitoring. Play MIDI notes. The editor shows MIDI activity, active voices and output level; **Audition C3** can test the audio path while the track is processed. [Step-by-step install guide](docs/INSTALL_LINUX.md).

Click **INIT** to start from a built-in sine, choose OSC A/B/C, browse a mono WAV wavetable, edit oscillator phase/random phase, SUB/NOISE, filter, envelope, macros and native sine LFO matrix routes, then **Save ZYG**. Use **Open ZYG** to reload a `.zygpreset`. For Serum import, click **Content** and choose a legally owned local Serum content root containing `Tables/` and `Samples/`, then **Load Serum**. The FX tab lists imported effect instances and labels them as DSP pending. No Xfer assets ship with the plugin. The editor uses the repository's `ui-design-kit/` logo, sprite controls, palette, and fixed 1000x600 layout.

Tested preset containers: authored Serum **2.0.11–2.0.15** factory corpus (structural decode only), **2.0.24** and **2.1.2** supplied user presets (partial native mapping/render). Full semantic compatibility is unmeasured.

## Build the experimental VST3 and CLAP

On x86_64 Linux, install CMake 3.24+, Ninja, a C++20 compiler, ALSA/X11/FreeType/OpenGL development packages and `libzstd`. Configuration fetches pinned JUCE 8.0.9 and nlohmann/json 3.11.3 from upstream. No Xfer content is downloaded or bundled.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The build outputs are `build/ZYGZXG_artefacts/Release/VST3/ZYG-ZXG.vst3` and `build/ZYGZXG_artefacts/Release/CLAP/ZYG-ZXG.clap`. `ZYG_BUILD_CLAP=OFF` skips the optional upstream CLAP extension. A GitHub Actions workflow builds, tests and packages both formats; it attaches artifacts only to explicitly published prereleases. No stable release is claimed.

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

The supplied 2.1.5 full installer was safely extracted with `7z` into the ignored `.local-serum-content/`; the updater has not been executed. Windows software is not a runtime dependency. See the [local installation inventory](docs/SERUM2_INSTALLATION_INVENTORY.md). The supplied `.fxp` is a legacy VST chunk preset and is not yet parsed.

See [HANDOFF.md](HANDOFF.md) for exact status, [AGENTS.md](AGENTS.md) for persistent engineering rules, [docs/TESTING.md](docs/TESTING.md) for verification, and [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidance.
