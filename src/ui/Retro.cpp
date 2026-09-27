#include "Retro.h"
#include <cmath>

namespace zyg::ui {
namespace {
RetroScreen* g_capture = nullptr;   // UI painting is single-threaded (message thread or GL thread under the MM lock)
constexpr int kCell = 2;            // one screen cell = 2 x 2 logical pixels
}

RetroStyle RetroStyle::greenLcd() {
    RetroStyle s;
    s.kind = Kind::lcd;
    s.off = juce::Colour(0xff0a170b);
    s.tint = pal::acid; s.secondary = pal::violetHot;
    s.persistence = 0.07f; s.rise = 0.72f; s.glow = 0.28f; s.noise = 0.03f; s.levels = 6;
    return s;
}
RetroStyle RetroStyle::envelopeLcd() {
    // The envelope is dragged constantly: keep only a hint of LCD lag so handles don't smear.
    RetroStyle s = greenLcd();
    s.persistence = 0.025f; s.rise = 0.92f;
    return s;
}
RetroStyle RetroStyle::violetCrt() {
    RetroStyle s;
    s.kind = Kind::crt;
    s.off = juce::Colour(0xff0d0717);
    s.tint = pal::violetHot; s.secondary = pal::acid;
    s.persistence = 0.055f; s.rise = 1.0f; s.glow = 0.45f; s.noise = 0.04f; s.levels = 6;
    return s;
}
RetroStyle RetroStyle::crt(juce::Colour tint) {
    RetroStyle s = violetCrt();
    s.tint = tint; s.secondary = tint == pal::acid || tint == pal::acidHot ? pal::violetHot : pal::acid;
    s.off = mix(juce::Colour(0xff08060d), tint, 0.06f);
    return s;
}

juce::Image& RetroScreen::canvas(int w, int h) {
    w = std::max(2, w); h = std::max(2, h);
    if (!canvas_.isValid() || canvas_.getWidth() != w || canvas_.getHeight() != h)
        canvas_ = juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    else canvas_.clear(canvas_.getBounds());
    texts.clear();
    return canvas_;
}

RetroScreen::TextCapture::TextCapture(RetroScreen& s) : previous_(g_capture) { g_capture = &s; }
RetroScreen::TextCapture::~TextCapture() { g_capture = static_cast<RetroScreen*>(previous_); }

bool retro::captureText(const juce::String& text, int x, int y, juce::Colour colour, int scale, bool bold, bool small) {
    if (!g_capture) return false;
    g_capture->texts.push_back({text, x, y, colour, scale, bold, small});
    return true;
}

namespace { bool g_enabled = true; }
bool retro::enabled() noexcept { return g_enabled; }
void retro::setEnabled(bool on) noexcept { g_enabled = on; }

void RetroScreen::present(juce::Graphics& g, juce::Rectangle<int> where, const RetroStyle& st, std::optional<juce::Point<int>> textOrigin) {
    if (!canvas_.isValid()) return;
    const int w = canvas_.getWidth(), h = canvas_.getHeight();
    const int cw = (w + kCell - 1) / kCell, ch = (h + kCell - 1) / kCell;
    bool first = false;
    if (cw != cw_ || ch != ch_) {
        cw_ = cw; ch_ = ch; first = true;
        acc_.assign(std::size_t(cw * ch * 3), 0.0f); glowBuf_.assign(acc_.size(), 0.0f); blurTmp_.assign(acc_.size(), 0.0f);
        illum_.resize(std::size_t(cw * ch));   // uneven backlight: hotspot upper-left of centre, darker corners
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx) {
                const float u = (float(cx) + 0.5f) / float(cw), v = (float(cy) + 0.5f) / float(ch);
                const float hx = u - 0.38f, hy = v - 0.32f, cxv = u - 0.5f, cyv = v - 0.5f;
                illum_[std::size_t(cy * cw + cx)] = (0.84f + 0.2f * std::exp(-(hx * hx + hy * hy) * 5.0f))
                                                  * (1.0f - 0.55f * std::max(0.0f, cxv * cxv + cyv * cyv - 0.08f));
            }
    }
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = lastTime_ > 0.0 ? std::clamp(now - lastTime_, 0.0, 0.25) : 1.0;
    lastTime_ = now;
    const float decay = float(std::exp(-dt / std::max(0.01, double(st.persistence))));
    const float rise = first ? 1.0f : float(1.0 - std::pow(1.0 - double(st.rise), std::max(0.0, dt * 60.0)));
    const float L = float(std::max(2, st.levels) - 1);

    // Static screen: identical content and a settled persistence buffer reuse the last image, so
    // idle displays keep their full frame rate at almost no cost.
    std::uint64_t hash = 1469598103934665603ull ^ std::uint64_t(st.kind == RetroStyle::Kind::lcd) ^ (std::uint64_t(st.tint.getARGB()) << 8);
    {
        const juce::Image::BitmapData src(canvas_, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(src.getLinePointer(y));
            for (int x = 0; x < w; ++x) { hash ^= row[x]; hash *= 1099511628211ull; }
        }
    }
    const bool reuse = !first && settled_ && hash == lastHash_ && out_.isValid() && out_.getWidth() == w && out_.getHeight() == h;
    lastHash_ = hash;
    if (reuse) { drawOutput(g, where, textOrigin); return; }
    bool changed = false;

