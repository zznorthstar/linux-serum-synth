#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace zyg {
using Json = nlohmann::json;

// Immutable decoded audio shared (cheaply) between patch copies. Filled on a
// control thread; the audio thread only reads it.
struct SampleData {
    std::vector<float> left, right; // right is empty for mono material
    double sampleRate = 44100.0;
    int rootNote = 60;
    std::size_t frames() const noexcept { return left.size(); }
    bool stereo() const noexcept { return !right.empty(); }
};
using SamplePtr = std::shared_ptr<const SampleData>;

// A user-drawn curve/path: normalized points with a per-segment bend value in
// [0,1] where 0.5 is straight (the same convention Serum path data serializes).
struct CurvePoints {
    std::vector<double> x, y, bend;
    bool closed = true; // periodic (LFO) versus one-shot (matrix curve)
    bool empty() const noexcept { return x.empty(); }
};

enum class OscMode { wavetable, sample, multisample, granular, spectral, sub, noise, unknown };
enum class RouteTarget { filter, main, direct, none, unknown };
enum class ModSource {
    unknown, envelope, lfo, macro, velocity, note,
    modWheel, channelPressure, polyPressure, noiseAudio, random, alternate, pitchBend,
    mpeX, mpeY, mpeZ, releaseVelocity, fixed, oscAudio, filterAudio, activeVoices,
    voiceMod, voiceIndex, discreteRandom,
    sidechain,           // ZYG extension: envelope follower of the host sidechain input
    midiCC               // ZYG extension: MIDI continuous controller, index = CC number (0..119), 0..1
};
// `wavetablePosition`..`filterCutoff` are the original typed destinations and
// remain for stored state; every other destination is addressed through a
// parameter-kind + instance (+ FX slot) triple.
enum class ModTarget {
    unknown, wavetablePosition, warpOneAmount, warpTwoAmount, filterCutoff,
    oscParam, filterParam, envParam, lfoParam, macroValue, globalParam, fxParam,
    arpParam, clipParam, routingParam, lfoPointBus
};
enum class OscParam {
    volume, pan, coarse, fine, octave, detune, blend, unisonStereo, unisonWarp, unisonWarp2,
    unisonWTPos, tablePos, warp1, warp2, warpVar1, warpVar2, initialPhase, randomPhase,
    start, end, position, scanRate, loopStart, loopEnd, pitch, pitchRatio, hzOffset,
    grainLength, density, randomOffset, randomPitch, randomPan, randomGain, randomDir,
    randomGrainLength, windowParam, windowSkew, timbreShift, envAttack, envDecay,
    envSustain, envRelease, color, freqLo, freqHi, specShift, specWet, randomWarp, randomWarp2,
    count
};
enum class FilterParam { freq, reso, drive, var, wet, level, stereo, x, y, pan, count };
enum class EnvParam { attack, hold, decay, sustain, release, curve1, curve2, curve3, count };
enum class LfoParam { rate, phase, rise, delay, smooth, count };
enum class GlobalParam { masterTuning, portamentoTime, swing, transpose, voiceAmp, masterVolume,
                         directVol, fxBus1Vol, fxBus2Vol, envTimeScale, lfoTimeScale, count };
enum class OscPitchMode { semitones, harmonics, ratio };
enum class RoutingParam { filterBalance, fxBus1Level, fxBus2Level, count };

enum class LfoShape { unknown, sine, lorenz, rossler, randomHold, path, curve };
enum class LfoMode { free, trigger, envelope, oneShot };
enum class SubShape { unknown, pulse, roundedRectangle, saw, square, triangle };
enum class NoiseType { white, pink, brown, geiger };
enum class DetuneMode { linear, exponential, inverse, random, super };
enum class UnisonStack { none, center12, octave1, octave2, octave3, octaveFifth1, octaveFifth2, octaveFifth3 };
enum class LoopMode { forward, reverse, pingPong, tailed };
enum class WindowShape { blackmanHarris, expDecay, gaussian, triangle, tukey };
enum class VoicePriority { latest, low, high };

