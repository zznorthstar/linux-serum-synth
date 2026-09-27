#pragma once
// Helpers for finding content-library files (wavetables, samples, presets) next
// to the one currently in use.
#include <JuceHeader.h>
#include "../Patch.h"
#include <vector>

namespace zyg::ui {

juce::File contentRoot(const Patch& patch);
// Absolute file for an oscillator's asset reference, or an empty File.
juce::File oscAssetFile(const Patch& patch, int osc);
// Audio files (wav/flac/aif) in the same folder as `file`, sorted by name.
juce::Array<juce::File> siblingAudioFiles(const juce::File& file);
juce::File neighbour(const juce::File& file, int dir);
// Preset files (Serum / ZYG native) next to `file`, sorted by name.
juce::File presetNeighbour(const juce::File& file, int dir);
// Recursively lists files with any of the wildcards under `dir`, sorted.
juce::Array<juce::File> listFiles(const juce::File& dir, const juce::String& wildcards);

}