    // Phosphor model: each cell is expressed as primary P + secondary S + white W intensities
    // (non-negative least squares on the two phosphor colours, white takes the remainder).
    const float P[3] = {st.tint.getFloatRed(), st.tint.getFloatGreen(), st.tint.getFloatBlue()};
    const float S[3] = {st.secondary.getFloatRed(), st.secondary.getFloatGreen(), st.secondary.getFloatBlue()};
    const float PP = P[0] * P[0] + P[1] * P[1] + P[2] * P[2], SS = S[0] * S[0] + S[1] * S[1] + S[2] * S[2];
    const float PS = P[0] * S[0] + P[1] * S[1] + P[2] * S[2], det = std::max(1.0e-6f, PP * SS - PS * PS);
    // 1) downsample (max keeps 1 px lines), 2) decompose + quantise (floor-biased so dark
    // backgrounds stay unlit), 3) persistence (LCD lag on rise, decay on fall)
    {
        const juce::Image::BitmapData src(canvas_, juce::Image::BitmapData::readOnly);
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx) {
                int mr = 0, mg = 0, mb = 0;
                for (int dy = 0; dy < kCell; ++dy) {
                    const int y = cy * kCell + dy; if (y >= h) break;
                    const auto* line = src.getLinePointer(y);
                    for (int dx = 0; dx < kCell; ++dx) {
                        const int x = cx * kCell + dx; if (x >= w) break;
                        const auto* p = reinterpret_cast<const juce::PixelARGB*>(line + x * src.pixelStride);
                        mr = std::max(mr, int(p->getRed())); mg = std::max(mg, int(p->getGreen())); mb = std::max(mb, int(p->getBlue()));
                    }
                }
                const float m[3] = {float(mr) / 255.0f, float(mg) / 255.0f, float(mb) / 255.0f};   // premultiplied: transparent reads as black
                const float cP = m[0] * P[0] + m[1] * P[1] + m[2] * P[2], cS = m[0] * S[0] + m[1] * S[1] + m[2] * S[2];
                float ia = (cP * SS - cS * PS) / det, ib = (cS * PP - cP * PS) / det;
                if (ia < 0.0f) { ia = 0.0f; ib = cS / std::max(1.0e-6f, SS); }
                if (ib < 0.0f) { ib = 0.0f; ia = cP / std::max(1.0e-6f, PP); }
                ia = std::clamp(ia, 0.0f, 1.0f); ib = std::clamp(ib, 0.0f, 1.0f);
                float iw = 1.0f;
                for (int k = 0; k < 3; ++k) iw = std::min(iw, m[k] - ia * P[k] - ib * S[k]);
                // black level: the displays' dark well/background colours must read as unlit screen
                auto black = [](float v) { return std::max(0.0f, (v - 0.10f) / 0.90f); };
                const float target[3] = {black(ia), black(ib), black(std::max(0.0f, iw))};
                float* a = &acc_[std::size_t((cy * cw + cx) * 3)];
                for (int k = 0; k < 3; ++k) {
                    // 4x4 ordered dither between levels; threshold kept in 0.05..0.55 so dark backgrounds stay unlit
                    static constexpr float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
                    const float t = 0.05f + 0.5f * bayer[(cy & 3) * 4 + (cx & 3)] / 16.0f;
                    const float q = std::min(1.0f, std::floor(target[k] * L + t) / L);
                    const float before = a[k];
                    a[k] = q > a[k] ? a[k] + (q - a[k]) * rise : std::max(q, a[k] * decay);
                    if (a[k] < 1.0e-3f) a[k] = 0.0f;
                    changed = changed || std::abs(a[k] - before) > 1.0e-3f;
                }
            }
    }
    // 4) bloom: separable 3-tap box blur of the persistent image
    if (st.glow > 0.0f) {
        std::vector<float>& gb = glowBuf_;
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx)
                for (int k = 0; k < 3; ++k) {
                    float s = 0; int n = 0;
                    for (int dx = -2; dx <= 2; ++dx) { const int x = cx + dx; if (x < 0 || x >= cw) continue; s += acc_[std::size_t((cy * cw + x) * 3 + k)]; ++n; }
                    gb[std::size_t((cy * cw + cx) * 3 + k)] = s / float(n);
                }
        std::copy(gb.begin(), gb.end(), blurTmp_.begin());
        const std::vector<float>& tmp = blurTmp_;
        for (int cy = 0; cy < ch; ++cy)
            for (int cx = 0; cx < cw; ++cx)
                for (int k = 0; k < 3; ++k) {
                    float s = 0; int n = 0;
                    for (int dy = -2; dy <= 2; ++dy) { const int y = cy + dy; if (y < 0 || y >= ch) continue; s += tmp[std::size_t((y * cw + cx) * 3 + k)]; ++n; }
                    gb[std::size_t((cy * cw + cx) * 3 + k)] = s / float(n);
                }
    }
    // 5-7) compose cells: unlit base, uneven illumination, noise, glow, then grid / scanlines
    if (!out_.isValid() || out_.getWidth() != w || out_.getHeight() != h)
        out_ = juce::Image(juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    const float off[3] = {st.off.getFloatRed(), st.off.getFloatGreen(), st.off.getFloatBlue()};
    const float* tint = P;
    const bool lcd = st.kind == RetroStyle::Kind::lcd;
    {
        juce::Image::BitmapData dst(out_, juce::Image::BitmapData::writeOnly);
        float base[3], gap[3];
        for (int k = 0; k < 3; ++k) {
            base[k] = off[k] * (lcd ? 1.35f : 1.15f) + tint[k] * 0.025f;   // unlit cells stay faintly visible
            gap[k] = off[k] * 0.9f;                                         // LCD gaps show the backplate, not black
        }
        for (int cy = 0; cy < ch; ++cy) {
            for (int cx = 0; cx < cw; ++cx) {
                const float ill = illum_[std::size_t(cy * cw + cx)];
                seed_ = seed_ * 1664525u + 1013904223u;
                const float n = 1.0f + (float(seed_ >> 8) / float(1 << 24) - 0.5f) * 2.0f * st.noise;
                const float* a = &acc_[std::size_t((cy * cw + cx) * 3)];
                const float* gl = &glowBuf_[std::size_t((cy * cw + cx) * 3)];
                const float glowK = st.glow * (0.6f + 0.4f * std::max({a[0], a[1], a[2]}));
                juce::uint8 lit[3], dim[3];
                for (int k = 0; k < 3; ++k) {
                    const float emit = a[0] * P[k] + a[1] * S[k] + a[2];
                    const float bloom = gl[0] * P[k] + gl[1] * S[k] + gl[2];
                    const float c = std::clamp(base[k] * ill + emit * ill * n + bloom * glowK, 0.0f, 1.0f);
                    const float d = lcd ? c * 0.62f + gap[k] * 0.38f : c * 0.48f;   // LCD grid gap / CRT scanline
                    lit[k] = juce::uint8(c * 255.0f); dim[k] = juce::uint8(std::min(1.0f, d) * 255.0f);
                }
                for (int dy = 0; dy < kCell; ++dy) {
                    const int y = cy * kCell + dy; if (y >= h) break;
                    auto* line = dst.getLinePointer(y);
                    for (int dx = 0; dx < kCell; ++dx) {
                        const int x = cx * kCell + dx; if (x >= w) break;
                        const bool gapPx = lcd ? (dx == kCell - 1 || dy == kCell - 1) : dy == kCell - 1;
                        const auto* c = gapPx ? dim : lit;
                        reinterpret_cast<juce::PixelARGB*>(line + x * dst.pixelStride)->setARGB(255, c[0], c[1], c[2]);
                    }
                }
            }
        }
    }
    settled_ = !changed;
    drawOutput(g, where, textOrigin);
}