enum class FilterResponse {
    unknown, low6, low12, low18, low24, high6, high12, high18, high24,
    band12, band24, notch12, notch24, ladder6, ladder18, ladder24,
    // Second-generation families. `Filter::variant` selects the exact member.
    ladder12, ladderDirty, ladderEms, ladderAcid,
    multi, comb, flange, phaser, formant, allpass, diffuser, djMixer, shelfEq,
    polezero, exponential, screamer, waveshaper, ringMod, sampleHold, addBass,
    zdfAnalog, distComb, filterReverb, bandReject, peaking
};
enum class WarpMode {
    off, bendPositive, bendNegative, bendBoth, asymPositive, asymNegative, asymBoth,
    pwm, flip, frequencyMod, ringMod, amplitudeMod,
    hardClip, softClip, sineFold, linearFold, sineShaper, asymmetricClip, rectify,
    // Extended families
    phaseMod, frequencyModX, frequencyModPhase, sync, remap, quantize, evenOdd, selfPhase,
    filterLow, filterHigh, diode1, diode2, softSat, tapeSat, tube, stompBox, zeroSquare,
    // Spectral-oscillator warps
    addHarmonics, addSubharmonics, spectralDetune, gate, mirror, smear, spectralComb,
    spectralPitchShift, spectralShift, spread, shepardFilter, shepardNarrow,
    peakOctaveDown, peakOctaveUp, peakHarmonicDown, peakHarmonicUp, phaseTwist, vocode, mask,
    unknown
};

struct WarpDefinition {
    WarpMode mode = WarpMode::off;
    // native source index: 0-2 main oscillators, 3 NOISE, 4 SUB, 5/6 Filter 1/2 audio; -1 none
    int sourceIndex = -1;
    int variant = 0;     // Remap index etc.
    double var = 0.5;    // secondary control (kParamWarpVar), 0..1
};

// A modulation bus attached to one property of one path/curve point: target 0 = x, 1 = y, 2 = bend.
struct LfoPointMod { int point = 0, bus = 0, target = 1; };

struct LfoDefinition {
    LfoShape shape = LfoShape::unknown;
    double rateHz = 1.0;
    bool tempoSync = false;
    // Cycle length in beats when tempoSync is set (a quarter note = 1.0).
    double syncBeats = 1.0;
    LfoMode mode = LfoMode::free;
    double phaseDegrees = 0.0;
    double rise = 0.0, delay = 0.0;   // seconds
    double smooth = 0.0;              // 0..100
    bool mono = false;                // one shared phase for all voices
    bool anchored = false;
    int direction = 0;                // 0 forward, 1 backward, 2 ping-pong
    CurvePoints path;                 // curve: periodic shape (empty renders a sine); path: 2D polyline
    std::vector<LfoPointMod> pointMods; // matrix-modulated point properties
};

struct Envelope {
    double attack = 0.005, hold = 0.0, decay = 2.0, sustain = 1.0, release = 0.075;
    std::array<double, 3> curve {50.0, 66.6, 66.6};
    double start = 0.0, end = 0.0;    // level offsets (normalized)
    bool legatoInverted = false;
    bool restartOnSteal = true;       // false: a stolen/retriggered voice re-attacks from its current level
    Json additional;
};

struct SpectralAnalysis; // STFT frames of a sample, built off the audio thread (dsp/Spectral.h)

struct SampleRegion {
    SamplePtr sample;
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127, rootKey = 60;
    double tuneCents = 0.0, volumeDb = 0.0, pan = 0.0;
    double loopStart = 0.0, loopEnd = 0.0; // frames; equal means unlooped
    bool loop = false;
    std::string path;
};

struct Oscillator {
    bool enabled = false;
    OscMode mode = OscMode::unknown;
    std::string modeId;
    SubShape subShape = SubShape::unknown;
    std::string asset;
    std::string tableName;    // display name of a table without an asset path (e.g. Serum "Custom" embedded frames)
    bool userSelectedAsset = false;
    int octave = 0, unison = 1;
    double semitone = 0.0, pitch = 0.0; // coarse and pitch knobs, in semitones
    double fine = 0.0, volume = 0.75, pan = 0.0, detune = 0.2;
    double tablePosition = 0.0, initialPhase = 0.0, randomPhase = 0.0;
    std::string warpOne, warpTwo;
    double warpOneAmount = 0.0, warpTwoAmount = 0.0;
    std::array<WarpDefinition, 2> warpDefinitions;
    std::vector<float> audio; // prepared on the control thread from a user-owned asset
    // Successive octave-bandlimited copies of `audio`, excluding the raw
    // level. Each complete level has audio.size() samples. This cache is
    // derived on the control thread and is intentionally not serialized.
    std::shared_ptr<const std::vector<float>> wavetableMipmaps;
    unsigned wavetableMipLevels = 0;
    unsigned frameSize = 2048;
    double sampleRate = 44100.0; // for sample-backed NOISE playback
    Json modeState;
    Json additional;

