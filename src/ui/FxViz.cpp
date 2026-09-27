#include "FxViz.h"
#include "../FxParams.h"
#include <cmath>

namespace zyg::ui {
namespace {
constexpr double kTau = 6.283185307179586;
float hash01(unsigned i) { i ^= i << 13; i ^= i >> 17; i ^= i * 2654435761u; i ^= i << 5; return float((i * 2246822519u) >> 8 & 0xffff) / 65535.0f; }
juce::Colour dimmed(juce::Colour c, bool on) { return on ? c : pal::textMuted; }
}

juce::Colour fxColour(FxType t) {
    switch (t) {
        case FxType::distortion: return pal::amber;
        case FxType::comp: return pal::acid;
        case FxType::reverb: case FxType::conv: return pal::violetHot;
        case FxType::delay: return pal::violetHot;
        case FxType::eq: return pal::acid;
        case FxType::filter: return pal::violetHot;
        case FxType::chorus: case FxType::flanger: case FxType::phaser: case FxType::hyperD: return juce::Colour(0xff4ee1ff);
        case FxType::bode: return pal::crimson;
        case FxType::utils: return pal::acidHot;
        case FxType::pump: return pal::acidHot;
        case FxType::stutter: return pal::crimson;
        default: return pal::violet;
    }
}

void drawFxGlyph(juce::Graphics& g, FxType t, juce::Rectangle<int> r, juce::Colour ink, juce::Colour dim) {
    // Every mark is drawn in a 48 x 30 design box centred in `r`, with 2 px strokes.
    const int W = 48, H = 30, ox = r.getX() + (r.getWidth() - W) / 2, oy = r.getY() + (r.getHeight() - H) / 2;
    auto P = [&](double x, double y) { return juce::Point<int>(ox + int(std::round(x)), oy + int(std::round(y))); };
    auto thick = [&](const std::vector<juce::Point<int>>& pts, juce::Colour c) {
        pixelPolyline(g, pts, c);
        std::vector<juce::Point<int>> q(pts); for (auto& p : q) p.y += 1;
        pixelPolyline(g, q, c);
    };
    auto fn = [&](double x0, double x1, const std::function<double(double)>& f, juce::Colour c, bool bold = true) {
        std::vector<juce::Point<int>> pts;
        for (double x = x0; x <= x1; x += 1.0) pts.push_back(P(x, f((x - x0) / std::max(1.0, x1 - x0))));
        if (bold) thick(pts, c); else pixelPolyline(g, pts, c);
    };
    auto rect = [&](double x, double y, double w, double h, juce::Colour c) { g.setColour(c); g.fillRect(ox + int(x), oy + int(y), int(w), int(h)); };
    constexpr double tau = 6.283185307179586;
    switch (t) {
        case FxType::delay:        // decaying echo pulses on a baseline
            for (int i = 0; i < 4; ++i) { const double h = 26.0 * std::pow(0.62, i); rect(4 + i * 12, 28 - h, 4, h, i ? mix(ink, dim, 0.2f * float(i)) : ink); }
            rect(0, 28, 48, 2, dim); break;
        case FxType::reverb: {     // dense diffuse tail after a direct hit
            rect(3, 2, 3, 26, ink);
            std::uint32_t s = 7u;
            for (int x = 8; x < 48; ++x) {
                const double env = std::exp(-(x - 8) / 14.0);
                for (int k = 0; k < 3; ++k) { s = s * 1664525u + 1013904223u; const double y = 28 - env * 24.0 * double(s >> 24) / 255.0; px(g, ox + x, oy + int(y), k ? dim : ink); }
            }
            rect(0, 28, 48, 2, dim); break;
        }
        case FxType::conv:         // impulse response: spike + ringing decay
            rect(3, 2, 3, 26, ink);
            fn(7, 47, [&](double u) { return 15 - 12 * std::exp(-u * 3.5) * std::sin(u * tau * 5.0); }, ink);
            break;
        case FxType::distortion:   // the clean sine (dim) and its clipped copy on the clip rails
            fn(0, 47, [&](double u) { return 15 - 13 * std::sin(u * tau * 2.0); }, dim, false);
            fn(0, 47, [&](double u) { return 15 - std::clamp(13 * std::sin(u * tau * 2.0), -7.0, 7.0); }, ink);
            for (int x = 0; x < 48; x += 2) { px(g, ox + x, oy + 7, dim); px(g, ox + x, oy + 23, dim); }
            break;
        case FxType::chorus:       // two detuned voices
            fn(0, 47, [&](double u) { return 15 - 10 * std::sin(u * tau * 1.5 + 0.9); }, dim);
            fn(0, 47, [&](double u) { return 15 - 10 * std::sin(u * tau * 1.5); }, ink); break;
        case FxType::flanger:      // many narrow comb teeth
            fn(0, 47, [&](double u) { return 4 + 22 * (1.0 - std::pow(std::abs(std::cos(u * tau * 3.5)), 8.0)); }, ink);
            break;
        case FxType::phaser:       // a few wide notches
            fn(0, 47, [&](double u) { const double d = std::pow(std::abs(std::sin(u * tau * 1.5)), 0.5); return 5 + 21 * (1.0 - d); }, ink);
            break;
        case FxType::eq:           // bell curves with band handles
            fn(0, 47, [&](double u) { return 17 - 11 * std::exp(-std::pow((u - 0.28) / 0.1, 2)) + 8 * std::exp(-std::pow((u - 0.72) / 0.09, 2)); }, ink);
            rect(12, 5, 3, 3, pal::textHi); rect(33, 24, 3, 3, pal::textHi); rect(0, 17, 48, 1, dim); break;
        case FxType::filter:       // resonant low-pass
            fn(0, 47, [&](double u) { return u < 0.55 ? 10 - 7 * std::exp(-std::pow((u - 0.55) / 0.08, 2)) : 10 - 7 * std::exp(-std::pow((u - 0.55) / 0.06, 2)) + (u - 0.55) * 42; }, ink);
            break;
        case FxType::comp:         // static transfer curve with knee + threshold
            for (int x = 0; x < 48; x += 3) px(g, ox + x, oy + 12, dim);
            fn(2, 45, [&](double u) { const double in = u, out = in < 0.6 ? in : 0.6 + (in - 0.6) * 0.25; return 28 - out * 26 / 0.7; }, ink);
            break;
        case FxType::bode:         // frequency shift: wave pushed right with an arrow
            fn(0, 34, [&](double u) { return 15 - 9 * std::sin(u * tau * 2.0); }, dim, false);
            fn(6, 40, [&](double u) { return 15 - 9 * std::sin(u * tau * 2.0); }, ink);
            thick({P(38, 7), P(46, 15), P(38, 23)}, ink); break;
        case FxType::hyperD:       // stacked detuned saws (dimension)
            for (int k = 2; k >= 0; --k) {
                const auto c = k ? mix(ink, dim, 0.4f * float(k)) : ink;
                fn(k * 3, 47 - (2 - k) * 3, [&](double u) { const double ph = u * 2.0 - std::floor(u * 2.0); return 26 - k * 5 - ph * 16; }, c, k == 0);
            }
            break;
        case FxType::utils:        // gain staircase
            for (int i = 0; i < 6; ++i) rect(2 + i * 8, 26 - i * 4 - 2, 6, i * 4 + 4, i < 4 ? ink : mix(ink, pal::amber, 0.6f));
            break;
        case FxType::split: case FxType::split3: case FxType::splitMS: {   // signal fork
            const int n = t == FxType::split3 ? 3 : 2;
            thick({P(0, 15), P(14, 15)}, ink);
            for (int i = 0; i < n; ++i) { const double y = n == 3 ? 3 + i * 12 : 6 + i * 18; thick({P(14, 15), P(28, y), P(46, y)}, ink); }
            if (t == FxType::splitMS) { drawSmall(g, "M", ox + 38, oy - 1, pal::textHi); drawSmall(g, "S", ox + 38, oy + 25, pal::textHi); }
            break;
        }
        case FxType::pump:         // sidechain ducking fins
            for (int k = 0; k < 3; ++k) fn(k * 16, k * 16 + 15, [&](double u) { return 4 + 24 * std::exp(-u * 4.0); }, ink);
            break;
        case FxType::stutter:      // repeated slices getting shorter
            for (int k = 0; k < 5; ++k) { const double w = std::max(2.0, 10.0 - k * 2); rect(k * 10, 4, w, 22, k ? mix(ink, dim, 0.15f * float(k)) : ink); }
            break;
        default:
            drawTextIn(g, "FX", r, ink, juce::Justification::centred, 2, true);
            break;
    }
}

FxViz::FxViz(UiContext& c, int fxIndex) : AnimatedView(30), ctx_(c), index_(fxIndex) {
    setOpaque(true);
    lastNotes_ = ctx_.proc.getMidiNoteCount();
}

void FxViz::pull(const Patch& p) {
    valid_ = index_ >= 0 && index_ < int(p.fx.size());
    if (valid_) m_ = p.fx[std::size_t(index_)];
    repaint();
}

void FxViz::tick(double dt) {
    const float peak = ctx_.proc.getOutputPeak();
    level_ = std::max(peak, level_ * std::exp(-float(dt) * 4.0f));
    const unsigned n = ctx_.proc.getMidiNoteCount();
    if (n != lastNotes_) { lastNotes_ = n; kick_ = 1.0f; age_ = 0.0; }
    kick_ = std::max(0.0f, kick_ - float(dt) * 0.8f);
    age_ += dt;
    if (age_ > 4.5) age_ = 0.0;      // idle loop so the display keeps demonstrating the tail
    phase_ += dt;
}

void FxViz::paintContent(juce::Graphics& g) {
    const auto r = getLocalBounds();
    const auto col = valid_ ? fxColour(m_.fxType) : pal::textMuted;
    wellBox(g, r, mix(pal::sunken, col, 0.05f));
    if (!valid_) return;
    const auto in = r.reduced(2);
    // faint grid
    for (int x = in.getX() + 8; x < in.getRight(); x += 16) for (int y = in.getY() + 1; y < in.getBottom(); y += 4) px(g, x, y, pal::edgeDark);
    g.saveState(); g.reduceClipRegion(in);
    switch (m_.fxType) {
        case FxType::reverb: case FxType::conv: paintReverb(g, in); break;
        case FxType::delay: paintDelay(g, in); break;
        case FxType::chorus: case FxType::flanger: case FxType::phaser: case FxType::hyperD: paintModulation(g, in); break;
        case FxType::distortion: paintDistortion(g, in); break;
        case FxType::comp: paintComp(g, in); break;
        case FxType::eq: paintEq(g, in); break;
        case FxType::filter: paintFilter(g, in); break;
        case FxType::utils: paintUtils(g, in); break;
        case FxType::split: case FxType::split3: case FxType::splitMS: paintSplit(g, in); break;
        case FxType::bode: paintBode(g, in); break;
        case FxType::pump: paintPump(g, in); break;
        case FxType::stutter: paintStutter(g, in); break;
        default: break;
    }
    g.restoreState();
}

// -------------------------------------------------------------------- reverb / convolve
void FxViz::paintReverb(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(fxColour(m_.fxType), on);
    const bool conv = m_.fxType == FxType::conv;
    const double size = conv ? v(fx::vSize) / 1000.0 : v(fx::rSize) / 100.0;
    const double fb = conv ? 0.7 : v(fx::rFeedback) / 100.0;
    const double decay = conv ? std::max(0.2, v(fx::vDecay) / 40.0) : 0.25 + size * 0.6 + fb * 0.3;      // 0..1 of the width
    const double preDelay = conv ? v(fx::vPredelay) / 0.4 * 0.15 : v(fx::rPreDelay) / 2.5 * 0.4;
    const int w = r.getWidth(), h = r.getHeight();
    const double playhead = on ? std::min(1.0, age_ / 3.2) : 1.0;     // 0..1 across the width
    // expanding rings from the source point (echo of the last note)
    if (on) {
        const int cx = r.getX() + 8, cy = r.getBottom() - 10;
        for (int k = 0; k < 3; ++k) {
            const double a = age_ * 1.6 - k * 0.55;
            if (a < 0 || a > 1.6) continue;
            const float R = float(a * double(h) * 0.9);
            const float alpha = float((1.0 - a / 1.6) * (0.45 + 0.4 * kick_ + 0.6 * double(level_)));
            g.setColour(col.withAlpha(std::clamp(alpha, 0.0f, 0.9f)));
            for (int i = 0; i < 40; ++i) {
                const float t = float(i) / 40.0f * 3.14159f * 0.5f;     // quarter ring is enough in the corner
                g.fillRect(cx + int(std::round(std::cos(t) * R)), cy - int(std::round(std::sin(t) * R)), 1, 1);
            }
        }
    }
    // decay tail: reflections whose height follows the envelope
    const int n = w - 4;
    for (int i = 0; i < n; i += 2) {
        const double x = double(i) / double(n);
        if (x < preDelay) continue;
        const double e = std::exp(-(x - preDelay) / std::max(0.05, decay) * 3.2);
        const float dens = hash01(unsigned(i) * 7u + 3u);
        const double sizeMod = (0.35 + 0.65 * dens);
        const int bar = int(std::round(e * sizeMod * double(h - 8)));
        if (bar < 1) continue;
        const bool lit = x <= playhead;
        const float pulse = lit ? float(1.0 - std::min(1.0, (playhead - x) * 6.0)) : 0.0f;
        g.setColour((lit ? mix(col, pal::acidHot, pulse * 0.5f) : mix(col, pal::sunken, 0.72f)).withAlpha(lit ? 0.95f : 0.8f));
        g.fillRect(r.getX() + 2 + i, r.getBottom() - 3 - bar, 1, bar);
    }
    vLine(g, r.getX() + 2 + int(playhead * n), r.getY() + 1, h - 2, on ? pal::acidHot.withAlpha(0.6f) : pal::edgeMid);
    // the direct sound
    g.setColour(on ? pal::acid : pal::textMuted); g.fillRect(r.getX() + 2, r.getBottom() - 3 - (h - 6), 2, h - 6);
}

// -------------------------------------------------------------------- delay
void FxViz::paintDelay(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const bool pingPong = v(fx::dMode) > 1.5;
    const double tl = v(fx::dTimeL) * v(fx::dOffsetL), tr = v(fx::dTimeR) * v(fx::dOffsetR);
    const double fb = v(fx::dFeedback) / 100.0;
    const double span = std::max(0.3, std::max(tl, tr) * 6.5);
    const int w = r.getWidth() - 6, h = r.getHeight();
    const double playhead = on ? std::fmod(age_ * 0.9, 1.0) * span * 1.0 : span;
    const auto cl = dimmed(pal::acid, on), cr = dimmed(pal::violetHot, on);
    for (int lane = 0; lane < 2; ++lane) {
        const int y0 = r.getY() + 3 + lane * (h / 2), lh = h / 2 - 5;
        const double step = pingPong ? (lane == 0 ? tl : tr) * 2.0 : (lane == 0 ? tl : tr);
        const double off = pingPong && lane == 1 ? tl : 0.0;
        for (int k = 0; k < 12; ++k) {
            const double t = off + step * k;
            if (t > span) break;
            const double amp = std::pow(std::max(0.02, fb), double(k) * (pingPong ? 0.5 : 1.0)) * (k == 0 && !pingPong ? 1.0 : 1.0);
            const int x = r.getX() + 3 + int(std::round(t / span * w));
            const int bh = std::max(2, int(std::round(amp * lh)));
            const bool hit = on && std::abs(playhead - t) < span * 0.04;
            g.setColour(hit ? pal::acidHot : (lane == 0 ? cl : cr));
            g.fillRect(x, y0 + lh - bh, 3, bh);
            if (hit) { g.setColour(pal::acidHot.withAlpha(0.3f)); g.fillRect(x - 1, y0 + lh - bh - 2, 5, bh + 2); }
        }
        hLine(g, r.getX() + 2, y0 + lh + 1, r.getWidth() - 4, pal::edgeMid);
    }
    drawText(g, "L", r.getX() + 2, r.getY() + 2, cl); drawText(g, "R", r.getX() + 2, r.getY() + h / 2 + 1, cr);
    const int px0 = r.getX() + 3 + int(playhead / span * w);
    if (on) vLine(g, px0, r.getY(), h, pal::acidHot.withAlpha(0.5f));
}

// -------------------------------------------------------------------- chorus / flanger / phaser / hyper-dimension
void FxViz::paintModulation(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(fxColour(m_.fxType), on);
    const int w = r.getWidth(), h = r.getHeight(), cy = r.getCentreY();
    double rate = 0.5, depth = 0.5;
    switch (m_.fxType) {
        case FxType::chorus: rate = v(fx::cRate) / 1.4; depth = v(fx::cDepth) / 26.0; break;
        case FxType::flanger: rate = v(fx::lRate) / 9.0; depth = v(fx::lDepth) / 100.0; break;
        case FxType::phaser: rate = v(fx::aRate) / 20.0; depth = v(fx::aDepth) / 100.0; break;
        default: rate = v(fx::hRate) / 100.0; depth = v(fx::hDetune) / 100.0; break;
    }
    const double speed = on ? (0.3 + rate * 3.0) : 0.0;
    const double ph = phase_ * speed * kTau * 0.5;
    if (m_.fxType == FxType::chorus || m_.fxType == FxType::hyperD) {
        const int voices = m_.fxType == FxType::hyperD ? std::max(2, int(v(fx::hUnison)) + 1) : 3;
        for (int k = 0; k < voices; ++k) {
            const double spread = (double(k) - double(voices - 1) * 0.5) * (0.35 + depth * 0.9);
            std::vector<juce::Point<int>> pts;
            for (int x = 0; x < w; ++x) {
                const double t = double(x) / double(w) * 3.0 * kTau;
                const double y = std::sin(t + ph * (1.0 + 0.11 * k) + spread) * (0.32 + 0.2 * depth) * h * 0.5;
                pts.push_back({r.getX() + x, cy - int(std::round(y))});
            }
            pixelPolyline(g, pts, k == voices / 2 ? mix(col, pal::acidHot, 0.35f) : col.withAlpha(0.55f + 0.1f * float(k % 3)));
        }
    } else {
        // moving comb / all-pass notches
        int prev = 0;
        const int stages = m_.fxType == FxType::phaser ? std::max(2, int(v(fx::aNumPoles))) : 4;
        const double g0 = 0.55 + 0.35 * (m_.fxType == FxType::phaser ? v(fx::aFeedback) / 100.0 : v(fx::lFeedback) / 95.0);
        const double sweep = 0.5 + 0.5 * std::sin(ph);
        for (int x = 0; x < w; ++x) {
            const double f = double(x) / double(w);
            double mag;
            if (m_.fxType == FxType::flanger) {
                const double T = 6.0 + sweep * (6.0 + depth * 20.0);
                mag = std::abs(1.0 + g0 * std::cos(f * T * 3.14159 * 2.0));
            } else {
                const double centre = 0.15 + sweep * depth * 0.7;
                const double theta = stages * 2.0 * std::atan((f + 0.02) / std::max(0.05, centre));
                mag = std::sqrt(1.0 + g0 * g0 + 2.0 * g0 * std::cos(theta));
            }
            const int y = r.getBottom() - 4 - int(std::round(std::clamp(mag / 2.0, 0.0, 1.0) * double(h - 8)));
            g.setColour(col.withAlpha(0.25f)); g.fillRect(r.getX() + x, y, 1, r.getBottom() - y);
            g.setColour(col);
            if (x == 0) g.fillRect(r.getX(), y, 1, 1); else g.fillRect(r.getX() + x, std::min(y, prev), 1, std::abs(y - prev) + 1);
            prev = y;
        }
    }
}

// -------------------------------------------------------------------- distortion
void FxViz::paintDistortion(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::amber, on);
    const double drive = 1.0 + v(fx::xDrive) / 100.0 * 9.0;
    const int mode = m_.modeVariant;
    auto shape = [&](double x) {
        const double d = x * drive;
        switch (mode) {
            case 0: return std::tanh(d + 0.3 * d * d) * 0.85;               // asym
            case 1: return d > 0 ? std::tanh(d) : 0.35 * std::tanh(d * 0.6);   // diode 1
            case 2: return d > 0 ? std::tanh(d * 1.4) : std::max(-0.15, 0.2 * d);
            case 3: return std::round(d * 6.0) / 6.0 / std::max(1.0, drive * 0.5);   // downsample
            case 4: return std::clamp(d, -1.0, 1.0);
            case 5: { double y = d; while (std::abs(y) > 1.0) y = std::copysign(2.0 - std::abs(y), y); return y; }
            case 7: return std::abs(std::tanh(d));
            case 8: return std::sin(d * 1.5707963);
            case 9: return std::sin(std::tanh(d) * 1.5707963);
            case 10: return d / (1.0 + std::abs(d));
            case 12: return std::tanh(d * 1.8) * 0.9;
            case 13: return std::tanh(d) * (1.0 - 0.15 * d * d / (1.0 + d * d));
            case 16: return d >= 0 ? 1.0 : -1.0;
            default: return std::tanh(d);
        }
    };
    const int w = r.getWidth(), h = r.getHeight();
    const int cx = r.getCentreX(), cy = r.getCentreY();
    vLine(g, cx, r.getY(), h, pal::edgeMid); hLine(g, r.getX(), cy, w, pal::edgeMid);
    std::vector<juce::Point<int>> pts;
    for (int x = 0; x < w; ++x) {
        const double in = (double(x) / double(w - 1)) * 2.0 - 1.0;
        const double out = std::clamp(shape(in), -1.2, 1.2);
        pts.push_back({r.getX() + x, cy - int(std::round(out * double(h - 6) * 0.5))});
    }
    pixelPolyline(g, pts, col);
    // moving operating point with a short trail
    const double amp = on ? std::clamp(0.35 + 0.55 * double(level_) + 0.3 * kick_, 0.2, 1.0) : 0.0;
    for (int k = 5; k >= 0; --k) {
        const double in = std::sin((phase_ - k * 0.03) * kTau * 0.6) * amp;
        const double out = std::clamp(shape(in), -1.2, 1.2);
        const int x = cx + int(std::round(in * double(w - 4) * 0.5)), y = cy - int(std::round(out * double(h - 6) * 0.5));
        g.setColour((k == 0 ? pal::acidHot : col).withAlpha(k == 0 ? 1.0f : 0.5f - 0.07f * float(k)));
        g.fillRect(x - (k == 0 ? 1 : 0), y - (k == 0 ? 1 : 0), k == 0 ? 3 : 1, k == 0 ? 3 : 1);
    }
}

