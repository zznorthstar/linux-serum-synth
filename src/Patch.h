#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace zyg {
using Json = nlohmann::json;

enum class OscMode { wavetable, sample, multisample, granular, spectral, sub, noise, unknown };
enum class RouteTarget { filter, main, direct, none, unknown };
enum class ModSource { unknown, lfo, macro };
enum class ModTarget { unknown, wavetablePosition, filterCutoff };
enum class LfoShape { unknown, sine, lorenz, rossler, randomHold };

struct LfoDefinition {
    LfoShape shape = LfoShape::unknown;
    double rateHz = 1.0;
    bool tempoSync = false;
};

struct Envelope {
    double attack = 0.005, hold = 0.0, decay = 2.0, sustain = 1.0, release = 0.075;
    std::array<double, 3> curve {50.0, 66.6, 66.6};
    Json additional;
};
struct Oscillator {
    bool enabled = false;
    OscMode mode = OscMode::unknown;
    std::string modeId;
    std::string asset;
    bool userSelectedAsset = false;
    int octave = 0, semitone = 0, unison = 1;
    double fine = 0.0, volume = 0.75, pan = 0.0, detune = 0.2;
    double tablePosition = 0.0, initialPhase = 0.0, randomPhase = 0.0;
    std::string warpOne, warpTwo;
    double warpOneAmount = 0.0, warpTwoAmount = 0.0;
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
};
struct Filter {
    bool enabled = false;
    std::string type = "L12";
    double cutoff = 1.0, resonance = 10.0, drive = 0.0, var = 0.0, wet = 100.0;
    Json additional;
};
struct Route {
    RouteTarget target = RouteTarget::unknown;
    double filterBalance = 0.0;
    double fxBus1Level = 0.0, fxBus2Level = 0.0;
    Json additional;
};
struct ModulationRoute {
    int slot = 0, source = 0, auxiliary = 0, destinationInstance = 0, destinationParameterId = -1;
    ModSource sourceKind = ModSource::unknown;
    int sourceIndex = 0;
    ModTarget targetKind = ModTarget::unknown;
    int targetIndex = 0;
    std::string sourceName, destinationModule, destinationParameter;
    double amount = 0.0;
    bool bipolar = false, bypass = false;
    Json additional;
};
struct FxModule {
    std::string type;
    int rack = 0, position = 0;
    bool enabled = true;
    Json parameters;
    Json additional;
};
struct Diagnostic {
    std::string path, status, detail;
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
}
