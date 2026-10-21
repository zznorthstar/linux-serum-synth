# External audio-rate modulation reconstruction — 2026-10-19

This is an original native DSP quality revision. It does not reproduce Serum's
implementation or establish its phase, feedback or modulation-depth semantics.
The public tools use original sine signals and generated coefficients, with
no reference binary, factory asset or commercial preset dependency.

## Implementation and timing

Previously FM, FMX, FMP, PM, RM and AM reused one source sample for every
internal 2x/4x oscillator substep. Increasing carrier oversampling left that
source staircase in place. New wavetable/SUB modules reconstruct external
audio sources with a four-phase cardinal windowed-sinc FIR. The integer phase
is an exact delayed sample; 2x evaluates one fractional phase and 4x evaluates
three. Each phase has unity DC gain. Dual warp slots sharing a source reuse
the same reconstructed values, computed outside the unison loop.

The finite sinc/window approach follows the sampling/reconstruction framework
described by Julius O. Smith III in
[ideal bandlimited interpolation](https://ccrma.stanford.edu/~jos/resample/Theory_Ideal_Bandlimited_Interpolation.html)
and [practical windowed-sinc design](https://ccrma.stanford.edu/~jos/resample/Theory_Practice.html).
The coefficients, generator and processing code are ZYG designs; no code or
coefficient table was copied from those references.

`tools/generate_modulation_fir.py` deterministically produces 48 coefficients
per phase with a Kaiser beta of 8 and a cutoff at input Nyquist. Integer sinc
zeros are explicit; fractional phases are DC-normalized. Fixed history, source
selection and scratch arrays require no preparation allocation, locks, I/O or
dynamic initialization while processing. Invalid/tiny inputs become zero and
source changes reset the affected bounded history. Note reset clears history.
The JUCE processor retains its existing block bridge, immutable patch exchange
and `ScopedNoDenormals` protection.

Both quality modes add **24 input samples to the modulation path**: .5 ms at
48 kHz. The carrier's existing output delay is unchanged, and zero-depth
modulation adds no output latency. This changes source phase, onset and
feedback relationships; it is not a phase-transparent replacement. Near input
Nyquist, the finite reconstruction transition still attenuates source content.
Audio-source route changes and quality switches are not crossfaded. Self-PD,
audio-rate matrix routes and spectral vocoder/mask sources are outside this
revision.
Depth automation retains the engine's existing control resolution; source
reconstruction does not make all modulation destinations audio-rate.

New native oscillators and new Serum imports set `bandlimitedAudioModulation`.
Older native JSON missing the boolean retains held-source behavior; explicit
false also survives recall. The model is independent of the existing 2x/4x
quality selector. Reimport original Serum presets to select reconstruction.
Imports with applicable external warps report the modulation delay and the
remaining reference-calibration limitation.

## Measurement

Run `zygzxg_dsp_quality --modulation-only`, optionally adding
`--held-audio-modulation`. The [numeric report](../compatibility/audio_modulation_quality_2026_10_19.json)
contains all 144 results: 48 distinct normalized carrier/source/depth/quality
conditions repeated at 44.1/48/96 kHz. Coherent 32768-sample windows follow
2048 warmup samples. Only physical in-band carrier plus/minus integer source
frequency bins count as intended energy. Sideband magnitudes are independently
checked against Bessel coefficients and the output decimator response, so
reducing modulation depth cannot silently masquerade as alias suppression.

| Metric across tested cases | 2x | 4x |
| --- | --- | --- |
| Median unwanted-energy reduction | 26.20 dB | 68.17 dB |
| Worst unwanted/intended energy before | -2.36 dB | -2.61 dB |
| Worst unwanted/intended energy after | -31.34 dB | -66.66 dB |
| Maximum intended-sideband magnitude error, components above -40 dB | .00092 dB | .00112 dB |

The retained held-source path exactly reproduces all baseline numeric
measurements. 129/144 cases have lower unwanted energy; the other 15 differ
only around the float FFT floor (approximately -136 to -138 dB). Results near
that floor are not physical rejection guarantees. The original 3077.64 Hz /
4879.39 Hz full-depth fixture falls below -110 dB in both modes. These are
synthetic PM measurements, not a broad aliasing or listening certification.

`--modulation-benchmark` measures one/two distinct external PM sources in one
oscillator, best of three local trials. This run adds approximately 3–17% to
that component's processing time versus held sources. Absolute timings and
conditions are in the report. It is not a whole-plugin polyphony/CPU budget;
the existing oscillator remains expensive under large voice/unison counts.

Independent tests cover fractional physical time/DC, exact delayed integer
samples, 2x/4x agreement, invalid-input recovery, allocation-free rendering,
zero-depth output timing, PM carrier magnitude, PM/RM frequency products,
dual-slot source sharing, second-slot-only operation, native migration and
import diagnostics. Preset-bank and installed-host checks are recorded in
[testing](TESTING.md).

The final Windows VST3/CLAP/tools build and all eight native suites pass,
including 127 DSP checks; Python interoperability passes 7/7. Fifteen private
bank smoke cases remain audible/finite with zero missing assets. The installed
VST3 passes seven native original-sine recalls in an owned REAPER 7.82 host:
held/reconstructed 2x/4x, missing-field legacy, corrected recall and zero depth.
Corrected recall equals corrected audio exactly; legacy equals held audio
exactly. The host saved its private project and exited normally. CLAP hosting
and Linux were not rerun; these checks do not certify full preset fidelity.

## Remaining limit isolated

Extreme high-frequency PM still folds at 2x. A 4x case with carrier
10108.89 Hz and source 4879.39 Hz measures -66.66 dB unwanted energy. An
independent analytic continuous-PM signal, with no source hold, interpolator
or table lookup, passed through the current output decimators measures
-66.67 dB. Its strongest unwanted line is 23252.93 Hz, the fold of the upper
third sideband at 24747.07 Hz. This isolates the output FIR transition as the
next limitation in that case. A steeper output filter requires latency,
oscillator alignment and CPU evaluation rather than only a coefficient swap.
Reference phase/depth mapping, arbitrary-waveform/FM/FM-X coverage, self
feedback, output-filter quality, broader CPU limits and listening tests remain
open. Factory replacement content remains deferred.
