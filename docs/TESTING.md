# Testing and compatibility evidence

## EQ mode/resonance and stage isolation (2026-10-20)

- Final Windows VST3/CLAP/tools build,8/8 C++ suites including149/149 DSP and7/7 Python pass. The direct-form prototype's rapid-automation failure prompted a native TPT/SVF path with2ms prepared control slew. Final tests cover import/migration, exact legacy behavior, static response, cut-gain inactivity, bounded allocation-free automation at44.1/48/96kHz and exact output across17/63/64/256-sample process partitions.
-103 fresh reference renders contain90 EQ comparisons, ten dry controls(five repeated) and three sparse Bode controls. Maximum steady dry-relative gain error54.694→.001385dB.26 valid musician stage-prefix controls identify an11-14dB pre-FX source gap. A malformed shortened reference fixture terminated its owned host and is excluded; valid variants prune dangling FX routes/use a neutral controller state.
- All24 musician renders finite/audible/missing0:13 levels closer,9 worse,2 sample-identical; one case becomes11.318dB quieter. Fifteen additional-bank smoke cases pass. Full sound, source generation, other FX and reference modulation transitions remain partial; GUI mode choices unchanged.
- Installed SHA256 `64A5BDB2FDFB60E7BE0BB276875677F379DF3C451CAB9554C0A663546D04FCC8`, rollback `ZYG-ZXG.before-eq-modes-20261020-025017.bin`. Twelve fresh-plugin REAPER cases verify mode/gain, exact repeat/sidecar-free recall/missing-field behavior and cut-gain inactivity. Owned hosts exit0; user project untouched. CLAP built but not hosted; Linux not rerun. See [measurements and limits](EQ_MODES_AND_RESONANCE.md).


## Multiband attack follow-up (2026-10-20)

- Windows VST3/CLAP/tools build and8/8 C++ suites pass, including144/144 DSP; Python7/7. New tests cover source/provenance, native migration, exact old numeric behavior, fixed-band invariance, single-band attack and allocation-free processing at44.1/48/96kHz.
- Normal editor/automation checks use post-render accepted state because immediate capture can precede queued parameter updates. Six settled-state recalls differ by at most0.0000812 sample amplitude. Twenty-one fresh three-frequency branch controls yield18 gain-envelope comparisons: nine improve/nine unchanged, median RMSE4.939 to2.151dB. All34 native synthetic cases finite/audible.
- All24 musician cases finite/audible/missing0. Eight unchanged, three level errors closer,13 worse; two bass cases about11dB quieter. Fifteen retained-bank smoke cases pass. These results do not establish full sound compatibility.
- Installed SHA256 `14ACE45501DD4B0C8534281025DDC06A660A11A5C8D0F4905B0F278ED0599489`; rollback `ZYG-ZXG.before-band-attack-20261020-020834.bin`. Ten owned fresh-plugin REAPER cases verify exact repeats, native recall without source sidecars, legacy-field behavior and single/multiband attack selection. Owned hosts exit0; user project untouched. CLAP not hosted; Linux not rerun. See [details and limitations](COMPRESSOR_BAND_ATTACK.md).


## Compressor makeup, parallel mix and timing follow-up (2026-10-19)

- Windows VST3/CLAP/tools build passes;8/8 C++ suites including141/141 DSP,
  and7/7 Python checks pass. Tests cover mapping/native-field migration, exact
  older-model recall, measured gain travel, independent dry/wet summation,
  transparent uncompensated dry and allocation-free smoothed makeup changes.
  RMS tests use complete periods so allpass phase cannot bias their gain check;
  wet-endpoint comparisons allow floating-point lerp rounding.
- 50 fresh-processor reference controls:29 steady cases improve maximum raw RMS
  error1.479→.037dB;21 step cases characterize20 timing curves and dry input.
  All21 native step renders remain sample-identical; timing is not corrected.
  An initial first-crossing release metric was misleading under nonmonotonic
  transients; the final report uses sustained crossing and1ms analysis bins.
- All24 musician renders finite/audible/missing0. Eight overall levels slightly
  closer, other16 sample-identical; envelope results mixed. Fifteen retained-bank
  smoke cases pass. Full sound and detector/transient behavior remain partial.
- Installed SHA256 `41ACFA411E24CD57D778B98E0788BCEFEEF6960D861E4D94868B199F535C58AA`; rollback
  `ZYG-ZXG.before-compressor-mix-20261019-174638.bin`. Twelve fresh-plugin REAPER7.82 cases verify
  gain/mix, repeats, sidecar-free recall, native migration, matched/unmatched
  dry and100% wet behavior. Owned host saved private project/exited0; user
  project untouched. CLAP built, not hosted; Linux not rerun.
