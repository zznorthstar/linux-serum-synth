#pragma once
#include <array>
#include <cstddef>

namespace zyg {
// Independent DSP inputs, separate from immutable preset data. The JUCE adapter
// supplies these streams; all-zero inputs preserve the imported patch exactly.
enum class HostControl : std::size_t {
    cutoff1, cutoff2, positionA, positionB, positionC, frameA, frameB, frameC, count
};
constexpr auto hostControlCount = std::size_t(HostControl::count);
using HostControlValues = std::array<float, hostControlCount>;
using HostControlStreams = std::array<const float*, hostControlCount>;
}
