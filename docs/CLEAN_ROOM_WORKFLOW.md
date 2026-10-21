# Serum interoperability research workflow

The target is preset behavior and similar musical character using original
ZYG DSP. On 2026-10-18 the user additionally authorized private REA static
analysis of the locally supplied reference binary to investigate preset
loading, control relationships and routing. The user subsequently expanded
that scope to private examination of DSP processing and its interactions with
modulation, filters, effects and voice behavior for learning and comparison.
This extends the earlier
black-box-only research scope; using an analysis tool does not itself create
independent clean-room separation.

Keep binary-derived evidence and decompiler output private and identify it as
static evidence, not recovered original source or observed execution. Do not
copy or translate Xfer implementation code into ZYG DSP. Express interoperability
findings as behavioral specifications and verify them through the licensed
reference plugin's normal UI, host parameters, preset/state interfaces and audio
output before treating a relationship as measured behavior. Record original
implementation choices and remaining uncertainty separately.

## Evidence to implementation

1. Inventory a documented feature and its actual preset keys. A decoded field
   is an observation, not a verified behavior. Preserve unknown state verbatim.
2. Specify controlled inputs: plugin version, preset state, MIDI/channel,
   velocity, tempo, sample rate, phase/randomization and duration. Prefer original
   synthetic signals to factory material when isolating a transfer function.
3. Load the reference and verify its resulting state. Sparse, invalid or old
   fields can be ignored or migrated. A successful host API return alone does
   not prove that the intended state was accepted. Discard invalid trials.
4. Measure controls, timing, levels and spectrum. Do not level-match away a
   gain error. For nondeterministic features record the randomization and use
   repeated trials rather than treating waveform subtraction as sound similarity.
5. Write the behavioral specification and uncertainty in
   [reference measurements](SERUM_ORACLE_MEASUREMENTS.md). Describe facts and
   test inputs, not a supposed proprietary algorithm inferred from its name.
6. Map understood semantics into independent typed `Patch` concepts. Implement
   original DSP choices and record differences. Add parser, render and host
   tests appropriate to the claim; retain unsupported diagnostics.
7. Repeat complete-preset comparisons. Similar level on an isolated sine does
   not establish matching timbre, modulation behavior or full-preset support.

## Private and public material

Reference binaries, user/factory presets, wavetables, samples, raw state chunks,
projects and reference WAVs stay outside the public Git checkout. They are not
release dependencies. Public artifacts are source code, original generated
test signals, aggregate facts, measured numeric behavior and limitations.
The runtime can resolve content that the user owns locally.

## Current map and next measurements

The [compatibility registry](../compatibility/serum2_support.json) and
[measurement report](SERUM_ORACLE_MEASUREMENTS.md) distinguish decoding, mapping,
native rendering and measured fidelity. Partial measured areas include sparse
defaults, envelope timing/gain/curves, ordinary LFO sync, table position,
unison pitch/phase, filter frequency controls, one overdrive range and scalar maps.

The six complete-preset trials still expose major level and spectral differences.
Atmospheric Boom loses most of its level agreement in the FX chain; bypassing
FX reduces its discrepancy from about −17 to −3.8 dB. Priorities are compressor
gain/default/range measurements, distortion/filter response, unison loudness,
warp depths and matrix aux/curve behavior. Legacy `.fxp` is still unsupported;
the supplied 300-preset Kraken bank must not be described as a tested native
Serum 2 corpus. None of this implies full compatibility or Serum DSP identity.

The first single-band compressor pass and 203 controller-declared FX defaults
are recorded in the 2026-10-18 measurement section. Declared defaults need
per-field unit conversion and do not establish a transfer function. Matrix aux
curves that are kept but not evaluated now produce an explicit import diagnostic.

The user explicitly deferred creation of an original replacement factory asset
pack until a later request. Continue resolving locally owned assets and retain
missing/substitution diagnostics; do not start generating that replacement pack.
The private Ghosthack pack is a 15-preset additional test corpus, not public
test data. Time-effect investigation and its limitations are recorded in the
2026-10-18 measurement section.

The private REA splitter-walker hypothesis has now been corroborated by 37
controlled reference renders with accepted-state capture. Whole/branch bypass,
nested boundaries and M/S behavior drove corrections to ZYG's existing original
graph, with independent native regressions and an installed VST3 host check.
Raw decompilation was not translated. Solo, bypass transitions/tails, routing
between racks, and reference modulation behavior remain separate open questions.

The delay timing follow-up reused retained REA ordering evidence and measured
102 normal state/audio passes, including startup controls and 60 primed passes.
Observed grid/free-time/loading behavior informed independent ZYG target mapping;
feedback/filter/interpolation code remains original. Timing-model migration
preserves older native saves. The measured echo-peak agreement does not establish
reference DSP identity, first-note settling or calibrated modulation transitions.
