# TSP_OVERVIEW_YAANO_Synth_stutter_candy

Private user-supplied Serum 2 preset, embedded `productVersion` **2.0.24**. The preset file and Xfer assets stay outside Git. This is a partial vertical-slice audit, not a compatibility certificate.

| Checkpoint | Result |
| --- | --- |
| DISCOVERED / PARSED | Yes. 75 explicit `plainParams` fields; 26 populated top-level objects. |
| MAPPED | 33/75 explicit fields have typed C++ destinations. The other 42 are retained and individually diagnosed as unmapped. Structural state such as eight ordered modulation routes and five FX instances is also retained; typed-field counts do not measure that state. |
| DSP IMPLEMENTED | Partial. OSC A/B/C wavetable playback and Env 1 work. Seven explicit B/C controls (enable, octave, unison, volume) affect the native oscillator path; the two explicit B/C volume values are zero. The main audible wavetable comes from an implicit OSC A default. Imported LFOs, FM warp, FX and these eight modulation routes do not render. NOISE slot 3 now plays its resolved local FLAC sample with basic loop playback; Serum noise pitch/color/one-shot/stereo semantics remain unsupported. |
| UI EDITABLE | Partial. OSC A/B/C wavetable source, enable, position, pitch, level, pan, initial/random phase, unison, detune and first routing target populate from the patch and can be edited in the design-kit UI. Env 1 and master are editable. Matrix rows are visible and source/destination/amount can be changed, but this preset's original route behaviors remain unsupported. Five FX identities appear on the FX tab as preserved, with DSP pending. NOISE enable/level and sample browsing are editable; SUB sine enable/level are editable natively. |
| REAL PRESET VERIFIED | **No full verification.** C++ import and a REAPER realtime transport test with the original preset produced nonzero audio. A private derivative with OSC A muted and B/C already at zero also produced nonzero output through the FLAC NOISE path. Full edit/save/reopen and listening comparison remain untested. |

## Used architecture

- **OSC A**: `Analog/SawRounded.wav` wavetable. Its mode state includes `kParamWarpMenu = kFM_OSC`; FM warp is not mapped or rendered.
- **OSC B/C**: both enabled, using `S2 Tables/Default Shapes.wav`; B is +1 octave with two unison voices, C has 16 unison voices. Both have explicit volume zero. They are typed and audible if raised in the editor.
- **NOISE**: slot 3 enabled, sample `S2 Noises/Vinyl Crackle.flac`, volume about 0.0678. Correct slot and reference are preserved; the local FLAC is decoded on the control thread and played as a looping mono source. Serum noise playback semantics are only partly implemented.
- **SUB**: slot 4 has default state and is not active in this preset. Native SUB sine playback is implemented for user-created patches.
- **FILTERS**: both voice-filter parameter objects are `default`; no explicit filter settings. Filter 1 can be enabled and edited natively, using a ZYG trapezoidal state-variable low-pass rather than an exact Serum filter type.
- **ENVELOPES**: Env 1–4 each have three explicit curve values. Only Env 1 ADSHR timing is rendered; imported curve shape is not.
- **LFOs**: LFO 1 and 2 use Lorenz chaos; LFO 3/4 have custom curve state. They are preserved as imported raw state but are not rendered. The native sine LFO 1 is a separate ZYG mode and is not silently substituted for these shapes.
- **MATRIX**: eight ordered routes. Destinations include OSC A wavetable position and warp, oscillator fine/volume, LFO smooth, master tuning, reverb wet and delay wet. All eight are visible and retained. Their original source/shape/target combinations are not executed; the user can explicitly click **Use sine** and set a row to native LFO 1 → OSC A WT position or Filter 1 cutoff to obtain ZYG modulation. This changes the patch from the imported LFO behavior.
- **MACROS**: three explicit macro values (Macro 1/2/4) map to typed ZYG macro controls. Macro 1–4 are visible in the editor; all eight values are in the patch. Macro sources render only when routed to OSC A WT position or Filter 1 cutoff, while this preset routes them to unsupported warp/FX/LFO targets.
- **FX**: Main rack contains Delay, Distortion, EQ, Reverb and Compressor in order. Bus 1/2 are empty. FX DSP and routing are absent; all instances are listed on the read-only FX tab.
- **ARP / CLIPS**: no active events found in the examined state; empty clip objects are preserved.
- **GLOBAL / VOICING**: explicit global parameters are `default`; native default polyphony is provisional rather than a verified Serum default.

## Assets and failures

The four referenced external assets resolve against the user's ignored `.local-serum-content/` tree: three wavetable references (two point to the same file) and one FLAC noise reference. **Unresolved assets: 0.** Missing assets and unsupported DSP are separate statuses. The original container bytes, decoded state, ordered routes, macro values, FX identities and diagnostics survive the v2 native patch and host-state serializer; this does not imply these modules are audible.

Current C++ fixture check: `ZYG_TEST_PRESET=TSP_OVERVIEW_YAANO_Synth_stutter_candy.SerumPreset ZYG_TEST_ASSET_ROOT=.local-serum-content build/zygzxg_core_tests` imports and renders finite audio. Its source preset has not been committed or packaged.

REAPER realtime host check of the original preset: `track_peak=0.015031695`, `master_peak=0.123394452`. A private, uncommitted derivative with OSC A volume set to zero (B/C were already at zero) returned `track_peak=0.035572886` and `master_peak=0.035572886`; the core-only importer/render test for that derivative is silent because FLAC noise decode occurs in the plugin control layer. This isolates the plugin’s local noise-sample path without redistributing the asset. These meter readings do not validate Serum noise semantics or subjective similarity.
