#pragma once
// ZYG-ZXG pixel-art drawing kit: palette, bitmap font, sprite icons and 1px
// bevel helpers. Everything here draws on whole-pixel coordinates with no
// anti-aliasing, so the editor stays crisp at any integer UI scale.
#include <JuceHeader.h>

namespace zyg::ui {

// ---------------------------------------------------------------- palette
// Values come from ui-design-kit/design-tokens.json; derived mid-tones are
// marked and only exist to make dithers and gradients between kit colours.
namespace pal {
inline const juce::Colour voidBg      {0xff08060d};
inline const juce::Colour chassis     {0xff100b1b};
inline const juce::Colour panel       {0xff181126};
inline const juce::Colour panelHi     {0xff1e1530};   // derived: panel .. raised
inline const juce::Colour raised      {0xff231838};
inline const juce::Colour raisedHi    {0xff2d2048};   // derived: raised .. neutral border
inline const juce::Colour sunken      {0xff0c0914};
inline const juce::Colour edgeDark    {0xff0a0710};
inline const juce::Colour edgeMid     {0xff31224d};
inline const juce::Colour edgeLight   {0xff4d3678};
inline const juce::Colour acidHot     {0xffd6ff85};
inline const juce::Colour acid        {0xff39ff14};
inline const juce::Colour acidDim     {0xff24a812};
inline const juce::Colour acidShadow  {0xff0f380a};
inline const juce::Colour acidWell    {0xff0b1c0c};   // derived: sunken tinted green
inline const juce::Colour violetHot   {0xffd84cff};
inline const juce::Colour violet      {0xff8a1fdf};
inline const juce::Colour violetDim   {0xff57148c};
inline const juce::Colour violetShadow{0xff2b0a45};
inline const juce::Colour violetWell  {0xff140a24};   // derived: sunken tinted violet
inline const juce::Colour textHi      {0xfff0fff2};
inline const juce::Colour textBody    {0xffa6b4a8};
inline const juce::Colour textMuted   {0xff59635a};
inline const juce::Colour lcd         {0xff63ff46};
inline const juce::Colour crimson     {0xffff2a55};
inline const juce::Colour crimsonDim  {0xff8a112a};
inline const juce::Colour amber       {0xffff9900};
inline const juce::Colour amberDim    {0xff7a4900};
}

// ------------------------------------------------------------------- text
// Two pixel faces. The regular face is a condensed 7px cap-height font used for
// labels, values and headers; `small` selects the 5px face for secondary marks
// (axis ticks, counters, badges). Lower case is folded to upper case.
constexpr int fontHeight = 7;
constexpr int smallFontHeight = 5;
int textWidth(const juce::String& text, int scale = 1, bool bold = false, bool small = false);
// Draws `text` with its top-left at (x, y). Cached per string/colour/scale/face.
void drawText(juce::Graphics& g, const juce::String& text, int x, int y, juce::Colour colour,
              int scale = 1, bool bold = false, bool small = false);
// Draws within `area`; `j` supports left/right/centre horizontally and is always
// vertically centred. Text wider than the area is shortened with "..".
void drawTextIn(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                juce::Colour colour, juce::Justification j = juce::Justification::centredLeft,
                int scale = 1, bool bold = false, bool small = false);
juce::String fitText(const juce::String& text, int maxWidth, int scale = 1, bool bold = false, bool small = false);
// Convenience for the small face.
inline void drawSmall(juce::Graphics& g, const juce::String& text, int x, int y, juce::Colour colour) {
    drawText(g, text, x, y, colour, 1, false, true);
}

// ---------------------------------------------------------------- sprites
enum class Icon {
    arrowLeft, arrowRight, arrowUp, arrowDown, caretDown, caretUp,
    disk, undo, redo, browser, power, gear, lock, unlock, search, close, plus,
    anchor, free, retrig, envelope, tripletNote, dottedNote, folder, keyboard, pencil,
    expand, collapse, bypass, fold, grip, record, play, stop, count
};
// Draws a monochrome icon; `accent` colours the '+' pixels of icons with two tones.
void drawIcon(juce::Graphics& g, Icon icon, int x, int y, juce::Colour colour,
              juce::Colour accent = juce::Colour());
juce::Point<int> iconSize(Icon icon);
void drawIconCentred(juce::Graphics& g, Icon icon, juce::Rectangle<int> area, juce::Colour colour,
                     juce::Colour accent = juce::Colour());

// ----------------------------------------------------------- primitives
inline void px(juce::Graphics& g, int x, int y, juce::Colour c) { g.setColour(c); g.fillRect(x, y, 1, 1); }
void hLine(juce::Graphics& g, int x, int y, int w, juce::Colour c);
void vLine(juce::Graphics& g, int x, int y, int h, juce::Colour c);
void frame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c);
// Raised 1px bevel (light top/left, dark bottom/right) over a fill.
void bevelBox(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill,
              juce::Colour light = pal::edgeLight, juce::Colour dark = pal::edgeDark);
// Sunken well: dark fill, dark top/left, mid bottom/right.
void wellBox(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill = pal::sunken);
// 2x2-checker dither between two colours across `r`.
void ditherFill(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour a, juce::Colour b, int phase = 0);
// Aliased line (Bresenham) as 1px rectangles.
void pixelLine(juce::Graphics& g, int x0, int y0, int x1, int y1, juce::Colour c);
// Pixel-perfect polyline through `pts`, drawn onto the graphics context.
void pixelPolyline(juce::Graphics& g, const std::vector<juce::Point<int>>& pts, juce::Colour c);
juce::Colour mix(juce::Colour a, juce::Colour b, float t);

// Filled disc / ring with per-pixel evaluation. Angles are radians clockwise from 12 o'clock.
void drawDisc(juce::Graphics& g, float cx, float cy, float radius, juce::Colour c);

// Image (ARGB) helpers used when building cached sprites.
void setPx(juce::Image& img, int x, int y, juce::Colour c);

// Draws an image with nearest-neighbour sampling.
void blit(juce::Graphics& g, const juce::Image& img, int x, int y);

// ----------------------------------------------------------------- knobs
struct KnobStyle {
    juce::Colour arc = pal::acid;        // value arc
    juce::Colour modArc = pal::violetHot;// modulation arc
    juce::Colour pointer = pal::acidHot;
    bool bipolar = false;                // arc grows from the centre
    bool dim = false;                    // disabled look
};
// Returns a cached pixel knob of `diameter` px. `t` is the normalised value
// 0..1; `modLo..modHi` (normalised) draw a modulation arc when modHi > modLo.
const juce::Image& knobImage(int diameter, float t, const KnobStyle& style,
                             float modLo = 0.0f, float modHi = 0.0f);

// 8-dot pixel spinner; `phase` in turns (e.g. seconds * 1.2).
void drawSpinner(juce::Graphics& g, int cx, int cy, int radius, juce::Colour colour, double phase);

// Wave icons for the SUB shape grid, drawn procedurally at `r`.
enum class WaveIcon { sine, rounded, saw, square, triangle, pulse, noise };
void drawWaveIcon(juce::Graphics& g, WaveIcon w, juce::Rectangle<int> r, juce::Colour c);

}
