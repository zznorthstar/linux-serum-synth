#pragma once
// Bounded control-thread conversion of inline audio to immutable native samples.
// These functions allocate and must never run in an audio callback.
#include "Patch.h"

namespace zyg {
inline constexpr std::size_t maxEmbeddedSampleFrames = 4u * 1024u * 1024u;
SamplePtr decodePlanarSample(const Json& channels, double sampleRate, int rootNote, std::string& error);
Json embeddedSampleToJson(const SampleData& sample);
SamplePtr embeddedSampleFromJson(const Json& json); // throws on invalid native state
}
