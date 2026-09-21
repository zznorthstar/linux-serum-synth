#include "SerumImporter.h"
#include "SynthEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void writeLe32(std::vector<std::uint8_t>& bytes, std::uint32_t v) {
    bytes.push_back(std::uint8_t(v)); bytes.push_back(std::uint8_t(v >> 8));
    bytes.push_back(std::uint8_t(v >> 16)); bytes.push_back(std::uint8_t(v >> 24));
}
void writeLe16(std::vector<std::uint8_t>& bytes, std::uint16_t v) {
    bytes.push_back(std::uint8_t(v)); bytes.push_back(std::uint8_t(v >> 8));
}
// Writes a minimal mono 16-bit PCM RIFF WAVE with `frames` samples, each the
// frame index scaled into int16 range, so the decoded floats are checkable.
std::filesystem::path writeSyntheticWav(unsigned frames) {
    std::vector<std::uint8_t> data;
    for (unsigned i = 0; i < frames; ++i) {
        const auto sample = std::int16_t((double(i) / double(frames) - 0.5) * 2.0 * 30000.0);
        data.push_back(std::uint8_t(sample & 0xff));
        data.push_back(std::uint8_t((sample >> 8) & 0xff));
    }
    std::vector<std::uint8_t> bytes;
    auto chunk = [&](const char* id) { bytes.insert(bytes.end(), id, id + 4); };
    chunk("RIFF"); writeLe32(bytes, std::uint32_t(36 + data.size())); chunk("WAVE");
    chunk("fmt "); writeLe32(bytes, 16); writeLe16(bytes, 1); writeLe16(bytes, 1);
    writeLe32(bytes, 44100); writeLe32(bytes, 44100 * 2); writeLe16(bytes, 2); writeLe16(bytes, 16);
    chunk("data"); writeLe32(bytes, std::uint32_t(data.size()));
    bytes.insert(bytes.end(), data.begin(), data.end());
    const auto path = std::filesystem::temp_directory_path() / "zygzxg_core_test_synthetic.wav";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}
}

