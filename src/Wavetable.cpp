#include "Wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace zyg {
namespace {

bool isPowerOfTwo(std::size_t value) noexcept {
    return value >= 2 && (value & (value - 1)) == 0;
}

void fft(std::vector<std::complex<double>>& values, bool inverse) {
    const std::size_t size = values.size();
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const double angle = (inverse ? 2.0 : -2.0) * std::numbers::pi / double(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> rotation(1.0, 0.0);
            for (std::size_t i = 0; i < length / 2; ++i) {
                const auto even = values[start + i];
                const auto odd = values[start + i + length / 2] * rotation;
                values[start + i] = even + odd;
                values[start + i + length / 2] = even - odd;
                rotation *= step;
            }
        }
    }
    if (inverse) for (auto& value : values) value /= double(size);
}

}

void prepareWavetableMipmaps(Oscillator& oscillator) {
    oscillator.wavetableMipmaps.reset();
    oscillator.wavetableMipLevels = 0;
    if (oscillator.mode != OscMode::wavetable || !isPowerOfTwo(oscillator.frameSize)
        || oscillator.audio.size() < oscillator.frameSize
        || oscillator.audio.size() % oscillator.frameSize != 0)
        return;

    const std::size_t size = oscillator.frameSize;
    const std::size_t frameCount = oscillator.audio.size() / size;
    unsigned levels = 0;
    for (std::size_t harmonics = size / 2; harmonics > 1; harmonics >>= 1) ++levels;
    if (levels == 0) return;

    auto mipmaps = std::make_shared<std::vector<float>>(
        std::size_t(levels) * oscillator.audio.size());
    std::vector<std::complex<double>> spectrum(size);
    std::vector<std::complex<double>> filtered(size);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const std::size_t frameOffset = frame * size;
        for (std::size_t i = 0; i < size; ++i)
            spectrum[i] = std::complex<double>(oscillator.audio[frameOffset + i], 0.0);
        fft(spectrum, false);

        for (unsigned level = 1; level <= levels; ++level) {
            filtered = spectrum;
            const std::size_t highestHarmonic = (size / 2) >> level;
            for (std::size_t bin = highestHarmonic + 1; bin < size - highestHarmonic; ++bin)
                filtered[bin] = {};
            fft(filtered, true);
            const std::size_t output = std::size_t(level - 1) * oscillator.audio.size() + frameOffset;
            for (std::size_t i = 0; i < size; ++i)
                (*mipmaps)[output + i] = float(filtered[i].real());
        }
    }
    oscillator.wavetableMipmaps = std::move(mipmaps);
    oscillator.wavetableMipLevels = levels;
}

}
