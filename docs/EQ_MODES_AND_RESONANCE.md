# EQ modes and resonance — 2026-10-20

The stage-by-stage musician-preset investigation exposed an EQ interpretation
error. Stored mode0 selects a shelf, mode1 a bell, and mode2 a cut. ZYG previously
clamped0 to1 and interpreted2 as a shelf. The cut mode was therefore missing,
and absent type fields incorrectly selected bells. Resonance travel was also
provisional.

## Native implementation and migration

New imports select `eqMeasuredModes`. Both bands retain their stored mode in
native parameter slots:0 shelf,1 bell,2 cut. The lower band offers a low shelf
or high-pass cut; the upper band offers a high shelf or low-pass cut. Cut mode
does not use the gain control. Source parameters and opaque provenance are
preserved separately. Absent imported frequencies use209.616962917974 and
2040.5359441367445Hz, resonance60, modes0 and gains0.

The independent native Q coordinates, with `u=clamp(resonance/100,0,1)`, are:

- Shelf: `Q = 0.4 + 1.7415*u^3`.
- Bell/cut: `Q = 0.1 + 18.9*u^4`.

These describe our native filter coordinates fitted against original test
tones, not a reconstruction of proprietary implementation. Missing native
`eqMeasuredModes` means false and retains the old numeric bell/shelf path and
its modulation range. Native creative defaults remain unchanged. New-import
mode processing accepts0..2; older saved models keep1..2. Native JSON recall
needs no source sidecar. Reimport original Serum presets to select the fix.

An intermediate direct-form implementation passed static response checks but
failed a rapid frequency/resonance stress test. It was not installed. The final
path uses the existing native trapezoidal state-variable structure with
low/band/input mixing for shelf, bell and cut responses. Prepared fixed state
slews the prewarped frequency, damping and output mixtures over2ms. First
binding starts at the requested controls. No audio-thread allocation, locks,
I/O or dynamic initialization is added. Legacy direct-form processing remains
unchanged.

The public mathematical background is
[RBJ's Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)
and [Simper's trapezoidal SVF derivation](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf).
The native implementation and measured coordinate fits are independent;
no proprietary DSP was translated. This pass adds no GUI/layout code. The
existing editor's EQ mode choices have not been expanded or relabeled for the
new imported model; editing these mode controls remains a UI follow-up.

## Measurements and practical limits

103 fresh-processor reference renders contain90 EQ tone comparisons, ten dry
controls (five repeated across batches) and three sparse Bode controls. Original
mono PCM at100/500/1000/2000/10000Hz is played through the normal licensed
Serum2.1.5/REAPER7.82 path at48kHz, velocity127, MIDI60,120BPM, note1..5s.
EQ frequency is1kHz; both bands, all modes, positive/negative12dB gains and
five resonance positions are covered in the selected combinations. Accepted
processor controls and embedded float samples are checked. Measurements use
the3..4s steady window relative to each instrument's own dry render, float32,
without level matching or latency fitting.

The corrected maximum gain error is below0.002dB across90 comparisons. This
establishes these static responses, not broadband phase identity, high-rate
automation equivalence or full-preset fidelity. Dynamic checks establish
finite bounded output and allocation-free operation at44.1/48/96kHz, plus
exact output across17/63/64/256-sample process partitions for a fixed event
schedule. Reference modulation transitions remain uncalibrated.

The26 musician stage controls retain the original chain's prefix and remove
only routes to omitted effects. A first malformed shortened fixture terminated
the owned reference host; it was excluded. Corrected fixtures also use a neutral
controller state. The source-only signal is already about11–14dB quieter in
ZYG, before the compressor, so compression is not the sole cause of the full
level gap. Disabling Serum1 compatibility barely changes this tested source;
that does not establish general compatibility-mode equivalence. Bode, source
generation, time effects and remaining full-chain differences need further work.

See [`eq_modes_2026_10_20.json`](../compatibility/eq_modes_2026_10_20.json)
for aggregate before/after results and musician-corpus outcomes. Private presets,
PCM, named results and chunks stay outside Git. All24 final musician renders
and15 additional-bank smoke cases remain finite/audible with zero missing
assets. Some full-chain level errors worsen when a previously incorrect EQ
mode is corrected; no compensating gain hides that discrepancy. Replacement
factory assets remain deferred.
