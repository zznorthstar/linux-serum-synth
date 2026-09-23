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

The last private-fixture run imports the authored-2.1.2 preset into `Patch`, loads its user-owned wavetable and renders finite non-silent audio. It reports `52/83` explicit fields typed, with 121 diagnostic entries for unmapped fields, mapped-but-not-rendered fields and unsupported modules/routes. Those numbers are **not semantic coverage**; diagnostic categories may overlap. The test asserts every explicit field is either typed or individually flagged as unmapped.

REAPER host smoke test, 2026-09-21: the built bundle was copied to `~/.vst3/`, REAPER discovered it as `VST3i: ZYG-ZXG (ZYG-ZXG)`, and `tests/reaper_smoke.lua` created a 2-second MIDI project. The developer-only `ZYGZXG_HOST_SMOKE_PRESET` and `ZYGZXG_HOST_SMOKE_ASSETS` environment variables loaded the private real preset without putting it into Git. The project was saved, REAPER exited, then `reaper -newinst -renderproject` reopened it **without those variables**, testing host-state restore. The saved REAPER plugin state contains the Serum 2.1.2 preset bytes and local asset-root path. The offline 48 kHz stereo WAV was non-silent (mean −34.1 dBFS, peak −28.2 dBFS). Test artifacts stayed under `/tmp/zygzxg-host-final-zM4kqU` and are not part of the repository. This proves a first native plugin pipeline; it does not prove modulation, FX, full routing, realtime stability, GUI workflows, or 2.1.4 compatibility.

REAPER **realtime** transport test, 2026-09-21: `tests/reaper_realtime.lua` is run with `reaper -nonewinst tests/reaper_realtime.lua` against the developer's own already-running interactive REAPER session (real PulseAudio/PipeWire device, 48 kHz, 128-sample buffer — confirmed from the REAPER window's own status bar, not a dummy/offline device). It creates a disposable project tab, inserts a track with `VST3: ZYG-ZXG` and a 1.6 s MIDI note, presses the real transport play button, polls `Track_GetPeakInfo` on the track and master for 3 seconds, then stops and closes its own tab. Two independent runs produced non-zero, consistent peaks during actual transport playback, e.g. `play_polls=63 max_position=1.901333 track_peak=0.020700455 master_peak=0.126993060`. This is evidence distinct from the offline render above: it exercises the real-time audio callback path through an actual live audio device with the transport actually running, not an offline bounce. It does not prove stability under different sample rates/buffer sizes, project reload, automation, or longer sessions — those remain untested.

Known environment quirk (this machine, REAPER v7.80 on a Wayland/XWayland/mutter desktop): `reaper -nonewinst <script>` does **not** reliably forward to the already-running instance. Observed behavior across many invocations: most of the time it runs the script against its own private, empty, throwaway project in a separate process (which still uses the same real configured audio device, so realtime evidence gathered that way is still valid, just not evidence of touching the pre-existing session); occasionally it does correctly forward into the real running instance's project list; and if its own startup (which always pays a slow, ~90–150 s LV2 plugin metadata scan) races badly, it can pop up as a **second, fully visible duplicate GUI window**, sometimes with a modal "REAPER Query" dialog. A future session driving REAPER this way must watch `ps aux | grep reaper` and the window list (`xwininfo -root -tree`, matching `_NET_WM_PID` via `xprop`) after every invocation and terminate (`kill -TERM`) any stray process that is not the known main instance PID — this is not destructive since these are the invoking script's own disposable throwaway state, never the user's real project. Do not attempt to drive REAPER via synthetic X11 input events (`XTestFakeKeyEvent`/`XTestFakeButtonEvent`) to work around this — that was explicitly denied by this environment's permission policy.

Future test layers, in dependency order:

1. Isolated Serum fixtures for every mode, parameter ID, enum, route, modulation source/aux/curve, filter type, FX module/split, arp/clip event, asset mode and default value. Keep user-owned fixtures private or replace with legitimate synthetic fixtures; never commit Xfer factory assets.
2. Import adapter tests: exact counts of recognized/unknown fields; every recognized field mapped into typed ZYG concepts; unknown sidecar survives native save/reload; missing assets reported separately.
3. Patch-model graph tests: routing/splitter topology, modulation cycles/ordering, voice behavior, clip/arp timing, MIDI/MPE, state and automation serialization.
4. DSP tests: silence/finite output, pitch, envelope timing, spectral behavior, alias/stability sweeps, sample-rate/buffer-size invariance, voice stealing, no allocations on the audio thread and bounded worst-case CPU.
5. Plugin tests in Linux REAPER for VST3 and separately CLAP if delivered: discovery, load, MIDI, preset import, state recall/project reload, automation, host tempo/transport, sample-rate/buffer changes, realtime/offline render and crash resistance.
6. Optional black-box oracle tests using legally installed Serum in isolated Wine/Windows: compare architectural behavior such as note/arp timing, modulation polarity, routing presence and envelope triggering. Waveform identity is not the acceptance metric.

Compatibility reports should show `decoded`, `recognized`, `mapped`, `rendered`, `unknown`, `missing assets`, and `unsupported` separately. “100% semantic coverage” is allowed only when known state has a behaviorally tested destination; opaque preservation alone does not qualify.

## 2026-09-22 independent host checks