int main() {
    try {
        zyg::Patch init;
        init.name = "test";
        init.oscillators[0].enabled = true;
        init.oscillators[0].mode = zyg::OscMode::wavetable;
        init.oscillators[0].audio.resize(2048);
        for (int i = 0; i < 2048; ++i) init.oscillators[0].audio[std::size_t(i)] = std::sin(6.283185307179586 * i / 2048);
        init.routes[0].target = zyg::RouteTarget::main;
        zyg::SynthEngine synth;
        synth.prepare(48000);
        synth.setPatch(&init);
        synth.noteOn(1, 60, 1.0f);
        std::vector<float> left(48000), right(48000);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        const auto max = *std::max_element(left.begin(), left.end());
        if (!(max > 0.001f && max < 1.0f)) throw std::runtime_error("init oscillator silent or unstable");
        synth.noteOff(1, 60);
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        if (std::abs(left.back()) > 1e-5f) throw std::runtime_error("release did not complete");

        const char* fixture = std::getenv("ZYG_TEST_PRESET");
        const char* assets = std::getenv("ZYG_TEST_ASSET_ROOT");
        if (fixture && assets) {
            auto patch = zyg::loadSerumFile(fixture, assets);
            if (patch.originalPreset.empty() || patch.unknownSerumState.empty())
                throw std::runtime_error("Serum provenance not preserved");
            if (patch.modulation.empty()) throw std::runtime_error("real preset matrix did not map");
            if (patch.oscillators[0].audio.empty()) throw std::runtime_error("real preset wavetable did not load");
            unsigned unmapped = 0;
            for (const auto& d : patch.diagnostics) unmapped += d.status == "unmapped_parameter";
            if (patch.mappedParameters + unmapped != patch.explicitParameters)
                throw std::runtime_error("explicit parameter accounting is incomplete");
            for (const auto& fx : patch.fx)
                if (fx.type.empty()) throw std::runtime_error("FX module identity was dropped");
            synth.setPatch(&patch);
            synth.noteOn(1, 48, 1.0f);
            std::fill(left.begin(), left.end(), 0);
            std::fill(right.begin(), right.end(), 0);
            synth.render(left.data(), right.data(), 0, int(left.size()));
            const auto peak = *std::max_element(left.begin(), left.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
            if (!(std::abs(peak) > 1e-5f) || !std::isfinite(peak))
                throw std::runtime_error("real Serum preset did not produce finite audio");
            std::cout << zyg::statusSummary(patch) << " | render peak " << peak << '\n';
        }
        {
            zyg::Patch original;
            original.name = "Round Trip";
            original.author = "tester";
            original.masterVolume = 0.42;
            original.mono = true;
            original.polyphony = 4;
            original.oscillators[0].enabled = true;
            original.oscillators[0].mode = zyg::OscMode::wavetable;
            original.oscillators[0].asset = "/tmp/example.wav";
            original.oscillators[0].tablePosition = 123.5;
            original.oscillators[0].unison = 3;
            original.oscillators[0].detune = 0.15;
            original.filters[0].enabled = true;
            original.filters[0].cutoff = 0.33;
            original.routes[0].target = zyg::RouteTarget::direct;
            original.envelopes[0].attack = 0.02;
            original.envelopes[0].sustain = 0.8;
            const auto json = zyg::patchToJson(original);
            const auto restored = zyg::patchFromJson(json);
            if (restored.name != original.name || restored.author != original.author)
                throw std::runtime_error(".zygpreset round trip lost identity fields");
            if (std::abs(restored.masterVolume - original.masterVolume) > 1e-9 || restored.mono != original.mono
                || restored.polyphony != original.polyphony)
                throw std::runtime_error(".zygpreset round trip lost voice/global fields");
            const auto& osc = restored.oscillators[0];
            if (!osc.enabled || osc.mode != zyg::OscMode::wavetable || osc.asset != original.oscillators[0].asset
                || std::abs(osc.tablePosition - original.oscillators[0].tablePosition) > 1e-9
                || osc.unison != original.oscillators[0].unison)
                throw std::runtime_error(".zygpreset round trip lost oscillator fields");
            if (!restored.filters[0].enabled || std::abs(restored.filters[0].cutoff - original.filters[0].cutoff) > 1e-9)
                throw std::runtime_error(".zygpreset round trip lost filter fields");
            if (restored.routes[0].target != zyg::RouteTarget::direct)
                throw std::runtime_error(".zygpreset round trip lost route target");
            if (std::abs(restored.envelopes[0].attack - original.envelopes[0].attack) > 1e-9)
                throw std::runtime_error(".zygpreset round trip lost envelope fields");
            // Fields this v1 format deliberately does not cover yet must stay
            // at their defaults, not silently fabricated.
            if (!restored.modulation.empty() || !restored.fx.empty())
                throw std::runtime_error(".zygpreset unexpectedly fabricated unsupported state");
        }
        {
            const auto path = writeSyntheticWav(2048);
            zyg::Oscillator osc;
            osc.frameSize = 2048;
            std::string error;
            if (!zyg::loadWavetableFromFile(osc, path, error))
                throw std::runtime_error("loadWavetableFromFile failed on a valid synthetic WAV: " + error);
            if (osc.audio.size() != 2048) throw std::runtime_error("loadWavetableFromFile decoded wrong frame count");
            if (osc.audio.front() > -0.85f || osc.audio.back() < 0.85f)
                throw std::runtime_error("loadWavetableFromFile decoded implausible sample values");
            std::filesystem::remove(path);
            zyg::Oscillator missing;
            if (zyg::loadWavetableFromFile(missing, "/nonexistent/zygzxg-test.wav", error))
                throw std::runtime_error("loadWavetableFromFile should fail on a missing file");
        }

        std::cout << "core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "core tests failed: " << e.what() << '\n';
        return 1;
    }
}