void RetroScreen::drawOutput(juce::Graphics& g, juce::Rectangle<int> where, std::optional<juce::Point<int>> textOrigin) {
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.drawImageAt(out_, where.getX(), where.getY());
    // glass: faint reflection band and a recessed bezel
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.05f), float(where.getX()), float(where.getY()),
                                           juce::Colours::white.withAlpha(0.0f), float(where.getX()) + float(where.getWidth()) * 0.25f,
                                           float(where.getY()) + float(where.getHeight()) * 0.45f, false));
    g.fillRect(where.reduced(2));
    hLine(g, where.getX(), where.getY(), where.getWidth(), pal::edgeDark);
    vLine(g, where.getX(), where.getY(), where.getHeight(), pal::edgeDark);
    hLine(g, where.getX(), where.getBottom() - 1, where.getWidth(), pal::edgeMid);
    vLine(g, where.getRight() - 1, where.getY(), where.getHeight(), pal::edgeMid);
    hLine(g, where.getX() + 1, where.getY() + 1, where.getWidth() - 2, juce::Colours::black.withAlpha(0.45f));
    // crisp text overlay with a faint phosphor halo
    for (const auto& t : texts) {
        const auto o = textOrigin.value_or(where.getPosition());
        const int x = o.x + t.x, y = o.y + t.y;
        const auto halo = t.colour.withAlpha(0.22f);
        drawText(g, t.text, x - 1, y, halo, t.scale, t.bold, t.small);
        drawText(g, t.text, x + 1, y, halo, t.scale, t.bold, t.small);
        drawText(g, t.text, x, y, t.colour, t.scale, t.bold, t.small);
    }
}

}
