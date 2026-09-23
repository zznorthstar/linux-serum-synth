#include "SerumImporter.h"
#include "SynthEngine.h"
#include "Oversampling.h"
#include "Wavetable.h"
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
        auto renderAttack = [&](zyg::Patch& patch) {
            std::vector<float> out(256), scratch(256);
            synth.setPatch(&patch); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
            synth.render(out.data(), scratch.data(), 0, int(out.size()));
            return out;
        };
        auto fixedA = renderAttack(init), fixedB = renderAttack(init);
        if (fixedA != fixedB) throw std::runtime_error("zero random phase did not retrigger deterministically");
        auto randomized = init;
        randomized.oscillators[0].randomPhase = 100.0;
        auto randomA = renderAttack(randomized), randomB = renderAttack(randomized);
        double phaseDifference = 0.0;
        for (std::size_t i = 0; i < randomA.size(); ++i)
            phaseDifference += std::abs(randomA[i] - randomB[i]);
        if (phaseDifference < 0.01) throw std::runtime_error("random phase had no rendered effect");

        // Envelope curve values are native DSP state, not display-only data.
        // Opposite attack bends must produce measurably different onset energy.
        auto attackEnergy = [&](double curve) {
            auto patch = init;
            patch.envelopes[0].attack = 0.1;
            patch.envelopes[0].curve[0] = curve;
            synth.setPatch(&patch); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
            std::vector<float> output(2400), other(2400);
            synth.render(output.data(), other.data(), 0, int(output.size()));
            double energy = 0.0;
            for (float sample : output) energy += sample * sample;
            return energy;
        };
        const double slowAttackEnergy = attackEnergy(0.0);
        const double fastAttackEnergy = attackEnergy(100.0);
        if (!(fastAttackEnergy > slowAttackEnergy * 4.0))
            throw std::runtime_error("envelope attack curve did not affect DSP shape");

        // Arbitrary wavetables must be prepared into harmonic-limited mip
        // levels off the audio thread, and the oscillator must select those
        // levels at high pitch. Bin 192 is the foldback of the saw's 20th
        // harmonic for this exact-bin render; it should be strongly reduced.
        zyg::Patch rawSaw;
        auto& saw = rawSaw.oscillators[0];
        saw.enabled = true;
        saw.mode = zyg::OscMode::wavetable;
        saw.frameSize = 2048;
        saw.audio.resize(2048);
        for (std::size_t i = 0; i < saw.audio.size(); ++i)
            saw.audio[i] = float(2.0 * double(i) / double(saw.audio.size()) - 1.0);
        rawSaw.routes[0].target = zyg::RouteTarget::main;
        rawSaw.envelopes[0].attack = 0.0;
        rawSaw.envelopes[0].hold = 1.0;
        rawSaw.masterVolume = 1.0;
        auto bandlimitedSaw = rawSaw;
        zyg::prepareWavetableMipmaps(bandlimitedSaw.oscillators[0]);
        if (bandlimitedSaw.oscillators[0].wavetableMipLevels != 10
            || !bandlimitedSaw.oscillators[0].wavetableMipmaps
            || bandlimitedSaw.oscillators[0].wavetableMipmaps->size() != 10 * 2048)
            throw std::runtime_error("wavetable mipmap preparation produced the wrong layout");
        constexpr int spectralSize = 4096;
        const double highNoteHz = 440.0 * std::exp2((108.0 - 69.0) / 12.0);
        const double exactBinSampleRate = highNoteHz * spectralSize / 400.0;
        auto foldedBinMagnitude = [&](zyg::Patch& patch) {
            zyg::SynthEngine oscillator;
            oscillator.prepare(exactBinSampleRate);
            oscillator.setPatch(&patch);
            oscillator.noteOn(1, 108, 1.0f);
            std::vector<float> output(spectralSize), other(spectralSize);
            oscillator.render(output.data(), other.data(), 0, spectralSize);
            double real = 0.0, imaginary = 0.0;
            for (int i = 0; i < spectralSize; ++i) {
                const double angle = 6.283185307179586 * 192.0 * i / spectralSize;
                real += output[std::size_t(i)] * std::cos(angle);
                imaginary -= output[std::size_t(i)] * std::sin(angle);
            }
            return std::hypot(real, imaginary);
        };
        const double rawFoldback = foldedBinMagnitude(rawSaw);
        const double limitedFoldback = foldedBinMagnitude(bandlimitedSaw);
        if (!(rawFoldback > 1.0 && limitedFoldback < rawFoldback * 0.1))
            throw std::runtime_error("bandlimited wavetable oscillator did not suppress high-pitch foldback");

        // The oscillator-warp 2x decimator must preserve the musical band and
        // strongly reject content above the base-rate Nyquist boundary.
        auto firMagnitude = [](double normalizedToOversampledNyquist) {
            double real = 0.0, imaginary = 0.0;
            for (std::size_t i = 0; i < zyg::oversamplingFir.size(); ++i) {
                const double angle = 3.141592653589793 * normalizedToOversampledNyquist * double(i);
                real += zyg::oversamplingFir[i] * std::cos(angle);
                imaginary -= zyg::oversamplingFir[i] * std::sin(angle);
            }
            return std::hypot(real, imaginary);
        };
        if (!(firMagnitude(0.40) > 0.95 && firMagnitude(0.55) < 0.0001))
            throw std::runtime_error("2x oscillator-warp decimator response is outside design bounds");

        // Audio-rate FM must use an enabled oscillator even when its audible
        // level is zero, and a macro route to warp depth must change audio.
        zyg::Patch fmPatch;
        for (int oscillatorIndex = 0; oscillatorIndex < 2; ++oscillatorIndex) {
            auto& oscillator = fmPatch.oscillators[std::size_t(oscillatorIndex)];
            oscillator.enabled = true;
            oscillator.mode = zyg::OscMode::wavetable;
            oscillator.audio.resize(2048);
            for (int i = 0; i < 2048; ++i)
                oscillator.audio[std::size_t(i)] = float(std::sin(6.283185307179586 * i / 2048.0));
            zyg::prepareWavetableMipmaps(oscillator);
        }
        fmPatch.oscillators[1].octave = 1;
        fmPatch.oscillators[1].volume = 0.0;
        fmPatch.oscillators[0].warpDefinitions[0] = {zyg::WarpMode::frequencyMod, 1};
        fmPatch.oscillators[0].warpOne = "native frequency modulation";
        fmPatch.routes[0].target = zyg::RouteTarget::main;
        fmPatch.routes[1].target = zyg::RouteTarget::none;
        fmPatch.envelopes[0].attack = 0.0;
        fmPatch.envelopes[0].hold = 1.0;
        zyg::ModulationRoute fmDepth;
        fmDepth.sourceKind = zyg::ModSource::macro;
        fmDepth.sourceIndex = 0;
        fmDepth.targetKind = zyg::ModTarget::warpOneAmount;
        fmDepth.targetIndex = 0;
        fmDepth.amount = 65.0;
        fmPatch.macroValues[0] = 1.0;
        fmPatch.modulation.push_back(fmDepth);
        auto renderFm = [&](zyg::Patch patch) {
            zyg::SynthEngine oscillator;
            oscillator.prepare(48000.0);
            oscillator.setPatch(&patch);
            oscillator.noteOn(1, 48, 1.0f);
            std::vector<float> output(8192), other(8192);
            oscillator.render(output.data(), other.data(), 0, int(output.size()));
            return output;
        };
        const auto fmAudio = renderFm(fmPatch);
        auto disabledModulator = fmPatch;
        disabledModulator.oscillators[1].enabled = false;
        const auto noFmAudio = renderFm(disabledModulator);
        double fmDifference = 0.0, fmEnergy = 0.0;
        for (std::size_t i = 128; i < fmAudio.size(); ++i) {
            if (!std::isfinite(fmAudio[i])) throw std::runtime_error("audio-rate FM produced non-finite output");
            fmDifference += std::abs(fmAudio[i] - noFmAudio[i]);
            fmEnergy += fmAudio[i] * fmAudio[i];
        }
        if (!(fmDifference > 10.0 && fmEnergy > 0.01))
            throw std::runtime_error("zero-level oscillator did not act as an audio-rate FM source");
        auto noDepthRoute = fmPatch;
        noDepthRoute.modulation.clear();
        const auto noDepthAudio = renderFm(noDepthRoute);
        double routeDifference = 0.0;
        for (std::size_t i = 128; i < fmAudio.size(); ++i)
            routeDifference += std::abs(fmAudio[i] - noDepthAudio[i]);
        if (routeDifference < 10.0)
            throw std::runtime_error("macro-to-warp-depth route had no audio effect");
        // Two carriers using the same audio-rate source must remain sample
        // aligned even when that source is rendered between them (A, source B,
        // then C). This catches oscillator-index-dependent modulation timing.
        auto orderedFm = fmPatch;
        orderedFm.modulation.clear();
        orderedFm.oscillators[0].warpOneAmount = 0.65;
        orderedFm.oscillators[0].pan = -1.0;
        orderedFm.oscillators[2] = orderedFm.oscillators[0];
        orderedFm.oscillators[2].pan = 1.0;
        orderedFm.routes[0].target = zyg::RouteTarget::main;
        orderedFm.routes[1].target = zyg::RouteTarget::none;
        orderedFm.routes[2].target = zyg::RouteTarget::main;
        zyg::SynthEngine orderedEngine;
        orderedEngine.prepare(48000.0);
        orderedEngine.setPatch(&orderedFm);
        orderedEngine.noteOn(1, 48, 1.0f);
        std::vector<float> orderedLeft(4096), orderedRight(4096);
        orderedEngine.render(orderedLeft.data(), orderedRight.data(), 0, int(orderedLeft.size()));
        double channelDifference = 0.0;
        for (std::size_t i = 128; i < orderedLeft.size(); ++i)
            channelDifference += std::abs(orderedLeft[i] - orderedRight[i]);
        if (channelDifference > 1.0e-4)
            throw std::runtime_error("audio-rate oscillator modulation depended on render order");
        auto alignedDry = fmPatch;
        alignedDry.modulation.clear();
        alignedDry.oscillators[0].warpDefinitions[0] = {};
        alignedDry.oscillators[0].octave = 4;
        alignedDry.oscillators[1].enabled = false;
        auto alignedOversampled = alignedDry;
        alignedOversampled.oscillators[0].warpDefinitions[0] = {zyg::WarpMode::softClip, -1};
        alignedOversampled.oscillators[0].warpOneAmount = 0.0;
        const auto dryAlignedAudio = renderFm(alignedDry);
        const auto oversampledAlignedAudio = renderFm(alignedOversampled);
        double dot = 0.0, dryPower = 0.0, oversampledPower = 0.0;
        for (std::size_t i = 256; i < dryAlignedAudio.size(); ++i) {
            dot += dryAlignedAudio[i] * oversampledAlignedAudio[i];
            dryPower += dryAlignedAudio[i] * dryAlignedAudio[i];
            oversampledPower += oversampledAlignedAudio[i] * oversampledAlignedAudio[i];
        }
        const double correlation = dot / std::sqrt(dryPower * oversampledPower);
        if (correlation < 0.99)
            throw std::runtime_error("oversampled and dry oscillator paths lost phase alignment");
        auto distorted = alignedDry;
        distorted.oscillators[0].octave = 0;
        distorted.oscillators[0].warpDefinitions[0] = {zyg::WarpMode::hardClip, -1};
        distorted.oscillators[0].warpDefinitions[1] = {zyg::WarpMode::sineFold, -1};
        distorted.oscillators[0].warpOneAmount = 0.75;
        distorted.oscillators[0].warpTwoAmount = 0.6;
        auto distortionDry = distorted;
        distortionDry.oscillators[0].warpDefinitions = {};
        const auto distortedAudio = renderFm(distorted);
        const auto distortionDryAudio = renderFm(distortionDry);
        double distortionDifference = 0.0;
        for (std::size_t i = 256; i < distortedAudio.size(); ++i) {
            if (!std::isfinite(distortedAudio[i]) || std::abs(distortedAudio[i]) > 2.0f)
                throw std::runtime_error("dual nonlinear warp became unstable");
            distortionDifference += std::abs(distortedAudio[i] - distortionDryAudio[i]);
        }
        if (distortionDifference < 10.0)
            throw std::runtime_error("dual hard-clip/sine-fold warp had no audio effect");
        // A UI patch handoff must not silence a held note. The filter edit
        // must change the waveform while the voice continues to render.
        auto edited = init;
        edited.routes[0].target = zyg::RouteTarget::filter;
        edited.filters[0].enabled = true;
        edited.filters[0].cutoff = 0.05;
        synth.setPatch(&edited);
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        synth.render(left.data(), right.data(), 0, 1024);
        const float heldPeak = *std::max_element(left.begin(), left.begin() + 1024);
        if (!(heldPeak > 1e-5f && std::isfinite(heldPeak)))
            throw std::runtime_error("live filter edit silenced the held note or became unstable");
        auto filterPatch = init;
        filterPatch.routes[0].target = zyg::RouteTarget::filter;
        filterPatch.filters[0].enabled = true;
        filterPatch.filters[0].cutoff = 0.37;
        auto filterEnergy = [&](zyg::Patch& p) {
            synth.setPatch(&p); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
            std::fill(left.begin(), left.end(), 0);
            synth.render(left.data(), right.data(), 0, 4096);
            double energy = 0;
            for (int i = 1024; i < 4096; ++i) {
                if (!std::isfinite(left[std::size_t(i)]) || std::abs(left[std::size_t(i)]) > 10)
                    throw std::runtime_error("resonant filter produced unstable output");
                energy += left[std::size_t(i)] * left[std::size_t(i)];
            }
            return energy;
        };
        const auto plainEnergy = filterEnergy(filterPatch);
        filterPatch.filters[0].resonance = 80;
        const auto resonantEnergy = filterEnergy(filterPatch);
        filterPatch.filters[0].resonance = 0;
        filterPatch.filters[0].drive = 80;
        const auto drivenEnergy = filterEnergy(filterPatch);
        if (std::abs(resonantEnergy - plainEnergy) < plainEnergy * 0.05 ||
            std::abs(drivenEnergy - plainEnergy) < plainEnergy * 0.05)
            throw std::runtime_error("filter resonance or drive had no audible DSP effect");
        synth.noteOff(1, 60);
        std::fill(left.begin(), left.end(), 0);
        std::fill(right.begin(), right.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        if (std::abs(left.back()) > 1e-5f) throw std::runtime_error("release did not complete");

        // A native LFO 1 matrix route must alter the rendered table position.
        zyg::Patch moving = init;
        moving.oscillators[0].audio.resize(4096);
        std::fill(moving.oscillators[0].audio.begin() + 2048, moving.oscillators[0].audio.end(), 0.0f);
        moving.lfoOneRateHz = 2.0;
        zyg::ModulationRoute route;
        route.source = 6; route.destinationModule = "WTOsc";
        route.destinationParameter = "kParamTablePos"; route.amount = 100.0;
        route.sourceKind = zyg::ModSource::lfo; route.sourceIndex = 0;
        route.targetKind = zyg::ModTarget::wavetablePosition; route.targetIndex = 0;
        moving.modulation.push_back(route);
        auto reference = moving;
        reference.modulation.clear();
        synth.setPatch(&reference); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double referenceEnergy = 0;
        for (float sample : left) referenceEnergy += sample * sample;
        synth.setPatch(&moving); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double movingEnergy = 0;
        for (float sample : left) movingEnergy += sample * sample;
        if (!(movingEnergy < referenceEnergy * 0.8 && movingEnergy > referenceEnergy * 0.01))
            throw std::runtime_error("LFO 1 to WT position route did not affect audio");
        moving.modulation[0].source = 999;
        moving.modulation[0].destinationModule = "unrecognized import label";
        moving.modulation[0].destinationParameter = "unrecognized parameter";
        synth.setPatch(&moving); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double typedEnergy = 0;
        for(float sample:left) typedEnergy += sample*sample;
        if(std::abs(typedEnergy-movingEnergy)>movingEnergy*1e-6)
            throw std::runtime_error("DSP route still depends on raw Serum identifiers");
        moving.modulation[0].source = 25;
        moving.modulation[0].sourceKind = zyg::ModSource::macro;
        moving.modulation[0].sourceIndex = 0;
        moving.macroValues[0] = 1.0;
        synth.setPatch(&moving); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double macroEnergy = 0;
        for(float sample:left) macroEnergy += sample*sample;
        if(!(macroEnergy < referenceEnergy*0.01))
            throw std::runtime_error("Macro 1 to WT position route did not affect audio");

        auto chaosMoving = moving;
        chaosMoving.modulation[0].sourceKind = zyg::ModSource::lfo;
        chaosMoving.modulation[0].sourceIndex = 0;
        chaosMoving.lfoOneSine = false;
        chaosMoving.lfoDefinitions[0].shape = zyg::LfoShape::lorenz;
        chaosMoving.lfoDefinitions[0].rateHz = 3.0;
        synth.setPatch(&chaosMoving); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double chaosEnergy = 0.0;
        for (float sample : left) {
            if (!std::isfinite(sample)) throw std::runtime_error("Lorenz LFO made render non-finite");
            chaosEnergy += sample * sample;
        }
        if (!(chaosEnergy < referenceEnergy * 0.8 && chaosEnergy > referenceEnergy * 0.01))
            throw std::runtime_error("Lorenz LFO route did not affect wavetable position");
        auto unsupportedMoving = chaosMoving;
        unsupportedMoving.lfoDefinitions[0].shape = zyg::LfoShape::unknown;
        synth.setPatch(&unsupportedMoving); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, int(left.size()));
        double unsupportedEnergy = 0.0;
        for (float sample : left) unsupportedEnergy += sample * sample;
        if (std::abs(unsupportedEnergy - referenceEnergy) > referenceEnergy * 1e-6)
            throw std::runtime_error("unsupported imported LFO silently applied constant modulation");

        zyg::Patch subPatch;
        subPatch.oscillators[4].enabled = true;
        subPatch.oscillators[4].mode = zyg::OscMode::sub;
        subPatch.oscillators[4].volume = 0.6;
        subPatch.routes[4].target = zyg::RouteTarget::main;
        synth.setPatch(&subPatch); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, 2048);
        if (*std::max_element(left.begin(), left.begin() + 2048) < 0.01f)
            throw std::runtime_error("native SUB source is silent");

        zyg::Patch noisePatch;
        noisePatch.oscillators[3].enabled = true;
        noisePatch.oscillators[3].mode = zyg::OscMode::noise;
        noisePatch.routes[3].target = zyg::RouteTarget::main;
        synth.setPatch(&noisePatch); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, 2048);
        if (*std::max_element(left.begin(), left.begin() + 2048) < 0.01f)
            throw std::runtime_error("native NOISE source is silent");
        noisePatch.originalPreset = {1}; // imported missing assets must not turn into white noise
        synth.setPatch(&noisePatch); synth.allNotesOff(); synth.noteOn(1, 60, 1.0f);
        std::fill(left.begin(), left.end(), 0);
        synth.render(left.data(), right.data(), 0, 2048);
        if (*std::max_element(left.begin(), left.begin() + 2048) > 1e-7f)
            throw std::runtime_error("missing imported NOISE asset generated unrelated white noise");

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
            bool hasRenderedImportedLfo = false;
            for (const auto& lfo : patch.lfoDefinitions)
                hasRenderedImportedLfo |= lfo.shape == zyg::LfoShape::lorenz
                    || lfo.shape == zyg::LfoShape::rossler || lfo.shape == zyg::LfoShape::randomHold;
            if (hasRenderedImportedLfo && std::none_of(patch.diagnostics.begin(), patch.diagnostics.end(),
                    [](const zyg::Diagnostic& d) { return d.status == "dsp_active" && d.path.starts_with("LFO"); }))
                throw std::runtime_error("rendered imported LFO was not reported as DSP-active");
            for (std::size_t i = 0; i < patch.oscillators.size(); ++i) {
                const auto& oscillator = patch.oscillators[i];
                if (oscillator.warpOne == "kFM_OSC") {
                    if (oscillator.warpDefinitions[0].mode != zyg::WarpMode::frequencyMod
                        || oscillator.warpDefinitions[0].sourceIndex < 0)
                        throw std::runtime_error("real preset kFM_OSC warp did not map to native audio-rate FM");
                }
            }
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
            original.oscillators[0].warpDefinitions[0] = {zyg::WarpMode::frequencyMod, 1};
            original.oscillators[0].warpOne = "native frequency modulation";
            original.oscillators[0].modeState = zyg::Json{{"plainParams", zyg::Json{{"kParamWarpMenu", "kFM_OSC"}}}};
            original.filters[0].enabled = true;
            original.filters[0].cutoff = 0.33;
            original.routes[0].target = zyg::RouteTarget::direct;
            original.envelopes[0].attack = 0.02;
            original.envelopes[0].sustain = 0.8;
            original.lfoOneRateHz = 2.5;
            original.lfoDefinitions[0] = {zyg::LfoShape::lorenz, 0.75, false};
            original.macroValues[0] = 0.75;
            original.modulation.push_back({});
            original.modulation[0].source = 6;
            original.modulation[0].sourceKind = zyg::ModSource::lfo;
            original.modulation[0].destinationModule = "WTOsc";
            original.modulation[0].destinationParameter = "kParamTablePos";
            original.modulation[0].targetKind = zyg::ModTarget::wavetablePosition;
            original.modulation[0].amount = 45;
            original.modulation.push_back({});
            original.modulation[1].targetKind = zyg::ModTarget::warpOneAmount;
            original.modulation[1].targetIndex = 0;
            original.fx.push_back({"FXDelay", 0, 0, true, zyg::Json{{"wet", 20}}, {}});
            original.unknownSerumState = zyg::Json{{"unknown", zyg::Json{{"exact", 123}}}};
            original.originalPreset = {0, 1, 255};
            original.diagnostics.push_back({"FXRack0", "not_rendered", "effect retained"});
            const auto json = zyg::patchToJson(original);
            const auto restored = zyg::patchFromJson(json);
            if (restored.modulation.size() != 2 || restored.modulation[0].amount != 45 ||
                restored.modulation[0].sourceKind != zyg::ModSource::lfo ||
                restored.modulation[0].targetKind != zyg::ModTarget::wavetablePosition ||
                restored.modulation[1].targetKind != zyg::ModTarget::warpOneAmount ||
                restored.fx.size() != 1 || restored.lfoOneRateHz != 2.5 || restored.macroValues[0] != 0.75 ||
                restored.lfoDefinitions[0].shape != zyg::LfoShape::lorenz
                || restored.lfoDefinitions[0].rateHz != 0.75 ||
                restored.unknownSerumState != original.unknownSerumState ||
                restored.originalPreset != original.originalPreset || restored.diagnostics.size() != 1)
                throw std::runtime_error(".zygpreset round trip lost modulation, FX or Serum provenance");
            auto legacyJson = json;
            legacyJson["modulation"][0].erase("sourceKind");
            legacyJson["modulation"][0].erase("sourceIndex");
            legacyJson["modulation"][0].erase("targetKind");
            legacyJson["modulation"][0].erase("targetIndex");
            legacyJson["oscillators"][0].erase("warpDefinitions");
            legacyJson["oscillators"][0]["warpOne"] = "";
            auto legacy = zyg::patchFromJson(legacyJson);
            zyg::mapLegacySerumModulationRoutes(legacy);
            if (legacy.modulation[0].sourceKind != zyg::ModSource::lfo ||
                legacy.modulation[0].targetKind != zyg::ModTarget::wavetablePosition ||
                legacy.oscillators[0].warpDefinitions[0].mode != zyg::WarpMode::frequencyMod ||
                legacy.oscillators[0].warpDefinitions[0].sourceIndex != 1)
                throw std::runtime_error("experimental v2 state did not migrate to native typed DSP");
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
            if (osc.warpDefinitions[0].mode != zyg::WarpMode::frequencyMod
                || osc.warpDefinitions[0].sourceIndex != 1)
                throw std::runtime_error(".zygpreset round trip lost native warp definition");
            if (!restored.filters[0].enabled || std::abs(restored.filters[0].cutoff - original.filters[0].cutoff) > 1e-9)
                throw std::runtime_error(".zygpreset round trip lost filter fields");
            if (restored.routes[0].target != zyg::RouteTarget::direct)
                throw std::runtime_error(".zygpreset round trip lost route target");
            if (std::abs(restored.envelopes[0].attack - original.envelopes[0].attack) > 1e-9)
                throw std::runtime_error(".zygpreset round trip lost envelope fields");
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
