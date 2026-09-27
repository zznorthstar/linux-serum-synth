#include "Spectral.h"

namespace zyg {
std::shared_ptr<const SpectralAnalysis> buildSpectralAnalysis(const SampleData& sample, double maxSeconds) {
    using namespace dsp;
    if (sample.frames() < 64) return nullptr;
    constexpr int N = SpectralAnalysis::fftSize, H = SpectralAnalysis::hop, B = SpectralAnalysis::bins;
    const std::size_t len = std::min<std::size_t>(sample.frames(), std::size_t(maxSeconds * sample.sampleRate));
    std::vector<float> mono(len);
    for (std::size_t i = 0; i < len; ++i)
        mono[i] = sample.stereo() ? 0.5f * (sample.left[i] + sample.right[i]) : sample.left[i];
    auto out = std::make_shared<SpectralAnalysis>();
    out->sampleRate = sample.sampleRate;
    out->frames = std::max<int>(1, int((len > std::size_t(N) ? len - N : 0) / H) + 1);
    out->mag.assign(std::size_t(out->frames) * B, 0.0f);
    out->freq.assign(out->mag.size(), 0.0f);
    Fft fft; fft.init(N);
    std::vector<float> win(N), re(N), im(N), prevPhase(B, 0.0f);
    for (int i = 0; i < N; ++i) win[std::size_t(i)] = float(0.5 - 0.5 * std::cos(tau * i / N));
    for (int f = 0; f < out->frames; ++f) {
        const std::size_t at = std::size_t(f) * H;
        for (int i = 0; i < N; ++i) {
            const std::size_t s = at + std::size_t(i);
            re[std::size_t(i)] = (s < len ? mono[s] : 0.0f) * win[std::size_t(i)]; im[std::size_t(i)] = 0.0f;
        }
        fft.forward(re.data(), im.data());
        for (int k = 0; k < B; ++k) {
            const double m = std::hypot(double(re[std::size_t(k)]), double(im[std::size_t(k)]));
            const double ph = std::atan2(double(im[std::size_t(k)]), double(re[std::size_t(k)]));
            // Calibrated so an amplitude-A sinusoid has magnitude A * N / 4 (Hann window).
            out->mag[std::size_t(f) * B + std::size_t(k)] = float(m);
            double dev = 0.0;
            if (f > 0) {
                double d = ph - double(prevPhase[std::size_t(k)]) - tau * double(k) * H / N;
                d -= tau * std::round(d / tau);
                dev = d / (tau * double(H) / N);
            }
            out->freq[std::size_t(f) * B + std::size_t(k)] = float(double(k) + clampd(dev, -2.0, 2.0));
            prevPhase[std::size_t(k)] = float(ph);
        }
    }
    // The first frame has no predecessor to measure phase advance against; borrow the next frame's estimate.
    if (out->frames > 1) std::copy(out->freq.begin() + B, out->freq.begin() + 2 * B, out->freq.begin());
    return out;
}
}

