#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Wavetable.h"
#include "Assets.h"
#include "dsp/FxEngine.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace {
// Control-thread asset preparation. Decoding uses JUCE (WAV, AIFF, FLAC, OGG ...); the DSP core only sees
// immutable SampleData. Nothing here runs on the audio thread.
void prepareAudioAssets(zyg::Patch& patch) {
    for (std::size_t oscIndex = 0; oscIndex < patch.oscillators.size(); ++oscIndex) {
        auto& osc = patch.oscillators[oscIndex];
        if (!osc.enabled || osc.mode != zyg::OscMode::wavetable || !osc.audio.empty()) continue;
        if (osc.asset.empty() && patch.originalPreset.empty()) {   // native patch without a table: a sine
            osc.frameSize = 2048;
            osc.audio.resize(2048);
            for (int i = 0; i < 2048; ++i) osc.audio[std::size_t(i)] = std::sin(juce::MathConstants<double>::twoPi * i / 2048.0);
            zyg::prepareWavetableMipmaps(osc);
        }
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const zyg::AudioDecoder decoder = [&formats](const std::filesystem::path& file, zyg::SampleData& out, std::string& error) {
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(file.string())));
        if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > 96 * 1024 * 1024 || reader->sampleRate < 8000 || reader->sampleRate > 384000) {
            error = "unreadable audio"; return false;
        }
        const int frames = int(reader->lengthInSamples);
        const int channels = int(std::min<unsigned>(reader->numChannels, 2u));
        juce::AudioBuffer<float> buffer(channels, frames);
        if (!reader->read(&buffer, 0, frames, 0, true, channels > 1)) { error = "decode failed"; return false; }
        out.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
        if (channels > 1) out.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames); else out.right.clear();
        out.sampleRate = reader->sampleRate;
        return true;
    };
    zyg::prepareAssets(patch, decoder);
}
}

ZygProcessor::ZygProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), false)       // unused main input (kept disabled) so the sidechain is an aux bus
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
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
    assetRoot = juce::String(zyg::defaultContentRoot().string());
    status = "INIT ready. Play MIDI or load a preset.";
    startTimerHz(10);
    // Developer-only host smoke hook. Ordinary users never need environment
    // variables; this lets a deterministic REAPER project import a private
    // fixture without embedding or redistributing that preset in Git.
    if (const char* fixture = std::getenv("ZYGZXG_HOST_SMOKE_PRESET")) {
        if (const char* root = std::getenv("ZYGZXG_HOST_SMOKE_ASSETS"))
            setAssetRoot(juce::File(root));
        loadPreset(juce::File(fixture));
    }
}

void ZygProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate); auditionVoiceActive = false;
    scBuf.setSize(2, std::max(samplesPerBlock, 8192), false, true, false);   // allocated here, never in processBlock
}
void ZygProcessor::releaseResources() { engine.allNotesOff(); auditionVoiceActive = false; }
bool ZygProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    if (layout.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 0; i < layout.inputBuses.size(); ++i) {
        const auto& set = layout.inputBuses.getReference(i);
        if (!set.isDisabled() && set != juce::AudioChannelSet::stereo() && set != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void ZygProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // Host sidechain (aux input bus 1): copy it out before the buffer is cleared for output.
    const int numSamples = buffer.getNumSamples();
    bool haveSidechain = false;
    if (getBusCount(true) > 1 && getBus(true, 1)->isEnabled() && numSamples <= scBuf.getNumSamples()) {
        auto sc = getBusBuffer(buffer, true, 1);
        const int chs = sc.getNumChannels();
        if (chs > 0) {
            scBuf.copyFrom(0, 0, sc, 0, 0, numSamples);
            scBuf.copyFrom(1, 0, sc, chs > 1 ? 1 : 0, 0, numSamples);
            haveSidechain = true;
        }
    }
    buffer.clear();
    if (haveSidechain) engine.setSidechain(scBuf.getReadPointer(0), scBuf.getReadPointer(1), numSamples);
    else engine.setSidechain(nullptr, nullptr, 0);
    const int requested = requestedIndex.load(std::memory_order_acquire);
    if (requested != activeIndex.load(std::memory_order_relaxed)) {
        engine.setPatch(slots[requested].get());
        activeIndex.store(requested, std::memory_order_release);
    }
    {   // notes from the on-screen keyboard
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        uiNoteFifo.prepareToRead(uiNoteFifo.getNumReady(), start1, size1, start2, size2);
        auto apply = [this](const UiNote& n) {
            if (n.kind == 1) { engine.allNotesOff(); heldLow.store(0); heldHigh.store(0); }
            else if (n.kind == 2) engine.setPitchBend(1, n.value);
            else if (n.kind == 3) engine.setControlChange(1, n.note, n.value);
            else if (n.on) { midiNotes.fetch_add(1, std::memory_order_relaxed); engine.noteOn(1, n.note, float(n.velocity) / 127.0f); markHeld(n.note, true); rsNoteOn(n.note, 0); }
            else { engine.noteOff(1, n.note); markHeld(n.note, false); }
        };
        for (int i = 0; i < size1; ++i) apply(uiNoteQueue[std::size_t(start1 + i)]);
        for (int i = 0; i < size2; ++i) apply(uiNoteQueue[std::size_t(start2 + i)]);
        uiNoteFifo.finishedRead(size1 + size2);
    }
    const bool audition = auditionHeld.load(std::memory_order_relaxed);
    if (audition && !auditionVoiceActive) { engine.noteOn(16, 60, 0.8f); auditionVoiceActive = true; }
    else if (!audition && auditionVoiceActive) { engine.noteOff(16, 60); auditionVoiceActive = false; }
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    if (auto* head = getPlayHead()) {
        if (const auto position = head->getPosition())
            { const double bpm = position->getBpm().orFallback(120.0); lastBpm.store(bpm, std::memory_order_relaxed);
              engine.setTransport(bpm, position->getPpqPosition().orFallback(0.0), position->getIsPlaying()); }
    }
    int cursor = 0;
    for (const auto metadata : midi) {
        const int at = juce::jlimit(cursor, buffer.getNumSamples(), metadata.samplePosition);
        engine.render(left, right, cursor, at - cursor);
        cursor = at;
        const auto message = metadata.getMessage();
        if (message.isNoteOn()) {
            midiNotes.fetch_add(1, std::memory_order_relaxed);
            engine.noteOn(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
            markHeld(message.getNoteNumber(), true);
            rsNoteOn(message.getNoteNumber(), at);
        }
        else if (message.isNoteOff()) { engine.noteOff(message.getChannel(), message.getNoteNumber(), message.getFloatVelocity()); markHeld(message.getNoteNumber(), false); }
        else if (message.isAllNotesOff() || message.isAllSoundOff()) { engine.allNotesOff(); heldLow.store(0); heldHigh.store(0); }
        else if (message.isPitchWheel()) engine.setPitchBend(message.getChannel(), float(message.getPitchWheelValue() - 8192) / 8192.0f);
        else if (message.isController()) engine.setControlChange(message.getChannel(), message.getControllerNumber(), float(message.getControllerValue()) / 127.0f);
        else if (message.isChannelPressure()) engine.setChannelPressure(message.getChannel(), float(message.getChannelPressureValue()) / 127.0f);
        else if (message.isAftertouch()) engine.setPolyPressure(message.getNoteNumber(), float(message.getAfterTouchValue()) / 127.0f);
    }
    engine.render(left, right, cursor, buffer.getNumSamples() - cursor);
    if (rsState.load(std::memory_order_acquire) == 2) captureResample(left, right, buffer.getNumSamples());
    activeVoices.store(engine.activeVoiceCount(), std::memory_order_relaxed);
    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        peakL = std::max(peakL, std::abs(left[i]));
        peakR = std::max(peakR, std::abs(right[i]));
    }
    outputPeak.store(std::max(peakL, peakR), std::memory_order_relaxed);
    peakLeft.store(peakL, std::memory_order_relaxed);
    peakRight.store(peakR, std::memory_order_relaxed);
    std::uint32_t w = scopeWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < buffer.getNumSamples(); ++i, ++w)
        scope_[w & (kScopeSize - 1)].store(0.5f * (left[i] + right[i]), std::memory_order_relaxed);
    scopeWrite_.store(w, std::memory_order_release);
}

void ZygProcessor::copyScope(float* dest, int n) const noexcept {
    n = std::clamp(n, 0, int(kScopeSize));
    const std::uint32_t w = scopeWrite_.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dest[i] = scope_[(w - std::uint32_t(n) + std::uint32_t(i)) & (kScopeSize - 1)].load(std::memory_order_relaxed);
}

