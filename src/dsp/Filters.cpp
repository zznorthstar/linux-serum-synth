#include "Filters.h"
#include <cstring>

namespace zyg::dsp {
namespace {
struct MultiDef { const char* id; char a, b, c; int stages; };
// Letters: L low, B band, H high, N notch, P peak. Two identical letters mean
// two spread resonators (Var sets their spacing) instead of a morph.
constexpr std::array<MultiDef, 20> multiTable {{
    {"LB12", 'L', 'B', 0, 1}, {"HB12", 'H', 'B', 0, 1}, {"LH12", 'L', 'H', 0, 1},
    {"LN12", 'L', 'N', 0, 1}, {"HN12", 'H', 'N', 0, 1}, {"BN12", 'B', 'N', 0, 1},
    {"BP12", 'B', 'P', 0, 1}, {"HP12", 'H', 'P', 0, 1}, {"LBH12", 'L', 'B', 'H', 1},
    {"LNH12", 'L', 'N', 'H', 1}, {"LBH24", 'L', 'B', 'H', 2}, {"LNH24", 'L', 'N', 'H', 2},
    {"LPH24", 'L', 'P', 'H', 2}, {"BPN12", 'B', 'P', 'N', 1}, {"BPN24", 'B', 'P', 'N', 2},
    {"PN12", 'P', 'N', 0, 1}, {"PP12", 'P', 'P', 0, 1}, {"NN12", 'N', 'N', 0, 1},
    {"P12", 'P', 'P', 0, 1}, {"LPH12", 'L', 'P', 'H', 1}}};

struct IdEntry { const char* id; FilterResponse response; int variant; };
constexpr IdEntry idTable[] = {
    {"L6", FilterResponse::low6, 0}, {"L12", FilterResponse::low12, 0}, {"L18", FilterResponse::low18, 0},
    {"L24", FilterResponse::low24, 0}, {"H6", FilterResponse::high6, 0}, {"H12", FilterResponse::high12, 0},
    {"H18", FilterResponse::high18, 0}, {"H24", FilterResponse::high24, 0}, {"B12", FilterResponse::band12, 0},
    {"B24", FilterResponse::band24, 0}, {"N12", FilterResponse::notch12, 0}, {"N24", FilterResponse::notch24, 0},
    {"BandReject", FilterResponse::bandReject, 0},
    {"MgL6", FilterResponse::ladder6, 0}, {"MgL12", FilterResponse::ladder12, 0},
    {"MgL18", FilterResponse::ladder18, 0}, {"MgL24", FilterResponse::ladder24, 0},
    {"LadderMg", FilterResponse::ladder24, 1}, {"DirtyMg", FilterResponse::ladderDirty, 0},
    {"LadderEMS", FilterResponse::ladderEms, 0}, {"LadderAcid", FilterResponse::ladderAcid, 0},
    // comb: bit0 negative, bit1 loop LP6, bit2 loop HP6; 0x100 = two combs, 0x200 = comb bank
    {"CombP", FilterResponse::comb, 0}, {"CombN", FilterResponse::comb, 1},
    {"CombL6N", FilterResponse::comb, 1 | 2}, {"CombH6N", FilterResponse::comb, 1 | 4},
    {"CombHL6N", FilterResponse::comb, 1 | 2 | 4}, {"CombH6P", FilterResponse::comb, 4},
    {"CombHL6P", FilterResponse::comb, 2 | 4}, {"Comb2", FilterResponse::comb, 0x100},
    {"Combs", FilterResponse::comb, 0x200},
    // flange: same flag layout, 0x400 adds a two-stage allpass in the loop
    {"FlangeP", FilterResponse::flange, 0}, {"FlangeN", FilterResponse::flange, 1},
    {"FlangeL6P", FilterResponse::flange, 2}, {"FlangeL6N", FilterResponse::flange, 1 | 2},
    {"FlangeH6P", FilterResponse::flange, 4}, {"FlangeHL6P", FilterResponse::flange, 2 | 4},
    {"FlangeHL6N", FilterResponse::flange, 1 | 2 | 4},
    {"FlangePhase12HL6P", FilterResponse::flange, 2 | 4 | 0x400},
    // phaser: low byte = allpass stage count, 0x100 negative feedback, 0x200 loop LP, 0x400 loop HP
    {"Phase12N", FilterResponse::phaser, 2 | 0x100}, {"Phase12P", FilterResponse::phaser, 2},
    {"Phase24N", FilterResponse::phaser, 4 | 0x100}, {"Phase24P", FilterResponse::phaser, 4},
    {"Phase36N", FilterResponse::phaser, 6 | 0x100}, {"Phase36P", FilterResponse::phaser, 6},
    {"Phase48N", FilterResponse::phaser, 8 | 0x100}, {"Phase48P", FilterResponse::phaser, 8},
    {"Phase48H6P", FilterResponse::phaser, 8 | 0x400}, {"Phase48HL6P", FilterResponse::phaser, 8 | 0x200 | 0x400},
    {"Phase48HL6N", FilterResponse::phaser, 8 | 0x100 | 0x200 | 0x400},
    {"FormantONE", FilterResponse::formant, 0}, {"FormantTWO", FilterResponse::formant, 1},
    {"FormantTWB", FilterResponse::formant, 2},
    {"Allpasses", FilterResponse::allpass, 0}, {"Diffuser", FilterResponse::diffuser, 0},
    {"DJMixer", FilterResponse::djMixer, 0}, {"HEQ6", FilterResponse::shelfEq, 0},
    {"HEQ12", FilterResponse::shelfEq, 1}, {"PZ_SVF", FilterResponse::polezero, 0},
    {"Exp", FilterResponse::exponential, 0}, {"ExpBPF", FilterResponse::exponential, 1},
    {"Scream", FilterResponse::screamer, 0}, {"Scream3LP", FilterResponse::screamer, 1},
    {"Scream3BP", FilterResponse::screamer, 2}, {"Wsp", FilterResponse::waveshaper, 0},
    {"RM", FilterResponse::ringMod, 0}, {"RMT", FilterResponse::ringMod, 1},
    {"SNH1", FilterResponse::sampleHold, 0}, {"ADD_BASS", FilterResponse::addBass, 0},
    {"ZDF_A", FilterResponse::zdfAnalog, 0},
    {"DistComb1LP", FilterResponse::distComb, 0}, {"DistComb1BP", FilterResponse::distComb, 1},
    {"DistComb2LP", FilterResponse::distComb, 2}, {"DistComb2BP", FilterResponse::distComb, 3},
    {"Reverb1", FilterResponse::filterReverb, 0},
};

constexpr double vowels[5][3] = {{800, 1150, 2900}, {350, 2000, 2800}, {270, 2140, 2950},
                                 {450, 800, 2830}, {325, 700, 2700}};
}

bool filterFromSerumId(const std::string& id, FilterResponse& response, int& variant) noexcept {
    for (const auto& e : idTable) if (id == e.id) { response = e.response; variant = e.variant; return true; }
    for (std::size_t i = 0; i < multiTable.size(); ++i)
        if (id == multiTable[i].id) { response = FilterResponse::multi; variant = int(i); return true; }
    return false;
}
std::string filterSerumIdName(FilterResponse response, int variant) {
    for (const auto& e : idTable) if (e.response == response && e.variant == variant) return e.id;
    if (response == FilterResponse::multi && variant >= 0 && variant < int(multiTable.size()))
        return multiTable[std::size_t(variant)].id;
    return {};
}
const char* filterFamilyName(FilterResponse r) noexcept {
    switch (r) {
        case FilterResponse::multi: return "multi"; case FilterResponse::comb: return "comb";
        case FilterResponse::flange: return "flange"; case FilterResponse::phaser: return "phaser";
        case FilterResponse::formant: return "formant"; default: return "normal";
    }
}

void FilterCore::prepare(double sampleRate) {
    sr_ = std::max(8000.0, sampleRate);
    const std::size_t comb = std::size_t(std::min(16384.0, sr_ / 5.0)) + 8;
    for (auto& c : ch_) {
        c.delayA.allocate(comb); c.delayB.allocate(comb);
        const double lens[4] = {0.0297, 0.0371, 0.0411, 0.0437};
        for (std::size_t i = 0; i < 4; ++i) c.diffuse[i].allocate(std::size_t(lens[i] * sr_ * 2.0) + 8);
        c.rng = Rng(0x1234567u + std::uint32_t(&c - ch_.data()) * 977u);
    }
    reset();
}

bool FilterCore::usesDelayMemory() const noexcept {
    switch (p_.type) {
        case FilterResponse::comb: case FilterResponse::flange: case FilterResponse::distComb:
        case FilterResponse::diffuser: case FilterResponse::filterReverb: return true;
        default: return false;
    }
}

void FilterCore::reset() noexcept {
    for (auto& c : ch_) {
        for (auto& s : c.svf) s.reset();
        for (auto& s : c.pole) s.reset();
        for (auto& s : c.ap) s.reset();
        for (auto& s : c.formant) s.reset();
        c.ladder = {}; c.ladderAux = {}; c.fdn = {};
        c.loopState = c.loopState2 = c.hold = c.phase = c.last = 0.0;
        if (c.delayA.allocated()) { c.delayA.clear(); c.delayB.clear(); for (auto& d : c.diffuse) d.clear(); }
    }
}

void FilterCore::update(const FilterParams& p) noexcept {
    p_ = p;
    hz_ = {clampd(p.cutoffHzL, 8.0, sr_ * 0.45), clampd(p.cutoffHzR, 8.0, sr_ * 0.45)};
    q_ = 0.707 + 9.293 * clampd(p.reso, 0.0, 1.0);
    k_ = 1.0 / q_;
    driveGain_ = 1.0 + 5.0 * clampd(p.drive, 0.0, 1.0);
    driveNorm_ = p.drive > 0.0 ? 1.0 / std::sqrt(driveGain_) : 1.0; // saturation without a large level jump
    feedback_ = 0.985 * clampd(p.reso, 0.0, 1.0);
    const auto t = p.type;
    for (int ci = 0; ci < 2; ++ci) {
        auto& c = ch_[std::size_t(ci)];
        const double hz = hz_[std::size_t(ci)];
        g1_[std::size_t(ci)] = std::tan(pi * std::min(hz, sr_ * 0.45) / sr_);
        switch (t) {
            case FilterResponse::low6: case FilterResponse::high6:
                c.pole[0].set(hz, sr_); break;
            case FilterResponse::low12: case FilterResponse::high12: case FilterResponse::band12:
            case FilterResponse::notch12: case FilterResponse::bandReject:
                c.svf[0].set(hz, t == FilterResponse::bandReject ? q_ * 1.5 : q_, sr_); break;
            case FilterResponse::low18: case FilterResponse::high18:
                c.svf[0].set(hz, q_, sr_); c.pole[0].set(hz, sr_); break;
            case FilterResponse::low24: case FilterResponse::high24: case FilterResponse::band24:
            case FilterResponse::notch24:
                c.svf[0].set(hz, q_, sr_); c.svf[1].set(hz, 0.707 + 0.35 * (q_ - 0.707), sr_); break;
            case FilterResponse::multi: {
                const auto& m = multiTable[std::size_t(std::clamp(p.variant, 0, int(multiTable.size()) - 1))];
                if (m.a == m.b) {
                    const double spread = std::exp2(0.02 + 2.5 * p.var);
                    c.svf[0].set(hz / spread, q_, sr_); c.svf[1].set(hz * spread, q_, sr_);
                } else {
                    c.svf[0].set(hz, q_, sr_); c.svf[1].set(hz, q_, sr_);
                }
                break;
            }
            case FilterResponse::polezero:
                c.svf[0].set(hz, q_ * (0.6 + 1.6 * p.y), sr_); break;
            case FilterResponse::exponential: {
                const double q = 0.5 + 40.0 * std::pow(clampd(p.reso, 0, 1), 2.5);
                c.svf[0].set(hz, q, sr_); break;
            }
            case FilterResponse::screamer:
                c.svf[0].set(hz, 0.9 + 4.0 * p.reso, sr_); c.svf[1].set(hz, 0.7, sr_); break;
            case FilterResponse::waveshaper:
                c.svf[0].set(hz, 0.8, sr_); break;
            case FilterResponse::sampleHold:
                c.pole[0].set(std::max(200.0, hz * (1.5 - clampd(p.reso, 0, 1))), sr_); break;
            case FilterResponse::addBass:
                c.svf[0].set(hz, 0.707, sr_); c.svf[1].set(hz, 0.707, sr_); break;
            case FilterResponse::formant: {
                const double ratio = clampd(std::pow(hz / 1000.0, 0.25), 0.5, 2.0);
                const double v = clampd(p.variant == 0 ? p.var : p.x, 0.0, 1.0) * 4.0;
                const int i0 = std::min(3, int(v)); const double f = v - i0;
                const double v2 = clampd(p.y, 0.0, 1.0) * 4.0; const int j0 = std::min(3, int(v2)); const double fj = v2 - j0;
                const double q = 4.0 + 24.0 * p.reso;
                for (int f3 = 0; f3 < 3; ++f3) {
                    double freq = lerp(vowels[i0][f3], vowels[i0 + 1][f3], f);
                    if (p.variant != 0) freq = lerp(freq, lerp(vowels[j0][f3], vowels[j0 + 1][f3], fj), 0.5);
                    c.formant[std::size_t(f3)].set(freq * ratio, q, sr_);
                }
                break;
            }
            case FilterResponse::phaser: {
                const int n = p.variant & 0xff;
                const double spread = std::exp2(0.35 * p.var);
                for (int i = 0; i < n && i < 16; ++i)
                    c.ap[std::size_t(i)].set(hz * std::pow(spread, i), sr_);
                c.pole[0].set(std::min(hz * 4.0, sr_ * 0.4), sr_); c.pole[1].set(std::max(20.0, hz * 0.25), sr_);
                break;
            }
            case FilterResponse::allpass: {
                const double spread = std::exp2(0.25 + 0.6 * p.var);
                for (int i = 0; i < 6; ++i) c.ap[std::size_t(i)].set(hz * std::pow(spread, i - 2), sr_);
                break;
            }
            case FilterResponse::comb: case FilterResponse::flange: case FilterResponse::distComb:
                c.pole[0].set(2000.0 * std::exp2(4.0 * p.var), sr_);
                c.pole[1].set(30.0 * std::exp2(3.0 * p.var), sr_);
                if (t == FilterResponse::distComb) c.svf[0].set(std::min(hz * 4.0, sr_ * 0.4), 1.0, sr_);
                if (t == FilterResponse::flange) { c.ap[0].set(hz * 2.0, sr_); c.ap[1].set(hz * 3.0, sr_); }
                break;
            case FilterResponse::djMixer: {
                const double norm = std::log(hz / 20.0) / std::log(1000.0);
                c.svf[0].set(20.0 * std::pow(1000.0, clampd(norm * 2.0, 0.0, 1.0)), 0.707 + 3.0 * p.reso, sr_);
                c.svf[1].set(20.0 * std::pow(1000.0, clampd(norm * 2.0 - 1.0, 0.0, 1.0)), 0.707 + 3.0 * p.reso, sr_);
                break;
            }
            case FilterResponse::shelfEq:
                c.pole[0].set(hz, sr_); c.svf[0].set(hz, 0.707, sr_); break;
            case FilterResponse::diffuser: case FilterResponse::filterReverb:
                c.pole[0].set(std::min(sr_ * 0.45, 2000.0 + hz), sr_); break;
            default: break;
        }
    }
}

double FilterCore::tick(Chan& c, int channel, double in) noexcept {
    const auto& p = p_;
    const std::size_t ci = std::size_t(channel);
    if (p.drive > 0.0) in = fastTanh(in * driveGain_) * driveNorm_;
    const double hz = hz_[ci];
    switch (p.type) {
        case FilterResponse::low6: return c.pole[0].low(in);
        case FilterResponse::high6: return c.pole[0].high(in);
        case FilterResponse::low12: return c.svf[0].process(in).low;
        case FilterResponse::high12: return c.svf[0].process(in).high;
        case FilterResponse::band12: { const auto o = c.svf[0].process(in); return o.band * c.svf[0].k; }
        case FilterResponse::notch12: case FilterResponse::bandReject: {
            const auto o = c.svf[0].process(in); return o.low + o.high;
        }
        case FilterResponse::low18: return c.pole[0].low(c.svf[0].process(in).low);
        case FilterResponse::high18: { const auto o = c.svf[0].process(in).high; return c.pole[0].high(o); }
        case FilterResponse::low24: return c.svf[1].process(c.svf[0].process(in).low).low;
        case FilterResponse::high24: return c.svf[1].process(c.svf[0].process(in).high).high;
        case FilterResponse::band24: {
            const auto o = c.svf[0].process(in); const auto o2 = c.svf[1].process(o.band * c.svf[0].k);
            return o2.band * c.svf[1].k;
        }
        case FilterResponse::notch24: {
            const auto o = c.svf[0].process(in); const auto o2 = c.svf[1].process(o.low + o.high);
            return o2.low + o2.high;
        }
        case FilterResponse::ladder6: case FilterResponse::ladder12: case FilterResponse::ladder18:
        case FilterResponse::ladder24: case FilterResponse::ladderDirty: case FilterResponse::ladderEms:
        case FilterResponse::ladderAcid: {
            const double G = g1_[ci] / (1.0 + g1_[ci]);
            const double fat = 1.0 + 3.0 * clampd(p.var, 0.0, 1.0);
            double res = 3.8 * clampd(p.reso, 0.0, 1.0), fbTap = c.ladder[3];
            int tap = 3; double pre = 1.0;
            switch (p.type) {
                case FilterResponse::ladder6: tap = 0; break;
                case FilterResponse::ladder12: tap = 1; break;
                case FilterResponse::ladder18: tap = 2; break;
                case FilterResponse::ladderDirty: pre = 2.2; break;
                case FilterResponse::ladderEms: tap = 1; res = 3.0 * clampd(p.reso, 0.0, 1.0); fbTap = c.ladder[1]; break;
                case FilterResponse::ladderAcid: tap = 2; res = 3.4 * clampd(p.reso, 0.0, 1.0); fbTap = c.ladder[3]; break;
                default: break;
            }
            const double bias = p.type == FilterResponse::ladderDirty ? 0.15 : (p.type == FilterResponse::ladderAcid ? 0.08 : 0.0);
            double stageInput = fastTanh(pre * (in - res * fastTanh(fbTap * fat)) + bias);
            double out = 0.0;
            for (int s = 0; s < 4; ++s) {
                auto& st = c.ladder[std::size_t(s)];
                const double v = (stageInput - st) * G;
                const double low = v + st;
                st = flush(low + v);
                stageInput = p.type == FilterResponse::ladderEms ? low / (1.0 + std::abs(low)) : fastTanh(low);
                if (s == tap) out = low;
            }
            return out / pre;
        }
        case FilterResponse::zdfAnalog: {
            const double g = g1_[ci], G = g / (1.0 + g), k = 4.0 * clampd(p.reso, 0.0, 1.0) * 0.98;
            const double G2 = G * G, G4 = G2 * G2;
            const double S = (G2 * G * c.ladder[0] + G2 * c.ladder[1] + G * c.ladder[2] + c.ladder[3]) / (1.0 + g);
            const double u = fastTanh((in - k * S) / (1.0 + k * G4));
            double x = u;
            for (int s = 0; s < 4; ++s) {
                auto& st = c.ladder[std::size_t(s)];
                const double v = (x - st) * G; const double y = v + st; st = flush(y + v); x = y;
            }
            return x;
        }
        case FilterResponse::multi: {
            const auto& m = multiTable[std::size_t(std::clamp(p.variant, 0, int(multiTable.size()) - 1))];
            auto pick = [&](char letter, const Svf::Out& o, const Svf& s) {
                switch (letter) {
                    case 'L': return o.low; case 'H': return o.high;
                    case 'B': return o.band * s.k; case 'N': return o.low + o.high;
                    default:  return o.low - o.high; // peak
                }
            };
            if (m.a == m.b) { // two spread resonators
                const auto o1 = c.svf[0].process(in), o2 = c.svf[1].process(in);
                return 0.5 * (pick(m.a, o1, c.svf[0]) + pick(m.a, o2, c.svf[1]));
            }
            double sig = in;
            double result = 0.0;
            for (int stage = 0; stage < m.stages; ++stage) {
                auto& s = c.svf[std::size_t(stage)];
                const auto o = s.process(sig);
                const int n = m.c ? 3 : 2;
                const double pos = clampd(p.var, 0.0, 1.0) * (n - 1);
                const int i0 = std::min(n - 2, int(pos)); const double f = pos - i0;
                const char letters[3] = {m.a, m.b, m.c};
                result = lerp(pick(letters[i0], o, s), pick(letters[i0 + 1], o, s), f);
                sig = result;
            }
            return result;
        }
        case FilterResponse::polezero: {
            const auto o = c.svf[0].process(in);
            const double pos = clampd(p.x, 0.0, 1.0) * 2.0;
            const double lowW = std::max(0.0, 1.0 - pos), highW = std::max(0.0, pos - 1.0);
            const double bandW = 1.0 - std::abs(pos - 1.0);
            return lowW * o.low + bandW * o.band * c.svf[0].k + highW * o.high;
        }
        case FilterResponse::exponential: {
            const auto o = c.svf[0].process(in);
            return p.variant == 0 ? o.low : o.band * c.svf[0].k;
        }
        case FilterResponse::screamer: {
            const double pre = in * (6.0 + 60.0 * clampd(p.reso, 0, 1) * clampd(p.reso, 0, 1) + 20.0 * p.var);
            const double d = fastTanh(pre + 0.3) - fastTanh(0.3);
            const auto o = c.svf[0].process(d * 0.6);
            if (p.variant == 2) return o.band * c.svf[0].k;
            if (p.variant == 1) return c.svf[1].process(o.low).low;
            return o.low;
        }
        case FilterResponse::waveshaper: {
            const double amount = 1.0 + 6.0 * clampd(p.reso, 0.0, 1.0);
            const double folded = std::sin(in * amount * 1.5707963267948966);
            return c.svf[0].process(lerp(in, folded, 0.3 + 0.7 * p.var)).low;
        }
        case FilterResponse::ringMod: {
            c.phase = wrap01(c.phase + hz / sr_);
            const double carrier = p.variant == 1 ? 4.0 * std::abs(c.phase - 0.5) - 1.0 : std::sin(tau * c.phase);
            return lerp(in, in * carrier, 0.25 + 0.75 * clampd(p.reso, 0.0, 1.0));
        }
        case FilterResponse::sampleHold: {
            c.phase += std::min(hz * 2.0, sr_) / sr_;
            if (c.phase >= 1.0) { c.phase -= std::floor(c.phase); c.hold = in; }
            return c.pole[0].low(c.hold);
        }
        case FilterResponse::addBass: {
            const auto lo = c.svf[0].process(in).low;
            const double harmonics = fastTanh(4.0 * lo) - lo * 0.5;
            const auto hi = c.svf[1].process(in).high;
            return lo + 0.5 * clampd(p.var, 0, 1) * (c.pole[0].low(harmonics)) + 0.0 * hi;
        }
        case FilterResponse::formant: {
            double sum = c.formant[0].process(in).band * c.formant[0].k;
            if (p.variant >= 1) sum += 0.7 * c.formant[1].process(in).band * c.formant[1].k;
            if (p.variant >= 2) sum += 0.5 * c.formant[2].process(in).band * c.formant[2].k;
            return sum * 1.5;
        }
        case FilterResponse::phaser: {
            const int n = std::min(16, p.variant & 0xff);
            const double sign = (p.variant & 0x100) ? -1.0 : 1.0;
            double fb = c.last;
            if (p.variant & 0x200) fb = c.pole[0].low(fb);
            if (p.variant & 0x400) fb = c.pole[1].high(fb);
            double s = in + sign * feedback_ * fb;
            for (int i = 0; i < n; ++i) s = c.ap[std::size_t(i)].process(s);
            c.last = fastTanh(s);
            return 0.5 * (in + s);
        }
        case FilterResponse::allpass: {
            double s = in;
            for (int i = 0; i < 6; ++i) s = c.ap[std::size_t(i)].process(s);
            return 0.5 * (in + s);
        }
        case FilterResponse::comb: case FilterResponse::flange: case FilterResponse::distComb: {
            const double d = clampd(sr_ / hz, 2.0, double(c.delayA.capacity() - 8));
            const double sign = (p.variant & 1) ? -1.0 : 1.0;
            const bool isComb = p.type == FilterResponse::comb;
            const bool dist = p.type == FilterResponse::distComb;
            double fbSample = c.delayA.read(d);
            const int flags = isComb || p.type == FilterResponse::flange ? p.variant : 0;
            if (flags & 2) fbSample = c.pole[0].low(fbSample);
            if (flags & 4) fbSample = c.pole[1].high(fbSample);
            double y = in + (dist ? 1.0 : sign) * feedback_ * fbSample;
            if (dist) y = fastTanh(y * (1.0 + 2.0 * p.var));
            if (p.type == FilterResponse::flange && (p.variant & 0x400)) y = c.ap[1].process(c.ap[0].process(y));
            c.delayA.push(y);
            double out = y;
            if (isComb && (p.variant & 0x100)) {
                const double d2 = clampd(d * (1.0 + 0.5 * p.var + 0.25), 2.0, double(c.delayB.capacity() - 8));
                const double y2 = in - feedback_ * c.delayB.read(d2);
                c.delayB.push(y2);
                out = 0.5 * (y + y2);
            } else if (isComb && (p.variant & 0x200)) {
                const double d2 = clampd(d * 0.5, 2.0, double(c.delayB.capacity() - 8));
                const double y2 = in + feedback_ * c.delayB.read(d2);
                c.delayB.push(y2);
                out = 0.5 * (y + y2);
            } else if (p.type == FilterResponse::flange) {
                out = 0.5 * (in + y);
            } else if (dist) {
                const auto o = c.svf[0].process(y);
                out = (p.variant & 1) ? o.band * c.svf[0].k : o.low;
                if (p.variant & 2) { // second cascaded comb
                    const double y3 = out + 0.5 * feedback_ * c.delayB.read(clampd(d * 0.75, 2.0, double(c.delayB.capacity() - 8)));
                    c.delayB.push(fastTanh(y3));
                    out = y3;
                }
            }
            return out * (1.0 - 0.45 * feedback_);
        }
        case FilterResponse::djMixer: {
            const double norm = std::log(hz / 20.0) / std::log(1000.0);
            if (norm < 0.5) return c.svf[0].process(in).low;
            return c.svf[1].process(in).high;
        }
        case FilterResponse::shelfEq: {
            const auto o = c.svf[0].process(in);
            const double gain = std::pow(10.0, (clampd(p.reso, 0, 1) - 0.5) * 2.0 * 24.0 / 20.0);
            const double high = p.variant == 1 ? o.high : in - c.pole[0].low(in);
            return in + (gain - 1.0) * high;
        }
        case FilterResponse::diffuser: {
            const double scale = clampd(std::pow(hz / 1000.0, -0.5), 0.1, 2.0);
            const double g = 0.3 + 0.4 * clampd(p.reso, 0, 1);
            double s = in;
            const double lens[4] = {0.0297, 0.0371, 0.0411, 0.0437};
            for (std::size_t i = 0; i < 4; ++i) {
                const double delay = lens[i] * sr_ * scale * 0.5 * (0.5 + p.var);
                const double dlyd = c.diffuse[i].read(delay);
                const double v = s - g * dlyd; c.diffuse[i].push(v); s = dlyd + g * v;
            }
            return s;
        }
        case FilterResponse::filterReverb: {
            const double scale = clampd(std::pow(hz / 1000.0, -0.25), 0.3, 2.0);
            const double g = 0.5 + 0.48 * clampd(p.reso, 0, 1);
            const double lens[4] = {0.0297, 0.0371, 0.0411, 0.0437};
            double outs[4]; double sum = 0.0;
            for (std::size_t i = 0; i < 4; ++i) { outs[i] = c.diffuse[i].read(lens[i] * sr_ * scale); sum += outs[i]; }
            const double half = sum * 0.5;
            for (std::size_t i = 0; i < 4; ++i) {
                double v = (outs[i] - half) * g;
                if (i == 0) v = c.pole[0].low(v);
                c.diffuse[i].push(in * 0.5 + v);
            }
            return 0.3 * in + 0.35 * sum;
        }
        default:
            return c.svf[0].process(in).low;
    }
}

void FilterCore::process(double& l, double& r) noexcept {
    l = tick(ch_[0], 0, l);
    r = tick(ch_[1], 1, r);
    if (!std::isfinite(l)) { l = 0.0; reset(); }
    if (!std::isfinite(r)) { r = 0.0; reset(); }
}
}
