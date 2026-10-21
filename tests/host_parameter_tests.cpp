#include "HostParameters.h"
#include "Wavetable.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
std::atomic<long> allocations {0};
thread_local bool countAllocations = false;
class Owner final : public juce::AudioProcessor {
public:
    const juce::String getName() const override { return "Host parameter test"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
zyg::Patch patch() {
    zyg::Patch p;
    auto& o = p.oscillators[0]; o.enabled = true; o.mode = zyg::OscMode::wavetable;
    o.volume = 1; o.interpolateFrames = true; o.audio.assign(4096, 0.5f);
    std::fill(o.audio.begin() + 2048, o.audio.end(), -0.5f);
    zyg::prepareWavetableMipmaps(o);
    p.masterVolume = 1; p.routes[0].target = zyg::RouteTarget::main;
    p.envelopes[0].attack = 0; p.envelopes[0].hold = 100; p.envelopes[0].sustain = 1;
    return p;
}
void set(HostParameters& p, const char* id, float value) {
    auto* parameter = p.state.getParameter(id);
    require(parameter != nullptr, "Missing stable parameter ID");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}

void* operator new(std::size_t n) {
    if (countAllocations) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    try {
        juce::ScopedJuceInitialiser_GUI juceInit;
        Owner owner; HostParameters parameters(owner);
        require(owner.getParameters().size() == int(zyg::hostControlCount), "Wrong host parameter count");
        auto p = patch();
        zyg::SynthEngine engine, reference;
        engine.prepare(48000); reference.prepare(48000);
        engine.setSafetyLimiter(false); reference.setSafetyLimiter(false);
        engine.setPatch(&p); reference.setPatch(&p);
        engine.noteOn(1, 60, 1); reference.noteOn(1, 60, 1);
        parameters.prepare(48000);
        std::array<float, 2048> left {}, right {}, refLeft {}, refRight {};
        parameters.beginBlock();
        parameters.render(engine, left.data(), right.data(), 0, 2048);
        reference.render(refLeft.data(), refRight.data(), 0, 2048);
        require(left == refLeft && right == refRight, "Zero controls changed calibrated preset audio");

        set(parameters, "oscAFrameOffset", 1);
        left.fill(0); right.fill(0);
        parameters.beginBlock();
        allocations = 0; countAllocations = true;
        // Nonzero first index verifies the same coordinates used by MIDI splits.
        parameters.render(engine, left.data(), right.data(), 17, 2000);
        countAllocations = false;
        require(allocations.load() == 0, "Automation render allocated");
        require(left[16] == 0 && left[2017] == 0, "Automation overwrote samples outside MIDI segment");
        double biggestStep = 0;
        for (int i = 18; i < 2017; ++i) {
            require(std::isfinite(left[std::size_t(i)]), "Nonfinite automated output");
            biggestStep = std::max(biggestStep, double(std::abs(left[std::size_t(i)] - left[std::size_t(i - 1)])));
        }
        require(biggestStep < 0.003, "Frame automation produced a control-rate staircase discontinuity");
        zyg::SynthEngine::OscDisplayState display;
        require(engine.oscDisplayState(0, display) && std::abs(display.tablePos - 256) < 1e-4, "Frame offset did not select final frame");
        require(left[2000] < -0.1f, "Frame automation did not reach negative DC frame");

        // APVTS persistence is strictly outside the counted audio path.
        juce::MemoryBlock data;
        { juce::MemoryOutputStream stream(data, false); parameters.state.copyState().writeToStream(stream); }
        parameters.resetState();
        parameters.prepare(48000); // hosts are allowed to prepare before restoring state
        juce::MemoryInputStream input(data, false);
        parameters.restoreState(juce::ValueTree::readFromStream(input));
        require(*parameters.state.getRawParameterValue("oscAFrameOffset") == 1, "APVTS frame offset not restored");
        left.fill(0); right.fill(0); parameters.beginBlock();
        parameters.render(engine, left.data(), right.data(), 0, 32);
        require(left[31] < -0.1f, "State restoration incorrectly ramped from the previous preset's frame");
        parameters.resetState();
        require(*parameters.state.getRawParameterValue("oscAFrameOffset") == 0, "Legacy-state defaults did not reset automation");
        for (int filter = 0; filter < 2; ++filter) {
            auto filtered = patch();
            auto& oscillator = filtered.oscillators[0]; oscillator.audio.resize(2048);
            for (int i = 0; i < 2048; ++i) oscillator.audio[std::size_t(i)] = float(std::sin(6.283185307179586 * i / 2048));
            zyg::prepareWavetableMipmaps(oscillator);
            filtered.filters[std::size_t(filter)].enabled = true;
            filtered.filters[std::size_t(filter)].response = zyg::FilterResponse::low12;
            filtered.filters[std::size_t(filter)].cutoff = 0.1;
            filtered.routes[0].target = zyg::RouteTarget::filter;
            filtered.routes[0].filterBalance = filter == 0 ? -100 : 100;
            filtered.routes[5].target = filtered.routes[6].target = zyg::RouteTarget::main;
            engine.setPatch(&filtered); engine.prepare(48000); engine.noteOn(1, 60, 1);
            parameters.resetState(); parameters.prepare(48000);
            auto energy = [&] {
                for (int block = 0; block < 6; ++block) {
                    left.fill(0); right.fill(0); parameters.beginBlock();
                    parameters.render(engine, left.data(), right.data(), 0, 2048);
                }
                double e = 0; for (float value : left) e += value * value; return e;
            };
            const double closed = energy();
            set(parameters, HostParameters::ids[std::size_t(filter)], 0.6f);
            const double open = energy();
            require(open > closed * 64, "Cutoff host offset did not open its routed filter");
        }
        std::cout << "Host parameter transparency, smoothing, frame selection, MIDI bounds, allocation and state tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        countAllocations = false;
        std::cerr << e.what() << '\n'; return 1;
    }
}
