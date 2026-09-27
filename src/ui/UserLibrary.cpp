#include "UserLibrary.h"

namespace zyg::ui::library {
namespace {
juce::File settingsFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ZYG-ZXG").getChildFile("library.txt");
}

void writeIfMissing(const juce::File& f, const juce::String& text) {
    if (!f.existsAsFile()) f.replaceWithText(text);
}

// Owning storage: an std::initializer_list member would dangle once the table's initialiser ends.
struct TypeRule { const char* type; std::vector<const char*> prefixes; std::vector<const char*> words; };
// Order matters: the first matching rule wins. Prefixes are Serum factory name codes ("BA - ...").
const std::vector<TypeRule>& rules() {
    static const std::vector<TypeRule> r = {
        {"BASS",   {"BA", "808", "SUB", "BS"},     {"bass", "808", "reese", "sub", "growl", "wob"}},
        {"LEAD",   {"LD", "LEAD"},                 {"lead"}},
        {"PAD",    {"PD", "PAD"},                  {"pad"}},
        {"PLUCK",  {"PL"},                         {"pluck"}},
        {"KEYS",   {"KY", "EP", "PN", "OR", "KEY"}, {"keyboard", "piano", "organ", "keys", "e piano", "rhodes"}},
        {"ARP/SEQ", {"ARP", "SEQ", "LOOP"},        {"arp", "seq", "loop", "sequence"}},
        {"CHORD",  {"CH", "CHORD"},                {"chord", "stab"}},
        {"BELL",   {"BL", "MAL"},                  {"bell", "mallet"}},
        {"ORCH",   {"STR", "BR", "ORCH", "WIND", "INST"}, {"string", "brass", "orchestral", "woodwind", "instrument"}},
        {"GUITAR", {"GTR"},                        {"guitar"}},
        {"VOX",    {"VOX", "VO"},                  {"vox", "vocal", "voice", "talkbox"}},
        {"DRUMS",  {"DR", "KIT", "HIT", "PERC"},   {"drum", "kick", "snare", "hat", "perc", "hit"}},
        {"FX",     {"FX", "SC", "SFX"},            {"sfx", "fx", "soundscape", "riser", "impact", "noise"}},
        {"SYNTH",  {"SY", "HV", "MDL", "SYN"},     {"synth", "hoover"}},
    };
    return r;
}
}

juce::File defaultUserRoot() {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ZYG-ZXG");
}

juce::File userRoot() {
    const auto f = settingsFile();
    if (f.existsAsFile()) {
        const juce::File dir(f.loadFileAsString().trim());
        if (dir.getFullPathName().isNotEmpty() && juce::File::isAbsolutePath(dir.getFullPathName())) return dir;
    }
    return defaultUserRoot();
}

void setUserRoot(const juce::File& dir) {
    const auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText(dir.getFullPathName());
    ensureUserFolders();
}

juce::File presetsDir()    { return userRoot().getChildFile("Presets"); }
juce::File wavetablesDir() { return userRoot().getChildFile("Wavetables"); }
juce::File noisesDir()     { return userRoot().getChildFile("Noises"); }
juce::File samplesDir()    { return userRoot().getChildFile("Samples"); }
juce::File impulsesDir()   { return userRoot().getChildFile("Impulses"); }

bool ensureUserFolders() {
    const auto root = userRoot();
    if (!root.createDirectory()) return false;
    struct D { juce::File dir; const char* readme; const char* body; };
    const D dirs[] = {
        {presetsDir(), "PUT YOUR PRESETS HERE.txt",
         "Put your presets here: Serum 2 (.SerumPreset) and ZYG-ZXG (.zygpreset) files are both listed as user presets.\n"
         "Sub-folders are shown as folders in the preset browser. Presets you save from ZYG-ZXG go here by default.\n"},
        {wavetablesDir(), "PUT YOUR WAVETABLES HERE.txt",
         "Put your wavetables here (.wav / .flac / .aif, single-cycle frames of 2048 samples or Serum-style tables).\n"
         "They appear under USER in the oscillator wavetable browser.\n"},
        {noisesDir(), "PUT YOUR NOISES HERE.txt", "Put your noise oscillator samples here (.wav / .flac / .aif).\n"},
        {samplesDir(), "PUT YOUR SAMPLES HERE.txt", "Put your samples for the sample / granular / spectral oscillator modes here.\n"},
        {impulsesDir(), "PUT YOUR IMPULSES HERE.txt", "Put impulse responses for the convolution effect here.\n"},
    };
    bool ok = true;
    for (const auto& d : dirs) {
        ok = d.dir.createDirectory() && ok;
        writeIfMissing(d.dir.getChildFile(d.readme), d.body);
    }
    writeIfMissing(root.getChildFile("README.txt"),
        "ZYG-ZXG user library\n"
        "====================\n\n"
        "Presets/     put your presets here (.SerumPreset and .zygpreset)\n"
        "Wavetables/  put your wavetables here\n"
        "Noises/      put your noise samples here\n"
        "Samples/     put your samples here\n"
        "Impulses/    put your convolution impulse responses here\n\n"
        "You can move this folder: MENU > USER LIBRARY FOLDER in the plug-in.\n"
        "ZYG-ZXG never modifies or deletes files you place here.\n");
    return ok;
}

const juce::StringArray& presetTypes() {
    static const juce::StringArray t = [] {
        juce::StringArray a;
        for (const auto& r : rules()) a.add(r.type);
        a.add("OTHER");
        return a;
    }();
    return t;
}

juce::String presetTypeOf(const juce::File& file, const juce::File& libraryRoot) {
    const auto name = file.getFileNameWithoutExtension();
    const auto prefix = name.upToFirstOccurrenceOf(" - ", false, false).trim().toUpperCase();
    const bool hasPrefix = name.contains(" - ") && prefix.length() <= 6;
    if (hasPrefix)
        for (const auto& r : rules())
            for (auto* p : r.prefixes) if (prefix == p) return r.type;
    // folder names (e.g. Factory/Bass/Reese), then words in the file name ("DS_AC2_bass_reese_trend")
    const auto rel = file.getParentDirectory().getRelativePathFrom(libraryRoot).toLowerCase().replaceCharacter('\\', '/');
    juce::StringArray folders; folders.addTokens(rel, "/", "");
    for (int i = folders.size(); --i >= 0;)
        for (const auto& r : rules())
            for (auto* w : r.words) if (folders[i] == w || folders[i] == juce::String(w) + "s") return r.type;
    const auto words = " " + name.toLowerCase().replaceCharacters("_-.()[]", "       ") + " ";
    for (const auto& r : rules())
        for (auto* w : r.words) if (words.contains(" " + juce::String(w))) return r.type;
    // short codes used as filename tokens by preset packs (e.g. "MO_HA_BS_Amped")
    juce::StringArray tokens; tokens.addTokens(name.toUpperCase(), "_- ", "");
    for (const auto& tok : tokens)
        for (const auto& r : rules())
            for (auto* p : r.prefixes) if (tok == p && juce::String(p).length() >= 2) return r.type;
    return "OTHER";
}

}
