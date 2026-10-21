#include "Oversampling.h"

namespace zyg {

double downsample2x(OversampleState& state, double first, double second, bool tight) noexcept {
    if (tight) {
        constexpr auto count = tightOversamplingFir.size();
        auto push = [&](double value) noexcept {
            state.history[state.write] = state.history[state.write + count] = value;
            if (++state.write == count) state.write = 0;
        };
        push(first); push(second);
        const auto newest = state.write + count - 1;
        // Linear-phase symmetry halves multiplies and avoids wrap branches
        // inside the dot product. No heap state or run-time coefficient setup.
        double output = 0.0;
        for (std::size_t i = 0; i < count / 2; ++i)
            output += tightOversamplingFir[i] * (state.history[newest-i] + state.history[newest-(count-1-i)]);
        return output + tightOversamplingFir[count/2] * state.history[newest-count/2];
    }
    auto push = [&](double value) noexcept {
        state.history[state.write] = value;
        state.write = (state.write + 1) % oversamplingFir.size();
    };
    push(first);
    push(second);
    double output = 0.0;
    std::size_t read = state.write;
    for (double coefficient : oversamplingFir) {
        read = read == 0 ? oversamplingFir.size() - 1 : read - 1;
        output += coefficient * state.history[read];
    }
    return output;
}

}
