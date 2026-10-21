# DSP decisions

## Measured EQ modes with native state-variable processing (2026-10-20)

Original normal-host tones establish shelf0/bell1/cut2 and nonlinear Q travel.
New imports select `eqMeasuredModes`, preserving raw state separately. The
first direct-form prototype was rejected by a rapid coefficient-change test;
the final path uses existing native trapezoidal SVF state with2ms control
slewing. First binding starts at requested controls, and missing fields retain
the older EQ path. Ninety static tone comparisons differ by at most0.001385dB;
full-chain levels remain mixed and the source-only gap is unresolved. See
[design, public mathematical sources and limitations](EQ_MODES_AND_RESONANCE.md).

## Independent fast band detector for new imports (2026-10-20)

Normal editor/host automation and original sine steps at100/1000/10000Hz
confirm the bounded near-invariant reference multiband attack response. New
imports select a0.1ms native band detector independently of global attack;
this is an original modeling choice, not a reference internal time constant.
Single-band and missing-field native behavior retain global attack. Median
dry-relative gain-envelope RMSE drops4.939 to2.151dB across18 cases. Full-chain
levels remain mixed, including about11dB additional attenuation in two musician
cases. No compensating gain is fitted to hide that remaining discrepancy.
Prepared processing adds no allocations, locks or I/O. See
[measurements and limitations](COMPRESSOR_BAND_ATTACK.md).

## Compressor makeup and optional dry-phase compensation (2026-10-19)

Normal-host original controls verify an affine multiband makeup coordinate and
the static compensated-wet/dry choice. New imported fields select those semantics;
missing fields retain previous native behavior. Original detector/crossovers
remain in use, with prepared2ms makeup slewing. Twenty timing curves show
unresolved transient differences; no timing algorithm was fitted to the initial
single-signal characterization. See [evidence and public DSP reference](COMPRESSOR_GAIN_MIX.md).

## Independent compressor curve with measured control coordinates (2026-10-19)

177 normal-host original-sine controls expose ignored multiband globals and
incorrect single-band BELOW gating. A new native model combines quadratic
threshold coordinates, local position, global/local downward strength, bipolar
below strength, physical deadbands and separate branch bypass. The gain computer
is an independent bounded piecewise-linear dB curve; existing native detection
and phase-coherent crossovers remain. Fixed arrays slew curve controls over2ms.
Old native saves and native creative defaults retain their prior numeric model.
See [derivation, public DSP reference and limitations](COMPRESSOR_CONTROLS.md).
Steady-level agreement does not prove transient or full-preset equivalence.

## Embedded audio stays outside the processing callback (2026-10-19)

Inline planar audio is decoded into immutable native samples on the control
thread. Independent versioned float32 native serialization preserves recall
without dispatching Serum state; spectral analysis remains a derived cache
prepared before publication. Bounds and malformed-data errors are explicit.
Original inline-versus-file oracle controls verify channel/rate behavior; this
changes asset preparation, not the native DSP algorithms. See
[design and evidence](EMBEDDED_AUDIO.md).

## Tighter oscillator output filtering (2026-10-19)

An independent analytic PM test isolated foldback to the previous output FIR
transition. New patches/imports select an original 131-tap symmetric Kaiser/sinc
decimator; old native state retains the 63-tap path. Exact 32/48-sample delays
align plain and warped wavetable/SUB paths at 2x/4x. The wide PM sweep's worst
4x unwanted energy improves from -66.66 to -93.05 dB; extreme 2x remains near
-31.3 dB. Near-Nyquist attenuation, additional output/feedback delay and fixed
state memory are explicit tradeoffs. See [design and sources](OUTPUT_DECIMATOR_QUALITY.md).

## External audio-rate source reconstruction (2026-10-19)

New wavetable/SUB external FM/PM/RM/AM families reconstruct sources at internal
substeps with an original fixed four-phase cardinal windowed-sinc FIR. Its
24-sample modulation-path delay is shared across 2x/4x modes; native state
missing the independent model flag retains held sources. The 144-case PM sweep
checks unwanted energy and analytic sideband magnitudes. 4x median unwanted
energy drops 68.17 dB; intended component error stays below .0012 dB. Extreme
2x folding and 4x output-FIR transition leakage remain measured limitations.
This changes source phase/feedback timing and has a CPU cost; it does not
certify Serum modulation semantics or whole-engine quality. See
[design, primary sources and limits](AUDIO_RATE_MODULATION.md).

## Native quality revision (2026-10-19)

Measured neutral multiband cancellation and hidden loudness matching motivated
an independently versioned phase-coherent model: upper-crossover allpass
compensation on the low band, phase-matched parallel dry audio, explicit makeup,
continuous upward floor and smoothed output controls. Missing native state
fields retain legacy behavior. Wet=0 retains allpass phase; module bypass is
sample transparent. A separate 4× filter-warp processing-rate error is fixed.
See [design, sources, measurements and limits](NATIVE_DSP_QUALITY.md). The new
coherent PM fixture shows little 4× benefit in that case, so wider antialiasing
work remains open. These are native quality corrections, not a claim of Serum
multiband calibration or whole-engine professional readiness.

