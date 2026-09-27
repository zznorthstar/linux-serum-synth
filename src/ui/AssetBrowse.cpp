#include "AssetBrowse.h"
#include "../Assets.h"

namespace zyg::ui {

juce::File contentRoot(const Patch& patch) {
    if (!patch.assetRoot.empty()) {
        const juce::File f(juce::String(patch.assetRoot));
        if (f.isDirectory()) return f;
    }
    return juce::File(juce::String(defaultContentRoot().string()));
}

juce::File oscAssetFile(const Patch& patch, int osc) {
    const auto& o = patch.oscillators[std::size_t(osc)];
    if (o.asset.empty()) return {};
    const juce::File direct(juce::String(o.asset));
    if (juce::File::isAbsolutePath(juce::String(o.asset)) && direct.existsAsFile()) return direct;
    const auto root = contentRoot(patch);
    if (!root.isDirectory()) return {};
    const char* category = o.mode == OscMode::wavetable ? "Tables" : o.mode == OscMode::noise ? "Samples/Factory Non-Tonal/Noises" :
                           o.mode == OscMode::multisample ? "Multisamples" : "Samples";
    const auto p = resolveSerumAsset(std::filesystem::path(root.getFullPathName().toStdString()), category, o.asset);
    return p.empty() ? juce::File() : juce::File(juce::String(p.string()));
}

juce::Array<juce::File> siblingAudioFiles(const juce::File& file) {
    juce::Array<juce::File> out;
    if (!file.getParentDirectory().isDirectory()) return out;
    out = file.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.wav;*.flac;*.aif;*.aiff;*.ogg");
    struct ByName { static int compareElements(const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()); } } cmp;
    out.sort(cmp);
    return out;
}

juce::File neighbour(const juce::File& file, int dir) {
    const auto list = siblingAudioFiles(file);
    if (list.isEmpty()) return {};
    int i = list.indexOf(file);
    if (i < 0) i = dir > 0 ? -1 : 0;
    return list[(i + dir + list.size()) % list.size()];
}

juce::File presetNeighbour(const juce::File& file, int dir) {
    if (!file.getParentDirectory().isDirectory()) return {};
    auto list = file.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*.SerumPreset;*.zygpreset");
    struct ByName { static int compareElements(const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()); } } cmp;
    list.sort(cmp);
    if (list.isEmpty()) return {};
    int i = list.indexOf(file);
    if (i < 0) i = dir > 0 ? -1 : 0;
    return list[(i + dir + list.size()) % list.size()];
}

juce::Array<juce::File> listFiles(const juce::File& dir, const juce::String& wildcards) {
    juce::Array<juce::File> out;
    if (!dir.isDirectory()) return out;
    out = dir.findChildFiles(juce::File::findFiles, true, wildcards);
    struct ByPath { static int compareElements(const juce::File& a, const juce::File& b) {
        return a.getFullPathName().compareNatural(b.getFullPathName()); } } cmp;
    out.sort(cmp);
    return out;
}

}