// -------------------------------------------------------------------- compressor
void FxViz::paintComp(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::acid, on);
    const double th = std::clamp(v(fx::pThresh), 0.0, 1.0);
    const double ratio = std::max(1.0, v(fx::pRatio));
    const double ratioBelow = std::clamp(v(fx::pRatioBelow), 0.02, 1.0);
    const int w = r.getWidth() - 4, h = r.getHeight() - 4;
    auto out = [&](double x) {
        if (x > th) return th + (x - th) / ratio;
        return th - (th - x) * (1.0 / ratioBelow > 1.0 ? std::min(4.0, 1.0 / ratioBelow) : 1.0) * (ratioBelow < 1.0 ? 1.0 : 1.0);
    };
    hLine(g, r.getX() + 2, r.getBottom() - 3, w, pal::edgeMid); vLine(g, r.getX() + 2, r.getY() + 2, h, pal::edgeMid);
    for (int x = 0; x < w; x += 3) px(g, r.getX() + 2 + x, r.getBottom() - 3 - x * h / w, pal::edgeMid);  // unity line
    std::vector<juce::Point<int>> pts;
    for (int x = 0; x < w; ++x) {
        const double in = double(x) / double(w - 1);
        pts.push_back({r.getX() + 2 + x, r.getBottom() - 3 - int(std::round(std::clamp(out(in), 0.0, 1.0) * double(h - 2)))});
    }
    pixelPolyline(g, pts, col);
    vLine(g, r.getX() + 2 + int(th * (w - 1)), r.getY() + 2, h, pal::amberDim);
    // live operating point from the output level
    const double lv = on ? std::clamp(0.05 + double(level_) * 1.4, 0.0, 1.0) : 0.0;
    const int x = r.getX() + 2 + int(lv * (w - 1)), y = r.getBottom() - 3 - int(std::clamp(out(lv), 0.0, 1.0) * double(h - 2));
    if (on) { g.setColour(pal::acidHot); g.fillRect(x - 1, y - 1, 3, 3); }
    // gain-reduction bar
    const int gr = on ? int(std::round(std::max(0.0, lv - out(lv)) * double(h))) : 0;
    g.setColour(pal::crimson); g.fillRect(r.getRight() - 5, r.getY() + 2, 3, gr);
}

