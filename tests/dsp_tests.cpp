// DSP unit and integration tests for the ZYG synthesis engine (filters, FX, modulators,
// oscillators, matrix, voice handling, arp/clips, Serum adapter and native serialization).
#include "Assets.h"
#include "SerumImporter.h"
#include "SynthEngine.h"
#include "Wavetable.h"
#include "dsp/Filters.h"
#include "dsp/FxEngine.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"
#include "dsp/Sequencer.h"
#include "dsp/Spectral.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <new>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

using namespace zyg;

// Allocation counter used to prove the render path does not allocate.
static std::atomic<long> g_allocations {0};
static std::atomic<bool> g_countAllocations {false};
void* operator new(std::size_t n) { if (g_countAllocations.load(std::memory_order_relaxed)) g_allocations.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (g_countAllocations.load(std::memory_order_relaxed)) g_allocations.fetch_add(1); if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
struct Failure { std::string what; };
std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r; return r;
}
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); } };
#define TEST(name) static void test_##name(); static Registrar reg_##name(#name, test_##name); static void test_##name()
#define CHECK(cond) do { if (!(cond)) { std::ostringstream os; os << __FILE__ << ":" << __LINE__ << " CHECK(" #cond ")"; throw Failure{os.str()}; } } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) { std::ostringstream os; os << __FILE__ << ":" << __LINE__ << " " << msg; throw Failure{os.str()}; } } while (0)
#define CHECK_NEAR(a, b, tol) do { const double aa_ = (a), bb_ = (b); if (!(std::abs(aa_ - bb_) <= (tol))) { std::ostringstream os; os << __FILE__ << ":" << __LINE__ << " " #a "=" << aa_ << " vs " #b "=" << bb_ << " tol " << (tol); throw Failure{os.str()}; } } while (0)

constexpr double sr = 48000.0;

Patch sineTablePatch() {
    Patch p;
    p.name = "test";
    auto& o = p.oscillators[0];
    o.enabled = true; o.mode = OscMode::wavetable; o.volume = 1.0;
    o.audio.resize(2048);
    for (int i = 0; i < 2048; ++i) o.audio[std::size_t(i)] = float(std::sin(6.283185307179586 * i / 2048.0));
    prepareWavetableMipmaps(o);
    p.routes[0].target = RouteTarget::main;
    p.envelopes[0].attack = 0.0; p.envelopes[0].hold = 100.0; p.envelopes[0].sustain = 1.0;
    p.masterVolume = 1.0;
    return p;
}
struct Stereo { std::vector<float> l, r; };
Stereo render(Patch& p, int note, int samples, float velocity = 1.0f, double releaseAt = -1.0, bool limiter = false) {
    SynthEngine e; e.prepare(sr); e.setSafetyLimiter(limiter); e.setPatch(&p);
    Stereo out; out.l.assign(std::size_t(samples), 0.0f); out.r.assign(std::size_t(samples), 0.0f);
    e.noteOn(1, note, velocity);
    bool released = false;
    for (int at = 0; at < samples; at += 256) {
        const int n = std::min(256, samples - at);
        if (releaseAt >= 0.0 && !released && at >= int(releaseAt * sr)) { e.noteOff(1, note); released = true; }
        e.render(out.l.data(), out.r.data(), at, n);
    }
    return out;
}
double rms(const std::vector<float>& v, std::size_t from = 0, std::size_t to = 0) {
    if (!to || to > v.size()) to = v.size();
    double s = 0; for (std::size_t i = from; i < to; ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / std::max<std::size_t>(1, to - from));
}
double peak(const std::vector<float>& v) { double p = 0; for (float x : v) p = std::max(p, double(std::abs(x))); return p; }
bool finite(const std::vector<float>& v) { for (float x : v) if (!std::isfinite(x)) return false; return true; }
// Frequency estimate from rising zero crossings over a window.
// Peak frequency via a Hann-windowed DFT with parabolic interpolation (robust for noisy signals).
double peakHzDft(const std::vector<float>& v, std::size_t from, double lo, double hi) {
    const int n = 8192; std::vector<double> w(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) w[static_cast<std::size_t>(i)] = double(v[from + static_cast<std::size_t>(i)]) * (0.5 - 0.5 * std::cos(6.283185307 * i / n));
    const int k0 = int(lo * n / sr), k1 = int(hi * n / sr);
    std::vector<double> mag(static_cast<std::size_t>(k1 + 2));
    for (int k = std::max(1, k0 - 1); k <= k1 + 1; ++k) {
        double re = 0, im = 0; for (int i = 0; i < n; ++i) { const double a = 6.283185307 * k * i / n; re += w[static_cast<std::size_t>(i)] * std::cos(a); im -= w[static_cast<std::size_t>(i)] * std::sin(a); }
        mag[static_cast<std::size_t>(k)] = std::hypot(re, im);
    }
    int best = k0; for (int k = k0; k <= k1; ++k) if (mag[static_cast<std::size_t>(k)] > mag[static_cast<std::size_t>(best)]) best = k;
    const double a = mag[static_cast<std::size_t>(best - 1)], b = mag[static_cast<std::size_t>(best)], c = mag[static_cast<std::size_t>(best + 1)];
    const double d = (a - c) / (2.0 * (a - 2.0 * b + c) + 1e-30);
    return (best + d) * sr / n;
}
double estimateHz(const std::vector<float>& v, std::size_t from, std::size_t to) {
    int crossings = 0; double first = -1, last = -1;
    for (std::size_t i = from + 1; i < to; ++i)
        if (v[i - 1] <= 0.0f && v[i] > 0.0f) {
            const double frac = double(-v[i - 1]) / double(v[i] - v[i - 1]);
            const double t = double(i - 1) + frac;
            if (first < 0) first = t; last = t; ++crossings;
        }
    if (crossings < 3) return 0.0;
    return (crossings - 1) * sr / (last - first);
}
std::vector<double> noise(int n, std::uint32_t seed = 7) {
    dsp::Rng rng(seed); std::vector<double> v(static_cast<std::size_t>(n)); for (auto& x : v) x = 0.5 * rng.bipolar(); return v;
}
double sineGain(dsp::FilterCore& f, double hz, int n = 24000, double amp = 1.0) {
    double e = 0; f.reset();
    for (int i = 0; i < n; ++i) {
        const double x = amp * std::sin(6.283185307179586 * hz * i / sr);
        double l = x, r = x; f.process(l, r);
        if (i > n / 2) e += l * l;
    }
    return std::sqrt(e / (n - n / 2 - 1) * 2.0) / amp;
}
}

// ============================================================================ filters
TEST(filter_id_table_covers_every_serum_id) {
    const char* ids[] = {"ADD_BASS", "Allpasses", "B12", "B24", "BN12", "BP12", "BPN12", "BPN24", "BandReject", "Comb2", "CombH6N",
        "CombH6P", "CombHL6N", "CombHL6P", "CombL6N", "CombN", "CombP", "Combs", "DJMixer", "Diffuser", "DirtyMg", "DistComb1BP",
        "DistComb1LP", "DistComb2BP", "DistComb2LP", "Exp", "ExpBPF", "FlangeH6P", "FlangeHL6N", "FlangeHL6P", "FlangeL6P", "FlangeN",
        "FlangeP", "FlangePhase12HL6P", "FormantONE", "FormantTWB", "FormantTWO", "H12", "H18", "H24", "H6", "HB12", "HEQ12", "HN12",
        "HP12", "L12", "L18", "L24", "L6", "LB12", "LBH12", "LBH24", "LH12", "LN12", "LNH12", "LNH24", "LPH24", "LadderAcid", "LadderEMS",
        "LadderMg", "MgL18", "MgL24", "MgL6", "N12", "N24", "NN12", "PP12", "PZ_SVF", "Phase24N", "Phase24P", "Phase36N", "Phase36P",
        "Phase48H6P", "Phase48HL6P", "Phase48N", "Phase48P", "RM", "RMT", "Reverb1", "Scream", "Scream3LP", "Wsp", "ZDF_A",
        // FX-filter-only identifiers
        "HEQ6", "LPH12", "MgL12", "P12", "PN12", "Phase12N", "Phase12P", "SNH1", "Scream3BP", "FlangeL6N"};
    for (const char* id : ids) {
        FilterResponse r = FilterResponse::unknown; int v = 0;
        CHECK_MSG(dsp::filterFromSerumId(id, r, v), "unmapped filter id " << id);
        CHECK(r != FilterResponse::unknown);
    }
}
TEST(filter_slopes_and_shapes) {
    dsp::FilterCore f; f.prepare(sr);
    auto setup = [&](FilterResponse t, double hz, double reso = 0.0) {
        dsp::FilterParams p; p.type = t; p.cutoffHzL = p.cutoffHzR = hz; p.reso = reso; f.update(p);
    };
    setup(FilterResponse::low12, 1000.0);
    CHECK_NEAR(sineGain(f, 100.0), 1.0, 0.05);
    CHECK(sineGain(f, 8000.0) < 0.03);
    setup(FilterResponse::high12, 1000.0);
    CHECK(sineGain(f, 100.0) < 0.03);
    CHECK_NEAR(sineGain(f, 8000.0), 1.0, 0.06);
    setup(FilterResponse::band12, 1000.0, 0.5);
    const double bp = sineGain(f, 1000.0);
    CHECK(bp > sineGain(f, 200.0) * 2.0 && bp > sineGain(f, 5000.0) * 2.0);
    setup(FilterResponse::notch12, 1000.0, 0.5);
    CHECK(sineGain(f, 1000.0) < 0.2 && sineGain(f, 150.0) > 0.8);
    // slope ordering
    double prev = 2.0;
    for (auto t : {FilterResponse::low6, FilterResponse::low12, FilterResponse::low18, FilterResponse::low24}) {
        setup(t, 500.0); const double g = sineGain(f, 2000.0); CHECK(g < prev); prev = g;
    }
    // ladder responses attenuate above cutoff and resonate at it
    setup(FilterResponse::ladder24, 1000.0, 0.0); const double lad = sineGain(f, 8000.0); CHECK(lad < 0.05);
    setup(FilterResponse::ladder24, 1000.0, 0.9); const double lr = sineGain(f, 900.0, 24000, 0.02); CHECK_MSG(lr > 1.5, "ladder resonant gain " << lr);
}
TEST(filter_families_finite_and_bounded_at_extremes) {
    const FilterResponse all[] = {FilterResponse::low6, FilterResponse::low24, FilterResponse::high18, FilterResponse::band24,
        FilterResponse::notch24, FilterResponse::ladder6, FilterResponse::ladder12, FilterResponse::ladder24, FilterResponse::ladderDirty,
        FilterResponse::ladderEms, FilterResponse::ladderAcid, FilterResponse::multi, FilterResponse::comb, FilterResponse::flange,
        FilterResponse::phaser, FilterResponse::formant, FilterResponse::allpass, FilterResponse::diffuser, FilterResponse::djMixer,
        FilterResponse::shelfEq, FilterResponse::polezero, FilterResponse::exponential, FilterResponse::screamer, FilterResponse::waveshaper,
        FilterResponse::ringMod, FilterResponse::sampleHold, FilterResponse::addBass, FilterResponse::zdfAnalog, FilterResponse::distComb,
        FilterResponse::filterReverb, FilterResponse::bandReject};
    const auto in = noise(24000);
    for (auto type : all) {
        for (int variant : {0, 1, 5, 8}) {
            dsp::FilterCore f; f.prepare(sr);
            for (double hz : {25.0, 800.0, 18000.0}) for (double reso : {0.0, 1.0}) {
                dsp::FilterParams p; p.type = type; p.variant = variant; p.cutoffHzL = hz; p.cutoffHzR = hz * 1.1; p.reso = reso;
                p.drive = 1.0; p.var = 1.0; p.x = 0.2; p.y = 0.9;
                f.update(p); f.reset();
                double mx = 0;
                for (int i = 0; i < 24000; ++i) {
                    if ((i & 7) == 0) f.update(p);
                    double l = in[std::size_t(i)], r = -in[std::size_t(i)]; f.process(l, r);
                    CHECK_MSG(std::isfinite(l) && std::isfinite(r), "non-finite output type " << int(type) << " variant " << variant);
                    mx = std::max(mx, std::abs(l));
                }
                CHECK_MSG(mx < 60.0, "unbounded filter type " << int(type) << " variant " << variant << " peak " << mx);
            }
        }
    }
}
TEST(filter_multi_morph_moves_between_modes) {
    dsp::FilterCore f; f.prepare(sr);
    int variant = 0; FilterResponse r; CHECK(dsp::filterFromSerumId("LBH12", r, variant));
    auto gains = [&](double var) {
        dsp::FilterParams p; p.type = r; p.variant = variant; p.cutoffHzL = p.cutoffHzR = 2000.0; p.reso = 0.1; p.var = var; f.update(p);
        return std::array<double, 2>{sineGain(f, 150.0), sineGain(f, 12000.0)};
    };
    const auto low = gains(0.0), high = gains(1.0);
    CHECK(low[0] > low[1] * 5.0);     // low-pass end
    CHECK(high[1] > high[0] * 5.0);   // high-pass end
}
TEST(filter_comb_places_resonances_at_multiples) {
    dsp::FilterCore f; f.prepare(sr);
    int variant = 0; FilterResponse r; CHECK(dsp::filterFromSerumId("CombP", r, variant));
    dsp::FilterParams p; p.type = r; p.variant = variant; p.cutoffHzL = p.cutoffHzR = 400.0; p.reso = 0.8; f.update(p);
    const double onPeak = sineGain(f, 400.0), offPeak = sineGain(f, 600.0);
    CHECK(onPeak > offPeak * 2.0);
}
TEST(filter_formant_has_different_spectra_per_vowel) {
    dsp::FilterCore f; f.prepare(sr);
    int variant = 0; FilterResponse r; CHECK(dsp::filterFromSerumId("FormantONE", r, variant));
    dsp::FilterParams p; p.type = r; p.variant = variant; p.cutoffHzL = p.cutoffHzR = 1000.0; p.reso = 0.5;
    p.var = 0.0; f.update(p); const double a0 = sineGain(f, 800.0), a1 = sineGain(f, 300.0);
    p.var = 0.5; f.update(p); const double b0 = sineGain(f, 800.0), b1 = sineGain(f, 300.0);
    CHECK(a0 > a1 && b1 > b0 * 0.5 && std::abs(a0 - b0) > 0.05);
}

