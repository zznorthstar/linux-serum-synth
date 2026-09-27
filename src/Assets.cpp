#include "Assets.h"
#include <cstdlib>
#include "SerumImporter.h"
#include "Wavetable.h"
#include "dsp/FxEngine.h"
#include "dsp/Spectral.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace zyg {
namespace {
constexpr std::size_t maxAudioFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t maxDecodedFrames = 96u * 1024u * 1024u;
const char* const contentDirs[] = {"Presets", "Tables", "Samples", "Multisamples", "Impulses", "Clips", "Arp Patterns",
                                   "Arp Banks", "Clip Banks", "Effect Chains", "PZ Filter", "Styles", "Curves",
                                   "LFO Paths", "LFO Shapes", "Skins", "System"};

std::uint32_t rd32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) | (std::uint32_t(b[at + 1]) << 8) | (std::uint32_t(b[at + 2]) << 16) | (std::uint32_t(b[at + 3]) << 24);
}
std::string lowerCopy(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }

int noteNumber(const std::string& v) {
    if (v.empty()) return 0;
    if (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-') return std::atoi(v.c_str());
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    const char c = char(std::tolower(static_cast<unsigned char>(v[0])));
    if (c < 'a' || c > 'g') return 0;
    std::size_t i = 1; int semis = base[c - 'a'];
    if (i < v.size() && (v[i] == '#')) { ++semis; ++i; } else if (i < v.size() && v[i] == 'b') { --semis; ++i; }
    const int octave = i < v.size() ? std::atoi(v.c_str() + i) : 4;
    return (octave + 1) * 12 + semis;
}
}

bool decodeWavFile(const std::filesystem::path& file, SampleData& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) { error = "file not found"; return false; }
    if (std::filesystem::file_size(file, ec) > maxAudioFileBytes) { error = "file exceeds size limit"; return false; }
    std::ifstream in(file, std::ios::binary);
    std::vector<std::uint8_t> b(std::istreambuf_iterator<char>(in), {});
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) { error = "not RIFF WAVE"; return false; }
    std::uint16_t format = 0, channels = 0, bits = 0; std::uint32_t rate = 44100;
    std::size_t dataAt = 0, dataSize = 0;
    for (std::size_t at = 12; at + 8 <= b.size();) {
        const std::uint32_t size = rd32(b, at + 4);
        const std::size_t next = at + 8u + size;
        const std::string id(reinterpret_cast<const char*>(b.data() + at), 4);
        if (id == "fmt " && size >= 16 && at + 8 + 16 <= b.size()) {
            format = std::uint16_t(b[at + 8] | (b[at + 9] << 8)); channels = std::uint16_t(b[at + 10] | (b[at + 11] << 8));
            rate = rd32(b, at + 12); bits = std::uint16_t(b[at + 22] | (b[at + 23] << 8));
            if (format == 0xFFFE && size >= 26) format = std::uint16_t(b[at + 32] | (b[at + 33] << 8));
        } else if (id == "data") { dataAt = at + 8; dataSize = std::min<std::size_t>(size, b.size() - dataAt); break; }
        if (next > b.size()) break;
        at = next + (size & 1u);
    }
    const bool okFormat = (format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32);
    if (!dataAt || !channels || !okFormat) { error = "unsupported WAVE encoding"; return false; }
    const std::size_t stride = std::size_t(channels) * (bits / 8u), frames = dataSize / stride;
    if (frames < 2 || frames > maxDecodedFrames) { error = "unsupported WAVE length"; return false; }
    out.left.assign(frames, 0.0f); out.right.clear();
    const bool stereo = channels >= 2;
    if (stereo) out.right.assign(frames, 0.0f);
    out.sampleRate = std::max<double>(8000.0, rate);
    auto readAt = [&](std::size_t frame, std::size_t ch) -> float {
        const std::uint8_t* s = b.data() + dataAt + frame * stride + ch * (bits / 8u);
        if (format == 3) { std::uint32_t raw = std::uint32_t(s[0]) | (std::uint32_t(s[1]) << 8) | (std::uint32_t(s[2]) << 16) | (std::uint32_t(s[3]) << 24); float v; std::memcpy(&v, &raw, 4); return std::isfinite(v) ? v : 0.0f; }
        if (bits == 8) return (int(s[0]) - 128) / 128.0f;
        if (bits == 16) return std::int16_t(s[0] | (s[1] << 8)) / 32768.0f;
        if (bits == 24) { int raw = int(s[0]) | (int(s[1]) << 8) | (int(s[2]) << 16); if (raw & 0x800000) raw |= ~0xffffff; return raw / 8388608.0f; }
        const std::uint32_t raw = std::uint32_t(s[0]) | (std::uint32_t(s[1]) << 8) | (std::uint32_t(s[2]) << 16) | (std::uint32_t(s[3]) << 24);
        return std::int32_t(raw) / 2147483648.0f;
    };
    for (std::size_t i = 0; i < frames; ++i) {
        if (!stereo) out.left[i] = readAt(i, 0);
        else {
            out.left[i] = readAt(i, 0); out.right[i] = readAt(i, 1);
            if (channels > 2) { double extra = 0; for (std::size_t c = 2; c < channels; ++c) extra += readAt(i, c); out.left[i] += float(extra / (channels - 2) * 0.5); out.right[i] += float(extra / (channels - 2) * 0.5); }
        }
    }
    return true;
}

