# Native DSP quality measurements — 2026-10-19

This pass improves original ZYG DSP. It does not establish Serum multiband
calibration or certify the whole instrument as production quality. No binary
implementation, factory asset or commercial preset is used in the public
measurement tool or report.

## Multiband compressor

The old three-way crossover sent the low band around the upper crossover's
phase response. It also mixed the filtered wet signal with unfiltered dry
audio. Neutral compression consequently changed the frequency balance,
especially at intermediate wet settings. Hidden automatic loudness matching
could undo compression, while an estimated makeup cap ignored explicit gain.

The new `phaseCoherentMultiband` model adds the upper crossover's allpass
response to the low branch. Writing each LR4 crossover's low/high transfers as
L/H and its sum as A, the three uncompressed bands sum to
`L1*A2 + H1*L2 + H1*H2 = A1*A2`. The parallel dry reference is that same band
sum. This follows the published LR4 phase/summation properties in
[Linkwitz's crossover discussion](https://www.linkwitzlab.com/filters.htm);
the implementation and three-way derivation are ZYG's own.

The new model applies requested linear makeup directly, removes automatic
loudness matching, links stereo detection and smoothly tapers upward gain
between -100 and -70 dB. Explicit band gain, makeup and wet controls use a
2 ms per-sample smoothing time constant. Detector attack/release remain
independent. Separating the gain computer, dynamics response and output makeup
is consistent with the signal flow described in Giannoulis, Massberg and
Reiss, [Parameter Automation in a Dynamic Range Compressor (2013)](https://joshreiss.github.io/documents/2013/Giannoulis%20Massberg%20Reiss%20-%20dynamic%20range%20compression%20automation%20-%20JAES%202013.pdf).
No implementation code from these sources was copied.

The new multiband dry path retains crossover **allpass phase at wet=0**. Its
magnitude is flat, but its waveform is not a sample-identical bypass. Disable
the module for transparent bypass. Parallel external routing and switching
between single/multiband modes still require transition work. Crossover,
threshold and ratio automation are not covered by the output-control smoother.

New modules and new Serum imports select this model. Native JSON missing
`phaseCoherentMultiband` selects the old model; explicit false also survives
save/recall. Existing single-band behavior, including the independently
calibrated imported single-band controls, is unchanged. Reimport the original
Serum preset to select the new model in an existing project.

Sparse Serum imports also disable native upward compression when the three
per-band below-threshold controls are absent. Retained controlled reference
renders show approximately unity gain at quiet input levels; the native
creative default of 2:1 upward compression was incorrect for those imports.
Explicit band controls remain preserved with their provisional mapping.
For four original-sine levels, native/reference RMS differences are now
-0.0134, -0.0134, -1.2923 and +2.4766 dB (input scales .001/.01/.1/1).
The historical full-input error was +11.9312 dB. The .1 case was previously
closer in RMS because hidden AGC masked the provisional threshold law; this
revision trades that incidental level match for predictable native dynamics.
These four steady fixtures do not calibrate multiband thresholds, ratios,
deadbands, bypass controls, detector timing or compensated-wet semantics.

## Reproducible results

Build/run `zygzxg_dsp_quality`, or pass `--legacy-multiband` for the retained
model. The [numeric report](../compatibility/native_dsp_quality_2026_10_19.json)
contains original-signal results and their measurement conditions.

| Measurement | Before | After |
| --- | --- | --- |
| Neutral 50% mix, 200/2500 Hz crossovers, 48 kHz, maximum tone gain error | 25.03 dB | <0.001 dB |
| Neutral 100% mix, 800/1600 Hz crossovers, 48 kHz, tone gain spread | 2.74 dB | <0.001 dB |
| Requested +6.0206 dB makeup at unity ratios | approximately 0 dB | +6.0206 dB |
| -20 dB DC input, -30 dB threshold, 4:1, upward off | approximately -20 dB output | -27.5 dB output |
| -6 dB DC input, same settings | -14.4576 dB output | -24 dB output |

All 18 multitone cases cover 44.1/48/96 kHz, 0/50/100% wet and two crossover
pairs. The retained legacy model exactly reproduces the stored baseline
crossover and steady-transfer measurements. This verifies migration of this
change; it does not guarantee unchanged sound after the separate 4× warp fix.

Regression tests independently measure impulse frequency response, static
compression transfer, upward-gain continuity, silent output, stereo linking,
output automation, host-block partition independence, native migration and
allocation-free processing. DC tests isolate the gain law; they do not certify
musical transient behavior or intermodulation.

The final Windows build passes all eight native suites (119 DSP checks) and
seven Python interoperability tests. Fifteen private additional-bank presets
remain finite/audible with their local assets resolved. An owned REAPER 7.82
instance verifies the installed VST3 in seven original-signal native recalls:
neutral mixes and corrected recall exactly equal, makeup +6.0205999 dB,
active compression and historical model migration. It saved its private
project and exited normally. CLAP builds; CLAP hosting and Linux were not
retested in this pass. These checks do not certify complete preset fidelity.

## Oscillator quality

Stateful filter warps previously received the 2× sample rate even when run
four times per output sample. The processing rate now follows the actual
substep. At a 1 kHz cutoff with a 1 kHz sine, both 2× and 4× modes measure
within 0.001 dB of the expected -3.0103 dB across 44.1/48/96 kHz. The independent
response regression tolerates 0.002 in linear amplitude for table/FIR error.

A coherent audio-rate phase-modulation fixture measures energy outside the
physical in-band sideband bins at -57.72 dB (2×) and -58.34 dB (4×), relative
to energy in physical bins. Those figures are a baseline, not a quality pass
criterion or proof of perceptual transparency. They exclude amplitude/phase
error in legitimate bins and cover only one carrier/modulator/depth. Current
audio-rate modulation sources are held across internal substeps; improving
their reconstruction and measuring wider nonlinear/filter/FX sweeps is a
priority. Increasing the oversampling label alone does not solve this case.

Further work remains on multiband reference control curves/transients,
aggressive modulation and aliasing, sample/grain resampling, stereo spectral
processing, character differences between filter/FX families, CPU budgets and
listening tests. Factory replacement content remains deferred.
