#pragma once
#include "Patch.h"
#include <filesystem>

namespace zyg {
struct SerumDocument {
    Json metadata, state;
    std::vector<std::uint8_t> bytes;
};
SerumDocument decodeSerum(const std::vector<std::uint8_t>& bytes);
Patch importSerum(const SerumDocument& document, const std::filesystem::path& assetRoot,
                  const std::string& sourcePath = {});
Patch loadSerumFile(const std::filesystem::path& file, const std::filesystem::path& assetRoot);

// Decodes a mono PCM/IEEE-float RIFF WAVE file at an absolute path directly
// into osc.audio (frames of osc.frameSize samples each, as SynthEngine
// expects). Shared by Serum-asset-path wavetable import and native
// user-picked wavetable loading in the editor. Returns false and sets error
// without touching osc on any failure; never throws.
bool loadWavetableFromFile(Oscillator& osc, const std::filesystem::path& absolutePath, std::string& error);
}
