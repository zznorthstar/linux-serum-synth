#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Wavetable.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace {
bool validAssetPath(const zyg::Patch& patch, const zyg::Oscillator& osc,
                    const std::filesystem::path& path) {
    if (patch.originalPreset.empty() || osc.userSelectedAsset) return true;
    if (path.is_absolute()) return false;
    for (const auto& part : path.lexically_normal()) if (part == "..") return false;
    return true;
}
void prepareAudioAssets(zyg::Patch& patch) {
    for (std::size_t oscIndex = 0; oscIndex < patch.oscillators.size(); ++oscIndex) {
        auto& osc = patch.oscillators[oscIndex];
        const auto oscPath = "Oscillator" + std::to_string(oscIndex);
        if (!osc.enabled) continue;
        if (osc.mode == zyg::OscMode::noise) {
            if (osc.asset.empty()) continue; // native white noise generator
            std::filesystem::path path(osc.asset);
            if (!validAssetPath(patch, osc, path) || (path.is_relative() && patch.assetRoot.empty())) {
                patch.diagnostics.push_back({"Oscillator3", "missing_asset", "unsafe or unset content path"});
                continue;
            }
            if (path.is_relative()) path = std::filesystem::path(patch.assetRoot) /
                "Samples/Factory Non-Tonal/Noises" / path;
            const juce::File file(path.string());
            if (!file.existsAsFile()) {
                patch.diagnostics.push_back({"Oscillator3", "missing_asset", path.string()});
                continue;
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            if (!reader || reader->lengthInSamples < 1 || reader->lengthInSamples > 8 * 1024 * 1024 ||
                reader->sampleRate < 8000 || reader->sampleRate > 192000) {
                patch.diagnostics.push_back({"Oscillator3", "unsupported_asset", path.string()});
                continue;
            }
            const int frames = int(reader->lengthInSamples);
            juce::AudioBuffer<float> buffer(2, frames);
            if (!reader->read(&buffer, 0, frames, 0, true, true)) {
                patch.diagnostics.push_back({"Oscillator3", "unsupported_asset", "decode failed: " + path.string()});
                continue;
            }
            osc.audio.resize(std::size_t(frames));
            const float* a = buffer.getReadPointer(0);
            const float* b = buffer.getReadPointer(1);
            for (int i = 0; i < frames; ++i)
                osc.audio[std::size_t(i)] = reader->numChannels > 1 ? 0.5f * (a[i] + b[i]) : a[i];
            osc.sampleRate = reader->sampleRate;
            continue;
        }
        if (osc.mode != zyg::OscMode::wavetable) continue;
        if (osc.asset.empty()) {
            if (!patch.originalPreset.empty()) {
                patch.diagnostics.push_back({oscPath, "missing_asset", "imported wavetable has no asset reference"});
                continue;
            }
            osc.frameSize = 2048;
            osc.audio.resize(2048);
            for (int i = 0; i < 2048; ++i)
                osc.audio[std::size_t(i)] = std::sin(juce::MathConstants<double>::twoPi * i / 2048.0);
            zyg::prepareWavetableMipmaps(osc);
            continue;
        }
        std::filesystem::path path(osc.asset);
        if (!validAssetPath(patch, osc, path) || (path.is_relative() && patch.assetRoot.empty())) {
            patch.diagnostics.push_back({oscPath, "missing_asset", "unsafe or unset content path"});
            continue;
        }
        if (path.is_relative()) path = std::filesystem::path(patch.assetRoot) / "Tables" / path;
        std::string error;
        if (!zyg::loadWavetableFromFile(osc, path, error))
            patch.diagnostics.push_back({oscPath, "missing_asset", error + ": " + path.string()});
    }
}
}