bool ZygProcessor::publish(zyg::Patch&& patch) {
    auto owned = std::make_shared<zyg::Patch>(std::move(patch));
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
            (d.path.rfind("Oscillator", 0) == 0 || d.path.rfind("FX", 0) == 0);
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
std::shared_ptr<const zyg::Patch> ZygProcessor::getPatch() const {
    const juce::ScopedLock lock(controlLock);
    return slots[std::size_t(requestedIndex.load(std::memory_order_acquire))];
}
namespace {
template <class Fifo, class Queue, class Item>
void pushUi(Fifo& fifo, Queue& queue, const Item& item) {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1) return;
    queue[std::size_t(size1 > 0 ? start1 : start2)] = item;
    fifo.finishedWrite(1);
}
}
void ZygProcessor::uiNote(int note, bool on, float velocity) noexcept {
    UiNote n; n.note = std::uint8_t(juce::jlimit(0, 127, note)); n.on = on;
    n.velocity = std::uint8_t(juce::jlimit(1, 127, int(velocity * 127.0f)));
    pushUi(uiNoteFifo, uiNoteQueue, n);
}
void ZygProcessor::markHeld(int note, bool on) noexcept {
    if (note < 0 || note > 127) return;
    auto& word = note < 64 ? heldLow : heldHigh;
    const std::uint64_t bit = std::uint64_t(1) << (note & 63);
    if (on) word.fetch_or(bit, std::memory_order_relaxed); else word.fetch_and(~bit, std::memory_order_relaxed);
}
void ZygProcessor::uiAllNotesOff() noexcept { UiNote n; n.kind = 1; pushUi(uiNoteFifo, uiNoteQueue, n); }
void ZygProcessor::uiPitchBend(float v) noexcept { UiNote n; n.kind = 2; n.value = juce::jlimit(-1.0f, 1.0f, v); pushUi(uiNoteFifo, uiNoteQueue, n); }
void ZygProcessor::uiController(int cc, float v) noexcept { UiNote n; n.kind = 3; n.note = std::uint8_t(juce::jlimit(0, 127, cc)); n.value = juce::jlimit(0.0f, 1.0f, v); pushUi(uiNoteFifo, uiNoteQueue, n); }
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
        patch.sourcePath = file.getFullPathName().toStdString();   // browser / prev-next navigation key
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

bool ZygProcessor::setOscSampleFile(int index, const juce::File& file) {
    if (index < 0 || index > 2) return false;
    auto patch = currentPatchCopy();
    auto& osc = patch.oscillators[std::size_t(index)];
    if (osc.mode == zyg::OscMode::wavetable || osc.mode == zyg::OscMode::unknown) return setOscWavetableFile(index, file);
    osc.asset = file.getFullPathName().toStdString();
    osc.userSelectedAsset = true;
    osc.sample.reset(); osc.spectral.reset(); osc.regions.clear();
    osc.enabled = true;
    std::erase_if(patch.diagnostics, [index](const zyg::Diagnostic& d) { return d.status == "missing_asset" && d.path == "Oscillator" + std::to_string(index); });
    prepareAudioAssets(patch);
    if (!osc.sample) { setStatus("Sample could not be decoded: " + file.getFileName()); return false; }
    if (!publish(std::move(patch))) { setStatus("Could not hand off patch to audio thread"); return false; }
    setStatus("OSC " + juce::String::charToString(juce::juce_wchar('A' + index)) + " sample: " + file.getFileName());
    return true;
}

bool ZygProcessor::setFxImpulseFile(int fxIndex, const juce::File& file) {
    auto patch = currentPatchCopy();
    if (fxIndex < 0 || fxIndex >= int(patch.fx.size()) || patch.fx[std::size_t(fxIndex)].fxType != zyg::FxType::conv) return false;
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > 48 * 1024 * 1024) { setStatus("Impulse could not be decoded"); return false; }
    const int frames = int(reader->lengthInSamples), channels = int(std::min<unsigned>(reader->numChannels, 2u));
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader->read(&buffer, 0, frames, 0, true, channels > 1)) { setStatus("Impulse decode failed"); return false; }
    auto ir = std::make_shared<zyg::SampleData>();
    ir->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    if (channels > 1) ir->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    ir->sampleRate = reader->sampleRate;
    auto& m = patch.fx[std::size_t(fxIndex)];
    m.impulse = ir; m.convIr = zyg::buildConvIr(*ir); m.impulsePath = file.getFullPathName().toStdString();
    if (!publish(std::move(patch))) { setStatus("Could not hand off impulse"); return false; }
    setStatus("Impulse: " + file.getFileName());
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

