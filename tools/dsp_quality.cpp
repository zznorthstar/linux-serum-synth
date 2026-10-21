// Reproducible offline measurements, deliberately separate from pass/fail tests.
// Uses original synthetic signals; never reads proprietary presets or content.
#include "FxParams.h"
#include "Wavetable.h"
#include "dsp/FxEngine.h"
#include "dsp/Oscillators.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <chrono>
#include <iostream>
#include <vector>

using namespace zyg;
namespace {
constexpr double tau = 6.2831853071795864769;
bool legacyMultiband = false;
bool heldAudioModulation = false;
bool legacyOutputDecimator = false;
void set(FxModule& m, int slot, double value) { m.p[slot] = value; m.set[slot] = true; }
FxModule compressor(double low, double high, double wet = 100) {
    FxModule m; m.fxType = FxType::comp; m.type = "FXComp";
    m.phaseCoherentMultiband = !legacyMultiband;
    applyFxDefaults(m);
    set(m, fx::pMultiband, 1); set(m, fx::pXoverLow, low); set(m, fx::pXoverHi, high);
    set(m, fx::pWet, wet);
    for (int band = 0; band < 3; ++band) {
        set(m, fx::pRatio0 + band, 0); set(m, fx::pRatioBelow0 + band, 0);
    }
    return m;
}
std::vector<double> process(const FxModule& m, const std::vector<double>& input, double sr) {
    auto u = dsp::makeFxUnit(FxType::comp); u->prepare(sr);
    auto l = input, r = input;
    for (std::size_t i = 0; i < l.size(); i += 137)
        u->process(m, m.p.data(), l.data() + i, r.data() + i, int(std::min<std::size_t>(137, l.size() - i)));
    return l;
}
double amplitude(const std::vector<double>& x, double hz, double sr, std::size_t start) {
    std::complex<double> sum{};
    for (std::size_t i = start; i < x.size(); ++i)
        sum += x[i] * std::polar(1.0, -tau * hz * double(i) / sr);
    return 2.0 * std::abs(sum) / double(x.size() - start);
}
Json crossoverMeasurement(double sr, double low, double high, double wet) {
    constexpr double frequencies[] = {40,80,125,200,315,500,800,1250,1600,2000,2500,3150,4000,5000,8000,10000,16000,20000};
    std::vector<double> input(std::size_t(sr * 4));
    for (std::size_t i = 0; i < input.size(); ++i)
        for (double f : frequencies) input[i] += 0.005 * std::sin(tau * f * double(i) / sr);
    auto output = process(compressor(low, high, wet), input, sr);
    Json gains = Json::array(); double lo = 1000, hi = -1000;
    for (double f : frequencies) {
        const double db = 20 * std::log10(amplitude(output, f, sr, std::size_t(sr * 3)) / 0.005);
        lo = std::min(lo, db); hi = std::max(hi, db);
        gains.push_back({{"hz", f}, {"gainDb", db}});
    }
    return {{"sampleRate",sr},{"lowHz",low},{"highHz",high},{"wetPct",wet},
            {"gainSpreadDb",hi-lo},{"maxAbsGainDb",std::max(std::abs(lo),std::abs(hi))},{"tones",gains}};
}
Json dynamicsMeasurement(double sr) {
    auto m = compressor(200, 2500);
    std::vector<double> input(std::size_t(sr * 4), 0.1);
    set(m, fx::pMakeup, 2);
    auto out = process(m, input, sr);
    const double makeupDb = 20 * std::log10(std::abs(out.back()) / input.back());
    set(m, fx::pMakeup, 1);
    for (int b = 0; b < 3; ++b) set(m, fx::pRatio0 + b, 0.75); // 4:1, -30 dB threshold
    Json levels = Json::array();
    for (double db : {-40.0, -20.0, -6.0}) {
        std::fill(input.begin(),input.end(), std::pow(10.0,db/20.0));
        out = process(m,input,sr);
        levels.push_back({{"inputDb",db},{"outputDb",20*std::log10(std::abs(out.back()))},
                          {"expectedOutputDb",db > -30 ? -30 + (db+30)/4 : db}});
    }
    return {{"sampleRate",sr},{"requestedMakeupDb",20*std::log10(2.0)},
            {"measuredMakeupDb",makeupDb},{"steadyDcTransfer",levels}};
}
// A coherent bin avoids mistaking FFT leakage for foldback. For phase modulation
// of a sine, only carrier +/- integer*modulator are legitimate spectral lines.
// This measures energy at all other bins, not a complete perceptual alias score.
double besselInteger(int order, double x) {
    order=std::abs(order); double term=1;
    for(int i=1;i<=order;++i) term*=x/(2*i);
    double sum=term;
    for(int i=1;i<=30;++i) { term*= -x*x/(4*i*(i+order)); sum+=term; }
    return sum;
}
double decimatorMagnitude(double normalizedHz, int quality) {
    auto response=[&](double frequency) {
        std::complex<double> h{};
        if (legacyOutputDecimator) {
            for(std::size_t i=0;i<oversamplingFir.size();++i) h+=oversamplingFir[i]*std::polar(1.0,-tau*frequency*double(i));
        } else {
            for(std::size_t i=0;i<tightOversamplingFir.size();++i) h+=tightOversamplingFir[i]*std::polar(1.0,-tau*frequency*double(i));
        }
        return std::abs(h);
    };
    return response(normalizedHz/2)*(quality ? response(normalizedHz/4):1);
}
Json phaseModMeasurement(int quality, double sr=48000, int carrierBin=2101, int modBin=3331, double depth=1) {
    constexpr int count = 32768, warmup = 2048;
    Oscillator o; o.enabled = true; o.mode = OscMode::wavetable; o.unison = 1;
    o.audio.resize(2048);
    for (int i = 0; i < 2048; ++i) o.audio[std::size_t(i)] = float(std::sin(tau * i / 2048));
    prepareWavetableMipmaps(o);
    o.warpDefinitions[0].mode = WarpMode::phaseMod; o.warpDefinitions[0].sourceIndex = 1;
    o.warpOneAmount = depth;
    o.bandlimitedAudioModulation = !heldAudioModulation;
    dsp::OscValues values{};
    for (std::size_t i = 0; i < values.size(); ++i) values[i] = dsp::oscBaseValue(o, OscParam(i));
    std::array<double,7> mods{};
    dsp::OscInputs in; in.osc = &o; in.v = &values; in.mods = &mods; in.sampleRate = sr;
    in.masterTuning = carrierBin * sr / count; in.semis = 0; in.oversampleLevel = quality;
    in.tightOutputDecimator = !legacyOutputDecimator;
    dsp::OscVoiceState state; state.reset(); dsp::oscNoteOn(state,in); dsp::oscBlockUpdate(state,in);
    std::vector<float> re(count), im(count);
    for (int i = 0; i < count + warmup; ++i) {
        mods[1] = std::sin(tau * modBin * double(i) / count);
        const auto y = dsp::oscRender(state,in);
        if (i >= warmup) re[std::size_t(i-warmup)] = float(y.l);
    }
    dsp::Fft fft; fft.init(count); fft.forward(re.data(),im.data());
    std::vector<bool> legitimate(count/2+1); double maxMagnitudeErrorDb=0, squaredMagnitudeError=0, expectedEnergy=0;
    // Physical in-band PM sidebands. Out-of-band sidebands must not fold back.
    for (int k = -100; k <= 100; ++k) {
        const int bin = std::abs(carrierBin + k * modBin);
        if (bin <= count/2) {
            legitimate[std::size_t(bin)] = true;
            const double expected=std::abs(besselInteger(k,tau*0.25*depth))*decimatorMagnitude(double(bin)/count,quality);
            const double actual=2*std::hypot(re[std::size_t(bin)],im[std::size_t(bin)])/count;
            if(expected>0.01) maxMagnitudeErrorDb=std::max(maxMagnitudeErrorDb,std::abs(20*std::log10(std::max(actual,1e-30)/expected)));
            squaredMagnitudeError+=(actual-expected)*(actual-expected); expectedEnergy+=expected*expected;
        }
    }
    double wanted = 0, folded = 0;
    for (int i = 1; i < count/2; ++i) {
        const double energy = double(re[std::size_t(i)]) * re[std::size_t(i)] + double(im[std::size_t(i)]) * im[std::size_t(i)];
        (legitimate[std::size_t(i)] ? wanted : folded) += energy;
    }
    return {{"oversampling",quality ? 4 : 2},{"sampleRate",sr},{"carrierHz",carrierBin*sr/count},
            {"bandlimitedAudioModulation",!heldAudioModulation},
            {"modulatorHz",modBin*sr/count},{"phaseDepthCycles",0.25*depth},
            {"maxPhysicalMagnitudeErrorDbAboveMinus40dB",maxMagnitudeErrorDb},
            {"relativePhysicalMagnitudeError",std::sqrt(squaredMagnitudeError/std::max(expectedEnergy,1e-30))},
            {"nonPhysicalBinEnergyDb",10*std::log10(std::max(folded,1e-30)/std::max(wanted,1e-30))}};
}
Json filterWarpMeasurement(int quality, double sr) {
    Oscillator o; o.enabled=true; o.mode=OscMode::wavetable; o.unison=1;
    o.audio.resize(2048);
    for(int i=0;i<2048;++i) o.audio[std::size_t(i)]=float(std::sin(tau*i/2048));
    prepareWavetableMipmaps(o);
    o.warpDefinitions[0].mode=WarpMode::filterLow;
    o.warpOneAmount=1-std::log(1000.0/20)/std::log(1000.0);
    dsp::OscValues values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
    dsp::OscInputs in; in.osc=&o; in.v=&values; in.sampleRate=sr;
    in.masterTuning=1000; in.oversampleLevel=quality;
    in.tightOutputDecimator = !legacyOutputDecimator;
    dsp::OscVoiceState state; dsp::oscNoteOn(state,in);
    double energy=0; const int count=int(sr);
    for(int i=0;i<count+2048;++i) {
        const auto y=dsp::oscRender(state,in).l;
        if(i>=2048) energy+=y*y;
    }
    return {{"sampleRate",sr},{"oversampling",quality?4:2},{"inputHz",1000},{"cutoffHz",1000},
            {"gainDb",10*std::log10(2*energy/count)},{"expectedGainDb",10*std::log10(0.5)}};
}
Json modulationBenchmark(int quality, int slots) {
    constexpr int count=262144;
    Oscillator o; o.enabled=true; o.mode=OscMode::wavetable; o.unison=1;
    o.audio.resize(2048);
    for(int i=0;i<2048;++i) o.audio[std::size_t(i)]=float(std::sin(tau*i/2048));
    prepareWavetableMipmaps(o); o.bandlimitedAudioModulation=!heldAudioModulation;
    o.warpOneAmount=o.warpTwoAmount=1;
    for(int i=0;i<slots;++i) { o.warpDefinitions[std::size_t(i)].mode=WarpMode::phaseMod; o.warpDefinitions[std::size_t(i)].sourceIndex=i+1; }
    dsp::OscValues values{};
    for(std::size_t i=0;i<values.size();++i) values[i]=dsp::oscBaseValue(o,OscParam(i));
    std::array<double,7> mods{};
    dsp::OscInputs in; in.osc=&o; in.v=&values; in.sampleRate=48000; in.masterTuning=3077.63671875;
    in.mods=&mods; in.oversampleLevel=quality;
    in.tightOutputDecimator = !legacyOutputDecimator;
    double best=1e9,checksum=0;
    for(int repeat=0;repeat<3;++repeat) {
        dsp::OscVoiceState state; dsp::oscNoteOn(state,in);
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<count;++i) {
            mods[1]=std::sin(tau*3331*double(i)/32768); mods[2]=std::cos(tau*2101*double(i)/32768);
            checksum+=dsp::oscRender(state,in).l;
        }
        best=std::min(best,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
    }
    return {{"oversampling",quality?4:2},{"slots",slots},{"samplesPerTrial",count},{"trials",3},
            {"bandlimitedAudioModulation",!heldAudioModulation},{"bestNanosecondsPerOutputSample",best*1e9/count},
            {"oneOscillatorCpuAt48kPercent",best*48000/count*100},{"checksum",checksum}};
}
}
int main(int argc, char** argv) {
    bool modulationOnly=false,benchmark=false;
    for(int i=1;i<argc;++i) {
        if(std::string(argv[i])=="--legacy-multiband") legacyMultiband=true;
        else if(std::string(argv[i])=="--modulation-only") modulationOnly=true;
        else if(std::string(argv[i])=="--modulation-benchmark") benchmark=true;
        else if(std::string(argv[i])=="--held-audio-modulation") heldAudioModulation=true;
        else if(std::string(argv[i])=="--legacy-output-decimator") legacyOutputDecimator=true;
        else { std::cerr << "Usage: zygzxg_dsp_quality [--legacy-multiband] [--modulation-only|--modulation-benchmark] [--held-audio-modulation] [--legacy-output-decimator]\n"; return 2; }
    }
    Json report; report["schema"] = "zygzxg-native-quality-1";
    report["phaseCoherentMultiband"] = !legacyMultiband;
    report["tightOutputDecimator"] = !legacyOutputDecimator;
    report["scope"] = "Original synthetic multitone, DC transfer and coherent audio-rate PM; no reference assets";
    if(benchmark) {
        for(int q : {0,1}) for(int slots : {1,2}) report["componentBenchmark"].push_back(modulationBenchmark(q,slots));
        std::cout << report.dump(2) << '\n'; return 0;
    }
    if(modulationOnly) {
        for(double sr : {44100.0,48000.0,96000.0})
        for(int carrier : {211,2101,6901}) for(int modulator : {331,3331,10531,13901})
        for(double depth : {0.25,1.0}) for(int q : {0,1})
            report["audioRatePhaseModulation"].push_back(phaseModMeasurement(q,sr,carrier,modulator,depth));
        std::cout << report.dump(2) << '\n'; return 0;
    }
    for (double sr : {44100.0,48000.0,96000.0}) {
        for (auto pair : {std::array<double,2>{200,2500},std::array<double,2>{800,1600}})
            for (double wet : {0.0,50.0,100.0}) report["crossovers"].push_back(crossoverMeasurement(sr,pair[0],pair[1],wet));
        report["dynamics"].push_back(dynamicsMeasurement(sr));
        for(int q : {0,1}) report["filterWarp"].push_back(filterWarpMeasurement(q,sr));
    }
    for (int q : {0,1}) report["audioRatePhaseModulation"].push_back(phaseModMeasurement(q));
    std::cout << report.dump(2) << '\n';
}
