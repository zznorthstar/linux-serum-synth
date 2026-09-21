#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

ZygProcessor::ZygProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    auto init = std::make_unique<zyg::Patch>();
    init->name = "ZYG init";
    init->oscillators[0].enabled = true;
    init->oscillators[0].mode = zyg::OscMode::wavetable;
    init->oscillators[0].audio.resize(2048);
    for (int i = 0; i < 2048; ++i)
        init->oscillators[0].audio[std::size_t(i)] = std::sin(juce::MathConstants<double>::twoPi * i / 2048.0);
    init->routes[0].target = zyg::RouteTarget::main;
    slots[0] = std::move(init);
    engine.setPatch(slots[0].get());
    status = "ZYG init — select your Serum content directory, then load a .SerumPreset";
    // Developer-only host smoke hook. Ordinary users never need environment
    // variables; this lets a deterministic REAPER project import a private
    // fixture without embedding or redistributing that preset in Git.
    if (const char* fixture = std::getenv("ZYGZXG_HOST_SMOKE_PRESET")) {
        if (const char* root = std::getenv("ZYGZXG_HOST_SMOKE_ASSETS"))
            setAssetRoot(juce::File(root));
        loadPreset(juce::File(fixture));
    }
}

void ZygProcessor::prepareToPlay(double sampleRate, int) { engine.prepare(sampleRate); }
void ZygProcessor::releaseResources() { engine.allNotesOff(); }
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
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    int cursor = 0;
    for (const auto metadata : midi) {
        const int at = juce::jlimit(cursor, buffer.getNumSamples(), metadata.samplePosition);
        engine.render(left, right, cursor, at - cursor);
        cursor = at;
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) engine.noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff()) engine.noteOff(message.getChannel(), message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff()) engine.allNotesOff();
    }
    engine.render(left, right, cursor, buffer.getNumSamples() - cursor);
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
    {
        const juce::ScopedLock lock(controlLock);
        assetRoot = root.getFullPathName();
        status = "Content directory set. Load a .SerumPreset.";
    }
    return true;
}

void ZygProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::MemoryOutputStream stream(destination, false);
    const juce::ScopedLock lock(controlLock);
    stream.writeString("ZYGZXG-state-1");
    stream.writeString(assetRoot);
    stream.writeInt(static_cast<int>(savedPreset.getSize()));
    if (savedPreset.getSize()) stream.write(savedPreset.getData(), savedPreset.getSize());
}

void ZygProcessor::setStateInformation(const void* data, int size) {
    if (!data || size <= 0) return;
    juce::MemoryInputStream stream(data, static_cast<std::size_t>(size), false);
    if (stream.readString() != "ZYGZXG-state-1") { setStatus("Unknown plugin state format"); return; }
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