// ================================================================================ FX
namespace {
FxModule makeFx(FxType t) { FxModule m; m.fxType = t; m.type = fxTypeName(t); return m; }
void effective(const FxModule& m, double* p) {
    const auto table = fxParamTable(m.fxType);
    for (int i = 0; i < fxParamSlots; ++i) p[i] = std::size_t(i) < table.size() ? (m.set[std::size_t(i)] ? m.p[std::size_t(i)] : table[std::size_t(i)].def) : 0.0;
    p[fxLevelSlot] = 0.5;
}
void setParam(FxModule& m, std::size_t slot, double v) { m.p[slot] = v; m.set[slot] = true; }
struct FxRun { std::vector<double> l, r; };
FxRun runFx(FxModule& m, const std::vector<double>& in, bool stereoDifferent = false, double* override_ = nullptr) {
    auto unit = dsp::makeFxUnit(m.fxType); unit->prepare(sr); unit->reset();
    double p[fxParamSlots]; effective(m, p);
    if (override_) for (int i = 0; i < fxParamSlots; ++i) if (override_[i] > -1.0e8) p[i] = override_[i];
    FxRun out; out.l = in; out.r = in;
    if (stereoDifferent) for (std::size_t i = 0; i < in.size(); ++i) out.r[i] = -in[i];
    for (std::size_t at = 0; at < in.size(); at += 200) {
        const int n = int(std::min<std::size_t>(200, in.size() - at));
        unit->process(m, p, out.l.data() + at, out.r.data() + at, n);
    }
    return out;
}
double rmsd(const std::vector<double>& v, std::size_t from = 0) { double s = 0; for (std::size_t i = from; i < v.size(); ++i) s += v[i] * v[i]; return std::sqrt(s / std::max<std::size_t>(1, v.size() - from)); }
}
TEST(fx_units_are_finite_and_level_sane_at_default_settings) {
    const FxType types[] = {FxType::bode, FxType::chorus, FxType::comp, FxType::delay, FxType::distortion, FxType::eq, FxType::filter,
                            FxType::flanger, FxType::hyperD, FxType::phaser, FxType::reverb, FxType::utils};
    const auto in = noise(48000);
    for (auto t : types) {
        auto m = makeFx(t);
        for (int wetOn = 0; wetOn < 2; ++wetOn) {
            auto out = runFx(m, in, wetOn == 1);
            for (double v : out.l) CHECK_MSG(std::isfinite(v), "non-finite FX " << fxTypeName(t));
            const double g = rmsd(out.l, 24000) / rmsd(in, 24000);
            CHECK_MSG(g > 0.03 && g < 12.0, "FX " << fxTypeName(t) << " gain " << g);
        }
    }
}
TEST(fx_delay_echo_arrives_at_the_set_time) {
    auto m = makeFx(FxType::delay);
    setParam(m, fx::dTimeL, 0.1); setParam(m, fx::dTimeR, 0.1); setParam(m, fx::dWet, 100); setParam(m, fx::dFeedback, 0);
    setParam(m, fx::dFreq, 18000); setParam(m, fx::dBW, 8.25);
    std::vector<double> in(24000, 0.0); in[0] = 1.0;
    auto out = runFx(m, in);
    std::size_t best = 0; double bv = 0;
    for (std::size_t i = 200; i < out.l.size(); ++i) if (std::abs(out.l[i]) > bv) { bv = std::abs(out.l[i]); best = i; }
    CHECK_MSG(std::abs(double(best) - 4800.0) < 40.0, "echo at " << best);
}
TEST(fx_reverb_has_a_decaying_tail_and_wet_zero_is_dry) {
    auto m = makeFx(FxType::reverb);
    setParam(m, fx::rWet, 100); setParam(m, fx::rSize, 50); setParam(m, fx::rFeedback, 60);
    std::vector<double> in(96000, 0.0); for (int i = 0; i < 2000; ++i) in[std::size_t(i)] = 0.5 * std::sin(i * 0.2);
    auto out = runFx(m, in);
    const double early = rmsd(std::vector<double>(out.l.begin() + 4000, out.l.begin() + 12000));
    const double late = rmsd(std::vector<double>(out.l.begin() + 60000, out.l.begin() + 68000));
    CHECK(early > 1.0e-4 && late < early && late > 1.0e-7);
    setParam(m, fx::rWet, 0);
    auto dry = runFx(m, in);
    for (int i = 0; i < 2000; ++i) CHECK_NEAR(dry.l[std::size_t(i)], in[std::size_t(i)], 1.0e-9);
}
TEST(fx_utils_width_polarity_and_lf_mono) {
    auto m = makeFx(FxType::utils);
    std::vector<double> in(9600); for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.4 * std::sin(i * 0.1);
    setParam(m, fx::uWidth, 0);
    auto mono = runFx(m, in, true);        // right is -left: pure side signal
    CHECK(rmsd(mono.l, 2000) < 1.0e-6);    // width 0 removes the side
    setParam(m, fx::uWidth, 100); setParam(m, fx::uPolarityL, 1);
    auto flipped = runFx(m, in, false);
    CHECK_NEAR(flipped.l[5000] * -1.0, in[5000], 1.0e-9);
    CHECK_NEAR(flipped.r[5000], in[5000], 1.0e-9);
}
TEST(fx_eq_boosts_the_selected_band) {
    auto m = makeFx(FxType::eq);
    setParam(m, fx::eType1, 1); setParam(m, fx::eFreq1, 1000); setParam(m, fx::eGain1, 12); setParam(m, fx::eReso1, 50);
    std::vector<double> in(24000); for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.1 * std::sin(6.283185307 * 1000.0 * double(i) / sr);
    auto out = runFx(m, in);
    CHECK_NEAR(rmsd(out.l, 12000) / rmsd(in, 12000), std::pow(10.0, 12.0 / 20.0), 0.6);
}
TEST(fx_compressor_reduces_peaks_without_changing_program_level_much) {
    auto m = makeFx(FxType::comp);
    setParam(m, fx::pThresh, 0.1); setParam(m, fx::pRatio, 8); setParam(m, fx::pAttack, 1); setParam(m, fx::pRelease, 50); setParam(m, fx::pWet, 100);
    std::vector<double> in(96000); for (std::size_t i = 0; i < in.size(); ++i) in[i] = (i / 4800 % 2 ? 0.8 : 0.05) * std::sin(i * 0.05);
    auto out = runFx(m, in);
    const double loudIn = rmsd(std::vector<double>(in.begin() + 4800 * 3, in.begin() + 4800 * 3 + 2000));
    CHECK(std::isfinite(out.l[50000]) && rmsd(out.l, 24000) < rmsd(in, 24000) * 3.0 && rmsd(out.l, 24000) > rmsd(in, 24000) * 0.2);
    (void) loudIn;
}
TEST(fx_convolution_reproduces_an_impulse_response) {
    auto m = makeFx(FxType::conv);
    auto ir = std::make_shared<SampleData>(); ir->sampleRate = sr; ir->left.assign(8192, 0.0f); ir->left[100] = 1.0f; ir->left[900] = 0.5f;
    m.convIr = buildConvIr(*ir);
    setParam(m, fx::vWet, 100); setParam(m, fx::vIpTrim, 0); setParam(m, fx::vDecay, 40); setParam(m, fx::vDamping, 100);
    std::vector<double> in(16384, 0.0); in[0] = 1.0;
    auto out = runFx(m, in);
    // unit-energy IR: the direct spike at 100 (+512 block latency) carries 1/sqrt(1.25) of the energy
    const double expectFirst = 1.0 / std::sqrt(1.25);
    std::size_t peakAt = 0; double pv = 0;
    for (std::size_t i = 0; i < out.l.size(); ++i) if (std::abs(out.l[i]) > pv) { pv = std::abs(out.l[i]); peakAt = i; }
    CHECK_MSG(peakAt == 612, "conv peak at " << peakAt);
    CHECK_NEAR(pv, expectFirst, 0.05);
    CHECK_NEAR(out.l[612 + 800] / pv, 0.5, 0.06);
}
TEST(fx_splitters_route_branches_and_sum_back) {
    Patch p = sineTablePatch();
    p.envelopes[0].hold = 100;
    // Split at 1 kHz; the high branch gets a utils module with width 0 (no change to mono), low branch untouched.
    FxModule split = makeFx(FxType::split); split.rack = 0; split.position = 0; setParam(split, fx::sFreq, 1000); setParam(split, fx::sCount2, 1);
    FxModule util = makeFx(FxType::utils); util.rack = 0; util.position = 1; setParam(util, fx::uPolarityL, 1); setParam(util, fx::uPolarityR, 1);
    p.fx = {split, util};
    auto lowNote = render(p, 45, 24000);        // 110 Hz -> low branch, untouched
    auto highNote = render(p, 108, 24000);      // 4.2 kHz -> high branch, polarity flipped
    Patch q = p; q.fx[1].enabled = false;       // same split, branch module bypassed
    auto lowRef = render(q, 45, 24000), highRef = render(q, 108, 24000);
    CHECK_NEAR(rms(lowNote.l, 12000), rms(lowRef.l, 12000), 0.02);
    double corr = 0, ea = 0, eb = 0;
    for (std::size_t i = 12000; i < 24000; ++i) { corr += double(highNote.l[i]) * highRef.l[i]; ea += double(highNote.l[i]) * highNote.l[i]; eb += double(highRef.l[i]) * highRef.l[i]; }
    CHECK(corr / std::sqrt(ea * eb) < -0.9);      // the high band is inverted by its branch module
    // with no branch content the split is transparent in level
    Patch t = sineTablePatch(); t.fx = {split};
    t.fx[0].p[fx::sCount2] = 0; Patch clean = sineTablePatch(); auto plain = render(t, 69, 24000), ref = render(clean, 69, 24000);
    CHECK_NEAR(rms(plain.l, 12000), rms(ref.l, 12000), 0.03);
}

