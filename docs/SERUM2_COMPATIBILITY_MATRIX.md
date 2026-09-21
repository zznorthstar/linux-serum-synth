# Serum 2 compatibility matrix for ZYG-ZXG

Status: research baseline, 2026-09-21. This is a specification and gap ledger, **not a compatibility claim**. The current implementation decodes Serum 2 preset containers and indexes local assets. It has no patch-model translation, DSP engine, GUI, or plugin.

Sources: [official Serum 2 User Guide](https://www.xferrecords.com/manual/serum-2/docs) (manual for Serum 2.0.18, April 2025), [official What's New](https://static.xferrecords.com/Serum%202%20What's%20New.pdf), and aggregate inspection of 626 factory `.SerumPreset` files from the supplied 2.1.5 installer. These factory presets were authored as versions **2.0.11–2.0.15**; the installer version does not make them 2.1.5 feature fixtures. The factory corpus is a finite sample, so absence from it does not prove a feature absent from Serum 2. The machine-readable [corpus schema](SERUM2_CORPUS_SCHEMA.json) lists 515 distinct explicit `plainParams` paths, 799 structural paths, 49 observed modulation source IDs, 214 modulated destination type/parameter pairs, observed string enum values, 16 FX module names, and reference-field counts. The provided 2.1.2 user preset is a separate fixture.

Legend: **D** = documented by Xfer; **O** = observed in a decoded preset; **C** = container decoded; **I** = local asset indexed/resolved; **M** = mapped into an independent ZYG patch; **R** = rendered by native ZYG DSP; **UI** = editable in ZYG. Only C and I are implemented, where specified. “Preserved in decoded state” means no field is deleted by the research parser; it does **not** mean a native patch or audio engine can use it.

| Subsystem | Required semantic state | Evidence | Current ZYG status |
| --- | --- | --- | --- |
| Main oscillators A/B/C | Independent enable, mode, pitch, level, pan, unison, key/velocity zones and routing | D/O | C; M/R/UI absent |
| Wavetable | Table identity/content, frame position/interpolation, phase/memory, two warp slots, unison | D/O | C; factory table index I; M/R/UI absent |
| Sample | Source, regions/loop/slices, scan, pitch, warp, modulation | D/O | C; factory sample index I; M/R/UI absent |
| Multisample | SFZ mapping, child samples, key/velocity layers, timbre, envelope | D/O | C; SFZ/sample index I; M/R/UI absent |
| Granular | Sample timeline, grain density/length/window/randomization, warp | D/O | C; sample index I; M/R/UI absent |
| Spectral | Source, frequency bounds, scan, spectral filter, phase/transients, spectral warps | D/O | C; sample index I; M/R/UI absent |
| SUB / NOISE | Dedicated source parameters and independent routing | D/O | C; noise asset index I; M/R/UI absent |
| Filters 1/2 | Exact type identity, cutoff/resonance/drive/var/wet/pan/level | D/O | C; 83 observed voice-filter IDs; M/R/UI absent |
| Mixer/routing | Seven routing slots, dual filter topology, Main/Direct/None, FX buses | D/O | C; M/R/UI absent |
| Modulation | Envelopes, LFO/path/chaos, macros, MIDI/MPE, audio-rate source modules | D/O | C; M/R/UI absent |
| Matrix | 64 ordered slots including source, target, curves, aux, slew, polarity, bypass | D/O | C; M/R/UI absent |
| Voice/global | Poly/mono/legato/portamento/priority, tuning, MPE, oversampling, voice steps | D/O | C; M/R/UI absent |
| Arp | 12 clips, rate, patterns, probability/gate/retrigger/velocity/transpose | D/O | C; factory arp index I; M/R/UI absent |
| Clip sequencer | 12 MIDI clips, notes, launch/playback, keyboard span, MIDI out | D/O | C; factory clip index I; M/R/UI absent |
| FX rack | 13 effect classes, three splitters, ordered instances, Main/Bus 1/Bus 2 | D/O | C; IR index I; M/R/UI absent |
| Asset browser/resolver | Factory/user path, embedded data, missing/ambiguous diagnostics | D/O | Basic local exact-path I; broader import/embedded handling absent |
| Native ZYG preset | Complete independent patch, ancestry, unknown Serum sidecar | Design requirement | Absent |
| Native Linux VST3 / CLAP / host state | Audio/MIDI/automation/transport/reload | Design requirement | Absent |

## Oscillators and source material

Serum 2 has three main oscillator slots, each with **wavetable, multisample, sample, granular, or spectral** mode, plus dedicated SUB and NOISE. The explicit `Oscillator#` parameters observed across factory presets are: enable; type; octave/coarse/fine/pitch/pitch ratio/mode/source/tracking/bend tracking/Hz offset; volume/pan; unison count/range/span/stereo/stack/detune mode/warp; key and velocity zones; sample start/end/position/random start/reverse; loop start/end/mode/crossfade/release behavior; slices; scan rate/range/tempo lock; and base tempo. See `Oscillator#.*` in the corpus schema for every exact key. Some parameters are implicit defaults, so the absence of a key in one preset cannot be interpreted as “off.”

### Wavetable

Represent `relativePathToWT`, embedded/custom wavetable data where present, frame count/channels/rate, position, crossfade interpolation, initial/random phase, phase memory (`kContiguous`, `kPerVoice`), unison frame spread, warp 1 and warp 2 type/amount/variable controls. The manual also describes table editor thumbnails, drawing, FFT/harmonic editing, frame insert/sort/copy, formula generation, import/morph/export and saved tables. ZYG needs its own editor and interchange format. The corpus contains **two** distinct warp menu fields and many mode IDs; their complete observed enum values are in the schema. These are not yet mapped or rendered.

### Sample

Represent `samplePathRelative`, sample dimensions, start/end, independent loop bounds, crossfade, forward/reverse/ping-pong/tailed behavior, one-shot and release behavior, auto/manual slices, slice root note, scan, pitch, unison and two warp slots. Read actual sample audio from the user's indexed library; do not infer audio is embedded merely because sample metadata is in CBOR. The manual documents the region and slice workflow in its Sample chapter.

### Multisample

Represent `sfzPathRelative`, `embedded_sfz`, the `files` map, sample mapping by key/velocity/root/tuning/articulation, the mode envelope including delay/attack/hold/decay/sustain/release, velocity tracking, timbre shift, unison, and two warp slots. In the examined factory presets, `embedded_sfz` contains mapping text while `files` contains child file metadata, **not** audio bytes. Resolve the SFZ and every child audio file. SFZ parsing needs an explicitly documented supported opcode set and diagnostics for unsupported opcodes.

### Granular

Represent the shared sample region/loop/slice/scan model plus density (`free` or BPM), grain length and length mode, window shape (`Blackman-Harris`, exponential decay, Gaussian, triangle, Tukey observed), window amount/skew, direction, X/Y assignment, unison trigger pattern (`even`, `exponential`, `random` observed), and randomization of offset, direction, gain, pitch, pan, length, window, and both warp slots. Source audio and grain scheduling must be native; no DSP equivalence to Serum is required.

### Spectral

Represent sample and timeline state, low/high frequency bounds, boundary smoothing, transient and phase-lock controls, spectral filter curve/shift/wet-dry, mix, two warp slots, and spectral-specific identifiers including shift, pitch shift, comb, smear, mirror, gate, vocode/mask from other modules, harmonic/subharmonic generation, peak transposition, detune/spread, and Shepard variants. The factory corpus observes these identifiers; the manual's Spectral chapter defines the UI-level controls. These need separate, musical ZYG interpretations, not a single “spectral” placeholder.

### SUB and NOISE

SUB needs pitch, phase, continuous-phase behavior, level/pan, routing and five observed shapes: pulse, rounded rectangle, saw, square, triangle. NOISE needs white/pink/brown/Geiger generator types as observed, local noise sample references, color, one-shot, pitch/fine, phase and routing. The direct path remains distinct from the filtered Main path.

## Filters and routing

Two voice filters are independently selectable. Preserve filter ID **exactly**, even if the ZYG transfer function differs. Common controls observed: `kParamType`, `kParamFreq`, `kParamReso`, `kParamDrive`, `kParamVar`, `kParamWet`, `kParamPad`, `kParamStereo`, `kParamX`, `kParamY`, `kParamLevelOut`, key track and enable. `Var` has type-specific meaning: e.g. second frequency, morph, feedback damping, formant shift, saturation, or smoothing. The official manual's “Filter Types and Var Parameter Functions” table is the reference for those meanings; the raw IDs below come from 2.1.5 presets.

The **83 observed voice-filter IDs** are:

`ADD_BASS`, `Allpasses`, `B12`, `B24`, `BN12`, `BP12`, `BPN12`, `BPN24`, `BandReject`, `Comb2`, `CombH6N`, `CombH6P`, `CombHL6N`, `CombHL6P`, `CombL6N`, `CombN`, `CombP`, `Combs`, `DJMixer`, `Diffuser`, `DirtyMg`, `DistComb1BP`, `DistComb1LP`, `DistComb2BP`, `DistComb2LP`, `Exp`, `ExpBPF`, `FlangeH6P`, `FlangeHL6N`, `FlangeHL6P`, `FlangeL6P`, `FlangeN`, `FlangeP`, `FlangePhase12HL6P`, `FormantONE`, `FormantTWB`, `FormantTWO`, `H12`, `H18`, `H24`, `H6`, `HB12`, `HEQ12`, `HN12`, `HP12`, `L12`, `L18`, `L24`, `L6`, `LB12`, `LBH12`, `LBH24`, `LH12`, `LN12`, `LNH12`, `LNH24`, `LPH24`, `LadderAcid`, `LadderEMS`, `LadderMg`, `MgL18`, `MgL24`, `MgL6`, `N12`, `N24`, `NN12`, `PP12`, `PZ_SVF`, `Phase24N`, `Phase24P`, `Phase36N`, `Phase36P`, `Phase48H6P`, `Phase48HL6P`, `Phase48N`, `Phase48P`, `RM`, `RMT`, `Reverb1`, `Scream`, `Scream3LP`, `Wsp`, `ZDF_A`.

This is an observed set, not a verified complete registry. FX Filter has its own observed ID set in the schema and must be covered separately. The manual groups filters as Normal, Multi, Flanges, Misc and New; named designs include French LP, German LP, formants, comb/flange/phase variations, Wsp, DJ Mixer, Diffusor, MG/Acid/EMS ladders, MG Dirty, PZ SVF and Exp MM/BPF. Some user-facing names differ from serialized IDs: create a validated ID-to-name table rather than guessing by string similarity.

`RoutingSlot0..6` corresponds to five sound sources plus two filters in observed state. The values seen for `kParamRoutingDest` are `kRoutingDestFilter`, `kRoutingDestMaster`, `kRoutingDestDirect`, and `kRoutingDestNone`. `kParamFilterBalance`, `kParamFXBus1Level`, `kParamFXBus2Level`, and `kParamViaEnv1..4` also occur. Global state includes direct level, each FX bus level and destination. The target architecture is a typed routing graph with ordered/parallel filter edges and Main/Direct/Bus paths. Validate exact graph semantics against the manual and controlled fixtures before implementation.

## Modulation, envelopes, LFOs and voice state

The manual specifies four envelopes, up to ten LFOs, eight macros, and **64 matrix slots with 49 source choices**. Envelope state observed includes start/end, attack/hold/decay/sustain/release, three curves, BPM sync and inverse-legato behavior. LFO state includes path points/curves, free/envelope/trigger behavior, Lorenz/Rössler chaos and random sample-and-hold, rate/10×, tempo sync/dotted/triplet, phase/snap, direction, rise/delay/smooth, swing, mono and anchor. Velocity and note have editable response curves. Macros are both sources and destinations.

Matrix rows must retain `source[0]` (main ID) and `source[1]` (auxiliary ID; 0 when unused), destination module type/instance/parameter ID/name, amount, polarity, source and auxiliary curves plus point data, aux inversion, rise/fall slew, delay, output scale, bypass and row order. Corpus keys include `kParamAmount`, `kParamAuxCurve`, `kParamAuxCurveData`, `kParamAuxInverted`, `kParamBipolar`, `kParamBypass`, `kParamCurveIn`, `kParamCurveOut`, `kParamDelayBeatSync`, `kParamDelayOffset`, `kParamMainCurveData`, `kParamOut`, `kParamSmoothFall`, `kParamSmoothLink`, `kParamSmoothRise`; `flex` stores nested curve point arrays. The corpus contains 13,258 populated slots and 49 distinct observed main-source IDs. These 49 observed IDs are not known to equal the manual's 49 menu choices; some separately identified source IDs do not occur in the factory corpus. Preserve the entire raw slot until every member has a tested semantic destination.

Sources documented in the manual include envelopes; LFOs; macros; velocity/note/release velocity; mod wheel; pitch bend; channel/poly aftertouch; Note Expression/MPE X/Y/Z; note-on random 1/2/discrete; alternating note-on 1/2; fixed; active voices; voice index and Voice Mod 1/2; and audio output of A/B/C/SUB/NOISE/filter 1/filter 2. The [serum2vital fixture research](https://github.com/btesser/serum2vital/blob/main/docs/FORMATS.md) identified much of the numeric mapping with GUI-created isolated presets: 1 Mod Wheel; 2–5 ENV 1–4; 6–15 LFO 1–10; 16 Velocity; 17 Note; 18/19 channel/poly aftertouch; 20 Noise audio; 21–22 note-on random 1/2; 23–24 alternating 1/2; 25–32 Macro 1–8; 33 Pitch Bend; 34–36 MPE X/Y/Z; 37 release velocity; 38 Fixed; 49–52 A/B/C/SUB audio; 53–54 filter 1/2 audio; 55 Active Voices; 56–57 Voice Mod 1/2; 58 Voice Index; 59 discrete note-on random. IDs 39–44 and 47–48 occur in the corpus but remain unidentified in that research. This table is research evidence, not an implemented ZYG mapping; validate the remaining IDs before declaring matrix coverage.

Voice/global state observed covers poly count, mono/legato, note priority/latch, portamento time/curve/always/scaled, bend range, tuning, oversampling, host render quality, transposition, swing, MIDI output, MPE enable/config/bend range, and up to eight Voice Panel step columns for detune, pan, envelope time, cutoff and two mod values. The pitch quantizer stores key and scale; oscillator mapping assigns keyboard zones to sources. These controls influence audible preset behavior and need tests.

## Arpeggiator and clip sequencer

The manual describes 12 arp clips and 12 MIDI/clip slots, with banks and keyboard-triggered selection. Observed arp parameters cover enable/active clip, key zone, launch quantize, MIDI-select octave, rate/sync/dot/triplet, shape, range/wrap, repeats, step action, gate, chance, transpose and velocity patterns, retrigger/launch behavior. Observed shape values include `UpDown`, `DownUp`, `Chord`, `Pattern`, `Played`, `Rand`, `RandNoDup` and others in the corpus schema. The actual custom step graph and clip data are nested beyond `plainParams`; structural parsing alone does not play them.

Observed MIDI clip parameters cover tempo rate and divisions, note gate, playback mode (`OneShot`, `Pendulum`, `Random`, `Static`), launch quantize/retrigger, transposition, velocity triggering and keyboard span (`Mono`, `Offset`, `Poly`). Nested `clip.notes[]` records include note number, timestamp, length, channel, attributes, mute and expression events. `clip.automation[]` includes destinations and timestamped values/curves. Arp clip notes use related records, with region boundaries and lane tabs. These structures need a lossless neutral event model and scheduled MIDI/audio behavior.

## FX system

Serum has three ordered racks: Main, Bus 1, Bus 2. Multiple instances and reorder/bypass must preserve instance identity, the modulation target binding, sends and returns. The official manual enumerates 13 effects plus three splitters. All 16 occur in the factory corpus:

| Serialized module | Required ZYG interpretation | Current |
| --- | --- | --- |
| `FXBode` | Frequency shifting, feedback/delay, mix/width | C only |
| `FXChorus` | Multi-delay modulation/filter/feedback | C only |
| `FXComp` | Single/multiband compression, crossover, wet/dry | C only |
| `FXConv` | Convolution and IR path, trim/tone/decay/size | C; IR index I |
| `FXDelay` | Tempo delay, stereo offset/modes/filter/feedback | C only |
| `FXDistortion` | All observed mode IDs and filters/bias/stages | C only |
| `FXEQ` | Multiple band frequency/gain/Q/type | C only |
| `FXFilter` | Its complete filter-ID set and controls | C only |
| `FXFlanger` | Modulated delay/feedback/width | C only |
| `FXHyperD` | Hyper and Dimension sections | C only |
| `FXPhaser` | Pole count/feedback/depth/stereo | C only |
| `FXReverb` | Hall/Space/Vintage/Abyss observed plus mode controls | C only |
| `FXUtils` | Width, balance, polarity, LF mono, HP/LP | C only |
| `FXSplit` | Low/high frequency branch graph | C only |
| `FXSplit3` | Low/mid/high frequency branch graph | C only |
| `FXSplitMS` | Mid/side branch graph | C only |

Each module's observed parameter identifiers are enumerated in `SERUM2_CORPUS_SCHEMA.json`. Splitter branch module counts determine nested rack topology; a flat effect list is insufficient. Convolution IRs may be local or user supplied; missing IRs must be reported separately from unsupported effect parameters.

## Assets, unknowns and acceptance criteria

The extracted installer has **7,357 indexed files** in the recognized content directories: Presets 627 entries (626 `.SerumPreset`), Tables 289 entries (288 `.wav`), Samples 856, Multisamples 4,118, Impulses 110, Clips 141, Arp Patterns 13, Arp Banks 4, Clip Banks 16, Effect Chains 240, PZ Filter 33, Curves 19, Styles 19, LFO Paths 22, LFO Shapes 152, Skins 691, System 7. Counts include placeholder and auxiliary files. It is excluded from Git and must never ship with ZYG. The asset index references the user's local root; it does not copy assets.

Known external reference fields from 626 presets are `relativePathToWT`, `relativePathToNoiseSample`, `samplePathRelative`, `sfzPathRelative`, and `relativePathToIR`; multisample `files` adds child references. Some SFZ mapping text is embedded while its audio remains external. Embedded wavetable/IR data and user-library locations require further inspection. The current local index resolves all **12,013 required external references** found in those factory presets; this verifies lookup only, not audio decoding or playback. Missing, ambiguous, unsupported-format, and decoded-but-unmapped are four distinct diagnostic categories.

A preset's semantic coverage may only be reported as 100% when every recognized state field has a patch-model destination and the corresponding engine behavior exists. The current diagnostic tool intentionally reports **semantic coverage unmeasured**. Future reports should count recognized modules/parameters/routes, rendered modules, unknown fields, unresolved assets, and explicit fallback decisions separately. A byte-preserving opaque sidecar is required for forward compatibility, but it cannot justify a semantic-coverage claim.