## Historical first-slice design

The native engine is still an incomplete instrument rather than a professional-quality final DSP implementation. It reads preloaded mipmapped wavetable frames, runs bounded polyphonic voices, Env 1, typed modulation routes, a small ZYG state-variable filter family and the first oversampled warp subset. No claim of broad Serum filter, oscillator or FX coverage follows from these vertical slices.

| First-slice choice | Why now | Limitation and replacement path |
| --- | --- | --- |
| FFT harmonic mip levels plus cubic phase interpolation and linear frame interpolation | Arbitrary user-owned tables are bandlimited off the audio thread; immutable caches make patch copies bounded | Octave spacing can retain some foldback during level crossfade and omit some valid high harmonics; warp processing needs its own antialiasing |
| Per-note 32-voice fixed array, deterministic oldest-voice steal | Bounded memory/work, sample-accurate note timing from host MIDI offsets | Click-free steal, legato/portamento/MPE and voice stack still required |
| Curved attack/hold/decay/sustain/release Env 1 | Three native bend controls shape onset, decay and release without audio-thread allocation | Curve transfer is ZYG-specific; tempo sync, retrigger modes and Env 2–4 modulation remain open |
| Trapezoidal SVF/one-pole and nonlinear ladder families for both filters | Provides stable low/high/band/notch and MG ladder responses with cutoff/resonance/drive while preserving exact filter ID | Fifteen Normal-family IDs are typed; add distinct ZYG implementations for remaining IDs and verify transfer scales |
| Prepare PCM wavetable on control thread, immutable patch handoff via three slots | No file I/O/allocation/locks in `processBlock`, and no audio-thread deallocation when switching | Later add resource cache, bounded background loader and click-free graph crossfade |

| Area | Candidate approaches | Evaluation required |
| --- | --- | --- |
| Wavetable | Bandlimited mipmapped tables, FFT harmonic truncation, high-quality frame interpolation | Alias level under pitch/warp modulation, frame-transition clicks, table-memory cost, CPU |
| Sample playback | Polyphase or windowed-sinc resampling, efficient high-quality lower-cost mode | Transposition artifacts, loop stability, dynamic pitch modulation, latency |
| Granular | Preallocated grain pool, bounded scheduling, overlap-add windows | Extreme density CPU, clicks, stereo spatial behavior, clock sync, deterministic random seed |
| Spectral | STFT/phase-vocoder or sinusoidal/peak tracking hybrid | Transient preservation, pitch/time independence, phase coherence, latency, CPU |
| Filters | TPT state-variable family, ladder topologies with controlled nonlinearities, explicit comb/allpass/formant/ring/S&H variants | Distinct character per ID, resonance stability, modulation rate, drive aliasing |
| Nonlinear FX/warps | Targeted oversampling and bandlimiting where beneficial | Musical coloration versus aliasing/CPU; bypass and latency behavior |
| Convolution | Partitioned FFT convolution with preloaded IR | Real-time allocations, tail behavior, latency, IR changes |
| Modulation | Typed source graph, audio/control-rate selection per destination, smoothing at edge | Correct polarity/aux/curve semantics, sample accuracy, cycles and bounded execution |
| Voice handling | Preallocated voice pool with deterministic priority/steal/release | Note overlap, legato, MPE, same-note behavior, click-free transitions |

General rules: sample-rate independence, denormal protection, stable extremes, no audio-thread allocation/locks/I/O/logging, bounded CPU, and parameter ramps. Avoid claiming “high quality” without alias/stability/performance tests and listening evaluation. ZYG may use deliberate saturation and coloration where it improves the instrument.

## 2026-09-22 implementation notes

- A patch pointer update no longer clears active voices. This makes held-note OSC/filter/envelope/master edits audible and keeps voice allocation stable. It is not yet a click-free crossfade; abrupt edits or loading a radically different patch can click. A bounded graph transition is still needed.
- OSC A/B/C pan uses equal-power left/right weights. A routed source reaches the filter graph when its destination is Filter; Main and Direct currently bypass it. Filter wet is a dry/wet blend. Resonance and drive affect ZYG's typed state-variable responses; individual Serum transfer functions remain unimplemented.
- Native sine LFO 1 and imported free-running Lorenz, Rössler and random sample-and-hold types are evaluated per active voice at sample rate. The adapter maps type/rate/tempo-sync into native LFO definitions; the DSP never reads Serum keys. Tempo-synced, Path/custom, mode/direction/phase/rise/delay/smooth state and matrix aux/curve/slew remain unsupported. The `amount / 100` cutoff and `amount * 2.56` position scales are provisional native ZYG units, not verified Serum units.
- The output meter, voice count and MIDI indicator use atomics to pass bounded scalar diagnostics from `processBlock` to the editor. No audio-thread logging, allocation, lock or file access was added. The editor's Audition C3 switch sends an atomic held-note request consumed at the next audio block; it is a diagnostic performance aid, not a substitute for validating host MIDI input.