    // ---- Unison / pitch (shared by all oscillator types)
    double blend = 75.0;             // 0..100, centre vs. detuned voices
    double unisonStereo = 100.0;     // -100..100
    double unisonRange = 2.0;        // semitones at detune = 1
    DetuneMode detuneMode = DetuneMode::linear;
    UnisonStack unisonStack = UnisonStack::none;
    double unisonWTPos = 0.0;        // -100..100 frame spread across voices
    double unisonWarp = 0.0, unisonWarp2 = 0.0; // -100..100 warp spread
    bool perVoicePhase = true;       // false = contiguous (phase memory)
    bool pitchTrack = true;
    double hzOffset = 0.0;
    OscPitchMode pitchMode = OscPitchMode::semitones;
    double pitchRatio = 1.0;
    int keyZoneLo = 0, keyZoneHi = 127, velZoneLo = 0, velZoneHi = 127;

    // ---- SUB / NOISE
    NoiseType noiseType = NoiseType::white;
    double noiseColor = 0.5;         // 0..1 tilt of the coloured noise generators
    bool noiseTypeExplicit = false;  // an imported preset named its generator type
    bool oneShot = false;            // noise sample plays once per note
    bool contiguousPhase = false;

    // ---- Sample / multisample / granular / spectral shared state
    int baseNote = 60;                 // root key of a Sample-mode source
    std::string embeddedSfz;           // multisample mapping text (Serum embeds it in the preset)
    std::vector<std::string> childFiles; // multisample child sample references
    SamplePtr sample;
    std::shared_ptr<const SpectralAnalysis> spectral; // derived; not serialized
    std::vector<SampleRegion> regions; // multisample key/velocity map
    double start = 0.0, end = 100.0;   // percent of the sample
    double loopStart = 0.0, loopEnd = 100.0, loopCrossfade = 0.0;
    LoopMode loopMode = LoopMode::forward;
    bool looping = false;
    bool reverse = false;
    double randomStart = 0.0;          // percent
    double position = 0.0;             // granular/spectral timeline position, percent
    double scanRate = 0.0;             // percent/s style scan speed, -200..200
    double scanRange = 1.0;
    bool scanTempoLock = false;
    double timbreShift = 0.0;
    double velTrack = 0.0;
    struct SampleEnv { double delay = 0, attack = 0, hold = 0, decay = 0, sustain = 1, release = 0.05; bool override_ = false; bool useSfzRelease = false; } sampleEnv;
    bool velTrackOverride = false;
    // granular
    double density = 20.0;             // grains per second
    bool densityBpm = false;
    double grainLength = 0.1;          // seconds
    bool lengthBpm = false;
    WindowShape windowShape = WindowShape::gaussian;
    double windowParam = 50.0, windowSkew = 0.0;
    double randomOffset = 0.0, randomDir = 0.0, randomGain = 0.0, randomPan = 0.0,
           randomPitch = 0.0, randomGrainLength = 0.0, randomWarp = 0.0, randomWarp2 = 0.0,
           randomWindowAmount = 0.0, randomWindowSkew = 0.0;
    int unisonTrigPattern = 0;         // 0 even, 1 exponential, 2 random
    // spectral
    double freqLo = 20.0, freqHi = 20000.0;
    bool loHiSmooth = false, phaseLock = false, keepTransients = false;
    double specFilterShift = 0.0, specFilterWet = 0.0;
    int slicingMode = 0;               // 0 off, 1 auto, 2 manual
    std::vector<double> sliceMarkers;  // normalized slice starts; empty + auto = detected on load
    int sliceRoot = 36;                // key of slice 0
    double baseTempo = 120.0;
};

struct Filter {
    bool enabled = false;
    std::string type = "L12";
    FilterResponse response = FilterResponse::low12;
    int variant = 0;                 // exact member of a response family (see Filters.cpp)
    double cutoff = 1.0, resonance = 10.0, drive = 0.0, var = 0.0, wet = 100.0;
    double level = 0.5, stereo = 50.0, x = 0.5, y = 0.5;
    bool keyTrack = false, pad = false;
    Json additional;
};
struct Route {
    RouteTarget target = RouteTarget::unknown;
    double filterBalance = -100.0;   // -100 = Filter 1 only, 0 = both, +100 = Filter 2 only
    double fxBus1Level = 0.0, fxBus2Level = 0.0;
    // Envelopes that shape this route's signal where it leaves for Main/Direct/a bus.
    std::array<bool, 4> viaEnv {true, false, false, false};
    Json additional;
};
struct ModulationRoute {
    int slot = 0, source = 0, auxiliary = 0, destinationInstance = 0, destinationParameterId = -1;
    ModSource sourceKind = ModSource::unknown;
    int sourceIndex = 0;
    ModSource auxKind = ModSource::unknown;
    int auxIndex = 0;
    ModTarget targetKind = ModTarget::unknown;
    int targetIndex = 0;
    int targetParam = 0;             // OscParam/FilterParam/... or FX parameter slot
    std::string sourceName, destinationModule, destinationParameter;
    double amount = 0.0;
    bool bipolar = false, bypass = false;
    // Serum-derived response shaping. Curves are native ZYG interpretations.
    double curveIn = 0.0, curveOut = 0.0;      // -100..100 power bends
    bool auxInverted = false;
    double smoothRise = 0.0, smoothFall = 0.0; // 0..100
    double delaySeconds = 0.0;
    CurvePoints mainCurve, auxCurve;
    Json additional;
};

