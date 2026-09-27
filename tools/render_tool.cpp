// Developer tool: renders a Serum preset through the native ZYG engine offline and reports
// level statistics. Not part of the plugin. Usage:
//   zygzxg_render preset.SerumPreset content_root [--note 48] [--vel 1] [--seconds 3] [--hold 2] [--out out.wav] [--quiet]
#include "Assets.h"
#include "SerumImporter.h"
#include "SynthEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::fprintf(stderr, "usage: %s preset root [--note N] [--vel V] [--seconds S] [--hold S] [--out wav] [--quiet]\n", argv[0]); return 2; }
    int note = 48; double vel = 1.0, seconds = 3.0, hold = 2.0; const char* outPath = nullptr; bool quiet = false, profile = false, nolimit = false, nofx = false, info = false, nomod = false, nowarp = false, nofilter = false; int fxoff = -1, onlyOsc = -1;
    for (int i = (argc > 2 && argv[2][0] != '-') ? 3 : 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--note") && i + 1 < argc) note = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--vel") && i + 1 < argc) vel = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--hold") && i + 1 < argc) hold = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
        else if (!std::strcmp(argv[i], "--quiet")) quiet = true;
        else if (!std::strcmp(argv[i], "--profile")) profile = true;
        else if (!std::strcmp(argv[i], "--nolimit")) nolimit = true;
        else if (!std::strcmp(argv[i], "--nofx")) nofx = true;
        else if (!std::strcmp(argv[i], "--info")) info = true;
        else if (!std::strcmp(argv[i], "--nomod")) nomod = true;
        else if (!std::strcmp(argv[i], "--nowarp")) nowarp = true;
        else if (!std::strcmp(argv[i], "--nofilter")) nofilter = true;
        else if (!std::strcmp(argv[i], "--fxoff") && i + 1 < argc) fxoff = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--osc") && i + 1 < argc) onlyOsc = std::atoi(argv[++i]);
    }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    zyg::AudioDecoder decoder = [&](const std::filesystem::path& file, zyg::SampleData& out, std::string& error) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(file.string())));
        if (!reader || reader->lengthInSamples < 2) { error = "unreadable audio"; return false; }
        const int frames = int(std::min<juce::int64>(reader->lengthInSamples, 96 * 1024 * 1024));
        juce::AudioBuffer<float> buf(int(std::min<unsigned>(reader->numChannels, 2u)), frames);
        reader->read(&buf, 0, frames, 0, true, true);
        out.left.assign(buf.getReadPointer(0), buf.getReadPointer(0) + frames);
        if (buf.getNumChannels() > 1) out.right.assign(buf.getReadPointer(1), buf.getReadPointer(1) + frames); else out.right.clear();
        out.sampleRate = reader->sampleRate; return true;
    };
    try {
        auto patch = zyg::loadSerumFile(argv[1], (argc > 2 && argv[2][0] != '-') ? argv[2] : "");
        zyg::prepareAssets(patch, decoder);
        if (nofx) for (auto& m : patch.fx) m.enabled = false;
        if (nomod) patch.modulation.clear();
        if (nofilter) for (auto& f : patch.filters) f.enabled = false;
        if (nowarp) for (auto& o : patch.oscillators) o.warpDefinitions = {};
        if (fxoff >= 0 && fxoff < int(patch.fx.size())) patch.fx[std::size_t(fxoff)].enabled = false;
        if (onlyOsc >= 0) for (int k = 0; k < 5; ++k) if (k != onlyOsc) patch.oscillators[std::size_t(k)].enabled = false;
        if (info) for (int i = 0; i < 5; ++i) {
            const auto& o = patch.oscillators[std::size_t(i)];
            std::printf("  osc%d en=%d mode=%s asset='%s' audio=%zu sample=%zu regions=%zu spectral=%d vol=%.3f\n", i, o.enabled, zyg::oscModeToString(o.mode).c_str(),
                        o.asset.c_str(), o.audio.size(), o.sample ? o.sample->frames() : 0, o.regions.size(), o.spectral != nullptr, o.volume);
        }
        zyg::SynthEngine synth; synth.prepare(48000.0); synth.setSafetyLimiter(!nolimit); synth.setPatch(&patch);
        const int total = int(seconds * 48000); const int holdSamples = int(hold * 48000);
        std::vector<float> l(std::size_t(total), 0.0f), r(std::size_t(total), 0.0f);
        const auto t0 = std::chrono::steady_clock::now();
        synth.noteOn(1, note, float(vel));
        int at = 0; bool released = false;
        while (at < total) {
            const int n = std::min(512, total - at);
            if (!released && at >= holdSamples) { synth.noteOff(1, note); released = true; }
            synth.render(l.data(), r.data(), at, n); at += n;
        }
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / seconds * 100.0;
        double peak = 0, sum = 0; std::size_t bad = 0;
        for (int i = 0; i < total; ++i) {
            const double a = l[std::size_t(i)], b = r[std::size_t(i)];
            if (!std::isfinite(a) || !std::isfinite(b)) { ++bad; continue; }
            peak = std::max({peak, std::abs(a), std::abs(b)}); sum += a * a + b * b;
        }
        const double rms = std::sqrt(sum / (2.0 * total));
        if (profile) {
            const int win = 6000;
            for (int a = 0; a + win <= total; a += win) {
                double e = 0; for (int i = a; i < a + win; ++i) e += double(l[std::size_t(i)]) * l[std::size_t(i)] + double(r[std::size_t(i)]) * r[std::size_t(i)];
                std::printf("  t=%.3f rms %.1f dB\n", a / 48000.0, 10 * std::log10(std::max(e / (2.0 * win), 1e-18)));
            }
        }
        unsigned notRendered = 0, missing = 0;
        for (const auto& d : patch.diagnostics) { notRendered += d.status == "not_rendered" || d.status == "not_rendered_parameter"; missing += d.status == "missing_asset" || d.status == "unsupported_asset"; }
        std::printf("%s | peak %.4f (%.1f dB) rms %.1f dB | cpu %.1f%% | nonfinite %zu | missing %u | not_rendered %u\n", patch.name.c_str(), peak,
                    20 * std::log10(std::max(peak, 1e-9)), 20 * std::log10(std::max(rms, 1e-9)), cpu, bad, missing, notRendered);
        if (!quiet) for (const auto& d : patch.diagnostics)
            if (d.status == "not_rendered" || d.status == "missing_asset" || d.status == "unsupported_asset" || d.status == "unmapped_parameter" || d.status == "not_rendered_parameter")
                std::printf("   %s | %s | %s\n", d.status.c_str(), d.path.c_str(), d.detail.c_str());
        if (outPath) {
            juce::File f(outPath); f.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> stream(f.createOutputStream());
            if (stream) {
                std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.get(), 48000.0, 2, 24, {}, 0));
                if (w) { stream.release(); const float* ch[2] = {l.data(), r.data()}; w->writeFromFloatArrays(ch, 2, total); }
            }
        }
        return bad ? 1 : 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
}