// ======================================================================== modulators
TEST(envelope_stages_follow_their_times) {
    dsp::EnvelopeGen e; dsp::EnvParams p; p.attack = 0.1; p.hold = 0.05; p.decay = 0.2; p.sustain = 0.5; p.release = 0.1;
    p.curve1 = 50; p.curve2 = 50; p.curve3 = 50;
    e.noteOn();
    double v = 0; const double dt = 0.001;
    for (int i = 0; i < 50; ++i) v = e.advance(p, dt); CHECK_NEAR(v, 0.5, 0.03);
    for (int i = 0; i < 60; ++i) v = e.advance(p, dt); CHECK_NEAR(v, 1.0, 0.001);  // hold
    for (int i = 0; i < 140; ++i) v = e.advance(p, dt); CHECK_NEAR(v, 0.75, 0.03);   // mid-decay
    for (int i = 0; i < 160; ++i) v = e.advance(p, dt); CHECK_NEAR(v, 0.5, 0.001);
    e.noteOff();
    for (int i = 0; i < 50; ++i) v = e.advance(p, dt); CHECK_NEAR(v, 0.25, 0.03);
    for (int i = 0; i < 60; ++i) v = e.advance(p, dt); CHECK(!e.active() && v < 1e-6);
}
TEST(lfo_curve_shapes_follow_stored_points) {
    LfoDefinition d; d.shape = LfoShape::curve;
    d.path.closed = true; d.path.x = {0.0, 0.5, 1.0}; d.path.y = {0.0, 1.0, 0.0}; d.path.bend = {0.5, 0.5, 0.5}; // triangle in value space
    CHECK_NEAR(dsp::evalPath(d.path, 0.0), -1.0, 1e-9);
    CHECK_NEAR(dsp::evalPath(d.path, 0.25), 0.0, 1e-9);
    CHECK_NEAR(dsp::evalPath(d.path, 0.5), 1.0, 1e-9);
    CHECK_NEAR(dsp::evalPath(d.path, 0.75), 0.0, 1e-9);
    // a vertical edge (saw up): value ramps 0 -> 1 then jumps back
    d.path.x = {0.0, 1.0, 1.0}; d.path.y = {0.0, 1.0, 0.0}; d.path.bend = {0.5, 0.5, 0.5};
    CHECK_NEAR(dsp::evalPath(d.path, 0.5), 0.0, 1e-9);
    CHECK(dsp::evalPath(d.path, 0.99) > 0.9);
}
TEST(lfo_rate_delay_rise_and_smooth) {
    LfoDefinition d; d.shape = LfoShape::sine; d.rateHz = 4.0;
    dsp::LfoState s; s.reset(1, 0.0);
    double peakV = 0; int rising = 0;
    for (int i = 0; i < 48000; ++i) { const double v = dsp::advanceLfo(s, d, 4.0, 1.0 / sr, 0.0, 0.0, 0.0, 0.0); peakV = std::max(peakV, v); if (v > 0.99) ++rising; }
    CHECK_NEAR(peakV, 1.0, 0.01);
    s.reset(1, 0.0); double early = 0;
    for (int i = 0; i < 4800; ++i) early = std::max(early, std::abs(dsp::advanceLfo(s, d, 4.0, 1.0 / sr, 0.0, 0.0, 1.0, 0.0)));
    CHECK(early < 0.2);   // rise = 1 s fades the LFO in
    s.reset(1, 0.0); double delayed = 0;
    for (int i = 0; i < 4800; ++i) delayed = std::max(delayed, std::abs(dsp::advanceLfo(s, d, 4.0, 1.0 / sr, 0.0, 0.0, 0.0, 0.5)));
    CHECK(delayed < 1e-9);
    s.reset(1, 0.0); double smoothPeak = 0;
    for (int i = 0; i < 24000; ++i) smoothPeak = std::max(smoothPeak, std::abs(dsp::advanceLfo(s, d, 4.0, 1.0 / sr, 0.0, 100.0, 0.0, 0.0)));
    CHECK(smoothPeak < 0.7);
}
TEST(lfo_chaos_and_random_are_bounded_and_deterministic) {
    for (auto shape : {LfoShape::lorenz, LfoShape::rossler, LfoShape::randomHold}) {
        LfoDefinition d; d.shape = shape; d.rateHz = 6.0;
        dsp::LfoState a, b; a.reset(5, 0.0); b.reset(5, 0.0);
        double mx = 0;
        for (int i = 0; i < 96000; ++i) {
            const double va = dsp::advanceLfo(a, d, 6.0, 8.0 / sr, 0, 0, 0, 0), vb = dsp::advanceLfo(b, d, 6.0, 8.0 / sr, 0, 0, 0, 0);
            CHECK(va == vb && std::isfinite(va) && std::abs(va) <= 1.0001); mx = std::max(mx, std::abs(va));
        }
        CHECK(mx > 0.3);
    }
}
TEST(lfo_point_bus_moves_a_point_property) {
    LfoDefinition d; d.shape = LfoShape::curve; d.path.closed = true;
    d.path.x = {0.0, 0.5, 1.0}; d.path.y = {0.0, 0.5, 0.0}; d.path.bend = {0.5, 0.5, 0.5};
    d.pointMods.push_back({1, 3, 1});     // bus 3 drives the y position of point 1
    dsp::LfoState s; s.reset(1, 0.25);
    double busValues[16] = {}; busValues[3] = 0.5;
    const double raised = dsp::advanceLfo(s, d, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, busValues);
    s.reset(1, 0.25);
    const double plain = dsp::advanceLfo(s, d, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, nullptr);
    CHECK(raised > plain + 0.4);          // the peak point is lifted while modulated
}
TEST(lfo_tempo_sync_uses_beats) {
    LfoDefinition d; d.tempoSync = true; d.syncBeats = 1.0;
    CHECK_NEAR(dsp::lfoRateHz(d, 120.0), 2.0, 1e-9);
    d.syncBeats = 4.0; CHECK_NEAR(dsp::lfoRateHz(d, 120.0), 0.5, 1e-9);
}