// -------------------------------------------------------------------- equalizer
void FxViz::paintEq(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::acid, on);
    const int w = r.getWidth(), h = r.getHeight();
    auto xOf = [&](double hz) { return r.getX() + int(std::round(std::log(std::max(20.0, hz) / 20.0) / std::log(1000.0) * (w - 1))); };
    auto bandDb = [&](int band, double hz) {
        const double f0 = band == 0 ? v(fx::eFreq1) : v(fx::eFreq2), gain = band == 0 ? v(fx::eGain1) : v(fx::eGain2);
        const double q = (band == 0 ? v(fx::eReso1) : v(fx::eReso2)) / 100.0;
        const double type = band == 0 ? v(fx::eType1) : v(fx::eType2);
        const double x = std::log2(hz / f0);
        if (type < 1.5) return gain * std::exp(-x * x * (0.6 + 2.2 * q));                 // peaking
        const double s = 1.0 / (1.0 + std::exp((band == 0 ? 1.0 : -1.0) * x * 3.0));       // shelf
        return gain * s;
    };
    const int cy = r.getCentreY();
    hLine(g, r.getX(), cy, w, pal::edgeMid);
    int prev = cy;
    for (int x = 0; x < w; ++x) {
        const double hz = 20.0 * std::pow(1000.0, double(x) / double(w - 1));
        const double db = bandDb(0, hz) + bandDb(1, hz);
        const int y = cy - int(std::round(std::clamp(db, -24.0, 24.0) / 24.0 * double(h) * 0.42));
        g.setColour(col.withAlpha(0.22f)); g.fillRect(r.getX() + x, std::min(y, cy), 1, std::abs(y - cy) + 1);
        g.setColour(col);
        if (x == 0) g.fillRect(r.getX(), y, 1, 1); else g.fillRect(r.getX() + x, std::min(y, prev), 1, std::abs(y - prev) + 1);
        prev = y;
    }
    for (int b = 0; b < 2; ++b) {
        const double f0 = b == 0 ? v(fx::eFreq1) : v(fx::eFreq2), gain = b == 0 ? v(fx::eGain1) : v(fx::eGain2);
        const int x = xOf(f0), y = cy - int(std::round(std::clamp(gain, -24.0, 24.0) / 24.0 * double(h) * 0.42));
        g.setColour(pal::edgeDark); g.fillRect(x - 2, y - 2, 5, 5); g.setColour(on ? pal::acidHot : pal::textMuted); g.fillRect(x - 1, y - 1, 3, 3);
    }
    if (on) { const int sx = r.getX() + int(std::fmod(phase_ * 0.35, 1.0) * w); vLine(g, sx, r.getY(), h, pal::acid.withAlpha(0.18f + 0.3f * level_)); }
}