- Public report: `compatibility/compressor_gain_mix_2026_10_19.json`;
  private scripts/audio: `../oracle-private/compressor-timing-20261019/`.
  [Design and limits](COMPRESSOR_GAIN_MIX.md). Save/restart REAPER and reimport
  original sources for corrected import semantics.

## Compressor control calibration (2026-10-19)

- Windows VST3/CLAP/tools builds pass;8/8 C++ suites including138/138 DSP
  checks and7/7 Python interoperability checks pass. New tests cover typed
  controls/provenance, native migration, inactive single-band BELOW, measured
  middle-band levels, separate deadbands, gain bounds, continuity, silence and
  allocation-free automated processing at44.1/48/96kHz.
- Initial assertions assumed perfect isolation with outer bands attenuated,
  and included the initial input attack in an automation-step bound. Revised
  the tolerance for measured crossover leakage and measured automation after
  settling; the DSP curve was unchanged. The restricted Python run could not
  create temporary fixtures; all7 pass with ordinary temporary-file access.
- 177 original Serum2.1.5/REAPER7.82 float32 controls at48kHz have maximum
  corrected steady RMS error .538dB; isolated middle55 cases <=.051dB and47
  additional curve/bypass cases <=.049dB. Matched126-case baseline maximum
  error60.038dB. Four controls verify physical deadband defaults; explicit and
  omitted reference middle defaults produce exactly equal audio.
- All24 musician cases finite/audible, missing assets0. Overall RMS error
  improves10/unchanged6/worsens8, maximum worsening .413dB; envelope results
  remain mixed. Fifteen retained-bank smoke cases pass. No full-sound claim.
- Installed VST3 SHA256 `D147480C5076CC29B7F3A54E32AF9351429FA61B3555EF2182D99879E2EF6ECC`;
  backup `ZYG-ZXG.before-compressor-controls-20261019-164336.bin`. Fourteen fresh-plugin installed
  REAPER cases verify global depth/threshold, local suppression/bypass, exact
  repeat and sidecar-free native recall, inactive single-band BELOW, and exact
  missing-model legacy behavior. Owned test hosts save only private projects
  and exit0; the user's project is untouched. CLAP builds but was not hosted;
  Linux not rerun. Save/restart REAPER and reimport source presets for corrections.
- Public report: `compatibility/compressor_controls_2026_10_19.json`;
  private evidence/scripts: `../oracle-private/compressor-*-20261019/`.
  Details and remaining limits: [COMPRESSOR_CONTROLS.md](COMPRESSOR_CONTROLS.md).

## Embedded sample audio and native recall (2026-10-19)

- Final Windows VST3/CLAP/tools build passes;8/8 C++ suites including133 DSP
  checks and7/7 Python interoperability checks pass. Tests cover malformed
  channels/rates/floats/native blobs, stereo float preservation, inline path
  precedence, source-independent sample/granular/spectral recall, derived cache
  rebuilding and allocation-free prepared binding/note/render.
- Nine original reference/native cases confirm sample-identical inline/WAV
  playback within each instrument at24/48/96kHz, including a conflicting-parent-
  count probe. These are loading checks, not cross-instrument waveform equality.
- All24 musician renders remain finite/audible with zero missing assets.
  The affected spectral preset's two renders change; other22 are sample-identical
  to baseline. Fifteen retained-bank smoke renders also pass. Full-preset sound,
  compressor mappings and legacy behavior remain partial.
- Installed VST3 SHA256
  `07E29F67B7430F157138E51F1B521F6B85E1615746C13A1158529C262D679750`
  passes eight fresh-plugin REAPER7.82 cases: exact source samples, repeat/import
  equality, native recall with Serum sidecars removed, and source-removal controls
  atMIDI36/60. Reusing one processor first advanced its random-note history;
  the final harness resets processors and includes unchanged-repeat controls.
  Private projects saved; hosts exited0; user project untouched. CLAP builds
  but was not host-tested this pass; Linux not rerun.
- See [design, validation bounds and remaining formats](EMBEDDED_AUDIO.md).


## Output filtering and supplied musician presets (2026-10-19)

- Final Windows VST3/CLAP/tools builds succeed; 8/8 C++ suites pass including
  130/130 DSP checks, and Python interoperability7/7. Independent convolution,
  ring wrap, response bounds, allocation-free processing, exact decimator delay,
  high-note layer phase and missing-field/missing-globals migration are tested.
