# ZYG-ZXG

ZYG-ZXG is the working name for a Linux-native software synthesizer that will interpret Serum 2 preset semantics through original DSP. This repository is at **research/tooling stage**. It does not yet contain a synthesizer, VST3/CLAP plugin, GUI, or playable Serum importer.

The first deliverables are a [compatibility matrix](docs/SERUM2_COMPATIBILITY_MATRIX.md), [factory-corpus schema](docs/SERUM2_CORPUS_SCHEMA.json), a bounded `.SerumPreset` container decoder, and a private local-asset indexer. The parser reads XferJson/Zstandard/CBOR containers and preserves unknown fields. The indexer points at content the user obtained; it does not bundle or copy Xfer assets.

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

See [HANDOFF.md](HANDOFF.md) for exact status, [AGENTS.md](AGENTS.md) for persistent engineering rules, and [docs/TESTING.md](docs/TESTING.md) for verification and future acceptance gates.