The previously installed VST3 binary hash differed from the current build, so it could not show the newest editor. The VST3 was replaced using a rename of the old bundle, then a fresh `reaper -newinst tests/reaper_realtime.lua` process loaded the installed binary. The script waits five seconds for REAPER startup, creates a disposable tab, inserts `VST3: ZYG-ZXG`, arms and monitors the track, inserts a MIDI note and starts realtime transport. The report at `/tmp/zygzxg-realtime-VST3-report.txt` recorded `play_polls=63`, `track_peak=0.145221353`, `master_peak=0.184542865`. `pactl` showed an active sink input on the configured PipeWire output. The test process was terminated after closing its disposable tab; the pre-existing REAPER PID was not touched.

A separate installed CLAP test used `ZYG_HOST_FORMAT=CLAP reaper -newinst tests/reaper_realtime.lua`; `/tmp/zygzxg-realtime-CLAP-report.txt` recorded `play_polls=57`, `track_peak=0.041655660`, `master_peak=0.145615637`. `tests/reaper_gui.lua` opened the VST3 floating editor in another disposable instance; an X11 screenshot was inspected locally. These tests demonstrate host MIDI-to-output operation and editor instantiation. They do not constitute a human listening test, guarantee the user's current REAPER project is configured, or validate edited-state reload, automation, CLAP Serum import, sample-rate changes or long-term realtime safety.

After code changes, the core build and `ctest` passed; private fixture checks imported/rendered the 2.1.2 and 2.0.24 user presets. The 2.0.24 report is in `docs/preset_verification/`. For no-sound troubleshooting in a real session, check that REAPER has an active audio device, the track is armed/monitoring or transport is playing a MIDI item, MIDI indicator flashes, voice count rises, output meter moves, and track/master meters move. If the plugin meter moves but REAPER meters do not, inspect host routing/bypass. If REAPER meters move but speakers do not, inspect the PipeWire sink and physical output. The on-screen Audition C3 switch is an extra note source while REAPER processes the track.

The final 2026-09-22 build also passed a separate `tests/reaper_state.lua` INIT save/reopen test: VST3 reopened with one FX and returned `track_peak=0.012770534`, `master_peak=0.125669762`; CLAP reopened with one FX and returned `track_peak=0.014681339`, `master_peak=0.124138013`. These tests validate host-state recall for INIT, not a user-edited Serum patch. The current local release packaging command produced an 11 MB archive containing only VST3, CLAP, install instructions and license notices; the GitHub Actions workflow has not run on GitHub.

The supplied authored-2.0.24 user preset was then loaded in a fresh VST3 REAPER instance using the private fixture hook: realtime `track_peak=0.015031695`, `master_peak=0.123394452`. A temporary, uncommitted CBOR derivative muted OSC A (B/C were already at volume zero), leaving only the sample-backed NOISE source; it returned `track_peak=0.035572886`, `master_peak=0.035572886`. The core-only render for this derivative was silent because JUCE FLAC preparation happens in the plugin control layer. This isolates the new local FLAC NOISE playback path. A REAPER editor screenshot of the original preset showed its name, OSC A wavetable, five preserved FX instances, eight matrix rows, enabled NOISE and imported noise level. Neither test establishes subjective sound similarity or full Serum semantics.

Final output-sink check (2026-09-22): while the installed VST3 played an INIT C4 MIDI note in a separate REAPER instance, `parec --device=@DEFAULT_MONITOR@ --format=float32le --rate=48000 --channels=2` captured the configured PipeWire sink monitor. The first 12 one-second windows were exactly zero; windows 12 and 13 had RMS 0.0681 and 0.0769 with peaks 0.1294 and 0.1400. The strongest FFT bin was 261.47 Hz (neighboring bins surround C4 = 261.63 Hz). REAPER reported `play_polls=62`, `track_peak=0.061973810`, `master_peak=0.138054490` in the same run. This verifies plugin → REAPER → PipeWire sink signal on this machine; it does not verify speaker volume or the user's already-running REAPER process, which must restart to map the new plugin binary.

The final imported-preset editor check after macro mapping showed the 2.0.24 preset name, its OSC A wavetable, 33/75 typed explicit fields, eight matrix routes, Macro 1–4 values (0.716, 1.0, 0, 1.0), enabled NOISE at 0.068, five FX instances marked DSP pending, and an explicit imported-LFO unsupported label. A screenshot was inspected locally and is not committed because the preset/assets are private.

## Design-kit editor and current-binary host check

The user-provided `ui-design-kit/` was integrated after the checks above. CMake embeds its logo and 32/48/64 px 128-frame knob strips in VST3 and CLAP. The editor now uses the kit's 1000x600 dimensions, hard-edge palette and sprite controls while drawing waveform, filter, envelope, LFO and output displays from live patch/processor state. `ZYG_UI_SNAPSHOT=/tmp/name.png reaper -newinst tests/reaper_gui.lua` exercises an environment-gated UI-thread snapshot hook, useful because the Wayland compositor denies external screenshot APIs. The actual REAPER-hosted 1000x600 snapshot was inspected twice; the second pass corrected control/title collisions and a clipped audition label.

After rebuilding and SHA-256 matching the installed copies to the build outputs, fresh realtime tests of this exact binary returned:

- VST3: `play_polls=57`, `max_position=1.877333`, `track_peak=0.002899170`, `master_peak=0.115633450`.
- CLAP: `play_polls=56`, `max_position=1.877333`, `track_peak=0.002410889`, `master_peak=0.115552559`.

The core test now also checks oscillator phase randomization: 0% retriggers identically, while 100% changes the note attack. Current private fixture runs report 2.1.2 at `52/83` typed with finite peak `-0.0542683`, and 2.0.24 at `33/75` typed with finite peak `-0.206158`. The large remaining diagnostic counts are retained gaps, not passing compatibility results.