ZygProcessor::ZygProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    auto init = std::make_unique<zyg::Patch>();
    init->name = "ZYG init";
    init->oscillators[0].enabled = true;
    init->oscillators[0].mode = zyg::OscMode::wavetable;
    init->oscillators[0].audio.resize(2048);
    for (int i = 0; i < 2048; ++i)
        init->oscillators[0].audio[std::size_t(i)] = std::sin(juce::MathConstants<double>::twoPi * i / 2048.0);
    zyg::prepareWavetableMipmaps(init->oscillators[0]);
    init->routes[0].target = zyg::RouteTarget::main;
    slots[0] = std::move(init);
    engine.setPatch(slots[0].get());
    status = "INIT ready. Play MIDI or load a preset.";
    // Developer-only host smoke hook. Ordinary users never need environment
    // variables; this lets a deterministic REAPER project import a private
    // fixture without embedding or redistributing that preset in Git.
    if (const char* fixture = std::getenv("ZYGZXG_HOST_SMOKE_PRESET")) {
        if (const char* root = std::getenv("ZYGZXG_HOST_SMOKE_ASSETS"))
            setAssetRoot(juce::File(root));
        loadPreset(juce::File(fixture));
    }
}

void ZygProcessor::prepareToPlay(double sampleRate, int) { engine.prepare(sampleRate); auditionVoiceActive = false; }
void ZygProcessor::releaseResources() { engine.allNotesOff(); auditionVoiceActive = false; }
bool ZygProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ZygProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int requested = requestedIndex.load(std::memory_order_acquire);
    if (requested != activeIndex.load(std::memory_order_relaxed)) {
        engine.setPatch(slots[requested].get());
        activeIndex.store(requested, std::memory_order_release);
    }
    const bool audition = auditionHeld.load(std::memory_order_relaxed);
    if (audition && !auditionVoiceActive) { engine.noteOn(16, 60, 0.8f); auditionVoiceActive = true; }
    else if (!audition && auditionVoiceActive) { engine.noteOff(16, 60); auditionVoiceActive = false; }
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    int cursor = 0;
    for (const auto metadata : midi) {
        const int at = juce::jlimit(cursor, buffer.getNumSamples(), metadata.samplePosition);
        engine.render(left, right, cursor, at - cursor);
        cursor = at;
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) {
            midiNotes.fetch_add(1, std::memory_order_relaxed);
            engine.noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff()) engine.noteOff(message.getChannel(), message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff()) engine.allNotesOff();
    }
    engine.render(left, right, cursor, buffer.getNumSamples() - cursor);
    activeVoices.store(engine.activeVoiceCount(), std::memory_order_relaxed);
    float peak = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    outputPeak.store(peak, std::memory_order_relaxed);
}

bool ZygProcessor::publish(zyg::Patch&& patch) {
    auto owned = std::make_unique<zyg::Patch>(std::move(patch));
    const juce::ScopedLock lock(controlLock);
    const int current = activeIndex.load(std::memory_order_acquire);
    const int requested = requestedIndex.load(std::memory_order_acquire);
    int freeSlot = -1;
    for (int i = 0; i < 3; ++i) if (i != current && i != requested) { freeSlot = i; break; }
    if (freeSlot < 0) return false;
    slots[freeSlot] = std::move(owned);
    requestedIndex.store(freeSlot, std::memory_order_release);
    return true;
}

void ZygProcessor::setStatus(juce::String text) {
    const juce::ScopedLock lock(controlLock);
    status = std::move(text);
}
juce::String ZygProcessor::getStatus() const {
    const juce::ScopedLock lock(controlLock);
    return status;
}
juce::String ZygProcessor::getAssetRoot() const {
    const juce::ScopedLock lock(controlLock);
    return assetRoot;
}
juce::String ZygProcessor::getDiagnosticsReport() const {
    const juce::ScopedLock lock(controlLock);
    const auto* patch = slots[requestedIndex.load(std::memory_order_acquire)].get();
    if (!patch) return "No patch loaded";
    juce::String report = juce::String(zyg::statusSummary(*patch)) + "\n";
    for (const auto& d : patch->diagnostics)
        report += juce::String(d.status) + " | " + juce::String(d.path) + " | " + juce::String(d.detail) + "\n";
    return report;
}

bool ZygProcessor::loadPreset(const juce::File& file) {
    try {
        const auto root = getAssetRoot().toStdString();
        auto patch = zyg::loadSerumFile(file.getFullPathName().toStdString(), root);
        prepareAudioAssets(patch);
        const auto summary = juce::String(zyg::statusSummary(patch));
        juce::MemoryBlock bytes(patch.originalPreset.data(), patch.originalPreset.size());
        if (!publish(std::move(patch))) { setStatus("Could not hand off patch to audio thread"); return false; }
        {
            const juce::ScopedLock lock(controlLock);
            savedPreset = std::move(bytes);
            status = summary;
        }
        return true;
    } catch (const std::exception& error) {
        setStatus("Load failed: " + juce::String(error.what()));
        return false;
    }
}