std::filesystem::path resolveSerumAsset(const std::filesystem::path& root, const std::string& category, const std::string& reference) {
    if (root.empty() || reference.empty()) return {};
    std::string ref = reference;
    std::replace(ref.begin(), ref.end(), '\\', '/');
    while (!ref.empty() && ref.front() == '/') ref.erase(ref.begin());
    if (ref.empty() || ref.find(':') != std::string::npos) return {};
    std::vector<std::string> dirs;
    if (category == "Samples/Factory Non-Tonal/Noises") { dirs = {"Samples/Factory Non-Tonal/Noises", "Samples"}; }
    else dirs = {category};
    for (const auto& d : dirs) {
        const auto candidate = (std::filesystem::path(d) / ref).lexically_normal();
        const auto first = *candidate.begin();
        if (first == ".." || first == ".") continue;
        bool known = false;
        for (const char* c : contentDirs) if (first == c) known = true;
        if (!known) continue;
        const auto full = root / candidate;
        std::error_code ec;
        if (std::filesystem::is_regular_file(full, ec)) return full;
        // Case-insensitive fallback (content authored on case-insensitive filesystems).
        auto cur = root;
        bool ok = true;
        for (const auto& part : candidate) {
            const auto exact = cur / part;
            if (std::filesystem::exists(exact, ec)) { cur = exact; continue; }
            bool found = false;
            for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
                if (lowerCopy(entry.path().filename().string()) == lowerCopy(part.string())) { cur = entry.path(); found = true; break; }
            }
            if (!found) { ok = false; break; }
        }
        if (ok && std::filesystem::is_regular_file(cur, ec)) return cur;
    }
    return {};
}

