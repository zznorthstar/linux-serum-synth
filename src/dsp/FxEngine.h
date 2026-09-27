#pragma once
// Native ZYG effect rack: 13 effect classes plus three splitters, three racks
// (Main, Bus 1, Bus 2). All effect units are preallocated in prepare() from a
// fixed pool, so binding a patch only re-associates parameters and never
// allocates on the audio thread.
#include "DspCommon.h"
#include "Fft.h"
#include "FxParams.h"
#include "Patch.h"
#include <memory>

namespace zyg {
// Uniformly partitioned impulse response spectra (block 512, FFT 1024).
struct ConvIr {
    static constexpr int block = 512;
    static constexpr int fftSize = 1024;
    int partitions = 0;
    int channels = 1;
    double sampleRate = 44100.0;
    std::vector<float> re, im; // [channel][partition][fftSize]
    std::size_t offset(int channel, int partition) const noexcept {
        return (std::size_t(channel) * std::size_t(partitions) + std::size_t(partition)) * fftSize;
    }
};
// Control-thread only: transforms an impulse response into partition spectra.
std::shared_ptr<const ConvIr> buildConvIr(const SampleData& ir, double maxSeconds = 6.0);
}

namespace zyg::dsp {

// Per-chunk host/engine context shared with tempo- and sidechain-aware units.
struct FxContext {
    double bpm = 120.0;
    double beat = 0.0;                 // quarter-note position at the first sample of the chunk
    unsigned noteSerial = 0;           // increments on every note-on
    const double* scL = nullptr;       // sidechain input for this chunk (null when absent)
    const double* scR = nullptr;
};

class FxUnit {
public:
    virtual ~FxUnit() = default;
    const FxContext* ctx = nullptr;
    virtual void prepare(double sampleRate) = 0;
    virtual void reset() noexcept = 0;
    // `p` holds the effective (modulated) native parameter values, sized fxParamSlots.
    virtual void process(const FxModule& m, const double* p, double* l, double* r, int n) noexcept = 0;
};
std::unique_ptr<FxUnit> makeFxUnit(FxType type);

class FxEngine {
public:
    static constexpr int maxModules = 96;
    static constexpr int chunk = 256;
    void prepare(double sampleRate);
    void reset() noexcept;
    // Context for the next processRack calls (positions refer to the first sample passed).
    void setContext(const FxContext& c) noexcept { ctxBase_ = c; }
    // Binds a patch. Allocation-free.
    void bind(const Patch* patch) noexcept;
    // Modulation offsets in native units: [moduleIndex * fxParamSlots + slot].
    double* modulationOffsets() noexcept { return offsets_.data(); }
    void clearOffsets() noexcept;
    // Processes one rack (0 Main, 1 Bus 1, 2 Bus 2) in place.
    void processRack(int rack, double* l, double* r, int n) noexcept;
    bool hasModules(int rack) const noexcept { return rack >= 0 && rack < 3 && rackCount_[std::size_t(rack)] > 0; }
    int moduleCount() const noexcept { return patch_ ? int(std::min<std::size_t>(patch_->fx.size(), maxModules)) : 0; }
private:
    void processRange(int rack, int begin, int end, double* l, double* r, int n, int depth) noexcept;
    void processModule(int index, double* l, double* r, int n) noexcept;
    double sr_ = 44100.0;
    const Patch* patch_ = nullptr;
    std::uint64_t boundPreset_ = ~std::uint64_t(0);
    struct Pool { std::vector<std::unique_ptr<FxUnit>> units; };
    FxContext ctxBase_, ctx_;
    std::array<Pool, 19> pools_;
    struct Key { FxType type = FxType::unknown; int rack = -1, position = -1; };
    std::array<Key, maxModules> keys_ {};
    std::array<FxUnit*, maxModules> unitFor_ {};
    std::array<int, maxModules> splitIndex_ {};
    std::array<std::array<int, maxModules>, 3> rackModules_ {};
    std::array<int, 3> rackCount_ {};
    std::vector<double> offsets_;
    std::vector<double> splitBuf_;
    struct SplitState { Crossover x1[2], x2[2]; };
    std::array<SplitState, 32> splits_ {};
    std::array<bool, 32> splitFresh_ {};
};
}
