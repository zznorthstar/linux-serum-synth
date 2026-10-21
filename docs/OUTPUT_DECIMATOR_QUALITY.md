# Native oscillator output filtering (2026-10-19)

ZYG uses original DSP. This change tightens the output filter used after
wavetable/SUB warps, independently of the preceding modulation reconstruction.
No proprietary implementation or reference audio was used to design it.

## Evidence and design

The previous 63-tap output FIR let a 24747.07 Hz PM sideband fold to
23252.93 Hz at a 48 kHz output rate. An independent analytic continuous PM
signal passed through the old FIRs reproduced the unwanted line without any
wavetable lookup or source interpolation. This isolated an output-filter limit.

The new kernel is an original 131-tap, unity-DC, symmetric Kaiser-windowed sinc,
beta 10, with cutoff .45 of the input Nyquist frequency. The deterministic
stdlib generator is `tools/generate_output_fir.py`. This applies ordinary
[bandlimited resampling theory](https://ccrma.stanford.edu/~jos/resample/Theory_Ideal_Bandlimited_Interpolation.html)
and the [window length/transition tradeoff](https://ccrma.stanford.edu/~jos/resample/Theory_Practice.html).
There is no borrowed coefficient table.

Dense numerical evaluation measures passband error below .0001 dB through .4
of input Nyquist and maximum stopband gain -99.57 dB from .5 of input Nyquist.
At 48 kHz output, 20 kHz attenuation is about .099 dB. The transition necessarily
attenuates valid near-Nyquist harmonics too; this is not full-band transparency.

The mirrored ring and symmetry require 66 multiplies per decimated sample,
versus 63 for the old unsymmetrized kernel. Storage is fixed at 262 doubles per
stage, versus 63 before. There are four stages per oscillator voice, increasing
ring storage by 6368 bytes per oscillator voice. No audio-thread allocation,
locks, coefficient construction or I/O is introduced. A local single-oscillator
component benchmark measured time ratios .83–1.01 versus the old filter; that
range includes scheduling noise and is not a whole-plugin CPU guarantee.

## Timing and state

The oscillator renders substep zero at the current base-sample timestamp;
decimation retains the last substep. With centre index 65, delay is
`(65-1)/2 = 32` samples at 2x and `(65+2*65-3)/4 = 48` samples at 4x. Plain
wavetable/SUB paths receive the same integer delays. At 48 kHz these are
.667 ms and 1 ms respectively. This is internal generator latency, not an added
host PDC declaration. Sample/noise/spectral paths do not receive this alignment.
Envelope application, mixed generator timing and voice-filter feedback deserve
further measurement. The separate modulation interpolator still adds its own
24-sample source-path delay.

New native patches and Serum imports select `globals.tightOutputDecimator`.
Older native JSON missing the field (including missing `globals`) retains the
63-tap filter and historical plain-path delays of 15/23 samples. Its 4x filter
delay is 22.5 samples, so the old integer alignment remains approximate.
Reimport source Serum presets to select the correction. This changes output
phase, transient timing and raw-source feedback relationships. The model is
patch state, not a new automated performance control.

## Validation and limits

The 144-case original coherent PM sweep runs at 44.1/48/96 kHz. It compares
physical sideband magnitudes against independent Bessel coefficients multiplied
by the selected output-filter response. Maximum magnitude error for components
above -40 dB is below .0012 dB. All old-model numeric measurements exactly
reproduce the previous checkpoint.

The diagnostic 4x fixture improves from -66.66 to -119.55 dB unwanted-bin
energy. Across the wider sweep, the worst 4x result improves from -66.66 to
-93.05 dB. Worst 2x remains about -31.3 dB: filtering cannot remove sidebands
already folded inside the oscillator. Some results move around the float FFT
floor; those values are not physical rejection guarantees. Broad FM, arbitrary
tables, nonlinear feedback, modulation automation, listening and full polyphony
budgets remain unverified.

Tests also check dense frequency bounds, independent convolution at startup and
after ring wrap, allocation-free processing, ramp first moments/exact delays,
1/8/16 kHz plain-versus-zero-depth warp phase, native recall and missing-state
migration. Public numeric evidence is
[`compatibility/output_decimator_quality_2026_10_19.json`](../compatibility/output_decimator_quality_2026_10_19.json).

Reproduce the sweep with `zygzxg_dsp_quality --modulation-only` and compare
`--legacy-output-decimator`; `--modulation-benchmark` runs the component timing
probe. `--held-audio-modulation` independently selects the old source model.

Final Windows VST3/CLAP/tools builds and all eight C++ suites pass (130 DSP
checks); Python interoperability passes7/7. Installed VST3 is verified in ten
owned REAPER7.82 cases, including exact old-model migration/corrected recall
and zero-depth/plain layer phase. The host's diagnostic unwanted energy changes
-66.68 to -115.94 dB2x / -66.66 to -119.62 dB4x using float32 exports. It saves
its private project and exits normally. CLAP was built but not host-tested in
this pass; Linux was not rerun. No user project was saved or restarted.
