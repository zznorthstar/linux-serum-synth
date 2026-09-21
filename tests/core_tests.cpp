#include "SerumImporter.h"
#include "SynthEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

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
        std::cout << "core tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "core tests failed: " << e.what() << '\n';
        return 1;
    }
}
