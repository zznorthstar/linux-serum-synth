#pragma once
#include <algorithm>

namespace zyg::dsp {
// Original control-coordinate fit from normal-host sine measurements.
// These are native biquad Q values, not a claim about reference internals.
inline double importedEqQ(double resonance, bool shelf) noexcept {
    const double x = std::clamp(resonance / 100.0, 0.0, 1.0);
    return shelf ? 0.4 + 1.7415 * x * x * x : 0.1 + 18.9 * x * x * x * x;
}
}
