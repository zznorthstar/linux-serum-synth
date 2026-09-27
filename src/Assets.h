#pragma once
// Control-thread asset preparation: resolves the user's local Serum content
// references and decodes audio into shared immutable buffers. Nothing here runs
// on the audio thread. The audio decoder is injected so the plugin can use JUCE
// (FLAC etc.) while the core library only needs WAV for its tests.
#include "Patch.h"
#include <filesystem>
#include <functional>

namespace zyg {
using AudioDecoder = std::function<bool(const std::filesystem::path& file, SampleData& out, std::string& error)>;

// RIFF WAVE (8/16/24/32-bit PCM and 32-bit float, any channel count, mixed to <=2).
bool decodeWavFile(const std::filesystem::path& file, SampleData& out, std::string& error);

// Resolves a Serum content reference (category-relative, may use "../" to reach a
// sibling content directory) beneath `root`. Returns an empty path if unsafe or missing.
std::filesystem::path resolveSerumAsset(const std::filesystem::path& root, const std::string& category,
                                        const std::string& reference);

struct SfzRegion {
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127, rootKey = 60;
    double tuneCents = 0.0, volumeDb = 0.0, pan = 0.0;
    bool loop = false; double loopStart = 0.0, loopEnd = 0.0;
    std::string sample; // as written in the SFZ, relative to the SFZ directory
};
struct SfzGroup { double ampVelTrack = 100.0, ampegRelease = 0.0; bool hasRelease = false; };
// Parses the SFZ subset Serum emits (group/region with key, velocity, tuning, loop and sample opcodes).
std::vector<SfzRegion> parseSfz(const std::string& text, SfzGroup* group = nullptr);

// Onset-based slice starts (normalized) for Auto slicing.
std::vector<double> detectSliceMarkers(const SampleData& sample, double threshold);

// The ZYG-ZXG content library (Serum-style folders: Tables, Samples, Multisamples, Impulses, ...).
// Order: $ZYGZXG_CONTENT, then $XDG_DATA_HOME (or ~/.local/share)/ZYG-ZXG/Content. Empty if absent.
// Populate it with tools/install_serum_content.sh.
std::filesystem::path defaultContentRoot();

// Loads every external asset the patch references. Failures become diagnostics
// (missing_asset / unsupported_asset) rather than exceptions.
void prepareAssets(Patch& patch, const AudioDecoder& decoder);
}