- The 144-case original PM sweep improves worst 4x unwanted energy from
  -66.66 to -93.05 dB with physical-component magnitude error below .0012 dB.
  Old-model numeric results exactly reproduce baseline. Extreme 2x internal
  folding remains near -31.3 dB. See [design/limits](OUTPUT_DECIMATOR_QUALITY.md).
- Installed VST3 SHA256
  `ED20F10157640146DF78D2245E6C32D456D66936046D0E1F444DD0484918BC80`
  passes ten original-sine recalls in fresh owned REAPER7.82. Legacy/corrected
  2x/4x, missing-model migration, corrected recall and zero-depth/plain layer
  phase are verified. Host diagnostic unwanted energy changes -66.68 to
  -115.94 dB2x / -66.66 to -119.62 dB4x. Respective legacy/recall pairs are
  exact; near-floor spectra do not establish physical rejection guarantees.
  The host saved its private project and exited0; user project untouched.
- The 15 retained private bank smoke cases remain finite/audible with zero
  missing assets. Newly supplied14 files contain12 distinct musician presets.
  All24 paired reference/native cases are finite/audible at notes36/60, full
  velocity, 120 BPM/48 kHz, start1s/hold4s, common first8s. Accepted-state checks
  reject init fallback; final float32 reference exports avoid integer clipping.
  RMS differences span -19.95 to +1.78 dB; envelope correlations .634–.992.
  One preset lacks embedded spectral-audio support; eight retain partial-feature
  diagnostics. See [method, findings and limits](MUSICIAN_PRESET_CORPUS.md).
- Broader FM/feedback/automation, embedded sample recall, compressor/legacy
  behavior, listening and full polyphony CPU budgets remain open. CLAP builds
  but was not host-tested in this pass; Linux was not rerun. Private content and
  reference audio remain outside Git; no replacement asset pack was created.

## External audio-rate modulation quality (2026-10-19)

- Windows VST3/CLAP/tools build succeeds. All 8 native suites pass, including
  127/127 DSP checks; Python interoperability passes 7/7. All 15 private bank
  smoke cases remain audible/finite with zero missing assets at
  MIDI36/velocity127/120 BPM, 2-second hold/6-second render.
- The 144 original PM cases compare held/reconstructed 2x/4x spectra at three
  sample rates and independently verify physical sideband magnitudes. Legacy
  measurements exactly reproduce baseline. The new model reduces median
  unwanted energy 26.20/68.17 dB (2x/4x), with intended-component magnitude
  error below .0012 dB. Near-floor results are not physical rejection bounds.
- New regressions measure fractional timing, DC/integer-phase exactness,
  nonfinite recovery, zero-depth output timing, PM/RM frequency products,
  allocation-free rendering, dual-slot sharing/second-slot-only execution,
  native migration and imported-model diagnostics. Source reconstruction adds
  24 input samples of modulation-path delay in both quality modes.
- Installed VST3 SHA256
  `4FD1F19ED98D626739BCC6E5EF0B36D42238B7D318705E3223837C30D0FACB97`
  passes seven native original-sine recalls in a fresh owned REAPER 7.82/x64.
  Held/reconstructed measurements are -57.72/-128.49 dB (2x) and
  -58.34/-136.49 dB (4x) in the original coherent fixture. Corrected recall
  exactly equals corrected audio; absent-field legacy exactly equals held
  audio. Saved chunks confirm model/quality selection. The host saved its
  private project and exited0; the live user project was untouched.
- CLAP builds but was not host-tested; Linux was not rerun. Extreme 2x
  folding, 4x output-FIR transition leakage, broad source/automation/feedback
  behavior, listening and whole-plugin CPU budgets remain open.
  See [design, public measurements and isolated next limit](AUDIO_RATE_MODULATION.md).

## Native multiband quality verification (2026-10-19)

- Final Windows VST3/CLAP/tools build succeeds. All 8 C++ suites pass,
  including 119/119 DSP checks; Python interoperability passes 7/7.
- The standalone `zygzxg_dsp_quality` tool renders original synthetic signals.
  Eighteen multitone cases cover two crossover pairs, 0/50/100% wet and
  44.1/48/96 kHz. New neutral magnitude response is flat within .001 dB;
  the retained legacy model exactly reproduces the baseline crossover/DC
  measurements. Explicit makeup and steady 4:1 downward gain match the native
  specification. This is not a reference transient/dynamics calibration.