std::vector<SfzRegion> parseSfz(const std::string& text, SfzGroup* groupOut) {
    std::vector<SfzRegion> regions;
    std::map<std::string, std::string> group;
    std::map<std::string, std::string>* current = nullptr;
    std::map<std::string, std::string> region;
    bool inRegion = false;
    SfzGroup g;
    auto flush = [&]() {
        if (!inRegion) return;
        auto get = [&](const std::string& k, const std::string& def = {}) {
            auto it = region.find(k); if (it != region.end()) return it->second;
            it = group.find(k); return it != group.end() ? it->second : def;
        };
        SfzRegion r;
        r.sample = get("sample");
        if (!r.sample.empty()) {
            if (auto k = get("key"); !k.empty()) { r.loKey = r.hiKey = r.rootKey = noteNumber(k); }
            if (auto k = get("lokey"); !k.empty()) r.loKey = noteNumber(k);
            if (auto k = get("hikey"); !k.empty()) r.hiKey = noteNumber(k);
            if (auto k = get("pitch_keycenter"); !k.empty()) r.rootKey = noteNumber(k);
            else if (get("key").empty()) r.rootKey = std::clamp((r.loKey + r.hiKey) / 2, 0, 127);
            if (auto k = get("lovel"); !k.empty()) r.loVel = std::atoi(k.c_str());
            if (auto k = get("hivel"); !k.empty()) r.hiVel = std::atoi(k.c_str());
            r.tuneCents = std::atof(get("tune", "0").c_str()) + 100.0 * std::atof(get("transpose", "0").c_str());
            r.volumeDb = std::atof(get("volume", "0").c_str()); r.pan = std::atof(get("pan", "0").c_str());
            const auto lm = get("loop_mode");
            r.loop = lm == "loop_continuous" || lm == "loop_sustain";
            r.loopStart = std::atof(get("loop_start", "0").c_str()); r.loopEnd = std::atof(get("loop_end", "0").c_str());
            regions.push_back(std::move(r));
        }
        region.clear(); inRegion = false;
    };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("//", 0) == 0) continue;
        std::size_t pos = 0;
        while (pos < line.size()) {
            if (line[pos] == '<') {
                const auto end = line.find('>', pos);
                if (end == std::string::npos) break;
                const auto tag = line.substr(pos + 1, end - pos - 1);
                flush();
                if (tag == "group") { group.clear(); current = &group; inRegion = false; }
                else if (tag == "region") { current = &region; inRegion = true; }
                else current = nullptr;
                pos = end + 1; continue;
            }
            if (std::isspace(static_cast<unsigned char>(line[pos]))) { ++pos; continue; }
            const auto eq = line.find('=', pos);
            if (eq == std::string::npos) break;
            const std::string name = line.substr(pos, eq - pos);
            // the value runs to the next " name=" occurrence or the end of the line
            std::size_t vend = line.size();
            for (std::size_t k = eq + 1; k < line.size(); ++k) {
                if (line[k] == ' ' || line[k] == '\t') {
                    std::size_t j = k + 1; while (j < line.size() && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_')) ++j;
                    if (j < line.size() && line[j] == '=' && j > k + 1) { vend = k; break; }
                    if (j < line.size() && line[j] == '<') { vend = k; break; }
                }
            }
            std::string value = line.substr(eq + 1, vend - eq - 1);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
            if (current) (*current)[name] = value;
            pos = vend;
        }
    }
    flush();
    if (groupOut) {
        if (auto it = group.find("amp_veltrack"); it != group.end()) g.ampVelTrack = std::atof(it->second.c_str());
        if (auto it = group.find("ampeg_release"); it != group.end()) { g.ampegRelease = std::atof(it->second.c_str()); g.hasRelease = true; }
        *groupOut = g;
    }
    return regions;
}

namespace {
void diag(Patch& p, const std::string& path, const std::string& status, const std::string& detail) {
    p.diagnostics.push_back({path, status, detail});
}
// A user-picked native file keeps its absolute path; an imported reference is category-relative
// (a leading slash is Serum's virtual root, not a Linux root).
std::filesystem::path assetPath(const Oscillator& osc, const std::filesystem::path& root, const std::string& category) {
    if (osc.userSelectedAsset && std::filesystem::path(osc.asset).is_absolute()) return osc.asset;
    return resolveSerumAsset(root, category, osc.asset);
}
}

// Energy-onset detector for Auto slicing: markers where the short-term energy jumps by `threshold`-scaled amounts.
std::vector<double> detectSliceMarkers(const SampleData& sd, double threshold) {
    std::vector<double> markers {0.0};
    const std::size_t hop = std::max<std::size_t>(64, std::size_t(sd.sampleRate * 0.005));
    if (sd.frames() < hop * 8) return markers;
    std::vector<double> env;
    for (std::size_t i = 0; i + hop <= sd.frames(); i += hop) {
        double e = 0; for (std::size_t k = 0; k < hop; ++k) { const double v = sd.stereo() ? 0.5 * (sd.left[i + k] + sd.right[i + k]) : sd.left[i + k]; e += v * v; }
        env.push_back(std::sqrt(e / double(hop)));
    }
    double peak = 1.0e-9; for (double v : env) peak = std::max(peak, v);
    const double rise = std::max(0.05, threshold) * peak * 0.6;
    std::size_t last = 0;
    for (std::size_t i = 4; i < env.size(); ++i) {
        const double before = 0.25 * (env[i - 4] + env[i - 3] + env[i - 2] + env[i - 1]);
        if (env[i] - before > rise && env[i] > 0.08 * peak && i - last > 20) { markers.push_back(double(i * hop) / double(sd.frames())); last = i; }
    }
    return markers;
}

