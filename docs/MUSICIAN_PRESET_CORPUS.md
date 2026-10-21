# Supplied musician-preset comparison (2026-10-19)

**Later same-day follow-up:** the missing embedded spectral source described
in the initial findings below is now decoded and retained in native state.
All24 musician renders resolve assets; other22 audio renders remain unchanged.
Full-preset sound still differs. See [embedded-audio follow-up](EMBEDDED_AUDIO.md).

The newly supplied 14 files contain 12 distinct presets and two byte-identical
duplicates. All presets, decoded state, assets, host projects and audio remain
private outside the checkout. Only aggregate measurements are published in
[`compatibility/musician_corpus_2026_10_19.json`](../compatibility/musician_corpus_2026_10_19.json).
No replacement factory assets were created.

## Method

Licensed Serum 2.1.5 runs in an owned REAPER 7.82/x64 instance. ZYG's native
offline engine uses the supplied local content root. Each preset receives
MIDI36 and MIDI60, velocity127, 120 BPM, at 48 kHz: note starts at 1 second,
holds for 4 seconds, with the common first 8 seconds analyzed. Saved macros are
retained. There is no level or phase matching. Both paths export 32-bit float;
ZYG's safety limiter is disabled. The host's extra apply-FX tail is excluded.

Normal host state loading and rendering use the
[published REAPER API](https://www.reaper.fm/sdk/reascript/reascripthelp.html#GetSetProjectInfo_String).
The shared oracle Lua now sets floating-point apply-FX output explicitly using
`APPLYFX_FORMAT` and `OPENCOPY_CFGIDX`. This was verified from actual WAV headers
and prevents integer-export clipping from biasing loud-preset comparisons.

The first attempt wrapped raw preset bytes without processor component markers.
Serum accepted the chunk call but retained init state; accepted-state checks
rejected those renders. Adding processor markers while preserving source preset
state fixed loading. A subsequent integer export clipped several peaks; only
the final float renders enter this report. Of 2896 explicit source control
leaves across the 24 accepted states, 2884 match within 1e-5. The remaining
differences are the loader's numeric-to-`On` representation, omission of a
disabled/default noise flag, and clearing an empty-destination modulation slot.
These checks establish that the intended preset loaded, not semantic coverage.

## Findings and next work

All 24 reference and 24 native renders are audible and finite. ZYG-minus-Serum
RMS differences span -19.95 to +1.78 dB. Correlation between their 10 ms RMS
envelopes spans .634–.992. These are whole-render observations, not perceptual
ratings or proof of why a module differs. A large level error must be isolated
before changing gain curves.

One preset contains embedded spectral audio that ZYG currently fails to load;
its other sources still produce sound. Two diagnostics refer to that same
missing source. This is an unsupported embedded-payload path even with the local
factory assets present. Eight presets have partial-feature diagnostics: six
expose provisional below-threshold compressor mapping, four select unmodeled
Serum 1 compatibility behavior, with overlapping scalar/empty-slot cases.
The reference loader clears the one dangling modulation slot; it does not prove
that a working destination was lost.

Priorities are bounded embedded sample decoding and native recall, followed by
isolated below-threshold compressor dynamics and full-rack levels. Filter,
warp and modulation interactions remain uncalibrated. The native output-filter
quality improvement is independently measured with original signals and does
not close these semantic gaps. Macro/velocity sweeps, legato, tempo changes,
polyphony and listening comparisons remain future work.