enum class FxType {
    unknown, bode, chorus, comp, conv, delay, distortion, eq, filter, flanger, hyperD,
    phaser, reverb, utils, split, split3, splitMS,
    pump, stutter        // ZYG extensions (not Serum effects): tempo/sidechain ducker and beat-repeat glitch
};
inline constexpr int fxParamSlots = 24;
inline constexpr int fxLevelSlot = 23; // LevelOut, shared by every module type
struct ConvIr; // partitioned impulse response prepared off the audio thread (dsp/FxEngine.h)
struct FxModule {
    std::string type;                 // provenance name (e.g. "FXDelay")
    FxType fxType = FxType::unknown;
    int rack = 0, position = 0;
    bool enabled = true;
    Json parameters;                  // raw provenance
    Json additional;
    // Typed parameters, indexed by the slot tables in FxParams.h. `set` records
    // which slots were explicitly present so defaults stay distinguishable.
    std::array<double, fxParamSlots> p {};
    std::array<bool, fxParamSlots> set {};
    FilterResponse filterResponse = FilterResponse::low12; // FXFilter type
    int filterVariant = 0;            // FXFilter family member
    int modeVariant = 0;              // distortion / reverb / delay mode enum
    SamplePtr impulse;                // FXConv impulse response audio
    std::shared_ptr<const ConvIr> convIr; // derived; not serialized
    std::string impulsePath;
};

struct Diagnostic {
    std::string path, status, detail;
};

struct GlobalState {
    double directVol = 1.0, fxBus1Vol = 1.0, fxBus2Vol = 1.0;
    int fxBus1Dest = 1, fxBus2Dest = 1; // 1 = Main, 2 = the other bus/none
    double portamentoTime = 0.0, portamentoCurve = 50.0;
    bool portaAlways = false, portaScaled = false, legato = false;
    double bendUp = 2.0, bendDown = -2.0;
    VoicePriority priority = VoicePriority::latest;
    double masterTuning = 440.0, transpose = 0.0, swing = 50.0;
    int oversampling = 0;
    bool noteLatch = false, limitSameNote = false;
    double voiceAmp = 1.0, modWheel = 0.0;
    double velocityAmpDepth = 0.0;   // 0 = velocity only acts through the matrix (Serum behaviour)
    double envTimeScale = 50.0, lfoTimeScale = 50.0; // 50 = unity
    int pitchQuantizerKey = 0, pitchQuantizerScale = 0; // Serum ids, retained as provenance
    int pitchQuantizerMask = 0;      // native: allowed pitch classes (bit 0 = key); 0 = off
    bool midiOutClipPlayer = false;
    bool mpeEnabled = false;
    double mpePitchBendRange = 48.0;
    // Serum "apply tuning" switches. ZYG has no microtuning tables, so they only matter once one exists.
    bool noteCurveApplyTuning = false, pitchExpressionApplyTuning = false;
    bool serum1Compatibility = false;   // Serum's legacy flag; behaviour differences are undocumented
};

struct VoicePanel {
    // Per-voice-index offsets (voice 1..8): detune, pan, envelope time, cutoff, mods
    std::array<double, 8> detune {}, pan {}, envTime {}, cutoff {}, mod1 {}, mod2 {};
    double randomDetune = 0.0, randomPan = 0.0, randomEnvTime = 0.0, randomCutoff = 0.0;
    double scalingEnvTime = 0.0, scalingLfoTime = 0.0;
    int voiceCount = 8;
    bool active = false;
};

