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
}