- Regression coverage adds independent impulse frequency response, upward
  floor continuity/silence, stereo linking, output-control smoothing, host-block
  partition independence, allocation-free processing, sparse imported upward
  defaults and native old/new state. One initial historical AGC assertion used
  insufficient settling; extending the original DC fixture to four seconds
  resolves it without changing legacy DSP.
- Filter-warp cutoff remains near -3.0103 dB at its 1 kHz cutoff in both 2x/4x
  modes at all three tested rates. The coherent audio-rate PM fixture remains
  a baseline exposing limited 4x improvement, not an antialiasing pass gate.
- Four retained controlled Serum 2.1.5 sine cases expose provisional import
  scaling: RMS differences at .001/.01/.1/1 input are now approximately
  -.0134/-.0134/-1.2923/+2.4766 dB. Full-input historical error was +11.9312 dB;
  the .1 case previously had a closer incidental level match from AGC.
- All 15 private additional-bank cases remain non-silent/finite with zero
  missing assets at MIDI36/velocity127/120 BPM, 2-second hold/6-second render.
- Installed VST3 SHA256
  `F820A3B459344A46A67DB9DBB4CC2A574CEACEC0DF4498A63D75899517E75830`
  passes seven recalls in an owned REAPER 7.82/x64 instance. Neutral 0/50/100%
  mixes and corrected recall produce exactly equal measured audio; makeup is
  +6.0205999 dB; active compression differs and absent-field native legacy state
  retains its historical model. Saved chunks confirm each selection. The host
  saved its private project and exited0; the live user project was untouched.
  CLAP builds but was not host-tested. Linux was not rerun in this pass.
- See [design, numeric evidence and open quality limits](NATIVE_DSP_QUALITY.md).

## Delay timing verification (2026-10-19)

- Windows VST3, CLAP and tools build succeeds; all 8 native suites pass,
  including 111/111 DSP checks, and all 7 Python interoperability tests pass.
- 42 initial reference controls expose first-note settling. Sixty further
  primed reference passes verify twelve grid centers, both sides of eleven
  switching neighborhoods, three tempos, free-time knots, stereo offsets and
  linked-state canonicalization with omitted defaults. Accepted processor state
  is captured for every case. All three owned reference hosts exited normally.
- Sixty paired native renders using the same priming/event schedule differ from
  reference echo-peak timing by at most .104167 ms. This does not measure full
  waveform similarity, feedback/filter sound or startup/modulation transitions.
- Eight new native regressions plus expanded state tests cover grid/free targets,
  stereo/Link, BPM changes, long-delay storage/capacity, fourth-power offsets,
  allocation-free processing, import provenance and old/new native state.
- All 15 private additional-pack presets render finite/non-silent with zero
  missing assets at MIDI36/full velocity/120 BPM, 2-second hold and 6 seconds
  total. This is a stability/asset smoke check, not full-preset fidelity.
- Measurement tools now support an optional priming note and delayed measured
  note: Lua `primeNoteHoldSeconds` / `noteStartSeconds`, native renderer
  `--prime` / `--start`. Defaults retain the existing single-note schedule.
- Installed VST3 in a fresh private REAPER host verifies six recalls: three
  BPMs, corrected free timing, older native JSON missing the timing-model field,
  and corrected state recall. Native chunks confirm numeric enum values 0/1;
  the harness's initial string assumption was corrected before completing the
  same host run. All renders are finite; echo peaks are within 2 ms of their
  intended targets including host/signal latency. The host saved and exited0.
  CLAP hosting, live time/sync modulation and Linux were not rerun.

## Splitter routing verification (2026-10-18)

- Windows VST3, CLAP and tools build successfully. All 8 native suites pass,
  including 103/103 DSP checks; all 7 Python interoperability tests pass.
  File-based suites require ordinary temporary-file access.
- 37 private original-signal Serum fixtures verify accepted state and 20 exact
  bypass relations, with nine audible active controls. Both frequency splitters,
  every branch, mono-mid/pure-side input, nested bypass and post effects are
  covered. The native baseline satisfied 3/14 relations in the first 22 cases;
  corrected native renders satisfy all 20 relations in the expanded corpus.
- Seven synthetic regressions cover traversal, native offset consumption,
  allocation-free processing/rebind, malformed/clipped nested counts, state and
  provenance round trips, capacity diagnostics and unsupported route accounting.
  The fixed capacity fallback is tested as transparent, not as a reference match
  for arbitrary deep graphs. Solo and bypass changes during tails remain open.
