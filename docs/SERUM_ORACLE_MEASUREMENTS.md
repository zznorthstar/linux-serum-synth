# Serum 2 reference measurements — 2026-10-17 / 2026-10-18

Reference: a user-owned Serum **2.1.5** VST3 in Windows REAPER **7.82/x64**.
This is behavioral interoperability research. No proprietary implementation was
read, copied, linked into ZYG, or included in the public repository.

## Method and private material

The `tools/reaper_oracle_*` Lua tools use REAPER's published host API in an
isolated, disposable configuration. A muted master prevents speaker output;
the instrument track remains armed and monitored so parameter changes reach
Serum's processor before its state is captured. Immediate state reads without
processing can return stale values. Reference projects, state chunks, presets,
indexes and audio remain in `../oracle-private/`, outside this repository.

Controlled states are loaded through the host's `vst_chunk` interface. The
wrapper contains separate processor/controller XferJson containers. Reading or
replacing preset state is distinct from examining the plugin's implementation.
The original user's REAPER project is not used as the test project.

The audio protocol is 48 kHz, stereo, MIDI note 60, velocity 127, note-off at
exactly 2 seconds, no level matching. REAPER's apply-FX operation adds a tail;
actual file duration must be reported rather than assumed. Curves are measured
against a longer-held reference with the same waveform/phase so the envelope
can be separated from oscillator variation.

