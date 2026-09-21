# Contributing

ZYG-ZXG is GPL-3.0 open source. Contributions to Serum semantic mappings, original DSP, host support, tests and documentation are welcome. Keep priorities in order: compatibility, sound quality, user experience.

Do not submit Xfer executables, installers, presets, factory wavetables, samples, skins or other proprietary resources. Small synthetic fixtures made without proprietary assets are preferred. If a private preset exposes a problem, describe structural fields and expected behavior without redistributing it. Code derived from third-party projects needs a compatible license and an entry in `docs/THIRD_PARTY.md`.

The architecture is `SerumImporter` → independent `Patch` → `SynthEngine` → JUCE host adapter. Unknown Serum state is preserved in a provenance sidecar; that does not mean it is implemented. When implementing a missing module, add tests, update `compatibility/serum2_support.json` and `docs/SERUM2_COMPATIBILITY_MATRIX.md`, and document substantial DSP choices in `docs/DSP_DECISIONS.md`.

Run `ctest --test-dir build --output-on-failure` and the Python tests (`PYTHONPATH=. .venv/bin/python -m unittest discover -s tests -v`). Where possible, test realtime and offline renders in REAPER and report host/version/sample-rate details. Never post a compatibility percentage without a defined denominator and supporting tests.
