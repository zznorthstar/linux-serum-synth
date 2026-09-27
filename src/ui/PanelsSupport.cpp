#include "Panels.h"
#include <complex>
#include <cmath>

namespace zyg::ui {
namespace {
using C = std::complex<double>;
constexpr double kPi = 3.14159265358979323846;

struct IdName { const char* id; const char* name; };
// Grouped like the manual: a null id starts a group heading.
constexpr IdName kFilters[] = {
    {nullptr, "NORMAL"},
    {"L6", "LOW 6"}, {"L12", "LOW 12"}, {"L18", "LOW 18"}, {"L24", "LOW 24"},
    {"H6", "HIGH 6"}, {"H12", "HIGH 12"}, {"H18", "HIGH 18"}, {"H24", "HIGH 24"},
    {"B12", "BAND 12"}, {"B24", "BAND 24"}, {"N12", "NOTCH 12"}, {"N24", "NOTCH 24"}, {"BandReject", "BAND REJECT"},
    {nullptr, "MG LADDER"},
    {"MgL6", "MG LOW 6"}, {"MgL12", "MG LOW 12"}, {"MgL18", "MG LOW 18"}, {"MgL24", "MG LOW 24"},
    {"LadderMg", "MG LADDER"}, {"DirtyMg", "MG DIRTY"}, {"LadderEMS", "EMS LADDER"}, {"LadderAcid", "ACID LADDER"},
    {nullptr, "MULTI"},
    {"LB12", "LOW-BAND 12"}, {"HB12", "HIGH-BAND 12"}, {"LH12", "LOW-HIGH 12"}, {"LN12", "LOW-NOTCH 12"},
    {"HN12", "HIGH-NOTCH 12"}, {"BN12", "BAND-NOTCH 12"}, {"BP12", "BAND-PEAK 12"}, {"HP12", "HIGH-PEAK 12"},
    {"LBH12", "LOW-BAND-HIGH 12"}, {"LNH12", "LOW-NOTCH-HIGH 12"}, {"LBH24", "LOW-BAND-HIGH 24"},
    {"LNH24", "LOW-NOTCH-HIGH 24"}, {"LPH24", "LOW-PEAK-HIGH 24"}, {"BPN12", "BAND-PEAK-NOTCH 12"},
    {"BPN24", "BAND-PEAK-NOTCH 24"}, {"PN12", "PEAK-NOTCH 12"}, {"PP12", "PEAK-PEAK 12"},
    {"NN12", "NOTCH-NOTCH 12"}, {"P12", "PEAK 12"}, {"LPH12", "LOW-PEAK-HIGH 12"},
    {nullptr, "COMB"},
    {"CombP", "COMB +"}, {"CombN", "COMB -"}, {"CombL6N", "COMB L6 -"}, {"CombH6N", "COMB H6 -"},
    {"CombHL6N", "COMB HL6 -"}, {"CombH6P", "COMB H6 +"}, {"CombHL6P", "COMB HL6 +"}, {"Comb2", "COMB 2"},
    {"Combs", "COMBS"},
    {nullptr, "FLANGE"},
    {"FlangeP", "FLANGE +"}, {"FlangeN", "FLANGE -"}, {"FlangeL6P", "FLANGE L6 +"}, {"FlangeL6N", "FLANGE L6 -"},
    {"FlangeH6P", "FLANGE H6 +"}, {"FlangeHL6P", "FLANGE HL6 +"}, {"FlangeHL6N", "FLANGE HL6 -"},
    {"FlangePhase12HL6P", "FLANGE PHASE 12"},
    {nullptr, "PHASER"},
    {"Phase12N", "PHASE 12 -"}, {"Phase12P", "PHASE 12 +"}, {"Phase24N", "PHASE 24 -"}, {"Phase24P", "PHASE 24 +"},
    {"Phase36N", "PHASE 36 -"}, {"Phase36P", "PHASE 36 +"}, {"Phase48N", "PHASE 48 -"}, {"Phase48P", "PHASE 48 +"},
    {"Phase48H6P", "PHASE 48 H6 +"}, {"Phase48HL6P", "PHASE 48 HL6 +"}, {"Phase48HL6N", "PHASE 48 HL6 -"},
    {nullptr, "FORMANT"},
    {"FormantONE", "FORMANT I"}, {"FormantTWO", "FORMANT II"}, {"FormantTWB", "FORMANT III"},
    {nullptr, "OTHER"},
    {"Allpasses", "ALLPASSES"}, {"Diffuser", "DIFFUSER"}, {"DJMixer", "DJ MIXER"}, {"HEQ6", "HI-SHELF 6"},
    {"HEQ12", "HI-SHELF 12"}, {"PZ_SVF", "POLE-ZERO SVF"}, {"Exp", "EXPONENTIAL"}, {"ExpBPF", "EXP BAND"},
    {"Scream", "SCREAMER"}, {"Scream3LP", "SCREAM LOW"}, {"Scream3BP", "SCREAM BAND"}, {"Wsp", "WAVESHAPER"},
    {"RM", "RING MOD"}, {"RMT", "RING MOD T"}, {"SNH1", "SAMPLE HOLD"}, {"ADD_BASS", "ADD BASS"},
    {"ZDF_A", "ZDF ANALOG"}, {"DistComb1LP", "DIST COMB 1 LP"}, {"DistComb1BP", "DIST COMB 1 BP"},
    {"DistComb2LP", "DIST COMB 2 LP"}, {"DistComb2BP", "DIST COMB 2 BP"}, {"Reverb1", "FILTER REVERB"},
};

C biquadLow(double w, double q) { return C(1.0, 0.0) / C(1.0 - w * w, w / q); }
C biquadHigh(double w, double q) { return C(-w * w, 0.0) / C(1.0 - w * w, w / q); }
C biquadBand(double w, double q) { return C(0.0, w / q) / C(1.0 - w * w, w / q); }
C biquadNotch(double w, double q) { return C(1.0 - w * w, 0.0) / C(1.0 - w * w, w / q); }
C biquadPeak(double w, double q) { return C(1.0, 0.0) + 2.0 * biquadBand(w, q); }
C onePoleLow(double w) { return C(1.0, 0.0) / C(1.0, w); }
C onePoleHigh(double w) { return C(0.0, w) / C(1.0, w); }
C letter(char c, double w, double q) {
    switch (c) {
        case 'L': return biquadLow(w, q); case 'H': return biquadHigh(w, q); case 'B': return biquadBand(w, q);
        case 'N': return biquadNotch(w, q); default: return biquadPeak(w, q);
    }
}
double toDb(double mag) { return 20.0 * std::log10(std::max(mag, 1.0e-6)); }
}

const std::vector<FilterChoice>& filterChoices() {
    static const std::vector<FilterChoice> list = [] {
        std::vector<FilterChoice> v;
        for (const auto& e : kFilters) {
            if (!e.id) { v.push_back({FilterResponse::unknown, 0, e.name, nullptr}); continue; }
            FilterResponse r; int variant;
            if (dsp::filterFromSerumId(e.id, r, variant)) v.push_back({r, variant, e.name, e.id});
        }
        return v;
    }();
    return list;
}

juce::String filterDisplayName(const Filter& f) {
    for (const auto& c : filterChoices())
        if (c.serumId && f.type == c.serumId) return c.name;
    for (const auto& c : filterChoices())
        if (c.serumId && c.response == f.response && c.variant == f.variant) return c.name;
    return juce::String(f.type.empty() ? "LOW 12" : f.type).toUpperCase();
}

double filterMagnitudeDb(const Filter& f, double hz) {
    const double fc = 20.0 * std::pow(1000.0, std::clamp(f.cutoff, 0.0, 1.0));
    const double w = hz / fc;
    const double reso = std::clamp(f.resonance / 100.0, 0.0, 1.0);
    const double var = std::clamp(f.var / 100.0, 0.0, 1.0);
    const double q = 0.707 + 9.293 * reso;
    const double q2 = 0.707 + 0.35 * (q - 0.707);
    C h(1.0, 0.0);
    switch (f.response) {
        case FilterResponse::low6: h = onePoleLow(w); break;
        case FilterResponse::high6: h = onePoleHigh(w); break;
        case FilterResponse::low12: h = biquadLow(w, q); break;
        case FilterResponse::high12: h = biquadHigh(w, q); break;
        case FilterResponse::band12: h = biquadBand(w, q); break;
        case FilterResponse::notch12: h = biquadNotch(w, q); break;
        case FilterResponse::bandReject: h = biquadNotch(w, q * 1.5); break;
        case FilterResponse::low18: h = onePoleLow(w) * biquadLow(w, q); break;
        case FilterResponse::high18: h = onePoleHigh(w) * biquadHigh(w, q); break;
        case FilterResponse::low24: h = biquadLow(w, q) * biquadLow(w, q2); break;
        case FilterResponse::high24: h = biquadHigh(w, q) * biquadHigh(w, q2); break;
        case FilterResponse::band24: h = biquadBand(w, q) * biquadBand(w, q2); break;
        case FilterResponse::notch24: h = biquadNotch(w, q) * biquadNotch(w, q2); break;
        case FilterResponse::ladder6: case FilterResponse::ladder12: case FilterResponse::ladder18:
        case FilterResponse::ladder24: case FilterResponse::ladderDirty: case FilterResponse::ladderEms:
        case FilterResponse::ladderAcid: {
            int n = 4;
            if (f.response == FilterResponse::ladder6) n = 1;
            else if (f.response == FilterResponse::ladder12 || f.response == FilterResponse::ladderEms) n = 2;
            else if (f.response == FilterResponse::ladder18) n = 3;
            C a(1.0, 0.0);
            for (int i = 0; i < n; ++i) a *= onePoleLow(w);
            const double k = 3.7 * reso * (n == 4 ? 1.0 : 0.6);
            h = a / (C(1.0, 0.0) + k * a);
            break;
        }
        case FilterResponse::multi: {
            static const char* letters[20][3] = {
                {"L", "B", ""}, {"H", "B", ""}, {"L", "H", ""}, {"L", "N", ""}, {"H", "N", ""}, {"B", "N", ""},
                {"B", "P", ""}, {"H", "P", ""}, {"L", "B", "H"}, {"L", "N", "H"}, {"L", "B", "H"}, {"L", "N", "H"},
                {"L", "P", "H"}, {"B", "P", "N"}, {"B", "P", "N"}, {"P", "N", ""}, {"P", "P", ""}, {"N", "N", ""},
                {"P", "P", ""}, {"L", "P", "H"}};
            const auto& l = letters[std::clamp(f.variant, 0, 19)];
            const char a = l[0][0], b = l[1][0], c = l[2][0];
            if (a == b) {
                const double spread = std::exp2(0.02 + 2.5 * var);
                h = letter(a, w * spread, q) * letter(a, w / spread, q);
            } else if (c) {
                const double t = var * 2.0;
                h = t < 1.0 ? letter(a, w, q) * (1.0 - t) + letter(b, w, q) * t
                            : letter(b, w, q) * (2.0 - t) + letter(c, w, q) * (t - 1.0);
            } else h = letter(a, w, q) * (1.0 - var) + letter(b, w, q) * var;
            break;
        }
        case FilterResponse::comb: case FilterResponse::flange: case FilterResponse::distComb: {
            const double g = 0.1 + 0.88 * reso;
            const bool negative = f.variant & 1;
            const double theta = -2.0 * kPi * hz / fc;
            h = C(1.0, 0.0) + (negative ? -g : g) * std::polar(1.0, theta);
            h /= (1.0 + g * 0.6);
            break;
        }
        case FilterResponse::phaser: {
            const int stages = std::max(2, f.variant & 0xff);
            const double g = 0.2 + 0.75 * reso;
            const double theta = stages * 2.0 * std::atan(w);
            h = C(1.0, 0.0) + ((f.variant & 0x100) ? -g : g) * std::polar(1.0, -theta);
            h /= (1.0 + g * 0.6);
            break;
        }
        case FilterResponse::formant: {
            double lin = 0.0;
            static const double vow[3] = {800.0, 1150.0, 2900.0};
            for (double v : vow) {
                const double d = std::log2(hz / (v * fc / 1000.0));
                lin += std::exp(-d * d / 0.08) * (0.6 + 0.6 * reso);
            }
            h = C(0.03 + lin, 0.0);
            break;
        }
        case FilterResponse::shelfEq: h = C(0.35, 0.0) + C(0.65, 0.0) * onePoleHigh(w); break;
        case FilterResponse::allpass: case FilterResponse::diffuser: case FilterResponse::djMixer:
        case FilterResponse::filterReverb: h = C(1.0, 0.0); break;
        default: h = biquadLow(w, q); break;
    }
    return std::clamp(toDb(std::abs(h)), -60.0, 30.0);
}

int countRoutes(const Patch& p, ModSource kind, int index) {
    int n = 0;
    for (const auto& r : p.modulation)
        if (!r.bypass && r.sourceKind == kind && r.sourceIndex == index && r.targetKind != ModTarget::unknown) ++n;
    return n;
}

juce::String oscName(int i) {
    static const char* names[] = {"A", "B", "C", "NOISE", "SUB"};
    return names[std::clamp(i, 0, 4)];
}

juce::String routeLabel(const Route& r) {
    switch (r.target) {
        case RouteTarget::filter: return r.filterBalance < -50.0 ? "F1" : r.filterBalance > 50.0 ? "F2" : "F1+2";
        case RouteTarget::main: return "MAIN"; case RouteTarget::direct: return "DIRECT";
        case RouteTarget::none: return "OFF"; default: return "--";
    }
}

void buildRouteMenu(UiContext& ctx, juce::PopupMenu& menu, int routeIndex) {
    struct Item { const char* name; RouteTarget target; double balance; };
    static const Item items[] = {{"FILTER 1", RouteTarget::filter, -100.0}, {"FILTER 2", RouteTarget::filter, 100.0},
                                 {"FILTER 1 + 2", RouteTarget::filter, 0.0}, {"MAIN", RouteTarget::main, 0.0},
                                 {"DIRECT", RouteTarget::direct, 0.0}, {"OFF", RouteTarget::none, 0.0}};
    const auto& cur = ctx.patch->routes[std::size_t(routeIndex)];
    for (const auto& it : items) {
        const bool ticked = cur.target == it.target && (it.target != RouteTarget::filter || std::abs(cur.filterBalance - it.balance) < 1.0 ||
            (it.balance == 0.0 && std::abs(cur.filterBalance) < 50.0 && cur.filterBalance != -100.0 && cur.filterBalance != 100.0));
        menu.addItem(it.name, true, ticked, [&ctx, routeIndex, it] {
            ctx.editNow([routeIndex, it](Patch& p) {
                auto& r = p.routes[std::size_t(routeIndex)];
                r.target = it.target;
                if (it.target == RouteTarget::filter) {
                    r.filterBalance = it.balance;
                    if (it.balance <= 0.0) p.filters[0].enabled = true;
                    if (it.balance >= 0.0) p.filters[1].enabled = true;
                }
            });
        });
    }
}

namespace {
struct WarpItem { WarpMode mode; const char* name; };
constexpr WarpItem kBend[] = {{WarpMode::bendPositive, "BEND +"}, {WarpMode::bendNegative, "BEND -"}, {WarpMode::bendBoth, "BEND +/-"}};
constexpr WarpItem kAsym[] = {{WarpMode::asymPositive, "ASYM +"}, {WarpMode::asymNegative, "ASYM -"}, {WarpMode::asymBoth, "ASYM +/-"}};
constexpr WarpItem kShape[] = {{WarpMode::pwm, "PWM"}, {WarpMode::flip, "FLIP"}, {WarpMode::sync, "SYNC"}, {WarpMode::remap, "REMAP"},
                               {WarpMode::quantize, "QUANTIZE"}, {WarpMode::evenOdd, "EVEN/ODD"}, {WarpMode::selfPhase, "SELF PM"}};
constexpr WarpItem kDist[] = {{WarpMode::hardClip, "HARD CLIP"}, {WarpMode::softClip, "SOFT CLIP"}, {WarpMode::sineFold, "SINE FOLD"},
    {WarpMode::linearFold, "LINEAR FOLD"}, {WarpMode::sineShaper, "SINE SHAPER"}, {WarpMode::asymmetricClip, "ASYM CLIP"},
    {WarpMode::rectify, "RECTIFY"}, {WarpMode::diode1, "DIODE 1"}, {WarpMode::diode2, "DIODE 2"}, {WarpMode::softSat, "SOFT SAT"},
    {WarpMode::tapeSat, "TAPE SAT"}, {WarpMode::tube, "TUBE"}, {WarpMode::stompBox, "STOMP BOX"}, {WarpMode::zeroSquare, "ZERO SQUARE"},
    {WarpMode::filterLow, "FILTER LOW"}, {WarpMode::filterHigh, "FILTER HIGH"}};
constexpr WarpItem kMod[] = {{WarpMode::frequencyMod, "FM"}, {WarpMode::frequencyModX, "FM X"}, {WarpMode::frequencyModPhase, "FM PHASE"},
    {WarpMode::phaseMod, "PM"}, {WarpMode::ringMod, "RM"}, {WarpMode::amplitudeMod, "AM"}};
constexpr WarpItem kSpectral[] = {{WarpMode::addHarmonics, "ADD HARMONICS"}, {WarpMode::addSubharmonics, "ADD SUBHARMONICS"},
    {WarpMode::spectralDetune, "SPEC DETUNE"}, {WarpMode::gate, "GATE"}, {WarpMode::mirror, "MIRROR"}, {WarpMode::smear, "SMEAR"},
    {WarpMode::spectralComb, "SPEC COMB"}, {WarpMode::spectralPitchShift, "PITCH SHIFT"}, {WarpMode::spectralShift, "SHIFT"},
    {WarpMode::spread, "SPREAD"}, {WarpMode::phaseTwist, "PHASE TWIST"}, {WarpMode::vocode, "VOCODE"}, {WarpMode::mask, "MASK"}};
const char* sourceShort(int s) {
    static const char* n[] = {"A", "B", "C", "NOISE", "SUB", "F1", "F2"};
    return s >= 0 && s < 7 ? n[s] : "?";
}
}

juce::String warpName(const WarpDefinition& w) {
    auto lookup = [&](const WarpItem* list, std::size_t n) -> const char* {
        for (std::size_t i = 0; i < n; ++i) if (list[i].mode == w.mode) return list[i].name;
        return nullptr;
    };
    if (w.mode == WarpMode::off) return "OFF";
    for (auto [list, n] : {std::pair{kBend, std::size(kBend)}, {kAsym, std::size(kAsym)}, {kShape, std::size(kShape)},
                           {kDist, std::size(kDist)}, {kSpectral, std::size(kSpectral)}})
        if (const char* s = lookup(list, n)) return s;
    if (const char* s = lookup(kMod, std::size(kMod))) return juce::String(s) + (w.sourceIndex >= 0 ? " (" + juce::String(sourceShort(w.sourceIndex)) + ")" : "");
    return "WARP";
}

void buildWarpMenu(UiContext& ctx, juce::PopupMenu& menu, int osc, int slot) {
    auto apply = [&ctx, osc, slot](WarpMode mode, int source) {
        ctx.editNow([osc, slot, mode, source](Patch& p) {
            auto& o = p.oscillators[std::size_t(osc)];
            o.warpDefinitions[std::size_t(slot)].mode = mode;
            o.warpDefinitions[std::size_t(slot)].sourceIndex = source;
            (slot == 0 ? o.warpOne : o.warpTwo).clear();
        });
    };
    const auto current = ctx.patch->oscillators[std::size_t(osc)].warpDefinitions[std::size_t(slot)].mode;
    menu.addItem("OFF", true, current == WarpMode::off, [apply] { apply(WarpMode::off, -1); });
    auto add = [&](juce::PopupMenu& m, const WarpItem* list, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) { const auto mode = list[i].mode;
            m.addItem(list[i].name, true, current == mode, [apply, mode] { apply(mode, -1); }); }
    };
    juce::PopupMenu bend, asym, shape, dist, spectral;
    add(bend, kBend, std::size(kBend)); add(asym, kAsym, std::size(kAsym)); add(shape, kShape, std::size(kShape));
    add(dist, kDist, std::size(kDist)); add(spectral, kSpectral, std::size(kSpectral));
    menu.addSubMenu("BEND", bend); menu.addSubMenu("ASYM", asym); menu.addSubMenu("SHAPE", shape);
    for (const auto& it : kMod) {
        juce::PopupMenu src;
        for (int s = 0; s < 7; ++s) {
            if (s == osc) continue;
            const auto mode = it.mode;
            src.addItem(juce::String("FROM ") + sourceShort(s), true, current == mode, [apply, mode, s] { apply(mode, s); });
        }
        menu.addSubMenu(it.name, src);
    }
    menu.addSubMenu("DISTORT", dist);
    menu.addSubMenu("SPECTRAL", spectral);
}

juce::String prettyAssetName(const std::string& asset) {
    if (asset.empty()) return {};
    auto name = juce::File::createFileWithoutCheckingPath(juce::String(asset)).getFileNameWithoutExtension();
    return name.replaceCharacters("_", " ");
}

}
