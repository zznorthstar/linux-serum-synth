#pragma once
// Late-1990s hardware screen emulation for every display (oscilloscopes, wavetable, filter,
// envelope, LFO, spectrum, FX). A display draws its content into `canvas()` exactly as before;
// `present()` turns it into a low-resolution LCD or CRT/phosphor image:
//   - 2 px cells (blocky waveforms), per-channel colour quantisation (limited depth)
//   - persistence: slow LCD rise/fall or phosphor decay (ghosting / screen persistence)
//   - bloom from bright cells, faint per-frame noise, uneven backlight + vignette
//   - LCD pixel-grid gaps or CRT scanlines, and faintly visible unlit cells
// Text drawn while capturing is not pixelated: it is replayed crisp on top in the screen tint,
// like a character-LCD overlay, so labels stay readable.
#include "PixelGfx.h"
#include <optional>

namespace zyg::ui {

struct RetroStyle {
    enum class Kind { lcd, crt } kind = Kind::lcd;
    juce::Colour off;             // unlit screen / backlight colour
    juce::Colour tint;            // primary phosphor / segment colour
    juce::Colour secondary;       // second phosphor (two-tone screens: violet + acid green)
    float persistence = 0.10f;    // seconds for a lit cell to fade to ~37 %
    float rise = 0.55f;           // 0..1 fraction of the way a cell moves toward a brighter target per frame (LCD lag)
    float glow = 0.35f;           // bloom strength
    float noise = 0.035f;         // per-frame luminance noise
    int levels = 6;               // intensity levels per phosphor (limited colour depth)
    static RetroStyle greenLcd();  // oscillator / noise screens
    static RetroStyle envelopeLcd(); // envelope screen (minimal trails while dragging)
    static RetroStyle violetCrt(); // filter / LFO / mapping screens
    static RetroStyle crt(juce::Colour tint);   // effect scopes in their effect colour
};

class RetroScreen {
public:
    // Transparent-black canvas of the given size (reused between frames).
    juce::Image& canvas(int w, int h);
    // Processes the canvas and draws it at `where`, then replays captured text and the bezel.
    // Captured text is replayed at `textOrigin` + its canvas position (defaults to `where`'s origin).
    void present(juce::Graphics& g, juce::Rectangle<int> where, const RetroStyle& style,
                 std::optional<juce::Point<int>> textOrigin = {});
    void reset() { acc_.clear(); }

    // While alive, drawText() calls on this thread are recorded instead of drawn.
    class TextCapture {
    public:
        explicit TextCapture(RetroScreen& s);
        ~TextCapture();
    private:
        void* previous_;
    };
    struct Text { juce::String text; int x, y; juce::Colour colour; int scale; bool bold, small; };
    std::vector<Text> texts;
private:
    void drawOutput(juce::Graphics& g, juce::Rectangle<int> where, std::optional<juce::Point<int>> textOrigin);
    juce::Image canvas_, out_;
    std::uint64_t lastHash_ = 0;
    bool settled_ = false;
    std::vector<float> acc_;          // persistent low-res phosphor intensities (primary, secondary, white) per cell
    std::vector<float> glowBuf_, blurTmp_, illum_;
    int cw_ = 0, ch_ = 0;
    double lastTime_ = 0.0;
    std::uint32_t seed_ = 0x1234567u;
};

namespace retro {
// Called by drawText(): true when the text was captured by an active RetroScreen.
bool captureText(const juce::String& text, int x, int y, juce::Colour colour, int scale, bool bold, bool small);
// Global switch (MENU > RETRO SCREENS); off draws the displays directly.
bool enabled() noexcept;
void setEnabled(bool on) noexcept;
}

// Paints `draw` (the display's normal content code, in canvas coordinates) through `screen`.
template <class F>
void paintRetro(RetroScreen& screen, juce::Graphics& g, juce::Rectangle<int> r, const RetroStyle& style, F&& draw) {
    if (!retro::enabled()) { juce::Graphics::ScopedSaveState s(g); g.setOrigin(r.getPosition()); draw(g); return; }
    auto& cv = screen.canvas(r.getWidth(), r.getHeight());
    { juce::Graphics cg(cv); RetroScreen::TextCapture tc(screen); draw(cg); }
    screen.present(g, r, style);
}

}
