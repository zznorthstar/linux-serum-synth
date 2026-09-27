#include "Sequencer.h"
#include <numeric>

namespace zyg::dsp {
void ArpEngine::reset() noexcept {
    heldN_ = 0; sounding_ = -1; active_ = false; stepPos_ = 0.0; step_ = 0; repeatCount_ = 0; lastRandom_ = -1; drift_ = 0; chordCount_ = 0;
}
bool ArpEngine::holding(int note) const noexcept {
    for (int i = 0; i < heldN_; ++i) if (held_[std::size_t(i)] == note) return true;
    return false;
}
void ArpEngine::noteOn(int note, float velocity) noexcept {
    if (holding(note)) return;
    if (heldN_ == 0) { active_ = false; step_ = 0; repeatCount_ = 0; drift_ = 0; }
    if (heldN_ < maxHeld) { held_[std::size_t(heldN_)] = note; heldVel_[std::size_t(heldN_)] = velocity; ++heldN_; }
}
void ArpEngine::noteOff(int note) noexcept {
    for (int i = 0; i < heldN_; ++i) if (held_[std::size_t(i)] == note) {
        for (int j = i; j + 1 < heldN_; ++j) { held_[std::size_t(j)] = held_[std::size_t(j + 1)]; heldVel_[std::size_t(j)] = heldVel_[std::size_t(j + 1)]; }
        --heldN_; return;
    }
}

int ArpEngine::buildSequence(const ArpClipDef& clip, std::array<int, maxHeld * 4>& seq, std::array<float, maxHeld * 4>& vel) const noexcept {
    std::array<int, maxHeld> order {};
    std::iota(order.begin(), order.begin() + heldN_, 0);
    if (clip.shape != "Played")
        std::sort(order.begin(), order.begin() + heldN_, [&](int a, int b) { return held_[std::size_t(a)] < held_[std::size_t(b)]; });
    const int octaves = std::clamp(clip.transposeRange, 1, 3);
    int n = 0;
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < heldN_ && n < int(seq.size()); ++i) {
            const int idx = order[std::size_t(i)];
            seq[std::size_t(n)] = held_[std::size_t(idx)] + 12 * o;
            vel[std::size_t(n)] = heldVel_[std::size_t(idx)];
            ++n;
        }
    return n;
}

int ArpEngine::pickIndex(const ArpClipDef& clip, int n) noexcept {
    const std::string& s = clip.shape;
    const long long st = step_;
    auto converge = [&](long long k) { return int((k % 2 == 0) ? (k / 2) : (n - 1 - k / 2)); };
    if (n == 1) return 0;
    if (s == "Down") return int(n - 1 - st % n);
    if (s == "UpDown" || s == "DownUp") {
        const long long period = std::max(1, 2 * n - 2); const long long i = st % period;
        const int up = int(i < n ? i : period - i);
        return s == "UpDown" ? up : n - 1 - up;
    }
    if (s == "UpAndDown" || s == "DownAndUp") {
        const long long period = 2 * n; const long long i = st % period;
        const int up = int(i < n ? i : period - 1 - i);
        return s == "UpAndDown" ? up : n - 1 - up;
    }
    if (s == "Converge") return converge(st % n);
    if (s == "ConvAndDiv") { const long long k = st % (2 * n); return converge(k < n ? k : 2 * n - 1 - k); }
    if (s == "Rand") return int(rng_.next() % std::uint32_t(n));
    if (s == "RandNoDup") {
        int i = int(rng_.next() % std::uint32_t(n));
        if (i == lastRandom_) i = (i + 1 + int(rng_.next() % std::uint32_t(n - 1))) % n;
        lastRandom_ = i; return i;
    }
    if (s == "RandOnce") {
        const long long cycle = st / n; static constexpr int strides[] = {7, 5, 3, 11, 13};
        int stride = strides[cycle % 5]; while (std::gcd(stride, n) != 1) ++stride;
        return int((st % n * stride + cycle * 3) % n);
    }
    if (s == "RandDrift") {
        drift_ += int(rng_.next() % 3u) - 1; drift_ = std::clamp(drift_, 0, n - 1); return drift_;
    }
    if (s == "ThumbUD") {
        if (st % 2 == 0) return 0;
        const long long k = (st / 2) % std::max(1, 2 * (n - 1) - 2 + 2); const int m = n - 1;
        const long long period = std::max<long long>(1, 2 * m - 2); const long long i = k % period;
        return 1 + int(i < m ? i : period - i);
    }
    if (s == "Pattern" && !clip.steps.empty()) {
        const auto& step = clip.steps[std::size_t(st % (long long)clip.steps.size())];
        return step.note % n;
    }
    return int(st % n); // Up, Played, default
}
}

namespace zyg::dsp {
void ClipPlayer::reset() noexcept { for (auto& h : heads_) h = Head{}; }
}
