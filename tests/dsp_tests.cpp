// DSP unit and integration tests for the ZYG synthesis engine (filters, FX, modulators,
// oscillators, matrix, voice handling, arp/clips, Serum adapter and native serialization).
#include "Assets.h"
#include "SerumImporter.h"
#include "SampleSerialization.h"
#include "SynthEngine.h"
#include "Wavetable.h"
#include "dsp/Filters.h"
#include "dsp/FxEngine.h"
#include "dsp/CompressorCurve.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillators.h"
#include "dsp/Sequencer.h"
#include "dsp/Spectral.h"
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <fstream>
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
Stereo render(Patch& p, int note, int samples, float velocity = 1.0f, double releaseAt = -1.0, bool limiter = false, double bpm = 120.0) {
    SynthEngine e; e.prepare(sr); e.setSafetyLimiter(limiter); e.setPatch(&p);
    e.setTransport(bpm, 0.0, false);
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
    setup(FilterResponse::ladder24, 1000.0, 0.0); const double unresonant = sineGain(f, 900.0, 24000, 0.02);
    setup(FilterResponse::ladder24, 1000.0, 0.9); const double lr = sineGain(f, 900.0, 24000, 0.02);
    CHECK_MSG(lr > 1.25 && lr > 3.0 * unresonant, "ladder resonant gain " << lr << " vs " << unresonant);
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

TEST(ladder_high_cutoff_subcritical_feedback_decays_after_input_stops) {
    // These moderate resonance values must not excite a persistent ultrasonic
    // limit cycle. Nonzero bounded output alone did not catch this regression.
    for (double sampleRate : {44100.0, 48000.0, 96000.0})
    for (auto type : {FilterResponse::ladder12, FilterResponse::ladderEms})
    for (double cutoff : {1000.0, 10000.0, 20000.0}) {
        dsp::FilterCore f; f.prepare(sampleRate);
        dsp::FilterParams p; p.type = type;
        p.cutoffHzL = p.cutoffHzR = cutoff;
        p.reso = type == FilterResponse::ladderEms ? 0.2586 : 0.1;
        p.var = type == FilterResponse::ladderEms ? 0.59 : 0.943;
        p.drive = 0.11; f.update(p);
        double tailEnergy = 0.0, initialPeak = 0.0;
        for (int i = 0; i < int(sampleRate); ++i) {
            double l = i == 0 ? 0.1 : 0.0, r = l;
            f.process(l, r);
            CHECK(std::isfinite(l) && std::isfinite(r));
            initialPeak = std::max(initialPeak, std::abs(l));
            if (i >= int(sampleRate * 0.75)) tailEnergy += l * l + r * r;
        }
        CHECK(initialPeak > 1.0e-5);
        CHECK_MSG(tailEnergy < 1.0e-14, "persistent ladder tail: type " << int(type)
            << " sample rate " << sampleRate << " cutoff " << cutoff << " energy " << tailEnergy);
    }
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
FxRun runFx(FxModule& m, const std::vector<double>& in, bool stereoDifferent = false, double* override_ = nullptr, double bpm = 120.0) {
    auto unit = dsp::makeFxUnit(m.fxType); unit->prepare(sr); unit->reset();
    dsp::FxContext context; context.bpm = bpm; unit->ctx = &context;
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

FxModule coherentComp() {
    auto m = makeFx(FxType::comp); applyFxDefaults(m);
    setParam(m, fx::pMultiband, 1);
    for (int b = 0; b < 3; ++b) {
        setParam(m, fx::pRatio0 + b, 0); setParam(m, fx::pRatioBelow0 + b, 0);
    }
    return m;
}
TEST(multiband_neutral_recombination_is_flat_at_all_parallel_mix_settings) {
    // DFT of an original impulse measures the entire linear transfer, without
    // a single-tone AGC hiding frequency-dependent cancellation.
    for (double sampleRate : {44100.0,48000.0,96000.0})
    for (auto crossover : {std::array<double,2>{200,2500},std::array<double,2>{800,1600}}) {
        auto m = coherentComp();
        setParam(m,fx::pXoverLow,crossover[0]); setParam(m,fx::pXoverHi,crossover[1]);
        std::vector<double> reference;
        for (double wet : {0.0,50.0,100.0}) {
            setParam(m,fx::pWet,wet);
            auto unit = dsp::makeFxUnit(FxType::comp); unit->prepare(sampleRate);
            std::vector<double> l(16384), r(16384); l[0]=r[0]=1;
            unit->process(m,m.p.data(),l.data(),r.data(),int(l.size()));
            if (reference.empty()) reference = l;
            for (std::size_t i=0;i<l.size();++i) CHECK_NEAR(l[i],reference[i],1e-13);
            for (double frequency : {40.0,200.0,500.0,800.0,1600.0,2500.0,8000.0,20000.0}) {
                double real=0,imag=0;
                for (std::size_t i=0;i<l.size();++i) {
                    const double phase=6.283185307179586*frequency*double(i)/sampleRate;
                    real+=l[i]*std::cos(phase); imag-=l[i]*std::sin(phase);
                }
                CHECK_NEAR(std::sqrt(real*real+imag*imag),1,1e-8);
            }
        }
    }
}
TEST(multiband_makeup_and_steady_compression_follow_the_requested_transfer) {
    auto m = coherentComp(); std::vector<double> dc(48000,0.1);
    setParam(m,fx::pMakeup,2);
    CHECK_NEAR(runFx(m,dc).l.back(),0.2,1e-8);
    setParam(m,fx::pMakeup,1);
    for(int b=0;b<3;++b) setParam(m,fx::pRatio0+b,0.75);
    for(double inputDb : {-40.0,-20.0,-6.0}) {
        std::fill(dc.begin(),dc.end(),std::pow(10.0,inputDb/20));
        const double outputDb = 20*std::log10(runFx(m,dc).l.back());
        CHECK_NEAR(outputDb,inputDb>-30 ? -30+(inputDb+30)/4 : inputDb,1e-5);
    }
}
TEST(multiband_upward_gain_is_continuous_and_fades_out_of_silence) {
    auto m = coherentComp();
    for(int b=0;b<3;++b) setParam(m,fx::pRatioBelow0+b,0.9);
    double previousDb = -200;
    for(double inputDb : {-110.0,-100.0,-90.0,-80.0,-70.001,-69.999,-60.0}) {
        std::vector<double> dc(48000,std::pow(10.0,inputDb/20));
        const auto out=runFx(m,dc);
        const double outputDb=20*std::log10(out.l.back());
        CHECK(outputDb>previousDb);
        if(inputDb<=-100) CHECK_NEAR(outputDb,inputDb,1e-5);
        if(inputDb==-69.999) CHECK_NEAR(outputDb-previousDb,0.002,1e-4);
        previousDb=outputDb;
    }
    CHECK_NEAR(rmsd(runFx(m,std::vector<double>(48000,0)).l),0,0);
}
TEST(multiband_output_controls_smooth_without_changing_stereo_linking_or_allocating) {
    auto m=coherentComp(); auto unit=dsp::makeFxUnit(FxType::comp); unit->prepare(sr);
    std::vector<double> left(48000,0.1),right(48000,-0.025);
    unit->process(m,m.p.data(),left.data(),right.data(),24000);
    setParam(m,fx::pMakeup,2);
    const long before=g_allocations.load(); g_countAllocations=true;
    unit->process(m,m.p.data(),left.data()+24000,right.data()+24000,24000);
    g_countAllocations=false; CHECK(g_allocations.load()==before);
    CHECK_NEAR(left.back(),0.2,1e-8);
    for(std::size_t i=1;i<left.size();++i) {
        CHECK_NEAR(right[i],-0.25*left[i],1e-12);
        if(i>=24000) CHECK(std::abs(left[i]-left[i-1])<0.0011);
    }
}
TEST(multiband_state_migration_preserves_historical_native_projects) {
    Patch p; p.fx={coherentComp()};
    auto state=patchToJson(p);
    CHECK(patchFromJson(state).fx[0].phaseCoherentMultiband);
    state["fx"][0].erase("phaseCoherentMultiband");
    auto restored=patchFromJson(state).fx[0];
    CHECK(!restored.phaseCoherentMultiband);
    setParam(restored,fx::pMakeup,2);
    const std::vector<double> dc(192000,0.1); // settle the historical 400 ms AGC
    CHECK_NEAR(runFx(restored,dc).l.back(),0.1,1e-6); // old capped makeup/AGC
    p.fx={restored}; CHECK(!patchFromJson(patchToJson(p)).fx[0].phaseCoherentMultiband);
}
TEST(multiband_static_and_automated_processing_is_independent_of_host_block_size) {
    auto m=coherentComp();
    auto generate=[&](int block) {
        auto unit=dsp::makeFxUnit(FxType::comp); unit->prepare(sr);
        std::vector<double> l(12000),r(12000);
        for(int i=0;i<12000;++i) l[std::size_t(i)]=r[std::size_t(i)]=0.1*std::sin(6.283185307179586*800*i/sr);
        for(int at=0;at<12000;) {
            const int next=(at/1000+1)*1000;
            setParam(m,fx::pWet,(at/1000)%2 ? 25:100);
            setParam(m,fx::pGain1,(at/1000)%2 ? -12:6);
            const int n=std::min({block,12000-at,next-at});
            unit->process(m,m.p.data(),l.data()+at,r.data()+at,n); at+=n;
        }
        return l;
    };
    const auto reference=generate(1);
    for(int block : {17,137,512}) {
        const auto out=generate(block);
        for(std::size_t i=0;i<out.size();++i) CHECK_NEAR(out[i],reference[i],1e-12);
    }
}
FxRun runRack(Patch& patch, const std::vector<double>& left, const std::vector<double>& right, double bpm = 120.0) {
    dsp::FxEngine engine; engine.prepare(sr); engine.bind(&patch);
    dsp::FxContext context; context.bpm = bpm; engine.setContext(context);
    FxRun out{left, right};
    engine.processRack(0, out.l.data(), out.r.data(), int(out.l.size()));
    return out;
}
FxModule invertFx(int position) {
    auto m = makeFx(FxType::utils); m.position = position;
    setParam(m, fx::uPolarityL, 1); setParam(m, fx::uPolarityR, 1);
    return m;
}
double rackDifference(const FxRun& a, const FxRun& b) {
    double difference = 0.0;
    for (std::size_t i = 0; i < a.l.size(); ++i)
        difference = std::max({difference, std::abs(a.l[i] - b.l[i]), std::abs(a.r[i] - b.r[i])});
    return difference;
}
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
TEST(delay_sync_grid_centers_boundaries_and_default_match_measured_divisions) {
    constexpr double beats[] = {1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16, 1.0 / 8, 1.0 / 4,
        1.0 / 2, 1, 2, 4, 8, 16};
    auto physical = [](double normalized) { return 0.001 + 0.5 * std::pow(normalized, 4); };
    for (int i = 0; i < 12; ++i) CHECK_NEAR(dsp::delayGridBeats(physical(double(i) / 11)), beats[i], 1e-12);
    for (int i = 0; i < 11; ++i) {
        const double boundary = (double(i) + 0.5) / 11;
        CHECK_NEAR(dsp::delayGridBeats(physical(boundary - 1e-7)), beats[i], 1e-12);
        CHECK_NEAR(dsp::delayGridBeats(physical(boundary + 1e-7)), beats[i + 1], 1e-12);
    }
    CHECK_NEAR(dsp::delayGridBeats(0.0772939453125), 1.0, 1e-12);
    CHECK_NEAR(dsp::delayGridBeats(-1), beats[0], 1e-12);
    CHECK_NEAR(dsp::delayGridBeats(3), beats[11], 1e-12);
}
TEST(delay_sync_converts_tempo_then_offsets_then_link_and_preserves_legacy_seconds) {
    auto m = makeFx(FxType::delay); m.delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    setParam(m, fx::dTimeL, 0.0772939453125); setParam(m, fx::dTimeR, 0.501);
    setParam(m, fx::dBeatSync, 1); setParam(m, fx::dOffsetL, 0.75); setParam(m, fx::dOffsetR, 1.25);
    double parameters[fxParamSlots]; effective(m, parameters);
    const auto fast = dsp::delayTimeTargets(m, parameters, 120), slow = dsp::delayTimeTargets(m, parameters, 60);
    CHECK_NEAR(fast[0], 0.375, 1e-12); CHECK_NEAR(fast[1], 10.0, 1e-12);
    CHECK_NEAR(slow[0], fast[0] * 2, 1e-12); CHECK_NEAR(slow[1], fast[1] * 2, 1e-12);
    parameters[fx::dLink] = 1;
    const auto linked = dsp::delayTimeTargets(m, parameters, 120);
    CHECK_NEAR(linked[0], fast[0], 1e-12); CHECK_NEAR(linked[1], fast[0], 1e-12);
    parameters[fx::dBeatSync] = 0;
    const auto free = dsp::delayTimeTargets(m, parameters, 120), freeSlow = dsp::delayTimeTargets(m, parameters, 60);
    CHECK_NEAR(free[0], (0.0772939453125 - 0.001) * 0.75, 1e-12); CHECK(free == freeSlow);
    parameters[fx::dBeatSync] = 1;
    m.delayTimingModel = DelayTimingModel::seconds;
    CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 120)[0], 0.0772939453125 * 0.75, 1e-12);
    parameters[fx::dTimeL] = 10;
    CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 120)[0], 2.5, 1e-12);
}
TEST(delay_free_timing_measured_knots_offsets_and_zero_endpoint) {
    auto m = makeFx(FxType::delay); m.delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    double parameters[fxParamSlots]; effective(m, parameters);
    constexpr double stored[] = {0.001, 0.005, 0.02, 0.1, 0.25, 0.501};
    constexpr double seconds[] = {0.0, 0.004, 0.019, 0.099, 0.249, 0.5};
    for (std::size_t i = 0; i < std::size(stored); ++i) {
        parameters[fx::dTimeL] = parameters[fx::dTimeR] = stored[i];
        CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 120)[0], seconds[i], 1e-12);
        parameters[fx::dOffsetL] = 0.5; parameters[fx::dOffsetR] = 1.5;
        const auto targets = dsp::delayTimeTargets(m, parameters, 120);
        CHECK_NEAR(targets[0], seconds[i] * 0.5, 1e-12); CHECK_NEAR(targets[1], seconds[i] * 1.5, 1e-12);
        parameters[fx::dOffsetL] = parameters[fx::dOffsetR] = 1.0;
    }
    parameters[fx::dTimeL] = -1; parameters[fx::dTimeR] = 10;
    CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 120)[0], 0, 1e-12);
    CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 120)[1], 0.5, 1e-12);
}
TEST(delay_sync_impulse_repeat_follows_host_tempo_while_free_timing_does_not) {
    auto m = makeFx(FxType::delay); m.delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    setParam(m, fx::dTimeL, 0.0772939453125); setParam(m, fx::dTimeR, 0.0772939453125);
    setParam(m, fx::dBeatSync, 1); setParam(m, fx::dWet, 100); setParam(m, fx::dFeedback, 0);
    std::vector<double> impulse(50000, 0); impulse[0] = 0.1;
    auto maximumAt = [](const FxRun& out) { return int(std::max_element(out.l.begin(), out.l.end(), [](double a, double b) { return std::abs(a) < std::abs(b); }) - out.l.begin()); };
    CHECK_NEAR(maximumAt(runFx(m, impulse, false, nullptr, 120)), 24000, 40);
    CHECK_NEAR(maximumAt(runFx(m, impulse, false, nullptr, 60)), 48000, 40);
    setParam(m, fx::dBeatSync, 0);
    CHECK_NEAR(rackDifference(runFx(m, impulse, false, nullptr, 60), runFx(m, impulse, false, nullptr, 120)), 0, 1e-12);
}
TEST(delay_time_modulation_moves_in_fourth_power_knob_coordinates_without_allocating) {
    auto m = makeFx(FxType::delay); m.delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    setParam(m, fx::dTimeL, 0.0772939453125); setParam(m, fx::dTimeR, 0.0772939453125);
    setParam(m, fx::dBeatSync, 1); setParam(m, fx::dWet, 100); setParam(m, fx::dFeedback, 0);
    Patch p; p.fx = {m};
    Patch expected = p;
    for (const int slot : {fx::dTimeL, fx::dTimeR}) setParam(expected.fx[0], slot, 0.001 + 0.5 * std::pow(0.725, 4));
    std::vector<double> input(50000, 0); input[0] = 0.1;
    const auto reference = runRack(expected, input, input);
    dsp::FxEngine engine; engine.prepare(sr); engine.bind(&p);
    engine.modulationOffsets()[fx::dTimeL] = engine.modulationOffsets()[fx::dTimeR] = 0.1;
    FxRun output{input, input};
    g_allocations = 0; g_countAllocations = true;
    engine.processRack(0, output.l.data(), output.r.data(), int(input.size()));
    g_countAllocations = false;
    CHECK(g_allocations.load() == 0);
    CHECK_NEAR(rackDifference(output, reference), 0, 1e-12);
    CHECK_NEAR(dsp::delayGridBeats(applyModulation(0.0772939453125, 0.1, dsp::delayTimeControlRange)), 2, 1e-12);
    for (int i = 0; i < 47900; ++i) CHECK_NEAR(output.l[std::size_t(i)], 0, 1e-12);
}
TEST(delay_tempo_change_and_long_time_cap_use_prepared_storage) {
    auto m = makeFx(FxType::delay); m.delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    setParam(m, fx::dTimeL, 0.0772939453125); setParam(m, fx::dTimeR, 0.0772939453125);
    setParam(m, fx::dBeatSync, 1); setParam(m, fx::dWet, 100); setParam(m, fx::dFeedback, 0);
    double parameters[fxParamSlots]; effective(m, parameters);
    auto unit = dsp::makeFxUnit(FxType::delay); unit->prepare(1000);
    dsp::FxContext context; context.bpm = 120; unit->ctx = &context;
    std::vector<double> left(6000, 0), right(6000, 0); left[3000] = right[3000] = 0.1;
    unit->process(m, parameters, left.data(), right.data(), 1000);
    context.bpm = 60;
    unit->process(m, parameters, left.data() + 1000, right.data() + 1000, 5000);
    const auto peakAt = std::max_element(left.begin(), left.end(), [](double a, double b) { return std::abs(a) < std::abs(b); }) - left.begin();
    CHECK_NEAR(double(peakAt), 4000, 30);
    unit->reset(); context.bpm = 30;
    parameters[fx::dTimeL] = parameters[fx::dTimeR] = 0.501;
    parameters[fx::dOffsetL] = parameters[fx::dOffsetR] = 1.5;
    CHECK_NEAR(dsp::delayTimeTargets(m, parameters, 30)[0], dsp::maxDelaySeconds, 1e-12);
    left.assign(32064, 0); right.assign(32064, 0); left[0] = right[0] = 0.1;
    g_allocations = 0; g_countAllocations = true;
    unit->process(m, parameters, left.data(), right.data(), int(left.size()));
    g_countAllocations = false;
    CHECK(g_allocations.load() == 0);
    for (int i = 0; i < 31990; ++i) CHECK_NEAR(left[std::size_t(i)], 0, 1e-12);
    CHECK(std::abs(*std::max_element(left.begin() + 31990, left.end(), [](double a, double b) { return std::abs(a) < std::abs(b); })) > 1e-5);
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
TEST(time_fx_full_wet_excludes_the_undelayed_attack) {
    std::vector<double> in(24000, 0.0); in[0] = 0.5;
    auto delay = makeFx(FxType::delay);
    setParam(delay, fx::dTimeL, 0.1); setParam(delay, fx::dTimeR, 0.1);
    setParam(delay, fx::dWet, 100); setParam(delay, fx::dFeedback, 0);
    auto echo = runFx(delay, in);
    for (int i = 0; i < 4700; ++i) CHECK_NEAR(echo.l[std::size_t(i)], 0.0, 1e-12);
    delay.legacyTimeFx = true;
    CHECK_NEAR(runFx(delay, in).l[0], 0.25, 1e-12);
    auto reverb = makeFx(FxType::reverb);
    setParam(reverb, fx::rWet, 100); setParam(reverb, fx::rPreDelay, 0.1);
    auto tail = runFx(reverb, in);
    for (int i = 0; i < 4700; ++i) CHECK_NEAR(tail.l[std::size_t(i)], 0.0, 1e-12);
}
TEST(delay_first_repeat_obeys_its_filter) {
    auto delay = makeFx(FxType::delay);
    setParam(delay, fx::dTimeL, 0.1); setParam(delay, fx::dTimeR, 0.1);
    setParam(delay, fx::dWet, 100); setParam(delay, fx::dFeedback, 0);
    setParam(delay, fx::dFreq, 2000); setParam(delay, fx::dBW, 0.75);
    std::vector<double> in(48000, 0.0);
    for (int i = 0; i < 48000; ++i) in[std::size_t(i)] = 0.05 * std::sin(dsp::tau * 65.0 * i / sr);
    const auto filtered = runFx(delay, in);
    delay.legacyTimeFx = true;
    const auto unfiltered = runFx(delay, in);
    CHECK(rmsd(filtered.l, 24000) < rmsd(unfiltered.l, 24000) * 0.05);
}
TEST(reverb_decay_control_does_not_add_pre_delay) {
    auto reverb = makeFx(FxType::reverb);
    setParam(reverb, fx::rWet, 100); setParam(reverb, fx::rPreDelay, 0);
    std::vector<double> in(48000, 0.0); in[0] = 0.1;
    auto first = [](const FxRun& out) {
        for (std::size_t i = 0; i < out.l.size(); ++i)
            if (std::abs(out.l[i]) + std::abs(out.r[i]) > 1e-8) return i;
        return out.l.size();
    };
    setParam(reverb, fx::rDelay, 0.14172336128867677); // measured 1 s display
    const auto shortTail = runFx(reverb, in);
    setParam(reverb, fx::rDelay, 119.18933848404833); // measured 8 s display
    const auto longTail = runFx(reverb, in);
    CHECK(first(shortTail) == first(longTail));
    CHECK(rmsd(longTail.l, 36000) > rmsd(shortTail.l, 36000) * 2.0);
}
TEST(reverb_modulation_has_no_common_phase_wrap_step) {
    auto reverb = makeFx(FxType::reverb);
    setParam(reverb, fx::rWet, 100); setParam(reverb, fx::rMode, 100);
    setParam(reverb, fx::rDelay, 119.18933848404833);
    std::vector<double> in(160000, 0.0);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.1 * std::sin(dsp::tau * 40.0 * double(i) / sr);
    const auto out = runFx(reverb, in);
    const int boundary = int(sr / 0.37);
    double worst = 0.0;
    for (int i = boundary - 8; i < boundary + 8; ++i)
        worst = std::max({worst, std::abs(out.l[std::size_t(i)] - out.l[std::size_t(i - 1)]),
            std::abs(out.r[std::size_t(i)] - out.r[std::size_t(i - 1)])});
    CHECK_MSG(worst < 0.002, "reverb phase wrap step " << worst);
}
TEST(time_fx_model_and_legacy_native_state_round_trip) {
    Patch p; p.fx = {makeFx(FxType::delay), makeFx(FxType::reverb)};
    p.fx[0].delayTimingModel = DelayTimingModel::fourthPowerBeatGrid;
    auto state = patchToJson(p);
    const auto modern = patchFromJson(state);
    CHECK(!modern.fx[0].legacyTimeFx && !modern.fx[1].legacyTimeFx);
    CHECK(modern.fx[0].delayTimingModel == DelayTimingModel::fourthPowerBeatGrid);
    state["fx"][0].erase("delayTimingModel");
    const auto previousModern = patchFromJson(state);
    CHECK(!previousModern.fx[0].legacyTimeFx && previousModern.fx[0].delayTimingModel == DelayTimingModel::seconds);
    for (auto& fx : state["fx"]) { fx.erase("legacyTimeFx"); fx.erase("delayTimingModel"); }
    const auto old = patchFromJson(state);
    CHECK(old.fx[0].legacyTimeFx && old.fx[1].legacyTimeFx);
    CHECK(old.fx[0].delayTimingModel == DelayTimingModel::seconds);
    CHECK(patchFromJson(patchToJson(old)).fx[0].legacyTimeFx);
    CHECK_NEAR(old.fx[1].p[fx::rPreDelay], 0.02, 1e-12);
    CHECK_NEAR(old.fx[1].p[fx::rFreq], 70, 1e-12);
}
TEST(saved_content_root_accepts_utf8_and_rejects_stale_or_invalid_folders) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("zygzxg-content-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto content = root / fs::u8path("contenu-\xc3\xa9");
    const auto setting = root / "content-root.txt";
    fs::create_directories(content / "Tables");
    { std::ofstream out(setting, std::ios::binary); const auto utf8 = content.u8string();
      out << "\xef\xbb\xbf"; out.write(reinterpret_cast<const char*>(utf8.data()), std::streamsize(utf8.size())); out << "\r\n"; }
    CHECK(readContentRootSetting(setting) == content);
    fs::remove(content / "Tables");
    CHECK(readContentRootSetting(setting).empty());
    { std::ofstream out(setting); out << "relative-content\n"; }
    CHECK(readContentRootSetting(setting).empty());
    fs::remove(setting); fs::remove(content); fs::remove(root);
    CHECK(readContentRootSetting(setting).empty());
}
TEST(reverb_low_and_high_cut_controls_have_the_documented_direction) {
    auto m = makeFx(FxType::reverb);
    setParam(m, fx::rWet, 100); setParam(m, fx::rFreq, 0); setParam(m, fx::rFreqB, 0);
    std::vector<double> in(48000, 0.0);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = 0.05 * std::sin(dsp::tau * 2000.0 * double(i) / sr);
    const double open = rmsd(runFx(m, in).l, 24000);
    setParam(m, fx::rFreqB, 100);
    CHECK(rmsd(runFx(m, in).l, 24000) < open * 0.05);
    setParam(m, fx::rFreqB, 0); setParam(m, fx::rFreq, 100);
    CHECK(rmsd(runFx(m, in).l, 24000) < open * 0.25);
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
TEST(imported_eq_modes_defaults_and_native_migration_preserve_raw_state) {
    SerumDocument d;d.metadata={{"presetName","synthetic"}};
    d.state["FXRack0"]["FX"]=Json::array({{{"FXEQ",{{"plainParams",{{"kParamGain1",12.0}}}}}}});
    const auto p=importSerum(d,{});const auto& m=p.fx[0];
    CHECK(m.eqMeasuredModes&&m.parameters["kParamGain1"]==12.0&&p.unknownSerumState==d.state);
    CHECK_NEAR(m.p[fx::eType1],0,0);CHECK_NEAR(m.p[fx::eType2],0,0);
    CHECK_NEAR(m.p[fx::eReso1],60,0);CHECK_NEAR(m.p[fx::eFreq1],209.616962917974,1e-10);
    auto j=patchToJson(p);CHECK(patchFromJson(j).fx[0].eqMeasuredModes);
    j["fx"][0].erase("eqMeasuredModes");auto old=patchFromJson(j).fx[0];CHECK(!old.eqMeasuredModes&&!FxModule{}.eqMeasuredModes);
    auto legacy=m;legacy.eqMeasuredModes=false;
    std::vector<double> signal(24000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=.01*std::sin(6.283185307179586*100*i/sr);
    CHECK(runFx(old,signal).l==runFx(legacy,signal).l);
}

TEST(imported_eq_shelf_bell_and_cut_follow_original_reference_tones) {
    auto m=makeFx(FxType::eq);m.eqMeasuredModes=true;
    setParam(m,fx::eFreq1,1000);setParam(m,fx::eReso1,50);setParam(m,fx::eGain1,12);
    const double frequencies[]={100,500,1000,2000,10000};
    const double measured[3][5]={{11.95731,10.46370,5.99877,1.52216,.02884},
        {.09786,2.88206,11.99876,2.86091,.07113},{-39.96561,-10.60576,2.15143,1.44536,.04282}};
    std::vector<double> signal(24000);
    for(int mode=0;mode<3;++mode){setParam(m,fx::eType1,mode);
        for(int f=0;f<5;++f){
            for(std::size_t i=0;i<signal.size();++i)signal[i]=.01*std::sin(6.283185307179586*frequencies[f]*i/sr);
            const double gain=20*std::log10(rmsd(runFx(m,signal).l,12000)/rmsd(signal,12000));
            CHECK_NEAR(gain,measured[mode][f],.02);
        }
    }
    const auto cut=runFx(m,signal).l;setParam(m,fx::eGain1,-24);CHECK(runFx(m,signal).l==cut);
}

TEST(imported_eq_engine_retains_zero_mode_and_correct_high_band_direction) {
    Patch patch;auto m=makeFx(FxType::eq);m.eqMeasuredModes=true;
    setParam(m,fx::eType2,0);setParam(m,fx::eFreq2,1000);setParam(m,fx::eReso2,50);setParam(m,fx::eGain2,12);
    patch.fx={m};dsp::FxEngine engine;engine.prepare(sr);engine.bind(&patch);
    std::vector<double> signal(24000),l,r;
    for(double frequency:{100.,10000.}){
        for(std::size_t i=0;i<signal.size();++i)signal[i]=.01*std::sin(6.283185307179586*frequency*i/sr);
        l=signal;r=signal;engine.reset();
        for(int i=0;i<24000;i+=200)engine.processRack(0,l.data()+i,r.data()+i,200);
        const double gain=20*std::log10(rmsd(l,12000)/rmsd(signal,12000));
        CHECK_NEAR(gain,frequency==100?.04022:11.96868,.02);
    }
}

TEST(imported_eq_modes_resonance_and_frequency_changes_use_prepared_storage) {
    auto m=makeFx(FxType::eq);m.eqMeasuredModes=true;
    for(double rate:{44100.,48000.,96000.}){
        auto unit=dsp::makeFxUnit(FxType::eq);unit->prepare(rate);double p[fxParamSlots];effective(m,p);
        double l[64],r[64];double maxOutput=0;bool finite=true;g_allocations=0;g_countAllocations=true;
        for(int j=0;j<1200;++j){
            p[fx::eType1]=p[fx::eType2]=(j/200)%3;p[fx::eReso1]=p[fx::eReso2]=(j%2)?0:100;
            p[fx::eFreq1]=p[fx::eFreq2]=(j%2)?20:18000;p[fx::eGain1]=12;p[fx::eGain2]=-12;
            for(int i=0;i<64;++i)l[i]=r[i]=.01*std::sin(6.283185307179586*1000*(j*64+i)/rate);
            unit->process(m,p,l,r,64);
            for(int i=0;i<64;++i){finite=finite&&std::isfinite(l[i])&&l[i]==r[i];maxOutput=std::max(maxOutput,std::abs(l[i]));}
        }
        g_countAllocations=false;CHECK(g_allocations==0&&finite);CHECK_MSG(maxOutput<2.0,"rapid EQ automation peak "<<maxOutput);
    }
}

TEST(imported_eq_control_slew_is_independent_of_process_partition) {
    auto m=makeFx(FxType::eq);m.eqMeasuredModes=true;
    auto process=[&](int block){
        auto unit=dsp::makeFxUnit(FxType::eq);unit->prepare(sr);double p[fxParamSlots];effective(m,p);
        auto l=noise(8192);auto r=l;
        for(int at=0;at<8192;){
            const int boundary=at<2048?2048:at<4096?4096:8192;
            if(at==0){p[fx::eType1]=0;p[fx::eFreq1]=100;p[fx::eGain1]=12;p[fx::eReso1]=100;}
            if(at==2048){p[fx::eType1]=1;p[fx::eFreq1]=10000;p[fx::eGain1]=-12;p[fx::eReso1]=0;}
            if(at==4096){p[fx::eType1]=2;p[fx::eFreq1]=1000;p[fx::eReso1]=75;}
            const int n=std::min(block,boundary-at);unit->process(m,p,l.data()+at,r.data()+at,n);at+=n;
        }
        return l;
    };
    const auto reference=process(64);
    for(int block:{17,63,256})CHECK(process(block)==reference);
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

TEST(fx_whole_splitter_bypass_skips_children_and_keeps_following_effect) {
    const auto left = noise(1025), right = noise(1025, 19);
    for (auto type : {FxType::split, FxType::split3, FxType::splitMS}) {
        auto split = makeFx(type); split.enabled = false;
        setParam(split, fx::sCount1, 1);
        Patch p; p.fx = {split, invertFx(1)};
        CHECK_NEAR(rackDifference(runRack(p, left, right), FxRun{left, right}), 0.0, 1e-12);
        p.fx.push_back(invertFx(2));
        Patch serial; serial.fx = {invertFx(0)};
        CHECK_NEAR(rackDifference(runRack(p, left, right), runRack(serial, left, right)), 0.0, 1e-12);
    }
}

TEST(fx_branch_bypass_retains_every_band_and_uses_modulation_offsets) {
    const auto left = noise(1025), right = noise(1025, 19);
    for (auto type : {FxType::split, FxType::split3, FxType::splitMS}) {
        const int bands = type == FxType::split3 ? 3 : 2;
        for (int band = 0; band < bands; ++band) {
            auto split = makeFx(type);
            setParam(split, fx::sFreq, 500); setParam(split, fx::sFreq2, 3000);
            for (int b = 0; b < bands; ++b) setParam(split, fx::sCount1 + b, 1);
            Patch active; active.fx = {split};
            for (int b = 0; b < bands; ++b) active.fx.push_back(invertFx(b + 1));
            Patch bypassed = active; setParam(bypassed.fx[0], fx::sBypass1 + band, 1);
            Patch expected = active; expected.fx[std::size_t(band + 1)].enabled = false;
            const auto reference = runRack(expected, left, right);
            CHECK_NEAR(rackDifference(runRack(bypassed, left, right), reference), 0.0, 1e-12);
            CHECK(rackDifference(runRack(active, left, right), reference) > 1e-3);
            dsp::FxEngine engine; engine.prepare(sr); engine.bind(&active);
            engine.modulationOffsets()[fx::sBypass1 + band] = 1.0;
            FxRun modulated{left, right};
            engine.processRack(0, modulated.l.data(), modulated.r.data(), int(left.size()));
            CHECK_NEAR(rackDifference(modulated, reference), 0.0, 1e-12);
        }
        Patch empty; empty.fx = {makeFx(type)};
        Patch allBypassed; allBypassed.fx = {makeFx(type)};
        for (int b = 0; b < bands; ++b) {
            setParam(allBypassed.fx[0], fx::sCount1 + b, 1);
            setParam(allBypassed.fx[0], fx::sBypass1 + b, 1);
            allBypassed.fx.push_back(invertFx(b + 1));
        }
        CHECK_NEAR(rackDifference(runRack(allBypassed, left, right), runRack(empty, left, right)), 0.0, 1e-12);
    }
}

TEST(fx_nested_splitter_bypass_and_depth_fallback_preserve_subtree_boundaries) {
    const auto left = noise(1025), right = noise(1025, 19);
    auto outer = makeFx(FxType::splitMS); setParam(outer, fx::sCount1, 2); setParam(outer, fx::sCount2, 1);
    auto inner = makeFx(FxType::splitMS); inner.position = 1; inner.enabled = false; setParam(inner, fx::sCount1, 1);
    Patch p; p.fx = {outer, inner, invertFx(2), invertFx(3), invertFx(4)};
    auto reducedOuter = outer; setParam(reducedOuter, fx::sCount1, 0);
    Patch expected; expected.fx = {reducedOuter, invertFx(1), invertFx(2)};
    CHECK_NEAR(rackDifference(runRack(p, left, right), runRack(expected, left, right)), 0.0, 1e-12);
    p.fx[0].enabled = false;
    Patch serial; serial.fx = {invertFx(0)};
    CHECK_NEAR(rackDifference(runRack(p, left, right), runRack(serial, left, right)), 0.0, 1e-12);
    Patch deep;
    for (int d = 0; d < 3; ++d) {
        auto s = makeFx(FxType::splitMS); s.position = d; setParam(s, fx::sCount1, 3 - d); deep.fx.push_back(s);
    }
    deep.fx.push_back(invertFx(3)); deep.fx.push_back(invertFx(4));
    CHECK_NEAR(rackDifference(runRack(deep, left, right), runRack(serial, left, right)), 0.0, 1e-12);
}

TEST(fx_mid_side_branch_bypass_preserves_pure_mid_and_side_signals) {
    const auto input = noise(513);
    for (int band = 0; band < 2; ++band) {
        auto split = makeFx(FxType::splitMS); setParam(split, fx::sCount1 + band, 1);
        setParam(split, fx::sBypass1 + band, 1);
        Patch p; p.fx = {split, invertFx(1)};
        auto right = input;
        if (band == 1) for (auto& value : right) value = -value;
        CHECK_NEAR(rackDifference(runRack(p, input, right), FxRun{input, right}), 0.0, 1e-12);
    }
    // The fixed splitter-state pool must also bypass a whole subtree when
    // exhausted, without turning its children into serial effects.
    Patch exhausted;
    for (int i = 0; i < 33; ++i) { auto split = makeFx(FxType::splitMS); split.position = i; exhausted.fx.push_back(split); }
    setParam(exhausted.fx.back(), fx::sCount1, 1);
    exhausted.fx.push_back(invertFx(33)); exhausted.fx.push_back(invertFx(34));
    Patch serial; serial.fx = {invertFx(0)};
    CHECK_NEAR(rackDifference(runRack(exhausted, input, input), runRack(serial, input, input)), 0.0, 1e-12);
}

TEST(fx_splitter_counts_clip_each_branch_and_ignore_stale_bindings_without_allocating) {
    const auto left = noise(1025), right = noise(1025, 19);
    auto split = makeFx(FxType::split3);
    for (int b = 0; b < 3; ++b) setParam(split, fx::sCount1 + b, 8);
    Patch malformed; malformed.fx = {split, invertFx(1)};
    Patch clipped = malformed;
    setParam(clipped.fx[0], fx::sCount1, 1); setParam(clipped.fx[0], fx::sCount2, 0); setParam(clipped.fx[0], fx::sCount3, 0);
    const auto expected = runRack(clipped, left, right);
    CHECK_NEAR(rackDifference(runRack(malformed, left, right), expected), 0.0, 1e-12);
    Patch old;
    for (int i = 0; i < 24; ++i) old.fx.push_back(invertFx(i));
    Patch bypassed = clipped; setParam(bypassed.fx[0], fx::sBypass1, 1);
    Patch disabled = clipped; disabled.fx[0].enabled = false;
    dsp::FxEngine engine; engine.prepare(sr); engine.bind(&old);
    FxRun out{left, right};
    g_allocations = 0; g_countAllocations = true;
    engine.bind(&malformed);
    engine.processRack(0, out.l.data(), out.r.data(), int(left.size()));
    engine.bind(&clipped);
    engine.reset();
    engine.processRack(0, out.l.data(), out.r.data(), int(left.size()));
    engine.bind(&bypassed); engine.processRack(0, out.l.data(), out.r.data(), int(left.size()));
    engine.bind(&disabled); engine.processRack(0, out.l.data(), out.r.data(), int(left.size()));
    g_countAllocations = false;
    CHECK(g_allocations.load() == 0);
    // Check output independently of the allocation pass, which repeatedly
    // processes its output while switching patches and bypass modes.
    out = FxRun{left, right}; engine.reset(); engine.bind(&malformed);
    engine.processRack(0, out.l.data(), out.r.data(), int(left.size()));
    CHECK_NEAR(rackDifference(out, expected), 0.0, 1e-12);
    // A nested malformed count must not consume the outer side branch.
    auto outer = makeFx(FxType::splitMS); setParam(outer, fx::sCount1, 2); setParam(outer, fx::sCount2, 1);
    malformed.fx[0].position = 1; malformed.fx[1].position = 2;
    malformed.fx.insert(malformed.fx.begin(), outer); malformed.fx.push_back(invertFx(3));
    clipped.fx[0].position = 1; clipped.fx[1].position = 2;
    clipped.fx.insert(clipped.fx.begin(), outer); clipped.fx.push_back(invertFx(3));
    CHECK_NEAR(rackDifference(runRack(malformed, left, right), runRack(clipped, left, right)), 0.0, 1e-12);
}

// ======================================================================== modulators
TEST(tempo_synced_envelope_follows_host_tempo_and_roundtrips) {
    Patch p = sineTablePatch();
    auto& env = p.envelopes[0];
    env.tempoSync = true; env.attack = 1.0; env.hold = 0.0;
    env.decay = 0.0; env.sustain = 1.0; env.curve[0] = 50.0;
    const auto normal = render(p, 60, 24000, 1.0f, -1.0, false, 120.0);
    const auto slow = render(p, 60, 24000, 1.0f, -1.0, false, 60.0);
    CHECK_NEAR(rms(normal.l, 7200, 9600) / rms(slow.l, 7200, 9600), 2.0, 0.01);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK(restored.envelopes[0].tempoSync);
    CHECK_NEAR(restored.envelopes[0].attack, 1.0, 1.0e-12);
    env.tempoSync = false;
    const auto free120 = render(p, 60, 24000, 1.0f, -1.0, false, 120.0);
    const auto free60 = render(p, 60, 24000, 1.0f, -1.0, false, 60.0);
    CHECK(free120.l == free60.l);
}

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

TEST(exponential_envelope_matches_measured_midpoints) {
    CHECK_NEAR(dsp::envelopeCurveShape(0.5, 25.0, true), 0.0625, 1e-12);
    CHECK_NEAR(dsp::envelopeCurveShape(0.5, 50.0, true), 0.5, 1e-12);
    CHECK_NEAR(dsp::envelopeCurveShape(0.5, 66.6, true), 0.850885, 0.0002);
    CHECK_NEAR(dsp::envelopeCurveShape(0.5, 75.0, true), 0.9375, 1e-12);
    for (double c : {0.0, 25.0, 50.0, 75.0, 100.0}) {
        CHECK_NEAR(dsp::envelopeCurveShape(0.0, c, true), 0.0, 1e-12);
        CHECK_NEAR(dsp::envelopeCurveShape(1.0, c, true), 1.0, 1e-12);
    }
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
TEST(tight_output_decimator_response_and_integer_delay) {
    // Independent frequency-response bounds across the pass/stop bands.
    // Normalization is cycles / high-rate input sample (2x the output rate).
    constexpr double tau=6.2831853071795864769;
    auto gain=[](double f) {
        double re=0,im=0;
        for(std::size_t i=0;i<tightOversamplingFir.size();++i) {
            re+=tightOversamplingFir[i]*std::cos(tau*f*double(i));
            im-=tightOversamplingFir[i]*std::sin(tau*f*double(i));
        }
        return std::hypot(re,im);
    };
    CHECK_NEAR(gain(0),1,1e-12);
    for(int i=0;i<=400;++i) CHECK(std::abs(20*std::log10(gain(.2*i/400)))<.02);
    for(int i=0;i<=1000;++i) CHECK(gain(.25+.25*i/1000)<3.2e-5);
    // Retained sample parity, not just the FIR centre, sets total delay.
    // A ramp's first moment passes through a symmetric unity-DC FIR exactly.
    for(int quality : {0,1}) {
        OversampleState a,b; double y=0;
        for(int n=0;n<400;++n) {
            if(!quality) y=downsample2x(a,n,n+.5,true);
            else {
                const double first=downsample2x(a,n,n+.25,true);
                const double second=downsample2x(a,n+.5,n+.75,true);
                y=downsample2x(b,first,second,true);
            }
        }
        CHECK_NEAR(y,399.0-(quality?tightDecimatorDelay4x:tightDecimatorDelay2x),1e-9);
    }
}
TEST(tight_decimator_preserves_high_note_layer_phase_and_legacy_state) {
    auto p=sineTablePatch(); auto& o=p.oscillators[0];
    dsp::OscValues values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
    for(int quality : {0,1}) for(double hz : {1000.0,8000.0,16000.0}) {
        dsp::OscInputs in;in.osc=&o;in.v=&values;in.sampleRate=sr;in.masterTuning=hz;in.oversampleLevel=quality;
        dsp::OscVoiceState plain,warped;dsp::oscNoteOn(plain,in);dsp::oscNoteOn(warped,in);
        double dot=0,pa=0,pb=0;
        for(int i=0;i<4096;++i) {
            o.warpDefinitions[0].mode=WarpMode::off;const double a=dsp::oscRender(plain,in).l;
            o.warpDefinitions[0].mode=WarpMode::softClip;o.warpOneAmount=0;const double b=dsp::oscRender(warped,in).l;
            if(i>=512) {dot+=a*b;pa+=a*a;pb+=b*b;}
        }
        CHECK_MSG(dot/std::sqrt(pa*pb)>.999999,"layer phase mismatch at "<<hz<<" quality "<<quality);
        o.warpDefinitions[0].mode=WarpMode::off;
    }
    auto j=patchToJson(p);CHECK(patchFromJson(j).globals.tightOutputDecimator);
    j["globals"].erase("tightOutputDecimator");const auto old=patchFromJson(j);
    CHECK(!old.globals.tightOutputDecimator);
    CHECK(!patchFromJson(patchToJson(old)).globals.tightOutputDecimator);
    j.erase("globals");CHECK(!patchFromJson(j).globals.tightOutputDecimator);
}
TEST(output_decimators_match_independent_convolution_without_allocating) {
    std::array<double,1024> input{};
    for(std::size_t n=0;n<input.size();++n) input[n]=std::sin(.834*double(n))+.2*std::cos(2.17*double(n));
    for(bool tight : {false,true}) {
        OversampleState state;double error=0;
        const auto before=g_allocations.load();g_countAllocations=true;
        for(std::size_t n=1;n<input.size();n+=2) {
            const double actual=downsample2x(state,input[n-1],input[n],tight);
            double expected=0;
            const auto count=tight?tightOversamplingFir.size():oversamplingFir.size();
            for(std::size_t k=0;k<count && k<=n;++k) expected+=(tight?tightOversamplingFir[k]:oversamplingFir[k])*input[n-k];
            error=std::max(error,std::abs(actual-expected));
        }
        g_countAllocations=false;
        CHECK(g_allocations.load()==before);CHECK_NEAR(error,0,2e-15);
    }
}
TEST(filter_warp_keeps_its_physical_cutoff_when_switching_oversampling_quality) {
    // An independently known one-pole response: at its cutoff a sine must be
    // attenuated by sqrt(1/2), regardless of the internal processing rate.
    auto p=sineTablePatch(); auto& o=p.oscillators[0];
    o.warpOneAmount=1-std::log(1000.0/20)/std::log(1000.0);
    o.warpDefinitions[0].mode=WarpMode::filterLow;
    dsp::OscValues values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
    for(double sampleRate : {44100.0,48000.0,96000.0}) for(int quality : {0,1}) {
        dsp::OscInputs in; in.osc=&o; in.v=&values; in.sampleRate=sampleRate;
        in.masterTuning=1000; in.oversampleLevel=quality;
        dsp::OscVoiceState state; dsp::oscNoteOn(state,in);
        double energy=0; const int length=int(sampleRate);
        for(int i=0;i<length+2048;++i) {
            const double y=dsp::oscRender(state,in).l;
            if(i>=2048) energy+=y*y;
        }
        CHECK_NEAR(std::sqrt(2*energy/length),std::sqrt(0.5),0.002);
    }
}
TEST(audio_modulation_interpolator_preserves_dc_integer_samples_and_physical_time) {
    ModulationInterpolator two,four;
    for(int n=0;n<1024;++n) {
        const double input=std::sin(6.283185307179586*0.424224853515625*n);
        const auto a=two.process(input,false),b=four.process(input,true);
        CHECK_NEAR(a[0],b[0],0); CHECK_NEAR(a[2],b[2],0);
        if(n>=64) for(int phase=0;phase<4;++phase) {
            const double expected=std::sin(6.283185307179586*0.424224853515625*(n+phase/4.0-24));
            CHECK_NEAR(b[std::size_t(phase)],expected,0.0002);
        }
    }
    four.reset();
    for(int n=0;n<128;++n) {
        const auto out=four.process(n==0?1:0,true);
        CHECK_NEAR(out[0],n==24?1:0,0);
    }
    for(int n=0;n<128;++n) {
        const auto out=four.process(0.25,true);
        if(n>=64) for(double v : out) CHECK_NEAR(v,0.25,1e-14);
    }
}
TEST(audio_modulation_interpolator_is_allocation_free_and_recovers_from_nonfinite_input) {
    ModulationInterpolator interpolation;
    const auto before=g_allocations.load(); g_countAllocations=true;
    interpolation.process(std::numeric_limits<double>::quiet_NaN(),true);
    interpolation.process(std::numeric_limits<double>::infinity(),true);
    for(int n=0;n<128;++n) interpolation.process(0.25,true);
    g_countAllocations=false; CHECK(g_allocations.load()==before);
    const auto out=interpolation.process(0.25,true);
    for(double v : out) CHECK_NEAR(v,0.25,1e-14);
    interpolation.reset();
    for(double v : interpolation.process(0,true)) CHECK_NEAR(v,0,0);
}

// Independent coherent spectral measurement: only physically in-band carrier
// +/- integer*modulator lines count as wanted. The analytic PM carrier is J0(pi/2).
struct ModulationSpectrum { double aliasDb=0, carrierAmplitude=0; };
ModulationSpectrum audioModulationSpectrum(bool reconstructed,int quality,int carrierBin,int modBin,WarpMode mode=WarpMode::phaseMod) {
    constexpr int count=32768,warmup=2048;
    auto p=sineTablePatch(); auto& osc=p.oscillators[0];
    osc.bandlimitedAudioModulation=reconstructed;
    osc.warpDefinitions[0].mode=mode; osc.warpDefinitions[0].sourceIndex=1; osc.warpOneAmount=1;
    dsp::OscValues values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(osc,OscParam(i));
    std::array<double,7> mods{};
    dsp::OscInputs in; in.osc=&osc; in.v=&values; in.mods=&mods; in.sampleRate=sr;
    in.masterTuning=carrierBin*sr/count; in.oversampleLevel=quality;
    dsp::OscVoiceState state; dsp::oscNoteOn(state,in);
    std::vector<float> real(count),imag(count);
    for(int i=0;i<count+warmup;++i) {
        mods[1]=std::sin(6.283185307179586*modBin*double(i)/count);
        const auto out=dsp::oscRender(state,in);
        if(i>=warmup) real[std::size_t(i-warmup)]=float(out.l);
    }
    dsp::Fft fft; fft.init(count); fft.forward(real.data(),imag.data());
    std::vector<bool> wanted(count/2+1);
    for(int k=-100;k<=100;++k) {
        const int b=std::abs(carrierBin+k*modBin);
        if(b<=count/2 && (mode!=WarpMode::ringMod || std::abs(k)==1)) wanted[std::size_t(b)]=true;
    }
    double desired=0,unwanted=0;
    for(int bin=1;bin<count/2;++bin) {
        const double energy=double(real[std::size_t(bin)])*real[std::size_t(bin)]+double(imag[std::size_t(bin)])*imag[std::size_t(bin)];
        (wanted[std::size_t(bin)]?desired:unwanted)+=energy;
    }
    return {10*std::log10(std::max(unwanted,1e-30)/std::max(desired,1e-30)),
            2*std::hypot(real[std::size_t(carrierBin)],imag[std::size_t(carrierBin)])/count};
}
TEST(audio_rate_phase_modulation_reconstruction_reduces_foldback_without_reducing_depth) {
    for(int quality : {0,1}) {
        const auto old=audioModulationSpectrum(false,quality,2101,3331);
        const auto corrected=audioModulationSpectrum(true,quality,2101,3331);
        CHECK(corrected.aliasDb<-110); CHECK(corrected.aliasDb<old.aliasDb-40);
        // J0(pi/2) = .472001215768..., independent of source phase/delay.
        CHECK_NEAR(corrected.carrierAmplitude,0.4720012157682348,0.0001);
    }
    const auto high2=audioModulationSpectrum(true,0,2101,13901);
    const auto high4=audioModulationSpectrum(true,1,2101,13901);
    CHECK(high4.aliasDb<high2.aliasDb-30);
}
TEST(audio_rate_ring_modulation_keeps_only_the_physical_frequency_products) {
    const auto old=audioModulationSpectrum(false,1,6901,10531,WarpMode::ringMod);
    const auto corrected=audioModulationSpectrum(true,1,6901,10531,WarpMode::ringMod);
    CHECK(corrected.aliasDb<-75); CHECK(corrected.aliasDb<old.aliasDb-30);
}
TEST(audio_modulation_zero_depth_adds_no_output_latency_or_allocations) {
    for(auto mode : {WarpMode::phaseMod,WarpMode::ringMod,WarpMode::amplitudeMod,WarpMode::frequencyMod,WarpMode::frequencyModX,WarpMode::frequencyModPhase}) {
        auto p=sineTablePatch(); auto& o=p.oscillators[0];
        o.warpDefinitions[0].mode=mode; o.warpDefinitions[0].sourceIndex=1;
        dsp::OscValues values{};
        for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
        std::array<double,7> mods{}; mods[1]=0.75;
        dsp::OscInputs in; in.osc=&o; in.v=&values; in.mods=&mods; in.sampleRate=sr; in.oversampleLevel=1;
        dsp::OscVoiceState legacy,modern; dsp::oscNoteOn(legacy,in); dsp::oscNoteOn(modern,in);
        const long before=g_allocations.load(); g_countAllocations=true;
        bool equal=true,finite=true;
        for(int i=0;i<4096;++i) {
            o.bandlimitedAudioModulation=false; const double a=dsp::oscRender(legacy,in).l;
            o.bandlimitedAudioModulation=true; const double b=dsp::oscRender(modern,in).l;
            equal &= std::abs(a-b)<1e-7; finite &= std::isfinite(a)&&std::isfinite(b);
        }
        g_countAllocations=false;
        CHECK(equal && finite); CHECK(g_allocations.load()==before);
    }
}
TEST(audio_modulation_dual_slots_share_a_source_and_work_with_only_the_second_slot) {
    auto p=sineTablePatch(); auto& o=p.oscillators[0];
    o.warpDefinitions[0].mode=WarpMode::phaseMod; o.warpDefinitions[0].sourceIndex=1; o.warpOneAmount=1;
    auto direct=[&]() {
        dsp::OscValues values{};
        for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
        std::array<double,7> mods{};
        dsp::OscInputs in; in.osc=&o; in.v=&values; in.mods=&mods; in.sampleRate=sr; in.oversampleLevel=1;
        dsp::OscVoiceState state; dsp::oscNoteOn(state,in);
        std::vector<double> out(4096);
        for(std::size_t i=0;i<out.size();++i) {
            mods[1]=std::sin(6.283185307179586*3331*double(i)/32768);
            out[i]=dsp::oscRender(state,in).l;
        }
        return out;
    };
    const auto one=direct();
    o.warpDefinitions[1]=o.warpDefinitions[0]; o.warpOneAmount=o.warpTwoAmount=0.5;
    const auto shared=direct();
    o.warpDefinitions[0].mode=WarpMode::off; o.warpTwoAmount=1;
    const auto second=direct();
    for(std::size_t i=0;i<one.size();++i) {
        CHECK_NEAR(shared[i],one[i],1e-7); CHECK_NEAR(second[i],one[i],1e-7);
    }
}
TEST(audio_modulation_model_survives_native_state_and_missing_fields_keep_legacy) {
    auto p=sineTablePatch(); auto j=patchToJson(p);
    CHECK(patchFromJson(j).oscillators[0].bandlimitedAudioModulation);
    j["oscillators"][0].erase("bandlimitedAudioModulation");
    const auto old=patchFromJson(j);
    CHECK(!old.oscillators[0].bandlimitedAudioModulation);
    CHECK(!patchFromJson(patchToJson(old)).oscillators[0].bandlimitedAudioModulation);
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
TEST(importer_splitter_bypass_roundtrips_and_diagnoses_unsupported_topology_routes) {
    for (const auto& name : {"FXSplit", "FXSplit3", "FXSplitMS"}) {
        auto d = doc(); d.bytes = {1, 2, 3};
        const Json fields = {{"kParamEnable", 0.0}, {"kParamModuleCount1", 1.0},
            {"kParamModuleCount2", 1.0}, {"kParamBand1Bypass", 1.0}, {"kParamBand2Bypass", 0.0},
            {"kParamLevelOut", 0.7}, {"kParamSolo", 1.0}};
        d.state["FXRack0"]["FX"] = Json::array({Json{{name, Json{{"plainParams", fields}}}}});
        if (std::string(name) == "FXSplit3")
            d.state["FXRack0"]["FX"][0][name]["plainParams"]["kParamBand3Bypass"] = 1.0;
        const std::array<const char*, 5> targets{{"kParamModuleCount1", "kParamModuleCount2", "kParamModuleCount3", "kParamLevelOut", "kParamBand1Bypass"}};
        for (std::size_t i = 0; i < targets.size(); ++i)
            d.state["ModSlot" + std::to_string(i)] = Json{{"source", Json::array({38, 0})},
                {"destModuleTypeString", name}, {"destModuleID", 0}, {"destModuleParamName", targets[i]},
                {"plainParams", Json{{"kParamAmount", 100.0}}}};
        const auto p = importSerum(d, {});
        CHECK(p.fx.size() == 1 && !p.fx[0].enabled);
        CHECK(p.fx[0].set[fx::sBypass1] && p.fx[0].p[fx::sBypass1] == 1.0);
        CHECK(p.fx[0].set[fx::sBypass2] && p.fx[0].p[fx::sBypass2] == 0.0);
        CHECK(p.modulation.size() == targets.size());
        for (std::size_t i = 0; i < targets.size() - 1; ++i) {
            CHECK(p.modulation[i].targetKind == ModTarget::unknown);
            CHECK(std::any_of(p.diagnostics.begin(), p.diagnostics.end(), [&](const Diagnostic& diag) {
                return diag.path == "ModSlot" + std::to_string(i) && diag.status == "not_rendered";
            }));
        }
        CHECK(p.modulation.back().targetKind == ModTarget::fxParam && p.modulation.back().targetParam == fx::sBypass1);
        CHECK(std::any_of(p.diagnostics.begin(), p.diagnostics.end(), [](const Diagnostic& diag) {
            return diag.path.ends_with(".kParamLevelOut") && diag.status == "not_rendered_parameter";
        }));
        CHECK(std::any_of(p.diagnostics.begin(), p.diagnostics.end(), [](const Diagnostic& diag) {
            return diag.path.ends_with(".kParamSolo") && diag.status == "unmapped_parameter";
        }));
        unsigned unmapped = 0; for (const auto& diag : p.diagnostics) unmapped += diag.status == "unmapped_parameter";
        CHECK(p.mappedParameters + unmapped == p.explicitParameters);
        const auto restored = patchFromJson(patchToJson(p));
        CHECK(restored.fx[0].p == p.fx[0].p && restored.fx[0].set == p.fx[0].set);
        CHECK(restored.fx[0].parameters == p.fx[0].parameters && restored.fx[0].additional == p.fx[0].additional);
        CHECK(restored.unknownSerumState == d.state && restored.originalPreset == d.bytes);
        CHECK(restored.modulation[0].additional == p.modulation[0].additional);
        if (std::string(name) == "FXSplit3") CHECK(restored.fx[0].p[fx::sBypass3] == 1.0);
        const auto table = fxParamTable(p.fx[0].fxType);
        CHECK(table[fx::sBypass1].def == 0.0 && table[fx::sBypass2].def == 0.0 && table[fx::sBypass3].def == 0.0);
    }
}
TEST(importer_reports_splitter_nesting_pool_and_module_capacity_limits) {
    auto nested = doc();
    nested.state["FXRack0"]["FX"] = Json::array();
    for (int depth = 0; depth < 3; ++depth)
        nested.state["FXRack0"]["FX"].push_back(Json{{"FXSplitMS", Json{{"plainParams", Json{{"kParamModuleCount1", 2 - depth}}}}}});
    const auto deep = importSerum(nested, {});
    CHECK(std::any_of(deep.diagnostics.begin(), deep.diagnostics.end(), [](const Diagnostic& diag) {
        return diag.path == "FXRack0.FX[2].FXSplitMS" && diag.status == "not_rendered" && diag.detail.find("nesting") != std::string::npos;
    }));
    auto excessive = doc(); excessive.state["FXRack0"]["FX"] = Json::array();
    for (int i = 0; i < 97; ++i)
        excessive.state["FXRack0"]["FX"].push_back(Json{{i < 33 ? "FXSplitMS" : "FXUtils", Json{{"plainParams", Json::object()}}}});
    const auto limited = importSerum(excessive, {});
    CHECK(limited.fx.size() == 97); // All source nodes remain preserved.
    CHECK(std::any_of(limited.diagnostics.begin(), limited.diagnostics.end(), [](const Diagnostic& diag) {
        return diag.path == "FXRack0.FX[32].FXSplitMS" && diag.status == "not_rendered" && diag.detail.find("32-splitter") != std::string::npos;
    }));
    CHECK(std::any_of(limited.diagnostics.begin(), limited.diagnostics.end(), [](const Diagnostic& diag) {
        return diag.path == "FXRack0.FX[96].FXUtils" && diag.status == "not_rendered" && diag.detail.find("96-module") != std::string::npos;
    }));
}
TEST(importer_decodes_measured_envelope_sync_divisions) {
    auto d = doc();
    d.state["Env0"]["plainParams"] = {{"kParamBeatSync", 1.0}, {"kParamAttack", 1.0},
        {"kParamHold", 0.0}, {"kParamDecay", 32.0}, {"kParamRelease", 0.03125}};
    const auto p = importSerum(d, {});
    const auto& e = p.envelopes[0];
    CHECK(e.tempoSync);
    CHECK_NEAR(e.attack, 2.0 / 3.0, 1e-12); // 1/4 triplet, measured at normalized .5
    CHECK_NEAR(e.hold, 0.0, 1e-12);        // "fast"
    CHECK_NEAR(e.decay, 128.0, 1e-12);     // 32 bars
    CHECK_NEAR(e.release, 3.0 / 64.0, 1e-12); // 1/128 dotted, normalized .25
    d.state["Env0"]["plainParams"]["kParamBeatSync"] = 0.0;
    const auto free = importSerum(d, {});
    CHECK(!free.envelopes[0].tempoSync);
    CHECK_NEAR(free.envelopes[0].attack, 1.0, 1e-12);
}

TEST(importer_sparse_defaults_use_measured_plain_values) {
    const auto p = importSerum(doc(), {});
    CHECK_NEAR(p.masterVolume, 0.25, 1e-12);
    CHECK_NEAR(p.oscillators[0].volume, 0.5625, 1e-12);
    CHECK_NEAR(p.oscillators[3].volume, 0.0625, 1e-12);
    CHECK_NEAR(p.oscillators[0].detune, 0.25, 1e-12);
    CHECK_NEAR(p.filters[0].cutoff, 0.5, 1e-12);
    CHECK_NEAR(p.envelopes[0].attack, 0.001, 1e-12);
    CHECK_NEAR(p.envelopes[1].attack, 0.0005, 1e-12);
    CHECK_NEAR(p.envelopes[0].decay, 1.0, 1e-12);
    CHECK_NEAR(p.envelopes[0].release, 0.015, 1e-12);
    auto d = doc();
    d.state["Oscillator0"]["plainParams"] = {{"kParamVolume", 0.25}, {"kParamDetune", 0.0625}};
    const auto explicitValues = importSerum(d, {});
    CHECK_NEAR(explicitValues.oscillators[0].volume, 0.25, 1e-12); // already squared in file
    CHECK_NEAR(explicitValues.oscillators[0].detune, 0.0625, 1e-12);
}

TEST(imported_fx_filter_omitted_resonance_is_zero_without_changing_native_defaults) {
    auto d = doc();
    d.state["FXRack0"]["FX"] = Json::array({{{"FXFilter", {{"plainParams",
        {{"kParamType", "MgL12"}, {"kParamFreq", 1.0}, {"kParamVar", 94.0}}}}}}});
    auto p = importSerum(d, {});
    CHECK(p.fx.size() == 1);
    CHECK(p.fx[0].set[fx::fReso]);
    CHECK_NEAR(p.fx[0].p[fx::fReso], 0.0, 1.0e-12);
    d.state["FXRack0"]["FX"][0]["FXFilter"]["plainParams"]["kParamReso"] = 25.0;
    p = importSerum(d, {});
    CHECK_NEAR(p.fx[0].p[fx::fReso], 25.0, 1.0e-12);
    CHECK_NEAR(fxParamTable(FxType::filter)[fx::fReso].def, 10.0, 1.0e-12);
}

TEST(imported_lfo_sync_grid_defaults_and_modifier_choices_match_reference) {
    auto d = doc();
    auto p = importSerum(d, {});
    CHECK(p.lfoDefinitions[0].tempoSync);
    CHECK_NEAR(p.lfoDefinitions[0].syncBeats, 1.0, 1e-12);
    CHECK_NEAR(dsp::lfoRateHz(p.lfoDefinitions[0], 120.0), 2.0, 1e-12);
    CHECK_NEAR(dsp::evalCurve(p.lfoDefinitions[0].path, 0.25), 0.5, 1e-12);
    for (int dotted = 0; dotted < 2; ++dotted) for (int triplets = 0; triplets < 2; ++triplets) {
        for (int step = 0; step <= 42; ++step) {
            const double normalized = double(step) / 42.0;
            d.state["LFO0"]["plainParams"] = {{"kParamRate", 100.0 * std::pow(normalized, 4)},
                {"kParamDotted", dotted}, {"kParamTriplets", triplets}};
            const auto imported = importSerum(d, {});
            double beats = std::exp2(7 - step / 3);
            if (step % 3 == 1 && dotted) beats *= 0.75;
            if (step % 3 == 2) beats *= triplets ? 2.0 / 3.0 : dotted ? 0.75 : 1.0;
            CHECK_NEAR(imported.lfoDefinitions[0].syncBeats, beats, 1e-12);
            CHECK_NEAR(dsp::lfoRateHz(imported.lfoDefinitions[0], 60), 1.0 / beats, 1e-12);
        }
    }
    d.state["LFO0"]["plainParams"] = {{"kParamRate", 2.0}, {"kParamBeatSync", 0}};
    p = importSerum(d, {});
    CHECK(!p.lfoDefinitions[0].tempoSync);
    CHECK_NEAR(dsp::lfoRateHz(p.lfoDefinitions[0], 60), 2.0, 1e-12);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK_NEAR(restored.lfoDefinitions[0].rateHz, 2.0, 1e-12);
}
TEST(imported_multiband_uses_original_quality_model_and_reports_uncalibrated_dynamics) {
    auto d=doc();
    d.state["FXRack0"]["FX"]=Json::array({{{"FXComp",{{"plainParams",{{"kParamMultiband",1.0}}}}}}});
    const auto p=patchFromJson(patchToJson(importSerum(d,{})));
    CHECK(p.fx.size()==1 && p.fx[0].phaseCoherentMultiband && p.fx[0].compressorCubicSingleBand);
    CHECK(p.fx[0].compressorGlobalControls);
    for(int band=0;band<3;++band) CHECK_NEAR(p.fx[0].p[fx::pRatioBelow0+band],1,0);
    bool reported=false;
    for(const auto& diagnostic : p.diagnostics)
        reported |= diagnostic.path.find("kParamMultiband")!=std::string::npos && diagnostic.detail.find("remaining transient/crossover/automation behavior partial")!=std::string::npos;
    CHECK(reported);
    auto effect=p.fx[0];
    for(double amplitude : {0.0001,0.001,0.01}) {
        std::vector<double> signal(96000);
        for(std::size_t i=0;i<signal.size();++i) signal[i]=amplitude*std::sin(6.283185307179586*400*double(i)/sr);
        CHECK_NEAR(rmsd(runFx(effect,signal).l,48000)/rmsd(signal,48000),1,1e-6);
    }
    d.state["FXRack0"]["FX"][0]["FXComp"]["plainParams"]["kParamRatioBelow1"]=0.4;
    CHECK_NEAR(importSerum(d,{}).fx[0].p[fx::pRatioBelow1],0.4,0);
}
TEST(imported_audio_rate_warp_selects_reconstruction_and_reports_its_delay) {
    auto d=doc();
    d.state["Oscillator0"]["WTOsc0"]["plainParams"]["kParamWarpMenu"]="kPD_OSC";
    const auto p=patchFromJson(patchToJson(importSerum(d,{})));
    CHECK(p.oscillators[0].bandlimitedAudioModulation);
    CHECK(p.oscillators[0].warpDefinitions[0].mode==WarpMode::phaseMod);
    bool found=false;
    for(const auto& diag : p.diagnostics) found |= diag.detail.find("24-sample modulation-path delay")!=std::string::npos;
    CHECK(found);
}

TEST(imported_filter_frequency_scale_matches_both_reference_controls) {
    const auto p = importSerum(doc(), {});
    const double measured[] = {8,9,10,10,11,12,13,14,15,17,18,19,21,23,25,27,29,31,34,37,40,43,47,50,54,59,
        64,69,75,81,88,95,102,111,120,130,141,152,165,178,193,209,226,244,265,286,310,335,363,393,425,460,498,
        539,583,631,683,739,800,865,937,1014,1097,1187,1285,1390,1505,1629,1762,1907,2064,2234,2418,2616,2832,
        3064,3316,3589,3884,4203,4549,4923,5328,5766,6240,6753,7309,7909,8560,9264,10025,10850,11742,12707,13752,
        14883,16107,17431,18864,20415,22050};
    for (const auto& filter : p.filters) {
        for (int i = 0; i <= 100; ++i) CHECK_NEAR(filterCutoffHz(filter, i / 100.0), measured[i], 0.501);
        for (double n : {0.1, 0.5, 0.9}) CHECK_NEAR(filterCutoffNormalized(filter, filterCutoffHz(filter, n)), n, 1e-12);
    }
    CHECK_NEAR(filterCutoffHz(Filter{}, 0.5), std::sqrt(20.0 * 20000.0), 1e-9);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK_NEAR(filterCutoffHz(restored.filters[0], 0.5), filterCutoffHz(p.filters[0], 0.5), 1e-12);
}

TEST(imported_scalar_maps_drive_modulation_and_survive_native_save) {
    auto d = doc();
    d.state["scalars"] = {{"velo", {{"xVals", {0.0, 1.0}}, {"yVals", {0.5, 0.5}}, {"curveVals", {0.5, 0.5}}}},
        {"note", {{"xVals", {0.0, 1.0}}, {"yVals", {0.75, 0.75}}, {"curveVals", {0.5, 0.5}}}}};
    const auto mapped = patchFromJson(patchToJson(importSerum(d, {})));
    CHECK_NEAR(dsp::evalCurve(mapped.velocityCurve, 1.0), 0.5, 1e-12);
    CHECK_NEAR(dsp::evalCurve(mapped.noteCurve, 0.1), 0.25, 1e-12);
    CHECK(mapped.quantizeVelocityCurve);
    Patch p = sineTablePatch(); p.oscillators[0].volume = 0.0;
    p.oscillators[0].volumeModulationPower = 2.0;
    p.velocityCurve = mapped.velocityCurve; p.noteCurve = mapped.noteCurve;
    p.quantizeVelocityCurve = mapped.quantizeVelocityCurve;
    p.modulation.push_back(route(ModSource::velocity, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100));
    const auto velocity = render(p, 60, 48000);
    p.modulation[0].sourceKind = ModSource::note;
    const auto note = render(p, 60, 48000);
    p.modulation.clear(); p.oscillators[0].volume = 1.0;
    const auto full = render(p, 60, 48000);
    CHECK_NEAR(rms(velocity.l, 24000, 48000) / rms(full.l, 24000, 48000), std::pow(64.0 / 127.0, 2), 1e-6);
    CHECK_NEAR(rms(note.l, 24000, 48000) / rms(full.l, 24000, 48000), 0.0625, 1e-6);
}

TEST(imported_overdrive_small_signal_gain_follows_measured_drive_range) {
    auto d = doc();
    d.state["FXRack0"]["FX"] = Json::array({{{"FXDistortion", {{"plainParams", {{"kParamMode", "kOverdrive"}, {"kParamWet", 100.0}}}}}}});
    const auto p = importSerum(d, {});
    CHECK(p.fx.size() == 1);
    auto effect = p.fx[0];
    std::vector<double> signal(24000);
    for (std::size_t i = 0; i < signal.size(); ++i) signal[i] = 0.001 * std::sin(6.283185307179586 * 261.6255653 * double(i) / sr);
    const double measuredDb[5] = {-8.647, 1.103, 10.853, 20.603, 30.353};
    for (int step = 0; step < 5; ++step) {
        setParam(effect, fx::xDrive, double(step * 25));
        const auto out = runFx(effect, signal);
        CHECK_NEAR(20.0 * std::log10(rmsd(out.l, 12000) / rmsd(signal, 12000)), measuredDb[step], 0.15);
    }
    const auto restored = patchFromJson(patchToJson(p));
    CHECK_NEAR(restored.fx[0].distortionDriveRangeDb, 39.0, 1e-12);
    CHECK_NEAR(restored.fx[0].distortionGainCompensation, 0.0, 1e-12);
}
TEST(imported_time_fx_defaults_modes_timing_and_limits_are_explicit) {
    auto d = doc();
    d.state["FXRack0"]["FX"] = Json::array({
        Json{{"type", 4}, {"FXDelay", {{"plainParams", {{"kParamMode", 1.0}}}}}},
        Json{{"type", 6}, {"FXReverb", {{"plainParams", {{"kParamType", "kHall"}}}}}}
    });
    const auto p = importSerum(d, {});
    CHECK(p.fx.size() == 2 && !p.fx[0].legacyTimeFx && !p.fx[1].legacyTimeFx);
    CHECK_NEAR(p.fx[0].p[fx::dMode], 2.0, 1e-12);
    CHECK_NEAR(p.fx[0].p[fx::dTimeL], 0.0772939453125, 1e-12);
    CHECK_NEAR(p.fx[0].p[fx::dBW], 6.75, 1e-12);
    CHECK_NEAR(p.fx[1].p[fx::rPreDelay], 0.0, 1e-12);
    CHECK_NEAR(p.fx[1].p[fx::rDelay], 30.625, 1e-12);
    bool timingLimit = false;
    for (const auto& diag : p.diagnostics)
        timingLimit |= diag.path.find("kParamBeatSync") != std::string::npos && diag.status == "render_limit";
    CHECK(timingLimit);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK(!restored.fx[0].legacyTimeFx && !restored.fx[1].legacyTimeFx);
    CHECK(restored.fx[0].delayTimingModel == DelayTimingModel::fourthPowerBeatGrid);
    double parameters[fxParamSlots]; effective(restored.fx[0], parameters);
    CHECK_NEAR(dsp::delayTimeTargets(restored.fx[0], parameters, 120)[0], 0.5, 1e-12);
}
TEST(imported_delay_retains_raw_time_and_qualifies_uncalibrated_timing_modulation) {
    auto d = doc(); d.bytes = {4, 5, 6};
    d.state["FXRack0"]["FX"] = Json::array({Json{{"FXDelay", Json{{"plainParams", Json{
        {"kParamBeatSync", 1.0}, {"kParamTimeL", 3.0}, {"kParamTimeR", -1.0}}}}}}});
    d.state["ModSlot0"] = Json{{"source", Json::array({38, 0})}, {"destModuleTypeString", "FXDelay"},
        {"destModuleID", 0}, {"destModuleParamName", "kParamTimeL"}, {"plainParams", Json{{"kParamAmount", 20.0}}}};
    const auto p = importSerum(d, {});
    CHECK(p.fx[0].p[fx::dTimeL] == 0.501 && p.fx[0].p[fx::dTimeR] == 0.001);
    CHECK(p.fx[0].parameters["kParamTimeL"] == 3.0);
    CHECK(p.modulation[0].targetKind == ModTarget::fxParam && p.modulation[0].targetParam == fx::dTimeL);
    CHECK(std::any_of(p.diagnostics.begin(), p.diagnostics.end(), [](const Diagnostic& diag) {
        return diag.path == "ModSlot0.destModuleParamName" && diag.status == "not_rendered_parameter";
    }));
    const auto restored = patchFromJson(patchToJson(p));
    CHECK(restored.unknownSerumState == d.state && restored.originalPreset == d.bytes);
    CHECK(restored.fx[0].parameters == p.fx[0].parameters);
}
TEST(imported_linked_delay_uses_effective_right_settings_and_preserves_requested_state) {
    for (const bool omitRight : {false, true}) {
        auto d = doc();
        Json values = {{"kParamLink", 1.0}, {"kParamTimeL", 0.25}, {"kParamOffsetL", 0.5}};
        if (!omitRight) { values["kParamTimeR"] = 0.0400625; values["kParamOffsetR"] = 1.5; }
        d.state["FXRack0"]["FX"] = Json::array({Json{{"FXDelay", Json{{"plainParams", values}}}}});
        const auto p = importSerum(d, {});
        const double rightTime = omitRight ? 0.0772939453125 : 0.0400625, rightOffset = omitRight ? 1.0 : 1.5;
        CHECK_NEAR(p.fx[0].p[fx::dTimeL], rightTime, 1e-12);
        CHECK_NEAR(p.fx[0].p[fx::dTimeR], rightTime, 1e-12);
        CHECK_NEAR(p.fx[0].p[fx::dOffsetL], rightOffset, 1e-12);
        double parameters[fxParamSlots]; effective(p.fx[0], parameters);
        const auto targets = dsp::delayTimeTargets(p.fx[0], parameters, 120);
        CHECK_NEAR(targets[0], targets[1], 1e-12);
        CHECK_NEAR(targets[0], omitRight ? 0.5 : 0.375, 1e-12);
        CHECK(p.fx[0].parameters == values && p.unknownSerumState == d.state);
        const auto restored = patchFromJson(patchToJson(p));
        CHECK(restored.fx[0].parameters == values && restored.fx[0].p == p.fx[0].p && restored.fx[0].set == p.fx[0].set);
    }
}

TEST(imported_single_band_compressor_matches_measured_threshold_and_makeup_travel) {
    auto d = doc();
    d.state["FXRack0"]["FX"] = Json::array({{{"type", 5}, {"FXComp", {{"plainParams", {
        {"kParamRatio", 1000000.0}, {"kParamAttack", 0.1}, {"kParamRelease", 1000.0}, {"kParamWet", 100.0}}}}}}});
    const auto imported = importSerum(d, {});
    CHECK(imported.fx.size() == 1 && imported.fx[0].compressorCubicSingleBand);
    auto effect = imported.fx[0];
    std::vector<double> signal(24000);
    for (std::size_t i = 0; i < signal.size(); ++i) signal[i] = std::sin(6.283185307179586 * 261.6255653 * double(i) / sr);
    const double thresholds[] = {0.5, 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95};
    const double measuredRms[] = {0.1034847485, 0.0529886252, 0.0223555588, 0.0129376287,
        0.0066244738, 0.0027951793, 0.0008287835, 0.0001043227};
    const double dry = rmsd(signal, 12000);
    for (std::size_t i = 0; i < std::size(thresholds); ++i) {
        setParam(effect, fx::pThresh, thresholds[i]);
        const auto out = runFx(effect, signal);
        const double relative = rmsd(out.l, 12000) / dry;
        CHECK_NEAR(20.0 * std::log10(relative), 20.0 * std::log10(measuredRms[i] / 0.2506760087), 0.15);
    }
    for (auto& sample : signal) sample *= 0.001;
    setParam(effect, fx::pThresh, 0); setParam(effect, fx::pRatio, 1);
    for (double makeup : {1.0, 2.0, 4.0, 8.0}) {
        setParam(effect, fx::pMakeup, makeup);
        const auto out = runFx(effect, signal);
        CHECK_NEAR(rmsd(out.l, 12000) / rmsd(signal, 12000), (31.0 * makeup - 1.0) / 30.0, 1e-5);
    }
    CHECK(patchFromJson(patchToJson(imported)).fx[0].compressorCubicSingleBand);
    CHECK(!FxModule{}.compressorCubicSingleBand);
    auto sparse = doc(); sparse.state["FXRack0"]["FX"] = Json::array({{{"type", 5}, {"FXComp", {{"plainParams", Json::object()}}}}});
    auto defaults = importSerum(sparse, {}).fx[0];
    CHECK_NEAR(defaults.p[fx::pAttack], 90.091, 1e-9);
    CHECK_NEAR(defaults.p[fx::pRelease], 90.091, 1e-9);
    CHECK_NEAR(defaults.p[fx::pRatio], 4.0, 1e-12);
    CHECK_NEAR(defaults.p[fx::pRatioBelow], 0.0, 1e-12); // raw global depth, inactive in single-band
    signal.resize(72000);
    for (std::size_t i = 0; i < signal.size(); ++i) signal[i] = std::sin(6.283185307179586 * 261.6255653 * double(i) / sr);
    const auto normal = runFx(defaults, signal);
    CHECK_NEAR(20.0 * std::log10(rmsd(normal.l, 48000) / rmsd(signal, 48000)),
        20.0 * std::log10(0.1642588 / 0.2507684), 0.3);
}

TEST(compressor_global_mapping_preserves_typed_state_and_native_migration) {
    auto d=doc();
    const Json source={{"kParamMultiband",1.0},{"kParamRatioBelow",0.25},
        {"kParamRatioBelow1",0.0},{"kParamDeadband0",0.0},{"kParamDeadband1",12.0},
        {"kParamDeadband2",48.0},{"kParamBypassUp2",1.0},{"kParamBypassDown3",1.0}};
    d.state["FXRack0"]["FX"]=Json::array({{{"FXComp",{{"plainParams",source}}}}});
    const auto p=importSerum(d,{}); const auto& m=p.fx[0];
    CHECK(m.compressorGlobalControls && m.parameters==source && p.unknownSerumState==d.state);
    CHECK_NEAR(m.p[fx::pRatioBelow],0.25,0); CHECK_NEAR(m.p[fx::pRatioBelow1],0,0);
    CHECK_NEAR(m.p[fx::pXoverLow],128.29653151726313,1e-9);
    CHECK((m.compressorDeadbandDb==std::array<double,3>{3,12,24}));
    CHECK(m.compressorBypassUp[1] && m.compressorBypassDown[2]);
    for(const auto& e:p.diagnostics) CHECK(e.status!="unmapped_parameter" || e.path.find("FXComp")==std::string::npos);
    auto j=patchToJson(p); auto restored=patchFromJson(j).fx[0];
    CHECK(restored.compressorGlobalControls && restored.compressorDeadbandDb==m.compressorDeadbandDb);
    CHECK(restored.compressorBypassUp==m.compressorBypassUp && restored.compressorBypassDown==m.compressorBypassDown);
    j["fx"][0].erase("compressorGlobalControls");
    restored=patchFromJson(j).fx[0]; CHECK(!restored.compressorGlobalControls);
    auto legacy=m; legacy.compressorGlobalControls=false;
    std::vector<double> signal(96000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=0.001*std::sin(6.283185307179586*440*i/sr);
    CHECK(runFx(legacy,signal).l==runFx(restored,signal).l);
    CHECK(!FxModule{}.compressorGlobalControls);
    CHECK_NEAR(fxParamTable(FxType::comp)[fx::pRatioBelow].lo,0.02,0); // old modulation coordinates retained
}

TEST(imported_single_band_below_control_is_inactive_and_legacy_is_preserved) {
    auto d=doc(); d.state["FXRack0"]["FX"]=Json::array({{{"FXComp",{{"plainParams",{
        {"kParamAttack",0.1},{"kParamRelease",1000.0},{"kParamRatioBelow",0.25}}}}}}});
    auto m=importSerum(d,{}).fx[0];
    std::vector<double> signal(24000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=0.001*std::sin(6.283185307179586*440*i/sr);
    const auto normal=runFx(m,signal).l;
    for(double below:{0.0,0.1,0.5,1.0}){setParam(m,fx::pRatioBelow,below);CHECK(runFx(m,signal).l==normal);}
    m.compressorGlobalControls=false;setParam(m,fx::pRatioBelow,0.25);
    CHECK(rmsd(runFx(m,signal).l,12000)<rmsd(normal,12000)*0.01);
}

TEST(multiband_global_and_local_controls_match_isolated_reference_levels) {
    auto m=makeFx(FxType::comp); m.compressorGlobalControls=true;
    setParam(m,fx::pMultiband,1);setParam(m,fx::pAttack,0.1);setParam(m,fx::pRelease,1000);
    setParam(m,fx::pRatio,4);setParam(m,fx::pXoverLow,50);setParam(m,fx::pXoverHi,10600);
    m.compressorDeadbandDb={3,3,3};
    for(int b=0;b<3;++b){setParam(m,fx::pRatio0+b,b==1?1:0);setParam(m,fx::pRatioBelow0+b,b==1?1:0);}
    setParam(m,fx::pGain0,-24);setParam(m,fx::pGain2,-24);
    std::vector<double> signal(96000);
    const auto fill=[&](double a){for(std::size_t i=0;i<signal.size();++i)signal[i]=a*std::sin(6.283185307179586*440*i/sr);};
    fill(0.001);const double dry=rmsd(signal,48000);
    const double depths[]={0,0.25,0.5,0.75,1};
    const double gains[]={0,9.2570,18.5139,27.7707,36};
    for(int i=0;i<5;++i){setParam(m,fx::pRatioBelow,depths[i]);
        CHECK_NEAR(20*std::log10(rmsd(runFx(m,signal).l,48000)/dry),gains[i],0.06);}
    setParam(m,fx::pRatioBelow,0.5);
    for(double local:{0.0,0.25,0.5,0.75,1.0}){setParam(m,fx::pRatioBelow1,local);
        CHECK_NEAR(20*std::log10(rmsd(runFx(m,signal).l,48000)/dry),18.5139*(2*local-1),0.06);}
    m.compressorBypassUp[1]=true;
    CHECK_NEAR(rmsd(runFx(m,signal).l,48000)/dry,1,3e-4); // residual attenuated outer bands
    fill(1.0);const double loud=rmsd(signal,48000);setParam(m,fx::pRatioBelow,0);
    const double ratios[]={1,2,4,10}, reductions[]={0,-9.9861,-14.9791,-17.9747};
    for(int i=0;i<4;++i){setParam(m,fx::pRatio,ratios[i]);
        CHECK_NEAR(20*std::log10(rmsd(runFx(m,signal).l,48000)/loud),reductions[i],0.07);}
    m.compressorBypassDown[1]=true;
    CHECK_NEAR(rmsd(runFx(m,signal).l,48000)/loud,1,3e-4);
}

TEST(multiband_curve_is_continuous_bounded_and_has_separate_deadband) {
    const double threshold=dsp::compressorBandThreshold(0.5,100);
    CHECK_NEAR(dsp::compressorBandThreshold(0.75,150)-dsp::compressorBandThreshold(0.75,100),12,1e-12);
    for(double dead:{3.,12.,24.}) {
        CHECK_NEAR(dsp::compressorBandGainDb(threshold,threshold,dead,0.75,0.5),0,0);
        CHECK_NEAR(dsp::compressorBandGainDb(threshold-dead,threshold,dead,0.75,0.5),0,0);
        CHECK_NEAR(dsp::compressorBandGainDb(threshold-dead-20,threshold,dead,0.75,0.5),10,1e-12);
        CHECK_NEAR(dsp::compressorBandGainDb(threshold+20,threshold,dead,0.75,0.5),-15,1e-12);
        for(int i=-18000;i<=6000;++i) {
            const double level=i*0.01, gain=dsp::compressorBandGainDb(level,threshold,dead,0.75,-0.5);
            CHECK(std::isfinite(gain)&&gain>=-40&&gain<=36);
            CHECK(std::abs(gain-dsp::compressorBandGainDb(level+1e-6,threshold,dead,0.75,-0.5))<1e-6);
        }
    }
}

TEST(multiband_global_automation_and_silence_are_allocation_free) {
    auto m=makeFx(FxType::comp);m.compressorGlobalControls=true;
    for(int b=0;b<3;++b){setParam(m,fx::pRatio0+b,1);setParam(m,fx::pRatioBelow0+b,1);}
    setParam(m,fx::pMultiband,1);setParam(m,fx::pRatioBelow,1);setParam(m,fx::pRatio,4);
    for(double rate:{44100.,48000.,96000.}) {
        auto unit=dsp::makeFxUnit(FxType::comp);unit->prepare(rate);
        double p[fxParamSlots];effective(m,p); double l[64]{},r[64]{};
        g_allocations=0;g_countAllocations=true;
        for(int j=0;j<50;++j)unit->process(m,p,l,r,64);
        g_countAllocations=false;CHECK(g_allocations==0);
        for(int i=0;i<64;++i)CHECK(l[i]==0&&r[i]==0);
        double previous=0,maxStep=0;
        g_allocations=0;g_countAllocations=true;
        for(int j=0;j<1800;++j) {
            if(j==1000)p[fx::pThresh]=0;
            if(j==1200)p[fx::pRatioBelow]=0;
            if(j==1400)p[fx::pThreshUD1]=0;
            for(int i=0;i<64;++i)l[i]=r[i]=0.001;
            unit->process(m,p,l,r,64);
            for(int i=0;i<64;++i){CHECK(std::isfinite(l[i])&&std::isfinite(r[i]));
                if(j>=900)maxStep=std::max(maxStep,std::abs(l[i]-previous));previous=l[i];}
        }
        g_countAllocations=false;
        CHECK(g_allocations==0);CHECK_MSG(std::isfinite(maxStep)&&maxStep<0.01,"automated sample step "<<maxStep);
    }
}

TEST(compressor_gain_mix_fields_import_and_preserve_previous_native_models) {
    auto d=doc();d.state["FXRack0"]["FX"]=Json::array({{{"FXComp",{{"plainParams",{
        {"kParamMultiband",1.0},{"kParamCompensatedWetDry",0.0},{"kParamRatio",1.0},{"kParamMakeup",4.0}}}}}}});
    auto p=importSerum(d,{});const auto& m=p.fx[0];
    CHECK(m.compressorAffineMakeup && !m.compressorPhaseMatchedDry);
    for(const auto& e:p.diagnostics)CHECK(e.status!="unmapped_parameter"||e.path.find("kParamCompensatedWetDry")==std::string::npos);
    auto j=patchToJson(p);auto restored=patchFromJson(j).fx[0];
    CHECK(restored.compressorAffineMakeup&&!restored.compressorPhaseMatchedDry);
    j["fx"][0].erase("compressorAffineMakeup");j["fx"][0].erase("compressorPhaseMatchedDry");
    restored=patchFromJson(j).fx[0];CHECK(!restored.compressorAffineMakeup&&restored.compressorPhaseMatchedDry);
    auto old=m;old.compressorAffineMakeup=false;old.compressorPhaseMatchedDry=true;
    std::vector<double> signal(24000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=0.001*std::sin(6.283185307179586*261.6255653*i/sr);
    CHECK(runFx(old,signal).l==runFx(restored,signal).l);
    CHECK(!FxModule{}.compressorAffineMakeup&&FxModule{}.compressorPhaseMatchedDry);
    d.state["FXRack0"]["FX"][0]["FXComp"]["plainParams"].erase("kParamCompensatedWetDry");
    CHECK(importSerum(d,{}).fx[0].compressorPhaseMatchedDry);
}

TEST(compressor_multiband_makeup_and_phase_switch_follow_measured_semantics) {
    auto m=makeFx(FxType::comp);m.compressorGlobalControls=true;m.compressorAffineMakeup=true;
    setParam(m,fx::pMultiband,1);setParam(m,fx::pRatio,1);setParam(m,fx::pRatioBelow,0);
    std::vector<double> signal(48000);
    // Whole cycles keep the RMS assertion independent of allpass phase.
    for(std::size_t i=0;i<signal.size();++i)signal[i]=0.001*std::sin(6.283185307179586*300*i/sr);
    const double dry=rmsd(signal,24000);
    for(double makeup:{1.,2.,4.,8.,16.,31.}){
        setParam(m,fx::pMakeup,makeup);
        CHECK_NEAR(rmsd(runFx(m,signal).l,24000)/dry,(31*makeup-1)/30,1e-5);
    }
    setParam(m,fx::pMakeup,4);setParam(m,fx::pWet,100);const auto wet=runFx(m,signal).l;
    m.compressorPhaseMatchedDry=false;
    setParam(m,fx::pWet,0);CHECK(runFx(m,signal).l==signal);
    for(double mix:{0.25,0.5}){
        setParam(m,fx::pWet,100*mix);const auto output=runFx(m,signal).l;
        for(std::size_t i=0;i<signal.size();++i)CHECK_NEAR(output[i],signal[i]*(1-mix)+wet[i]*mix,1e-14);
        const double unaligned=rmsd(output,24000);
        m.compressorPhaseMatchedDry=true;const auto matched=runFx(m,signal).l;
        CHECK_NEAR(rmsd(matched,24000)/dry,1-mix+4.1*mix,1e-5);
        CHECK(unaligned<rmsd(matched,24000)*0.98);
        m.compressorPhaseMatchedDry=false;
    }
    setParam(m,fx::pWet,100);const auto wetAgain=runFx(m,signal).l;
    for(std::size_t i=0;i<wet.size();++i)CHECK_NEAR(wetAgain[i],wet[i],1e-14); // final lerp rounding only
}

TEST(compressor_affine_makeup_automation_uses_prepared_slew_without_allocation) {
    auto m=makeFx(FxType::comp);m.compressorAffineMakeup=true;m.compressorGlobalControls=true;
    setParam(m,fx::pMultiband,1);setParam(m,fx::pRatio,1);setParam(m,fx::pRatioBelow,0);
    auto unit=dsp::makeFxUnit(FxType::comp);unit->prepare(sr);
    double p[fxParamSlots];effective(m,p);double l[64],r[64],previous=0,maxStep=0;
    g_allocations=0;g_countAllocations=true;
    for(int j=0;j<2000;++j){
        if(j==1000)p[fx::pMakeup]=31;
        for(int i=0;i<64;++i)l[i]=r[i]=0.001;
        unit->process(m,p,l,r,64);
        for(int i=0;i<64;++i){CHECK(std::isfinite(l[i])&&l[i]==r[i]);
            if(j>900)maxStep=std::max(maxStep,std::abs(l[i]-previous));previous=l[i];}
    }
    g_countAllocations=false;CHECK(g_allocations==0);
    CHECK(maxStep<0.001);CHECK_NEAR(l[63],0.032,1e-10);
}

TEST(compressor_band_attack_import_and_missing_state_preserve_global_control) {
    auto d=doc();d.state["FXRack0"]["FX"]=Json::array({{{"FXComp",{{"plainParams",{
        {"kParamMultiband",1.0},{"kParamAttack",1000.0}}}}}}});
    const auto p=importSerum(d,{});const auto& m=p.fx[0];
    CHECK_NEAR(m.compressorBandAttackMs,0.1,0);
    CHECK_NEAR(m.p[fx::pAttack],1000,0);
    CHECK(m.parameters["kParamAttack"]==1000.0 && p.unknownSerumState==d.state);
    auto j=patchToJson(p);CHECK_NEAR(patchFromJson(j).fx[0].compressorBandAttackMs,0.1,0);
    j["fx"][0].erase("compressorBandAttackMs");auto restored=patchFromJson(j).fx[0];
    CHECK_NEAR(restored.compressorBandAttackMs,-1,0);CHECK_NEAR(FxModule{}.compressorBandAttackMs,-1,0);
    auto legacy=m;legacy.compressorBandAttackMs=-1;
    std::vector<double> signal(24000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=0.8*std::sin(6.283185307179586*1000*i/sr);
    CHECK(runFx(legacy,signal).l==runFx(restored,signal).l);
}

TEST(compressor_fast_band_detector_does_not_override_single_band_or_legacy_attack) {
    auto m=makeFx(FxType::comp);m.compressorGlobalControls=true;m.compressorCubicSingleBand=true;
    m.compressorBandAttackMs=0.1;setParam(m,fx::pMultiband,1);setParam(m,fx::pRatio,4);
    setParam(m,fx::pThresh,0.5);setParam(m,fx::pRatioBelow,0);setParam(m,fx::pWet,100);
    std::vector<double> signal(24000);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=(i<4800?0.01:0.8)*std::sin(6.283185307179586*1000*i/sr);
    setParam(m,fx::pAttack,0.1);const auto fast=runFx(m,signal).l;
    setParam(m,fx::pAttack,1000);CHECK(runFx(m,signal).l==fast);
    m.compressorBandAttackMs=-1;const auto slow=runFx(m,signal).l;
    CHECK(rmsd(slow,4800)>rmsd(fast,4800)*1.5);
    setParam(m,fx::pAttack,0.1);CHECK(runFx(m,signal).l==fast);
    setParam(m,fx::pMultiband,0);setParam(m,fx::pAttack,1000);
    const auto singleSlow=runFx(m,signal).l;m.compressorBandAttackMs=0.1;
    CHECK(runFx(m,signal).l==singleSlow);setParam(m,fx::pAttack,0.1);
    CHECK(rmsd(singleSlow,4800)>rmsd(runFx(m,signal).l,4800)*1.1);
}

TEST(compressor_fast_band_attack_automation_uses_prepared_storage_at_all_rates) {
    auto m=makeFx(FxType::comp);m.compressorGlobalControls=true;m.compressorBandAttackMs=0.1;
    setParam(m,fx::pMultiband,1);setParam(m,fx::pRatioBelow,1);
    for(double rate:{44100.,48000.,96000.}){
        auto unit=dsp::makeFxUnit(FxType::comp);unit->prepare(rate);
        double p[fxParamSlots];effective(m,p);double l[64],r[64];bool finite=true;
        g_allocations=0;g_countAllocations=true;
        for(int j=0;j<2000;++j){
            p[fx::pAttack]=(j%2)?0.1:1000;
            for(int i=0;i<64;++i)l[i]=r[i]=(j<1000?0.8:0.01)*std::sin(6.283185307179586*1000*(j*64+i)/rate);
            unit->process(m,p,l,r,64);
            for(int i=0;i<64;++i)finite=finite&&std::isfinite(l[i])&&l[i]==r[i];
        }
        g_countAllocations=false;CHECK(g_allocations==0&&finite);
    }
}

TEST(imported_envelope_gain_and_level_modulation_match_reference_ratios) {
    auto d = doc();
    d.state["Env0"]["plainParams"] = {{"kParamAttack", 0.02}, {"kParamDecay", 0.2}, {"kParamSustain", 0.5}};
    const auto imported = importSerum(d, {});
    CHECK_NEAR(imported.envelopes[0].sustain, 0.25, 1e-12);
    CHECK(imported.envelopes[0].exponentialCurves);
    Patch p = sineTablePatch(); p.envelopes[0] = imported.envelopes[0];
    const auto half = render(p, 60, 48000);
    p.envelopes[0].sustain = 1.0;
    const auto full = render(p, 60, 48000);
    CHECK_NEAR(rms(half.l, 24000, 48000) / rms(full.l, 24000, 48000), 0.25, 0.001);
    p.envelopes[0].sustain = 0.25;
    p.oscillators[0].volume = 0.0; p.oscillators[0].volumeModulationPower = 2.0;
    p.modulation.push_back(route(ModSource::envelope, 0, ModTarget::oscParam, 0, int(OscParam::volume), 100.0));
    const auto modulated = render(p, 60, 48000);
    CHECK_NEAR(rms(modulated.l, 24000, 48000) / rms(full.l, 24000, 48000), 0.015625, 0.0001);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK_NEAR(restored.oscillators[0].volumeModulationPower, 2.0, 1e-12);
    CHECK(restored.envelopes[0].exponentialCurves);
}

TEST(master_volume_modulation_reaches_audio_in_control_domain) {
    Patch p = sineTablePatch();
    p.masterVolume = 0.25; p.globals.masterModulationPower = 2.0; p.macroValues[0] = 0.5;
    const auto baseline = render(p, 60, 24000);
    p.modulation.push_back(route(ModSource::macro, 0, ModTarget::globalParam, 0, int(GlobalParam::masterVolume), 50.0));
    const auto moved = render(p, 60, 24000);
    CHECK_NEAR(rms(moved.l, 12000, 24000) / rms(baseline.l, 12000, 24000), 2.25, 0.001);
    p.globals.masterAfterEffects = true;
    const auto post = render(p, 60, 24000);
    CHECK_NEAR(rms(post.l, 12000, 24000) / rms(baseline.l, 12000, 24000), 2.25, 0.001);
}

TEST(post_effects_master_changes_level_without_driving_distortion_differently) {
    Patch p = sineTablePatch(); p.globals.masterAfterEffects = true;
    auto effect = makeFx(FxType::distortion); setParam(effect, fx::xDrive, 75); setParam(effect, fx::xWet, 100);
    p.fx.push_back(effect);
    const auto full = render(p, 60, 24000);
    p.masterVolume = 0.25;
    const auto quiet = render(p, 60, 24000);
    for (std::size_t i = 1000; i < quiet.l.size(); ++i) CHECK_NEAR(quiet.l[i] * 4.0, full.l[i], 1e-6);
}

TEST(measured_unison_detune_and_shared_phase_are_native_patch_options) {
    Patch p = sineTablePatch();
    auto& o = p.oscillators[0];
    o.unison = 2; o.detuneExponent = 1.25; o.detunePolarity = -1.0; o.spreadUnisonPhase = false;
    o.detune = 0.0;
    const auto coherent = render(p, 60, 48000);
    CHECK(coherent.l == coherent.r);
    o.detune = 0.25;
    const auto spread = render(p, 60, 48000);
    const double left = 1200.0 * std::log2(peakHzDft(spread.l, 12000, 240, 285) / 261.6255653);
    const double right = 1200.0 * std::log2(peakHzDft(spread.r, 12000, 240, 285) / 261.6255653);
    CHECK_NEAR(left, 35.355, 2.0); CHECK_NEAR(right, -35.355, 2.0);
    const auto restored = patchFromJson(patchToJson(p));
    CHECK_NEAR(restored.oscillators[0].detuneExponent, 1.25, 1e-12);
    CHECK(!restored.oscillators[0].spreadUnisonPhase);
}

TEST(inline_audio_codec_preserves_float_samples_and_rejects_malformed_native_state) {
    for(int channels : {1,2}) {
        SampleData source;source.sampleRate=24000;source.rootNote=45;
        source.left={-0.0f,.125f,-2.5f,3.0f,std::numeric_limits<float>::min()};
        if(channels==2)source.right={.5f,-.25f,.75f,-1.0f,0.0f};
        const auto json=embeddedSampleToJson(source);const auto decoded=embeddedSampleFromJson(json);
        CHECK(decoded->left==source.left && decoded->right==source.right);
        CHECK(std::signbit(decoded->left[0]));CHECK(decoded->sampleRate==24000 && decoded->rootNote==45);
        std::vector<Json> bad;
        auto j=json;j["frames"]=maxEmbeddedSampleFrames+1;bad.push_back(j);
        j=json;j["frames"]=-1;bad.push_back(j);
        j=json;j["frames"]=2.5;bad.push_back(j);
        j=json;j["sampleRate"]=0;bad.push_back(j);
        j=json;j["sampleRate"]="44100";bad.push_back(j);
        j=json;j["rootNote"]=128;bad.push_back(j);
        j=json;j["channels"]=Json::array();bad.push_back(j);
        j=json;j["channels"].push_back("bad");j["channels"].push_back("bad");bad.push_back(j);
        j=json;j["channels"][0]="invalid!";bad.push_back(j);
        j=json;auto text=j["channels"][0].get<std::string>();text[0]='!';j["channels"][0]=text;bad.push_back(j);
        j=json;j["encoding"]="unknown";bad.push_back(j);
        j=json;j["frames"]=2;j["channels"]=Json::array({"AACAfwAAgH8="});bad.push_back(j); // +infinity twice
        for(const auto& broken:bad) {
            bool rejected=false;try {embeddedSampleFromJson(broken);}catch(const std::exception&){rejected=true;}
            CHECK(rejected);
        }
    }
}
TEST(imported_inline_audio_precedes_paths_and_native_recall_needs_no_serum_sidecar) {
    Json planar=Json::array({Json::array(),Json::array()});
    for(int i=0;i<4096;++i) {
        planar[0].push_back(.5*std::sin(6.283185307179586*250*i/24000));
        planar[1].push_back(.3*std::cos(6.283185307179586*375*i/24000));
    }
    for(const auto& family : {std::pair{"kOsc_Sample","SampleOsc0"},std::pair{"kOsc_Granular","GranularOsc0"},std::pair{"kOsc_Spectral","SpectralOsc0"}}) {
        auto d=doc();d.state["Oscillator0"]["plainParams"]={{"kParamType",family.first},{"kParamVolume",1.0}};
        d.state["Oscillator0"][family.second]={{"samplePathRelative","missing.wav"},{"numFrames",1},{"numChannels",8},
            {"embedded_audio",{{"sampleRate",24000.0},{"data",planar},{"table_name","Original test audio"}}}};
        auto p=importSerum(d,{});auto& o=p.oscillators[0];
        CHECK(o.sample && o.sample->frames()==4096 && o.sample->stereo());CHECK(o.asset.empty());
        CHECK_NEAR(o.sample->left[1],planar[0][1].get<double>(),1e-7);
        CHECK_NEAR(o.sample->right[1],planar[1][1].get<double>(),1e-7);
        CHECK(o.sample->sampleRate==24000 && o.tableName=="Original test audio");
        CHECK(std::none_of(p.diagnostics.begin(),p.diagnostics.end(),[](const Diagnostic& x){return x.status=="missing_asset" || x.status=="unsupported_asset";}));
        int fileReads=0;auto decoder=[&](const std::filesystem::path&,SampleData&,std::string&){++fileReads;return false;};
        prepareAssets(p,decoder);CHECK(fileReads==0);
        CHECK((o.mode==OscMode::spectral)==bool(o.spectral));
        SynthEngine engine;engine.prepare(sr);
        std::array<float,512> left{},right{};
        const auto before=g_allocations.load();g_countAllocations=true;
        engine.setPatch(&p);engine.noteOn(1,60,1);engine.render(left.data(),right.data(),0,512);
        g_countAllocations=false;CHECK(g_allocations.load()==before);
        p.unknownSerumState=Json{};p.originalPreset.clear();o.modeState=Json{};o.additional=Json{};
        const auto json=patchToJson(p);CHECK(json["oscillators"][0].contains("embeddedSample"));
        auto recalled=patchFromJson(json);CHECK(!recalled.oscillators[0].spectral);
        prepareAssets(recalled,decoder);CHECK(fileReads==0);
        CHECK(recalled.oscillators[0].sample->left==o.sample->left && recalled.oscillators[0].sample->right==o.sample->right);
        CHECK((o.mode==OscMode::spectral)==bool(recalled.oscillators[0].spectral));
        p.routes[0].target=recalled.routes[0].target=RouteTarget::main;
        p.envelopes[0].hold=recalled.envelopes[0].hold=1;
        const auto a=render(p,60,12000),b=render(recalled,60,12000);
        CHECK(finite(a.l) && finite(a.r) && peak(a.l)>1e-5);CHECK(a.l==b.l && a.r==b.r);
    }
}
TEST(imported_inline_audio_validates_channels_rates_and_samples_with_explicit_diagnostics) {
    Json valid={{"sampleRate",44100},{"data",Json::array({Json::array({.1,.2,.3})})}};
    std::vector<Json> bad;
    auto x=valid;x["data"]=Json::array({Json::array({.1,.2}),Json::array({.1})});bad.push_back(x);
    x=valid;x["data"]=Json::array({Json::array({.1})});bad.push_back(x);
    x=valid;x["data"].push_back(Json::array({.1,.2,.3}));x["data"].push_back(Json::array({.1,.2,.3}));bad.push_back(x);
    x=valid;x["data"][0][0]=".1";bad.push_back(x);
    x=valid;x["data"][0][0]=1e100;bad.push_back(x);
    x=valid;x["data"][0][0]=std::numeric_limits<double>::quiet_NaN();bad.push_back(x);
    x=valid;x["sampleRate"]="44100";bad.push_back(x);
    x=valid;x["sampleRate"]=0;bad.push_back(x);
    x=valid;x["sampleRate"]=std::numeric_limits<double>::infinity();bad.push_back(x);
    x=valid;x.erase("data");bad.push_back(x);
    bad.push_back("not an object");
    for(const auto& payload:bad) {
        auto d=doc();d.state["Oscillator0"]["plainParams"]={{"kParamType","kOsc_Sample"}};
        d.state["Oscillator0"]["SampleOsc0"]={{"sampleRate",44100},{"embedded_audio",payload}};
        auto p=importSerum(d,{});CHECK(!p.oscillators[0].sample);
        prepareAssets(p,[](const std::filesystem::path&,SampleData&,std::string&){return false;});
        CHECK(std::count_if(p.diagnostics.begin(),p.diagnostics.end(),[](const Diagnostic& x){return x.path=="Oscillator0" && x.status=="unsupported_asset";})==1);
        CHECK(std::none_of(p.diagnostics.begin(),p.diagnostics.end(),[](const Diagnostic& x){return x.path=="Oscillator0" && x.status=="missing_asset";}));
        // NaN is intentionally rejected above and is not equal even to itself.
        CHECK(p.unknownSerumState.dump()==d.state.dump());
    }
    std::string error;auto mono=decodePlanarSample(valid["data"],44100,60,error);
    CHECK(mono && mono->right.empty() && error.empty());
}
TEST(importer_uses_embedded_samples_and_explicit_frame_interpolation) {
    auto d = doc();
    std::vector<float> audio(4096);
    for (int i = 0; i < 2048; ++i) {
        audio[std::size_t(i)] = float(std::sin(6.283185307179586 * i / 2048));
        audio[std::size_t(i + 2048)] = float(std::sin(3 * 6.283185307179586 * i / 2048));
    }
    d.state["Oscillator0"]["WTOsc0"] = {{"numFrames", 18432}, {"numChannels", 1}, {"embeddedWTData", audio},
        {"plainParams", {{"kParamTablePos", 128.0}, {"kParamInitialPhase", 0.0}, {"kParamRandomPhase", 0.0}}}};
    auto p = importSerum(d, {});
    p.routes[0].target = RouteTarget::main;
    CHECK(p.oscillators[0].audio.size() == 4096);
    CHECK(p.oscillators[0].frameBinPosition && !p.oscillators[0].interpolateFrames);
    const auto stepped = render(p, 60, 24000);
    p.oscillators[0].tablePosition = 0.0;
    const auto first = render(p, 60, 24000);
    CHECK(stepped.l == first.l);
    d.state["Oscillator0"]["WTOsc0"]["plainParams"]["kParamXfadeMode"] = 1.0;
    p = importSerum(d, {}); p.routes[0].target = RouteTarget::main;
    CHECK(p.oscillators[0].interpolateFrames);
    const auto interpolated = render(p, 60, 24000);
    CHECK(peakHzDft(interpolated.l, 12000, 100, 1000) > 780.0);
    const auto back = patchFromJson(patchToJson(p));
    CHECK(back.oscillators[0].interpolateFrames && back.oscillators[0].frameBinPosition);
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
    d.state["ModSlot0"]["plainParams"]["kParamAuxCurveData"] = 1.0;
    d.state["ModSlot0"]["flex"] = Json::array({Json::object(),
        {{"xVals", {0.0, 1.0}}, {"yVals", {0.75, 0.75}}, {"curveVals", {0.5, 0.5}}}});
    auto p = importSerum(d, {});
    CHECK(p.fx.size() == 2 && p.fx[1].fxType == FxType::reverb && p.fx[1].modeVariant == 2 && p.fx[1].rack == 1 && p.fx[1].position == 1);
    CHECK_NEAR(p.fx[0].p[fx::dTimeL], 0.3, 1e-12);
    CHECK(p.modulation.size() == 3);
    const auto& r0 = p.modulation[0];
    CHECK(r0.sourceKind == ModSource::velocity && r0.auxKind == ModSource::macro && r0.auxIndex == 1 && r0.auxInverted && r0.bipolar);
    CHECK(r0.targetKind == ModTarget::fxParam && r0.targetIndex == 1 && r0.targetParam == fx::rSize);
    CHECK_NEAR(dsp::evalCurve(r0.auxCurve, 0.6), 0.25, 1e-12);
    CHECK(std::any_of(p.diagnostics.begin(), p.diagnostics.end(), [](const Diagnostic& value) {
        return value.path == "ModSlot0.flex[1]" && value.status == "not_rendered_parameter";
    }));
    CHECK_NEAR(dsp::evalCurve(patchFromJson(patchToJson(p)).modulation[0].auxCurve, 0.6), 0.25, 1e-12);
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

TEST(patch_binding_with_drawn_lfos_does_not_allocate_on_audio_thread) {
    Patch first = sineTablePatch(), next = first;
    for (auto& lfo : next.lfoDefinitions) {
        lfo.shape = LfoShape::curve;
        lfo.path.x = {0, 0.25, 0.5, 0.75, 1};
        lfo.path.y = {0, 0.8, 0.2, 1, 0};
        lfo.path.bend.assign(5, 0.5);
        lfo.pointMods.push_back({1, 0, 1});
    }
    SynthEngine engine; engine.prepare(sr); engine.setPatch(&first);
    g_allocations = 0; g_countAllocations = true;
    engine.setPatch(&next);
    engine.setPatch(&first);
    engine.setPatch(&next);
    g_countAllocations = false;
    CHECK_MSG(g_allocations.load() == 0, "patch binding allocated " << g_allocations.load() << " times");
}

TEST(ultrasonic_fundamentals_do_not_fold_back_into_audible_notes) {
    auto p = sineTablePatch();
    p.oscillators[0].octave = 2; // MIDI 127 transposed above 48 kHz Nyquist
    const auto table = render(p, 127, 4096);
    CHECK(peak(table.l) < 1e-8 && peak(table.r) < 1e-8);
    p.oscillators[0].mode = OscMode::sub;
    for (auto shape : {SubShape::unknown, SubShape::saw, SubShape::square, SubShape::triangle, SubShape::pulse, SubShape::roundedRectangle}) {
        p.oscillators[0].subShape = shape;
        const auto sub = render(p, 127, 4096);
        CHECK(peak(sub.l) < 1e-8 && peak(sub.r) < 1e-8);
    }
}

TEST(high_pitch_rounded_sub_rejects_the_folded_seventh_harmonic) {
    auto p = sineTablePatch(); p.oscillators[0].mode = OscMode::sub;
    p.oscillators[0].subShape = SubShape::roundedRectangle;
    constexpr int size = 4096;
    const double frequency = 440.0 * std::exp2((108.0 - 69.0) / 12.0);
    SynthEngine engine; engine.prepare(frequency * size / 400.0); engine.setSafetyLimiter(false);
    engine.setPatch(&p); engine.noteOn(1, 108, 1);
    std::vector<float> left(size * 2), right(size * 2);
    engine.render(left.data(), right.data(), 0, size * 2);
    auto magnitude = [&](int bin) {
        double re = 0, im = 0;
        for (int i = 0; i < size; ++i) {
            const double phase = 6.283185307179586 * bin * i / size;
            re += left[std::size_t(size + i)] * std::cos(phase);
            im -= left[std::size_t(size + i)] * std::sin(phase);
        }
        return std::hypot(re, im);
    };
    CHECK(magnitude(400) > 500);
    CHECK(magnitude(1296) / magnitude(400) < 1.0e-4); // 7*400 folds from 2800 to 1296
}

TEST(new_preset_releases_old_voices_but_same_preset_edits_keep_them) {
    auto p = sineTablePatch(); p.presetId = 10;
    SynthEngine engine; engine.prepare(sr); engine.setPatch(&p); engine.noteOn(1, 60, 1);
    auto edit = p; edit.oscillators[0].volume = 0.5;
    engine.setPatch(&edit); CHECK(engine.activeVoiceCount() == 1);
    auto next = edit; next.presetId = 11;
    engine.setPatch(&next); CHECK(engine.activeVoiceCount() == 0);
}

TEST(live_multisample_region_removal_does_not_read_retired_regions) {
    auto p = sineTablePatch(); auto sample = std::make_shared<SampleData>();
    sample->sampleRate = sr; sample->left.assign(4096, 0.5f);
    auto& osc = p.oscillators[0]; osc.mode = OscMode::multisample;
    SampleRegion region; region.sample = sample; osc.regions = {region};
    SynthEngine engine; engine.prepare(sr); engine.setSafetyLimiter(false); engine.setPatch(&p);
    engine.noteOn(1, 60, 1);
    std::vector<float> left(512), right(512); engine.render(left.data(), right.data(), 0, 512);
    CHECK(peak(left) > 0.1);
    auto edit = p; edit.oscillators[0].regions.clear();
    engine.setPatch(&edit); left.assign(512, 0); right.assign(512, 0);
    engine.render(left.data(), right.data(), 0, 512);
    CHECK(peak(left) < 1e-8 && peak(right) < 1e-8);
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