- Native SUB renders typed pulse, rounded-rectangle, saw, square and triangle shapes. NOISE uses a deterministic per-voice white-noise generator when no sample is selected. When a local NOISE WAV/FLAC sample is selected, JUCE decodes it to mono on the control thread with a bounded frame count, then the audio thread loops it with linear interpolation at the recorded sample rate. This is a first playable source path, not a verified Serum noise implementation: stereo imaging, pitch/color, one-shot/retrigger behavior and higher-quality resampling remain open. A private noise-only derivative of the 2.0.24 preset produced nonzero REAPER realtime peaks, while core import without plugin-side FLAC preparation was silent, isolating the new decode/playback path.

- Macro 1–8 values are normalized on import and stored in the independent patch. They feed the same bounded per-voice matrix path as native sine LFO 1 for OSC A wavetable position and Filter 1 cutoff. The provisional modulation amount scale remains a ZYG convention, and source curves, smoothing and most imported destinations are still unsupported.

- The audio engine no longer matches Serum source IDs or parameter strings. The adapter resolves supported imported routes into native source/target kinds and indices before rendering. A core test deliberately corrupts the raw Serum labels while keeping typed route fields and checks that audio is unchanged. This enforces the container → semantic adapter → patch → DSP boundary.

## Filter 1 revision (2026-09-22)

The first one-pole fallback was replaced by a second-order trapezoidal state-variable low-pass using the structure described by [Andrew Simper, Cytomic, *Linear Trapezoidal Integrated State Variable Filter with Low Noise Optimisation*](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf). ZYG uses its own code and provisional cutoff/Q/drive scales; no source code was copied. Cutoff is smoothed over about 5 ms per voice, resonance controls Q from 0.707 to 10, and drive is a bounded tanh input stage. The wet control blends filtered and dry source paths. A core test checks that live filter edits keep held notes sounding, resonance and drive alter audio, and a resonant case remains finite. This is an original ZYG filter behavior, **not** a Serum filter-type emulation; other filter types remain unsupported. The same low-pass structure now also powers Filter 2. The nonlinear drive is not oversampled, so aliasing and CPU at high voice counts still require measurement.

## Oscillator phase randomization (2026-09-22)

Each note starts from the oscillator's editable initial phase plus a deterministic pseudorandom offset whose range is controlled by the native `randomPhase` value from 0–100%. A value of zero retriggers sample-identically; 100% spans a full cycle. The generator is part of the preallocated voice state and performs no allocation or locking. A core render test checks deterministic zero-range retrigger and changed audio at full range. This is a ZYG interpretation of the imported numeric field; exact Serum distribution, phase-memory and legato behavior still require controlled oracle presets.

## Wavetable bandlimiting and curve/LFO DSP (2026-09-22)

- Each complete 2048-sample wavetable frame is transformed once off the audio thread. Ten derived levels successively remove harmonics above 512, 256, …, 1. At render time the oscillator selects from `log2(frameSize * frequency / sampleRate)` and crossfades neighboring levels. Four-point cubic interpolation is used within a frame; adjacent wavetable frames remain linearly interpolated. The derived arrays are immutable and shared by patch copies, so a knob edit does not copy the roughly 10× cache and no cache is built or freed in the audio callback. A deterministic saw test measures the known 20th-harmonic foldback bin below 10% of the raw-table level. This is a targeted regression, not a complete alias/noise/CPU characterization.
- Envelope curve `50` is linear. Values above and below 50 bend the normalized stage with a bounded power curve; release uses the captured note-off amplitude and duration. These are native ZYG curve semantics pending controlled Serum comparison.
- Lorenz uses the standard `(sigma, rho, beta) = (10, 28, 8/3)` system; Rössler uses `(a, b, c) = (0.2, 0.2, 5.7)`. Per-voice Euler state is fixed-size and finite-checked. Imported free-rate `kParamRate` is currently treated as hertz. Beat-synced instances are retained and reported unsupported until host tempo/division behavior exists. The supplied 2.0.24 fixture now drives OSC A wavetable position from its original Lorenz LFO 1 route and renders a changed finite signal.

## EDM wavetable warp and audio-rate modulation slice (2026-09-22)

The first EDM-focused warp slice follows the official Serum 2 manual's architectural distinction between phase remapping, distortion, FM, AM and RM, while using original ZYG transfer functions. The adapter now reads the observed Serum keys `kParamWarpMenu`, `kParamWarpMenu2`, `kParamWarp`, and `kParamWarp2`. The prior `kParamWarpMode` names do not occur in the inspected corpus. Raw IDs remain in provenance; DSP receives only `WarpMode`, source index and normalized depth.