- Installed x64 VST3 verification in a fresh disposable REAPER instance captures
  native branch state, reloads five controlled native chunks and renders at
  48 kHz. Whole/branch/recalled bypass match dry exactly; the active inverter
  differs (maximum error .070627). No missing assets or nonfinite samples.
  The private project saved and the owned host exited normally. This check
  does not validate CLAP hosting, live bypass transitions or Linux.

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

The current private-fixture run imports the authored-2.1.2 preset into `Patch`, loads its user-owned wavetable and renders finite non-silent audio. It reports `57/83` explicit fields typed, 91 unsupported/unrendered diagnostics and finite peak `0.0548913`. The authored-2.0.24 fixture reports `43/75`, 84 diagnostics and peak `-0.259292`; its imported Lorenz LFO 1 drives wavetable position and Macro 1 drives audio-rate FM depth from zero-level OSC B. These numbers are **not semantic coverage**; diagnostic categories may overlap. The test asserts every explicit field is either typed or individually flagged as unmapped.

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

The imported-preset editor check before the later DSP work showed the 2.0.24 preset name, its OSC A wavetable, then-current 33/75 typed explicit fields, eight matrix routes, Macro 1–4 values (0.716, 1.0, 0, 1.0), enabled NOISE at 0.068, five FX instances marked DSP pending, and the then-unsupported imported LFO label. A screenshot was inspected locally and is not committed because the preset/assets are private. The later LFO DSP change was core-tested, not re-screenshotted.

## Design-kit editor and current-binary host check

The user-provided `ui-design-kit/` was integrated after the checks above. CMake embeds its logo and 32/48/64 px 128-frame knob strips in VST3 and CLAP. The editor now uses the kit's 1000x600 dimensions, hard-edge palette and sprite controls while drawing waveform, filter, envelope, LFO and output displays from live patch/processor state. `ZYG_UI_SNAPSHOT=/tmp/name.png reaper -newinst tests/reaper_gui.lua` exercises an environment-gated UI-thread snapshot hook, useful because the Wayland compositor denies external screenshot APIs. The actual REAPER-hosted 1000x600 snapshot was inspected twice; the second pass corrected control/title collisions and a clipped audition label.

After rebuilding and SHA-256 matching the installed copies to the build outputs, fresh realtime tests of this exact binary returned:

- VST3: `play_polls=57`, `max_position=1.877333`, `track_peak=0.002899170`, `master_peak=0.115633450`.
- CLAP: `play_polls=56`, `max_position=1.877333`, `track_peak=0.002410889`, `master_peak=0.115552559`.

The core test checks oscillator phase randomization, curved envelope onset, wavetable mip-cache geometry, more than 20 dB suppression of a known high-note saw foldback bin, 2× FIR pass/stop bounds, oversampled/dry phase alignment, zero-level oscillator FM, render-order-independent audio-rate source timing, Macro→warp depth, stable dual hard-clip/sine-fold processing, typed-route independence from raw Serum labels, native sine/macro/Lorenz modulation, finite filter extremes and native state round-trip of LFO/warp definitions. Current private fixture runs report 2.1.2 at `57/83` typed with finite peak `0.0548913`, and 2.0.24 at `43/75` typed with finite peak `-0.259292`. The large remaining diagnostic counts are retained gaps, not passing compatibility results.

After the final render-order timing fix, the rebuilt VST3 (`284d06ea6756af7102b4a07ca18eb36502db57a313c369d5b76b16b1587bf228`) and CLAP (`9a2dd16e1b251cdb190cd92fe91f4de84ff8fcdab633da42852738024d2aaa3e`) were installed with hashes exactly matching their build outputs. A new separate REAPER VST3 process loaded the private 2.0.24 fixture via the environment-gated developer hook. The disposable armed/monitored track ran realtime MIDI transport and reported `play_polls=59`, `max_position=1.877333`, `track_peak=0.002166748`, `master_peak=0.114783958`. The process was terminated after the script. This proves the exact rebuilt VST3 remains realtime-active with the fixture and its imported Macro 1 → FM-depth route; it does not validate subjective similarity, every route, tempo sync, CLAP realtime for this exact code change, or speaker audibility.


## DSP suite and corpus rendering (2026-09-26)

`build/zygzxg_dsp_tests` (58 tests) covers filters, all FX, envelope/LFO/point buses, unison, pitch, wavetable position, sample/multisample/granular/spectral/noise/warps/SUB/oversampling/slicing, matrix routing and viaEnv, buses, portamento/mono/sustain/bend, velocity, zones, arp, clip player, importer maps, SFZ, JSON round-trip, extreme-patch finiteness, and zero allocation on the audio path with every module active. Run via `ctest --test-dir build`.