// -------------------------------------------------------------------- filter effect
void FxViz::paintFilter(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    Filter f; f.response = m_.filterResponse; f.variant = m_.filterVariant; f.cutoff = v(fx::fFreq);
    f.resonance = v(fx::fReso); f.var = v(fx::fVar); f.drive = v(fx::fDrive);
    const auto col = dimmed(pal::violetHot, on);
    const int w = r.getWidth(), h = r.getHeight();
    auto yOf = [&](double db) { return r.getY() + 2 + int(std::round((24.0 - std::clamp(db, -42.0, 24.0)) / 66.0 * (h - 5))); };
    int prev = 0;
    std::vector<int> curveY; curveY.resize(std::size_t(w));
    for (int x = 0; x < w; ++x) {
        const double hz = 20.0 * std::pow(1000.0, double(x) / double(w - 1));
        const int y = yOf(filterMagnitudeDb(f, hz)); curveY[std::size_t(x)] = y;
        g.setColour(mix(pal::violetShadow, pal::sunken, 0.3f)); g.fillRect(r.getX() + x, y + 1, 1, std::max(0, r.getBottom() - y));
        g.setColour(col);
        if (x == 0) g.fillRect(r.getX(), y, 1, 1); else g.fillRect(r.getX() + x, std::min(y, prev), 1, std::abs(y - prev) + 1);
        prev = y;
    }
    if (on) {   // a spark that runs along the response; brighter while sound passes
        const int sx = int(std::fmod(phase_ * (0.25 + 0.5 * level_), 1.0) * (w - 1));
        g.setColour(pal::acidHot.withAlpha(0.5f + 0.5f * level_));
        g.fillRect(r.getX() + sx - 1, curveY[std::size_t(sx)] - 1, 3, 3);
    }
}