// ==================================================================== oscillators
TEST(unison_voices_are_spread_symmetrically_with_blend_and_stereo) {
    Patch p = sineTablePatch();
    auto& o = p.oscillators[0]; o.unison = 7; o.detune = 0.6; o.blend = 100; o.unisonStereo = 100;
    p.envelopes[0].attack = 0;
    auto out = render(p, 60, 24000);
    CHECK(rms(out.l, 4000) > 0.1 && rms(out.r, 4000) > 0.1);
    double diff = 0; for (std::size_t i = 4000; i < 24000; ++i) diff += std::abs(out.l[i] - out.r[i]);
    CHECK(diff > 1.0);                    // stereo spread
    o.unisonStereo = 0;
    auto mono = render(p, 60, 24000);
    double d2 = 0; for (std::size_t i = 4000; i < 24000; ++i) d2 += std::abs(mono.l[i] - mono.r[i]);
    CHECK(d2 < 1e-3);
    // level stays close to a single voice thanks to power normalisation
    o.unison = 1; auto single = render(p, 60, 24000);
    CHECK(rms(mono.l, 4000) < rms(single.l, 4000) * 1.6 && rms(mono.l, 4000) > rms(single.l, 4000) * 0.4);
}
TEST(zero_blend_still_sounds_for_even_unison_counts) {
    for (int count : {2, 3, 4, 8}) {
        Patch p = sineTablePatch(); auto& o = p.oscillators[0]; o.unison = count; o.blend = 0.0; o.detune = 0.3;
        auto r = render(p, 60, 12000);
        CHECK_MSG(rms(r.l, 4000) > 0.05, "unison " << count << " at blend 0 is silent");
    }
}
TEST(detune_modes_produce_distinct_stacks) {
    Patch p = sineTablePatch(); auto& o = p.oscillators[0]; o.unison = 5; o.detune = 0.5; o.unisonStereo = 0;
    std::vector<std::vector<float>> renders;
    for (auto mode : {DetuneMode::linear, DetuneMode::exponential, DetuneMode::inverse, DetuneMode::random, DetuneMode::super}) {
        o.detuneMode = mode; renders.push_back(render(p, 60, 12000).l);
    }
    for (std::size_t a = 0; a < renders.size(); ++a) for (std::size_t b = a + 1; b < renders.size(); ++b) {
        double d = 0; for (std::size_t i = 6000; i < 12000; ++i) d += std::abs(renders[a][i] - renders[b][i]);
        CHECK_MSG(d > 1.0, "detune modes " << a << " and " << b << " render identically");
    }
    o.detuneMode = DetuneMode::linear; o.unisonStack = UnisonStack::octave1; auto stacked = render(p, 60, 12000).l;
    double d = 0; for (std::size_t i = 6000; i < 12000; ++i) d += std::abs(stacked[i] - renders[0][i]);
    CHECK(d > 1.0);
}
TEST(pitch_controls_octave_coarse_fine_pitchmode) {
    Patch p = sineTablePatch(); auto& o = p.oscillators[0];
    auto hz = [&](int note) { auto r = render(p, note, 24000); return estimateHz(r.l, 6000, 24000); };
    CHECK_NEAR(hz(69), 440.0, 1.5);
    o.octave = 1; CHECK_NEAR(hz(69), 880.0, 3.0); o.octave = 0;
    o.semitone = 7; CHECK_NEAR(hz(69), 440.0 * std::exp2(7.0 / 12.0), 2.0); o.semitone = 0;
    o.fine = 100; CHECK_NEAR(hz(69), 440.0 * std::exp2(1.0 / 12.0), 2.0); o.fine = 0;
    o.pitchMode = OscPitchMode::ratio; o.pitchRatio = 3.0; CHECK_NEAR(hz(69), 1320.0, 6.0); o.pitchMode = OscPitchMode::semitones;
    o.pitchTrack = false; CHECK_NEAR(hz(81), hz(48), 1.0); o.pitchTrack = true;
    o.hzOffset = 100; CHECK_NEAR(hz(69), 540.0, 2.5);
}
TEST(wavetable_position_selects_frames_and_unison_position_spread_changes_sound) {
    Patch p = sineTablePatch(); auto& o = p.oscillators[0];
    o.audio.assign(2048 * 2, 0.0f);
    for (int i = 0; i < 2048; ++i) { o.audio[std::size_t(i)] = float(std::sin(6.283185307 * i / 2048.0)); o.audio[std::size_t(2048 + i)] = float(std::sin(6.283185307 * 3 * i / 2048.0)); }
    prepareWavetableMipmaps(o);
    o.tablePosition = 0; auto a = render(p, 57, 24000); o.tablePosition = 256; auto b = render(p, 57, 24000);
    CHECK_NEAR(estimateHz(b.l, 6000, 24000) / estimateHz(a.l, 6000, 24000), 3.0, 0.05);
}
TEST(sample_oscillator_plays_at_root_and_transposes) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[0].target = RouteTarget::main;
    auto s = std::make_shared<SampleData>(); s->sampleRate = 44100; s->rootNote = 69; s->left.resize(88200);
    for (std::size_t i = 0; i < s->left.size(); ++i) s->left[i] = 0.5f * std::sin(6.283185307 * 440.0 * double(i) / 44100.0);
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = OscMode::sample; o.sample = s; o.baseNote = 69; o.volume = 1.0; o.looping = true;
    auto r0 = render(p, 69, 48000); CHECK_NEAR(rms(r0.l, 6000), 0.3536, 0.01); CHECK_NEAR(estimateHz(r0.l, 6000, 48000), 440.0, 2.0);
    auto r1 = render(p, 81, 48000); CHECK_NEAR(estimateHz(r1.l, 6000, 48000), 880.0, 4.0);
    // one-shot (no loop) stops at the end of the sample
    o.looping = false; o.end = 100;
    auto shot = render(p, 69, 48000 * 3); CHECK(rms(shot.l, 48000 * 2 + 4000) < 1e-4);
    // reverse plays backwards: same pitch
    o.looping = true; o.reverse = true; auto rev = render(p, 69, 48000); CHECK_NEAR(estimateHz(rev.l, 6000, 48000), 440.0, 3.0);
}
TEST(sample_slicing_maps_keys_to_slices) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[0].target = RouteTarget::main;
    auto s = std::make_shared<SampleData>(); s->sampleRate = sr; s->rootNote = 60; s->left.resize(48000);
    for (std::size_t i = 0; i < s->left.size(); ++i) s->left[i] = 0.5f * std::sin(6.283185307 * (i < 24000 ? 300.0 : 900.0) * double(i) / sr);
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = OscMode::sample; o.sample = s; o.volume = 1.0; o.slicingMode = 2; o.sliceMarkers = {0.0, 0.5};
    auto a = render(p, 36, 12000); CHECK_NEAR(peakHzDft(a.l, 2000, 200.0, 500.0), 300.0, 10.0);
    auto b = render(p, 37, 12000); CHECK_NEAR(peakHzDft(b.l, 2000, 700.0, 1100.0), 900.0, 20.0);
    CHECK(rms(render(p, 50, 6000).l, 1000) < 1e-6);   // no slice on that key
    auto detected = detectSliceMarkers(*s, 0.25); CHECK(!detected.empty());
}
TEST(multisample_selects_regions_by_key_and_velocity) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[0].target = RouteTarget::main;
    auto mk = [](double hz) { auto s = std::make_shared<SampleData>(); s->sampleRate = sr; s->left.resize(48000); for (std::size_t i = 0; i < s->left.size(); ++i) s->left[i] = 0.5f * std::sin(6.283185307 * hz * double(i) / sr); return SamplePtr(s); };
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = OscMode::multisample; o.volume = 1.0;
    SampleRegion low; low.sample = mk(220.0); low.loKey = 0; low.hiKey = 59; low.rootKey = 57; low.loop = true;
    SampleRegion high; high.sample = mk(880.0); high.loKey = 60; high.hiKey = 127; high.rootKey = 81; high.loVel = 0; high.hiVel = 60; high.loop = true;
    SampleRegion hard; hard.sample = mk(1320.0); hard.loKey = 60; hard.hiKey = 127; hard.rootKey = 88; hard.loVel = 61; hard.hiVel = 127; hard.loop = true;
    o.regions = {low, high, hard};
    auto a = render(p, 57, 24000, 0.9f); CHECK_NEAR(estimateHz(a.l, 4000, 24000), 220.0, 2.0);
    auto b = render(p, 81, 24000, 0.3f); CHECK_NEAR(estimateHz(b.l, 4000, 24000), 880.0, 5.0);
    auto c = render(p, 88, 24000, 0.9f); CHECK_NEAR(estimateHz(c.l, 4000, 24000), 1320.0, 8.0);
}
TEST(granular_oscillator_is_deterministic_finite_and_responds_to_density) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[0].target = RouteTarget::main;
    auto s = std::make_shared<SampleData>(); s->sampleRate = 44100; s->rootNote = 69; s->left.resize(88200);
    for (std::size_t i = 0; i < s->left.size(); ++i) s->left[i] = 0.5f * std::sin(6.283185307 * 440.0 * double(i) / 44100.0);
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = OscMode::granular; o.sample = s; o.volume = 1.0; o.density = 30; o.grainLength = 0.1; o.position = 30;
    auto a = render(p, 69, 48000), b = render(p, 69, 48000);
    CHECK(finite(a.l) && a.l == b.l);
    const double lvl = rms(a.l, 12000);
    CHECK(lvl > 0.1 && lvl < 0.7);
    CHECK_NEAR(estimateHz(a.l, 12000, 48000), 440.0, 15.0);
    o.randomPitch = 12; o.randomPan = 100; auto c = render(p, 69, 48000);
    double d = 0; for (std::size_t i = 12000; i < 48000; ++i) d += std::abs(a.l[i] - c.l[i]); CHECK(d > 1.0);
}
TEST(spectral_oscillator_tracks_pitch_and_level) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[0].target = RouteTarget::main;
    auto s = std::make_shared<SampleData>(); s->sampleRate = 44100; s->rootNote = 69; s->left.resize(88200);
    for (std::size_t i = 0; i < s->left.size(); ++i) s->left[i] = 0.5f * std::sin(6.283185307 * 440.0 * double(i) / 44100.0);
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = OscMode::spectral; o.sample = s; o.spectral = buildSpectralAnalysis(*s); o.volume = 1.0;
    auto a = render(p, 69, 48000); CHECK(finite(a.l)); CHECK_NEAR(rms(a.l, 12000), 0.3536, 0.09); CHECK_NEAR(peakHzDft(a.l, 12000, 300.0, 700.0), 440.0, 4.0);
    auto b = render(p, 81, 48000); CHECK_NEAR(peakHzDft(b.l, 12000, 600.0, 1200.0), 880.0, 8.0);
    // a spectral warp (comb) alters the sound
    o.warpDefinitions[0].mode = WarpMode::spread; o.warpOneAmount = 1.0;
    auto c = render(p, 69, 48000); double d = 0; for (std::size_t i = 12000; i < 48000; ++i) d += std::abs(a.l[i] - c.l[i]); CHECK(d > 1.0);
}
TEST(noise_types_have_the_expected_spectral_tilt) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[3].target = RouteTarget::main;
    auto& o = p.oscillators[3]; o.enabled = true; o.mode = OscMode::noise; o.volume = 1.0; o.noiseColor = 0.5;
    auto ratio = [&](NoiseType t) {
        o.noiseType = t; auto r = render(p, 60, 96000);
        // low-band versus high-band energy by first difference (high-pass proxy)
        double lo = 0, hi = 0; for (std::size_t i = 4000; i + 1 < r.l.size(); ++i) { lo += double(r.l[i]) * r.l[i]; const double dlt = r.l[i + 1] - r.l[i]; hi += dlt * dlt; }
        return lo / hi;
    };
    const double white = ratio(NoiseType::white), pink = ratio(NoiseType::pink), brown = ratio(NoiseType::brown);
    CHECK(pink > white * 1.5 && brown > pink * 1.5);
    o.noiseType = NoiseType::geiger; auto g = render(p, 60, 48000); CHECK(finite(g.l) && peak(g.l) > 0.1);
    o.noiseType = NoiseType::white; o.noiseColor = 0.1; auto dark = render(p, 60, 48000);
    o.noiseColor = 0.9; auto bright = render(p, 60, 48000);
    double dd = 0, bb = 0; for (std::size_t i = 4000; i + 1 < dark.l.size(); ++i) { dd += std::pow(dark.l[i + 1] - dark.l[i], 2.0); bb += std::pow(bright.l[i + 1] - bright.l[i], 2.0); }
    CHECK(bb > dd * 5.0);
}
TEST(warps_change_the_waveform_and_stay_bounded) {
    Patch p = sineTablePatch(); auto& o = p.oscillators[0]; o.warpOneAmount = 0.7;
    auto base = render(p, 48, 12000).l;
    const WarpMode modes[] = {WarpMode::bendPositive, WarpMode::asymBoth, WarpMode::pwm, WarpMode::flip, WarpMode::sync, WarpMode::remap, WarpMode::quantize,
        WarpMode::evenOdd, WarpMode::hardClip, WarpMode::softClip, WarpMode::sineFold, WarpMode::linearFold, WarpMode::sineShaper,
        WarpMode::asymmetricClip, WarpMode::rectify, WarpMode::diode1, WarpMode::diode2, WarpMode::softSat, WarpMode::tapeSat, WarpMode::tube,
        WarpMode::stompBox, WarpMode::zeroSquare, WarpMode::filterLow, WarpMode::filterHigh, WarpMode::selfPhase};
    for (auto mode : modes) {
        o.warpDefinitions[0].mode = mode; o.warpDefinitions[0].variant = 2;
        auto r = render(p, 48, 12000).l;
        CHECK_MSG(finite(r) && peak(r) < 3.0, "warp " << int(mode));
        double d = 0; for (std::size_t i = 6000; i < 12000; ++i) d += std::abs(r[i] - base[i]);
        CHECK_MSG(d > 0.5, "warp " << int(mode) << " did not change the waveform");
    }
    // modulator-based warps
    p.oscillators[1] = p.oscillators[0]; p.oscillators[1].octave = 1; p.oscillators[1].volume = 0.0; p.oscillators[1].warpDefinitions = {};
    for (auto mode : {WarpMode::frequencyMod, WarpMode::frequencyModX, WarpMode::frequencyModPhase, WarpMode::phaseMod, WarpMode::ringMod, WarpMode::amplitudeMod}) {
        o.warpDefinitions[0].mode = mode; o.warpDefinitions[0].sourceIndex = 1;
        auto r = render(p, 48, 12000).l; CHECK_MSG(finite(r) && peak(r) < 4.0, "modulator warp " << int(mode));
        double d = 0; for (std::size_t i = 6000; i < 12000; ++i) d += std::abs(r[i] - base[i]); CHECK_MSG(d > 0.5, "modulator warp " << int(mode) << " inaudible");
    }
}
TEST(oversampling_quality_levels_keep_warped_and_plain_oscillators_aligned) {
    Patch plain = sineTablePatch(); plain.oscillators[0].octave = 3;
    Patch warped = plain; warped.oscillators[0].warpDefinitions[0].mode = WarpMode::softClip; warped.oscillators[0].warpOneAmount = 0.0;
    for (int level : {0, 1, 2}) {
        plain.globals.oversampling = level; warped.globals.oversampling = level;
        auto a = render(plain, 48, 12000), b = render(warped, 48, 12000);
        double dot = 0, pa = 0, pb = 0; for (std::size_t i = 4000; i < 12000; ++i) { dot += double(a.l[i]) * b.l[i]; pa += double(a.l[i]) * a.l[i]; pb += double(b.l[i]) * b.l[i]; }
        CHECK_MSG(dot / std::sqrt(pa * pb) > 0.99, "oversampling level " << level << " lost alignment: " << dot / std::sqrt(pa * pb));
    }
}
TEST(sub_oscillator_shapes_are_bandlimited_and_tracked) {
    Patch p; p.masterVolume = 1.0; p.envelopes[0].hold = 100; p.routes[4].target = RouteTarget::main;
    auto& o = p.oscillators[4]; o.enabled = true; o.mode = OscMode::sub; o.volume = 1.0;
    for (auto shape : {SubShape::pulse, SubShape::saw, SubShape::square, SubShape::triangle, SubShape::roundedRectangle}) {
        o.subShape = shape; auto r = render(p, 45, 24000);
        CHECK(finite(r.l) && peak(r.l) > 0.3 && peak(r.l) < 1.5);
        CHECK_NEAR(estimateHz(r.l, 4000, 24000), 110.0, 1.5);
    }
}