Corpus method: `zygzxg_render preset.SerumPreset --note 48 --vel 0.8 --seconds 3 --nolimit` over all factory presets (assets resolved from `.local-serum-content`). Result: all 621 renderable presets finite; peak dBFS percentiles 5% −28, 50% −13, 95% +3 (limiter off). Silent outputs were investigated; the remainder are legitimate (e.g. a kit whose keys start above the test note). This is an objective sanity/level check only, not a similarity test against Serum.


## UI (2026-09-26)

- `ctest` now also runs `ui_model_tests` (FX rack add/remove/move, splitter band counts, modulation re-indexing).
- Visual checks: `build/zygzxg_ui_snapshot_artefacts/Release/zygzxg_ui_snapshot --editor out.png --preset SO_IS_bass_press.SerumPreset --size 1280x720 --page 2` (needs an X display, e.g. `DISPLAY=:1`). Pages: 0 OSC, 1 MIX, 2 FX, 3 MATRIX, 4 GLOBAL. `--modal browser --wait 1500` shows the preset browser after its scan; `--live 900 --play` drives the engine with a held note so the reactive animations are captured.
- Not covered by automation: real pointer/keyboard interaction inside a plugin host.

## Editor in REAPER (2026-09-26)
A disposable `reaper -newinst tests/reaper_gui.lua` instance loaded the private 2.0.24 preset; input was injected with XTest (`XTestFakeMotionEvent/ButtonEvent/KeyEvent`) and inspected via root-window screenshots. Results: page tabs, browser open/search/Enter-to-load (`BA - FM Wobble` factory preset loaded with its assets from `~/.local/share/ZYG-ZXG/Content`), knob keyboard entry ("50" + Return -> 50%), and dragging LFO 1 onto Reverb WIDTH (route created, badge 1->2, toast, LFO chip drag image) all worked. Before the modal-overlay fix none of the clicks reached any widget. Realtime transport tests with the final binaries (VST3 sha256 `b246c8d3...`, CLAP `c691d38b...`, installed copies matching): VST3 `play_polls=58`, `master_peak=0.9994`; CLAP `play_polls=59`, `master_peak=0.9985` with the loud FM Wobble preset (the soft limiter is at its ceiling). Not verified: audible LFO->cutoff by ear, host resize, 44.1/96 kHz, automation, project reload of edited state.

## Windows black-editor recovery (2026-10-18)

The normal user REAPER 7.82 process reproduced a black custom editor. Windows
now starts on software/GDI and reads/writes UI preferences in the shared profile
config folder, avoiding packaged-process AppData redirection. Legacy size/retro
settings migrate; the old GPU choice is reset during migration. No layout or
DSP changes were made. The Linux default/preferences remain as before.

A fresh disposable host visibly rendered the installed VST3 with a legacy GPU-on
file and no `ZYGZXG_NO_GPU` override. Closing/reopening the editor and selecting
the FX page worked. A shorter repeat using saved software settings opened and
reopened the editor, saved its private project and exited with code 0 after
explicit `Audio_Quit()`. The first longer interactive harness stalled during
shutdown after a save prompt; its saved private project was retained and only
that disposable process was stopped. Unconstrained shutdown and other Windows
GPU/host combinations are not established by this test.

All 8 C++ suites pass, including 96 DSP checks; resample/library suites were
rerun with normal filesystem permissions after sandboxed file creation failed.
Private receipts, project, component snapshot and host result are outside Git
in `../oracle-private/editor-recovery/`. Installed VST3 SHA256:
`4A03690E3F5BCAF59AAFEEAA66D1B5F88756FCBE44A5EE426FDF2414BB7815F4`.
The user project was preserved. A live recovery script was not run after an
approval-review rejection; the installed fix requires a normal host restart.

## UI legibility/proportion pass and GPU renderer (2026-09-27)