bool ZygProcessor::setAssetRoot(const juce::File& root) {
    if (!root.isDirectory()) { setStatus("Content directory does not exist"); return false; }
    auto patch = currentPatchCopy();
    patch.assetRoot = root.getFullPathName().toStdString();
    std::erase_if(patch.diagnostics, [](const zyg::Diagnostic& d) {
        return d.status == "missing_asset" &&
            (d.path == "Oscillator0" || d.path == "Oscillator1" || d.path == "Oscillator2" || d.path == "Oscillator3");
    });
    prepareAudioAssets(patch);
    if (!publish(std::move(patch))) { setStatus("Could not hand off content root to audio thread"); return false; }
    {
        const juce::ScopedLock lock(controlLock);
        assetRoot = root.getFullPathName();
        status = "Content directory set. Load a .SerumPreset.";
    }
    return true;
}

zyg::Patch ZygProcessor::currentPatchCopy() const {
    const juce::ScopedLock lock(controlLock);
    const auto* current = slots[std::size_t(requestedIndex.load(std::memory_order_acquire))].get();
    return current ? *current : zyg::Patch{};
}
zyg::Patch ZygProcessor::getPatchSnapshot() const { return currentPatchCopy(); }
bool ZygProcessor::editPatch(const std::function<void(zyg::Patch&)>& edit) {
    auto patch = currentPatchCopy();
    edit(patch);
    return publish(std::move(patch));
}

juce::String ZygProcessor::getPresetName() const { return juce::String(currentPatchCopy().name); }

void ZygProcessor::newBlankPatch() {
    zyg::Patch patch;
    patch.name = "New patch";
    patch.oscillators[0].enabled = true;
    patch.oscillators[0].mode = zyg::OscMode::wavetable;
    patch.oscillators[0].audio.resize(2048);
    for (int i = 0; i < 2048; ++i)
        patch.oscillators[0].audio[std::size_t(i)] = std::sin(juce::MathConstants<double>::twoPi * i / 2048.0);
    zyg::prepareWavetableMipmaps(patch.oscillators[0]);
    patch.routes[0].target = zyg::RouteTarget::main;
    if (publish(std::move(patch))) {
        const juce::ScopedLock lock(controlLock);
        savedPreset = {};
        status = "New blank patch";
    }
}

bool ZygProcessor::saveNativePreset(const juce::File& file) {
    try {
        const auto json = zyg::patchToJson(currentPatchCopy());
        if (!file.replaceWithText(juce::String(json.dump(2)))) {
            setStatus("Could not write " + file.getFullPathName()); return false;
        }
        setStatus("Saved " + file.getFileName());
        return true;
    } catch (const std::exception& error) {
        setStatus("Save failed: " + juce::String(error.what())); return false;
    }
}

bool ZygProcessor::loadNativePreset(const juce::File& file) {
    try {
        const auto text = file.loadFileAsString().toStdString();
        auto patch = zyg::patchFromJson(zyg::Json::parse(text));
        zyg::mapLegacySerumModulationRoutes(patch);
        prepareAudioAssets(patch);
        const auto summary = juce::String(zyg::statusSummary(patch));
        const auto loadedRoot = juce::String(patch.assetRoot);
        if (!publish(std::move(patch))) { setStatus("Could not hand off patch to audio thread"); return false; }
        {
            const juce::ScopedLock lock(controlLock);
            savedPreset = {};
            assetRoot = loadedRoot;
            status = "Loaded " + file.getFileName() + " | " + summary;
        }
        return true;
    } catch (const std::exception& error) {
        setStatus("Load failed: " + juce::String(error.what())); return false;
    }
}

ZygProcessor::OscAInfo ZygProcessor::getOscAInfo() const {
    const auto patch = currentPatchCopy();
    const auto& osc = patch.oscillators[0];
    return {osc.enabled, juce::String(zyg::oscModeToString(osc.mode)), juce::String(osc.asset), osc.tablePosition};
}

void ZygProcessor::setOscATablePosition(double position) {
    auto patch = currentPatchCopy();
    patch.oscillators[0].tablePosition = juce::jlimit(0.0, 256.0, position);
    publish(std::move(patch));
}

