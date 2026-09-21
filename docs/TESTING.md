# Testing and compatibility evidence

Current research-tool tests:

```sh
PYTHONPATH=. .venv/bin/python -m unittest discover -s tests -v
PYTHONPATH=. .venv/bin/python -m zygzxg.cli scan-corpus .local-serum-content/Presets --output docs/SERUM2_CORPUS_SCHEMA.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli inspect-preset SO_IS_bass_press.SerumPreset --asset-index .local-serum-content/zygzxg-index.json
PYTHONPATH=. .venv/bin/python -m zygzxg.cli audit-assets .local-serum-content/Presets .local-serum-content/zygzxg-index.json
```

The first command tests container round-trip preservation, malformed-frame rejection, exact/missing asset resolution and multisample child references. The second verified 626/626 factory presets decode and observed 515 explicit parameter paths from the supplied 2.1.5 installer. An additional 626/626 encode/decode equality check passed for the decoded CBOR documents. The third verifies the supplied user preset decodes and its four external wavetable/noise references resolve. The fourth resolved all 12,013 required external references across those factory presets. All of those are **structural** tests, not sound or semantic-compatibility tests.

Current native-engine tests:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
ZYG_TEST_PRESET=SO_IS_bass_press.SerumPreset ZYG_TEST_ASSET_ROOT=.local-serum-content build/zygzxg_core_tests
```

The last private-fixture run imports the authored-2.1.2 preset into `Patch`, loads its user-owned wavetable and renders finite non-silent audio. It reports `47/83` explicit fields typed, with 123 diagnostic entries for unmapped fields, mapped-but-not-rendered fields and unsupported modules/routes. Those numbers are **not semantic coverage**; diagnostic categories may overlap. The test asserts every explicit field is either typed or individually flagged as unmapped.

REAPER host smoke test, 2026-09-21: the built bundle was copied to `~/.vst3/`, REAPER discovered it as `VST3i: ZYG-ZXG (ZYG-ZXG)`, and `tests/reaper_smoke.lua` created a 2-second MIDI project. The developer-only `ZYGZXG_HOST_SMOKE_PRESET` and `ZYGZXG_HOST_SMOKE_ASSETS` environment variables loaded the private real preset without putting it into Git. The project was saved, REAPER exited, then `reaper -newinst -renderproject` reopened it **without those variables**, testing host-state restore. The saved REAPER plugin state contains the Serum 2.1.2 preset bytes and local asset-root path. The offline 48 kHz stereo WAV was non-silent (mean −34.1 dBFS, peak −28.2 dBFS). Test artifacts stayed under `/tmp/zygzxg-host-final-zM4kqU` and are not part of the repository. This proves a first native plugin pipeline; it does not prove modulation, FX, full routing, realtime stability, GUI workflows, or 2.1.4 compatibility.

Future test layers, in dependency order:

1. Isolated Serum fixtures for every mode, parameter ID, enum, route, modulation source/aux/curve, filter type, FX module/split, arp/clip event, asset mode and default value. Keep user-owned fixtures private or replace with legitimate synthetic fixtures; never commit Xfer factory assets.
2. Import adapter tests: exact counts of recognized/unknown fields; every recognized field mapped into typed ZYG concepts; unknown sidecar survives native save/reload; missing assets reported separately.
3. Patch-model graph tests: routing/splitter topology, modulation cycles/ordering, voice behavior, clip/arp timing, MIDI/MPE, state and automation serialization.
4. DSP tests: silence/finite output, pitch, envelope timing, spectral behavior, alias/stability sweeps, sample-rate/buffer-size invariance, voice stealing, no allocations on the audio thread and bounded worst-case CPU.
5. Plugin tests in Linux REAPER for VST3 and separately CLAP if delivered: discovery, load, MIDI, preset import, state recall/project reload, automation, host tempo/transport, sample-rate/buffer changes, realtime/offline render and crash resistance.
6. Optional black-box oracle tests using legally installed Serum in isolated Wine/Windows: compare architectural behavior such as note/arp timing, modulation polarity, routing presence and envelope triggering. Waveform identity is not the acceptance metric.

Compatibility reports should show `decoded`, `recognized`, `mapped`, `rendered`, `unknown`, `missing assets`, and `unsupported` separately. “100% semantic coverage” is allowed only when known state has a behaviorally tested destination; opaque preservation alone does not qualify.