Official references: [REAPER ReaScript API](https://www.reaper.fm/sdk/reascript/reascripthelp.html)
and [Serum 2 user guide](https://www.xferrecords.com/manual/serum-2/docs).
The guide documents envelope MS/BPM units; it does not document the serialized
time encoding below. That encoding was measured, not inferred from the manual.

## Host control, stored value, and audio are different domains

| Control | Host normalized | Stored plain value | Observed display |
| --- | ---: | ---: | --- |
| OSC A level | 0.5 | 0.25 | −12.0 dB |
| OSC A level | 0.75 | omitted default (0.5625) | −5.0 dB |
| OSC A detune | 0.25 | 0.0625 | 0.06 |
| OSC A detune | 0.5 | omitted default (0.25) | 0.25 |
| Main volume | 0.25 | 0.0625 | −21.1 dB |
| Main volume | 0.5 | omitted default (0.25) | −9.0 dB |
| Envelope attack | 0.25 | 0.03125 seconds | 31 ms |
| Envelope attack | 0.5 | 1 second | 1.00 s |
| Envelope attack | 0.75 | 7.59375 seconds | 7.59 s |
| Envelope attack | 1 | 32 seconds | 32.0 s |
| Env 1 sustain | 0.5 | 0.5 | −12.0 dB |

Thus oscillator level/detune and main volume are already squared in the file;
the importer must not square these values again. Modulation, however, moves
the host control domain. A generic power-shaped parameter range reproduces
that travel while leaving ZYG's stored base value in physical gain units.
Main output uses the displayed +3 dB reference scale. Controlled overdrive
renders at stored master levels 0.25 and 0.0625 differ by exactly a factor of
four, with less than 2.4e-7 sample residual after scaling. The direct path also
follows the master control. ZYG's imported master therefore operates after the
FX/mix stage; native older patches retain their original placement.

Measured sparse defaults replace the earlier adapter guesses: master 0.25;
main/SUB oscillator gain 0.5625; NOISE gain 0.0625; detune 0.25; cutoff control
0.5; Env 1 attack 1 ms; Env 2–4 attack 0.5 ms; decay 1 second; release 15 ms.
Explicit preset values still take precedence. Native INIT defaults are unchanged.

## Envelope level and shape

For the controlled saw case, sustained reference RMS was 0.2038986001.
Env 1 sustain 0.5 produced RMS 0.0509746502 (ratio **0.25**); sustain 0.25
produced a ratio **0.0625**. Decay interpolates directly toward that gain,
rather than squaring the entire attack/decay trajectory.

With sustain 0.5, oscillator base gain zero, and Env 1 → oscillator level at
amount 100, sustained RMS was 0.0031859194: **0.015625** of the full reference.
This distinguishes both the envelope's output domain and the squared oscillator
control travel. It is a single positive, unipolar route; bipolar/auxiliary laws
and every destination have not been calibrated by this result.

Attack, decay and release were separately rendered at 500 ms and curve controls
0, 25, 50, 66.6, 75 and 100. A native normalized exponential interpolation fits
the measured internal shape with small residuals at the sampled positions:

```
c = clamp(curve / 100, 0, 1)
m = c <= 0.5 ? 4*c^3 : 1 - 4*(1-c)^3
k = 2 * log((1-m)/m)
F(x) = expm1(k*x) / expm1(k), or x near k=0
```

The midpoint observations are approximately 0.0625, 0.5, 0.85094 and 0.9375
for controls 25, 50, 66.6 and 75. Endpoint midpoints saturate at zero/one;
ZYG bounds the exponent numerically. This is an independently expressed
interpolation fitted to measured behavior, not a claim of implementation identity.
Native patch state selects this curve family explicitly; older native state
retains its original power-curve behavior.

## Tempo-synced envelope times

The sync flag changes the interpretation of time controls. Their stored encoding
still follows `32 * hostNormalized^5`, but the host control selects 47 divisions:
`round(46 * hostNormalized)`, with index zero meaning `fast`.

Index 1 is 1/1024 note, then 1/512 triplet, 1/1024 dotted, 1/512 note, etc.
In quarter-note beats, for nonzero index `i`:

```
base = 2^floor((i-1)/3) / 256
beats = base * [1, 4/3, 3/2][(i-1) % 3]
```

Stored time 1.0 selects index 23, a quarter-note triplet (2/3 beat).
Reference attack completes around 333 ms at 120 BPM and 667 ms at 60 BPM.
ZYG now converts these values in the adapter and stores beat times in its own
envelope model; the DSP converts beats to seconds using current host tempo.
Time modulation still uses ZYG's provisional travel law and needs further
oracle calibration. Changing tempo during a running stage also needs a host test.

## Corpus and asset checks

- 626/626 supplied factory `.SerumPreset` files decode.
- All 12,013 external references resolve against the supplied 7,357-file library.
- The Kraken pack contains 300 legacy `.fxp` files and 90 WAV files. It is not
  a second native Serum 2 corpus. Legacy import remains a separate gap.
- Windows indexing exposed a Unicode filename failure caused by locale-dependent
  JSON I/O. Index and corpus JSON now explicitly use UTF-8, with a regression test.

These checks establish structural access, not preset sound compatibility.

## Embedded table metadata and frame position

An original synthetic two-frame table (sine, then third harmonic, 4096 samples)
was rendered with `numFrames` set to either 4096 or 18432. All three pairs at
stored positions 0, 128 and 256 produced sample-identical Serum output. This
supports using the actual embedded sample count for these cases; the field is
not an instruction to invent missing frames. It does not prove every embedding
format uses the same convention.

The same test exposed the missing frame-position and crossfade semantics:

- Stored WT position uses **1..256**, while the host uses 0..1. A stored value
  of 192 reports host normalized 0.7490196, i.e. `(192-1)/255`.
- Frame position spans **frame bins**, `normalized * frameCount`, clamped at
  the last frame. It is not `normalized * (frameCount-1)`.
- With the crossfade field absent/zero, frames are stepped. In the two-frame
  case, stored position 128 still selects the sine frame.
- With `kParamXfadeMode=1`, position 64 gives a roughly equal sine/third mix;
  128 is nearly all third harmonic; 192 is entirely the last frame.

ZYG's native oscillator model now carries independent frame-bin and
interpolation options. The adapter converts the position domain. The public
regression fixture contains generated sine samples, not Xfer wavetable data.

## Unison pitch and initial phase

For two fixed-phase sine voices at note 60 and stereo width 100, the reference
left/right pitch offsets measured approximately:

| Stored detune | Left cents | Right cents |
| ---: | ---: | ---: |
| 0.01 | +0.659 | −0.616 |
| 0.0625 | +6.286 | −6.203 |
| 0.25 | +35.368 | −35.408 |
| 0.5625 | +97.408 | −97.475 |
| 1 | +200.046 | −199.955 |

The native power law `100 * rangeSemitones * detune^1.25` fits these measurements
(range 2 semitones in this test). The old native exponent was 1.5. The observed
stereo pitch polarity is reversed relative to ZYG's old convention. At detune
zero with random phase disabled, reference left/right correlation is +1;
forcing a half-cycle unison phase offset would cancel the mono sum incorrectly.
Imported oscillators now select the measured power, polarity and shared-phase
options; existing native patches retain their previous settings.

Unison loudness/blend normalization remains uncalibrated. At default blend,
reference RMS relative to the single voice was about 0.807 for two voices,
0.934 for four and 0.955 for seven. This must not be inferred from pitch spread.

## Filter frequency controls

Dense 101-position sweeps of both filters match
`min(22050, 8.175798915643707 * 2^(11.4 * normalized))` within 0.501 Hz
of every rounded host display value. Imported filters select that control
scale; native patches retain their existing 20–20000 Hz scale. This measures
the frequency control, not the filter's acoustic response, resonance or drive.

## LFO timing and sparse shape

An omitted LFO shape is a triangle, not a sine. Beat sync defaults on and
stored rate defaults to 6.25. The serialized rate is `100 * normalized^4`.
Measured division boundaries follow `step = round(42 * normalized)`;
the base duration is `2^(7-floor(step/3))` quarter-note beats. Dotted and
triplet flags enable intermediate grid choices: remainder 1 uses 0.75 times
the base if dotted is enabled; remainder 2 uses 2/3 if triplets are enabled,
otherwise 0.75 if dotted is enabled. Other positions retain the base.
These flags do not multiply every division. The default centre remains a
quarter note with either flag. Free mode reads the stored value as Hz.

The default, free 2 Hz, dotted-centre and triplet-centre controlled renders
all differ in steady RMS from Serum by about −0.038 dB. The fastest setting
is 256 Hz at 120 BPM (verified by reference sidebands); its RMS difference
is +0.238 dB. Rate modulation in sync mode, rise/delay/smooth, chaos and
nonlinear drawn-segment curves remain uncalibrated.

## Overdrive input gain

A sine at oscillator gain 0.001 gives reference small-signal gains of
−8.647, +1.103, +10.853, +20.603 and +30.353 dB at drive controls
0, 25, 50, 75 and 100 percent. The native overdrive retains its original
waveshaper and selects the measured approximately −8.65 + 39*drive gain
range, without the previous automatic half-drive gain compensation.
At full input and drive 50, steady RMS error improves from −7.737 dB to
+0.377 dB. Waveform/harmonic character is not claimed identical; other
distortion modes and compression remain provisional.

## Velocity and note source maps

Constant maps at 0, .25, .5, .75 and 1 verify top-origin stored Y values.
Note-source output follows the mapped value. Velocity-source output rounds
to the nearest MIDI step: a constant .5 produces 64/127. With full-depth
modulation into oscillator gain, the measured audio ratios are the square
of those source values. Typed native curves now drive both sources and
survive preset save/reload; the editor displays the actual map.
Nonlinear segment bend and scalar legato interpolation remain uncalibrated;
the latter is diagnosed for imported presets with portamento enabled.

## Complete-preset sample

Six feature-diverse factory presets were rendered at note 60, velocity 127,
120 BPM, four seconds held and six seconds total. All native renders were
finite with zero missing assets. These are measurements, not a listening
verdict or a compatibility percentage. No phase or level matching was used.

| Preset | ZYG minus Serum RMS, dB | Serum / ZYG spectral centroid, Hz |
| --- | ---: | ---: |
| BA - Ultraterrestrial | −0.25 | 971 / 181 |
| PD - Granular Ambience | −3.61 | 312 / 70 |
| ARP - Cosmic Lives Forever | −6.23 | 1238 / 349 |
| PL - Poppy Pluck | −1.98 | 500 / 1129 |
| FX - Atmospheric Boom | −16.94 | 350 / 290 |
| LD - Daily | −6.88 | 1528 / 1359 |

The bass example illustrates why matching level alone is insufficient: its
spectral balance differs substantially. Serum's restored host state was
checked: older presets migrate distortion frequency controls (and some
numeric enable flags into enum strings). These migration differences remain
unmapped in ZYG. Preset names here identify test cases; preset files and all
audio/asset content stay private.

For Atmospheric Boom, disabling all effects reduces RMS difference to
−3.77 dB; additionally bypassing the filters leaves −3.77 dB. Explicitly
zeroing all matrix amounts then gives +5.92 dB. This points to both FX and
source/modulation differences. Replacing a modulation slot with an invalid
empty string did not clear it in Serum; that trial was discarded and rerun
with verified zero amounts.

## 2026-10-18: FX defaults and single-band compressor

The reference controller's normal state exposes `defaultPlainValues`. A
disabled instance of each of the 16 observed FX types yielded **203 default
entries**, recorded as aggregate facts in
[`serum2_fx_declared_defaults_2_1_5.json`](../compatibility/serum2_fx_declared_defaults_2_1_5.json).
These controller values have mixed domains: for example an FX frequency
default can be expressed in Hz while its processor preset field is normalized.
They must not be applied wholesale as raw preset defaults. This table contains
no preset, audio, wavetable or binary implementation data.

The compressor declares attack/release about **90.091 ms**, ratio **4:1**,
threshold control **0.5**, makeup **1** and below-threshold control **0 = off**.
The UI displays approximately −18.1 dB at threshold control .5. The previous
native assumptions of 10 ms attack, 100 ms release and ratio 2 were incorrect
for a sparse import. An FX slot also needs its numeric identity (`type=5`
for compressor); the first incomplete-slot trial was ignored by Serum and
discarded after checking its restored state.

Thirty-four controlled compressor states cover input levels, default single/
multiband behavior, threshold, limiting and makeup. At ratio 1, makeup controls
1, 2, 4 and 8 produce gains 1, 2.0333, 4.1 and 8.2333: the observed mapping is
`(31*stored - 1)/30`. The previous native makeup cap suppressed these gains.

In ZYG's current signal normalization, a fast limiting sweep is closely fit
by equivalent threshold gain `3.3*((1-control)^3 + 1e-6)`. The factor is an
empirical adapter to ZYG's gain staging, not a claim about Serum's internal
implementation. The native single-band family uses its own RMS follower,
inverse-cubic threshold, linear makeup and no programme-level AGC. Native
patches retain their old family; imported multiband behavior remains provisional.
At ordinary default settings, full-input RMS now differs by about **+0.13 dB**;
ratio-4 threshold .75 differs by **+0.43 dB**, and threshold 1 by about **−1 dB**.
Detector dynamics, below-threshold behavior and compensated wet/dry remain open.

The final limiter/makeup sweep is within approximately 0.07 dB in steady
RMS; the regression suite checks the threshold sweep within 0.15 dB. These are steady-sine tests, not
full compressor equivalence. The multiband full-input test still differs by
about **+11.9 dB** and must not be described as calibrated.

## 2026-10-18: current complete-preset comparison

The newer alias-safe mip interpolation and compressor mapping expose additional
gain errors in full chains. The earlier table is historical, not current-build
evidence. The renderer now writes 32-bit float WAVs; a limiter-disabled signal
above 0 dBFS must survive export rather than being silently clipped to PCM.
Output creation/write failures now fail the measurement command.

| Preset | Current ZYG minus Serum RMS, dB | ZYG raw peak | Serum / ZYG centroid, Hz |
| --- | ---: | ---: | ---: |
| BA - Ultraterrestrial | +6.73 | 1.780 | 971 / 180 |
| PD - Granular Ambience | −3.50 | 0.090 | 312 / 70 |
| ARP - Cosmic Lives Forever | −6.01 | 0.367 | 1238 / 347 |
| PL - Poppy Pluck | +4.61 | 0.586 | 500 / 1183 |
| FX - Atmospheric Boom | −13.14 | 1.189 | 350 / 269 |
| LD - Daily | −6.84 | 0.539 | 1528 / 1318 |

All six remain finite with resolved assets. Correcting isolated controls does
not uniformly improve a complete preset: the old errors could partially
compensate one another. These one-pass observations identify work, not a sound
similarity score; randomized presets need repeated trials. The earlier envelope
cases remain within about 0.08 dB after the alias-safe mip change; ordinary
LFO/scalar/generated-table cases remain within about 0.04 dB. Unison loudness
and the fast LFO case still have the differences recorded above.

## Remaining calibration

Broader full-preset listening/repeated comparisons, unison loudness/blend, filter response,
resonance/drive, warps, matrix aux/curves, most effects, advanced LFO behavior
and legacy import remain open. Similar RMS in isolated cases is not a claim
of similar sound across the factory corpus.

## 2026-10-18: unwanted repeated attacks and time effects

The user describes an extra delayed percussive hit, rather than a generic
digital crackle. Investigation uses original synthetic sine inputs through
the licensed reference's normal state/UI/audio interfaces. No factory asset
replacement pack is being created; that work is explicitly deferred until
the user requests it.

Nineteen initial states cover delay time/sync, low/high input notes, full-wet
endpoints, Hall decay and pre-delay. Additional states cover mix and stereo
mode; a 17-position sync probe was repeated with one second of pre-roll.
Loaded processor state was checked for accepted FX identity and values.
These measurements are private; the following aggregate observations are public:

- At delay wet 100%, the reference has no direct attack before the repeat.
  ZYG previously retained half the dry input. New time-effect modules use a
  fully wet endpoint. The intermediate mix curve is still provisional; this
  is not a measured equal-power/crossfade fit.
- The first reference delay repeat is filtered: a 65 Hz input is suppressed
  by a narrow band around 2 kHz, whereas a roughly 2 kHz input passes.
  ZYG previously filtered feedback only and emitted the first repeat unfiltered.
  New modules emit the filtered tap. This can reduce an unintended bass thump,
  but the user's particular sound has not yet been reproduced and diagnosed.
- Reference delay mode 0 produces equal left/right first repeats; mode 1 has
  a left-only first repeat and then alternating feedback. The importer now
  translates these into native normal/ping-pong modes 1/2.
- Hall `kParamDelay` is **DECAY**, not additional pre-delay. Direct UI entry of
  1, 2 and 8 seconds saves 0.14172336128867677, 3.543083921345183 and
  119.18933848404833 respectively. These fit `seconds = 0.75 + sqrt(0.441*x)`.
  Default x=30.625 displays about 4.4 seconds. This sets the original native
  FDN's decay target; it does not demonstrate matching acoustic RT60 or timbre.
- Sweeping that decay field from 0 to 250 leaves the reference's initial
  wet onset essentially unchanged. Setting actual `kParamPreDelay` to .1
  moves onset to approximately .1 seconds. ZYG no longer adds decay/1000
  seconds to pre-delay. Its original FDN still has about 28 ms onset in the
  controlled default-size case, unlike the reference's immediate early response.
- Hall's `kParamFreq`/`kParamFreqB` correspond to low/high cuts. Corrected
  modules now use those directions, with zero bypassing the respective cut.
  Their frequency curves remain independent provisional ZYG choices.
  Control meanings also follow the [official FX guide](https://xferrecords.com/manual/serum-2).
- The original reverb multiplied an already wrapped common LFO phase by
  fractional rate ratios. Each line therefore jumped at the common wrap,
  approximately every 2.703 seconds. Independent phase accumulators remove
  that structural discontinuity. Delay lengths and pre-delay are smoothed
  over 10 ms when changed; this does not establish arbitrary modulation parity.

Older native JSON without `legacyTimeFx` retains the historical wet/tap/decay
model; old implicit reverb defaults are baked during migration. The periodic
phase discontinuity is fixed for both models. Reimport Serum presets to use
the corrected model. A previously saved native preset retains its old model.

**Historical sync status (2026-10-18; superseded by the delay timing pass below).** The declared default delay sync flag is on;
default time .0772939453125 produces a .5 s repeat at 120 BPM. The old engine
treated it as .0773 seconds. Grid probes confirm beat divisions, but the
fast end is not yet adequately characterized. Imported delay sync and reverb
pre-delay sync now report explicit unsupported diagnostics instead of being
silently marked handled. No full tempo-grid calibration is claimed here.

Offline REAPER renders can include an additional event at the MIDI item's
closing boundary. Tail analysis must separate that boundary from the intended
note and its effects. Pre-roll/tail controls were added to the public Lua
measurement helper. Ordinary free-delay onsets also include about 1 ms of
reference host/HQ latency compensation; do not fit that away as a time law.

The new private Ghosthack Jump Up DnB Detonator pack contains 15 native Serum 2
presets. All 15 rendered finite, non-silent audio at MIDI note 36, velocity 127,
120 BPM with zero missing assets. All contain a reverb module, nine contain
delay and 14 compression; many reverb/delay wet controls are zero until changed
or modulated. This is an inventory/render smoke check, not fidelity validation,
and does not explain every silent preset reported by the user.

`LEAD - Flames` provides a concrete missing-content reproduction: with an
explicit absent root it renders peak/RMS zero with missing-asset diagnostics;
with the supplied local content root it renders peak about .9955. Its audible
sources are two sample oscillators reading Trombone.flac; the wavetable source
has zero base level. This isolates an asset dependency, not a DSP failure.
The old Windows default discovery checked only an environment override and
Linux data paths, so a plugin without a configured content root could fail
while the offline tool with an explicit root succeeded.

The content-folder action now saves a per-user setting; fresh instances read it
without audio-thread I/O. The installed updated VST3 was verified in a new
private REAPER instance with `LEAD - Flames` and no asset-root override: the
native state contains the saved local root, zero missing assets and corrected
time-effect models. Its finite 48 kHz render peaks at .9687774 (RMS .1029764).
All 15 new presets also pass at C2 and C4 without explicit roots (30 renders).
These checks establish asset resolution and sound generation, not Serum fidelity.

## Reported silence and persistent tones (2026-10-18)

These measurements concern the independent ZYG engine, not reference Serum
behavior. The four user-named factory examples were rendered at MIDI 36 and 60,
full velocity, 120 BPM and 48 kHz, with one second held and three seconds after
release. All four resolve assets with the supplied local root. Gnarly Bass Stack
and Analog Glow are audible with that root and exactly silent with a deliberately
nonexistent root, reproducing the missing-content failure mechanism.

Windows canonical path inspection showed the earlier AppData content setting
was redirected into Codex's private MSIX cache. The previous fresh child-REAPER
test therefore did not establish ordinary user-host configuration. The saved
selection now prefers `USERPROFILE/.config/ZYG-ZXG/content-root.txt`, outside that
redirection. Existing AppData settings remain a fallback. No factory content is
copied into the public project.

ElectroFluxx and Glowing Pluck reproduce persistent high-frequency tails with
all assets present. Bypassing all FX removes them; bypassing voice filters or
modulation does not. Individual FX bypass isolates their ladder FX filters.
ElectroFluxx has no reverb or delay in this rack. At MIDI 60 its final-second RMS
was .00781086 with a dominant 21,162 Hz component; Glowing Pluck was .02405278
with a dominant 17,161 Hz component. The original native feedback correction
described in `DSP_DECISIONS.md` reduces those RMS values to zero and 1.991e-8
respectively. Both tested pitches lose the high-frequency plateau. Sparse
FXFilter resonance also now imports its previously measured default zero instead
of the native provisional 10%; explicit values and native defaults remain intact.

The updated installed VST3 was tested in two disposable REAPER 7.82 instances
without content-root environment overrides. Gnarly Bass Stack and ElectroFluxx
both have the intended saved root, zero missing assets and finite nonzero audio.
ElectroFluxx's measured final two-second tail is zero. The private test projects
were saved and their owned hosts exited; the user's modified project remained
open. This establishes host use of the corrections, not matching Serum behavior
for these presets or the remainder of the library.

## Splitter bypass and branch boundaries (2026-10-18)

Private REA routing findings were checked with 37 controlled original-sine
presets in Serum 2.1.5 / REAPER 7.82 at 48 kHz, MIDI 60, velocity 127.
Accepted state was captured after each load, including explicit branch bypass
and child counts. These are within-instrument behavioral comparisons, not a
claim that ZYG's crossover or effects match Serum's sound.

- Disabled two-band, three-band and mid/side splitters reproduce the dry input
  exactly, despite an active polarity-inverting child.
- Each frequency band's bypass reproduces the same splitter with no children;
  active child controls differ audibly. Mid and pure-side input confirm the
  same rule for both M/S branches: bypass preserves the band signal.
- A following top-level effect still executes. Nested whole/inner bypass
  preserves the corresponding subtree boundaries.

Twenty expected equality relations have zero maximum sample error in the
reference; nine active controls differ. The initial 22-case ZYG baseline
satisfied only 3 of its 14 bypass relations. The original native graph now
skips a disabled subtree and skips bypassed branch effects while retaining the
branch audio. It also clips each child span to its parent before recursion.
No proprietary processing equations or crossover implementation were copied.
The expanded native render pass satisfies all 20 bypass relations with zero
maximum sample error within ZYG. This verifies those structural relationships;
the complete reference and native waveforms are not asserted identical.

Solo, automation transitions during effect tails, crossover transfer functions,
and reference bypass-state/latency retention remain unverified. Native Boolean
offset handling has regression coverage, not reference modulation calibration.
Two processed nesting levels, 32 splitter states and 96 total modules remain
native capacity limits; excess subtrees bypass transparently and imports report
the limitation. Structural-count modulation and splitter output level remain
explicitly unsupported. Raw reference material stays outside the checkout.

## Delay beat grid and linked-state loading (2026-10-19)

Retained REA evidence established the offset-before-Link processing order.
Normal state/audio measurements now establish the parameter scale separately:
42 initial timing/startup controls, then 43 primed grid/tempo/offset cases and
17 additional primed free-time/linked-state controls in Serum 2.1.5 / REAPER 7.82.
All reference state, presets, projects and WAVs remain private. The source
oscillator is an original embedded sine; no factory asset is needed.

A .25-second priming note at time zero precedes a .002-second measured note at
two seconds, at 48 kHz and velocity 127. This separates the steady timing law
from the reference's startup settling. Unprimed short echoes can arrive early;
do not fit that transient into the beat grid or claim it has been reproduced.

- Accepted stored time spans .001 to .501. Its knob coordinate follows
  `stored = .001 + .5 * normalized^4`; the sparse default .0772939453125
  corresponds to normalized .625.
- Twelve division centers and probes on both sides of eleven switching
  neighborhoods select quarter-note durations
  `1/128, 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4, 8, 16`.
  Nearest selection over eleven equal normalized intervals matches those probes.
- The default is one quarter note: 1, .5 and 1/3 seconds at 60, 120 and
  180 BPM. Primed reference peaks carry approximately .917 ms of common signal/
  filter/host latency, apart from sample rounding and the zero-time endpoint.
- Free delay targets `max(0, stored - .001)` seconds before side offsets.
  Six knots from .001 through .501, plus unequal-offset controls, distinguish
  the encoding offset from the common output latency.
- Unlinked sides multiply their own time by their own offset. When linked
  unequal state is loaded, the reference controller writes the right side's
  effective base time and offset to both sides. An omitted right value supplies
  its default; it does not inherit a custom left value. Runtime Link then copies
  the completed left target to the right. Accepted state is essential here.

The original native delay retains its own interpolator, filter, feedback and
50 ms target smoothing. A separate native `delayTimingModel` selects these
measured target mappings; old native JSON missing the field retains seconds
behavior, independently of the earlier wet/tap migration. Reimport the source
Serum preset to select corrected timing. Original requested state stays preserved.

Prepared float delay storage supports targets up to 32 seconds. At the longest
division and maximum ordinary offset this covers BPM >=45; slower requests
clamp to 32 seconds and imports expose that limit. Four pooled stereo delays
consume 64 MiB at 44.1/48 kHz, 128 MiB at 96 kHz and 256 MiB at 192 kHz after
power-of-two allocation (an increase of 60 MiB at 48 kHz). No allocation or
diagnostic emission occurs while processing. Native zero-time reading retains
the engine's own interpolation guard.

Time/sync modulation uses the native fourth-power coordinate but remains
explicitly qualified as uncalibrated for reference depths and transitions.
Reverb pre-delay sync, first-note delay settling, feedback/filter character and
complete preset sound are separate open questions. Aggregate corpus inspection
finds delay modules in 389/626 factory presets and 9/15 private additional-pack
presets; presence does not imply an audible wet or enabled routing path.

The corrected Windows native render pass covers all 60 primed cases with the
same MIDI schedule. Maximum echo-peak timing difference against the paired
reference is .104167 ms; the previous default stayed at about .078313 seconds
at all three tempos. This establishes static timing improvement, not matching
waveforms, first-note settling or complete preset sound. All 8 native suites
pass (111 DSP checks), and 15 additional-pack smoke renders remain finite,
audible and asset-complete.

The installed VST3 additionally passes six recalls in a fresh private REAPER
host: three tempos, free timing, legacy state without the new model, and
corrected state recall. Numeric model values survive native state restoration.
Echo peaks are within 2 ms of their targets including that host's signal/event
latency; this bound is separate from the .104167 ms native/reference CLI result.
The owned host saved and exited normally. The user's session was untouched.