bool ZygProcessor::setOscAWavetableFile(const juce::File& file) {
    return setOscWavetableFile(0, file);
}

bool ZygProcessor::setOscWavetableFile(int index, const juce::File& file) {
    if (index < 0 || index > 2) return false;
    auto patch = currentPatchCopy();
    auto& osc = patch.oscillators[std::size_t(index)];
    std::string error;
    const auto path = file.getFullPathName().toStdString();
    if (!zyg::loadWavetableFromFile(osc, path, error)) { setStatus("Wavetable load failed: " + juce::String(error)); return false; }
    osc.enabled = true;
    osc.mode = zyg::OscMode::wavetable;
    osc.asset = path;
    osc.userSelectedAsset = true;
    if (!publish(std::move(patch))) { setStatus("Could not hand off patch to audio thread"); return false; }
    setStatus("OSC " + juce::String::charToString(juce::juce_wchar('A' + index)) + " wavetable: " + file.getFileName());
    return true;
}

bool ZygProcessor::setNoiseSampleFile(const juce::File& file) {
    auto patch = currentPatchCopy();
    auto& noise = patch.oscillators[3];
    noise.mode = zyg::OscMode::noise;
    noise.asset = file.getFullPathName().toStdString();
    noise.userSelectedAsset = true;
    noise.enabled = true;
    noise.audio.clear();
    prepareAudioAssets(patch);
    if (noise.audio.empty()) { setStatus("Noise sample could not be decoded"); return false; }
    if (!publish(std::move(patch))) { setStatus("Could not hand off noise sample"); return false; }
    setStatus("Noise sample: " + file.getFileName());
    return true;
}

void ZygProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::MemoryOutputStream stream(destination, false);
    stream.writeString("ZYGZXG-state-2");
    const auto state = zyg::patchToJson(currentPatchCopy()).dump();
    stream.writeString(juce::String::fromUTF8(state.data(), int(state.size())));
}

void ZygProcessor::setStateInformation(const void* data, int size) {
    if (!data || size <= 0) return;
    juce::MemoryInputStream stream(data, static_cast<std::size_t>(size), false);
    const auto version = stream.readString();
    if (version == "ZYGZXG-state-2") {
        try {
            auto patch = zyg::patchFromJson(zyg::Json::parse(stream.readString().toStdString()));
            zyg::mapLegacySerumModulationRoutes(patch);
            prepareAudioAssets(patch);
            const auto summary = juce::String(zyg::statusSummary(patch));
            const auto restoredRoot = juce::String(patch.assetRoot);
            if (publish(std::move(patch))) {
                const juce::ScopedLock lock(controlLock);
                assetRoot = restoredRoot;
                status = "Restored: " + summary;
            }
            else setStatus("Restoration could not hand off patch");
        } catch (const std::exception& error) { setStatus("Restore failed: " + juce::String(error.what())); }
        return;
    }
    if (version != "ZYGZXG-state-1") { setStatus("Unknown plugin state format"); return; }
    const auto root = stream.readString();
    const int length = stream.readInt();
    if (length < 0 || length > 128 * 1024 * 1024 || stream.getNumBytesRemaining() < length) {
        setStatus("Invalid plugin state length"); return;
    }
    juce::MemoryBlock bytes;
    bytes.setSize(static_cast<std::size_t>(length));
    if (length) stream.read(bytes.getData(), length);
    {
        const juce::ScopedLock lock(controlLock);
        assetRoot = root;
        savedPreset = bytes;
    }
    if (!length) return;
    try {
        const auto* first = static_cast<const std::uint8_t*>(bytes.getData());
        std::vector<std::uint8_t> dataCopy(first, first + bytes.getSize());
        auto patch = zyg::importSerum(zyg::decodeSerum(dataCopy), root.toStdString());
        const auto summary = juce::String(zyg::statusSummary(patch));
        if (publish(std::move(patch))) setStatus("Restored: " + summary);
        else setStatus("Restoration could not hand off patch");
    } catch (const std::exception& error) { setStatus("Restore failed: " + juce::String(error.what())); }
}

juce::AudioProcessorEditor* ZygProcessor::createEditor() { return new ZygEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ZygProcessor(); }