// ------------------------------------------------------------------ resample
juce::File ZygProcessor::resampleFolder() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/share/ZYG-ZXG/Resampled");
}

bool ZygProcessor::armResample(int oscIndex, double beats) {
    if (oscIndex < 0 || oscIndex > 2 || rsState.load() != 0) return false;
    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const double seconds = juce::jlimit(0.05, 30.0, beats * 60.0 / std::max(20.0, lastBpm.load()));
    const int frames = int(seconds * sr);
    for (auto& b : rsBuf) b.assign(std::size_t(frames), 0.0f);      // allocated here, never on the audio thread
    rsTarget = oscIndex; rsFrames = frames; rsWritten.store(0); rsStartAt = 0;
    rsState.store(1, std::memory_order_release);
    return true;
}

void ZygProcessor::cancelResample() {
    if (rsState.load() == 3) return;                                  // already being saved
    rsState.store(0, std::memory_order_release);
    const juce::ScopedLock lock(controlLock);
    rsMessage = "RESAMPLE CANCELLED";
}

float ZygProcessor::getResampleProgress() const noexcept {
    return rsFrames > 0 ? float(rsWritten.load(std::memory_order_relaxed)) / float(rsFrames) : 0.0f;
}

juce::String ZygProcessor::takeResampleMessage() {
    const juce::ScopedLock lock(controlLock);
    auto m = std::move(rsMessage); rsMessage = {};
    return m;
}

void ZygProcessor::captureResample(const float* l, const float* r, int n) noexcept {
    const int from = std::min(n, rsStartAt);
    rsStartAt = 0;
    int written = rsWritten.load(std::memory_order_relaxed);
    const int take = std::min(n - from, rsFrames - written);
    for (int i = 0; i < take; ++i) { rsBuf[0][std::size_t(written + i)] = l[from + i]; rsBuf[1][std::size_t(written + i)] = r[from + i]; }
    written += take;
    rsWritten.store(written, std::memory_order_relaxed);
    if (written >= rsFrames) rsState.store(3, std::memory_order_release);
}

void ZygProcessor::timerCallback() {
    if (rsState.load(std::memory_order_acquire) == 3) finishResample();
}

void ZygProcessor::finishResample() {
    juce::String message;
    const auto folder = resampleFolder();
    folder.createDirectory();
    const auto file = folder.getNonexistentChildFile("ZYG Resample " + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S") + " " +
        juce::MidiMessage::getMidiNoteName(rsNote, true, true, 3), ".wav");
    bool ok = false;
    {
        // Trim inaudible leading silence but keep the timing of the first note.
        juce::AudioBuffer<float> out(2, rsFrames);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, 0, rsBuf[std::size_t(c)].data(), rsFrames);
        juce::WavAudioFormat wav;
        if (auto stream = file.createOutputStream()) {
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), getSampleRate(), 2, 24, {}, 0));
            if (writer) { stream.release(); ok = writer->writeFromAudioSampleBuffer(out, 0, rsFrames); }
        }
    }
    if (ok) {
        auto patch = currentPatchCopy();
        auto& osc = patch.oscillators[std::size_t(rsTarget)];
        osc.mode = zyg::OscMode::sample; osc.enabled = true; osc.pitchTrack = true;
        osc.baseNote = rsNote; osc.looping = false; osc.oneShot = false;
        osc.start = 0.0; osc.end = 100.0; osc.loopMode = zyg::LoopMode::forward;
        osc.asset = file.getFullPathName().toStdString(); osc.userSelectedAsset = true;
        osc.sample.reset(); osc.spectral.reset(); osc.regions.clear();
        prepareAudioAssets(patch);
        auto& reloaded = patch.oscillators[std::size_t(rsTarget)];
        if (reloaded.sample) { auto copy = std::make_shared<zyg::SampleData>(*reloaded.sample); copy->rootNote = rsNote; reloaded.sample = copy; }
        ok = reloaded.sample && publish(std::move(patch));
    }
    message = ok ? "RESAMPLED TO OSC " + juce::String::charToString(juce::juce_wchar('A' + rsTarget)) + ": " + file.getFileName()
                 : "RESAMPLE FAILED";
    {
        const juce::ScopedLock lock(controlLock);
        rsMessage = message;
    }
    for (auto& b : rsBuf) { b.clear(); b.shrink_to_fit(); }
    rsState.store(0, std::memory_order_release);
}
