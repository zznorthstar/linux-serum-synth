#pragma once
// The user's own library folder (default ~/Documents/ZYG-ZXG, like Serum's Documents folder):
// presets (Serum and ZYG alike), wavetables, noises, samples and impulses the user adds.
// The location is configurable and persisted in ~/.config/ZYG-ZXG/library.txt.
#include <JuceHeader.h>

namespace zyg::ui::library {

juce::File defaultUserRoot();
juce::File userRoot();                       // configured root, or the default
void setUserRoot(const juce::File& dir);     // persists; creates the folder layout
juce::File presetsDir();
juce::File wavetablesDir();
juce::File noisesDir();
juce::File samplesDir();
juce::File impulsesDir();
// Creates the folder layout and the "PUT YOUR ... HERE" readme files when missing.
// Never overwrites anything the user has placed there. Returns false if the root is not writable.
bool ensureUserFolders();

// Preset files the browser shows, in any library.
inline const char* presetWildcards() { return "*.SerumPreset;*.zygpreset"; }

// Sound category for browsing, derived from the folder (e.g. Factory/Bass), Serum's name
// prefixes ("BA - ", "LD - ") and filename keywords. Returns one of presetTypes() ("OTHER" last).
juce::String presetTypeOf(const juce::File& file, const juce::File& libraryRoot);
const juce::StringArray& presetTypes();

}