- Implemented phase remaps: Bend +/−/±, Asym +/−/±, PWM and Flip. Implemented nonlinear stages: hard clip, soft clip, sine fold, linear fold, sine shaper, asymmetric clip and rectify. Dual phase maps compose in slot order before lookup and dual amplitude operations process in slot order afterward. Mixed phase/amplitude slot ordering is provisional pending controlled fixtures. These transfer functions are ZYG designs and are not claims of waveform identity with Serum.
- Implemented audio-rate sources: first/second other main oscillator, SUB and NOISE for FM, RM and AM. A source remains active as a modulator when its audible level is zero, matching the architecture described by the official manual. Needed sources are sampled before any carrier advances, making modulation timing independent of oscillator render order. The `OSC`/`OSC2` serialized-source ordering is inferred as the first/second other main oscillator in ascending slot order; this still needs controlled oracle presets.
- ZYG FM integrates instantaneous frequency with a native index of `8 × normalized depth × modulator`; negative instantaneous frequency naturally runs phase backward. RM crossfades from dry carrier to carrier×modulator. AM crossfades to carrier×unipolar-modulator. Modulation is evaluated for every generated subsample. Filter audio sources, FMX/FMP, PD, Self PD, Sync and Remap remain unsupported.
- Any implemented warp selects a 2× oscillator path. A 63-tap Kaiser-windowed FIR low-pass (normalized cutoff 0.45 at the 2× rate, beta 8) decimates back to the host rate. Its measured design response is above 0.95 at normalized frequency 0.40 and below 0.0001 at 0.55. Unwarped oscillators use a matching fixed 15-sample delay so layered SUB and carrier phases remain aligned. The instrument does not yet report or require host latency because this is a deterministic internal note-on delay. 4×/8× modes, adaptive quality and full alias sweeps remain open.
- The supplied 2.0.24 fixture now executes its original Macro 1 → OSC A warp depth route and uses enabled, zero-level OSC B as the audio-rate source for OSC A. Core tests cover that silent-modulator behavior, FIR response, dual nonlinear stability, phase alignment, native state round-trip and finite real-preset rendering.

## Two-filter routing slice (2026-09-23)

