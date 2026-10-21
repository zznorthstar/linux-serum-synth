# Embedded sample audio (2026-10-19)

The supplied musician corpus exposed a spectral oscillator with audio stored
inside the preset rather than at an external factory path. The adapter had
ignored `embedded_audio`, leaving that oscillator without a prepared sample.
Other modulated sources still made the preset audible. This was an unsupported
payload path, not missing factory content.

## Conversion and native state

Sample, granular and spectral imports now decode `embedded_audio.data` as one
or two planar numeric channels. A valid embedded rate takes precedence; if
absent, the parent sample rate is used. An explicitly invalid rate is rejected.
Actual channel lengths determine allocation, independently of parent
`numFrames`/`numChannels`. Valid embedded data takes precedence over external
paths, which remain preserved in Serum provenance. Successful preparation uses
immutable native `SampleData`; it does not dispatch proprietary DSP.

Native `embeddedSample` stores an independent versioned format:
`planar-f32le-base64-v1`, explicit frame count, sample rate, root note and one
or two float32 little-endian channels. Pathless native sample-family buffers
are also saved this way. External path-backed caches remain references.
Playback does not need the original Serum bytes, oscillator raw state or
unknown-state sidecar. Spectral analysis remains a derived cache, rebuilt by
control-thread asset preparation after recall. The existing analysis uses a
mono mix of stereo audio and at most 28 seconds; this change does not revise
spectral synthesis or establish reference stereo/warp character.

Bounds are 2–4,194,304 frames per channel, one/two equal-length channels and a
finite positive rate <=768 kHz. Those rate bounds are validation limits, not
a certification of every rate. Nonnumeric/nonfinite/overflowing samples are
rejected. Native decoding additionally checks encoding, integer frame bounds,
root note, exact byte count, alphabet and canonical base64 padding before
allocating the channel. Finite amplitudes above unity are retained. Rejected
imports preserve source state and report `unsupported_asset` without duplicate
missing-reference messages. Invalid native state fails load rather than silently
truncating or replacing audio.

Decoding, serialization and spectral analysis allocate on the control thread;
prepared playback only reads immutable buffers. Tests include allocation-free
patch binding/note/render for all three sample families. No GUI/layout code,
factory replacement pack or proprietary implementation was added.

## Evidence and limits

Nine original-signal cases run through licensed Serum 2.1.5 in an owned
REAPER7.82 instance and through the native renderer. Mono/stereo samples at
24/48/96 kHz produce sample-identical inline-versus-WAV renders **within each
instrument**. A deliberately conflicting parent frame/channel count also makes
no difference in either instrument. This verifies payload ordering/rate/loading;
it does not claim that Serum and ZYG waveforms match each other.

Core tests cover malformed channels, rates, numeric samples and native blobs,
stereo float preservation, precedence over a missing external path, independent
native recall and derived spectral preparation. The first malformed-data test
compared NaN-containing JSON using equality; since NaN is unequal to itself,
the preservation assertion was corrected to compare serialized trees. The
decoder correctly rejected the bad audio throughout.

Windows VST3/CLAP/tools build and all8 C++ suites pass (133 DSP checks), with
Python interoperability7/7. Eight fresh-plugin installed-VST3 host cases verify
sample identity, repeat/import equality and exact native recall without original
Serum bytes or oscillator raw/unknown sidecars. Source-removal controls change
audio at both notes. An initial reused-processor comparison advanced random-note
history despite identical typed state; fresh processors plus unchanged-repeat
controls remove that confound. Owned projects were saved and hosts exited0;
the user project was untouched. CLAP was built but not host-tested this pass;
Linux was not rerun.

All 24 musician-preset renders remain finite/audible with zero missing assets.
Only the affected preset's two renders change; the other22 are sample-identical
to the previous checkpoint. Its ZYG-minus-Serum RMS changes -7.45 to -6.94 dB at
MIDI36 and -.69 to -.58 dB at MIDI60. Envelope correlations change .843 to .871
and .98470 to .98455 respectively. Restoring the source does not finish its
sound calibration. Compressor/legacy/filter/warp/modulation differences remain.

Noise, multisample child audio, convolution IR payloads, compressed/binary
embedded formats and more than two channels are outside this implementation.
Older native saves without `embeddedSample` are not repaired by consulting opaque
Serum state during playback; reimport the original source preset. Private
presets, reference states, generated test files and audio stay outside Git under
`../oracle-private/embedded-audio-20261019/`. Public aggregate evidence is in
[`compatibility/embedded_audio_2026_10_19.json`](../compatibility/embedded_audio_2026_10_19.json).
