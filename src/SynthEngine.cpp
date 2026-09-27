#include "SynthEngine.h"
#include "dsp/Filters.h"
#include "dsp/FxEngine.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"
#include "dsp/Sequencer.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace zyg {
using namespace dsp;

namespace {
constexpr int kBlock = 8;
constexpr int kChunk = 256;
constexpr int kMaxVoices = 32;
constexpr int kOscStride = 64;
enum : int {
    baseOsc = 0, baseFilter = 320, baseEnv = 352, baseLfo = 384, baseMacro = 464, baseGlobal = 472,
    baseRouting = 488, baseArp = 516, baseClip = 524, baseBus = 532, totalDest = 548
};
constexpr std::size_t maxRoutes = 64;

struct Dest {
    int flat = -1;
    double lo = 0.0, hi = 1.0;
    int fxModule = -1, fxSlot = -1;
    bool global = false; // evaluated once per chunk instead of per voice
};

Dest resolveDest(const Patch& patch, const ModulationRoute& r) noexcept {
    ModTarget k = r.targetKind;
    int inst = r.targetIndex, prm = r.targetParam;
    switch (k) {
        case ModTarget::wavetablePosition: k = ModTarget::oscParam; prm = int(OscParam::tablePos); break;
        case ModTarget::warpOneAmount: k = ModTarget::oscParam; prm = int(OscParam::warp1); break;
        case ModTarget::warpTwoAmount: k = ModTarget::oscParam; prm = int(OscParam::warp2); break;
        case ModTarget::filterCutoff: k = ModTarget::filterParam; prm = int(FilterParam::freq); break;
        default: break;
    }
    Dest d;
    switch (k) {
        case ModTarget::oscParam: {
            if (inst < 0 || inst >= 5 || prm < 0 || prm >= int(OscParam::count)) return d;
            const auto rg = oscParamRange(OscParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseOsc + inst * kOscStride + prm; break;
        }
        case ModTarget::filterParam: {
            if (inst < 0 || inst >= 2 || prm < 0 || prm >= int(FilterParam::count)) return d;
            const auto rg = filterParamRange(FilterParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseFilter + inst * 16 + prm; break;
        }
        case ModTarget::envParam: {
            if (inst < 0 || inst >= 4 || prm < 0 || prm >= int(EnvParam::count)) return d;
            const auto rg = envParamRange(EnvParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseEnv + inst * 8 + prm; break;
        }
        case ModTarget::lfoParam: {
            if (inst < 0 || inst >= 10 || prm < 0 || prm >= int(LfoParam::count)) return d;
            const auto rg = lfoParamRange(LfoParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseLfo + inst * 8 + prm; break;
        }
        case ModTarget::macroValue:
            if (inst < 0 || inst >= 8) return d;
            d.lo = 0.0; d.hi = 1.0; d.flat = baseMacro + inst; d.global = true; break;
        case ModTarget::globalParam: {
            if (prm < 0 || prm >= int(GlobalParam::count)) return d;
            const auto rg = globalParamRange(GlobalParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseGlobal + prm; break;
        }
        case ModTarget::routingParam: {
            if (inst < 0 || inst >= 7 || prm < 0 || prm >= int(RoutingParam::count)) return d;
            const auto rg = routingParamRange(RoutingParam(prm)); d.lo = rg.lo; d.hi = rg.hi; d.flat = baseRouting + inst * 4 + prm; break;
        }
        case ModTarget::lfoPointBus:
            if (inst < 0 || inst >= 16) return d;
            d.lo = 0.0; d.hi = 1.0; d.flat = baseBus + inst; d.global = true; break;
        case ModTarget::arpParam:
            if (prm < 0 || prm >= 8) return d;
            d.lo = 0.0; d.hi = prm == 1 ? 133.0 : (prm == 3 ? 3.0 : (prm == 2 ? 1.0 : 100.0)); d.flat = baseArp + prm; d.global = true; break;
        case ModTarget::clipParam:
            if (prm < 0 || prm >= 8) return d;
            d.lo = 0.0; d.hi = 1.0; d.flat = baseClip + prm; d.global = true; break;
        case ModTarget::fxParam: {
            if (inst < 0 || inst >= int(patch.fx.size()) || inst >= FxEngine::maxModules || prm < 0 || prm >= fxParamSlots) return d;
            const auto& m = patch.fx[std::size_t(inst)];
            const auto table = fxParamTable(m.fxType);
            if (prm == fxLevelSlot) { d.lo = 0.0; d.hi = 1.0; }
            else if (std::size_t(prm) < table.size()) { d.lo = table[std::size_t(prm)].lo; d.hi = table[std::size_t(prm)].hi; }
            else return d;
            d.fxModule = inst; d.fxSlot = prm; d.flat = -2; d.global = true; break;
        }
        default: break;
    }
    return d;
}

// Output protection: transparent below 0.8, then a smooth ceiling at 1.0. Native FX are
// not level-matched to Serum's, so this keeps an imported preset from damaging monitors.
inline double safetyLimit(double x) noexcept {
    const double a = std::abs(x);
    if (a <= 0.8) return x;
    const double y = 0.8 + 0.2 * std::tanh((a - 0.8) / 0.2);
    return x < 0.0 ? -y : y;
}

inline double softenCurve(double x, double curve) noexcept {
    if (std::abs(curve) < 1.0e-9) return x;
    const double e = std::exp2(-clampd(curve, -100.0, 100.0) / 100.0 * 2.0);
    return x < 0.0 ? -std::pow(-x, e) : std::pow(x, e);
}
}

struct SynthEngine::Impl {
    struct Eff {
        std::array<OscValues, 5> osc {};
        std::array<std::array<double, 10>, 2> filt {};
        std::array<EnvParams, 4> env {};
        std::array<double, 7> routeBalance {}, routeBus1 {}, routeBus2 {};
        double masterTuning = 440.0, transpose = 0.0, voiceAmp = 1.0, directVol = 1.0, portaTime = 0.0, envScale = 1.0, lfoScale = 1.0;
    };
    struct Voice {
        bool active = false, released = false, sustained = false, latched = false;
        int channel = 0, note = 60, index = 0;
        std::uint64_t age = 0;
        double velocity = 0.0, releaseVelocity = 0.0;
        double pitchNote = 60.0, glideFrom = 60.0, glideTo = 60.0, glideElapsed = 0.0, glideTime = 0.0;
        bool gliding = false;
        std::array<EnvelopeGen, 4> env;
        std::array<double, 4> envVal {};
        std::array<double, 7> slotAmpPrev {};
        std::array<LfoState, 10> lfo;
        std::array<double, 10> lfoVal {};
        std::array<OscVoiceState, 5> osc;
        std::array<FilterCore, 2> filter;
        std::array<double, 2> smoothedCutoff {1.0, 1.0};
        std::array<double, 7> raw {};
        std::array<bool, 5> zoneOk {true, true, true, true, true};
        // Declick when a still-sounding voice is stolen: its last output decays over a few ms.
        std::array<double, 8> lastOut {}, tail {};
        int tailLeft = 0;
        std::array<double, 10> rnd {};
        std::array<double, 2> alt {}, voiceMod {};
        double discrete = 0.0;
        double voiceDetuneCents = 0.0, voicePan = 0.0, voiceEnvScale = 1.0, voiceCutoff = 0.0;
        std::array<double, maxRoutes> routeSmooth {};
        std::array<double, totalDest> off {};
        std::array<std::uint16_t, maxRoutes * 2> touched {};
        int touchedN = 0;
        double elapsed = 0.0;
        Eff eff;
        Rng rng;
        bool everRendered = false;
    };

    double sr = 44100.0;
    const Patch* patch = nullptr;
    std::vector<std::unique_ptr<Voice>> voices;
    std::unique_ptr<SpectralShared> spectralShared;
    FxEngine fx;
    ArpEngine arp;
    ClipPlayer clipPlayer;
    std::uint64_t clock = 0, noteSerial = 0;
    double bpm = 120.0, beat = 0.0;
    unsigned pumpSerial = 0;                       // counts note-ons for FX triggers
    const float* scIn[2] = {nullptr, nullptr};   // host sidechain input for the current block
    int scFrames = 0;
    double scFollow = 0.0;                        // sidechain envelope follower (0..1)
    std::array<double, 256> scL {}, scR {};
    bool playing = false;
    float modWheel = 0.0f;
    std::array<float, 16> bend {}, pressure {}, cc74 {};
    std::array<float, 128> midiCC {};   // latest value of every controller (any channel), 0..1
    std::array<float, 128> polyPressure {};
    bool sustain = false;
    std::array<int, 64> held {}; std::array<float, 64> heldVel {};
    int heldCount = 0;
    double lastNote = -1.0;
    int altCounter = 0;
    std::array<std::array<double, 16>, 5> phaseMemory {};   // last started voice's oscillator phases (contiguous phase mode)
    // caches derived from the bound patch
    std::array<OscValues, 5> oscBase {};
    std::array<bool, 10> lfoSupported {};
    std::array<LfoDefinition, 10> lfoDefs {};
    std::array<bool, 10> lfoFree {};
    LfoDefinition legacySine;
    // global (chunk-rate) modulation state
    std::array<double, 8> macroOff {}, macroEff {};
    std::array<LfoState, 10> gLfo {};
    std::array<std::array<double, 10>, kChunk / kBlock + 1> gLfoBlock {};
    std::array<double, 10> gLfoNow {};
    std::array<double, 16> busOff {};
    std::array<double, totalDest> gOff {};
    std::array<double, maxRoutes> gRouteSmooth {};
    Voice dummy;
    // chunk buses
    std::array<double, kChunk> mainL {}, mainR {}, dirL {}, dirR {}, b1L {}, b1R {}, b2L {}, b2R {};
    double arpOffChance = 0.0, arpOffGate = 0.0, arpOffRate = 0.0, arpOffRange = 0.0;
    bool arpWasEnabled = false;
    bool limiterOn = true;
    std::uint64_t boundPreset = ~std::uint64_t(0);

    Impl() {
        voices.reserve(kMaxVoices);
        for (int i = 0; i < kMaxVoices; ++i) voices.push_back(std::make_unique<Voice>());
        legacySine.shape = LfoShape::sine; legacySine.mode = LfoMode::trigger;
        spectralShared = std::make_unique<SpectralShared>();
    }

    // ------------------------------------------------------------------ setup
    void prepare(double sampleRate) {
        sr = std::max(8000.0, sampleRate);
        if (spectralShared->window.empty()) spectralShared->allocate();
        for (auto& vp : voices) {
            auto& v = *vp;
            for (int i = 0; i < 2; ++i) v.filter[std::size_t(i)].prepare(sr);
            for (int i = 0; i < 5; ++i) v.osc[std::size_t(i)].allocate(i < 3 ? spectralShared.get() : nullptr);
        }
        for (int i = 0; i < 2; ++i) dummy.filter[std::size_t(i)].prepare(sr);
        fx.prepare(sr);
        allNotesOff();
        if (patch) fx.bind(patch);
    }

    void setPatch(const Patch* p) noexcept {
        patch = p;
        if (!p) return;
        for (std::size_t i = 0; i < 5; ++i)
            for (std::size_t k = 0; k < oscParamCount; ++k) oscBase[i][k] = oscBaseValue(p->oscillators[i], OscParam(k));
        for (std::size_t i = 0; i < 10; ++i) {
            lfoDefs[i] = p->lfoDefinitions[i];
            lfoSupported[i] = lfoDefs[i].shape != LfoShape::unknown;
            lfoFree[i] = lfoDefs[i].mode == LfoMode::free || lfoDefs[i].mono;
        }
        if (p->lfoOneSine) { lfoSupported[0] = true; lfoFree[0] = false; }
        legacySine.rateHz = p->lfoOneRateHz;
        fx.bind(p);
        const bool enabled = p->arpSettings.enabled;
        if (arpWasEnabled && !enabled) releaseArp();
        arpWasEnabled = enabled;
    }

    const LfoDefinition& lfoDef(int i) const noexcept {
        return (i == 0 && patch && patch->lfoOneSine) ? legacySine : lfoDefs[std::size_t(i)];
    }

    // ------------------------------------------------------------ voice mgmt
    int activeCount() const noexcept {
        int c = 0; for (const auto& v : voices) c += v->active; return c;
    }

    static double pitchQuantize(double note, int mask, int key) noexcept {
        if (!mask) return note;
        const int base = int(std::lround(note));
        int best = base; double bestD = 1.0e9;
        for (int cand = base - 6; cand <= base + 6; ++cand) {
            const int pc = ((cand - key) % 12 + 12) % 12;
            if (!(mask & (1 << pc))) continue;
            const double d = std::abs(double(cand) - note);
            if (d < bestD) { bestD = d; best = cand; }
        }
        return best;
    }

    Voice* allocateVoice() noexcept {
        const int limit = std::clamp(patch->polyphony, 1, kMaxVoices);
        for (int i = 0; i < limit; ++i) if (!voices[std::size_t(i)]->active) return voices[std::size_t(i)].get();
        Voice* best = nullptr;
        for (int i = 0; i < limit; ++i) {
            Voice* v = voices[std::size_t(i)].get();
            if (!best) { best = v; continue; }
            const bool vr = v->released, br = best->released;
            if (vr != br) { if (vr) best = v; continue; }
            if (v->age < best->age) best = v;
        }
        return best;
    }

    double masterTuningBase() const noexcept { return patch->globals.masterTuning; }

    void startVoice(Voice& v, int channel, int note, float velocity, bool glideFromLast, double glideFromNote) noexcept {
        const auto& g = patch->globals;
        if (v.active) { v.tail = v.lastOut; v.tailLeft = 96; } else { v.tail.fill(0.0); v.tailLeft = 0; }
        v.active = true; v.released = false; v.sustained = false; v.latched = false;
        v.channel = channel; v.note = note; v.age = ++clock; v.velocity = clampd(velocity, 0.0, 1.0); v.releaseVelocity = 0.0;
        v.index = int(noteSerial % std::uint64_t(std::max(1, patch->voicePanel.voiceCount)));
        ++noteSerial;
        v.rng = Rng(0x9e3779b9u ^ std::uint32_t(clock * 2654435761u) ^ std::uint32_t(note * 334214467u));
        for (auto& x : v.rnd) x = v.rng.unipolar();
        v.discrete = std::floor(v.rng.unipolar() * 8.0) / 7.0;
        ++altCounter;
        v.alt = {double(altCounter & 1), double((altCounter >> 1) & 1) * 0.5 + double(altCounter & 1) * 0.5};
        v.elapsed = 0.0; v.everRendered = false;
        const auto& vp = patch->voicePanel;
        v.voiceDetuneCents = v.voicePan = v.voiceCutoff = 0.0; v.voiceEnvScale = 1.0; v.voiceMod = {0.0, 0.0};
        if (vp.active || true) {
            const auto ix = std::size_t(v.index);
            const double rd = v.rng.bipolar(), rp = v.rng.bipolar(), re = v.rng.bipolar(), rc = v.rng.bipolar();
            v.voiceDetuneCents = (vp.detune[ix] + rd * vp.randomDetune) * 0.5;
            v.voicePan = (vp.pan[ix] + rp * vp.randomPan) / 100.0;
            v.voiceEnvScale = std::exp2((vp.envTime[ix] + re * vp.randomEnvTime) / 100.0);
            v.voiceCutoff = (vp.cutoff[ix] + rc * vp.randomCutoff) / 100.0 * 0.25;
            v.voiceMod = {vp.mod1[ix] / 100.0, vp.mod2[ix] / 100.0};
        }
        // portamento
        v.pitchNote = double(note); v.gliding = false; v.glideElapsed = 0.0;
        const double portaTime = g.portamentoTime;
        if (portaTime > 1.0e-4 && glideFromNote >= 0.0 && (glideFromLast || g.portaAlways) && glideFromNote != double(note)) {
            v.glideFrom = glideFromNote; v.glideTo = double(note); v.pitchNote = glideFromNote;
            v.glideTime = portaTime * (g.portaScaled ? std::max(0.1, std::abs(v.glideTo - v.glideFrom) / 12.0) : 1.0);
            v.gliding = true;
        }
        const bool stolen = v.tailLeft > 0;   // the voice was still sounding (steal or mono retrigger)
        for (int i = 0; i < 4; ++i) {
            const auto& pe = patch->envelopes[std::size_t(i)];
            auto& e = v.env[std::size_t(i)];
            const double from = stolen && !pe.restartOnSteal && e.active() ? std::max(pe.start, e.value()) : pe.start;
            e.reset(); e.noteOn(from); v.envVal[std::size_t(i)] = from;
        }
        for (std::size_t i = 0; i < 10; ++i) { v.lfo[i].reset(std::uint32_t(v.rng.next()), 0.0); v.lfoVal[i] = 0.0; }
        v.smoothedCutoff = {patch->filters[0].cutoff, patch->filters[1].cutoff};
        for (auto& f : v.filter) f.reset();
        v.raw.fill(0.0); v.routeSmooth.fill(0.0); v.off.fill(0.0); v.touchedN = 0;
        v.slotAmpPrev.fill(0.0);
        for (int i = 0; i < 5; ++i) {
            const auto& zo = patch->oscillators[std::size_t(i)];
            const int vel127 = int(v.velocity * 127.0 + 0.5);
            v.zoneOk[std::size_t(i)] = note >= zo.keyZoneLo && note <= zo.keyZoneHi && vel127 >= zo.velZoneLo && vel127 <= zo.velZoneHi;
        }
        // first resolve pass so oscillators see their modulated start values
        for (std::size_t i = 0; i < 5; ++i) v.osc[i].reset();
        prepareBlock(v, 0.0, true);
        for (int i = 0; i < 5; ++i) {
            if (!patch->oscillators[std::size_t(i)].enabled || !v.zoneOk[std::size_t(i)]) continue;
            OscInputs in = oscInputs(v, i);
            oscNoteOn(v.osc[std::size_t(i)], in);
            const auto& po = patch->oscillators[std::size_t(i)];
            if ((po.mode == OscMode::wavetable && !po.perVoicePhase) || (po.mode == OscMode::sub && po.contiguousPhase))
                for (int u = 0; u < v.osc[std::size_t(i)].unisonCount; ++u) v.osc[std::size_t(i)].phase[std::size_t(u)] = phaseMemory[std::size_t(i)][std::size_t(u)];
        }
        if (!v.env[0].active()) v.env[0].noteOn(patch->envelopes[0].start);
    }

    OscInputs oscInputs(Voice& v, int i) const noexcept {
        OscInputs in;
        in.osc = &patch->oscillators[std::size_t(i)]; in.v = &v.eff.osc[std::size_t(i)]; in.index = i;
        in.note = v.note; in.velocity = v.velocity; in.sampleRate = sr; in.bpm = bpm;
        in.masterTuning = v.eff.masterTuning; in.mods = &v.raw; in.released = v.released; in.serial = v.age; in.importedPatch = !patch->originalPreset.empty(); in.oversampleLevel = patch->globals.oversampling;
        const auto& g = patch->globals;
        double bendSemis = 0.0;
        const float b = patch->globals.mpeEnabled ? bend[std::size_t(std::clamp(v.channel - 1, 0, 15))] : bend[0];
        if (b >= 0.0f) bendSemis = double(b) * g.bendUp; else bendSemis = double(b) * (-g.bendDown);
        if (g.mpeEnabled) bendSemis = double(b) * g.mpePitchBendRange;
        const double base = pitchQuantize(v.pitchNote, g.pitchQuantizerMask, g.pitchQuantizerKey);
        in.semis = base - 69.0 + v.eff.transpose + bendSemis + v.voiceDetuneCents / 100.0;
        return in;
    }

    void voiceNoteOn(int channel, int note, float velocity) noexcept {
        ++pumpSerial;
        if (!patch || note < 0 || note > 127) return;
        const auto& g = patch->globals;
        // track held keys for mono priority / legato
        if (heldCount < int(held.size())) { held[std::size_t(heldCount)] = note; heldVel[std::size_t(heldCount)] = velocity; ++heldCount; }
        if (g.noteLatch) for (auto& vp : voices) if (vp->active && vp->latched) { vp->latched = false; releaseVoice(*vp, 0.0f); }
        if (patch->mono) {
            Voice* cur = nullptr;
            for (auto& vp : voices) if (vp->active && !vp->released) { cur = vp.get(); break; }
            if (!cur) for (auto& vp : voices) if (vp->active) { cur = vp.get(); break; }
            const bool legatoNow = g.legato && cur && !cur->released;
            if (legatoNow) {
                cur->note = note; cur->channel = channel;
                if (g.portamentoTime > 1.0e-4) {
                    cur->glideFrom = cur->pitchNote; cur->glideTo = double(note); cur->glideElapsed = 0.0;
                    cur->glideTime = g.portamentoTime * (g.portaScaled ? std::max(0.1, std::abs(cur->glideTo - cur->glideFrom) / 12.0) : 1.0);
                    cur->gliding = true;
                } else cur->pitchNote = double(note);
                lastNote = double(note);
                return;
            }
            const bool glide = cur != nullptr && !cur->released;
            const double from = cur ? cur->pitchNote : lastNote;
            Voice* target = cur ? cur : allocateVoice();
            if (cur) for (auto& vp : voices) if (vp.get() != cur) vp->active = false;
            startVoice(*target, channel, note, velocity, glide, from);
            lastNote = double(note);
            return;
        }
        // poly
        if (g.limitSameNote) for (auto& vp : voices) if (vp->active && vp->note == note) { vp->active = false; }
        const bool anyHeld = heldCount > 1;
        Voice* target = allocateVoice();
        if (!target) return;
        startVoice(*target, channel, note, velocity, anyHeld, lastNote);
        lastNote = double(note);
    }

    void voiceNoteOff(int channel, int note, float releaseVelocity) noexcept {
        if (!patch) return;
        for (int i = 0; i < heldCount; ++i) if (held[std::size_t(i)] == note) {
            for (int j = i; j + 1 < heldCount; ++j) { held[std::size_t(j)] = held[std::size_t(j + 1)]; heldVel[std::size_t(j)] = heldVel[std::size_t(j + 1)]; }
            --heldCount; break;
        }
        if (patch->mono) {
            Voice* cur = nullptr;
            for (auto& vp : voices) if (vp->active && !vp->released && vp->note == note) { cur = vp.get(); break; }
            if (!cur) return;
            if (heldCount > 0) {
                int pick = held[std::size_t(heldCount - 1)];
                if (patch->globals.priority == VoicePriority::low) { pick = 127; for (int i = 0; i < heldCount; ++i) pick = std::min(pick, held[std::size_t(i)]); }
                else if (patch->globals.priority == VoicePriority::high) { pick = 0; for (int i = 0; i < heldCount; ++i) pick = std::max(pick, held[std::size_t(i)]); }
                cur->note = pick;
                const auto& g = patch->globals;
                if (g.portamentoTime > 1.0e-4) {
                    cur->glideFrom = cur->pitchNote; cur->glideTo = double(pick); cur->glideElapsed = 0.0;
                    cur->glideTime = g.portamentoTime; cur->gliding = true;
                } else cur->pitchNote = double(pick);
                return;
            }
            releaseVoice(*cur, releaseVelocity);
            return;
        }
        for (auto& vp : voices) {
            auto& v = *vp;
            if (v.active && !v.released && v.channel == channel && v.note == note) {
                if (patch->globals.noteLatch) v.latched = true;
                else if (sustain) v.sustained = true; else releaseVoice(v, releaseVelocity);
            }
        }
    }

    void releaseVoice(Voice& v, float releaseVelocity) noexcept {
        v.released = true; v.sustained = false; v.releaseVelocity = releaseVelocity;
        for (auto& e : v.env) e.noteOff();
        for (int i = 0; i < 5; ++i) {
            if (!patch->oscillators[std::size_t(i)].enabled) continue;
            OscInputs in = oscInputs(v, i);
            oscNoteOff(v.osc[std::size_t(i)], in);
        }
    }

    void releaseSustained() noexcept {
        for (auto& vp : voices) if (vp->active && vp->sustained) releaseVoice(*vp, 0.0f);
    }

    void allNotesOff() noexcept {
        for (auto& vp : voices) { vp->active = false; vp->released = false; vp->sustained = false; }
        heldCount = 0; arp.reset(); clipPlayer.reset();
    }

    // ---------------------------------------------------------------- arp/clip
    void releaseArp() noexcept {
        arp.reset();
        for (auto& vp : voices) if (vp->active && vp->channel == 17) releaseVoice(*vp, 0.0f);
    }

    void routeNoteOn(int channel, int note, float velocity) noexcept {
        if (!patch) return;
        NoteEmitter em{this};
        if (patch->clipSettings.enabled && clipPlayer.noteOn(patch->clipSettings, note, velocity, em)) return;
        const auto& a = patch->arpSettings;
        if (a.enabled && note >= a.keyZoneMin && note <= a.keyZoneMax) { arp.noteOn(note, velocity); return; }
        voiceNoteOn(channel, note, velocity);
    }
    void routeNoteOff(int channel, int note, float rv) noexcept {
        if (!patch) return;
        NoteEmitter em{this};
        if (patch->clipSettings.enabled) {
            const int base = std::max(0, 12 * (patch->clipSettings.selectOctave + 3));
            if (note >= base && note < base + 12) { clipPlayer.noteOff(patch->clipSettings, note, em); return; }
        }
        if (arp.holding(note)) { arp.noteOff(note); return; }
        voiceNoteOff(channel, note, rv);
    }
    struct NoteEmitter {
        Impl* self;
        void operator()(const NoteEmit& e) const noexcept {
            // sequencer output uses the pseudo channel 17 so it can be released as a group
            if (e.on) self->voiceNoteOn(17, e.note, e.velocity); else self->voiceNoteOff(17, e.note, 0.0f);
        }
    };
    void tickSequencers(int samples) noexcept {
        if (!patch) return;
        NoteEmitter em{this};
        const auto& a = patch->arpSettings;
        const bool hostSync = playing;
        (void) hostSync;
        if (a.enabled) {
            ArpClipDef clip = a.active();
            clip.chance = clampd(clip.chance + arpOffChance, 0.0, 100.0);
            clip.gate = clampd(clip.gate + arpOffGate, 1.0, 133.0);
            clip.rate = std::max(1.0 / 64.0, clip.rate + arpOffRate);
            for (int i = 0; i < samples; ++i) arp.tick(clip, sr, bpm, patch->globals.swing, em);
            if (!arp.anySounding()) arp.releaseExtras(em);
        }
        if (patch->clipSettings.enabled && clipPlayer.anyActive())
            for (int i = 0; i < samples; ++i) clipPlayer.tick(patch->clipSettings, sr, bpm, 60, em);
    }

    // ------------------------------------------------------------- modulation
    bool sourceValue(const Voice& v, bool global, ModSource kind, int idx, bool bipolarRoute, double& out) const noexcept {
        double raw = 0.0;
        const bool nat = modSourceIsBipolar(kind);
        switch (kind) {
            case ModSource::envelope: raw = (idx >= 0 && idx < 4) ? v.envVal[std::size_t(idx)] : 0.0; break;
            case ModSource::lfo:
                if (idx < 0 || idx >= 10 || !lfoSupported[std::size_t(idx)]) return false;
                raw = global ? gLfoNow[std::size_t(idx)] : v.lfoVal[std::size_t(idx)]; break;
            case ModSource::macro: raw = (idx >= 0 && idx < 8) ? macroEff[std::size_t(idx)] : 0.0; break;
            case ModSource::velocity: raw = v.velocity; break;
            case ModSource::note: raw = clampd(v.note / 127.0, 0.0, 1.0); break;
            case ModSource::modWheel: raw = modWheel; break;
            case ModSource::channelPressure: raw = pressure[std::size_t(std::clamp(v.channel - 1, 0, 15))]; break;
            case ModSource::polyPressure: raw = polyPressure[std::size_t(std::clamp(v.note, 0, 127))]; break;
            case ModSource::noiseAudio: raw = v.raw[3]; break;
            case ModSource::oscAudio: { static constexpr int map[4] = {0, 1, 2, 4}; raw = v.raw[std::size_t(map[std::clamp(idx, 0, 3)])]; break; }
            case ModSource::filterAudio: raw = v.raw[std::size_t(5 + std::clamp(idx, 0, 1))]; break;
            case ModSource::random: raw = v.rnd[std::size_t(std::clamp(idx, 0, 9))]; break;
            case ModSource::alternate: raw = v.alt[std::size_t(std::clamp(idx, 0, 1))]; break;
            case ModSource::discreteRandom: raw = v.discrete; break;
            case ModSource::pitchBend: raw = bend[patch->globals.mpeEnabled ? std::size_t(std::clamp(v.channel - 1, 0, 15)) : 0]; break;
            case ModSource::mpeX: raw = bend[std::size_t(std::clamp(v.channel - 1, 0, 15))]; break;
            case ModSource::mpeY: raw = cc74[std::size_t(std::clamp(v.channel - 1, 0, 15))] * 2.0 - 1.0; break;
            case ModSource::mpeZ: raw = pressure[std::size_t(std::clamp(v.channel - 1, 0, 15))]; break;
            case ModSource::releaseVelocity: raw = v.releaseVelocity; break;
            case ModSource::fixed: raw = 1.0; break;
            case ModSource::sidechain: raw = scFollow; break;
            case ModSource::midiCC: raw = midiCC[std::size_t(std::clamp(idx, 0, 127))]; break;
            case ModSource::activeVoices: raw = double(activeCount()) / double(kMaxVoices); break;
            case ModSource::voiceMod: raw = v.voiceMod[std::size_t(std::clamp(idx, 0, 1))]; break;
            case ModSource::voiceIndex: raw = double(v.index) / 8.0; break;
            default: return false;
        }
        const bool natBipolar = nat || kind == ModSource::mpeY;
        out = bipolarRoute ? (natBipolar ? raw : 2.0 * raw - 1.0) : (natBipolar ? 0.5 * (raw + 1.0) : raw);
        if (kind == ModSource::mpeY) out = bipolarRoute ? raw : 0.5 * (raw + 1.0);
        return true;
    }

    // Applies one route to an offset array. Returns false if the route is inactive.
    bool applyRoute(Voice& v, bool global, const ModulationRoute& r, std::size_t routeIndex, const Dest& d,
                    double dt, double* off, std::array<double, maxRoutes>& smooth, double* addTo = nullptr) noexcept {
        double x;
        if (!sourceValue(v, global, r.sourceKind, r.sourceIndex, r.bipolar, x)) return false;
        double aux = 1.0;
        if (r.auxKind != ModSource::unknown) {
            if (!sourceValue(v, global, r.auxKind, r.auxIndex, false, aux)) return false;
            if (r.auxInverted) aux = 1.0 - aux;
        }
        if (!r.mainCurve.empty()) {
            const double u = evalCurve(r.mainCurve, r.bipolar ? 0.5 * (x + 1.0) : x);
            x = r.bipolar ? 2.0 * u - 1.0 : u;
        }
        x = softenCurve(x, r.curveIn);
        double value = x * aux;
        if (r.delaySeconds > 0.0 && v.elapsed < r.delaySeconds) value = 0.0;
        if (routeIndex < maxRoutes && (r.smoothRise > 0.0 || r.smoothFall > 0.0)) {
            double& s = smooth[routeIndex];
            const double amt = value > s ? r.smoothRise : r.smoothFall;
            const double tauS = (amt / 100.0) * (amt / 100.0) * 1.0;
            s += (1.0 - std::exp(-dt / std::max(tauS, 1.0e-5))) * (value - s);
            value = s;
        }
        value = softenCurve(value, r.curveOut);
        const double delta = r.amount / 100.0 * value;   // fraction of the parameter's knob travel
        if (addTo) *addTo += delta; else off[d.flat] += delta;
        return true;
    }

    void evalVoiceMatrix(Voice& v, double dt) noexcept {
        for (int i = 0; i < v.touchedN; ++i) v.off[v.touched[std::size_t(i)]] = 0.0;
        v.touchedN = 0;
        const auto& routes = patch->modulation;
        const std::size_t n = std::min(routes.size(), maxRoutes);
        for (std::size_t ri = 0; ri < n; ++ri) {
            const auto& r = routes[ri];
            if (r.bypass) continue;
            const Dest d = resolveDest(*patch, r);
            if (d.flat < 0 || d.global) continue;
            if (applyRoute(v, false, r, ri, d, dt, v.off.data(), v.routeSmooth) && v.touchedN < int(v.touched.size()))
                v.touched[std::size_t(v.touchedN++)] = std::uint16_t(d.flat);
        }
    }

    void evalGlobalMatrix(double dt) noexcept {
        std::fill(gOff.begin(), gOff.end(), 0.0);
        fx.clearOffsets();
        double* fxOff = fx.modulationOffsets();
        Voice* ref = nullptr;
        for (auto& vp : voices) if (vp->active && (!ref || vp->age > ref->age)) ref = vp.get();
        Voice& rv = ref ? *ref : dummy;
        if (!ref) { rv.velocity = 0.0; rv.envVal.fill(0.0); rv.raw.fill(0.0); }
        const auto& routes = patch->modulation;
        const std::size_t n = std::min(routes.size(), maxRoutes);
        for (std::size_t ri = 0; ri < n; ++ri) {
            const auto& r = routes[ri];
            if (r.bypass) continue;
            const Dest d = resolveDest(*patch, r);
            if (!d.global) continue;
            if (d.fxModule >= 0) {
                double delta = 0.0;
                if (applyRoute(rv, true, r, ri, d, dt, nullptr, gRouteSmooth, &delta))
                    fxOff[std::size_t(d.fxModule) * fxParamSlots + std::size_t(d.fxSlot)] += delta;
            } else if (d.flat >= 0) {
                applyRoute(rv, true, r, ri, d, dt, gOff.data(), gRouteSmooth);
            }
        }
        for (int i = 0; i < 8; ++i) {
            macroOff[std::size_t(i)] = gOff[std::size_t(baseMacro + i)];
            macroEff[std::size_t(i)] = clampd(patch->macroValues[std::size_t(i)] + macroOff[std::size_t(i)], 0.0, 1.0);
        }
        for (int b = 0; b < 16; ++b) busOff[std::size_t(b)] = gOff[std::size_t(baseBus + b)];
        arpOffChance = gOff[baseArp + 0]; arpOffGate = gOff[baseArp + 1]; arpOffRate = gOff[baseArp + 2]; arpOffRange = gOff[baseArp + 3];
    }

    // Resolves every parameter of a voice for the coming block.
    void prepareBlock(Voice& v, double dt, bool first) noexcept {
        (void) first;
        const auto& g = patch->globals;
        // global params first (masterTuning, transpose ...)
        auto goff = [&](GlobalParam p) { return v.off[std::size_t(baseGlobal + int(p))]; };
        auto& e = v.eff;
        auto gmod = [&](GlobalParam gp, double base) { return applyModulation(base, goff(gp), globalParamRange(gp)); };
        e.masterTuning = clampd(g.masterTuning, 400.0, 480.0);
        e.transpose = gmod(GlobalParam::transpose, g.transpose) + gmod(GlobalParam::masterTuning, 0.0);
        e.envScale = std::exp2((gmod(GlobalParam::envTimeScale, g.envTimeScale) - 50.0) / 25.0);
        e.lfoScale = std::exp2((gmod(GlobalParam::lfoTimeScale, g.lfoTimeScale) - 50.0) / 25.0);
        e.voiceAmp = gmod(GlobalParam::voiceAmp, g.voiceAmp);
        e.directVol = gmod(GlobalParam::directVol, g.directVol);
        e.portaTime = gmod(GlobalParam::portamentoTime, g.portamentoTime);
        for (int i = 0; i < 5; ++i) {
            OscValues& ev = e.osc[std::size_t(i)];
            ev = oscBase[std::size_t(i)];
            const int b = baseOsc + i * kOscStride;
            for (int t = 0; t < v.touchedN; ++t) {
                const int flat = v.touched[std::size_t(t)];
                if (flat >= b && flat < b + kOscStride && flat - b < int(OscParam::count)) {
                    const auto p = std::size_t(flat - b); const auto rg = oscParamRange(OscParam(p));
                    ev[p] = applyModulation(ev[p], v.off[std::size_t(flat)], rg);
                }
            }
            at(ev, OscParam::pan) = clampd(at(ev, OscParam::pan) + v.voicePan, -1.0, 1.0);
        }
        for (int i = 0; i < 2; ++i) {
            auto& f = e.filt[std::size_t(i)];
            const auto& pf = patch->filters[std::size_t(i)];
            f = {pf.cutoff, pf.resonance, pf.drive, pf.var, pf.wet, pf.level, pf.stereo, pf.x, pf.y, 0.0};
            for (int p = 0; p < int(FilterParam::count); ++p) {
                const double o = v.off[std::size_t(baseFilter + i * 16 + p)];
                if (o != 0.0) f[std::size_t(p)] = applyModulation(f[std::size_t(p)], o, filterParamRange(FilterParam(p)));
            }
        }
        for (int i = 0; i < 4; ++i) {
            const auto& pe = patch->envelopes[std::size_t(i)];
            auto& ep = e.env[std::size_t(i)];
            double vals[8] = {pe.attack, pe.hold, pe.decay, pe.sustain, pe.release, pe.curve[0], pe.curve[1], pe.curve[2]};
            for (int p = 0; p < 8; ++p) {
                const double o = v.off[std::size_t(baseEnv + i * 8 + p)];
                if (o != 0.0) vals[p] = applyModulation(vals[p], o, envParamRange(EnvParam(p)));
            }
            const double scale = v.voiceEnvScale * e.envScale;
            ep.attack = vals[0] * scale; ep.hold = vals[1] * scale; ep.decay = vals[2] * scale; ep.sustain = vals[3];
            ep.release = vals[4] * scale; ep.curve1 = vals[5]; ep.curve2 = vals[6]; ep.curve3 = vals[7];
            ep.start = pe.start; ep.end = pe.end;
        }
        for (int s = 0; s < 7; ++s) {
            const auto& r = patch->routes[std::size_t(s)];
            e.routeBalance[std::size_t(s)] = applyModulation(r.filterBalance, v.off[std::size_t(baseRouting + s * 4 + 0)], routingParamRange(RoutingParam::filterBalance));
            e.routeBus1[std::size_t(s)] = applyModulation(r.fxBus1Level, v.off[std::size_t(baseRouting + s * 4 + 1)], routingParamRange(RoutingParam::fxBus1Level));
            e.routeBus2[std::size_t(s)] = applyModulation(r.fxBus2Level, v.off[std::size_t(baseRouting + s * 4 + 2)], routingParamRange(RoutingParam::fxBus2Level));
        }
        (void) dt;
    }

    void advancePitch(Voice& v, double dt) noexcept {
        if (!v.gliding) return;
        v.glideElapsed += dt;
        const double t = clampd(v.glideElapsed / std::max(v.glideTime, 1.0e-4), 0.0, 1.0);
        const double curve = patch->globals.portamentoCurve;
        const double shaped = std::abs(curve - 50.0) < 1.0e-6 ? t : std::pow(t, std::exp2((50.0 - curve) / 50.0 * 1.5));
        v.pitchNote = v.glideFrom + (v.glideTo - v.glideFrom) * shaped;
        if (t >= 1.0) { v.gliding = false; v.pitchNote = v.glideTo; }
    }

    void advanceLfos(Voice& v, double dt, int blockIndex) noexcept {
        for (int i = 0; i < 10; ++i) {
            if (!lfoSupported[std::size_t(i)]) continue;
            const auto& def = lfoDef(i);
            if (lfoFree[std::size_t(i)]) { v.lfoVal[std::size_t(i)] = gLfoBlock[std::size_t(blockIndex)][std::size_t(i)]; continue; }
            auto lmod = [&](LfoParam lp, double base) { return applyModulation(base, v.off[std::size_t(baseLfo + i * 8 + int(lp))], lfoParamRange(lp)); };
            double rate = lfoRateHz(def, bpm);
            if (!def.tempoSync) rate = lmod(LfoParam::rate, rate);
            rate *= v.eff.lfoScale;
            const double phaseOff = lmod(LfoParam::phase, def.phaseDegrees) / 360.0;
            const double smooth = lmod(LfoParam::smooth, def.smooth);
            const double rise = lmod(LfoParam::rise, def.rise);
            const double delay = lmod(LfoParam::delay, def.delay);
            v.lfoVal[std::size_t(i)] = advanceLfo(v.lfo[std::size_t(i)], def, rate, dt, phaseOff, smooth, rise, delay, busOff.data());
        }
    }

    // ----------------------------------------------------------------- render
    static void filterParams(const Voice& v, const Patch& p, int i, double sr, FilterParams& fp, double cutoffNorm) noexcept {
        const auto& pf = p.filters[std::size_t(i)];
        const auto& f = v.eff.filt[std::size_t(i)];
        fp.type = pf.response; fp.variant = pf.variant;
        double hz = 20.0 * std::pow(1000.0, clampd(cutoffNorm, 0.0, 1.0));
        if (pf.keyTrack) hz *= std::exp2((v.pitchNote - 60.0) / 12.0);
        hz = clampd(hz, 8.0, sr * 0.45);
        const double st = (clampd(f[std::size_t(FilterParam::stereo)], 0.0, 100.0) - 50.0) / 50.0;
        fp.cutoffHzL = hz * std::exp2(-0.5 * st); fp.cutoffHzR = hz * std::exp2(0.5 * st);
        fp.reso = clampd(f[std::size_t(FilterParam::reso)] / 100.0, 0.0, 1.0);
        fp.drive = clampd(f[std::size_t(FilterParam::drive)] / 100.0, 0.0, 1.0);
        fp.var = clampd(f[std::size_t(FilterParam::var)] / 100.0, 0.0, 1.0);
        fp.x = f[std::size_t(FilterParam::x)]; fp.y = f[std::size_t(FilterParam::y)];
    }

    void renderVoiceBlock(Voice& v, int bl, int at0, int blockIndex) noexcept {
        const double dt = double(bl) / sr;
        advancePitch(v, dt);
        // envelopes and LFOs use the previous block's modulation offsets
        std::array<double, 4> envStart = v.envVal;
        for (int i = 0; i < 4; ++i) v.envVal[std::size_t(i)] = v.env[std::size_t(i)].advance(v.eff.env[std::size_t(i)], dt);
        (void) envStart;
        advanceLfos(v, dt, blockIndex);
        evalVoiceMatrix(v, dt);
        prepareBlock(v, dt, false);
        v.elapsed += dt;
        const auto& eff = v.eff;
        const double cutoffSmooth = 1.0 - std::exp(-dt / 0.005);
        // oscillator block setup
        std::array<OscInputs, 5> inputs;
        std::array<double, 7> modsSnap = v.raw; // audio-rate sources: previous sample, identical for every oscillator
        for (int i = 0; i < 5; ++i) {
            if (!patch->oscillators[std::size_t(i)].enabled || !v.zoneOk[std::size_t(i)]) continue;
            inputs[std::size_t(i)] = oscInputs(v, i);
            inputs[std::size_t(i)].mods = &modsSnap;
            oscBlockUpdate(v.osc[std::size_t(i)], inputs[std::size_t(i)]);
        }
        std::array<FilterParams, 2> fparams;
        std::array<double, 2> wetMix {}, levelGain {}, padGain {};
        for (int i = 0; i < 2; ++i) {
            const auto& pf = patch->filters[std::size_t(i)];
            const auto& f = eff.filt[std::size_t(i)];
            const double target = clampd(f[std::size_t(FilterParam::freq)] + v.voiceCutoff, 0.0, 1.0);
            v.smoothedCutoff[std::size_t(i)] += cutoffSmooth * (target - v.smoothedCutoff[std::size_t(i)]);
            if (pf.enabled) {
                filterParams(v, *patch, i, sr, fparams[std::size_t(i)], v.smoothedCutoff[std::size_t(i)]);
                v.filter[std::size_t(i)].update(fparams[std::size_t(i)]);
            }
            wetMix[std::size_t(i)] = clampd(f[std::size_t(FilterParam::wet)] / 100.0, 0.0, 1.0);
            levelGain[std::size_t(i)] = clampd(f[std::size_t(FilterParam::level)] * 2.0, 0.0, 2.0);
            padGain[std::size_t(i)] = pf.pad ? 0.5 : 1.0;
        }
        const double velGain = 1.0 + patch->globals.velocityAmpDepth * (v.velocity - 1.0);
        const double ampBase = velGain * patch->masterVolume * eff.voiceAmp;
        // Each routing slot is shaped by the envelopes it selects (Env 1 by default).
        std::array<double, 7> slotNow {};
        for (int sl = 0; sl < 7; ++sl) {
            double g = ampBase;
            for (int e = 0; e < 4; ++e) if (patch->routes[std::size_t(sl)].viaEnv[std::size_t(e)]) g *= clampd(v.envVal[std::size_t(e)], 0.0, 1.5);
            slotNow[std::size_t(sl)] = g;
        }
        const double directScale = eff.directVol;
        const bool secondToFirst = patch->routes[6].target == RouteTarget::filter && patch->routes[5].target != RouteTarget::filter;
        const std::array<int, 2> order = secondToFirst ? std::array<int, 2>{1, 0} : std::array<int, 2>{0, 1};
        const double b1 = clampd(patch->globals.fxBus1Vol, 0.0, 2.0), b2 = clampd(patch->globals.fxBus2Vol, 0.0, 2.0);
        (void) b1; (void) b2;
        for (int k = 0; k < bl; ++k) {
            const double t = double(k + 1) / double(bl);
            std::array<double, 7> a {};
            for (int sl = 0; sl < 7; ++sl) a[std::size_t(sl)] = v.slotAmpPrev[std::size_t(sl)] + (slotNow[std::size_t(sl)] - v.slotAmpPrev[std::size_t(sl)]) * t;
            modsSnap = v.raw;
            std::array<std::array<double, 2>, 2> fin {};
            double mainA[2] = {0, 0}, directA[2] = {0, 0}, bus1[2] = {0, 0}, bus2[2] = {0, 0};
            for (int i = 0; i < 5; ++i) {
                const auto& osc = patch->oscillators[std::size_t(i)];
                if (!osc.enabled || !v.zoneOk[std::size_t(i)]) { v.raw[std::size_t(i)] = 0.0; continue; }
                const OscOut o = oscRender(v.osc[std::size_t(i)], inputs[std::size_t(i)]);
                v.raw[std::size_t(i)] = o.raw;
                const auto& ev = eff.osc[std::size_t(i)];
                const double vol = at(ev, OscParam::volume), pan = at(ev, OscParam::pan);
                const double sl = o.l * vol * std::sqrt(1.0 - pan), sr2 = o.r * vol * std::sqrt(1.0 + pan);
                const auto& route = patch->routes[std::size_t(i)];
                const double ga = a[std::size_t(i)];
                if (eff.routeBus1[std::size_t(i)] > 0.0) { const double s1 = eff.routeBus1[std::size_t(i)] / 100.0 * ga; bus1[0] += sl * s1; bus1[1] += sr2 * s1; }
                if (eff.routeBus2[std::size_t(i)] > 0.0) { const double s2 = eff.routeBus2[std::size_t(i)] / 100.0 * ga; bus2[0] += sl * s2; bus2[1] += sr2 * s2; }
                switch (route.target) {
                    case RouteTarget::none: break;
                    case RouteTarget::filter: {
                        const double bal = eff.routeBalance[std::size_t(i)];
                        const double w1 = clampd((100.0 - bal) / 100.0, 0.0, 1.0), w2 = clampd((100.0 + bal) / 100.0, 0.0, 1.0);
                        fin[0][0] += sl * w1; fin[0][1] += sr2 * w1; fin[1][0] += sl * w2; fin[1][1] += sr2 * w2;
                        break;
                    }
                    case RouteTarget::direct: directA[0] += sl * ga; directA[1] += sr2 * ga; break;
                    default: mainA[0] += sl * ga; mainA[1] += sr2 * ga; break;
                }
            }
            for (int position = 0; position < 2; ++position) {
                const int index = order[std::size_t(position)];
                const auto slot = std::size_t(index);
                const auto& pf = patch->filters[slot];
                auto out = fin[slot];
                if (pf.enabled) {
                    double a = out[0] * padGain[slot], b = out[1] * padGain[slot];
                    const double da = out[0], db = out[1];
                    v.filter[slot].process(a, b);
                    const double mix = wetMix[slot];
                    out[0] = (da + mix * (a - da)) * levelGain[slot]; out[1] = (db + mix * (b - db)) * levelGain[slot];
                }
                v.raw[5 + slot] = 0.5 * (out[0] + out[1]);
                const auto& rt = patch->routes[std::size_t(index + 5)];
                const double gf = a[std::size_t(index + 5)];
                if (eff.routeBus1[std::size_t(index + 5)] > 0.0) { const double s1 = eff.routeBus1[std::size_t(index + 5)] / 100.0 * gf; bus1[0] += out[0] * s1; bus1[1] += out[1] * s1; }
                if (eff.routeBus2[std::size_t(index + 5)] > 0.0) { const double s2 = eff.routeBus2[std::size_t(index + 5)] / 100.0 * gf; bus2[0] += out[0] * s2; bus2[1] += out[1] * s2; }
                if (rt.target == RouteTarget::none) continue;
                if (rt.target == RouteTarget::filter && position == 0) { fin[std::size_t(1 - index)][0] += out[0]; fin[std::size_t(1 - index)][1] += out[1]; }
                else if (rt.target == RouteTarget::direct) { directA[0] += out[0] * gf; directA[1] += out[1] * gf; }
                else { mainA[0] += out[0] * gf; mainA[1] += out[1] * gf; }
            }
            if (v.tailLeft > 0) {
                const double f = double(v.tailLeft--) / 96.0;
                mainA[0] += v.tail[0] * f; mainA[1] += v.tail[1] * f; directA[0] += v.tail[2] * f; directA[1] += v.tail[3] * f;
                bus1[0] += v.tail[4] * f; bus1[1] += v.tail[5] * f; bus2[0] += v.tail[6] * f; bus2[1] += v.tail[7] * f;
            }
            v.lastOut = {mainA[0], mainA[1], directA[0], directA[1], bus1[0], bus1[1], bus2[0], bus2[1]};
            const int n = at0 + k;
            mainL[std::size_t(n)] += mainA[0]; mainR[std::size_t(n)] += mainA[1];
            dirL[std::size_t(n)] += directA[0] * directScale; dirR[std::size_t(n)] += directA[1] * directScale;
            b1L[std::size_t(n)] += bus1[0]; b1R[std::size_t(n)] += bus1[1];
            b2L[std::size_t(n)] += bus2[0]; b2R[std::size_t(n)] += bus2[1];
        }
        v.slotAmpPrev = slotNow;
        if (v.age == clock) for (int i = 0; i < 5; ++i) phaseMemory[std::size_t(i)] = v.osc[std::size_t(i)].phase;
        v.everRendered = true;
        if (v.released && !v.env[0].active()) v.active = false;
        if (!v.env[0].active() && v.env[0].value() <= 0.0 && v.released) v.active = false;
    }

    void renderChunk(int len) noexcept {
        std::fill_n(mainL.begin(), len, 0.0); std::fill_n(mainR.begin(), len, 0.0);
        std::fill_n(dirL.begin(), len, 0.0); std::fill_n(dirR.begin(), len, 0.0);
        std::fill_n(b1L.begin(), len, 0.0); std::fill_n(b1R.begin(), len, 0.0);
        std::fill_n(b2L.begin(), len, 0.0); std::fill_n(b2R.begin(), len, 0.0);
        const int blocks = (len + kBlock - 1) / kBlock;
        // global LFO block values
        for (int b = 0; b < blocks; ++b) {
            const int bl = std::min(kBlock, len - b * kBlock);
            for (int i = 0; i < 10; ++i) {
                if (!lfoSupported[std::size_t(i)]) { gLfoBlock[std::size_t(b)][std::size_t(i)] = 0.0; continue; }
                const auto& def = lfoDef(i);
                gLfoBlock[std::size_t(b)][std::size_t(i)] = advanceLfo(gLfo[std::size_t(i)], def, lfoRateHz(def, bpm), double(bl) / sr,
                    def.phaseDegrees / 360.0, def.smooth, def.rise, def.delay, busOff.data());
            }
        }
        for (int i = 0; i < 10; ++i) gLfoNow[std::size_t(i)] = gLfoBlock[0][std::size_t(i)];
        for (int b = 0; b < blocks; ++b) {
            const int at0 = b * kBlock;
            const int bl = std::min(kBlock, len - at0);
            tickSequencers(bl);
            for (auto& vp : voices) if (vp->active) renderVoiceBlock(*vp, bl, at0, b);
        }
        publishDisplayState();
    }

    // editor display feed: relaxed atomic stores only (no locks, no allocation)
    std::array<std::array<std::atomic<float>, 3>, 3> display {};
    std::atomic<bool> displayValid {false};
    void publishDisplayState() noexcept {
        const Voice* newest = nullptr;
        for (const auto& vp : voices) if (vp->active && !vp->released && (!newest || vp->age > newest->age)) newest = vp.get();
        if (!newest) for (const auto& vp : voices) if (vp->active && (!newest || vp->age > newest->age)) newest = vp.get();
        if (!newest) { displayValid.store(false, std::memory_order_relaxed); return; }
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& v = newest->eff.osc[i];
            display[i][0].store(float(at(v, OscParam::tablePos)), std::memory_order_relaxed);
            display[i][1].store(float(at(v, OscParam::warp1)), std::memory_order_relaxed);
            display[i][2].store(float(at(v, OscParam::warp2)), std::memory_order_relaxed);
        }
        displayValid.store(true, std::memory_order_relaxed);
    }

    void render(float* left, float* right, int first, int count) noexcept {
        if (!patch) return;
        int done = 0;
        while (done < count) {
            const int len = std::min(kChunk, count - done);
            // Sidechain: widen this chunk to double, update the follower, hand pointers to the FX units.
            FxContext fc; fc.bpm = bpm; fc.beat = beat; fc.noteSerial = pumpSerial;
            if (scIn[0] && first + done + len <= scFrames) {
                for (int i = 0; i < len; ++i) {
                    scL[std::size_t(i)] = double(scIn[0][first + done + i]); scR[std::size_t(i)] = double(scIn[1][first + done + i]);
                    const double x = std::max(std::abs(scL[std::size_t(i)]), std::abs(scR[std::size_t(i)]));
                    scFollow += (x > scFollow ? 0.02 : 0.0005) * (x - scFollow);
                }
                fc.scL = scL.data(); fc.scR = scR.data();
            } else scFollow *= 0.9;
            fx.setContext(fc);
            evalGlobalMatrix(double(len) / sr);
            renderChunk(len);
            // FX racks
            fx.processRack(0, mainL.data(), mainR.data(), len);
            const auto& g = patch->globals;
            if (fx.hasModules(1)) fx.processRack(1, b1L.data(), b1R.data(), len);
            if (fx.hasModules(2)) fx.processRack(2, b2L.data(), b2R.data(), len);
            const double g1 = clampd(g.fxBus1Vol, 0.0, 2.0), g2 = clampd(g.fxBus2Vol, 0.0, 2.0);
            const bool oneToTwo = g.fxBus1Dest == 2;
            for (int i = 0; i < len; ++i) {
                double l = mainL[std::size_t(i)] + dirL[std::size_t(i)], r = mainR[std::size_t(i)] + dirR[std::size_t(i)];
                if (oneToTwo) { b2L[std::size_t(i)] += b1L[std::size_t(i)] * g1; b2R[std::size_t(i)] += b1R[std::size_t(i)] * g1; }
                else { l += b1L[std::size_t(i)] * g1; r += b1R[std::size_t(i)] * g1; }
                l += b2L[std::size_t(i)] * g2; r += b2R[std::size_t(i)] * g2;
                if (!std::isfinite(l)) l = 0.0;
                if (!std::isfinite(r)) r = 0.0;
                if (limiterOn) { l = safetyLimit(l); r = safetyLimit(r); }
                left[first + done + i] += float(l); right[first + done + i] += float(r);
            }
            // advance host beat position bookkeeping
            beat += double(len) / sr * beatsPerSecond(bpm);
            done += len;
        }
    }
};

SynthEngine::SynthEngine() : impl_(std::make_unique<Impl>()) { impl_->prepare(44100.0); }
SynthEngine::~SynthEngine() = default;
void SynthEngine::prepare(double sampleRate) { impl_->prepare(sampleRate); }
void SynthEngine::setPatch(const Patch* patch) noexcept { impl_->setPatch(patch); }
void SynthEngine::noteOn(int channel, int note, float velocity) noexcept { impl_->routeNoteOn(channel, note, velocity); }
void SynthEngine::noteOff(int channel, int note, float releaseVelocity) noexcept { impl_->routeNoteOff(channel, note, releaseVelocity); }
void SynthEngine::allNotesOff() noexcept { impl_->allNotesOff(); }
void SynthEngine::render(float* left, float* right, int firstSample, int count) noexcept { impl_->render(left, right, firstSample, count); }
int SynthEngine::activeVoiceCount() const noexcept { return impl_->activeCount(); }
bool SynthEngine::oscDisplayState(int osc, OscDisplayState& out) const noexcept {
    if (osc < 0 || osc > 2 || !impl_->displayValid.load(std::memory_order_relaxed)) return false;
    const auto& d = impl_->display[std::size_t(osc)];
    out.tablePos = d[0].load(std::memory_order_relaxed); out.warp1 = d[1].load(std::memory_order_relaxed); out.warp2 = d[2].load(std::memory_order_relaxed);
    return true;
}
void SynthEngine::setTransport(double bpm, double beatPosition, bool playing) noexcept {
    impl_->bpm = std::clamp(bpm, 20.0, 400.0); impl_->playing = playing;
    if (playing) impl_->beat = beatPosition;   // stopped: the beat clock free-runs so tempo-synced effects keep moving
}
void SynthEngine::setSidechain(const float* left, const float* right, int frames) noexcept {
    impl_->scIn[0] = left; impl_->scIn[1] = right ? right : left; impl_->scFrames = frames;
}
void SynthEngine::setModWheel(float value) noexcept { impl_->modWheel = std::clamp(value, 0.0f, 1.0f); }
void SynthEngine::setPitchBend(int channel, float value) noexcept {
    const float v = std::clamp(value, -1.0f, 1.0f);
    if (channel <= 0) { impl_->bend.fill(v); return; }
    impl_->bend[std::size_t(std::clamp(channel - 1, 0, 15))] = v; impl_->bend[0] = impl_->patch && impl_->patch->globals.mpeEnabled ? impl_->bend[0] : v;
}
void SynthEngine::setChannelPressure(int channel, float value) noexcept {
    const float v = std::clamp(value, 0.0f, 1.0f);
    if (channel <= 0) { impl_->pressure.fill(v); return; }
    impl_->pressure[std::size_t(std::clamp(channel - 1, 0, 15))] = v;
    if (!(impl_->patch && impl_->patch->globals.mpeEnabled)) impl_->pressure.fill(v);
}
void SynthEngine::setPolyPressure(int note, float value) noexcept { impl_->polyPressure[std::size_t(std::clamp(note, 0, 127))] = std::clamp(value, 0.0f, 1.0f); }
void SynthEngine::setControlChange(int channel, int controller, float value) noexcept {
    if (controller >= 0 && controller < 128) impl_->midiCC[std::size_t(controller)] = std::clamp(value, 0.0f, 1.0f);
    if (controller == 1) impl_->modWheel = std::clamp(value, 0.0f, 1.0f);
    else if (controller == 64) setSustainPedal(value >= 0.5f);
    else if (controller == 74) impl_->cc74[std::size_t(std::clamp(channel - 1, 0, 15))] = std::clamp(value, 0.0f, 1.0f);
    else if (controller == 120 || controller == 123) allNotesOff();
}
void SynthEngine::setSafetyLimiter(bool enabled) noexcept { impl_->limiterOn = enabled; }
void SynthEngine::setSustainPedal(bool down) noexcept {
    impl_->sustain = down;
    if (!down) impl_->releaseSustained();
}
}