// ============================================================================ matrix
namespace {
ModulationRoute route(ModSource s, int si, ModTarget t, int ti, int tp, double amount, bool bipolar = false) {
    ModulationRoute r; r.sourceKind = s; r.sourceIndex = si; r.targetKind = t; r.targetIndex = ti; r.targetParam = tp; r.amount = amount; r.bipolar = bipolar; return r;
}
}
TEST(matrix_macro_to_osc_volume_scales_by_parameter_range) {
    Patch p = sineTablePatch(); p.oscillators[0].volume = 0.2;
    auto base = render(p, 60, 12000);
    p.modulation.push_back(route(ModSource::macro, 0, ModTarget::oscParam, 0, int(OscParam::volume), 50.0)); p.macroValues[0] = 1.0;
    auto mod = render(p, 60, 12000);
    CHECK_NEAR(rms(mod.l, 4000) / rms(base.l, 4000), 0.7 / 0.2, 0.15);   // +50% of a 0..1 range
}
TEST(matrix_aux_source_curve_and_polarity) {
    Patch p = sineTablePatch(); p.oscillators[0].volume = 0.0;
    auto r = route(ModSource::macro, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100.0);
    r.auxKind = ModSource::macro; r.auxIndex = 1; p.macroValues[0] = 1.0; p.macroValues[1] = 0.5;
    p.modulation.push_back(r);
    auto half = render(p, 60, 12000); CHECK_NEAR(rms(half.l, 4000), 0.5 * 0.7071, 0.03);
    p.modulation[0].auxInverted = true; p.macroValues[1] = 0.25;
    auto inv = render(p, 60, 12000); CHECK_NEAR(rms(inv.l, 4000), 0.75 * 0.7071, 0.03);
    // bipolar macro maps 0..1 to -1..1
    p.modulation[0].auxKind = ModSource::unknown; p.modulation[0].bipolar = true; p.macroValues[0] = 1.0;
    auto bi = render(p, 60, 12000); CHECK_NEAR(rms(bi.l, 4000), 1.0 * 0.7071, 0.03);
    p.macroValues[0] = 0.0; auto bi0 = render(p, 60, 12000); CHECK(rms(bi0.l, 4000) < 1e-4);   // clamps at 0
    // response curve: input curve bends the source value
    p.modulation[0].bipolar = false; p.macroValues[0] = 0.5; p.modulation[0].curveIn = 100.0;
    auto curved = render(p, 60, 12000); CHECK(std::abs(rms(curved.l, 4000) - 0.5 * 0.7071) > 0.05);
}
TEST(matrix_drives_envelope_lfo_and_macro_destinations) {
    Patch p = sineTablePatch();
    p.envelopes[0].attack = 0.0; p.envelopes[0].decay = 0.5; p.envelopes[0].sustain = 0.0; p.envelopes[0].hold = 0.0;
    auto plain = render(p, 60, 24000);
    // macro raises Env1 decay via the matrix
    p.modulation.push_back(route(ModSource::macro, 0, ModTarget::envParam, 0, int(EnvParam::decay), 100.0)); p.macroValues[0] = 1.0;
    auto longer = render(p, 60, 24000);
    CHECK(rms(longer.l, 12000, 24000) > rms(plain.l, 12000, 24000) * 3.0);
    // LFO 1 rate controlled by a macro
    Patch q = sineTablePatch(); q.lfoOneSine = false;
    q.lfoDefinitions[0].shape = LfoShape::sine; q.lfoDefinitions[0].rateHz = 1.0; q.lfoDefinitions[0].mode = LfoMode::trigger;
    q.modulation.push_back(route(ModSource::lfo, 0, ModTarget::oscParam, 0, int(OscParam::volume), -100.0, true));
    auto slow = render(q, 60, 24000);
    q.modulation.push_back(route(ModSource::macro, 0, ModTarget::lfoParam, 0, int(LfoParam::rate), 10.0)); q.macroValues[0] = 1.0;
    auto fast = render(q, 60, 24000);
    auto zeroCrossings = [](const std::vector<float>& v) { int n = 0; double env = 0; (void) env; for (std::size_t i = 1; i < v.size(); ++i) if (std::abs(v[i]) < 0.02 && std::abs(v[i - 1]) >= 0.02) ++n; return n; };
    CHECK(zeroCrossings(fast.l) != zeroCrossings(slow.l));
    // a macro can be a destination
    Patch m = sineTablePatch(); m.oscillators[0].volume = 0.0;
    m.modulation.push_back(route(ModSource::macro, 1, ModTarget::macroValue, 0, 0, 100.0)); m.macroValues[1] = 1.0;
    m.modulation.push_back(route(ModSource::macro, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100.0));
    auto viaMacro = render(m, 60, 12000); CHECK(rms(viaMacro.l, 6000) > 0.3);
}
TEST(matrix_global_fx_parameter_destination) {
    Patch p = sineTablePatch();
    FxModule util = makeFx(FxType::utils); util.rack = 0; util.position = 0; setParam(util, fx::uWidth, 100);
    p.fx = {util};
    p.oscillators[0].unison = 5; p.oscillators[0].detune = 0.4; p.oscillators[0].unisonStereo = 100; p.oscillators[0].blend = 100;
    auto wide = render(p, 60, 24000);
    // macro drives the FX width down by 100% of its 0..800 range -> mono
    p.modulation.push_back(route(ModSource::macro, 0, ModTarget::fxParam, 0, fx::uWidth, -12.5)); p.macroValues[0] = 1.0;
    auto narrow = render(p, 60, 24000);
    double dW = 0, dN = 0; for (std::size_t i = 8000; i < 24000; ++i) { dW += std::abs(wide.l[i] - wide.r[i]); dN += std::abs(narrow.l[i] - narrow.r[i]); }
    CHECK(dN < dW * 0.05);
}
TEST(matrix_curve_slew_and_delay_shape_the_modulation) {
    Patch p = sineTablePatch(); p.oscillators[0].volume = 0.0;
    auto r = route(ModSource::macro, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100.0); p.macroValues[0] = 1.0;
    r.delaySeconds = 0.25; p.modulation = {r};
    auto d = render(p, 60, 24000);
    CHECK(rms(d.l, 0, 6000) < 0.02 && rms(d.l, 14000, 24000) > 0.3);
    p.modulation[0].delaySeconds = 0.0; p.modulation[0].smoothRise = 60.0;
    auto s = render(p, 60, 24000);
    CHECK(rms(s.l, 0, 2400) < rms(s.l, 20000, 24000) * 0.6);
}

// ======================================================================= voices etc.
TEST(portamento_glides_between_notes) {
    Patch p = sineTablePatch(); p.globals.portamentoTime = 0.5; p.globals.portaAlways = true;
    SynthEngine e; e.prepare(sr); e.setSafetyLimiter(false); e.setPatch(&p);
    std::vector<float> l(72000, 0.0f), r(72000, 0.0f);
    e.noteOn(1, 60, 1.0f); e.render(l.data(), r.data(), 0, 12000); e.noteOff(1, 60);
    e.noteOn(1, 72, 1.0f);
    e.render(l.data(), r.data(), 12000, 6000);    // early in the glide
    e.render(l.data(), r.data(), 18000, 30000);
    const double early = estimateHz(l, 12500, 15500), late = estimateHz(l, 40000, 48000);
    CHECK(early > 262.0 && early < 500.0);
    CHECK_NEAR(late, 523.25, 4.0);
}
TEST(mono_legato_keeps_one_voice_and_returns_to_held_notes) {
    Patch p = sineTablePatch(); p.mono = true; p.globals.legato = true;
    SynthEngine e; e.prepare(sr); e.setPatch(&p);
    std::vector<float> l(4000), r(4000);
    e.noteOn(1, 60, 1.0f); e.render(l.data(), r.data(), 0, 2000);
    e.noteOn(1, 64, 1.0f); e.render(l.data(), r.data(), 0, 2000);
    CHECK(e.activeVoiceCount() == 1);
    e.noteOff(1, 64); e.render(l.data(), r.data(), 0, 2000);
    CHECK(e.activeVoiceCount() == 1);                      // fell back to the held C
    e.noteOff(1, 60); for (int i = 0; i < 20; ++i) e.render(l.data(), r.data(), 0, 2000);
    CHECK(e.activeVoiceCount() == 0);
}
TEST(sustain_pedal_holds_released_notes_and_pitch_bend_uses_range) {
    Patch p = sineTablePatch(); p.envelopes[0].release = 0.02; p.globals.bendUp = 12.0;
    SynthEngine e; e.prepare(sr); e.setPatch(&p);
    std::vector<float> l(24000, 0.f), r(24000, 0.f);
    e.setSustainPedal(true); e.noteOn(1, 69, 1.0f); e.render(l.data(), r.data(), 0, 4800); e.noteOff(1, 69);
    e.render(l.data(), r.data(), 4800, 4800); CHECK(e.activeVoiceCount() == 1);
    e.setSustainPedal(false); for (int i = 0; i < 4; ++i) e.render(l.data(), r.data(), 0, 4800); CHECK(e.activeVoiceCount() == 0);
    e.noteOn(1, 69, 1.0f); e.setPitchBend(1, 1.0f); std::fill(l.begin(), l.end(), 0.f);
    e.render(l.data(), r.data(), 0, 24000); CHECK_NEAR(estimateHz(l, 8000, 24000), 880.0, 5.0);
}
TEST(polyphony_limit_steals_and_never_exceeds_the_cap) {
    Patch p = sineTablePatch(); p.polyphony = 4;
    SynthEngine e; e.prepare(sr); e.setPatch(&p);
    std::vector<float> l(512), r(512);
    for (int n = 60; n < 72; ++n) { e.noteOn(1, n, 1.0f); e.render(l.data(), r.data(), 0, 256); }
    CHECK(e.activeVoiceCount() == 4);
}
TEST(velocity_reaches_amplitude_only_through_the_matrix_or_depth_control) {
    Patch p = sineTablePatch();
    auto soft = render(p, 60, 12000, 0.3f), hard = render(p, 60, 12000, 1.0f);
    CHECK_NEAR(rms(soft.l, 4000), rms(hard.l, 4000), 0.01);
    p.globals.velocityAmpDepth = 1.0; auto d = render(p, 60, 12000, 0.3f); CHECK_NEAR(rms(d.l, 4000) / rms(hard.l, 4000), 0.3, 0.05);
    p.globals.velocityAmpDepth = 0.0;
    p.modulation.push_back(route(ModSource::velocity, 0, ModTarget::globalParam, 0, int(GlobalParam::voiceAmp), 100.0, true));
    auto viaMatrix = render(p, 60, 12000, 0.25f); CHECK(rms(viaMatrix.l, 4000) < rms(hard.l, 4000) * 0.9);
}
TEST(key_and_velocity_zones_gate_oscillators) {
    Patch p = sineTablePatch(); p.oscillators[0].keyZoneLo = 60; p.oscillators[0].keyZoneHi = 72; p.oscillators[0].velZoneLo = 0; p.oscillators[0].velZoneHi = 100;
    CHECK(rms(render(p, 65, 6000, 0.5f).l, 2000) > 0.1);
    CHECK(rms(render(p, 50, 6000, 0.5f).l, 2000) < 1e-6);
    CHECK(rms(render(p, 65, 6000, 1.0f).l, 2000) < 1e-6);
}
TEST(filter_balance_routes_between_filters_and_via_env_bypasses_amp_envelope) {
    Patch p = sineTablePatch();
    p.routes[0].target = RouteTarget::filter; p.filters[0].enabled = true; p.filters[0].response = FilterResponse::low12; p.filters[0].cutoff = 0.0;
    p.filters[1].enabled = true; p.filters[1].response = FilterResponse::low12; p.filters[1].cutoff = 1.0;
    p.routes[5].target = RouteTarget::main; p.routes[6].target = RouteTarget::main;
    p.routes[0].filterBalance = -100; auto f1 = render(p, 60, 24000);   // closed filter only
    p.routes[0].filterBalance = 100; auto f2 = render(p, 60, 24000);    // open filter only
    p.routes[0].filterBalance = 0; auto both = render(p, 60, 24000);
    CHECK(rms(f1.l, 8000) < 0.05 && rms(f2.l, 8000) > 0.5);
    CHECK(rms(both.l, 8000) > rms(f2.l, 8000) * 0.9);
    Patch q = sineTablePatch(); q.envelopes[0].attack = 0.5; q.routes[0].target = RouteTarget::main;
    auto shaped = render(q, 60, 12000); q.routes[0].viaEnv[0] = false; auto ungated = render(q, 60, 12000);
    CHECK(rms(shaped.l, 0, 3000) < 0.1 && rms(ungated.l, 0, 3000) > 0.5);
}
TEST(fx_bus_sends_reach_their_racks_and_direct_bypasses_the_main_rack) {
    Patch p = sineTablePatch();
    FxModule mute = makeFx(FxType::utils); mute.rack = 0; setParam(mute, fx::uWidth, 100); mute.set[fxLevelSlot] = true; mute.p[fxLevelSlot] = 0.0;   // silences the main rack
    p.fx = {mute};
    CHECK(rms(render(p, 60, 12000).l, 4000) < 1e-9);
    p.routes[0].target = RouteTarget::direct; CHECK(rms(render(p, 60, 12000).l, 4000) > 0.5);
    p.routes[0].target = RouteTarget::none; p.routes[0].fxBus1Level = 100; CHECK(rms(render(p, 60, 12000).l, 4000) > 0.5);
    FxModule mute1 = mute; mute1.rack = 1; p.fx.push_back(mute1);
    CHECK(rms(render(p, 60, 12000).l, 4000) < 1e-9);
}
TEST(arpeggiator_orders_notes_and_respects_gate) {
    dsp::ArpEngine arp; ArpClipDef clip; clip.rate = 0.25; clip.shape = "Up"; clip.gate = 50.0;
    arp.noteOn(64, 0.8f); arp.noteOn(60, 0.8f); arp.noteOn(67, 0.8f);
    std::vector<int> ons; int offs = 0;
    for (int i = 0; i < int(sr * 2.0); ++i)
        arp.tick(clip, sr, 120.0, 50.0, [&](const dsp::NoteEmit& e) { if (e.on) ons.push_back(e.note); else ++offs; });
    CHECK(ons.size() >= 12);
    CHECK((ons[0] == 60 && ons[1] == 64 && ons[2] == 67 && ons[3] == 60));
    CHECK(offs >= int(ons.size()) - 1);
    // beats: 0.25 beat @120bpm = 0.125 s per step
    CHECK_NEAR(double(ons.size()), 16.0, 1.5);
    for (const char* shape : {"Down", "UpDown", "DownUp", "UpAndDown", "Converge", "ConvAndDiv", "Rand", "RandNoDup", "RandOnce", "RandDrift", "Played", "Chord", "ThumbUD"}) {
        dsp::ArpEngine a2; ArpClipDef c2; c2.shape = shape; c2.rate = 0.25;
        a2.noteOn(60, 1.0f); a2.noteOn(64, 1.0f); a2.noteOn(67, 1.0f);
        int count = 0; for (int i = 0; i < 48000; ++i) a2.tick(c2, sr, 120.0, 50.0, [&](const dsp::NoteEmit& e) { if (e.on) { ++count; CHECK(e.note >= 0 && e.note < 128); } });
        CHECK_MSG(count >= 7, "arp shape " << shape << " produced " << count);
    }
}
TEST(clip_player_plays_notes_at_their_timestamps) {
    dsp::ClipPlayer cp; ClipSettings cs; cs.enabled = true; cs.selectOctave = -3;
    cs.clips[0].playbackMode = "OneShot"; cs.clips[0].lengthBeats = 4; cs.clips[0].rate = 1.0; cs.clips[0].spanMode = "Poly";
    cs.clips[0].notes = {{0.0, 0.5, 60, 1.0}, {1.0, 0.5, 64, 1.0}, {2.0, 0.5, 67, 1.0}};
    std::vector<std::pair<int, int>> events;  // sample index, note
    auto noop = [](const dsp::NoteEmit&) {};
    CHECK(cp.noteOn(cs, 0, 1.0f, noop));
    for (int i = 0; i < int(sr * 3); ++i) cp.tick(cs, sr, 120.0, 60, [&](const dsp::NoteEmit& e) { if (e.on) events.emplace_back(i, e.note); });
    CHECK(events.size() == 3);
    CHECK(events[0].second == 60 && events[1].second == 64 && events[2].second == 67);
    CHECK_NEAR(double(events[1].first), sr * 0.5, 50.0);   // 1 beat at 120 bpm
    CHECK_NEAR(double(events[2].first), sr * 1.0, 50.0);
}

