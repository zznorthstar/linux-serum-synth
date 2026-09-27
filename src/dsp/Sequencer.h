#pragma once
// MIDI-domain generators: the arpeggiator and the clip player. They receive key
// events and emit note events with sample-accurate timing through a callback;
// the synth engine owns the voices. Fixed-size, allocation-free.
#include "DspCommon.h"
#include "Patch.h"

namespace zyg::dsp {

struct NoteEmit { int note; float velocity; bool on; };

class ArpEngine {
public:
    static constexpr int maxHeld = 32;
    void reset() noexcept;
    void noteOn(int note, float velocity) noexcept;
    void noteOff(int note) noexcept;
    bool holding(int note) const noexcept;
    int heldCount() const noexcept { return heldN_; }
    bool anySounding() const noexcept { return sounding_ >= 0; }
    // Advances one sample. `emit(NoteEmit)` is called for note events.
    template <class Emit>
    void tick(const ArpClipDef& clip, double sampleRate, double bpm, double swing, Emit&& emit) noexcept {
        if (heldN_ == 0) {
            if (sounding_ >= 0) { emit(NoteEmit{sounding_, 0.0f, false}); sounding_ = -1; }
            active_ = false; return;
        }
        double stepLen = clip.rate * (clip.dotted ? 1.5 : 1.0) * (clip.triplet ? 2.0 / 3.0 : 1.0);
        stepLen = std::max(stepLen, 1.0 / 64.0);
        const double sw = clampd(swing, 50.0, 75.0) / 100.0;
        stepLen *= 2.0 * ((step_ & 1) ? 1.0 - sw : sw);
        const double beatInc = beatsPerSecond(bpm) / sampleRate;
        if (!active_) { active_ = true; stepPos_ = stepLen; step_ = 0; repeatCount_ = 0; }
        stepPos_ += beatInc;
        if (sounding_ >= 0 && stepPos_ >= clip.gate / 100.0 * stepLen && clip.gate < 100.0) {
            emit(NoteEmit{sounding_, 0.0f, false}); sounding_ = -1;
        }
        if (stepPos_ >= stepLen) {
            stepPos_ -= stepLen;
            if (sounding_ >= 0) { emit(NoteEmit{sounding_, 0.0f, false}); sounding_ = -1; }
            trigger(clip, emit);
        }
    }
private:
    template <class Emit>
    void trigger(const ArpClipDef& clip, Emit&& emit) noexcept {
        std::array<int, maxHeld * 4> seq {};
        std::array<float, maxHeld * 4> vel {};
        const int n = buildSequence(clip, seq, vel);
        if (n == 0) return;
        if (clip.chance < 100.0 && rng_.unipolar() * 100.0 >= clip.chance) { advance(clip, n); return; }
        const std::string& shape = clip.shape;
        if (shape == "Chord") {
            // play every held note at once; the first is tracked for gating
            for (int i = 0; i < heldN_; ++i) {
                const int note = std::clamp(held_[std::size_t(i)] + int(std::lround(clip.transpose)), 0, 127);
                emit(NoteEmit{note, heldVel_[std::size_t(i)], true});
                if (i == 0) sounding_ = note; else chordExtra_[std::size_t(std::min(i - 1, 30))] = note;
            }
            chordCount_ = std::min(heldN_ - 1, 31);
            advance(clip, n); return;
        }
        int idx = pickIndex(clip, n);
        idx = std::clamp(idx, 0, n - 1);
        const int note = std::clamp(seq[std::size_t(idx)] + int(std::lround(clip.transpose)), 0, 127);
        emit(NoteEmit{note, vel[std::size_t(idx)], true});
        sounding_ = note;
        advance(clip, n);
    }
    void advance(const ArpClipDef& clip, int n) noexcept {
        if (++repeatCount_ >= std::max(1, clip.repeats)) { repeatCount_ = 0; ++step_; }
        (void) n;
    }
    int buildSequence(const ArpClipDef& clip, std::array<int, maxHeld * 4>& seq, std::array<float, maxHeld * 4>& vel) const noexcept;
    int pickIndex(const ArpClipDef& clip, int n) noexcept;

