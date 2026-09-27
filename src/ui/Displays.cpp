#include "Displays.h"
#include "../dsp/Warp.h"
#include "../PluginProcessor.h"
#include <cmath>

namespace zyg::ui {
namespace {
juce::Colour lineColour(bool live, float depth) {
    // depth 0 = front, 1 = back
    const auto front = live ? pal::acid : pal::acidDim;
    const auto back = live ? pal::acidDim : pal::acidShadow;
    return mix(front, back, depth * 0.75f);
}
void gridBackground(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour well, juce::Colour grid) {
    wellBox(g, r, well);
    const auto in = r.reduced(1);
    for (int i = 1; i < 4; ++i) {
        const int x = in.getX() + in.getWidth() * i / 4;
        for (int y = in.getY() + (i & 1); y < in.getBottom(); y += 2) px(g, x, y, grid);
    }
    const int cy = in.getCentreY();
    for (int x = in.getX(); x < in.getRight(); x += 2) px(g, x, cy, grid);
}
}

// ------------------------------------------------------------- OscDisplay
namespace {
// Raw table lookup for drawing: linear in phase and frame (no mip levels, the display wants the stored shape).
float tableRaw(const Oscillator& o, double phase, double framePos) {
    const int size = int(o.frameSize);
    const int frames = size > 0 ? int(o.audio.size()) / size : 0;
    if (frames < 1 || size < 2) return 0.0f;
    const double fp = std::clamp(framePos, 0.0, double(frames - 1));
    const int f0 = int(fp), f1 = std::min(f0 + 1, frames - 1);
    const float fm = float(fp - f0);
    const double x = (phase - std::floor(phase)) * size;
    const int i0 = std::clamp(int(x), 0, size - 1), i1 = (i0 + 1) % size;
    const float t = float(x - std::floor(x));
    const float* A = o.audio.data() + std::size_t(f0) * std::size_t(size);
    const float* B = o.audio.data() + std::size_t(f1) * std::size_t(size);
    const float a = A[i0] + (A[i1] - A[i0]) * t, b = B[i0] + (B[i1] - B[i0]) * t;
    return a + (b - a) * fm;
}
double framePosOf(const Oscillator& o, double tablePos) {
    return std::clamp(tablePos / 256.0, 0.0, 1.0) * double(std::max(0, frameCount(o) - 1));
}

// Evaluates one displayed cycle of oscillator `idx` at `framePos`, applying both warp slots in the
// engine's order (phase warps / phase modulation first, then amplitude warps). Modulator warps
// (PM/RM/AM) are previewed with the source oscillator at the same pitch; FM changes pitch over
// time and is shown unwarped.
struct WarpPreview {
    const Patch* patch = nullptr;
    int idx = 0;
    float amount[2] {};
    void curve(double framePos, int n, std::vector<float>& out) const {
        const auto& o = patch->oscillators[std::size_t(idx)];
        out.resize(std::size_t(n));
        dsp::WarpState st[2];
        for (int i = 0; i < n; ++i) {
            const double ph = double(i) / double(n);
            double lookup = ph;
            std::array<double, 2> mod {};
            for (std::size_t k = 0; k < 2; ++k) mod[k] = modulator(o.warpDefinitions[k], ph);
            for (std::size_t k = 0; k < 2; ++k) {
                const auto& w = o.warpDefinitions[k];
                const double am = amount[k];
                switch (w.mode) {
                    case WarpMode::frequencyModPhase: lookup += 2.0 * am * mod[k]; break;
                    case WarpMode::phaseMod: lookup += 0.25 * am * mod[k]; break;
                    default: if (dsp::isPhaseWarp(w.mode)) lookup = dsp::warpPhase(lookup, w.mode, am, w.variant); break;
                }
            }
            lookup -= std::floor(lookup);
            double v = tableRaw(o, lookup, framePos);
            for (std::size_t k = 0; k < 2; ++k) {
                const auto& w = o.warpDefinitions[k];
                if (w.mode == WarpMode::evenOdd) { v += (amount[k] - 0.5) * 2.0 * tableRaw(o, lookup + 0.5, framePos); v *= 0.7; }
                else if (!dsp::isPhaseWarp(w.mode) && !dsp::isSpectralWarp(w.mode) && w.mode != WarpMode::frequencyMod
                         && w.mode != WarpMode::frequencyModX && w.mode != WarpMode::frequencyModPhase && w.mode != WarpMode::phaseMod
                         && w.mode != WarpMode::selfPhase && w.mode != WarpMode::off && w.mode != WarpMode::unknown)
                    v = dsp::warpAmplitude(v, lookup, w.mode, amount[k], mod[k], st[k], 48000.0, w.var);
            }
            out[std::size_t(i)] = float(std::isfinite(v) ? std::clamp(v, -1.0, 1.0) : 0.0);
        }
    }
    double modulator(const WarpDefinition& w, double ph) const {
        const int src = w.sourceIndex;
        if (src >= 0 && src < 3) {
            const auto& so = patch->oscillators[std::size_t(src)];
            return so.audio.empty() ? 0.0 : tableRaw(so, ph, framePosOf(so, so.tablePosition));
        }
        if (src == 4) return std::sin(ph * 6.283185307179586);
        return 0.0;
    }
    std::uint64_t key() const {   // everything the mesh depends on besides the table itself
        const auto& o = patch->oscillators[std::size_t(idx)];
        std::uint64_t h = 1469598103934665603ull;
        auto mixIn = [&h](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
        for (std::size_t k = 0; k < 2; ++k) {
            const auto& w = o.warpDefinitions[k];
            mixIn(std::uint64_t(w.mode)); mixIn(std::uint64_t(w.variant + 16)); mixIn(std::uint64_t(std::llround(w.var * 1000.0)));
            mixIn(std::uint64_t(std::llround(amount[k] * 400.0)));
            if (w.sourceIndex >= 0 && w.sourceIndex < 3) {
                const auto& so = patch->oscillators[std::size_t(w.sourceIndex)];
                mixIn(std::uint64_t(reinterpret_cast<std::uintptr_t>(so.audio.data()))); mixIn(std::uint64_t(std::llround(so.tablePosition * 10.0)));
            }
            mixIn(std::uint64_t(w.sourceIndex + 8));
        }
        return h;
    }
};

// Direct raster target for the cached mesh: whole-pixel writes with alpha blending.
struct Raster {
    juce::Image::BitmapData bd;
    int w, h;
    explicit Raster(juce::Image& img) : bd(img, juce::Image::BitmapData::readWrite), w(img.getWidth()), h(img.getHeight()) {}
    void blend(int x, int y, juce::Colour c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const float a = c.getFloatAlpha();
        if (a >= 0.999f) { bd.setPixelColour(x, y, c); return; }
        bd.setPixelColour(x, y, bd.getPixelColour(x, y).interpolatedWith(c.withAlpha(1.0f), a).withAlpha(1.0f));
    }
    void vspan(int x, int y0, int y1, juce::Colour c) { if (y0 > y1) std::swap(y0, y1); for (int y = std::max(0, y0); y <= std::min(h - 1, y1); ++y) blend(x, y, c); }
    void line(int x0, int y0, int x1, int y1, juce::Colour c) {
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (int guard = 0; guard < 4096; ++guard) {
            blend(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
};

// Oblique projection of the table volume into the display rectangle (local coordinates).
struct MeshGeo {
    int w = 0, h = 0, frames = 1;
    double depthX = 0, depthY = 0, amp = 0, frontW = 0, frontY = 0;
    // Tables with few frames (Basic/Default Shapes: a handful of unrelated shapes) are shown as one
    // flat morphing cycle; only real wavetables get the 3D stack.
    static constexpr int kMinMeshFrames = 16;
    bool deep = false;
    explicit MeshGeo(int W, int H, int f) : w(W), h(H), frames(f) {
        deep = frames >= kMinMeshFrames;
        depthX = deep ? W * 0.20 : 0.0;
        depthY = deep ? H * 0.34 : 0.0;
        amp = deep ? H * 0.25 : H * 0.40;
        frontW = W - depthX - 10.0;
        frontY = H - amp - 7.0;
    }
    double persp(double z) const { return 1.0 - 0.16 * z; }   // far slices are slightly narrower and flatter
    double x(double u, double z) const { return 5.0 + z * depthX + frontW * (1.0 - persp(z)) * 0.5 + u * frontW * persp(z); }
    double base(double z) const { return frontY - z * depthY; }
    double y(double v, double z) const { return base(z) - v * amp * persp(z); }
    double floor(double z) const { return base(z) + amp * persp(z); }
};
}

void OscDisplay::pull(const Patch& p) {
    const auto& o = p.oscillators[std::size_t(idx_)];
    pos_ = o.tablePosition;
    if (!modulated_) { warp_[0] = float(o.warpOneAmount); warp_[1] = float(o.warpTwoAmount); }
    repaint();
}

void OscDisplay::tick(double dt) {
    // follow the engine's modulated values while a voice sounds, the patch values otherwise
    zyg::SynthEngine::OscDisplayState st;
    double target = pos_;
    const auto& o = ctx_.patch->oscillators[std::size_t(idx_)];
    modulated_ = idx_ < 3 && ctx_.proc.oscDisplayState(idx_, st);
    if (modulated_) { target = st.tablePos; warp_[0] = st.warp1; warp_[1] = st.warp2; }
    else { warp_[0] = float(o.warpOneAmount); warp_[1] = float(o.warpTwoAmount); }
    const double k = 1.0 - std::exp(-dt * 18.0);
    shownPos_ += (target - shownPos_) * k;
    if (std::abs(target - shownPos_) < 0.02) shownPos_ = target;
    for (int i = 0; i < 2; ++i) { shownWarp_[i] += (warp_[i] - shownWarp_[i]) * float(k); if (std::abs(warp_[i] - shownWarp_[i]) < 1.0e-3f) shownWarp_[i] = warp_[i]; }
    const float act = ctx_.proc.getActiveVoiceCount() > 0 ? std::min(1.0f, 0.35f + ctx_.proc.getOutputPeak() * 2.0f) : 0.0f;
    activity_ += (act - activity_) * float(1.0 - std::exp(-dt * 8.0));
    phase_ += dt;
}

void OscDisplay::mouseDrag(const juce::MouseEvent& e) {
    const int idx = idx_;
    const auto& o = ctx_.patch->oscillators[std::size_t(idx)];
    if (o.mode != OscMode::wavetable) return;
    const double v = juce::jlimit(0.0, 256.0, dragStartPos_ - double(e.getDistanceFromDragStartY()) * 1.5 +
                                                  double(e.getDistanceFromDragStartX()) * 0.75);
    pos_ = v;
    ctx_.edit(this, [idx, v](Patch& p) { p.oscillators[std::size_t(idx)].tablePosition = v; });
    repaint();
}

void OscDisplay::paintContent(juce::Graphics& g) {
    const auto& o = ctx_.patch->oscillators[std::size_t(idx_)];
    const bool live = o.enabled;
    const auto r = getLocalBounds();
    const auto in = r.reduced(2);
    switch (o.mode) {
        case OscMode::wavetable: case OscMode::unknown:
            wellBox(g, r, live ? pal::acidWell : pal::sunken);
            paintWavetable(g, o, in, live); break;
        case OscMode::sub: gridBackground(g, r, live ? pal::acidWell : pal::sunken, live ? pal::acidShadow : pal::panel); paintSub(g, o, in, live); break;
        case OscMode::noise: gridBackground(g, r, live ? pal::acidWell : pal::sunken, live ? pal::acidShadow : pal::panel); paintNoise(g, o, in, live); break;
        default: gridBackground(g, r, live ? pal::acidWell : pal::sunken, live ? pal::acidShadow : pal::panel); paintSample(g, o, in, live); break;
    }
}

void OscDisplay::paintWavetable(juce::Graphics& g, const Oscillator& o, juce::Rectangle<int> r, bool live) {
    const int frames = frameCount(o);
    if (frames < 1) {
        drawTextIn(g, live ? "NO WAVETABLE - DOUBLE CLICK TO LOAD" : "OSC OFF", r, pal::textMuted, juce::Justification::centred);
        return;
    }
    WarpPreview wp {ctx_.patch.get(), idx_, {shownWarp_[0], shownWarp_[1]}};
    const MeshGeo geo(r.getWidth(), r.getHeight(), frames);
    std::vector<float> cyc;

    // ---- cached mesh: back-to-front ribbons with hidden-line fills and depth cross-lines
    std::uint64_t key = wp.key();
    auto mixIn = [&key](std::uint64_t v) { key ^= v; key *= 1099511628211ull; };
    mixIn(std::uint64_t(reinterpret_cast<std::uintptr_t>(o.audio.data()))); mixIn(o.audio.size());
    mixIn(std::uint64_t(r.getWidth()) << 20 | std::uint64_t(r.getHeight())); mixIn(live ? 7 : 3);
    if (geo.deep && (key != meshKey_ || !mesh_.isValid())) {
        meshKey_ = key;
        mesh_ = juce::Image(juce::Image::ARGB, r.getWidth(), r.getHeight(), true);
        Raster ras(mesh_);
        // real frames when there are enough of them; a two-frame table shows its crossfade
        const int slices = frames <= 1 ? 1 : frames == 2 ? 12 : std::min(frames, 40);
        const int cols = std::max(8, int(geo.frontW));
        const auto well = live ? pal::acidWell : pal::sunken;
        std::vector<std::vector<juce::Point<int>>> pts(static_cast<std::size_t>(slices));
        for (int s = slices - 1; s >= 0; --s) {
            const double z = slices > 1 ? double(s) / double(slices - 1) : 0.0;
            wp.curve(z * double(frames - 1), cols, cyc);
            auto& P = pts[std::size_t(s)];
            P.resize(std::size_t(cols));
            for (int i = 0; i < cols; ++i)
                P[std::size_t(i)] = {int(std::round(geo.x(double(i) / double(cols - 1), z))), int(std::round(geo.y(cyc[std::size_t(i)], z)))};
            const float depth = float(z);
            const auto lineC = live ? mix(mix(pal::acid, pal::acidDim, 0.35f), pal::acidShadow, depth * 0.8f) : mix(pal::textMuted, pal::edgeMid, depth);
            const auto crossC = (live ? pal::acidShadow : pal::edgeMid).withAlpha(0.9f);
            // depth cross-lines only for smooth, dense tables: between a handful of unrelated shapes
            // (sine -> square -> saw) they join into meaningless polygons
            if (s + 1 < slices && frames >= 16) {
                const auto& Q = pts[std::size_t(s + 1)];
                for (int c = 0; c <= 16; ++c) {
                    const std::size_t i = std::size_t(c * (cols - 1) / 16);
                    ras.line(Q[i].x, Q[i].y, P[i].x, P[i].y, crossC);
                }
            }
            // occluding ribbon under the curve, then the curve
            const int floorY = int(std::round(geo.floor(z)));
            int prevX = P[0].x;
            for (int i = 0; i < cols; ++i) {
                const auto p = P[std::size_t(i)];
                for (int x = prevX; x <= p.x; ++x) ras.vspan(x, p.y + 1, floorY, well.withAlpha(frames >= 16 ? 0.8f : 0.55f));
                prevX = p.x + 1;
            }
            for (int i = 1; i < cols; ++i) ras.line(P[std::size_t(i - 1)].x, P[std::size_t(i - 1)].y, P[std::size_t(i)].x, P[std::size_t(i)].y, lineC);
        }
        // floor rails (front and right edges of the volume)
        if (frames > 1) {
            const auto rail = live ? pal::acidShadow : pal::edgeMid;
            ras.line(int(geo.x(0, 0)), int(geo.floor(0)), int(geo.x(1, 0)), int(geo.floor(0)), rail);
            ras.line(int(geo.x(1, 0)), int(geo.floor(0)), int(geo.x(1, 1)), int(geo.floor(1)), rail);
        }
    }
    if (geo.deep) g.drawImageAt(mesh_, r.getX(), r.getY());
    else {   // flat view: centre line and faint quarter marks behind the single morphing cycle
        const int cy = r.getY() + int(std::round(geo.base(0)));
        for (int x = r.getX() + 4; x < r.getRight() - 4; x += 2) px(g, x, cy, live ? pal::acidShadow : pal::edgeMid);
    }

    // ---- live frame: warped curve at the (modulated) position with a translucent curtain
    const double fp = framePosOf(o, shownPos_);
    const double z = geo.deep ? fp / double(frames - 1) : 0.0;
    const int cols = std::max(8, int(geo.frontW));
    wp.curve(fp, cols, cyc);
    const auto hot = live ? mix(pal::acidHot, pal::textHi, 0.35f * activity_ * (0.5f + 0.5f * std::sin(float(phase_) * 7.0f))) : pal::textMuted;
    const int ox = r.getX(), oy = r.getY();
    const int baseY = oy + int(std::round(geo.base(z)));
    std::vector<juce::Point<int>> hp(static_cast<std::size_t>(cols));
    for (int i = 0; i < cols; ++i)
        hp[std::size_t(i)] = {ox + int(std::round(geo.x(double(i) / double(cols - 1), z))), oy + int(std::round(geo.y(cyc[std::size_t(i)], z)))};
    if (live) {
        g.setColour(pal::acidHot.withAlpha(0.09f + 0.08f * activity_));
        for (int i = 0; i < cols; ++i) {
            const auto p = hp[std::size_t(i)];
            if (p.y < baseY) g.fillRect(p.x, p.y + 1, 1, baseY - p.y);
            else if (p.y > baseY) g.fillRect(p.x, baseY, 1, p.y - baseY);
        }
        hLine(g, hp.front().x, baseY, hp.back().x - hp.front().x, pal::acidDim.withAlpha(0.6f));
    }
    pixelPolyline(g, hp, hot);
    if (live && activity_ > 0.02f) {   // a spark runs along the live frame while notes play
        const int sx = int(std::fmod(phase_ * 0.9, 1.0) * (cols - 1));
        const auto p = hp[std::size_t(sx)];
        g.setColour(pal::acid.withAlpha(0.35f * activity_)); g.fillRect(p.x - 3, p.y - 3, 7, 7);
        g.setColour(pal::acidHot.withAlpha(activity_)); g.fillRect(p.x - 1, p.y - 1, 3, 3);
    }
    // position marker on the depth rail + readout
    if (geo.deep) {
        const int mx = ox + int(std::round(geo.x(1, z))) + 3, my = oy + int(std::round(geo.floor(z)));
        g.setColour(live ? pal::acidHot : pal::textMuted); g.fillRect(mx, my - 1, 3, 3);
    }
    const juce::String label = frames > 1 ? "FRAME " + juce::String(int(std::round(fp)) + 1) + "/" + juce::String(frames) : "SINGLE CYCLE";
    drawSmall(g, label, r.getX() + 3, r.getY() + 3, modulated_ && live ? pal::acid : pal::textMuted);
}

void OscDisplay::paintSample(juce::Graphics& g, const Oscillator& o, juce::Rectangle<int> r, bool live) {
    const SampleData* s = o.sample.get();
    if (!s && !o.regions.empty()) s = o.regions.front().sample.get();
    if (!s || s->frames() < 2) {
        drawTextIn(g, o.asset.empty() ? "NO SAMPLE LOADED" : "SAMPLE UNRESOLVED", r, pal::textMuted, juce::Justification::centred);
        return;
    }
    const int w = r.getWidth();
    const int cy = r.getCentreY();
    const int half = r.getHeight() / 2 - 2;
    const std::size_t n = s->frames();
    const int startX = r.getX() + int(o.start / 100.0 * w), endX = r.getX() + int(o.end / 100.0 * w);
    for (int x = 0; x < w; ++x) {
        const std::size_t a = std::size_t(x) * n / std::size_t(w), b = std::max(a + 1, std::size_t(x + 1) * n / std::size_t(w));
        float lo = 0, hi = 0;
        for (std::size_t i = a; i < b && i < n; i += std::max<std::size_t>(1, (b - a) / 32)) {
            const float v = s->left[i];
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        const bool inRange = (r.getX() + x >= startX && r.getX() + x <= endX) || o.mode == OscMode::granular;
        const auto c = !live ? pal::textMuted : inRange ? pal::acid : pal::acidDim;
        const int y0 = cy - int(hi * float(half)), y1 = cy - int(lo * float(half));
        g.setColour(c); g.fillRect(r.getX() + x, y0, 1, std::max(1, y1 - y0 + 1));
    }
    if (o.mode == OscMode::granular || o.mode == OscMode::spectral) {
        const int px0 = r.getX() + int(o.position / 100.0 * (w - 1));
        vLine(g, px0, r.getY(), r.getHeight(), pal::acidHot);
    } else {
        vLine(g, startX, r.getY(), r.getHeight(), pal::acidHot);
        vLine(g, endX, r.getY(), r.getHeight(), pal::acidHot);
    }
    drawSmall(g, prettyAssetName(o.asset).substring(0, 30), r.getX() + 3, r.getY() + 3, pal::textMuted);
}

void OscDisplay::paintNoise(juce::Graphics& g, const Oscillator& o, juce::Rectangle<int> r, bool live) {
    const auto body = live ? pal::acidShadow : pal::edgeMid;
    const auto edge = live ? pal::acid : pal::textMuted;
    const int w = r.getWidth(), cy = r.getCentreY();
    const int half = r.getHeight() / 2 - 4;
    std::vector<float> env(std::size_t(std::max(1, w)), 0.0f);
    if (!o.audio.empty()) {
        for (int x = 0; x < w; ++x) {
            const std::size_t a = std::size_t(x) * o.audio.size() / std::size_t(w);
            const std::size_t b = std::max(a + 1, std::size_t(x + 1) * o.audio.size() / std::size_t(w));
            float pk = 0;
            for (std::size_t i = a; i < b; ++i) pk = std::max(pk, std::abs(o.audio[i]));
            env[std::size_t(x)] = pk;
        }
    } else {
        std::uint32_t seed = 0x9e3779b9u;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1 << 24); };
        for (int x = 0; x < w; ++x) {
            float a = 0.55f + 0.45f * rnd();
            if (o.noiseType == NoiseType::pink) a *= 1.0f / (1.0f + float(x) / float(w) * 1.5f);
            else if (o.noiseType == NoiseType::brown) a *= 1.0f / (1.0f + float(x) / float(w) * 4.0f);
            if (o.noiseType == NoiseType::geiger) a = rnd() > 0.93f ? rnd() : 0.02f;
            env[std::size_t(x)] = a * 0.85f;
        }
    }
    float peak = 1.0e-6f; for (float v : env) peak = std::max(peak, v);
    int prevH = -1;
    for (int x = 0; x < w; ++x) {
        const int hgt = int(std::round(env[std::size_t(x)] / peak * float(half) * 0.82f));
        g.setColour(body); g.fillRect(r.getX() + x, cy - hgt, 1, 2 * hgt + 1);
        g.setColour(edge);
        if (prevH < 0) prevH = hgt;
        g.fillRect(r.getX() + x, cy - std::max(hgt, prevH), 1, std::abs(hgt - prevH) + 1);
        g.fillRect(r.getX() + x, cy + std::min(hgt, prevH), 1, std::abs(hgt - prevH) + 1);
        prevH = hgt;
    }
    hLine(g, r.getX(), cy, w, live ? pal::acidHot.withAlpha(0.5f) : pal::edgeMid);
}

void OscDisplay::paintSub(juce::Graphics& g, const Oscillator& o, juce::Rectangle<int> r, bool live) {
    WaveIcon w = WaveIcon::sine;
    switch (o.subShape) {
        case SubShape::pulse: w = WaveIcon::pulse; break; case SubShape::roundedRectangle: w = WaveIcon::rounded; break;
        case SubShape::saw: w = WaveIcon::saw; break; case SubShape::square: w = WaveIcon::square; break;
        case SubShape::triangle: w = WaveIcon::triangle; break; default: break;
    }
    drawWaveIcon(g, w, r.reduced(4, 6), live ? pal::acid : pal::textMuted);
}

// ---------------------------------------------------------- FilterDisplay
void FilterDisplay::pull(const Patch& p) {
    const auto& f = p.filters[std::size_t(index())];
    if (!have_) shown_ = f;
    filter_ = f; have_ = true;
    repaint();
}

void FilterDisplay::tick(double dt) {
    if (!have_) return;
    const double k = 1.0 - std::exp(-dt * 16.0);
    shown_.response = filter_.response; shown_.variant = filter_.variant; shown_.enabled = filter_.enabled;
    shown_.cutoff += (filter_.cutoff - shown_.cutoff) * k;
    shown_.resonance += (filter_.resonance - shown_.resonance) * k;
    shown_.var += (filter_.var - shown_.var) * k;
    shown_.drive = filter_.drive;
    const float target = ctx_.proc.getActiveVoiceCount() > 0 ? std::min(1.0f, 0.3f + ctx_.proc.getOutputPeak() * 2.0f) : 0.0f;
    activity_ += (target - activity_) * float(1.0 - std::exp(-dt * 8.0));
    phase_ += dt * (0.25 + 0.6 * double(activity_));
    if (!mini_) updateSpectrum(dt);
}

// 4096-point Hann-windowed FFT of the newest plugin output, reduced to one peak value per display
// column on the same log-frequency axis as the response, with fast attack / slow fall smoothing.
void FilterDisplay::updateSpectrum(double dt) {
    constexpr int N = 4096;
    const int cols = std::max(1, getWidth() - 4);
    if (fft_.size() != std::size_t(N)) {
        fft_.init(std::size_t(N));
        scope_.assign(N, 0.0f); re_.assign(N, 0.0f); im_.assign(N, 0.0f); window_.resize(N);
        for (int i = 0; i < N; ++i) window_[std::size_t(i)] = 0.5f - 0.5f * std::cos(6.2831853f * float(i) / float(N - 1));
    }
    if (int(specDb_.size()) != cols) specDb_.assign(std::size_t(cols), -120.0f);
    ctx_.proc.copyScope(scope_.data(), N);
    for (int i = 0; i < N; ++i) { re_[std::size_t(i)] = scope_[std::size_t(i)] * window_[std::size_t(i)]; im_[std::size_t(i)] = 0.0f; }
    fft_.forward(re_.data(), im_.data());
    const double sr = ctx_.proc.getSampleRate() > 0 ? ctx_.proc.getSampleRate() : 48000.0;
    const double binHz = sr / N;
    auto magDb = [&](int bin) {
        bin = std::clamp(bin, 1, N / 2 - 1);
        const float m = std::sqrt(re_[std::size_t(bin)] * re_[std::size_t(bin)] + im_[std::size_t(bin)] * im_[std::size_t(bin)]) * 4.0f / float(N);
        return 20.0f * std::log10(std::max(m, 1.0e-7f));
    };
    const float fall = float(dt * 38.0);   // dB per second of release
    for (int x = 0; x < cols; ++x) {
        const double f0 = 20.0 * std::pow(1000.0, double(x) / double(cols)), f1 = 20.0 * std::pow(1000.0, double(x + 1) / double(cols));
        const int b0 = int(std::floor(f0 / binHz)), b1 = std::max(b0, int(std::floor(f1 / binHz)));
        float db;
        if (b1 == b0) {   // less than a bin per column: interpolate between neighbouring bins
            const double fb = f0 / binHz; const float t = float(fb - std::floor(fb));
            db = magDb(b0) * (1.0f - t) + magDb(b0 + 1) * t;
        } else { db = -140.0f; for (int b = b0; b <= b1; ++b) db = std::max(db, magDb(b)); }
        auto& cur = specDb_[std::size_t(x)];
        cur = db > cur ? cur + (db - cur) * 0.6f : std::max(db, cur - fall);
    }
}

void FilterDisplay::mouseDrag(const juce::MouseEvent& e) {
    if (mini_) return;
    const int sel = index();
    const auto r = getLocalBounds().reduced(2);
    const double cutoff = juce::jlimit(0.0, 1.0, double(e.x - r.getX()) / double(std::max(1, r.getWidth() - 1)) * (3.0 + 0.0) / 3.0);
    // x maps log-frequency 20 Hz..20 kHz which is exactly the cutoff parameter range.
    const double reso = juce::jlimit(0.0, 100.0, (1.0 - double(e.y - r.getY()) / double(std::max(1, r.getHeight() - 1))) * 130.0 - 30.0);
    ctx_.edit(this, [sel, cutoff, reso](Patch& p) { p.filters[std::size_t(sel)].cutoff = cutoff; p.filters[std::size_t(sel)].resonance = reso; });
    filter_.cutoff = cutoff; filter_.resonance = reso;
    repaint();
}

void FilterDisplay::paintContent(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const bool live = have_ && filter_.enabled;
    wellBox(g, r, pal::violetWell);
    const auto in = r.reduced(2);
    // octave grid: 100 Hz, 1 kHz, 10 kHz
    for (double hz : {100.0, 1000.0, 10000.0}) {
        const int x = in.getX() + int(std::round(std::log(hz / 20.0) / std::log(1000.0) * (in.getWidth() - 1)));
        for (int y = in.getY() + 1; y < in.getBottom(); y += 2) px(g, x, y, pal::violetShadow);
    }
    const double dbTop = 24.0, dbBottom = -42.0;
    auto yOf = [&](double db) {
        return in.getY() + int(std::round((dbTop - std::clamp(db, dbBottom, dbTop)) / (dbTop - dbBottom) * (in.getHeight() - 1)));
    };
    const int y0 = yOf(0.0);
    for (int x = in.getX(); x < in.getRight(); x += 2) px(g, x, y0, pal::violetShadow);
    if (!have_) return;
    const auto line = live ? pal::violetHot : pal::textMuted;
    const auto fill = (live ? pal::violetShadow : pal::panelHi).withAlpha(0.72f);
    std::vector<int> ys(std::size_t(std::max(1, in.getWidth())));
    for (int i = 0; i < in.getWidth(); ++i) {
        const double hz = 20.0 * std::pow(1000.0, double(i) / double(std::max(1, in.getWidth() - 1)));
        ys[std::size_t(i)] = yOf(filterMagnitudeDb(shown_, hz));
        g.setColour(fill);
        g.fillRect(in.getX() + i, ys[std::size_t(i)] + 1, 1, std::max(0, in.getBottom() - ys[std::size_t(i)] - 1));
    }
    // output spectrum over the response fill (-90..0 dBFS over the full height, +4.5 dB/oct tilt so pink noise reads flat)
    if (!mini_ && int(specDb_.size()) == in.getWidth()) {
        int prevY = -1;
        for (int i = 0; i < in.getWidth(); ++i) {
            const double oct = std::log2(20.0 * std::pow(1000.0, double(i) / double(in.getWidth())) / 1000.0);
            const double db = double(specDb_[std::size_t(i)]) + 4.5 * oct;
            if (db < -90.0) { prevY = -1; continue; }
            const int y = in.getBottom() - 1 - int(std::round(std::clamp((db + 90.0) / 90.0, 0.0, 1.0) * (in.getHeight() - 8)));
            g.setColour(pal::acid.withAlpha(0.16f)); g.fillRect(in.getX() + i, y, 1, in.getBottom() - y);
            g.setColour(pal::acidDim.withAlpha(0.85f));
            if (prevY < 0) g.fillRect(in.getX() + i, y, 1, 1); else g.fillRect(in.getX() + i, std::min(y, prevY), 1, std::abs(y - prevY) + 1);
            prevY = y;
        }
    }
    int prev = 0;
    g.setColour(line);
    for (int i = 0; i < in.getWidth(); ++i) {
        const int y = ys[std::size_t(i)];
        if (i == 0) g.fillRect(in.getX(), y, 1, 1);
        else g.fillRect(in.getX() + i, std::min(y, prev), 1, std::abs(y - prev) + 1);
        prev = y;
    }
    // cutoff marker
    const int cx = in.getX() + int(std::round(std::clamp(shown_.cutoff, 0.0, 1.0) * (in.getWidth() - 1)));
    vLine(g, cx, in.getY(), 3, live ? pal::acid : pal::textMuted);
    vLine(g, cx, in.getBottom() - 3, 3, live ? pal::acid : pal::textMuted);
    if (mini_) return;
    // a spark travels along the response and glows harder while sound plays
    if (live) {
        const double u = std::fmod(phase_, 1.0);
        const int sx = int(u * (in.getWidth() - 1));
        const double hz = 20.0 * std::pow(1000.0, u);
        const int sy = yOf(filterMagnitudeDb(shown_, hz));
        g.setColour(pal::violetHot.withAlpha(0.25f + 0.4f * activity_)); g.fillRect(in.getX() + sx - 3, sy - 3, 7, 7);
        g.setColour(pal::acidHot.withAlpha(0.6f + 0.4f * activity_)); g.fillRect(in.getX() + sx - 1, sy - 1, 3, 3);
    }
    drawText(g, fmt::cutoffHz(filter_.cutoff), in.getX() + 3, in.getY() + 2, live ? pal::lcd : pal::textMuted);
    drawSmall(g, "100", in.getX() + int(std::round(std::log(100.0 / 20.0) / std::log(1000.0) * (in.getWidth() - 1))) + 2, in.getBottom() - 7, pal::textMuted);
    drawSmall(g, "1K", in.getX() + int(std::round(std::log(1000.0 / 20.0) / std::log(1000.0) * (in.getWidth() - 1))) + 2, in.getBottom() - 7, pal::textMuted);
    drawSmall(g, "10K", in.getX() + int(std::round(std::log(10000.0 / 20.0) / std::log(1000.0) * (in.getWidth() - 1))) + 2, in.getBottom() - 7, pal::textMuted);
}

}
