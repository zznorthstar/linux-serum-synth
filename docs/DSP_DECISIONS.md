# DSP decisions

The first native engine exists as a **proof of pipeline**, not a professional-quality final DSP implementation. It reads preloaded wavetable frames, runs bounded polyphonic voices, Env 1 and a single low-pass fallback. These choices are deliberately temporary; no claim of Serum filter-type coverage or high-quality antialiasing follows from them.

| First-slice choice | Why now | Limitation and replacement path |
| --- | --- | --- |
| FFT harmonic mip levels plus cubic phase interpolation and linear frame interpolation | Arbitrary user-owned tables are bandlimited off the audio thread; immutable caches make patch copies bounded | Octave spacing can retain some foldback during level crossfade and omit some valid high harmonics; warp processing needs its own antialiasing |
| Per-note 32-voice fixed array, deterministic oldest-voice steal | Bounded memory/work, sample-accurate note timing from host MIDI offsets | Click-free steal, legato/portamento/MPE and voice stack still required |
| Curved attack/hold/decay/sustain/release Env 1 | Three native bend controls shape onset, decay and release without audio-thread allocation | Curve transfer is ZYG-specific; tempo sync, retrigger modes and Env 2–4 modulation remain open |
| One-pole low-pass for enabled Filter 1, regardless of serialized type | Keeps a first audible filter path while preserving exact filter ID in patch diagnostics | **Not semantic filter compatibility.** Replace with distinct ZYG implementations for all observed IDs; do not hide fallback |
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
- OSC A/B/C pan uses equal-power left/right weights. A routed source reaches the one-pole filter only when its destination is Filter; Main and Direct currently bypass it. Filter 1 wet is a dry/wet blend. Resonance, drive and individual Serum filter transfer functions remain unimplemented, so the current filter is explicitly a ZYG fallback.
- Native sine LFO 1 and imported free-running Lorenz, Rössler and random sample-and-hold types are evaluated per active voice at sample rate. The adapter maps type/rate/tempo-sync into native LFO definitions; the DSP never reads Serum keys. Tempo-synced, Path/custom, mode/direction/phase/rise/delay/smooth state and matrix aux/curve/slew remain unsupported. The `amount / 100` cutoff and `amount * 2.56` position scales are provisional native ZYG units, not verified Serum units.
- The output meter, voice count and MIDI indicator use atomics to pass bounded scalar diagnostics from `processBlock` to the editor. No audio-thread logging, allocation, lock or file access was added. The editor's Audition C3 switch sends an atomic held-note request consumed at the next audio block; it is a diagnostic performance aid, not a substitute for validating host MIDI input.

- Native SUB currently uses a sine oscillator; NOISE uses a deterministic per-voice white-noise generator when no sample is selected. When a local NOISE WAV/FLAC sample is selected, JUCE decodes it to mono on the control thread with a bounded frame count, then the audio thread loops it with linear interpolation at the recorded sample rate. This is a first playable source path, not a verified Serum noise implementation: stereo imaging, pitch/color, one-shot/retrigger behavior, higher-quality resampling and other SUB shapes remain open. A private noise-only derivative of the 2.0.24 preset produced nonzero REAPER realtime peaks, while core import without plugin-side FLAC preparation was silent, isolating the new decode/playback path.

- Macro 1–8 values are normalized on import and stored in the independent patch. They feed the same bounded per-voice matrix path as native sine LFO 1 for OSC A wavetable position and Filter 1 cutoff. The provisional modulation amount scale remains a ZYG convention, and source curves, smoothing and most imported destinations are still unsupported.

- The audio engine no longer matches Serum source IDs or parameter strings. The adapter resolves supported imported routes into native source/target kinds and indices before rendering. A core test deliberately corrupts the raw Serum labels while keeping typed route fields and checks that audio is unchanged. This enforces the container → semantic adapter → patch → DSP boundary.

## Filter 1 revision (2026-09-22)

The first one-pole fallback was replaced by a second-order trapezoidal state-variable low-pass using the structure described by [Andrew Simper, Cytomic, *Linear Trapezoidal Integrated State Variable Filter with Low Noise Optimisation*](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised.pdf). ZYG uses its own code and provisional cutoff/Q/drive scales; no source code was copied. Cutoff is smoothed over about 5 ms per voice, resonance controls Q from 0.707 to 10, and drive is a bounded tanh input stage. The wet control blends filtered and dry source paths. A core test checks that live filter edits keep held notes sounding, resonance and drive alter audio, and a resonant case remains finite. This is an original ZYG filter behavior, **not** a Serum filter-type emulation; other types and Filter 2 remain unsupported. The nonlinear drive is not oversampled, so aliasing and CPU at high voice counts still require measurement.

## Oscillator phase randomization (2026-09-22)

Each note starts from the oscillator's editable initial phase plus a deterministic pseudorandom offset whose range is controlled by the native `randomPhase` value from 0–100%. A value of zero retriggers sample-identically; 100% spans a full cycle. The generator is part of the preallocated voice state and performs no allocation or locking. A core render test checks deterministic zero-range retrigger and changed audio at full range. This is a ZYG interpretation of the imported numeric field; exact Serum distribution, phase-memory and legato behavior still require controlled oracle presets.

## Wavetable bandlimiting and curve/LFO DSP (2026-09-22)

- Each complete 2048-sample wavetable frame is transformed once off the audio thread. Ten derived levels successively remove harmonics above 512, 256, …, 1. At render time the oscillator selects from `log2(frameSize * frequency / sampleRate)` and crossfades neighboring levels. Four-point cubic interpolation is used within a frame; adjacent wavetable frames remain linearly interpolated. The derived arrays are immutable and shared by patch copies, so a knob edit does not copy the roughly 10× cache and no cache is built or freed in the audio callback. A deterministic saw test measures the known 20th-harmonic foldback bin below 10% of the raw-table level. This is a targeted regression, not a complete alias/noise/CPU characterization.
- Envelope curve `50` is linear. Values above and below 50 bend the normalized stage with a bounded power curve; release uses the captured note-off amplitude and duration. These are native ZYG curve semantics pending controlled Serum comparison.
- Lorenz uses the standard `(sigma, rho, beta) = (10, 28, 8/3)` system; Rössler uses `(a, b, c) = (0.2, 0.2, 5.7)`. Per-voice Euler state is fixed-size and finite-checked. Imported free-rate `kParamRate` is currently treated as hertz. Beat-synced instances are retained and reported unsupported until host tempo/division behavior exists. The supplied 2.0.24 fixture now drives OSC A wavetable position from its original Lorenz LFO 1 route and renders a changed finite signal.
