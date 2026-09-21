# DSP decisions

No native DSP engine exists yet. No algorithm has been selected as final. Decisions below are research constraints and candidates; update them with measurements and listening results when implemented.

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
