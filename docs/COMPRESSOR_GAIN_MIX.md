# Compressor makeup, parallel mix and timing follow-up — 2026-10-19

The subsequent [2026-10-20 band-attack follow-up](COMPRESSOR_BAND_ATTACK.md)
confirms the bounded multiband observation through normal UI/host automation
and additional frequencies. It changes new-import band detection while retaining
older saved models. The timing discussion below records this earlier checkpoint.

This pass corrects two measured static controls in the original native
compressor. It also records an initial attack/release characterization without
changing the detector or transient model. No GUI/layout or factory-pack work.

## Native behavior and migration

New imports select `compressorAffineMakeup`. Multiband makeup now uses the same
measured affine coordinate already used by imported single-band compression:
`gain = (31 * clamp(storedMakeup, 1, 31) - 1) / 30`. Stored1 means unity; stored31
means32x gain. The old multiband interpretation was simply the stored value.
The maximum difference is approximately .276dB. Existing 2ms output-control
slewing remains in place, using prepared fixed state without audio-thread
allocation, locks or I/O.

`compressorPhaseMatchedDry` represents the static compensated-wet/dry choice.
Its default is true. When true, the independent phase-coherent crossover's band
sum supplies the parallel dry signal. When false, the original input supplies
dry while the wet bands still sum with their coherent phase. This intentionally
changes intermediate-mix cancellation. At100% wet the switch cannot change
audio; at0% wet the uncompensated mode is sample-transparent. The phase-matched
mode retains crossover phase. Native and reference filters remain independent.

Both fields serialize in native JSON. Missing affine flag means the historical
multiband makeup scale; missing phase flag means the historical matched dry
path. Single-band behavior and native creative defaults remain unchanged.
Reimport original Serum presets to select new import semantics. Older native
files continue using their saved numeric model. The phase switch is a static
typed field, not a newly supported matrix destination.

## Reference measurements

50 original controls use licensed Serum2.1.5 in owned REAPER7.82 instances,
fresh processor per case,48kHz float32, MIDI60/velocity127,120BPM and a held
note from1..5s. Accepted processor controls and original source samples are
checked. No normalization or level matching is applied.

- 29 steady sine cases cover single/multiband makeup1/2/4/8/16/31,
  compensated/uncompensated dry, wet0/25/50/100%, and dry input. Maximum raw RMS
  difference improves1.479→.037dB, including the underlying native dry-level
  difference. The measured source is261.626Hz; this is not a broadband or
  all-frequency phase-equivalence claim.
- 21 step-signal cases use an original four-second1kHz sine sample: quiet for
  one second, loud for one second, then quiet for two. Each mode has attack
  and release sweeps at.1/1/10/100/1000ms, plus a dry control. All21 native
  step renders remain sample-identical across this update because makeup is
  unity and compensated dry is unchanged.
- All24 musician cases remain finite/audible with zero missing assets.
  Eight change and move slightly closer in overall RMS; the other16 are
  sample-identical. Envelope correlations remain mixed. These small level
  corrections do not resolve the larger full-chain sound differences.

The [official manual](https://xferrecords.com/manual/serum-2/docs) describes
compressor attack/release and approximate makeup boost. Its labels are context,
not a numeric transfer-function specification. For public background on
time constants, detector design and gain-computer separation, see
[Giannoulis, Massberg and Reiss (2012)](https://joshreiss.github.io/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf).
The gain/mix implementation above is independent native code informed by our
normal-host measurements; no proprietary implementation was translated.

## What the timing cases do and do not establish

One-millisecond, full-period RMS bins compare each processed step with that
instrument's own dry step. Attack metrics use half of the corresponding
fast-attack plateau reduction. Sustained release is the last crossing of half
the pre-drop reduction, avoiding early nonmonotonic crossings. These are
operational measurements, not a direct identification of detector time constants.
The signal's initial transients, crossover phase and any unmeasured reference
latency can influence the ratio.

For example, the single-band10ms attack gives a9.5ms reference half-reduction
crossing versus19.5ms native. At1000ms release the sustained crossing is692.5ms
versus662.5ms. The multiband loaded-state attack sweep gives a nearly constant
1.5ms reference crossing, while native timing varies strongly. This requires
confirmation through normal UI/host automation and other source signals; it
does not prove the reference control is universally inactive.

Short release cases include nonmonotonic behavior and the1ms analysis floor.
An initial first-crossing metric incorrectly reported fast recovery for long
releases; it was replaced by the sustained-crossing metric. Detector timing,
early transients, source resampling/latency, limiter modes, stereo/different
frequencies and modulation depths remain partial. This update deliberately
does not fit a timing algorithm to that incomplete characterization.

Public numeric results:
[`compatibility/compressor_gain_mix_2026_10_19.json`](../compatibility/compressor_gain_mix_2026_10_19.json).
Private source states, scripts, presets and audio remain outside Git under
`../oracle-private/compressor-timing-20261019/` and the musician test folder.