// -------------------------------------------------------------------- utility
void FxViz::paintUtils(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::acidHot, on);
    const double width = std::clamp(v(fx::uWidth) / 100.0, 0.0, 2.0);
    const int cx = r.getCentreX(), cy = r.getCentreY();
    const int rad = std::min(r.getWidth(), r.getHeight()) / 2 - 3;
    vLine(g, cx, r.getY(), r.getHeight(), pal::edgeMid); hLine(g, r.getX(), cy, r.getWidth(), pal::edgeMid);
    // rotating goniometer: mid axis is vertical; width stretches the side axis
    const double amp = on ? 0.55 + 0.4 * std::min(1.0, double(level_) * 2.0) + 0.2 * kick_ : 0.5;
    for (int i = 0; i < 64; ++i) {
        const double t = phase_ * 1.3 + double(i) * kTau / 64.0;
        const double m = std::sin(t) * amp, s = std::sin(t * 1.5 + 0.7) * amp * width * 0.7;
        const double lx = (m + s) * 0.7071, ly = (m - s) * 0.7071;
        const int x = cx + int(std::round((lx - ly) * 0.7071 * rad)), y = cy - int(std::round((lx + ly) * 0.7071 * rad));
        g.setColour(col.withAlpha(0.15f + 0.6f * float(i) / 64.0f)); g.fillRect(x, y, 1, 1);
    }
    drawText(g, "L", r.getX() + 2, r.getY() + 2, pal::textMuted); drawText(g, "R", r.getRight() - 6, r.getY() + 2, pal::textMuted);
}

