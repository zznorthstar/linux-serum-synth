# TSP_OVERVIEW_YAANO_Synth_stutter_candy

Private user-supplied Serum 2 preset, embedded `productVersion` **2.0.24**. The preset file and Xfer assets stay outside Git. This is a partial vertical-slice audit, not a compatibility certificate.

| Checkpoint | Result |
| --- | --- |
| DISCOVERED / PARSED | Yes. 75 explicit `plainParams` fields; 26 populated top-level objects. |
| MAPPED | 43/75 explicit fields have typed C++ destinations. The other 32 are retained and individually diagnosed as unmapped. The added field is OSC A's observed `kParamWarpMenu = kFM_OSC`; typed LFO type/rate/sync state accounts for the previous increase. Structural state such as eight ordered modulation routes and five FX instances is also retained; typed-field counts do not measure that state. |
| DSP IMPLEMENTED | Partial. OSC A/B/C wavetable playback uses harmonic mip levels and cubic interpolation. The preset's Lorenz LFO 1 drives OSC A wavetable position. Macro 1 drives OSC A warp depth, and OSC B modulates OSC A at audio rate while remaining at audible level zero. That warped carrier is generated at 2× and FIR-decimated. Env 1 timing/curves and basic NOISE loop playback work. Six other matrix routes, five FX, detailed noise semantics and most architecture remain silent. |
| UI EDITABLE | Partial. OSC A/B/C wavetable source, enable, position, pitch, level, pan, initial/random phase, unison, detune and first routing target populate from the patch and can be edited in the design-kit UI. Env 1 and master are editable. Matrix rows are visible and source/destination/amount can be changed, but this preset's original route behaviors remain unsupported. Five FX identities appear on the FX tab as preserved, with DSP pending. NOISE enable/level and sample browsing are editable; SUB sine enable/level are editable natively. |
| REAL PRESET VERIFIED | **No full verification.** C++ import and a REAPER realtime transport test with the original preset produced nonzero audio. A private derivative with OSC A muted and B/C already at zero also produced nonzero output through the FLAC NOISE path. Full edit/save/reopen and listening comparison remain untested. |

## Used architecture

- **OSC A**: `Analog/SawRounded.wav` wavetable. `kParamWarpMenu = kFM_OSC` maps to native true frequency modulation from the first other main oscillator, inferred as OSC B for OSC A. Macro 1's original route supplies depth. The mapping needs a controlled Serum oracle check; ZYG's FM index/transfer is intentionally its own.
- **OSC B/C**: both enabled, using `S2 Tables/Default Shapes.wav`; B is +1 octave with two unison voices, C has 16 unison voices. Both have explicit volume zero. They are typed and audible if raised in the editor.
- **NOISE**: slot 3 enabled, sample `S2 Noises/Vinyl Crackle.flac`, volume about 0.0678. Correct slot and reference are preserved; the local FLAC is decoded on the control thread and played as a looping mono source. Serum noise playback semantics are only partly implemented.
- **SUB**: slot 4 has default state and is not active in this preset. Native SUB sine playback is implemented for user-created patches.
- **FILTERS**: both voice-filter parameter objects are `default`; no explicit filter settings. Filter 1 can be enabled and edited natively, using a ZYG trapezoidal state-variable low-pass rather than an exact Serum filter type.
- **ENVELOPES**: Env 1–4 each have three explicit curve values. Env 1 ADSHR and its three ZYG curve bends render; Env 2–4 are retained only.
- **LFOs**: LFO 1 and 2 use free-running Lorenz chaos and now map to native fixed-state generators. LFO 3/4 custom curve state remains preserved and silent. Tempo sync and detailed Serum chaos behavior have not been verified.
- **MATRIX**: eight ordered routes. LFO 1 → OSC A wavetable position and Macro 1 → OSC A warp depth are DSP-active using imported source values, amounts and polarity. Oscillator fine/volume, LFO smooth, master tuning, reverb wet and delay wet remain retained and silent. Aux, route curves and slew remain unsupported.
- **MACROS**: three explicit macro values (Macro 1/2/4) map to typed ZYG macro controls. Macro 1–4 are visible in the editor; all eight values are in the patch. Supported macro destinations are oscillator warp depth, OSC A wavetable position and Filter 1 cutoff. This preset's Macro 1 → OSC A FM-depth route renders; its FX/LFO destinations remain unsupported.
- **FX**: Main rack contains Delay, Distortion, EQ, Reverb and Compressor in order. Bus 1/2 are empty. FX DSP and routing are absent; all instances are listed on the read-only FX tab.
- **ARP / CLIPS**: no active events found in the examined state; empty clip objects are preserved.
- **GLOBAL / VOICING**: explicit global parameters are `default`; native default polyphony is provisional rather than a verified Serum default.

## Assets and failures

The four referenced external assets resolve against the user's ignored `.local-serum-content/` tree: three wavetable references (two point to the same file) and one FLAC noise reference. **Unresolved assets: 0.** Missing assets and unsupported DSP are separate statuses. The original container bytes, decoded state, ordered routes, macro values, FX identities and diagnostics survive the v2 native patch and host-state serializer; this does not imply these modules are audible.

Current C++ fixture check: `ZYG_TEST_PRESET=TSP_OVERVIEW_YAANO_Synth_stutter_candy.SerumPreset ZYG_TEST_ASSET_ROOT=.local-serum-content build/zygzxg_core_tests` reports `43/75` explicit fields mapped, 84 unsupported/unrendered diagnostics, and finite peak `-0.259292`. Its source preset has not been committed or packaged.

REAPER realtime host check of the original preset after the audio-rate warp work: `track_peak=0.002166748`, `master_peak=0.114783958`. An earlier private, uncommitted derivative with OSC A volume set to zero (B/C were already at zero) returned `track_peak=0.035572886` and `master_peak=0.035572886`; the core-only importer/render test for that derivative is silent because FLAC noise decode occurs in the plugin control layer. This isolates the plugin’s local noise-sample path without redistributing the asset. These meter readings do not validate Serum noise semantics or subjective similarity.
