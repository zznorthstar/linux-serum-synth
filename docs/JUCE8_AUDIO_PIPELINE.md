# JUCE 8 audio pipeline

The project already pins JUCE **8.0.9**. The 2026-10-18 work extends the
functional plugin integration; it adds no editor, layout or GUI implementation.
The independent preset and DSP layers remain usable by offline test tools.

## Ownership and processing

- Decoder → Serum semantic adapter → independent immutable `Patch` → native DSP.
  File decoding, spectral analysis, convolution IRs and wavetable mipmaps are
  prepared on the control side, never by the render callback.
- `prepareToPlay(sampleRate, samplesPerBlock)` drains pending publications while
  processing is stopped, prepares DSP pools and resets JUCE linear smoothers.
- `processBlock(AudioBuffer<float>&, MidiBuffer&)` starts with
  `juce::ScopedNoDenormals`, reads cached atomic host values and passes left/right
  write pointers into the block engine. MIDI timestamps split renders at their
  sample positions. No ValueTree access, file I/O or patch cloning occurs there.
  Unsupported long MIDI messages are skipped before constructing `MidiMessage`,
  avoiding heap allocation for SysEx that the engine does not consume.
- `PatchExchange` permits one unacknowledged publication. Later edits coalesce
  on the control side. The audio thread binds a borrowed immutable pointer and
  acknowledges it only after binding; shared ownership/destruction stays off
  the audio thread. The control timer retries a coalesced update. A control-side
  snapshot returns the newest queued edit, including before the next callback.
  A second structural edit can wait for the control timer's retry; APVTS
  automation bypasses this structural queue.
- LFO definitions are referenced from that bound patch. Binding must not copy
  their vectors; a regression test counts allocations across drawn-LFO binds.
  A new preset identity releases old voices. Live multisample-map edits check
  cached region indices against the current immutable map before reading it.

## First APVTS controls

`HostParameters` owns APVTS and caches its raw atomic pointers at construction.
These are additive performance controls over the loaded patch, with zero
defaults; they do not rewrite imported parameters or replace matrix modulation.
This choice preserves measured preset output with no automation and lets the
same host lanes work across presets. It is the first functional binding set,
not a claim that every synth parameter has migrated to APVTS.

| Stable ID | Domain | Effect before matrix modulation |
| --- | --- | --- |
| `filter1CutoffOffset`, `filter2CutoffOffset` | −1..1 | Offset in normalized cutoff-control travel |
| `oscAPositionOffset`, `oscBPositionOffset`, `oscCPositionOffset` | −1..1 | Offset across the full wavetable position range |
| `oscAFrameOffset`, `oscBFrameOffset`, `oscCFrameOffset` | −255..255, integer targets | Offset by actual table frames |

JUCE `LinearSmoothedValue` ramps host targets over 10 ms. Fixed 256-sample
scratch streams are filled without allocation. Wavetable motion consumes each
smoothed sample; cutoff and matrix evaluation retain the existing 8-sample
control quantum, including the filter's existing smoothing. Splitting MIDI does not
restart the smoother. A zero-input fast path retains the original rendering
cadence and audio. Stepped imported tables intentionally remain stepped;
smoothing a position does not imply changing the preset's interpolation mode.
Frame offsets use each oscillator's real frame count and endpoint/bin convention.
Position and frame offsets add together and clamp at the table limits.

Host state version 3 stores the independent patch plus APVTS's ValueTree.
Versions 1/2 remain readable and reset the new host offsets to zero. APVTS
`copyState`/`replaceState` run only on the state/control side: JUCE documents
them as thread-safe but not real-time safe. Native `.zygpreset` remains the
independent patch format; these host performance lanes are host session state.
State restoration requests an atomic audio-side smoother reset. Loading state
before or after `prepareToPlay` must start at the saved targets; ordinary host
automation still ramps. No state-thread write touches a live smoother.

## Bandwidth and remaining limits

Wavetable harmonic mipmaps are built off-thread and crossfaded by pitch during
lookup. Crossfades select two alias-safe levels; mixing the preceding brighter
level would reintroduce harmonics above Nyquist. All six original SUB primitives
have prepared mipmaps, including triangle and rounded waveforms. Warped oscillators and distortion
already have a 2× FIR decimation path. The existing high-pitch saw test checks
foldback suppression; a high-pitch rounded-SUB test requires its folded seventh
harmonic below −80 dB relative to the fundamental. FIR tests check passband and stopband response. These
tests do not establish alias-free output for arbitrary extreme FM/phase warp,
all sample modes or every distortion setting. Higher-quality oversampling and
aggressive-modulation spectral measurements remain work items.

The oscillator rejects actual fundamentals at/above the base-rate Nyquist
frequency, including SUB, rather than folding them into a lower audible note.
It does not reject a bass merely because a phase warp's conservative bandwidth
estimate is large. The CLAP wrapper dispatches parameter events at one-sample
resolution; smoothing and the engine's existing control quantum still apply.

The current sidechain scratch allocation handles the prepared block size
(at least 8192 samples). An unexpectedly larger host block currently disables
sidechain for that callback; the output/render path remains bounded. This is
a known host-routing limit, not a validated behavior under every host.

References: [APVTS API](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html),
[SmoothedValue API](https://docs.juce.com/master/classjuce_1_1SmoothedValue.html).
Implementation was checked against the locally pinned JUCE 8.0.9 headers.

Content-folder settings are read at instance creation/asset preparation, never
inside `processBlock`. The existing content-folder action now remembers a path
in the user's ZYG configuration folder. Windows checks that setting and its
native app-data content location in addition to the existing environment/Linux
fallbacks. Invalid/stale settings return no root and assets remain diagnosed;
no substitute content is silently fabricated. Developer host-smoke overrides
do not overwrite the user's saved folder.