// ====================================================================== adapter & IO
namespace {
SerumDocument doc() { SerumDocument d; d.metadata = {{"presetName", "synthetic"}, {"productVersion", "2.1.5"}}; return d; }
}
TEST(importer_maps_lfo_curve_data_with_inverted_y) {
    auto d = doc();
    d.state["LFO1"]["plainParams"] = {{"kParamMode", "Free"}, {"kParamRate", 2.0}};
    d.state["LFO1"]["curveData"] = {{"curveVals", {0.5, 0.5, 0.5}}, {"numPoints", 2}, {"xVals", {0.0, 1.0, 1.0}}, {"yVals", {1.0, 0.0, 1.0}}};
    auto p = importSerum(d, {});
    const auto& l = p.lfoDefinitions[1];
    CHECK(l.shape == LfoShape::curve && l.mode == LfoMode::free && l.path.x.size() == 3);
    CHECK_NEAR(l.path.y[0], 0.0, 1e-12);       // stored y 1.0 = value 0 ("Saw Up" starts at the bottom)
    CHECK_NEAR(l.path.y[1], 1.0, 1e-12);
    CHECK_NEAR(dsp::evalPath(l.path, 0.5), 0.0, 1e-6);
}
TEST(importer_maps_sources_destinations_aux_and_fx_parameters) {
    auto d = doc();
    d.state["FXRack1"]["FX"] = Json::array({Json{{"FXDelay", Json{{"plainParams", Json{{"kParamTimeL", 0.3}}}}}, {"type", 2}},
                                            Json{{"FXReverb", Json{{"plainParams", Json{{"kParamType", "kSpace"}, {"kParamSize", 80.0}}}}}, {"type", 6}}});
    d.state["ModSlot0"] = Json{{"source", {16, 26}}, {"destModuleTypeString", "FXReverb"}, {"destModuleID", 101}, {"destModuleParamName", "kParamSize"},
                               {"destModuleParamID", 3}, {"plainParams", Json{{"kParamAmount", 40.0}, {"kParamBipolar", 1.0}, {"kParamAuxInverted", 1.0}}}};
    d.state["ModSlot1"] = Json{{"source", {59, 0}}, {"destModuleTypeString", "WTOsc"}, {"destModuleID", 2}, {"destModuleParamName", "kParamWarp2"},
                               {"plainParams", Json{{"kParamAmount", 10.0}}}};
    d.state["ModSlot2"] = Json{{"source", {25, 0}}, {"destModuleTypeString", "VoiceFilter"}, {"destModuleID", 1}, {"destModuleParamName", "kParamLevelOut"},
                               {"plainParams", Json{{"kParamAmount", 100.0}}}};
    auto p = importSerum(d, {});
    CHECK(p.fx.size() == 2 && p.fx[1].fxType == FxType::reverb && p.fx[1].modeVariant == 2 && p.fx[1].rack == 1 && p.fx[1].position == 1);
    CHECK_NEAR(p.fx[0].p[fx::dTimeL], 0.3, 1e-12);
    CHECK(p.modulation.size() == 3);
    const auto& r0 = p.modulation[0];
    CHECK(r0.sourceKind == ModSource::velocity && r0.auxKind == ModSource::macro && r0.auxIndex == 1 && r0.auxInverted && r0.bipolar);
    CHECK(r0.targetKind == ModTarget::fxParam && r0.targetIndex == 1 && r0.targetParam == fx::rSize);
    CHECK(p.modulation[1].sourceKind == ModSource::discreteRandom && p.modulation[1].targetKind == ModTarget::warpTwoAmount && p.modulation[1].targetIndex == 2);
    CHECK(p.modulation[2].targetKind == ModTarget::filterParam && p.modulation[2].targetParam == int(FilterParam::level) && p.modulation[2].targetIndex == 1);
}
TEST(importer_reads_units_defaults_and_accounts_for_every_field) {
    auto d = doc();
    d.state["Oscillator0"]["plainParams"] = {{"kParamPan", -50.0}, {"kParamCoarsePit", 7.5}, {"kParamDetuneWid", 40.0}, {"kParamUnison", 5.0},
                                              {"kParamDetuneMode", "kDetuneSuper"}, {"kParamUnisonStack", "kOctave2"}, {"kParamPitchMode", "Ratio"}};
    d.state["Oscillator0"]["WTOsc0"]["plainParams"] = {{"kParamWarpMenu", "kFM_OSC2"}, {"kParamWarpVar", 0.25}, {"kParamPhaseMemory", "kContiguous"}};
    d.state["Oscillator3"]["plainParams"] = {{"kParamEnable", 1.0}};
    d.state["Oscillator3"]["NoiseOsc3"]["plainParams"] = {{"kParamNoiseType", "Pink"}, {"kParamColor", 0.8}};
    d.state["Global0"]["plainParams"] = {{"kParamBendRangeUp", 12.0}, {"kParamBendRangeDn", -5.0}, {"kParamPortamentoTime", 0.3}, {"kParamVoicePriority", "Low"},
                                          {"kParamLegato", 1.0}, {"kParamGlobalTuning", 432.0}};
    d.state["RoutingSlot0"]["plainParams"] = {{"kParamFilterBalance", 100.0}, {"kParamViaEnv1", 0.0}, {"kParamFXBus1Level", 60.0}};
    d.state["Env1"]["plainParams"] = {{"kParamAttack", 0.2}, {"kParamStart", 0.3}, {"kParamEnd", 0.1}};
    d.state["VoiceFilter1"]["plainParams"] = {{"kParamEnable", 1.0}, {"kParamType", "Phase24N"}, {"kParamKeyTrack", 1.0}, {"kParamLevelOut", 0.4}};
    auto p = importSerum(d, {});
    const auto& o = p.oscillators[0];
    CHECK_NEAR(o.pan, -1.0, 1e-12); CHECK_NEAR(o.semitone, 7.5, 1e-12); CHECK_NEAR(o.blend, 40.0, 1e-12); CHECK(o.unison == 5);
    CHECK(o.detuneMode == DetuneMode::super && o.unisonStack == UnisonStack::octave2 && o.pitchMode == OscPitchMode::ratio);
    CHECK(o.warpDefinitions[0].mode == WarpMode::frequencyMod && o.warpDefinitions[0].sourceIndex == 2 && o.warpDefinitions[0].var == 0.25 && !o.perVoicePhase);
    CHECK(p.oscillators[3].noiseType == NoiseType::pink && p.oscillators[3].noiseTypeExplicit && p.oscillators[3].noiseColor == 0.8);
    CHECK(p.globals.bendUp == 12.0 && p.globals.bendDown == -5.0 && p.globals.priority == VoicePriority::low && p.globals.legato && p.globals.masterTuning == 432.0);
    CHECK(p.routes[0].filterBalance == 100.0 && !p.routes[0].viaEnv[0] && p.routes[0].fxBus1Level == 60.0);
    CHECK(p.envelopes[1].start == 0.3 && p.envelopes[1].end == 0.1);
    CHECK(p.filters[1].enabled && p.filters[1].response == FilterResponse::phaser && p.filters[1].keyTrack && p.filters[1].level == 0.4);
    unsigned unmapped = 0; for (const auto& g : p.diagnostics) unmapped += g.status == "unmapped_parameter";
    CHECK(p.mappedParameters + unmapped == p.explicitParameters);
    CHECK(unmapped == 0);
}
TEST(importer_maps_lfo_point_buses_and_random_sources) {
    auto d = doc();
    d.state["lfoPointModAssignments"] = Json::array({Json{{"busID", 2}, {"lfoID", 1}, {"lfoType", 0}, {"pointID", 1}, {"target", 1}}});
    d.state["ModSlot0"] = Json{{"source", {40, 0}}, {"destModuleTypeString", "LFOPointModBus"}, {"destModuleID", 2}, {"destModuleParamName", "kParamValue"}, {"plainParams", Json{{"kParamAmount", 30.0}}}};
    d.state["ModSlot1"] = Json{{"source", {42, 0}}, {"destModuleTypeString", "Oscillator"}, {"destModuleID", 0}, {"destModuleParamName", "kParamPan"}, {"plainParams", Json{{"kParamAmount", 30.0}}}};
    auto p = importSerum(d, {});
    CHECK(p.lfoDefinitions[1].pointMods.size() == 1 && p.lfoDefinitions[1].pointMods[0].bus == 2 && p.lfoDefinitions[1].pointMods[0].point == 1);
    CHECK(p.modulation[0].targetKind == ModTarget::lfoPointBus && p.modulation[0].targetIndex == 2);
    CHECK(p.modulation[0].sourceKind == ModSource::random && p.modulation[1].sourceKind == ModSource::random);
}
TEST(importer_reads_arp_and_clip_patterns) {
    auto d = doc();
    d.state["Arp0"]["plainParams"] = {{"kParamEnabled", 1.0}, {"kParamActiveClipID", 2.0}};
    d.state["ArpClip2"]["plainParams"] = {{"kParamRate", 0.5}, {"kParamShape", "UpDown"}, {"kParamGate", 60.0}};
    d.state["ArpClip2"]["clip"] = {{"regionEndBeats", 2.0}, {"notes", Json::array({Json{{"timeStamp", 0.0}, {"length", 0.25}, {"noteNum", 3}, {"attributes", {0.5}}}})}};
    d.state["ClipPlayer0"]["plainParams"] = {{"kParamEnabled", 1.0}};
    d.state["MidiClip0"]["clip"] = {{"regionEndBeats", 8.0}, {"notes", Json::array({Json{{"timeStamp", 1.0}, {"length", 2.0}, {"noteNum", 60}, {"attributes", {0.9}}}})}};
    auto p = importSerum(d, {});
    CHECK(p.arpSettings.enabled && p.arpSettings.activeClip == 2 && p.arpSettings.active().shape == "UpDown" && p.arpSettings.active().gate == 60.0);
    CHECK(p.arpSettings.active().steps.size() == 1 && p.arpSettings.active().steps[0].note == 3);
    CHECK(p.clipSettings.enabled && p.clipSettings.clips[0].notes.size() == 1 && p.clipSettings.clips[0].notes[0].time == 1.0);
}
TEST(sfz_parser_reads_regions_groups_and_note_names) {
    const std::string text = "// generated\n<group> amp_veltrack=25 ampeg_release=0.7\n<region> lokey=36 hikey=47 pitch_keycenter=40 lovel=1 hivel=64 sample=Kit A/Kick one.flac tune=-10\n"
                             "<region> key=c4 sample=Snare.flac loop_mode=loop_continuous loop_start=100 loop_end=900 volume=-3\n";
    SfzGroup g; auto r = parseSfz(text, &g);
    CHECK(r.size() == 2 && g.ampVelTrack == 25.0 && g.hasRelease && g.ampegRelease == 0.7);
    CHECK(r[0].loKey == 36 && r[0].hiKey == 47 && r[0].rootKey == 40 && r[0].hiVel == 64 && r[0].sample == "Kit A/Kick one.flac" && r[0].tuneCents == -10.0);
    CHECK(r[1].loKey == 60 && r[1].hiKey == 60 && r[1].loop && r[1].loopEnd == 900.0 && r[1].volumeDb == -3.0);
}
TEST(native_state_round_trips_every_new_field) {
    Patch p = sineTablePatch();
    p.name = "roundtrip"; p.mono = true; p.polyphony = 6;
    auto& o = p.oscillators[1]; o.enabled = true; o.mode = OscMode::granular; o.density = 77; o.windowShape = WindowShape::tukey; o.detuneMode = DetuneMode::super;
    o.unisonStack = UnisonStack::octaveFifth2; o.pitchMode = OscPitchMode::harmonics; o.noiseType = NoiseType::brown; o.keyZoneLo = 10; o.slicingMode = 2;
    o.warpDefinitions[1] = {WarpMode::vocode, 3, 2, 0.3}; o.sampleEnv.override_ = true; o.sampleEnv.release = 1.5;
    p.filters[0].response = FilterResponse::multi; p.filters[0].variant = 7; p.filters[0].keyTrack = true; p.filters[0].level = 0.3;
    p.routes[2].viaEnv = {false, true, true, false}; p.routes[2].filterBalance = 33;
    p.lfoDefinitions[3].shape = LfoShape::path; p.lfoDefinitions[3].path.x = {0.1, 0.9}; p.lfoDefinitions[3].path.y = {0.2, 0.7}; p.lfoDefinitions[3].mode = LfoMode::envelope;
    auto r = route(ModSource::channelPressure, 0, ModTarget::fxParam, 0, fx::uWidth, 12.0); r.auxKind = ModSource::modWheel; r.mainCurve.x = {0, 1}; r.mainCurve.y = {0, 1};
    p.modulation.push_back(r);
    FxModule m = makeFx(FxType::filter); m.filterResponse = FilterResponse::comb; m.filterVariant = 3; setParam(m, fx::fFreq, 0.4); p.fx.push_back(m);
    p.globals.portamentoTime = 0.4; p.globals.priority = VoicePriority::high; p.voicePanel.detune[3] = 12; p.arpSettings.enabled = true; p.arpSettings.clips[0].shape = "Chord";
    p.clipSettings.clips[1].notes = {{0, 1, 61, 0.7}};
    const auto q = patchFromJson(patchToJson(p));
    CHECK(q.name == "roundtrip" && q.mono && q.polyphony == 6);
    const auto& oo = q.oscillators[1];
    CHECK(oo.mode == OscMode::granular && oo.density == 77 && oo.windowShape == WindowShape::tukey && oo.detuneMode == DetuneMode::super &&
          oo.unisonStack == UnisonStack::octaveFifth2 && oo.pitchMode == OscPitchMode::harmonics && oo.noiseType == NoiseType::brown && oo.keyZoneLo == 10 && oo.slicingMode == 2);
    CHECK(oo.warpDefinitions[1].mode == WarpMode::vocode && oo.warpDefinitions[1].sourceIndex == 3 && oo.warpDefinitions[1].variant == 2 && oo.warpDefinitions[1].var == 0.3);
    CHECK(oo.sampleEnv.override_ && oo.sampleEnv.release == 1.5);
    CHECK(q.filters[0].response == FilterResponse::multi && q.filters[0].variant == 7 && q.filters[0].keyTrack && q.filters[0].level == 0.3);
    CHECK(!q.routes[2].viaEnv[0] && q.routes[2].viaEnv[1] && q.routes[2].filterBalance == 33);
    CHECK(q.lfoDefinitions[3].shape == LfoShape::path && q.lfoDefinitions[3].path.x.size() == 2 && q.lfoDefinitions[3].mode == LfoMode::envelope);
    CHECK(q.modulation.size() == 1 && q.modulation[0].sourceKind == ModSource::channelPressure && q.modulation[0].auxKind == ModSource::modWheel &&
          q.modulation[0].targetKind == ModTarget::fxParam && q.modulation[0].mainCurve.x.size() == 2);
    CHECK(q.fx.size() == 1 && q.fx[0].filterResponse == FilterResponse::comb && q.fx[0].filterVariant == 3 && q.fx[0].set[fx::fFreq] && q.fx[0].p[fx::fFreq] == 0.4);
    CHECK(q.globals.portamentoTime == 0.4 && q.globals.priority == VoicePriority::high && q.voicePanel.detune[3] == 12);
    CHECK(q.arpSettings.enabled && q.arpSettings.clips[0].shape == "Chord" && q.clipSettings.clips[1].notes.size() == 1 && q.clipSettings.clips[1].notes[0].note == 61);
}
TEST(engine_never_produces_non_finite_output_under_extreme_patches) {
    Patch p = sineTablePatch();
    auto& o = p.oscillators[0]; o.unison = 16; o.detune = 1.0; o.warpOneAmount = 1.0; o.warpTwoAmount = 1.0;
    o.warpDefinitions[0].mode = WarpMode::selfPhase; o.warpDefinitions[1].mode = WarpMode::frequencyMod; o.warpDefinitions[1].sourceIndex = 0;
    p.routes[0].target = RouteTarget::filter; p.filters[0].enabled = true; p.filters[0].response = FilterResponse::ladderAcid; p.filters[0].resonance = 100; p.filters[0].drive = 100;
    p.filters[1].enabled = true; p.filters[1].response = FilterResponse::comb; p.filters[1].resonance = 100;
    p.routes[5].target = RouteTarget::filter; p.routes[6].target = RouteTarget::main;
    for (auto t : {FxType::bode, FxType::chorus, FxType::delay, FxType::distortion, FxType::flanger, FxType::hyperD, FxType::phaser, FxType::reverb, FxType::comp}) {
        auto m = makeFx(t); for (std::size_t i = 0; i < fxParamTable(t).size(); ++i) setParam(m, i, fxParamTable(t)[i].hi);
        p.fx.push_back(m);
    }
    for (std::size_t i = 0; i < p.fx.size(); ++i) p.fx[i].position = int(i);
    p.modulation.push_back(route(ModSource::lfo, 0, ModTarget::filterCutoff, 0, 0, 100.0, true)); p.lfoOneSine = true; p.lfoOneRateHz = 30;
    SynthEngine e; e.prepare(sr); e.setPatch(&p);
    std::vector<float> l(48000), r(48000);
    for (int n = 36; n < 72; n += 3) e.noteOn(1, n, 1.0f);
    for (int at = 0; at < 48000; at += 128) e.render(l.data(), r.data(), at, 128);
    CHECK(finite(l) && finite(r));
    CHECK(peak(l) <= 1.0001);   // the output ceiling holds
}

