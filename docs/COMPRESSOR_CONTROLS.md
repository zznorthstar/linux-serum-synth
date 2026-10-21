# Compressor control calibration — 2026-10-19

Follow-up: [makeup, optional dry-phase compensation and initial timing evidence](COMPRESSOR_GAIN_MIX.md). The default dry path remains phase-matched; new imports also honor the uncompensated option.

New Serum imports select `FxModule::compressorGlobalControls`. The native
compressor still uses original feed-forward dynamics, linked stereo detection
and phase-coherent LR4 crossovers. No proprietary implementation was translated.
There are no GUI or factory-content changes.

## Corrected behavior

- Global BELOW is inactive in single-band mode. Previously the provisional
  mapping could suppress quiet single-band material by 60 dB.
- Multiband processing now combines the global threshold, ratio and BELOW depth
  with each band's threshold position and compression controls. Previously the
  global controls were ignored in that mode.
- Local downward strength spans zero to full compression. Local below-threshold
  strength is bipolar: zero suppresses quiet material, .5 is neutral, one lifts
  it. Global BELOW zero disables both directions of the below-threshold branch.
- Each band's deadband separates the below-threshold branch from the downward
  threshold. Native typed fields use physical 3..24 dB, with measured sparse
  defaults 7 / 11.6 / 5.3 dB. These are not the normalized controller defaults
  retained in the older declared-default inventory. Explicit default controls
  canonicalize away in the reference state; isolated omitted/explicit middle
  defaults produce exactly equal reference audio.
- Separate up/down bypass fields act on the corresponding band branch. Static
  deadbands and bypasses use typed native state; matrix modulation of those
  fields remains unsupported and diagnosed. Existing modulation slots keep
  their identities. New-model global BELOW modulation uses the full 0..1 range;
  legacy state retains its .02..1 modulation coordinates.

## Original native curve

`src/dsp/CompressorCurve.h` expresses a bounded piecewise-linear dB gain curve.
The measured control coordinate is approximated by

```
T = 10.2 - 120.8 * clamp(globalThreshold, 0, 1)^2
    + .24 * (clamp(bandPosition, 0, 200) - 100)
D = clamp(localDown, 0, 1) * (1 - 1 / globalRatio)
U = clamp(globalBelow, 0, 1) * (2 * clamp(localBelow, 0, 1) - 1)
gainDb = clamp(-max(0, levelDb - T) * D
               + max(0, T - deadbandDb - levelDb) * U, -40, 36)
```

Bypass sets the respective slope to zero. Thresholds, slopes, deadbands,
makeup, band gains and wet mix slew over 2 ms in the corrected multiband path;
detector attack/release remain separate. Fixed arrays hold all new state.
Prepared processing performs no allocation, locks or I/O. Zero input stays zero;
the gain cap does not generate a signal. This revision retains the existing
native detector, makeup and crossover character.

This is standard independent compressor design informed by measured controls.
For public background on detector/gain-computer separation and dB-domain curves,
see Giannoulis, Massberg and Reiss,
[Digital Dynamic Range Compressor Design—A Tutorial and Analysis (2012)](https://joshreiss.github.io/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf).
The [official Serum manual](https://xferrecords.com/manual/serum-2/docs) also
describes BELOW as a multiband control. Numeric mappings above are our own
normal-host measurements, not an inference from generic manual labels.

## Evidence and limits

177 original sine controls use licensed Serum 2.1.5 in owned REAPER 7.82 hosts:
48 kHz float32, MIDI69/velocity127, 120 BPM, start1s/hold4s, with steady RMS
measured over seconds3..5. Inputs include oscillator amplitudes 1e-6..1,
threshold endpoints/intermediate knots, global ratios1/2/4/10, global/local
below amounts, local ratios, threshold positions, deadbands and band bypasses.
Loaded processor controls and source tables are checked; intentionally
out-of-range deadbands are compared with the accepted clamped values.

The first71 cases reuse a reference processor; the following106 use a fresh
processor for every case. Four final controls independently verify physical
deadband defaults. No peak normalization or level matching is applied. Native
dry level is already about .037 dB below the reference in this fixture.

- All177 corrected steady levels are within .538 dB. The55 isolated middle-band
  cases are within .051 dB; the47 additional curve/bypass cases within .049 dB.
- The matched126-case before/after baseline contains errors up to60.038 dB
  before correction. These are controlled steady-level results, not complete
  waveform or transient equivalence.
- All24 musician renders remain finite/audible with zero missing assets. Overall
  RMS error improves in10, is unchanged in6 and worsens in8; maximum worsening
  is .413 dB. Some envelopes improve and others worsen. Full-chain level and
  sound remain affected by oscillators, filters, distortion, detection/timing,
  legacy behavior and other unresolved semantics. A removed compressor warning
  is not a claim of complete preset compatibility.

Public numeric evidence is in
[`compatibility/compressor_controls_2026_10_19.json`](../compatibility/compressor_controls_2026_10_19.json).
Private scripts, source chunks, commercial preset names and audio remain outside
Git under `../oracle-private/compressor-*-20261019/` and the musician test folder.
Reference transient timing, different detector signals/frequencies, crossover
character, makeup travel, compensated-wet options, limiter modes and parameter
automation depths remain uncalibrated. Stability tests at44.1/48/96kHz do not
establish reference fidelity at those rates.

## Saved projects

The version flag and typed band fields round-trip in independent native JSON.
Missing `compressorGlobalControls` means the historical numeric model, including
previous single-band BELOW behavior. Native creative defaults remain unchanged.
Reimport the original Serum preset to select the correction; an older native
save is not silently reinterpreted through its Serum provenance.
