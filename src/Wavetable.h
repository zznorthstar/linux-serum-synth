#pragma once

#include "Patch.h"

namespace zyg {

// Builds octave-spaced, FFT harmonic-limited wavetable levels. Call only on
// a control/background thread after changing Oscillator::audio. Invalid or
// non-power-of-two frame layouts simply leave the derived cache empty.
void prepareWavetableMipmaps(Oscillator& oscillator);

}