TEST(render_path_never_allocates_with_every_module_active) {
    // one patch that exercises voices, matrix, all five oscillator types, both filters, arp, clips, macros and a full FX rack
    Patch p = sineTablePatch();
    p.polyphony = 8; p.arpSettings.enabled = false;
    auto sample = std::make_shared<SampleData>(); sample->sampleRate = 44100; sample->left.resize(88200);
    for (std::size_t i = 0; i < sample->left.size(); ++i) sample->left[i] = 0.4f * std::sin(6.283185307 * 220.0 * double(i) / 44100.0);
    p.oscillators[1].enabled = true; p.oscillators[1].mode = OscMode::granular; p.oscillators[1].sample = sample; p.oscillators[1].density = 60;
    p.oscillators[2].enabled = true; p.oscillators[2].mode = OscMode::spectral; p.oscillators[2].sample = sample; p.oscillators[2].spectral = buildSpectralAnalysis(*sample); p.oscillators[2].unison = 3;
    p.oscillators[3].enabled = true; p.oscillators[3].mode = OscMode::noise; p.oscillators[4].enabled = true; p.oscillators[4].mode = OscMode::sub;
    for (int i = 0; i < 5; ++i) p.routes[std::size_t(i)].target = RouteTarget::filter;
    p.filters[0].enabled = p.filters[1].enabled = true; p.filters[0].response = FilterResponse::ladder24; p.filters[1].response = FilterResponse::comb;
    p.routes[5].target = RouteTarget::filter; p.routes[6].target = RouteTarget::main; p.routes[0].fxBus1Level = 50; p.routes[1].fxBus2Level = 50;
    p.oscillators[0].warpDefinitions[0].mode = WarpMode::softClip; p.oscillators[0].warpOneAmount = 0.5; p.oscillators[0].unison = 5;
    p.lfoOneSine = false; p.lfoDefinitions[0].shape = LfoShape::curve; p.lfoDefinitions[0].path.x = {0, 1}; p.lfoDefinitions[0].path.y = {0, 1}; p.lfoDefinitions[0].rateHz = 3;
    p.modulation = {route(ModSource::lfo, 0, ModTarget::filterCutoff, 0, 0, 50.0, true), route(ModSource::velocity, 0, ModTarget::oscParam, 0, int(OscParam::volume), -20.0),
                    route(ModSource::macro, 0, ModTarget::fxParam, 2, fx::dWet, 30.0), route(ModSource::envelope, 0, ModTarget::envParam, 0, 0, 20.0)};
    int idx = 0;
    for (auto t : {FxType::bode, FxType::chorus, FxType::delay, FxType::distortion, FxType::eq, FxType::filter, FxType::flanger, FxType::hyperD, FxType::phaser, FxType::reverb, FxType::utils, FxType::comp, FxType::pump, FxType::stutter}) {
        auto m = makeFx(t); m.rack = idx % 3; m.position = idx / 3; m.filterResponse = FilterResponse::phaser; m.filterVariant = 4; p.fx.push_back(m); ++idx;
    }
    FxModule conv = makeFx(FxType::conv); auto ir = std::make_shared<SampleData>(); ir->sampleRate = sr; ir->left.assign(20000, 0.0f); for (int i = 0; i < 20000; ++i) ir->left[std::size_t(i)] = float(std::sin(i * 0.37) * std::exp(-i / 3000.0));
    conv.convIr = buildConvIr(*ir); conv.rack = 0; conv.position = 9; p.fx.push_back(conv);
    SynthEngine e; e.prepare(sr); e.setPatch(&p);
    std::vector<float> l(512), r(512);
    e.noteOn(1, 60, 0.9f); e.render(l.data(), r.data(), 0, 512);          // warm-up: first block
    g_allocations = 0; g_countAllocations = true;
    for (int n = 0; n < 40; ++n) {
        if (n % 5 == 0) e.noteOn(1, 48 + n, 0.7f);
        if (n % 7 == 3) e.noteOff(1, 48 + n - 3);
        if (n == 10) e.setPitchBend(1, 0.5f);
        e.setModWheel(float(n) / 40.0f);
        e.render(l.data(), r.data(), 0, 512);
    }
    g_countAllocations = false;
    CHECK_MSG(g_allocations.load() == 0, "audio path allocated " << g_allocations.load() << " times");
    CHECK(finite(l) && finite(r));
}