// -------------------------------------------------------------------- splitter
void FxViz::paintSplit(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const int w = r.getWidth(), h = r.getHeight();
    if (m_.fxType == FxType::splitMS) {
        for (int b = 0; b < 2; ++b) {
            const auto box = juce::Rectangle<int>(r.getX() + 4 + b * (w / 2), r.getY() + 4, w / 2 - 8, h - 8);
            const double a = 0.5 + 0.5 * std::sin(phase_ * (b ? 2.4 : 1.6) + b) * (0.4 + level_);
            g.setColour(dimmed(b ? pal::violetHot : pal::acid, on).withAlpha(0.2f + 0.5f * float(a))); g.fillRect(box);
            drawTextIn(g, b ? "SIDE" : "MID", box, pal::textHi, juce::Justification::centred, 1, true);
        }
        return;
    }
    const int bands = m_.fxType == FxType::split3 ? 3 : 2;
    auto xOf = [&](double hz) { return r.getX() + int(std::round(std::log(std::max(20.0, hz) / 20.0) / std::log(1000.0) * (w - 1))); };
    const int x1 = xOf(v(fx::sFreq)), x2 = bands == 3 ? xOf(v(fx::sFreq2)) : r.getRight();
    const int xs[4] = {r.getX(), x1, x2, r.getRight()};
    const juce::Colour cols[3] = {pal::acid, pal::violetHot, pal::amber};
    for (int b = 0; b < bands; ++b) {
        const int xa = xs[b], xb = bands == 3 ? xs[b + 1] : (b == 0 ? x1 : r.getRight());
        const double a = 0.35 + 0.65 * (0.5 + 0.5 * std::sin(phase_ * (1.4 + b * 0.9) + b * 2.0)) * (0.3 + std::min(1.0, double(level_) * 3.0));
        const int bh = int(a * double(h - 8));
        g.setColour(dimmed(cols[b], on).withAlpha(0.6f)); g.fillRect(xa + 1, r.getBottom() - 3 - bh, std::max(1, xb - xa - 2), bh);
        vLine(g, xa, r.getY(), h, pal::edgeLight);
    }
}

