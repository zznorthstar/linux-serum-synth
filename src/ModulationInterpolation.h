#pragma once
#include "ModulationFir.h"
#include <array>
#include <cmath>

namespace zyg {
// Fixed storage, no setup or dynamic initialization on the processing thread.
// Output phase p represents input time n + p/4 - 24. Both quality modes
// share this delay. The 2x mode computes phases 0/2; 4x computes all four.
struct ModulationInterpolator {
    std::array<double, 128> history {};
    unsigned write = 0;
    void reset() noexcept { history.fill(0); write=0; }
    std::array<double,4> process(double input, bool four) noexcept {
        if(!std::isfinite(input) || std::abs(input)<1e-30) input=0;
        history[write]=history[write+64]=input; write=(write+1)&63u;
        const double* samples=history.data()+write+(64-modulationFirTaps);
        std::array<double,4> result{};
        // The integer phase of a cardinal sinc is an exact delayed sample.
        result[0]=samples[modulationFirTaps-1-modulationDelaySamples];
        if(four) {
            for(unsigned i=0;i<modulationFirTaps;++i) {
                const double x=samples[i];
                for(unsigned phase=1;phase<4;++phase) result[phase]+=modulationFir[phase][i]*x;
            }
        } else {
            for(unsigned i=0;i<modulationFirTaps;++i) {
                result[2]+=modulationFir[2][i]*samples[i];
            }
        }
        return result;
    }
};
}