namespace zyg::dsp {
void SpectralShared::allocate() {
    fft.init(N);
    window.resize(N); re.assign(N, 0.0f); im.assign(N, 0.0f);
    mag.assign(K + 1, 0.0f); scratch.assign(K + 1, 0.0f); phaseTmp.assign(K + 1, 0.0f); partialHz.assign(K + 1, 0.0f);
    modMag.assign(K + 1, 0.0f); tmpRe.assign(N, 0.0f); tmpIm.assign(N, 0.0f);
    for (int i = 0; i < N; ++i) window[std::size_t(i)] = float(0.5 - 0.5 * std::cos(tau * i / N));
}

void SpectralVoice::allocate(SpectralShared* shared) {
    sh_ = shared;
    ola_.assign(N, 0.0f); out_.assign(H, 0.0f); phase_.assign(K + 1, 0.0f); modRing_.assign(N, 0.0f);
    reset(1);
}

void SpectralVoice::reset(std::uint32_t seed) noexcept {
    if (!allocated()) return;
    rng_ = Rng(seed);
    std::fill(ola_.begin(), ola_.end(), 0.0f); std::fill(out_.begin(), out_.end(), 0.0f);
    std::fill(modRing_.begin(), modRing_.end(), 0.0f);
    for (auto& p : phase_) p = float(rng_.unipolar() * tau);
    idx_ = H; modW_ = 0;
}

void SpectralVoice::applyWarp(WarpMode mode, double amount, double var, const SpectralAnalysis& a, const SpectralParams& p) noexcept {
    const double amt = clampd(amount, 0.0, 1.0);
    if (amt <= 1.0e-6) return;
    auto& m = sh_->mag; auto& s = sh_->scratch;
    const int n = K + 1;
    std::copy(m.begin(), m.end(), s.begin());
    switch (mode) {
        case WarpMode::spectralShift: {
            const int shift = int(std::lround(amt * 96.0));
            for (int j = 0; j < n; ++j) m[std::size_t(j)] = j - shift >= 0 ? s[std::size_t(j - shift)] : 0.0f;
            for (int j = 0; j < n; ++j) sh_->partialHz[std::size_t(j)] += float(shift * 0.0f);
            break;
        }
        case WarpMode::spectralPitchShift: {
            const double r = std::exp2(amt * 12.0 / 12.0);
            for (int j = 0; j < n; ++j) {
                const double sb = j / r; const int k = int(sb); const double t = sb - k;
                m[std::size_t(j)] = k + 1 < n ? float(lerp(s[std::size_t(k)], s[std::size_t(k + 1)], t)) : 0.0f;
                sh_->partialHz[std::size_t(j)] *= float(r);
            }
            break;
        }
        case WarpMode::spread: case WarpMode::smear: {
            const int w = 1 + int(amt * (mode == WarpMode::spread ? 24.0 : 12.0));
            for (int j = 0; j < n; ++j) {
                double sum = 0.0, wsum = 0.0;
                for (int d = -w; d <= w; ++d) {
                    const int k = j + d; if (k < 0 || k >= n) continue;
                    const double wt = 1.0 - std::abs(double(d)) / (w + 1); sum += wt * s[std::size_t(k)]; wsum += wt;
                }
                m[std::size_t(j)] = float(lerp(s[std::size_t(j)], sum / std::max(wsum, 1e-9) * 1.5, amt));
            }
            break;
        }
        case WarpMode::mirror: {
            const int c = 20 + int(var * 300.0);
            for (int j = 0; j < n; ++j) {
                const int k = 2 * c - j;
                const float mirrored = (k >= 0 && k < n && j > c) ? s[std::size_t(k)] : 0.0f;
                m[std::size_t(j)] = s[std::size_t(j)] + float(amt) * mirrored;
            }
            break;
        }
        case WarpMode::gate: {
            float peak = 0.0f; for (int j = 0; j < n; ++j) peak = std::max(peak, s[std::size_t(j)]);
            const float thr = float(amt * 0.5) * peak;
            for (int j = 0; j < n; ++j) m[std::size_t(j)] = s[std::size_t(j)] >= thr ? s[std::size_t(j)] : 0.0f;
            break;
        }
        case WarpMode::spectralComb: {
            const double period = 3.0 + var * 60.0;
            for (int j = 0; j < n; ++j) m[std::size_t(j)] = s[std::size_t(j)] * float(1.0 - amt + amt * (0.5 + 0.5 * std::cos(tau * j / period)) * 1.6);
            break;
        }
        case WarpMode::addHarmonics: {
            for (int j = 0; j < n; ++j) {
                float add = 0.0f;
                if (j % 2 == 0) add += 0.7f * s[std::size_t(j / 2)];
                if (j % 3 == 0) add += 0.5f * s[std::size_t(j / 3)];
                m[std::size_t(j)] = s[std::size_t(j)] + float(amt) * add;
            }
            break;
        }
        case WarpMode::addSubharmonics: {
            for (int j = 0; j < n; ++j) {
                const int k = j * 2;
                m[std::size_t(j)] = s[std::size_t(j)] + float(amt) * (k < n ? 0.7f * s[std::size_t(k)] : 0.0f);
            }
            break;
        }
        case WarpMode::spectralDetune: {
            const double shift = amt * 6.0;
            for (int j = 0; j < n; ++j) {
                const double sb = j - shift; const int k = int(std::floor(sb)); const double t = sb - k;
                const float shifted = (k >= 0 && k + 1 < n) ? float(lerp(s[std::size_t(k)], s[std::size_t(k + 1)], t)) : 0.0f;
                m[std::size_t(j)] = 0.7f * s[std::size_t(j)] + float(amt) * 0.6f * shifted;
            }
            break;
        }
        case WarpMode::shepardFilter: case WarpMode::shepardNarrow: {
            const double sharp = mode == WarpMode::shepardNarrow ? 4.0 : 1.0;
            for (int j = 1; j < n; ++j) {
                const double oct = std::log2(double(j)) - std::floor(std::log2(double(j)));
                const double g = std::pow(0.5 + 0.5 * std::cos(tau * (oct - var)), sharp);
                m[std::size_t(j)] = s[std::size_t(j)] * float(1.0 - amt + amt * g * 1.6);
            }
            break;
        }
        case WarpMode::peakOctaveUp: case WarpMode::peakOctaveDown: case WarpMode::peakHarmonicUp: case WarpMode::peakHarmonicDown: {
            const bool up = mode == WarpMode::peakOctaveUp || mode == WarpMode::peakHarmonicUp;
            const int mult = (mode == WarpMode::peakOctaveUp || mode == WarpMode::peakOctaveDown) ? 2 : 3;
            for (int j = 2; j < n - 2; ++j) {
                const bool peak = s[std::size_t(j)] > s[std::size_t(j - 1)] && s[std::size_t(j)] >= s[std::size_t(j + 1)]
                                  && s[std::size_t(j)] > 0.02f * float(a.frames > 0 ? 1.0 : 1.0);
                if (!peak) continue;
                const int k = up ? j * mult : j / mult;
                if (k > 0 && k < n) m[std::size_t(k)] += float(amt) * s[std::size_t(j)];
            }
            break;
        }
        case WarpMode::phaseTwist: {
            for (int j = 0; j < n; ++j) phase_[std::size_t(j)] += float(amt * (rng_.bipolar()) * pi);
            break;
        }
        case WarpMode::vocode: case WarpMode::mask: {
            float mmax = 0.0f; for (int j = 0; j < n; ++j) mmax = std::max(mmax, sh_->modMag[std::size_t(j)]);
            if (mmax < 1.0e-4f) break;   // a silent modulator leaves the spectrum untouched
            for (int j = 0; j < n; ++j) {
                const float g = sh_->modMag[std::size_t(j)] / mmax;
                const float w = mode == WarpMode::vocode ? g * 2.0f : (g > 0.1f ? 1.0f : 0.0f);
                m[std::size_t(j)] = float(lerp(s[std::size_t(j)], s[std::size_t(j)] * w, amt));
            }
            break;
        }
        default: break;
    }
    (void) p;
}

void SpectralVoice::synthFrame(const SpectralAnalysis& a, const SpectralParams& p, double sr) noexcept {
    const int n = K + 1;
    const int frames = a.frames;
    const double fpos = clampd(p.frame, 0.0, double(frames - 1));
    const int f0 = int(fpos), f1 = std::min(f0 + 1, frames - 1);
    const double ft = fpos - f0;
    const int fn = ft < 0.5 ? f0 : f1;
    const double srr = std::max(p.sourceRateRatio * p.ratio, 1.0e-4);
    // 0.75 restores unity gain for the analysis/synthesis Hann pair at 75% overlap.
    const float ratioComp = float(0.75 / std::sqrt(std::max(p.ratio, 1.0)));
    const float* m0 = a.mag.data() + std::size_t(f0) * n; const float* m1 = a.mag.data() + std::size_t(f1) * n;
    const float* fq = a.freq.data() + std::size_t(fn) * n;
    const double binHz = a.sampleRate / SpectralAnalysis::fftSize;
    for (int j = 0; j < n; ++j) {
        const double sb = j / srr;
        if (sb >= K - 1) { sh_->mag[std::size_t(j)] = 0.0f; sh_->partialHz[std::size_t(j)] = float(j * sr / N); continue; }
        const int k = int(sb); const double kt = sb - k;
        const double ma = lerp(m0[k], m0[k + 1], kt), mb = lerp(m1[k], m1[k + 1], kt);
        sh_->mag[std::size_t(j)] = float(lerp(ma, mb, ft)) * ratioComp;
        const int kn = int(std::lround(sb));
        sh_->partialHz[std::size_t(j)] = float(double(fq[std::min(kn, K)]) * binHz * p.ratio);
    }
    // modulator spectrum (vocode / mask)
    bool needMod = false;
    for (int s = 0; s < 2; ++s) needMod |= p.warp[std::size_t(s)] == WarpMode::vocode || p.warp[std::size_t(s)] == WarpMode::mask;
    if (needMod) {
        for (int i = 0; i < N; ++i) { sh_->tmpRe[std::size_t(i)] = modRing_[std::size_t((modW_ + i) % N)] * sh_->window[std::size_t(i)]; sh_->tmpIm[std::size_t(i)] = 0.0f; }
        sh_->fft.forward(sh_->tmpRe.data(), sh_->tmpIm.data());
        for (int j = 0; j < n; ++j) sh_->modMag[std::size_t(j)] = std::hypot(sh_->tmpRe[std::size_t(j)], sh_->tmpIm[std::size_t(j)]);
    }
    for (int s = 0; s < 2; ++s) applyWarp(p.warp[std::size_t(s)], p.amount[std::size_t(s)], p.var[std::size_t(s)], a, p);
    // band limits and the spectral filter
    const double cut = 20.0 * std::pow(1000.0, clampd((p.filterShift + 100.0) / 200.0, 0.0, 1.0));
    const double wet = clampd(p.filterWet / 100.0, 0.0, 1.0);
    for (int j = 0; j < n; ++j) {
        const double hz = j * sr / N;
        double g = 1.0;
        if (hz < p.freqLo || hz > p.freqHi) {
            if (p.smoothBand) {
                const double edge = hz < p.freqLo ? p.freqLo : p.freqHi;
                const double oct = std::abs(std::log2(std::max(hz, 1.0) / std::max(edge, 1.0)));
                g = std::exp(-4.0 * oct * oct);
            } else g = 0.0;
        }
        if (wet > 0.0) { const double f = hz / cut; g *= lerp(1.0, 1.0 / (1.0 + f * f * f * f), wet); }
        sh_->mag[std::size_t(j)] *= float(g);
    }
    // phase advance with peak-locked neighbours
    const double adv = tau * double(H) / sr;
    for (int j = 0; j < n; ++j) {
        phase_[std::size_t(j)] += float(adv * sh_->partialHz[std::size_t(j)]);
        if (phase_[std::size_t(j)] > 1.0e4f) phase_[std::size_t(j)] = std::fmod(phase_[std::size_t(j)], float(tau));
    }
    const int reach = p.phaseLock ? 3 : 1;
    for (int j = 2; j < n - 2; ++j) {
        if (sh_->mag[std::size_t(j)] > sh_->mag[std::size_t(j - 1)] && sh_->mag[std::size_t(j)] >= sh_->mag[std::size_t(j + 1)]) {
            for (int d = 1; d <= reach; ++d) {
                const float alt = (d & 1) ? float(pi) : 0.0f;
                if (j - d >= 0) phase_[std::size_t(j - d)] = phase_[std::size_t(j)] + alt;
                if (j + d < n) phase_[std::size_t(j + d)] = phase_[std::size_t(j)] + alt;
            }
        }
    }
    for (int j = 0; j < n; ++j) {
        const float ph = phase_[std::size_t(j)];
        sh_->re[std::size_t(j)] = sh_->mag[std::size_t(j)] * std::cos(ph); sh_->im[std::size_t(j)] = sh_->mag[std::size_t(j)] * std::sin(ph);
    }
    sh_->im[0] = 0.0f; sh_->im[K] = 0.0f;
    for (int j = 1; j < K; ++j) { sh_->re[std::size_t(N - j)] = sh_->re[std::size_t(j)]; sh_->im[std::size_t(N - j)] = -sh_->im[std::size_t(j)]; }
    sh_->fft.inverse(sh_->re.data(), sh_->im.data());
    for (int i = 0; i < N; ++i) ola_[std::size_t(i)] += sh_->re[std::size_t(i)] * sh_->window[std::size_t(i)];
    std::copy(ola_.begin(), ola_.begin() + H, out_.begin());
    std::copy(ola_.begin() + H, ola_.end(), ola_.begin());
    std::fill(ola_.end() - H, ola_.end(), 0.0f);
    idx_ = 0;
}

double SpectralVoice::process(const SpectralAnalysis& a, const SpectralParams& p, double sr, double modulator) noexcept {
    if (!allocated() || a.frames == 0) return 0.0;
    modRing_[std::size_t(modW_)] = float(modulator); modW_ = (modW_ + 1) % N;
    if (idx_ >= H) synthFrame(a, p, sr);
    return double(out_[std::size_t(idx_++)]);
}
}