// -------------------------------------------------------------------- bode frequency shifter
void FxViz::paintBode(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::crimson, on);
    const double shift = v(fx::bShift) / 100.0;
    const int w = r.getWidth(), h = r.getHeight();
    const double drift = on ? std::fmod(phase_ * 0.12 * (shift == 0 ? 0 : (shift > 0 ? 1 : -1)) * (0.3 + std::abs(shift)), 1.0) : 0.0;
    for (int i = 1; i < 22; ++i) {
        const double base = double(i) / 22.0;
        const double x0 = std::fmod(base + shift * 0.35 * 0.5 + drift + 2.0, 1.0);
        const int x = r.getX() + 2 + int(x0 * (w - 4));
        const int bh = int(std::round((1.0 - base * 0.8) * double(h - 8) * (0.65 + 0.35 * std::sin(phase_ * 3.0 + i))));
        g.setColour(mix(pal::edgeMid, pal::sunken, 0.3f)); g.fillRect(r.getX() + 2 + int(base * (w - 4)), r.getBottom() - 3 - (int((1.0 - base * 0.8) * (h - 8))), 1, int((1.0 - base * 0.8) * (h - 8)));
        g.setColour(col); g.fillRect(x, r.getBottom() - 3 - bh, 2, bh);
    }
}

}