- `ctest`: 5/5 pass, including new core tests for embedded-wavetable JSON round trip (frames bit-exact, mipmaps rebuilt, asset-backed tables stay references) and Env `restartOnSteal` on mono retrigger.
- Offline import/render (`zygzxg_render ... --info`) of the 11 user presets in the repo root: all finite; remaining notes are `Env BeatSync` (69 factory presets, semantics unverified) and `kParamS1Compatibility`.
- Snapshots of all pages at 1280x720 and the OSC page at 1000x600, idle and with a held note (`--live 900 --play`).
- REAPER 7.80 (X11, disposable `-newinst` instance, VST3 installed by atomic rename so the user's running REAPER kept its mapped binary): editor opened at exactly 1280x720 with a nested JUCE OpenGL child window (GPU path active); an XTest click on the on-screen keyboard played a note and the live WT frame, spectrum, envelope playhead and LFO cursor animated. Not verified: CLAP editor under GL, host resize under GL, other GPU drivers/Wayland, listening checks.

## Modulation / FX / preset browser pass (2026-09-27)
- `ctest` 6/6 (new `library_tests`: folder layout + readmes never overwritten, Serum and ZYG presets listed equally as user presets, type classification; new core test: MIDI CC source modulates OSC level and survives JSON).
- REAPER 7.80 disposable instance, XTest: right-click CUTOFF > MOD SOURCE > LFOS > LFO 2 created a route (LFO 2 tab badge 1, ring on the knob); dragging the MW chip onto OSC A FIN created a route (violet bar, MW chip lit); FX page EQ toggled off shows OFF with a dark LED (re-verified in a fresh instance with the final binary); preset browser stayed open while a click and a Down key loaded two presets (toast + top bar updated); logo click opened ABOUT. Not verified: CLAP, sound while auditioning with a running DAW loop (engine keeps voices across patch swaps by design), wavetable browser USER folder listing in-host.

## Browser crash, retro screens (2026-09-27)
- Reproduced the REAPER crash under `gdb -batch -ex run -ex "thread apply all bt" --args reaper -newinst tests/reaper_gui.lua` via preset dropdown > BROWSE ALL PRESETS: SIGSEGV in `presetTypeOf` on the scan thread (dangling initializer_list). After the fix both BROWSE ALL and USER PRESETS open in REAPER; USER shows an explicit "no user presets yet" message with the folder path.
- AddressSanitizer build of `zygzxg_ui_snapshot` (RelWithDebInfo, `-fsanitize=address`, leaks off): `--selftest-library`, `--selftest-routes`, browser modal with a completed 628-preset scan, OSC page with a held note, MIX/FX/MATRIX/GLOBAL pages: no errors. The ASan build dir was removed afterwards.
- `ctest` 6/6. REAPER CPU (whole process, editor open, 2 s `top` samples): idle ~37 % retro on = retro off; holding a note ~66 % retro on vs ~57 % off (after 30 fps + idle skipping + loop optimisation; before: idle 71-77 %).

## v0.1.0-beta checks (2026-09-27)
- `ctest` 6/6, also with `DISPLAY`/`WAYLAND_DISPLAY` unset (CI-like headless run).
- REAPER disposable instance: DS_AC2 reese shows flat single cycles for its 7-frame Basic Shapes oscillators and the 3D mesh for PWM Juno (112 frames); browser BASS filter -> pick "808 - Simple Electro" -> DONE -> top-bar next arrow loaded "808 - Straight"; reopening restored the BASS filter with Straight selected. Idle process CPU with the editor open ~44 % of one core (steady 30 fps screens).

## Windows JUCE 8 functional validation (2026-10-18)

Windows installation follow-up: installed the x64 VST3 in `%LOCALAPPDATA%/Programs/Common/VST3/ZYG-ZXG.vst3` and added that folder to REAPER's VST path. The editor initially appeared black; software mode now selects the Windows GDI peer instead of Direct2D. Rebuilt VST3 opened with its custom editor visibly rendered in REAPER 7.82. No DSP changes or Serum comparisons were performed in this installation follow-up.

- MSVC 2022 / JUCE 8.0.9: sequential builds of VST3, CLAP, renderer and test tools succeeded. `ctest --test-dir build/windows -C Release --output-on-failure`: 8/8 suites passed, including 86/86 DSP tests. Python interoperability tests: 7 passed.
- Python command: `.venv/Scripts/python.exe -m unittest discover -s tests -p test_interop.py`. Windows sandbox restrictions on temporary-directory path resolution caused three infrastructure errors; the authorized run outside that sandbox passed all seven. Pytest is not installed or required.
- Patch exchange: 10,000 concurrent publications. Host parameter tests cover zero-offset audio identity, per-sample frame smoothing, actual frame bounds, MIDI segment bounds, allocation-free rendering, state restore and prepare-before-restore ordering. DSP additions cover allocation-free LFO binding, SUB alias suppression, ultrasonic fundamentals, preset voice replacement and live multisample-region removal.
- REAPER 7.82/x64: private legacy state-2 project loaded in both native VST3 and CLAP. Frame-offset automation changed audio; fresh instances restored the state-3 control value. Both formats produced identical rendered samples. Live-automated versus restored output differed in 479 initial frames from the expected 10 ms parameter ramp; output after 20 ms matched exactly. This is limited host validation, not every automation/state scenario.
- Serum 2.1.5 measurements: 203 declared FX defaults across 16 types; 34 validated distinct compressor states. Controlled limiter/makeup level cases are within 0.07 dB RMS. Default single-band compression differs by +0.13 dB; other threshold cases remain approximately +0.43/-1.00 dB. Multiband full-input case differs by +11.93 dB.
- Six full factory presets rendered finite float WAV audio with resolved assets, RMS differences -13.14 to +6.73 dB and substantial spectral differences. Renderer float output retains values above unity instead of hiding them with PCM clipping. See `SERUM_ORACLE_MEASUREMENTS.md` for individual cases and limitations.
- No new GUI work or GUI validation. Linux, other hosts, host sidechain/resampling, extreme modulation spectra and full-preset listening remain outstanding. Existing remote CI success applies only to the older committed revision.

## Time-effect and Windows content follow-up (2026-10-18)

- Rebuilt VST3, CLAP and test tools. Final CTest: 8/8 suites, including 94/94 DSP tests; Python: 7/7. Added wet-endpoint, first-repeat filtering, decay-versus-pre-delay, reverb phase continuity, low/high-cut direction, time-effect migration, importer defaults/mode/unsupported-sync and saved content-folder tests. Existing allocation-free render checks pass.
- Missing-content reproduction: private `LEAD - Flames` renders zero with an absent root. After saving the supplied local root, the renderer resolves it without an explicit root and produces nonzero output. Saved-setting tests cover UTF-8/BOM/CRLF, missing folders and relative paths; no test modifies the real user setting.
- Installed the rebuilt x64 VST3. A fresh private REAPER 7.82 instance loaded the same preset through the normal processor import path with only the preset smoke hook, no asset-root override. Captured native state reports the correct saved content folder, zero missing assets and corrected time-effect models. Render: 48 kHz, finite, peak .9687774, RMS .1029764. The private test/reference sessions were saved and closed; the user's working instance was left running.
- All 15 private Ghosthack presets passed at notes 36 and 60, velocity 127, 120 BPM: 30 finite/non-silent renders and zero missing assets without explicit asset roots. This is a smoke check, not a comparison score. Reference time-effect observations and remaining timing/mix/character limits are in `SERUM_ORACLE_MEASUREMENTS.md`.
- Existing open plugins keep their loaded DLL. Restart REAPER to use the installed update; reimport Serum source presets for corrected time-effect models. Legacy native saves intentionally retain the historical model. No replacement factory library was generated.

## Four reported presets and Windows configuration boundary (2026-10-18)

- Full Windows rebuild succeeds for VST3, CLAP and tools. CTest passes 8/8
  suites, including 96/96 DSP tests; Python interoperability tests pass 7/7.
- Before/after isolation runs cover four private presets at MIDI 36/60,
  full velocity and 120 BPM, with FX/filter/modulation bypass controls: 32 runs
  per revision. Missing-root controls reproduce exact silence in both reported
  silent examples. Explicit roots resolve every asset in the baseline runs.
- Synthetic impulse tests cover native ladder decay at 44.1/48/96 kHz,
  1/10/20 kHz cutoff and high Var with moderate resonance. A sparse import
  regression distinguishes absent FXFilter resonance, explicit resonance and
  unchanged native defaults. Finite output alone is not the decay assertion.
- The earlier AppData saved-root host check shared Codex's MSIX redirection;
  it did not prove ordinary user-host configuration. The selected root now uses
  a canonical, nonvirtualized profile config path. Legacy AppData selection
  remains a fallback. No audio-thread file access was added.
- Two fresh disposable REAPER hosts validate the installed updated VST3 using
  Gnarly Bass Stack and ElectroFluxx, with no asset-root environment overrides:
  correct saved root, zero missing assets, finite nonzero output and no final
  sustained ElectroFluxx tail. Saved private projects and state/audio evidence
  are retained outside the checkout; both owned hosts exited normally.
- CLAP builds, but this particular correction's new host checks use VST3.
  Current editor behavior, arbitrary automation sweeps, Linux and complete-preset
  Serum fidelity are not established by these tests.
