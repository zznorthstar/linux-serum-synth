#pragma once
#include <algorithm>

namespace zyg::dsp {
// Independent feed-forward dB curve. Coordinates are calibrated from original
// steady sine controls through the normal reference host, not implementation code.
inline double compressorBandThreshold(double threshold, double position) noexcept {
    const double t = std::clamp(threshold, 0.0, 1.0);
    return 10.2 - 120.8 * t * t + 0.24 * (std::clamp(position, 0.0, 200.0) - 100.0);
}
inline double compressorBandGainDb(double level, double threshold, double deadband,
                                   double downSlope, double belowSlope) noexcept {
    const double down = -std::max(0.0, level - threshold) * downSlope;
    const double below = std::max(0.0, threshold - deadband - level) * belowSlope;
    return std::clamp(down + below, -40.0, 36.0);
}
}
