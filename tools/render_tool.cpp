// Developer tool: renders a Serum preset through the native ZYG engine offline and reports
// level statistics. Not part of the plugin. Usage:
//   zygzxg_render preset.SerumPreset content_root [--note 48] [--vel 1] [--seconds 3] [--hold 2] [--start 0] [--prime 0] [--bpm 120] [--out out.wav] [--quiet]
// Output uses 32-bit float WAV so --nolimit measurements above 0 dBFS are retained.
#include "Assets.h"
#include "SerumImporter.h"
#include "SynthEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::fprintf(stderr, "usage: %s preset root [--note N] [--vel V] [--seconds S] [--hold S] [--start S] [--prime S] [--bpm BPM] [--out wav] [--quiet]\n", argv[0]); return 2; }
    int note = 48; double vel = 1.0, seconds = 3.0, hold = 2.0, start = 0.0, prime = 0.0, bpm = 120.0; const char* outPath = nullptr; bool quiet = false, profile = false, nolimit = false, nofx = false, info = false, nomod = false, nowarp = false, nofilter = false; int fxoff = -1, onlyOsc = -1;
    for (int i = (argc > 2 && argv[2][0] != '-') ? 3 : 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--note") && i + 1 < argc) note = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--vel") && i + 1 < argc) vel = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--hold") && i + 1 < argc) hold = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--start") && i + 1 < argc) start = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--prime") && i + 1 < argc) prime = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--bpm") && i + 1 < argc) bpm = std::atof(argv[++i]);
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
    if (!std::isfinite(seconds) || !std::isfinite(hold) || !std::isfinite(start) || !std::isfinite(prime)
        || seconds <= 0.0 || seconds > 3600.0 || hold < 0.0 || start < 0.0 || prime < 0.0
        || (prime > 0.0 && prime >= start)) {
        std::fprintf(stderr, "invalid render timing (priming note must end before --start)\n"); return 2;
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
        synth.setTransport(bpm, 0.0, false);
        const int total = int(seconds * 48000);
        std::vector<float> l(std::size_t(total), 0.0f), r(std::size_t(total), 0.0f);
        const auto t0 = std::chrono::steady_clock::now();
        struct Event { int sample; bool on; };
        std::vector<Event> events;
        auto sampleAt = [&](double time) { return int(std::min(time, seconds) * 48000); };
        if (prime > 0.0) { events.push_back({0, true}); events.push_back({sampleAt(prime), false}); }
        events.push_back({sampleAt(start), true}); events.push_back({sampleAt(start + hold), false});
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.sample < b.sample; });
        int at = 0; std::size_t nextEvent = 0;
        while (at < total) {
            while (nextEvent < events.size() && events[nextEvent].sample <= at) {
                if (events[nextEvent].on) synth.noteOn(1, note, float(vel)); else synth.noteOff(1, note);
                ++nextEvent;
            }
            // Split at every event, including the optional priming note.
            const int boundary = nextEvent < events.size() ? events[nextEvent].sample : total;
            const int n = std::min({512, total - at, boundary - at});
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
            juce::File f(outPath);
            if (f.existsAsFile() && !f.deleteFile()) throw std::runtime_error("Could not replace output WAV");
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> stream(f.createOutputStream());
            if (stream) {
                const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(2)
                    .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
                std::unique_ptr<juce::OutputStream> output(std::move(stream));
                auto writer = wav.createWriterFor(output, options);
                if (!writer) throw std::runtime_error("Could not create float WAV writer");
                const float* channels[2] = {l.data(), r.data()};
                if (!writer->writeFromFloatArrays(channels, 2, total)) throw std::runtime_error("Could not write rendered WAV");
            } else throw std::runtime_error("Could not open output WAV");
        }
        return bad ? 1 : 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "error: %s\n", e.what()); return 1; }
}
