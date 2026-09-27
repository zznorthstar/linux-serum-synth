#pragma once
// ZYG spectral oscillator: a phase-vocoder style resynthesiser. The analysis is
// computed once per sample off the audio thread; each voice then rebuilds a
// spectrum per hop (pitch mapping, spectral warps, band limits), inverse
// transforms it and overlap-adds. The spectral warps are original ZYG designs
// organised by the names Serum lists.
#include "DspCommon.h"
#include "Fft.h"
#include "Patch.h"
#include <memory>

namespace zyg {
struct SpectralAnalysis {
    static constexpr int fftSize = 2048, hop = 512, bins = fftSize / 2 + 1;
    int frames = 0;
    double sampleRate = 44100.0;
    std::vector<float> mag;   // [frame * bins + k] amplitude-calibrated magnitude
    std::vector<float> freq;  // instantaneous frequency of each bin, in (fractional) bins
};
// Control-thread only. Audio is mixed to mono; long material is truncated.
std::shared_ptr<const SpectralAnalysis> buildSpectralAnalysis(const SampleData& sample, double maxSeconds = 28.0);
}

namespace zyg::dsp {
struct SpectralParams {
    double frame = 0.0;          // analysis timeline position, in frames
    double ratio = 1.0;          // playback pitch ratio of the note
    double sourceRateRatio = 1.0;// analysis sample rate / host sample rate
    double freqLo = 20.0, freqHi = 20000.0;
    bool smoothBand = false;
    double filterShift = 0.0, filterWet = 0.0; // spectral filter: shift in [-100,100]
    std::array<WarpMode, 2> warp {WarpMode::off, WarpMode::off};
    std::array<double, 2> amount {0.0, 0.0};
    std::array<double, 2> var {0.5, 0.5};
    double detuneCents = 0.0;
    bool phaseLock = false;
};

// Tables and scratch shared by every spectral voice (rendering is single-threaded).
struct SpectralShared {
    static constexpr int N = SpectralAnalysis::fftSize, K = N / 2;
    Fft fft;
    std::vector<float> window, re, im, mag, scratch, phaseTmp, partialHz, modMag, tmpRe, tmpIm;
    void allocate();
};

class SpectralVoice {
public:
    static constexpr int N = SpectralAnalysis::fftSize, K = N / 2, H = SpectralAnalysis::hop;
    void allocate(SpectralShared* shared);  // not for the audio thread
    void reset(std::uint32_t seed) noexcept;
    // Feeds the modulator (for vocode/mask warps) and returns one output sample.
    double process(const SpectralAnalysis& a, const SpectralParams& p, double sampleRate, double modulator) noexcept;
    bool allocated() const noexcept { return !ola_.empty(); }
private:
    void synthFrame(const SpectralAnalysis& a, const SpectralParams& p, double sampleRate) noexcept;
    void applyWarp(WarpMode mode, double amount, double var, const SpectralAnalysis& a, const SpectralParams& p) noexcept;
    SpectralShared* sh_ = nullptr;
    std::vector<float> ola_, out_, phase_, modRing_;
    int idx_ = H, modW_ = 0;
    Rng rng_;
};
}