namespace zyg::ui {
// -------------------------------------------------------------------- pump (ZYG)
void FxViz::paintPump(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::acidHot, on);
    const int w = r.getWidth() - 4, h = r.getHeight() - 6;
    const double depth = v(fx::uDepth) / 100.0;
    const int mode = int(std::lround(v(fx::uTrigger)));
    hLine(g, r.getX() + 2, r.getBottom() - 3, w, pal::edgeMid);
    std::vector<juce::Point<int>> pts;
    for (int x = 0; x < w; ++x) {
        const double t = double(x) / double(std::max(1, w - 1));
        const double gain = mode == fx::pumpFollow ? 1.0 - depth * (1.0 - t) : 1.0 - depth * fx::pumpDuck(t, v(fx::uHold), v(fx::uShape));
        const int y = r.getBottom() - 3 - int(std::round(gain * double(h)));
        pts.push_back({r.getX() + 2 + x, y});
        if (on) { g.setColour(pal::acidDim.withAlpha(0.35f)); g.fillRect(r.getX() + 2 + x, y, 1, r.getBottom() - 3 - y); }
    }
    pixelPolyline(g, pts, col);
    // moving cursor: cycle demo (tempo), or restarts with each played note (note/sidechain)
    const double cyc = mode == fx::pumpTempo ? std::fmod(phase_ * 1.2 / std::max(0.25, v(fx::uBeats)), 1.0) : std::min(1.0, age_ * 1.2 / std::max(0.25, v(fx::uBeats)));
    const int cx = r.getX() + 2 + int(cyc * double(w - 1));
    if (on) vLine(g, cx, r.getY() + 2, r.getHeight() - 5, pal::acidHot);
    static const char* names[] = {"TEMPO", "NOTE", "SIDECHAIN", "FOLLOW"};
    drawText(g, names[std::clamp(mode, 0, 3)], r.getX() + 4, r.getY() + 3, pal::textMuted);
}

// -------------------------------------------------------------------- stutter (ZYG)
void FxViz::paintStutter(juce::Graphics& g, juce::Rectangle<int> r) {
    const bool on = m_.enabled;
    const auto col = dimmed(pal::crimson, on);
    const int w = r.getWidth() - 4, h = r.getHeight() - 8, cy = r.getCentreY();
    const int reps = 4, seg = std::max(4, w / reps);
    const double gate = v(fx::tGate) / 100.0, keep = 1.0 - v(fx::tFalloff) / 100.0 * 0.5;
    const bool rev = v(fx::tReverse) > 0.5;
    double amp = 1.0;
    for (int k = 0; k < reps; ++k) {
        for (int x = 0; x < seg; ++x) {
            const double t = double(x) / double(seg);
            if (t > gate) break;
            const double u = rev ? 1.0 - t : t;
            const double wave = std::sin(u * 9.0) * 0.5 + std::sin(u * 23.0 + 1.0) * 0.3 * (1.0 - u) + (hash01(unsigned(x * 7)) - 0.5) * 0.15;
            const int amph = int(std::round(std::abs(wave) * amp * double(h) * 0.5));
            const int px0 = r.getX() + 2 + k * seg + x;
            g.setColour(col.withAlpha(k == 0 ? 0.5f : 1.0f)); g.fillRect(px0, cy - amph, 1, std::max(1, 2 * amph));
        }
        if (k > 0) vLine(g, r.getX() + 2 + k * seg, r.getY() + 3, r.getHeight() - 6, pal::edgeMid);
        amp *= keep;
    }
    const double cyc = std::fmod(phase_ * 0.9, 1.0);
    if (on) vLine(g, r.getX() + 2 + int(cyc * double(reps * seg)), r.getY() + 2, r.getHeight() - 5, pal::amber);
    drawText(g, v(fx::tMode) > 0.5 ? "AUTO" : "GATE", r.getX() + 4, r.getY() + 3, pal::textMuted);
}
}