std::filesystem::path defaultContentRoot() {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto usable = [&](const fs::path& p) { return !p.empty() && fs::is_directory(p / "Tables", ec); };
    if (const char* e = std::getenv("ZYGZXG_CONTENT")) if (usable(e)) return e;
    fs::path base;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) base = x;
    else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".local" / "share";
    const fs::path p = base / "ZYG-ZXG" / "Content";
    return (!base.empty() && usable(p)) ? p : fs::path();
}

void prepareAssets(Patch& patch, const AudioDecoder& decoder) {
    if (patch.assetRoot.empty()) patch.assetRoot = defaultContentRoot().string();
    const std::filesystem::path root(patch.assetRoot);
    std::map<std::string, SamplePtr> cache;
    std::size_t totalFrames = 0;
    auto load = [&](const std::filesystem::path& path, std::string& error) -> SamplePtr {
        const auto key = path.string();
        if (auto it = cache.find(key); it != cache.end()) return it->second;
        auto data = std::make_shared<SampleData>();
        if (!decoder(path, *data, error) || data->frames() < 2) { if (error.empty()) error = "decode failed"; return nullptr; }
        totalFrames += data->frames();
        if (totalFrames > 4u * maxDecodedFrames) { error = "total decoded audio exceeds limit"; return nullptr; }
        SamplePtr p = data; cache[key] = p; return p;
    };
    for (std::size_t i = 0; i < patch.oscillators.size(); ++i) {
        auto& osc = patch.oscillators[i];
        if (!osc.enabled) continue;
        const std::string where = "Oscillator" + std::to_string(i);
        std::string error;
        switch (osc.mode) {
            case OscMode::wavetable:
                if (osc.audio.empty() && !osc.asset.empty()) {
                    const auto path = assetPath(osc, root, "Tables");
                    if (path.empty() || !loadWavetableFromFile(osc, path, error)) diag(patch, where, "missing_asset", error.empty() ? osc.asset : error + ": " + osc.asset);
                }
                break;
            case OscMode::noise:
                if (osc.audio.empty() && !osc.asset.empty()) {
                    const auto path = assetPath(osc, root, "Samples/Factory Non-Tonal/Noises");
                    if (path.empty()) { diag(patch, where, "missing_asset", osc.asset); break; }
                    if (auto s = load(path, error)) {
                        osc.audio.resize(s->frames());
                        for (std::size_t k = 0; k < s->frames(); ++k) osc.audio[k] = s->stereo() ? 0.5f * (s->left[k] + s->right[k]) : s->left[k];
                        osc.sampleRate = s->sampleRate;
                    } else diag(patch, where, "unsupported_asset", error + ": " + osc.asset);
                }
                break;
            case OscMode::sample: case OscMode::granular: case OscMode::spectral:
                if (!osc.sample && !osc.asset.empty()) {
                    const auto path = assetPath(osc, root, "Samples");
                    if (path.empty()) { diag(patch, where, "missing_asset", osc.asset); break; }
                    if (auto s = load(path, error)) {
                        auto copy = std::make_shared<SampleData>(*s); copy->rootNote = osc.baseNote;
                        osc.sample = copy;
                    } else diag(patch, where, "unsupported_asset", error + ": " + osc.asset);
                }
                if (osc.mode == OscMode::spectral && osc.sample && !osc.spectral) osc.spectral = buildSpectralAnalysis(*osc.sample);
                if (osc.mode == OscMode::sample && osc.sample && osc.slicingMode == 1 && osc.sliceMarkers.empty()) osc.sliceMarkers = detectSliceMarkers(*osc.sample, 0.25);
                if (!osc.sample && osc.asset.empty()) diag(patch, where, "missing_asset", "no audio asset reference");
                break;
            case OscMode::multisample:
                if (osc.regions.empty()) {
                    std::string sfzText = osc.embeddedSfz;
                    std::filesystem::path sfzDir;
                    if (!osc.asset.empty()) sfzDir = std::filesystem::path(osc.asset).parent_path();
                    if (sfzText.empty() && !osc.asset.empty()) {
                        const auto path = resolveSerumAsset(root, "Multisamples", osc.asset);
                        if (!path.empty()) { std::ifstream in(path); sfzText.assign(std::istreambuf_iterator<char>(in), {}); }
                    }
                    if (sfzText.empty()) { diag(patch, where, "missing_asset", osc.asset.empty() ? "no SFZ mapping" : osc.asset); break; }
                    SfzGroup group;
                    const auto sfz = parseSfz(sfzText, &group);
                    unsigned missing = 0;
                    for (const auto& r : sfz) {
                        std::string ref = r.sample; std::replace(ref.begin(), ref.end(), '\\', '/');
                        std::filesystem::path path;
                        if (const auto pos = ref.find("/Samples/"); pos != std::string::npos) path = resolveSerumAsset(root, "Samples", ref.substr(pos + 9));
                        else if (const auto p2 = ref.find("/Multisamples/"); p2 != std::string::npos) path = resolveSerumAsset(root, "Multisamples", ref.substr(p2 + 14));
                        else path = resolveSerumAsset(root, "Multisamples", (sfzDir / ref).generic_string());
                        SamplePtr s = path.empty() ? nullptr : load(path, error);
                        if (!s) { ++missing; continue; }
                        SampleRegion sr;
                        sr.sample = s; sr.loKey = r.loKey; sr.hiKey = r.hiKey; sr.loVel = r.loVel; sr.hiVel = r.hiVel; sr.rootKey = r.rootKey;
                        sr.tuneCents = r.tuneCents; sr.volumeDb = r.volumeDb; sr.pan = r.pan; sr.loop = r.loop;
                        sr.loopStart = r.loopStart; sr.loopEnd = r.loopEnd; sr.path = r.sample;
                        osc.regions.push_back(std::move(sr));
                    }
                    if (!osc.velTrackOverride) osc.velTrack = group.ampVelTrack;
                    if (!osc.sampleEnv.override_ && group.hasRelease) { osc.sampleEnv.release = group.ampegRelease; osc.sampleEnv.useSfzRelease = true; }
                    if (missing) diag(patch, where, "missing_asset", std::to_string(missing) + " of " + std::to_string(sfz.size()) + " multisample regions unresolved");
                    if (osc.regions.empty()) break;
                }
                break;
            default: break;
        }
    }
    for (std::size_t i = 0; i < patch.fx.size(); ++i) {
        auto& m = patch.fx[i];
        if (m.fxType == FxType::conv && !m.convIr && m.impulsePath.empty()) {
            // Serum's convolver starts on a built-in impulse response; ZYG synthesises a comparable diffuse room.
            auto ir = std::make_shared<SampleData>(); ir->sampleRate = 48000.0;
            const std::size_t n = 96000; ir->left.resize(n); ir->right.resize(n);
            std::uint32_t seed = 0x1234567u;
            auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return double(seed) / 2147483648.0 - 1.0; };
            double lpl = 0.0, lpr = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                const double t = double(k) / 48000.0, env = std::exp(-6.9 * t / 1.6) * std::min(1.0, t / 0.004);
                const double damp = 0.15 + 0.8 * std::exp(-t * 1.5);          // highs decay faster than lows
                lpl += damp * (rnd() - lpl); lpr += damp * (rnd() - lpr);
                ir->left[k] = float(lpl * env); ir->right[k] = float(lpr * env);
            }
            m.impulse = ir; m.convIr = buildConvIr(*ir);
            diag(patch, "FX" + std::to_string(i), "dsp_active", "built-in ZYG impulse response (no impulse selected)");
            continue;
        }
        if (m.fxType != FxType::conv || m.convIr || m.impulsePath.empty()) continue;
        std::string error;
        const auto path = resolveSerumAsset(root, "Impulses", m.impulsePath);
        if (path.empty()) { diag(patch, "FX" + std::to_string(i), "missing_asset", m.impulsePath); continue; }
        if (auto s = load(path, error)) { m.impulse = s; m.convIr = buildConvIr(*s); }
        else diag(patch, "FX" + std::to_string(i), "unsupported_asset", error + ": " + m.impulsePath);
    }
}
}
