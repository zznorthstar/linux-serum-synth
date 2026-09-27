#include "ModCatalog.h"
#include "FxTree.h"

namespace zyg::ui {

const std::vector<SourceEntry>& sourceCatalog() {
    static const std::vector<SourceEntry> list = [] {
        std::vector<SourceEntry> v;
        for (int i = 0; i < 4; ++i) v.push_back({"ENVELOPES", "ENV " + juce::String(i + 1), ModSource::envelope, i});
        for (int i = 0; i < 10; ++i) v.push_back({"LFOS", "LFO " + juce::String(i + 1), ModSource::lfo, i});
        for (int i = 0; i < 8; ++i) v.push_back({"MACROS", "MACRO " + juce::String(i + 1), ModSource::macro, i});
        v.push_back({"KEYBOARD", "VELOCITY", ModSource::velocity, 0}); v.push_back({"KEYBOARD", "NOTE", ModSource::note, 0});
        v.push_back({"KEYBOARD", "MOD WHEEL", ModSource::modWheel, 0}); v.push_back({"KEYBOARD", "PITCH BEND", ModSource::pitchBend, 0});
        v.push_back({"KEYBOARD", "CHANNEL PRESSURE", ModSource::channelPressure, 0}); v.push_back({"KEYBOARD", "POLY PRESSURE", ModSource::polyPressure, 0});
        v.push_back({"KEYBOARD", "RELEASE VELOCITY", ModSource::releaseVelocity, 0});
        v.push_back({"MPE", "MPE X", ModSource::mpeX, 0}); v.push_back({"MPE", "MPE Y", ModSource::mpeY, 0}); v.push_back({"MPE", "MPE Z", ModSource::mpeZ, 0});
        v.push_back({"RANDOM", "RANDOM 1", ModSource::random, 0}); v.push_back({"RANDOM", "RANDOM 2", ModSource::random, 1});
        v.push_back({"RANDOM", "ALTERNATE 1", ModSource::alternate, 0}); v.push_back({"RANDOM", "ALTERNATE 2", ModSource::alternate, 1});
        v.push_back({"RANDOM", "DISCRETE RANDOM", ModSource::discreteRandom, 0});
        v.push_back({"VOICE", "VOICE MOD 1", ModSource::voiceMod, 0}); v.push_back({"VOICE", "VOICE MOD 2", ModSource::voiceMod, 1});
        v.push_back({"VOICE", "VOICE INDEX", ModSource::voiceIndex, 0}); v.push_back({"VOICE", "ACTIVE VOICES", ModSource::activeVoices, 0});
        v.push_back({"VOICE", "FIXED", ModSource::fixed, 0});
        v.push_back({"AUDIO", "OSC A AUDIO", ModSource::oscAudio, 0}); v.push_back({"AUDIO", "OSC B AUDIO", ModSource::oscAudio, 1});
        v.push_back({"AUDIO", "OSC C AUDIO", ModSource::oscAudio, 2}); v.push_back({"AUDIO", "SUB AUDIO", ModSource::oscAudio, 3});
        v.push_back({"AUDIO", "NOISE AUDIO", ModSource::noiseAudio, 0});
        v.push_back({"AUDIO", "SIDECHAIN", ModSource::sidechain, 0});
        v.push_back({"AUDIO", "FILTER 1 AUDIO", ModSource::filterAudio, 0}); v.push_back({"AUDIO", "FILTER 2 AUDIO", ModSource::filterAudio, 1});
        static const char* ccGroups[4] = {"MIDI CC 0-31", "MIDI CC 32-63", "MIDI CC 64-95", "MIDI CC 96-119"};
        for (int cc = 0; cc < 120; ++cc) v.push_back({ccGroups[cc / 32], "CC " + juce::String(cc), ModSource::midiCC, cc});
        return v;
    }();
    return list;
}

juce::String sourceLabel(ModSource kind, int index) {
    for (const auto& e : sourceCatalog()) if (e.kind == kind && e.index == index) return e.label;
    return "--";
}

static int serumIdOf(ModSource kind, int index) {
    for (int id = 1; id <= 59; ++id) {
        ModSource k; int i;
        if (modSourceFromId(id, k, i) && k == kind && i == index) return id;
    }
    return 0;
}

void setRouteSource(ModulationRoute& r, ModSource kind, int index) {
    r.sourceKind = kind; r.sourceIndex = index; r.source = serumIdOf(kind, index); r.sourceName = sourceName(r.source);
    r.bipolar = modSourceIsBipolar(kind) ? true : r.bipolar;
}
void setRouteAux(ModulationRoute& r, ModSource kind, int index) {
    r.auxKind = kind; r.auxIndex = index; r.auxiliary = kind == ModSource::unknown ? 0 : serumIdOf(kind, index);
}

std::vector<DestEntry> destinationCatalog(const Patch& p) {
    std::vector<DestEntry> v;
    auto add = [&](const juce::String& g, const juce::String& l, ModTarget k, int inst, int prm) { v.push_back({g, l, k, inst, prm}); };
    const OscParam wt[] = {OscParam::tablePos, OscParam::warp1, OscParam::warp2, OscParam::volume, OscParam::pan, OscParam::octave, OscParam::coarse, OscParam::pitch,
        OscParam::fine, OscParam::detune, OscParam::blend, OscParam::unisonStereo, OscParam::unisonWTPos, OscParam::unisonWarp, OscParam::unisonWarp2,
        OscParam::initialPhase, OscParam::randomPhase, OscParam::start, OscParam::end, OscParam::position, OscParam::scanRate, OscParam::density,
        OscParam::grainLength, OscParam::randomOffset, OscParam::randomPitch, OscParam::timbreShift, OscParam::freqLo, OscParam::freqHi,
        OscParam::specShift, OscParam::specWet, OscParam::warpVar1, OscParam::warpVar2};
    const char* names[] = {"OSC A", "OSC B", "OSC C", "NOISE", "SUB"};
    for (int o = 0; o < 3; ++o) for (auto pr : wt) add(names[o], juce::String(oscParamName(pr)), ModTarget::oscParam, o, int(pr));
    for (auto pr : {OscParam::volume, OscParam::pan, OscParam::color}) add("NOISE", oscParamName(pr), ModTarget::oscParam, 3, int(pr));
    for (auto pr : {OscParam::volume, OscParam::pan, OscParam::octave, OscParam::coarse, OscParam::fine}) add("SUB", oscParamName(pr), ModTarget::oscParam, 4, int(pr));
    for (int f = 0; f < 2; ++f)
        for (auto pr : {FilterParam::freq, FilterParam::reso, FilterParam::drive, FilterParam::var, FilterParam::wet, FilterParam::level, FilterParam::stereo, FilterParam::x, FilterParam::y})
            add("FILTER " + juce::String(f + 1), filterParamName(pr), ModTarget::filterParam, f, int(pr));
    for (int e = 0; e < 4; ++e) {
        const char* n[] = {"ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
        for (int k = 0; k < 5; ++k) add("ENV " + juce::String(e + 1), n[k], ModTarget::envParam, e, k);
    }
    for (int l = 0; l < 10; ++l) { const char* n[] = {"RATE", "PHASE", "RISE", "DELAY", "SMOOTH"};
        for (int k = 0; k < 5; ++k) add("LFO " + juce::String(l + 1), n[k], ModTarget::lfoParam, l, k); }
    for (int m = 0; m < 8; ++m) add("MACROS", "MACRO " + juce::String(m + 1), ModTarget::macroValue, m, 0);
    const char* gn[] = {"MASTER TUNING", "PORTAMENTO TIME", "SWING", "TRANSPOSE", "VOICE AMP", "MASTER VOLUME", "DIRECT VOLUME", "BUS 1 VOLUME", "BUS 2 VOLUME", "ENV TIME SCALE", "LFO TIME SCALE"};
    for (int k = 0; k < int(GlobalParam::count); ++k) add("GLOBAL", gn[k], ModTarget::globalParam, 0, k);
    const char* src[] = {"OSC A", "OSC B", "OSC C", "NOISE", "SUB", "FILTER 1", "FILTER 2"};
    for (int s = 0; s < 7; ++s) {
        add("MIX", juce::String(src[s]) + " F1/F2 BALANCE", ModTarget::routingParam, s, int(RoutingParam::filterBalance));
        add("MIX", juce::String(src[s]) + " BUS 1 SEND", ModTarget::routingParam, s, int(RoutingParam::fxBus1Level));
        add("MIX", juce::String(src[s]) + " BUS 2 SEND", ModTarget::routingParam, s, int(RoutingParam::fxBus2Level));
    }
    for (std::size_t i = 0; i < p.fx.size(); ++i) {
        const auto& m = p.fx[i];
        if (isSplitter(m.fxType) && m.fxType != FxType::split && m.fxType != FxType::split3) continue;
        const juce::String group = juce::String(m.rack == 0 ? "FX MAIN " : m.rack == 1 ? "FX BUS 1 " : "FX BUS 2 ") + juce::String(int(i) + 1) + " " + fxDisplayName(m.fxType);
        const auto table = fxParamTable(m.fxType);
        for (std::size_t s = 0; s < table.size(); ++s) {
            const juce::String n = table[s].name;
            if (n.startsWith("count")) continue;
            add(group, n.toUpperCase(), ModTarget::fxParam, int(i), int(s));
        }
        if (!isSplitter(m.fxType)) add(group, "OUT LEVEL", ModTarget::fxParam, int(i), fxLevelSlot);
    }
    return v;
}

juce::String destinationLabel(const Patch& p, const ModulationRoute& r) {
    if (r.targetKind == ModTarget::unknown) return {};
    const TargetId t = normalizedTarget(r);
    static const char* osc[] = {"OSC A", "OSC B", "OSC C", "NOISE", "SUB"};
    static const char* src[] = {"OSC A", "OSC B", "OSC C", "NOISE", "SUB", "FILTER 1", "FILTER 2"};
    switch (t.kind) {
        case ModTarget::oscParam: return juce::String(osc[std::clamp(t.inst, 0, 4)]) + " " + oscParamName(OscParam(t.param));
        case ModTarget::filterParam: return "FILTER " + juce::String(t.inst + 1) + " " + filterParamName(FilterParam(t.param));
        case ModTarget::envParam: { static const char* n[] = {"ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"}; return "ENV " + juce::String(t.inst + 1) + " " + n[std::clamp(t.param, 0, 4)]; }
        case ModTarget::lfoParam: { static const char* n[] = {"RATE", "PHASE", "RISE", "DELAY", "SMOOTH"}; return "LFO " + juce::String(t.inst + 1) + " " + n[std::clamp(t.param, 0, 4)]; }
        case ModTarget::macroValue: return "MACRO " + juce::String(t.inst + 1);
        case ModTarget::globalParam: { static const char* n[] = {"MASTER TUNING", "PORTA TIME", "SWING", "TRANSPOSE", "VOICE AMP", "MASTER VOL", "DIRECT VOL", "BUS 1 VOL", "BUS 2 VOL", "ENV TIME", "LFO TIME"}; return juce::String("GLOBAL ") + n[std::clamp(t.param, 0, 10)]; }
        case ModTarget::routingParam: { static const char* n[] = {"F1/F2 BAL", "BUS 1", "BUS 2"}; return juce::String(src[std::clamp(t.inst, 0, 6)]) + " " + n[std::clamp(t.param, 0, 2)]; }
        case ModTarget::fxParam: {
            if (t.inst < 0 || t.inst >= int(p.fx.size())) return "FX (MISSING)";
            const auto& m = p.fx[std::size_t(t.inst)];
            juce::String pn = "OUT LEVEL";
            const auto table = fxParamTable(m.fxType);
            if (t.param != fxLevelSlot && std::size_t(t.param) < table.size()) pn = juce::String(table[std::size_t(t.param)].name).toUpperCase();
            return juce::String(fxDisplayName(m.fxType)) + " " + pn;
        }
        default: break;
    }
    if (!r.destinationModule.empty()) return juce::String(r.destinationModule) + " " + juce::String(r.destinationParameter);
    return "UNKNOWN";
}

void setRouteDestination(ModulationRoute& r, const DestEntry& d) {
    r.targetKind = d.kind; r.targetIndex = d.inst; r.targetParam = d.param;
    r.destinationModule = "ZYG"; r.destinationParameter = (d.group + " " + d.label).toStdString();
    r.destinationInstance = d.inst; r.destinationParameterId = -1;
}


namespace {
bool g_dragActive = false;
constexpr int kMaxRoutes = 64;
}
bool modDragActive() noexcept { return g_dragActive; }
void setModDragActive(bool b) noexcept { g_dragActive = b; }

juce::var makeDragPayload(ModSource kind, int index) {
    return "zygmod:" + juce::String(int(kind)) + ":" + juce::String(index);
}
bool parseDragPayload(const juce::var& v, ModSource& kind, int& index) {
    const juce::String s = v.toString();
    if (!s.startsWith("zygmod:")) return false;
    const auto parts = juce::StringArray::fromTokens(s, ":", "");
    if (parts.size() != 3) return false;
    kind = ModSource(parts[1].getIntValue()); index = parts[2].getIntValue();
    return kind != ModSource::unknown;
}

juce::String targetLabel(const Patch& p, const TargetId& t) {
    ModulationRoute r; r.targetKind = t.kind; r.targetIndex = t.inst; r.targetParam = t.param;
    return destinationLabel(p, r);
}

int addModulationRoute(Patch& p, ModSource kind, int index, const TargetId& t, bool& created, double amount) {
    created = false;
    for (std::size_t i = 0; i < p.modulation.size(); ++i) {
        const auto& r = p.modulation[i];
        if (r.sourceKind == kind && r.sourceIndex == index && r.targetKind != ModTarget::unknown && normalizedTarget(r) == t) {
            p.modulation[i].bypass = false;
            return int(i);
        }
    }
    int slot = -1;
    for (std::size_t i = 0; i < p.modulation.size(); ++i)
        if (p.modulation[i].sourceKind == ModSource::unknown && p.modulation[i].targetKind == ModTarget::unknown) { slot = int(i); break; }
    if (slot < 0) {
        if (int(p.modulation.size()) >= kMaxRoutes) return -1;
        ModulationRoute blank; blank.slot = int(p.modulation.size());
        p.modulation.push_back(std::move(blank)); slot = int(p.modulation.size()) - 1;
    }
    auto& r = p.modulation[std::size_t(slot)];
    r = ModulationRoute{}; r.slot = slot;
    setRouteSource(r, kind, index);
    DestEntry d {"", "", t.kind, t.inst, t.param};
    setRouteDestination(r, d);
    r.destinationParameter = targetLabel(p, t).toStdString();
    r.amount = amount; r.bypass = false;
    created = true;
    return slot;
}


void startModDrag(juce::Component& src, ModSource kind, int index, int uiScale) {
    auto* c = juce::DragAndDropContainer::findParentDragContainerFor(&src);
    if (!c || c->isDragAndDropActive()) return;
    const juce::String text = sourceLabel(kind, index);
    const int s = std::max(1, uiScale), w = textWidth(text) + 10, h = 11;
    juce::Image img(juce::Image::ARGB, w * s, h * s, true);
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale(float(s)));
        g.setColour(pal::violetShadow); g.fillRect(0, 0, w, h);
        frame(g, {0, 0, w, h}, pal::violetHot);
        drawTextIn(g, text, {0, 0, w, h}, pal::textHi, juce::Justification::centred);
    }
    c->startDragging(makeDragPayload(kind, index), &src, juce::ScaledImage(img, double(s)), false);
}

}