struct ArpStep { double time = 0, length = 0.25; int note = 0; double velocity = 0.8; };
struct ArpClipDef {
    double rate = 0.25;              // step length in beats
    bool dotted = false, triplet = false;
    std::string shape = "Up";        // Serum shape name; the arp engine interprets it
    int repeats = 1;
    double wrapRange = 12.0;
    std::string wrapMode;
    int wrapPhantomNote = 60;
    double gate = 100.0, chance = 100.0;
    double transpose = 0.0;
    int transposeRange = 1;
    std::string transposeShape;
    bool retrigger = true;
    double lengthBeats = 4.0;
    std::vector<ArpStep> steps;      // "Pattern" clip content
};
struct ArpSettings {
    bool enabled = false;
    int activeClip = 0;
    int keyZoneMin = 0, keyZoneMax = 127;
    std::array<ArpClipDef, 12> clips;
    const ArpClipDef& active() const noexcept { return clips[std::size_t(activeClip < 0 ? 0 : activeClip > 11 ? 11 : activeClip)]; }
};
struct ClipNote { double time = 0, length = 1; int note = 60; double velocity = 0.8; };
struct ClipDef {
    double rate = 1.0, lengthBeats = 16.0;
    std::string playbackMode = "OneShot";   // OneShot, Pendulum, Random, Static
    std::string spanMode = "Mono";          // Mono, Offset, Poly
    bool retrigger = true, noteGate = false, triplet = false, dotted = false;
    int transpose = 0;
    std::vector<ClipNote> notes;
};
struct ClipSettings {
    bool enabled = false;
    int selectOctave = -3;
    std::array<ClipDef, 12> clips;
};

struct Patch {
    std::string name, author, serumVersion, sourcePath, assetRoot;
    double masterVolume = 0.7;
    bool mono = false;
    int polyphony = 16;
    double lfoOneRateHz = 1.0;
    bool lfoOneSine = true; // native sine; unsupported imported LFO shapes stay visible as raw state
    std::array<Oscillator, 5> oscillators;
    std::array<Filter, 2> filters;
    std::array<Route, 7> routes;
    std::array<Envelope, 4> envelopes;
    std::vector<ModulationRoute> modulation;
    std::vector<FxModule> fx;
    std::array<Json, 10> lfos;
    std::array<LfoDefinition, 10> lfoDefinitions;
    std::array<Json, 8> macros;
    std::array<double, 8> macroValues {};
    Json arp, clips, global, unknownSerumState;
    std::array<Json, 12> arpClips, midiClips;
    GlobalState globals;
    VoicePanel voicePanel;
    ArpSettings arpSettings;
    ClipSettings clipSettings;
    std::uint64_t presetId = 0;     // stable across edits; changes when a different preset loads
    std::vector<Diagnostic> diagnostics;
    std::vector<std::string> typedParameterPaths;
    std::vector<std::uint8_t> originalPreset;
    unsigned explicitParameters = 0, mappedParameters = 0;
};

std::string sourceName(int id);
std::string statusSummary(const Patch& patch);

// Native ZYG preset format (v2, ".zygpreset"). Serializes the independent
// patch and preservation sidecar, including unsupported imported state.
// Audio assets remain local file references and are reloaded off the audio
// thread. Serialization of state does not imply DSP/UI support for it.
Json patchToJson(const Patch& patch);
Patch patchFromJson(const Json& json);

std::string oscModeToString(OscMode mode);
OscMode oscModeFromString(const std::string& name);
std::string routeTargetToString(RouteTarget target);
RouteTarget routeTargetFromString(const std::string& name);

// Value range of a modulation destination in the units stored by the patch.
// Matrix amounts are a percentage of (hi - lo).
// `log`: the parameter's normalized knob position is logarithmic between max(lo, hi * 1e-4) and hi
// (times and frequencies), so a percentage of modulation moves it by a ratio, not a fixed amount.
struct ParamRange { double lo, hi; bool log = false; };
// Applies a normalized modulation delta (a fraction of the knob travel) to a stored value.
double applyModulation(double base, double normalizedDelta, const ParamRange& range) noexcept;
ParamRange oscParamRange(OscParam param) noexcept;
ParamRange filterParamRange(FilterParam param) noexcept;
ParamRange envParamRange(EnvParam param) noexcept;
ParamRange lfoParamRange(LfoParam param) noexcept;
ParamRange globalParamRange(GlobalParam param) noexcept;
ParamRange routingParamRange(RoutingParam param) noexcept;

// Serum modulation-matrix source id <-> native source kind/index. Unresolved ids
// return false and leave the outputs unchanged.
bool modSourceFromId(int id, ModSource& kind, int& index) noexcept;
// Effective per-source polarity: sources that are natively bipolar (LFOs, pitch
// bend, audio) versus unipolar (envelopes, velocity, ...).
bool modSourceIsBipolar(ModSource kind) noexcept;
}
