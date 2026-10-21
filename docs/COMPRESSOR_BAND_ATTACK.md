# Multiband detector attack follow-up — 2026-10-20

New Serum imports use an independent fast native band detector instead of
applying the global attack time to each band. This is a bounded behavioral
correction based on normal editor/host controls and original test signals.
The native detector and gain computer remain our own implementation.

## Behavior and saved state

`compressorBandAttackMs=0.1` selects the native band detector attack in
multiband mode. Negative values follow the global attack as before. The global
attack remains stored, preserved in source provenance and active in single-band
mode. Missing native fields default to -1, preserving older saved processing;
native creative defaults also remain -1. Reimport original Serum presets to
select the new model. No UI or APVTS parameter slots are added.

The 0.1 ms value is our native modeling choice, not an identification of
Serum's internal time constant. Release, stereo linking, crossover filters and
gain curves retain their independent native designs. This pass does not establish
the semantics of arbitrary multiband attack modulation. In the selected native
model, global attack automation does not change the fixed band attack.

## Evidence and limits

Licensed Serum 2.1.5 in owned REAPER 7.82 instances was exercised through its
editor and normal host automation. Direct editor edits and five-point host
attack sweeps confirmed the nearly invariant multiband response previously
observed through preset loading. Host attack/release coordinates follow
`0.1 + 999.9 * normalized^2` at the five measured points. Matrix amounts have
not been shown to use that same coordinate, so they are not changed here.

An immediate state capture after a host parameter write can precede the queued
update. Validation therefore uses accepted processor state captured after
rendering. Six recalls of that settled state differ from the warm reference by
at most 0.0000812 sample amplitude; they are not claimed bit-identical. Earlier
pre-render captures were excluded from that recall comparison.

Twenty-one fresh-processor controls cover original 100 Hz, 1 kHz and 10 kHz
sine steps, dry/downward/upward/combined compression and 0.1/1000 ms attacks.
Each sample is quiet for one second, loud for one second and quiet for two;
48 kHz float32, MIDI60, velocity127, 120 BPM, note1..5 seconds. Loaded controls
and embedded float samples are checked. Ten-millisecond gain envelopes are
relative to each instrument's own dry control over1.02..4.95 seconds, without
level matching or latency fitting.

All nine slow-attack responses improve; the nine fast-attack controls remain
unchanged. Median gain-envelope RMSE drops4.939 to2.151 dB. Remaining per-case
RMSE reaches4.808 dB, particularly at high frequency. This is not a complete
detector, recovery, broadband, stereo or full-preset calibration.

All24 musician renders remain finite and audible with zero missing assets.
Eight are sample-identical; among16 changed cases, three overall RMS errors
improve and13 worsen. Two bass cases become about11 dB quieter despite improved
envelope correlations. Faster compression exposes substantial unresolved
full-chain level differences; no compensating gain is added to conceal them.
Four source presets still have partial-feature diagnostics. Fifteen retained
bank smoke cases remain finite/audible with zero missing assets.

Public aggregate measurements are in
[`compressor_band_attack_2026_10_20.json`](../compatibility/compressor_band_attack_2026_10_20.json).
Private presets, PCM, chunks and named results remain outside Git. The next
priority is stage-isolated gain/detector/source-level investigation in the
affected musician presets, followed by release/stereo/limiter and broader
filter/warp/modulation behavior. Factory replacement assets remain deferred.
