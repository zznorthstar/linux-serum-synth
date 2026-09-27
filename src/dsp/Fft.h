#pragma once
// In-place radix-2 complex FFT with precomputed tables. init() allocates; the
// transforms themselves do not.
#include <cmath>
#include <cstddef>
#include <vector>

namespace zyg::dsp {
class Fft {
public:
    void init(std::size_t n) {
        n_ = n;
        cosT_.resize(n / 2); sinT_.resize(n / 2); rev_.resize(n);
        for (std::size_t i = 0; i < n / 2; ++i) {
            const double a = -6.283185307179586 * double(i) / double(n);
            cosT_[i] = float(std::cos(a)); sinT_[i] = float(std::sin(a));
        }
        std::size_t bits = 0; while ((std::size_t(1) << bits) < n) ++bits;
        for (std::size_t i = 0; i < n; ++i) {
            std::size_t r = 0;
            for (std::size_t b = 0; b < bits; ++b) if (i & (std::size_t(1) << b)) r |= std::size_t(1) << (bits - 1 - b);
            rev_[i] = r;
        }
    }
    std::size_t size() const noexcept { return n_; }
    void forward(float* re, float* im) const noexcept { run(re, im, false); }
    void inverse(float* re, float* im) const noexcept {
        run(re, im, true);
        const float s = 1.0f / float(n_);
        for (std::size_t i = 0; i < n_; ++i) { re[i] *= s; im[i] *= s; }
    }
private:
    void run(float* re, float* im, bool inv) const noexcept {
        for (std::size_t i = 0; i < n_; ++i) {
            const std::size_t j = rev_[i];
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (std::size_t len = 2; len <= n_; len <<= 1) {
            const std::size_t half = len / 2, step = n_ / len;
            for (std::size_t s = 0; s < n_; s += len) {
                for (std::size_t k = 0; k < half; ++k) {
                    const float wr = cosT_[k * step], wi = inv ? -sinT_[k * step] : sinT_[k * step];
                    const std::size_t a = s + k, b = s + k + half;
                    const float tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
            }
        }
    }
    std::size_t n_ = 0;
    std::vector<float> cosT_, sinT_;
    std::vector<std::size_t> rev_;
};
}
