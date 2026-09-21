# DSP decisions

The first native engine exists as a **proof of pipeline**, not a professional-quality final DSP implementation. It reads preloaded wavetable frames, runs bounded polyphonic voices, Env 1 and a single low-pass fallback. These choices are deliberately temporary; no claim of Serum filter-type coverage or high-quality antialiasing follows from them.

| First-slice choice | Why now | Limitation and replacement path |
| --- | --- | --- |
| Linear interpolation within/across 2048-sample wavetable frames | Small, deterministic, no allocation on audio thread; tests the real asset → oscillator path | Aliases at high pitch and during warp; add precomputed harmonic-limited mip levels and verified frame geometry |
| Per-note 32-voice fixed array, deterministic oldest-voice steal | Bounded memory/work, sample-accurate note timing from host MIDI offsets | Click-free steal, legato/portamento/MPE and voice stack still required |
| Linear attack/hold/decay/sustain/release Env 1 | Allows playable phrasing and explicit sample-rate dependence | Serum curves/sync/retrigger and other envelopes not rendered; smooth live parameter changes later |
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