// ========================================================== ZYG extensions: pump / stutter / sidechain
namespace {
struct Ext {
    std::vector<double> l, r;
};
// Runs a unit in 200-sample blocks with an advancing tempo context. `hook(blockStartSample, ctx, params)` may change state.
Ext runExt(FxModule& m, const std::vector<double>& in, double bpm,
           const std::function<void(std::size_t, dsp::FxContext&, double*)>& hook = {}, const std::vector<double>* sc = nullptr) {
    auto unit = dsp::makeFxUnit(m.fxType); unit->prepare(sr); unit->reset();
    dsp::FxContext ctx; ctx.bpm = bpm; unit->ctx = &ctx;
    double p[fxParamSlots]; effective(m, p);
    Ext out; out.l = in; out.r = in;
    for (std::size_t at = 0; at < in.size(); at += 200) {
        const int n = int(std::min<std::size_t>(200, in.size() - at));
        ctx.beat = double(at) / sr * bpm / 60.0;
        ctx.scL = ctx.scR = sc ? sc->data() + at : nullptr;
        if (hook) hook(at, ctx, p);
        unit->process(m, p, out.l.data() + at, out.r.data() + at, n);
    }
    return out;
}
}
TEST(pump_tempo_mode_ducks_once_per_cycle_and_recovers) {
    auto m = makeFx(FxType::pump);
    setParam(m, fx::uDepth, 100); setParam(m, fx::uBeats, 1); setParam(m, fx::uAttack, 0.1); setParam(m, fx::uTrigger, fx::pumpTempo);
    const std::vector<double> in(72000, 1.0);
    auto o = runExt(m, in, 120.0);   // one beat = 24000 samples
    CHECK(o.l[100] < 0.15);
    CHECK_NEAR(o.l[12000], 0.5, 0.05);
    CHECK(o.l[23900] > 0.97);
    CHECK_NEAR(o.l[24100], o.l[100], 0.1);       // periodic
    CHECK_NEAR(o.l[36000], 0.5, 0.05);
    setParam(m, fx::uDepth, 0);
    auto flat = runExt(m, in, 120.0);
    CHECK(std::abs(flat.l[100] - 1.0) < 1e-6);
}
TEST(pump_shape_and_hold_change_the_recovery_curve) {
    auto m = makeFx(FxType::pump);
    setParam(m, fx::uDepth, 100); setParam(m, fx::uBeats, 1); setParam(m, fx::uAttack, 0.1);
    const std::vector<double> in(24000, 1.0);
    setParam(m, fx::uShape, -100); const double fast = runExt(m, in, 120.0).l[6000];
    setParam(m, fx::uShape, 100); const double slow = runExt(m, in, 120.0).l[6000];
    CHECK(fast > 0.6 && slow < 0.25);
    setParam(m, fx::uShape, 0); setParam(m, fx::uHold, 50);
    auto held = runExt(m, in, 120.0);
    CHECK(held.l[8000] < 0.1 && held.l[23000] > 0.9);
}
TEST(pump_note_mode_retriggers_on_note_on) {
    auto m = makeFx(FxType::pump);
    setParam(m, fx::uDepth, 100); setParam(m, fx::uBeats, 0.5); setParam(m, fx::uAttack, 0.1); setParam(m, fx::uTrigger, fx::pumpNote);
    const std::vector<double> in(48000, 1.0);
    unsigned serial = 0;
    auto o = runExt(m, in, 120.0, [&](std::size_t at, dsp::FxContext& c, double*) { if (at == 20000) ++serial; c.noteSerial = serial; });
    CHECK(o.l[10000] > 0.97);                    // no note yet: open
    CHECK(o.l[20100] < 0.2);                     // note at 20000 ducks
    CHECK(o.l[20000 + 11900] > 0.9);             // recovered after half a beat
}
TEST(pump_sidechain_mode_reacts_to_the_external_input_only) {
    auto m = makeFx(FxType::pump);
    setParam(m, fx::uDepth, 100); setParam(m, fx::uBeats, 0.5); setParam(m, fx::uAttack, 0.1); setParam(m, fx::uTrigger, fx::pumpSidechain);
    setParam(m, fx::uThresh, -20);
    const std::vector<double> in(48000, 1.0);
    std::vector<double> sc(48000, 0.0); for (int i = 0; i < 400; ++i) sc[std::size_t(10000 + i)] = 0.9;
    auto with = runExt(m, in, 120.0, {}, &sc);
    CHECK(with.l[5000] > 0.99 && with.l[10500] < 0.3 && with.l[10000 + 11900] > 0.9);
    auto without = runExt(m, in, 120.0);
    CHECK(without.l[10500] > 0.99);              // no sidechain connected: passes audio
    setParam(m, fx::uTrigger, fx::pumpFollow);
    std::vector<double> loud(48000, 0.0); for (int i = 20000; i < 30000; ++i) loud[std::size_t(i)] = 0.8 * ((i & 16) ? 1.0 : -1.0);
    auto fol = runExt(m, in, 120.0, {}, &loud);
    CHECK(fol.l[10000] > 0.99 && fol.l[25000] < 0.35 && fol.l[45000] > 0.9);
}
TEST(stutter_loops_the_captured_slice_and_stays_transparent_when_off) {
    auto m = makeFx(FxType::stutter);
    setParam(m, fx::tBeats, 0.25); setParam(m, fx::tMode, 0); setParam(m, fx::tSmooth, 0.5); setParam(m, fx::tGate, 100);
    const auto in = noise(60000, 11);
    // off: passes the input untouched
    setParam(m, fx::tActive, 0);
    auto off = runExt(m, in, 120.0);
    for (std::size_t i = 0; i < in.size(); i += 997) CHECK_NEAR(off.l[i], in[i], 1e-9);
    // engage at 12000 -> loops in[6000..12000) with a 6000 sample period (0.25 beat at 120 bpm)
    auto o = runExt(m, in, 120.0, [&](std::size_t at, dsp::FxContext&, double* p) { p[fx::tActive] = at >= 12000 ? 1.0 : 0.0; });
    for (std::size_t k : {1000u, 3000u, 5500u}) {
        CHECK_NEAR(o.l[12000 + k], in[6001 + k], 0.02);
        CHECK_NEAR(o.l[18000 + k], in[6001 + k], 0.02);
        CHECK_NEAR(o.l[24000 + k], in[6001 + k], 0.02);
    }
    // reverse plays the slice backwards
    setParam(m, fx::tReverse, 1);
    auto rv = runExt(m, in, 120.0, [&](std::size_t at, dsp::FxContext&, double* p) { p[fx::tActive] = at >= 12000 ? 1.0 : 0.0; });
    CHECK_NEAR(rv.l[12000 + 1000], in[12000 - 1000], 0.03);
}
TEST(stutter_gate_decay_and_auto_chance_behave) {
    auto m = makeFx(FxType::stutter);
    setParam(m, fx::tBeats, 0.25); setParam(m, fx::tSmooth, 0.5);
    const std::vector<double> in(60000, 0.5);
    setParam(m, fx::tGate, 50); setParam(m, fx::tActive, 1);
    auto g = runExt(m, in, 120.0, [&](std::size_t at, dsp::FxContext&, double* p) { p[fx::tActive] = at >= 12000 ? 1.0 : 0.0; });
    CHECK(g.l[12000 + 1500] > 0.4 && g.l[12000 + 4500] < 0.05);   // open in the first half of the loop, muted in the second
    setParam(m, fx::tGate, 100); setParam(m, fx::tFalloff, 80);
    auto d = runExt(m, in, 120.0, [&](std::size_t at, dsp::FxContext&, double* p) { p[fx::tActive] = at >= 12000 ? 1.0 : 0.0; });
    CHECK(d.l[12000 + 3000] > d.l[12000 + 6000 * 4 + 3000] * 1.5);   // every repeat is quieter
    // auto mode with chance 0 never engages, with chance 100 engages
    setParam(m, fx::tFalloff, 0); setParam(m, fx::tMode, 1); setParam(m, fx::tChance, 0);
    const auto n = noise(60000, 3);
    auto none = runExt(m, n, 120.0);
    for (std::size_t i = 0; i < n.size(); i += 991) CHECK_NEAR(none.l[i], n[i], 1e-9);
    setParam(m, fx::tChance, 100);
    auto all = runExt(m, n, 120.0);
    double diff = 0; for (std::size_t i = 20000; i < 60000; ++i) diff += std::abs(all.l[i] - n[i]);
    CHECK(diff > 100.0);
}
TEST(native_json_round_trips_the_zyg_extension_effects_and_sidechain_route) {
    Patch p = sineTablePatch();
    FxModule pump = makeFx(FxType::pump); setParam(pump, fx::uDepth, 65); setParam(pump, fx::uTrigger, fx::pumpSidechain);
    FxModule st = makeFx(FxType::stutter); st.position = 1; setParam(st, fx::tBeats, 0.125); setParam(st, fx::tReverse, 1);
    p.fx = {pump, st};
    p.modulation.push_back(route(ModSource::sidechain, 0, ModTarget::fxParam, 0, fx::uDepth, 40.0));
    const auto q = patchFromJson(patchToJson(p));
    CHECK(q.fx.size() == 2 && q.fx[0].fxType == FxType::pump && q.fx[1].fxType == FxType::stutter);
    CHECK_NEAR(q.fx[0].p[fx::uDepth], 65.0, 1e-9); CHECK_NEAR(q.fx[1].p[fx::tBeats], 0.125, 1e-9);
    CHECK(q.modulation.size() == 1 && q.modulation[0].sourceKind == ModSource::sidechain);
}
TEST(engine_sidechain_input_drives_the_follower_source_and_the_pump) {
    // follower source: sidechain level -> oscillator volume
    Patch p = sineTablePatch(); p.oscillators[0].volume = 0.0;
    p.modulation.push_back(route(ModSource::sidechain, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100.0));
    SynthEngine e; e.prepare(sr); e.setSafetyLimiter(false); e.setPatch(&p);
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f), scin(24000, 0.0f);
    for (int i = 12000; i < 24000; ++i) scin[std::size_t(i)] = 0.8f * float(std::sin(i * 0.05));
    e.noteOn(1, 60, 1.0f);
    for (int at = 0; at < 24000; at += 256) {
        const int n = std::min(256, 24000 - at);
        e.setSidechain(scin.data(), scin.data(), 24000);
        e.render(l.data(), r.data(), at, n);
    }
    CHECK(rms(l, 0, 10000) < 0.01 && rms(l, 18000, 24000) > 0.1);
    // pump in follow mode ducks the synth while the sidechain is loud
    Patch q = sineTablePatch();
    FxModule pump = makeFx(FxType::pump); pump.rack = 0; pump.position = 0;
    setParam(pump, fx::uTrigger, fx::pumpFollow); setParam(pump, fx::uDepth, 100); setParam(pump, fx::uThresh, -30); setParam(pump, fx::uAttack, 1);
    q.fx = {pump};
    SynthEngine e2; e2.prepare(sr); e2.setSafetyLimiter(false); e2.setPatch(&q);
    std::vector<float> l2(24000, 0.0f), r2(24000, 0.0f);
    e2.noteOn(1, 60, 1.0f);
    for (int at = 0; at < 24000; at += 256) {
        const int n = std::min(256, 24000 - at);
        e2.setSidechain(scin.data(), scin.data(), 24000);
        e2.render(l2.data(), r2.data(), at, n);
    }
    CHECK_MSG(rms(l2, 6000, 11000) > 0.3 && rms(l2, 18000, 24000) < 0.15, rms(l2, 6000, 11000) << " " << rms(l2, 18000, 24000));
}
TEST(engine_tempo_pump_follows_the_beat_clock_even_when_stopped) {
    Patch p = sineTablePatch();
    FxModule pump = makeFx(FxType::pump); pump.rack = 0; pump.position = 0;
    setParam(pump, fx::uTrigger, fx::pumpTempo); setParam(pump, fx::uDepth, 100); setParam(pump, fx::uBeats, 1); setParam(pump, fx::uAttack, 1);
    p.fx = {pump};
    SynthEngine e; e.prepare(sr); e.setSafetyLimiter(false); e.setPatch(&p);
    std::vector<float> l(72000, 0.0f), r(72000, 0.0f);
    e.noteOn(1, 60, 1.0f);
    for (int at = 0; at < 72000; at += 512) {
        e.setTransport(120.0, 0.0, false);       // host stopped: position constant, clock must still advance
        e.render(l.data(), r.data(), at, std::min(512, 72000 - at));
    }
    // energy per 24000-sample beat should dip near each beat start
    const double a = rms(l, 24000, 25500), b = rms(l, 33000, 36000), c = rms(l, 48000, 49500);
    CHECK_MSG(a < b * 0.5 && c < b * 0.5, a << " " << b << " " << c);
}

int main() {
    int failed = 0;
    for (auto& [name, fn] : registry()) {
        try { fn(); std::cout << "[ ok ] " << name << '\n'; }
        catch (const Failure& f) { ++failed; std::cout << "[FAIL] " << name << " :: " << f.what << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "[FAIL] " << name << " :: exception " << e.what() << '\n'; }
    }
    std::cout << (registry().size() - std::size_t(failed)) << "/" << registry().size() << " DSP tests passed\n";
    return failed ? 1 : 0;
}