    std::array<int, maxHeld> held_ {}; std::array<float, maxHeld> heldVel_ {}; int heldN_ = 0;
    std::array<int, 32> chordExtra_ {}; int chordCount_ = 0;
    int sounding_ = -1;
    bool active_ = false;
    double stepPos_ = 0.0;
    long long step_ = 0;
    int repeatCount_ = 0, lastRandom_ = -1, drift_ = 0;
    Rng rng_ {0x51ed2701u};
public:
    // Called by the engine when it needs chord extras released together with the primary note.
    template <class Emit> void releaseExtras(Emit&& emit) noexcept {
        for (int i = 0; i < chordCount_; ++i) emit(NoteEmit{chordExtra_[std::size_t(i)], 0.0f, false});
        chordCount_ = 0;
    }
};

class ClipPlayer {
public:
    static constexpr int maxHeads = 16;
    void reset() noexcept;
    // Returns true when the key was consumed (it selected/triggered a clip).
    template <class Emit>
    bool noteOn(const ClipSettings& s, int key, float velocity, Emit&&) noexcept {
        if (!s.enabled) return false;
        const int base = std::max(0, 12 * (s.selectOctave + 3));
        if (key < base || key >= base + 12) return false;
        const int clipIndex = key - base;
        const auto& clip = s.clips[std::size_t(clipIndex)];
        if (clip.notes.empty()) return true;
        if (clip.spanMode == "Mono") for (auto& h : heads_) h.on = false;
        for (auto& h : heads_) if (!h.on) {
            h = Head{}; h.on = true; h.clip = clipIndex; h.key = key; h.velocity = velocity; h.dir = 1;
            return true;
        }
        return true;
    }
    template <class Emit> void noteOff(const ClipSettings&, int key, Emit&& emit) noexcept {
        for (auto& h : heads_) if (h.on && h.key == key) { releaseHead(h, emit); h.on = false; }
    }
    template <class Emit>
    void tick(const ClipSettings& s, double sampleRate, double bpm, int rootKey, Emit&& emit) noexcept;
    bool anyActive() const noexcept { for (const auto& h : heads_) if (h.on) return true; return false; }
private:
    struct Head {
        bool on = false; int clip = 0, key = 0; float velocity = 1.0f;
        double time = 0.0; int dir = 1;
        std::array<int, 16> sounding {}; std::array<double, 16> endTime {}; int soundingN = 0;
        std::array<bool, 128> played {};
    };
    template <class Emit> void releaseHead(Head& h, Emit&& emit) noexcept {
        for (int i = 0; i < h.soundingN; ++i) emit(NoteEmit{h.sounding[std::size_t(i)], 0.0f, false});
        h.soundingN = 0;
    }
    std::array<Head, maxHeads> heads_ {};
    Rng rng_ {0x7f4a7c15u};
};

template <class Emit>
void ClipPlayer::tick(const ClipSettings& s, double sampleRate, double bpm, int, Emit&& emit) noexcept {
    for (auto& h : heads_) {
        if (!h.on) continue;
        const auto& clip = s.clips[std::size_t(h.clip)];
        const double inc = beatsPerSecond(bpm) / sampleRate * std::max(clip.rate, 0.05) * h.dir;
        const double len = std::max(clip.lengthBeats, 0.25);
        const double prev = h.time;
        h.time += inc;
        // note offs
        for (int i = 0; i < h.soundingN;) {
            if (h.time >= h.endTime[std::size_t(i)]) {
                emit(NoteEmit{h.sounding[std::size_t(i)], 0.0f, false});
                h.sounding[std::size_t(i)] = h.sounding[std::size_t(h.soundingN - 1)];
                h.endTime[std::size_t(i)] = h.endTime[std::size_t(h.soundingN - 1)];
                --h.soundingN;
            } else ++i;
        }
        // note ons crossing this sample
        const double lo = std::min(prev, h.time), hi = std::max(prev, h.time);
        bool first = prev == 0.0 && h.time > 0.0;
        for (const auto& n : clip.notes) {
            const bool crossed = first ? (n.time >= 0.0 && n.time <= hi) : (n.time > lo && n.time <= hi);
            if (!crossed) continue;
            const int transpose = clip.spanMode == "Offset" ? h.key - (12 * (s.selectOctave + 3) + h.clip) : 0;
            const int note = std::clamp(n.note + transpose + clip.transpose, 0, 127);
            if (h.soundingN < 16) {
                emit(NoteEmit{note, float(n.velocity * h.velocity), true});
                h.sounding[std::size_t(h.soundingN)] = note; h.endTime[std::size_t(h.soundingN)] = n.time + n.length; ++h.soundingN;
            }
        }
        if (h.time >= len || h.time < 0.0) {
            if (clip.playbackMode == "OneShot") { releaseHead(h, emit); h.on = false; }
            else if (clip.playbackMode == "Pendulum") { h.dir = -h.dir; h.time = h.dir > 0 ? 0.0 : len; }
            else { releaseHead(h, emit); h.time = 0.0; }
        }
    }
}
}
