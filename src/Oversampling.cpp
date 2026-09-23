#include "Oversampling.h"

namespace zyg {

double downsample2x(OversampleState& state, double first, double second) noexcept {
    auto push = [&](double value) noexcept {
        state.history[state.write] = value;
        state.write = (state.write + 1) % state.history.size();
    };
    push(first);
    push(second);
    double output = 0.0;
    std::size_t read = state.write;
    for (double coefficient : oversamplingFir) {
        read = read == 0 ? state.history.size() - 1 : read - 1;
        output += coefficient * state.history[read];
    }
    return output;
}

}
