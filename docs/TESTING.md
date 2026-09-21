# Testing and compatibility evidence

Current research-tool tests:

```sh
PYTHONPATH=. .venv/bin/python -m unittest discover -s tests -v
PYTHONPATH=. .venv/bin/python -m zygzxg.cli scan-corpus .local-serum-content/Presets --output docs/SERUM2_CORPUS_SCHEMA.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli inspect-preset SO_IS_bass_press.SerumPreset --asset-index .local-serum-content/zygzxg-index.json
```

The first command tests container round-trip preservation, malformed-frame rejection, exact/missing asset resolution and multisample child references. The second verified 626/626 factory presets decode and observed 515 explicit parameter paths from the supplied 2.1.5 installer. The third verifies the supplied user preset decodes and its four external wavetable/noise references resolve. All of these are **structural** tests, not sound or semantic-compatibility tests.

Future test layers, in dependency order:

1. Isolated Serum fixtures for every mode, parameter ID, enum, route, modulation source/aux/curve, filter type, FX module/split, arp/clip event, asset mode and default value. Keep user-owned fixtures private or replace with legitimate synthetic fixtures; never commit Xfer factory assets.
2. Import adapter tests: exact counts of recognized/unknown fields; every recognized field mapped into typed ZYG concepts; unknown sidecar survives native save/reload; missing assets reported separately.
3. Patch-model graph tests: routing/splitter topology, modulation cycles/ordering, voice behavior, clip/arp timing, MIDI/MPE, state and automation serialization.
4. DSP tests: silence/finite output, pitch, envelope timing, spectral behavior, alias/stability sweeps, sample-rate/buffer-size invariance, voice stealing, no allocations on the audio thread and bounded worst-case CPU.
5. Plugin tests in Linux REAPER for VST3 and separately CLAP if delivered: discovery, load, MIDI, preset import, state recall/project reload, automation, host tempo/transport, sample-rate/buffer changes, realtime/offline render and crash resistance.
6. Optional black-box oracle tests using legally installed Serum in isolated Wine/Windows: compare architectural behavior such as note/arp timing, modulation polarity, routing presence and envelope triggering. Waveform identity is not the acceptance metric.

Compatibility reports should show `decoded`, `recognized`, `mapped`, `rendered`, `unknown`, `missing assets`, and `unsupported` separately. “100% semantic coverage” is allowed only when known state has a behaviorally tested destination; opaque preservation alone does not qualify.