The [official Serum 2 routing guide](https://xferrecords.com/web-manual/serum-2/routing-an-oscillator-or-filter) describes five sound sources routing to either filter or a blend and filter outputs routing to the other filter, Main, Direct or None. ZYG now allocates independent per-voice stereo SVF state for both filters, splits each source route using the native `filterBalance` field, and evaluates one-way chains in dependency order. A disabled filter passes its input through. Main and Direct sum to the same output until the FX graph exists. Filter 1's existing typed cutoff modulation remains limited to Filter 1.

The current native balance rule is 0 = Filter 1, 100 = Filter 2, with a linear blend in between. It preserves the previous Filter 1 default, but has not been established as Serum's exact transfer; observed factory values include negative numbers. Simultaneous Filter 1→2 and Filter 2→1 edges have no validated feedback model. The first edge runs and the backward edge falls through to Main, with an import diagnostic. This is a bounded deterministic fallback, not semantic support for a feedback cycle. Filter pan/level, FX sends and envelope-gated edges remain unrendered. Core tests isolate the two filter cutoffs, source balance, one-way chain and None output. No REAPER validation was performed for this slice.

## Normal state-variable filter family (2026-09-23)

The official Serum 2 manual identifies its Normal Low, High, Band and Notch families and distinguishes their slopes. The semantic adapter maps `L6/L12/L18/L24`, `H6/H12/H18/H24`, `B12/B24`, and `N12/N24`. `Patch::FilterResponse` is independent native state, while the exact serialized ID remains provenance. The audio engine does not inspect that ID.

A 6 dB response uses a trapezoidal one-pole stage. The 12 dB responses use low, band and high outputs of the existing trapezoidal SVF, with notch summing low and high. An 18 dB low/high response cascades the SVF and one-pole, while 24 dB cascades two SVFs with separate preallocated state. Drive is applied once before the cascade; wet/dry blending occurs after it. Tests verify distinct low/high and band/notch transfer, ordered 6/12/18/24 low-pass attenuation, finite output, typed-state serialization, and independence from the raw type label. These are ZYG response designs guided by the documented categories. Cutoff, resonance, drive, slope normalization and nonlinear behavior have not been matched to Serum with an oracle. Multi, morph, comb, flange, phase, formant and special filters remain unsupported.

## MG ladder filter slice (2026-09-23)

The official manual describes MG Low 6/12/18/24 as ladder low-pass filters and identifies FAT as saturation in the resonance signal path. The observed corpus has unambiguous serialized IDs for `MgL6`, `MgL18`, and `MgL24`; those three now map to typed native ladder responses. No `MgL12` ID occurs in the inspected corpus, and the unsuffixed `LadderMg` is deliberately not guessed to be the missing slope.

ZYG's ladder is four cascaded trapezoidal one-pole integrators with taps after stages one, three and four. A bounded `tanh` feedback path provides resonance, `Var` increases saturation in that path, and the existing drive stage processes the ladder input. Every state is preallocated per voice, filter and channel. The design is stable under the tested parameter extremes and produces ordered 6/18/24 dB attenuation; resonance and FAT produce measurable finite changes near cutoff. It is an original ZYG ladder response, not a transfer match for Serum or a circuit emulation. The three IDs add 96 active corpus instances, bringing typed filter coverage to 479/859. Exact cutoff/resonance/FAT scales, aliasing under heavy drive, `LadderMg`, `LadderEMS`, `LadderAcid` and `DirtyMg` remain open.

## Matrix source and destination expansion (2026-09-23)

The adapter now represents Serum source IDs 2–5 as Env 1–4, 16 as velocity and 17 as note. DSP evaluates Env 1, velocity and normalized MIDI note per voice; Env 2–4 remain typed but inactive until their independent envelope generators exist. Unipolar values become `2x-1` only for a bipolar route. The same rule now applies to macros, correcting their earlier bipolar omission. Existing LFO polarity behavior is retained.

Wavetable-position routes now address Oscillators A, B or C, and cutoff routes address Filter 1 or 2. Warp destinations already addressed all three wavetable oscillators. Fixed-size offset arrays keep evaluation bounded and allocation-free. Synthetic renders cover velocity→OSC B position, Env 1→OSC B position and note→Filter 2 cutoff. Imported route tests verify the typed source mapping. Amount scales remain provisional ZYG units, and Serum source/aux curves, slew, delay, output scaling and most destinations still require implementation and controlled verification.

## SUB waveform family (2026-09-23)

The factory corpus exposes five explicit `SubOsc4.kParamShape` values: `kPulse`, `kRoundRect`, `kSaw`, `kSquare`, and `kTriangle`. The adapter maps these to independent `SubShape` values. DSP uses polyBLEP edge correction for pulse, saw and square, a bounded sine-based rounded rectangle, and an analytic triangle. The same typed waveform is used when SUB acts as an audible source or audio-rate modulator. Unknown or absent shape state retains the prior native sine fallback without claiming a Serum default.

Adapter and render tests cover all five mappings, distinct output, and native-state serialization. These are original ZYG waveforms designed for clean electronic bass layers; they are not waveform matches to Serum. `kParamContiguousPhase`, exact initial-phase behavior, pitch tracking, zones and oracle comparison remain open.


## DSP completion rebuild (2026-09-26) — supersedes earlier per-slice notes where they conflict

Audit outcome: the four-layer split (container decoder → semantic adapter → independent `Patch` → DSP) was sound and is kept; DSP never reads raw Serum strings. The old engine was too narrow (one filter, one envelope, a handful of destinations), so it was rebuilt around a generic parameter/modulation system. Code lives in `src/dsp/` (`Filters`, `Modulators`, `Oscillators`, `Spectral`, `Warp`, `Fft`, `FxEngine`, `Sequencer`, `DspCommon`), `src/FxParams.*`, `src/Assets.*`, `src/SynthEngine.*`. All of it is original ZYG DSP; nothing is derived from Xfer code or assets.

### Engine structure
- 32 voices; modulation and parameter resolution every 8 samples, render chunks of 256. Per voice: 4 envelopes, 10 LFOs, 5 oscillators, 2 filters. Global-scope destinations (macros, FX, arp, clip, LFO point buses) are evaluated once per chunk.
- Every modulatable parameter has a flat destination index (osc 0, filter 320, env 352, LFO 384, macro 464, global 472, routing 488, arp 516, clip 524, bus 532; 548 total). Offsets are stored as **normalized deltas** and mapped through `applyModulation(base, delta, ParamRange{lo,hi,log})`. Time and frequency ranges are log-domain (a linear delta previously turned a 20 ms attack into 6 s). Range tables are ZYG choices, not Serum's.
- Audio-rate sources (osc/SUB/NOISE/filter output as FM/RM/AM/PD sources) read a per-sample snapshot of the previous sample's outputs, so results do not depend on render order.
- No allocation, locks or I/O on the audio thread; enforced by a test that counts global `operator new` while rendering with every module active. Assets (samples, SFZ, IRs, spectral analysis, mip tables) are decoded on the control thread by `prepareAssets` (`src/Assets.*`) with a decoder injected by the plugin.
- Output has an on-by-default soft safety limiter (0.8 → 1.0 ceiling); the old fixed 0.25 gain was removed. Steals use a 96-sample declick tail.

### Oscillators
Wavetable and SUB share one path: FFT mip levels, cubic interpolation, unison (detune modes, stack, blend, WT-position/warp spread, phase controls), and a 2×/4× oversampled warp path through a 63-tap FIR. Warp library: bend, asym, PWM, flip, sync, remap, quantize, even/odd, self-PD, FM/FMX/FMP/PD/RM/AM, filter-type warps and distortion shapers. Sample (loop modes, crossfade, reverse, random start, slicing with markers or onset detection), multisample (SFZ regions, layers, velocity/key selection, envelope override/SFZ release, timbre tilt), granular (64-grain pool, window LUT, linear reads, randomization, Start..End), spectral (phase-vocoder STFT 2048/512, 15+ spectral warps, vocode/mask, magnitude scale 0.75/sqrt(ratio)) and noise (white/pink/brown/Geiger/color/sample-backed one-shot). The default table is `S2 Tables/Default Shapes.wav` when none is stored. Contiguous phase (`perVoicePhase=false`, SUB `contiguousPhase`) starts from the last started voice's phases.

### Filters
About 80 Serum IDs map through `filterFromSerumId` (adapter side) to native families: SVF/one-pole slopes, MG ladder (+12/dirty/EMS/acid), multimode morphs, comb, flange, phaser, formant, allpass, diffuser, DJ mixer, shelf EQ, pole-zero, exponential, screamer, waveshaper, ring-mod, sample-hold, add-bass, ZDF analog, dist-comb, filter-reverb, band-reject. `fastTanh` in hot loops; drive is normalized by 1/sqrt(gain).
**Routing balance** is now −100..100: w1=(100−bal)/100, w2=(100+bal)/100 (both clamped), so −100 = Filter 1, 0 = both, +100 = Filter 2. This replaces the earlier 0..100 rule; native JSON migrates old values through `filterBalanceScale`. Route default −100. Per-route `viaEnv[4]` multiplies the slot gain by the selected envelopes. Buses: main, direct, bus 1, bus 2; `fxBus1Dest==2` sends bus 1 into bus 2.

### FX (13 effects + 3 splitters)
bode, chorus, comp (single/multiband, slow AGC, makeup capped, thresh 0 = 0 dBFS, thresh = −60·x dB), conv (partitioned FFT, unit-energy IR; a built-in synthetic decaying room IR when no impulse is selected), delay, distortion (17 modes, 2× oversampled, half gain compensation), eq, filter, flanger, hyperD, phaser, reverb (FDN, 4 types), utils; splitters `split`, `split3`, `splitMS` nest by module counts. Slot tables and log flags live in `FxParams`. **All FX defaults, ranges, levels and curves are provisional ZYG choices**; a modulator with no signal bypasses mask FX.

### Performance/sequencing/voice behavior
Portamento, mono/legato, note priority, sustain pedal, note latch, per-channel bend/pressure/CC74, arp (many shapes, gate, chance, swing, repeats, chord) and clip player (OneShot/Pendulum/Random/Static, span modes) are implemented. **Velocity affects amplitude only through `globals.velocityAmpDepth` (default 0) or matrix routes.**

### Unverified assumptions (no Serum oracle exists here)
Exact Serum transfer functions for filters, warps, FX and envelopes/LFO curves (bend k = 4.4·b is ZYG's); tempo-sync semantics (stored rates are read as Hz/seconds; beat-sync flags are flagged not-rendered); quantizer scale ids; unison range default (2 semitones); oversampling level meanings; slicing root note 36; source IDs 39–44 and 47/48 treated as extra note-on random sources (a guess); `Env#.kParamLegatoInverted` and BeatSync not rendered; sub-oscillator/noise levels and every FX default. Compatibility here means *plausible sonic parity by original DSP*, not identical output.

## ZYG extensions beyond Serum 2 (2026-09-26)

Target genres: dubstep/riddim, hard techno, complextro, electro. Their sound design revolves around (1) rhythmic gain shaping locked to the kick, (2) glitch/stutter edits and (3) resampling loops of a processed bass. Serum 2 has none of these as built-ins (producers reach for Kickstart/LFO Tool, beat-repeat plug-ins and bounce-in-place). Three original features were added; none uses Serum data, so they have no Serum-fidelity caveats. They are ZYG-native only and are not produced by the Serum importer.

1. **PUMP effect (`FxType::pump`) + host sidechain input + SIDECHAIN modulation source.** Volume ducker with four triggers: TEMPO (locked to the host beat clock; the clock free-runs when the transport is stopped so it also works while auditioning), NOTE (restarts on every note-on, including arp/clip notes), SIDECHAIN (retriggers the curve when the external input crosses a threshold; 40 ms hold-off) and FOLLOW (classic level-driven ducking). Curve: `depth`, `length` (beats), `shape` (recovery power curve k=2^(shape/40)), `hold`, `attack`; the same `fx::pumpDuck` function draws the editor display. The plug-in exposes an aux stereo input bus "Sidechain" (disabled until the host routes it; REAPER pins 3/4). `ModSource::sidechain` is an envelope follower of that input (fast attack, ~2 s release constant) usable anywhere in the matrix, e.g. kick -> wobble depth. Without a sidechain signal the follower reads 0 and the pump's sidechain/follow modes pass audio unchanged.
2. **STUTTER effect (`FxType::stutter`).** Tempo-sized beat repeat: it captures the last `length` beats of the input and loops it with `pitch` (rate), `gate`, `falloff` (per-repeat gain loss), `reverse` and a `smooth` crossfade. GATE mode is switched by the `active` parameter (route a macro/LFO/velocity to it in the matrix for rhythmic edits); AUTO mode re-captures at every grid cell with probability `chance` (deterministic RNG). Loop memory is preallocated in `prepare()`. `active` is a toggle in the editor, so it is automated through the matrix (destination "STUTTER ACTIVE"), not by drag-and-drop.
3. **RESAMPLE.** TopBar MENU -> RESAMPLE OUTPUT -> INTO OSC A/B/C -> 1 beat / 1-2-4 bars. The audio thread records the final stereo output starting at the next note-on into a buffer allocated at arm time; a 10 Hz message-thread timer writes a 24-bit WAV to `~/.local/share/ZYG-ZXG/Resampled/` and loads it into the chosen oscillator as a pitch-tracked sample rooted on the played note. Because the reference is a file path, native presets keep it. Resampling records post-FX output, so distortion/pump/stutter are baked in.

Related engine fixes: `setTransport` no longer overwrites the beat clock while the host is stopped. `FxUnit::ctx` gives units the tempo, beat position, note counter and sidechain pointers for the current chunk. Tests: pump (tempo/shape/hold/note/sidechain/follow), stutter (loop, reverse, gate, falloff, auto chance), engine sidechain->source and pump end-to-end, stopped-transport clock, JSON round-trip, zero-allocation render with the new units, plus `resample_tests` (full arm->record->save->load cycle).

## Voice-steal envelope restart and display feed (2026-09-27)

- `Envelope.restartOnSteal` (Serum `kParamVoiceStealRestart`, default on): when a still-sounding voice is re-used (poly steal or mono non-legato retrigger) and the flag is off, each envelope re-enters attack from `max(start, current level)` instead of from `start`. Inferred from the parameter name and observed default (only explicit 0 values occur in the corpus); not oracle-verified. Test: `core_tests` mono retrigger keeps >4x the level with the flag off.
- Editor feed: after each render chunk the engine publishes OSC A–C modulated WT position and warp amounts of the newest held voice through relaxed `std::atomic<float>` stores (`SynthEngine::oscDisplayState`); the processor mirrors mono output into an 8192-sample ring of relaxed atomics (`copyScope`). Both are lock- and allocation-free; torn reads only blur one display frame.
- Tuning switches (`kParamNoteCurveApplyTuning`, `kParamPitchExpressionApplyTuning`) are typed but inert: ZYG has no microtuning tables, so equal temperament is used and a diagnostic is raised only when a switch is on. `kParamS1Compatibility` is typed and always diagnosed as not rendered.

## Controlled Serum reference calibration (2026-10-17)

The historical “no oracle” assumptions above are superseded only where measured in [SERUM_ORACLE_MEASUREMENTS.md](SERUM_ORACLE_MEASUREMENTS.md). Native `Patch` stores optional envelope curve families, time units, gain powers, filter-frequency bounds, table interpolation/bin position, detune law/polarity/phase and distortion gain settings. The Serum adapter selects measured values; the engine remains independent and has no proprietary implementation dependency. Existing native presets default to their prior behavior.

LFO sync maps the measured 43-position host grid into quarter-note durations. Empty imported LFO curves become a triangle, while empty native curves keep the native fallback. Scalar source curves use existing allocation-free curve evaluation; imported velocity maps round to MIDI steps. Nonlinear scalar bend and legato interpolation are not calibrated; scalar legato with portamento is diagnosed. Matrix aux curves are still not applied. The overdrive change fits small-signal gain while retaining the original waveshaper; it is not an exact distortion algorithm match.

Complete-preset measurements remain necessary: matching oscillator/envelope level does not prove filter/FX/modulation character or spectral similarity. Private assets and reference audio are never build or release inputs.

## JUCE 8 integration and further reference measurements (2026-10-18)

The independent engine remains block based. JUCE `processBlock` passes raw output pointers through a bounded host-control bridge; `prepareToPlay` prepares resources and smoothing, and `ScopedNoDenormals` guards processing. APVTS initially exposes eight additive performance offsets so zero automation leaves imported patch values intact. Table/frame ramps are consumed per sample; the existing eight-sample filter/matrix control quantum remains. State format 3 stores APVTS separately from the independent patch. See `JUCE8_AUDIO_PIPELINE.md` for ownership, publication and remaining limits.

Mip selection now keeps both interpolation levels band limited for the playback frequency. All six original SUB primitives have prepared mipmaps. Actual fundamentals at or above Nyquist are rejected without rejecting a bass just because a warp bandwidth estimate is large. This improves tested stationary cases; extreme nonlinear modulation still needs spectral measurements and oversampling work.

Imported single-band compressors select an optional native family: fitted equivalent threshold gain `3.3 * ((1-control)^3 + 1e-6)`, measured makeup travel and approximately 90.091 ms default attack/release with ratio 4. The fitted gain includes ZYG's own signal staging and is not a claim about Serum's internal implementation. Original RMS detection and compression remain our DSP. Multiband retains its provisional native behavior and does not inherit this fit. Aggregate controller defaults use mixed domains and are reference observations, not a replacement parameter table. Matrix aux curves remain preserved but unevaluated and are now diagnosed explicitly.

## Time-effect attack/tail correction (2026-10-18)

New time-effect modules use fully wet endpoints and filtered first delay taps. Reference mode IDs map into the existing native stereo enum. Hall decay's observed UI/state conversion selects the original FDN's T60 target, independently of pre-delay. Hall low/high-cut directions now follow their documented meaning; frequency curves, intermediate mix law, early reflections and other reverb types remain provisional. Declared sparse defaults are applied only after checking the fields' domains. See `SERUM_ORACLE_MEASUREMENTS.md` for measured values and remaining sync limitations.

`legacyTimeFx` keeps older native state on the historical model and bakes its implicit reverb defaults before applying the new default table. New modules and reimported Serum presets use the corrections. Reverb fractional-rate LFOs now have independent phase accumulators, eliminating the common-wrap jump in either model. Corrected size/pre-delay changes slew without allocation; buffers remain prepared outside the audio callback. This is original DSP repair and behavioral mapping, not proprietary algorithm reconstruction.

## Native ladder feedback stability (2026-10-18)

The reported persistent high-pitched tails were reproduced with local assets
resolved and isolated to FX ladder filters. The original stage cascade fed back
an integrator's stored state, adding effective loop phase lag. At high cutoff,
moderate resonance and high FAT this could sustain a numerical limit cycle after
the input stopped. The native cascade now closes negative feedback around its
current-sample outputs using a monotone scalar solve: safeguarded Newton steps
inside the saturator's bounded input interval, with at most 12 iterations and no
allocation. Existing stage nonlinearities, slope taps and family voicing remain.
This is an original ZYG numerical correction, with no binary-derived DSP code.

Synthetic impulse-tail regressions cover 44.1/48/96 kHz, 1/10/20 kHz cutoff,
moderate resonance and high Var. The existing resonance test checks a boost above
unity and over three times the zero-resonance response instead of preserving an
arbitrary amplitude threshold from the delayed-feedback model. Resonant response
therefore remains intentional; the tests do not establish matching Serum filter
transfer functions or stability under every time-varying parameter sequence.

An omitted Serum FXFilter resonance now imports as zero, based on the previously
measured controller defaults. Explicit imported values and existing native
defaults remain intact. This correction is separate from the feedback solver.

## Splitter subtree bypass (2026-10-18)

Controlled reference observations establish graph semantics: whole-splitter
bypass preserves input and skips descendants; branch bypass skips child effects
but retains the split audio. ZYG implements these rules in its existing native
recursive rack, retaining its own crossover and M/S equations. Appended Boolean
parameter slots preserve existing native slot identities and JSON state. Branch
offsets use a Boolean threshold; their reference modulation behavior is uncalibrated.

Child counts are finite, bounded and clipped per branch to the remaining parent
span before traversal. This prevents malformed nested counts from consuming a
sibling or a stale module after rebinding. Prepared buffers and fixed pools
remain unchanged; processing and binding add no allocations. Beyond two processed
nesting levels or the splitter pool, a whole subtree passes through transparently
instead of erroneously turning its children into a serial chain. Control-thread
import diagnostics expose these limits, clipped topology and total module capacity.
Solo and splitter output gain remain unsupported and preserved. Structural child
counts cannot be modulated while a patch is bound; imported routes targeting
those counts or unapplied splitter output gain remain unresolved explicitly.

## Measured delay target timing (2026-10-19)

Original-signal reference tests establish a fourth-power time coordinate and a
12-position beat grid; see `SERUM_ORACLE_MEASUREMENTS.md`. The adapter selects
the native `fourthPowerBeatGrid` timing model, including the measured free-time
encoding offset and linked-state canonicalization. Raw source state is retained.
Runtime target composition remains beats / current host beats-per-second,
per-side offset, then Link. Native feedback/filter/interpolation and smoothing
remain independent. No reference implementation was translated.

Missing native `delayTimingModel` loads as `seconds`, preserving already saved
projects even when they previously selected the corrected full-wet model.
The new model uses a dedicated power-4 `ParamRange` for time modulation while
keeping the existing parameter slots and physical stored values. Matrix offsets
are still normalized deltas; reference modulation transitions/depth are not
claimed calibrated. Reverb pre-delay sync is unchanged and unsupported.

Prepare allocates 32 seconds of float delay storage per channel, using the
existing ring's power-of-two capacity. This costs 64 MiB across four stereo
delay units at 48 kHz, versus 4 MiB previously. Targets beyond 32 seconds clamp
without changing the host BPM; a control-thread import diagnostic states the
capacity limitation. With ordinary offsets this supports the entire static
grid down to 45 BPM. Processing/binding do not allocate, lock, log or perform
file I/O. Existing JUCE host-tempo delivery supplies the current BPM.
