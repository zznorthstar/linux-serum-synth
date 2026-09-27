#include "PixelGfx.h"
#include "Retro.h"
#include <cmath>
#include <unordered_map>

namespace zyg::ui {
namespace {

// ------------------------------------------------------------- font data
struct GlyphSrc { char c; const char* rows; };
// Primary face: condensed 7px cap height (most glyphs 4px wide so the advance
// matches the old 5px face and existing layouts keep their widths).
// Rows separated by '/', '#' = ink. Width is the row length.
const GlyphSrc glyphSrc7[] = {
    {'A', ".##./#..#/#..#/#..#/####/#..#/#..#"}, {'B', "###./#..#/#..#/###./#..#/#..#/###."},
    {'C', ".##./#..#/#.../#.../#.../#..#/.##."}, {'D', "###./#..#/#..#/#..#/#..#/#..#/###."},
    {'E', "####/#.../#.../###./#.../#.../####"}, {'F', "####/#.../#.../###./#.../#.../#..."},
    {'G', ".##./#..#/#.../#.##/#..#/#..#/.###"}, {'H', "#..#/#..#/#..#/####/#..#/#..#/#..#"},
    {'I', "###/.#./.#./.#./.#./.#./###"},     {'J', "..##/...#/...#/...#/...#/#..#/.##."},
    {'K', "#..#/#..#/#.#./##../#.#./#..#/#..#"}, {'L', "#.../#.../#.../#.../#.../#.../####"},
    {'M', "#...#/##.##/#.#.#/#.#.#/#...#/#...#/#...#"}, {'N', "#..#/#..#/##.#/#.##/#..#/#..#/#..#"},
    {'O', ".###./#...#/#...#/#...#/#...#/#...#/.###."}, {'P', "###./#..#/#..#/###./#.../#.../#..."},
    {'Q', ".##./#..#/#..#/#..#/#..#/#.#./.#.#"}, {'R', "###./#..#/#..#/###./#.#./#..#/#..#"},
    {'S', ".##./#..#/#.../.##./...#/#..#/.##."}, {'T', "#####/..#../..#../..#../..#../..#../..#.."},
    {'U', "#..#/#..#/#..#/#..#/#..#/#..#/.##."}, {'V', "#...#/#...#/#...#/#...#/.#.#./.#.#./..#.."},
    {'W', "#...#/#...#/#...#/#.#.#/#.#.#/##.##/#...#"}, {'X', "#...#/#...#/.#.#./..#../.#.#./#...#/#...#"},
    {'Y', "#...#/#...#/.#.#./..#../..#../..#../..#.."}, {'Z', "####/...#/...#/..#./.#../#.../####"},
    {'0', ".##./#..#/#.##/#..#/##.#/#..#/.##."}, {'1', ".#./##./.#./.#./.#./.#./###"},
    {'2', ".##./#..#/...#/..#./.#../#.../####"}, {'3', ".##./#..#/...#/.##./...#/#..#/.##."},
    {'4', "#..#/#..#/#..#/####/...#/...#/...#"}, {'5', "####/#.../###./...#/...#/#..#/.##."},
    {'6', ".##./#.../#.../###./#..#/#..#/.##."}, {'7', "####/...#/...#/..#./.#../.#../.#.."},
    {'8', ".##./#..#/#..#/.##./#..#/#..#/.##."}, {'9', ".##./#..#/#..#/.###/...#/...#/.##."},
    {'.', "././././././#"},                  {',', "../../../../.#/.#/#."},
    {':', "././#/././#/."},                    {';', "../../.#/../../.#/#."},
    {'-', ".../.../.../###/.../.../..."},      {'+', "...../...../..#../.###./..#../...../....."},
    {'/', "..#/..#/.#./.#./.#./#../#.."},      {'\\', "#../#../.#./.#./.#./..#/..#"},
    {'%', "##..#/##..#/...#./..#../.#.../#..##/#..##"},
    {'(', ".#/#./#./#./#./#./.#"},             {')', "#./.#/.#/.#/.#/.#/#."},
    {'[', "##/#./#./#./#./#./##"},             {']', "##/.#/.#/.#/.#/.#/##"},
    {'<', "...#/..#./.#../#.../.#../..#./...#"}, {'>', "#.../.#../..#./...#/..#./.#../#..."},
    {'=', "..../..../####/..../####/..../...."}, {'#', ".#.#./.#.#./#####/.#.#./#####/.#.#./.#.#."},
    {'*', "...../#.#.#/.###./#####/.###./#.#.#/....."}, {'_', "..../..../..../..../..../..../####"},
    {'?', ".##./#..#/...#/..#./.#../..../.#.."}, {'!', "#/#/#/#/#/./#"},
    {'\'', "#/#/././././."},                 {'"', "#.#/#.#/.../.../.../.../..."},
    {'&', ".##../#..#./#.#../.#.../#.#.#/#..#./.##.#"}, {'|', "#/#/#/#/#/#/#"},
    {'~', "..../..../.#.#/#.#./..../..../...."}, {'^', ".#./#.#/.../.../.../.../..."},
    {'$', "..#../.####/#.#../.###./..#.#/####./..#.."}, {'@', ".###./#...#/#.###/#.#.#/#.###/#..../.###."},
    {'`', "#./.#/../../../../.."},              {'{', "..#/.#./.#./#../.#./.#./..#"},
    {'}', "#../.#./.#./..#/.#./.#./#.."},       {char(0xb0), ".#./#.#/.#./.../.../.../..."},
};
// Small face: the original 5px cap-height font, kept for axis ticks, counters and badges.
const GlyphSrc glyphSrc5[] = {
    {'A', ".##./#..#/####/#..#/#..#"}, {'B', "###./#..#/###./#..#/###."},
    {'C', ".###/#.../#.../#.../.###"}, {'D', "###./#..#/#..#/#..#/###."},
    {'E', "####/#.../###./#.../####"}, {'F', "####/#.../###./#.../#..."},
    {'G', ".###/#.../#.##/#..#/.###"}, {'H', "#..#/#..#/####/#..#/#..#"},
    {'I', "###/.#./.#./.#./###"},       {'J', "..##/...#/...#/#..#/.##."},
    {'K', "#..#/#.#./##../#.#./#..#"}, {'L', "#.../#.../#.../#.../####"},
    {'M', "#...#/##.##/#.#.#/#...#/#...#"}, {'N', "#..#/##.#/#.##/#..#/#..#"},
    {'O', ".##./#..#/#..#/#..#/.##."}, {'P', "###./#..#/###./#.../#..."},
    {'Q', ".##./#..#/#..#/#.##/.###"}, {'R', "###./#..#/###./#.#./#..#"},
    {'S', ".###/#.../.##./...#/###."}, {'T', "###/.#./.#./.#./.#."},
    {'U', "#..#/#..#/#..#/#..#/.##."}, {'V', "#...#/#...#/.#.#./.#.#./..#.."},
    {'W', "#...#/#...#/#.#.#/##.##/#...#"}, {'X', "#...#/.#.#./..#../.#.#./#...#"},
    {'Y', "#...#/.#.#./..#../..#../..#.."}, {'Z', "####/...#/.##./#.../####"},
    {'0', "###/#.#/#.#/#.#/###"},      {'1', ".#./##./.#./.#./###"},
    {'2', ".##./#..#/..#./.#../####"}, {'3', "###./...#/.##./...#/###."},
    {'4', "#..#/#..#/####/...#/...#"}, {'5', "####/#.../###./...#/###."},
    {'6', ".###/#.../###./#..#/.##."}, {'7', "####/...#/..#./.#../.#.."},
    {'8', ".##./#..#/.##./#..#/.##."}, {'9', ".##./#..#/.###/...#/###."},
    {'.', "././././#"},              {',', "../../../.#/#."},
    {':', "./#/./#/."},                {';', "../.#/../.#/#."},
    {'-', ".../.../###/.../..."},      {'+', ".../.#./###/.#./..."},
    {'/', "..#/..#/.#./#../#.."},      {'\\', "#../#../.#./..#/..#"},
    {'%', "#...#/...#./..#../.#.../#...#"},
    {'(', ".#/#./#./#./.#"},           {')', "#./.#/.#/.#/#."},
    {'[', "##/#./#./#./##"},           {']', "##/.#/.#/.#/##"},
    {'<', "..#/.#./#../.#./..#"},      {'>', "#../.#./..#/.#./#.."},
    {'=', ".../###/.../###/..."},      {'#', ".#.#./#####/.#.#./#####/.#.#."},
    {'*', "#.#/.#./###/.#./#.#"},      {'_', "..../..../..../..../####"},
    {'?', ".##./#..#/..#./..../..#."}, {'!', "#/#/#/./#"},
    {'\'', "#/#/././."},               {'"', "#.#/#.#/.../.../..."},
    {'&', ".##./#..#/.##./#.##/.###"}, {'|', "#/#/#/#/#"},
    {'~', "..../.#.#/#.#./..../...."}, {'^', ".#./#.#/.../.../..."},
    {'$', ".###/#.#./.###/.#.#/###."}, {'@', ".###./#...#/#.###/#.#.#/.###."},
    {'`', "#../.#./.../.../..."},       {'{', ".##/.#./##./.#./.##"},
    {'}', "##./.#./.##/.#./##."},      {char(0xb0), ".#./#.#/.#./.../..."},
};

constexpr int kMaxRows = 7;
struct Glyph { int w = 0; std::uint8_t rows[kMaxRows] {}; bool valid = false; };
using GlyphTable = std::array<Glyph, 256>;

template <std::size_t N>
GlyphTable buildTable(const GlyphSrc (&src)[N], int rowsPerGlyph) {
    GlyphTable t {};
    for (const auto& s : src) {
        Glyph g; g.valid = true;
        int row = 0, col = 0, width = 0;
        for (const char* p = s.rows; ; ++p) {
            if (*p == '/' || *p == '\0') {
                width = std::max(width, col);
                if (*p == '\0' || ++row >= rowsPerGlyph) break;
                col = 0;
            } else {
                if (*p == '#') g.rows[row] = std::uint8_t(g.rows[row] | (1u << (7 - col)));
                ++col;
            }
        }
        g.w = width;
        t[std::size_t(std::uint8_t(s.c))] = g;
    }
    Glyph space; space.w = 3; space.valid = true; t[' '] = space;
    return t;
}

const GlyphTable& glyphTable(bool small) {
    static const GlyphTable regular = buildTable(glyphSrc7, fontHeight);
    static const GlyphTable tiny = buildTable(glyphSrc5, smallFontHeight);
    return small ? tiny : regular;
}

const Glyph* lookup(juce::juce_wchar ch, bool small) {
    if (ch >= 'a' && ch <= 'z') ch = juce::juce_wchar(ch - 32);
    if (ch == 0x00b0) ch = juce::juce_wchar(0xb0);
    if (ch > 255) return nullptr;
    const auto& g = glyphTable(small)[std::size_t(ch)];
    return g.valid ? &g : nullptr;
}

int advance(const Glyph& g, bool bold) { return g.w + 1 + (bold ? 1 : 0); }

// ------------------------------------------------------------- caches
std::unordered_map<std::string, juce::Image>& textCache() {
    static std::unordered_map<std::string, juce::Image> cache;
    return cache;
}

juce::Image renderText(const juce::String& text, juce::Colour colour, int scale, bool bold, bool small) {
    const int rows = small ? smallFontHeight : fontHeight;
    const int w = std::max(1, textWidth(text, 1, bold, small));
    juce::Image img(juce::Image::ARGB, w * scale, rows * scale, true);
    int x = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        const Glyph* g = lookup(*it, small);
        if (!g) g = lookup('?', small);
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < g->w; ++col)
                if (g->rows[row] & (1u << (7 - col)))
                    for (int b = 0; b <= (bold ? 1 : 0); ++b)
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx)
                                img.setPixelAt((x + col + b) * scale + sx, row * scale + sy, colour);
        x += advance(*g, bold);
    }
    return img;
}

// ------------------------------------------------------------- icons
struct IconSrc { Icon icon; const char* rows; };
const IconSrc iconSrc[] = {
    {Icon::arrowLeft,  "..#/.#./#../.#./..#"},
    {Icon::arrowRight, "#../.#./..#/.#./#.."},
    {Icon::arrowUp,    "..#../.#.#./#...#"},
    {Icon::arrowDown,  "#...#/.#.#./..#.."},
    {Icon::caretDown,  "#####/.###./..#.."},
    {Icon::caretUp,    "..#../.###./#####"},
    {Icon::disk,       "########./#.####.##/#.####.##/#.####..#/#.......#/#.#####.#/#.#...#.#/#.#...#.#/#########"},
    {Icon::undo,       "..#....../.##.####./#####..#./.##.#...#/..#.....#./.......#../..####..."},
    {Icon::redo,       "......#../.####.##./.#..#####/#...#.##./.#.....#./..#....../...####.."},
    {Icon::browser,    "#########/........./#.#######/........./#.#######/........./#.#######"},
    {Icon::power,      "...#.../.#.#.#./#..#..#/#.....#/#.....#/.#...#./..###.."},
    {Icon::gear,       "...#.../.#.#.#./..###../###.###/..###../.#.#.#./...#..."},
    {Icon::lock,       ".###./#...#/#...#/#####/##.##/#####/#####"},
    {Icon::unlock,     ".###./#..../#..../#####/##.##/#####/#####"},
    {Icon::search,     ".###.../#...#../#...#../#...#../.###.../....##./.....##"},
    {Icon::close,      "#...#/.#.#./..#../.#.#./#...#"},
    {Icon::plus,       "..#../..#../#####/..#../..#.."},
    {Icon::anchor,     "...#.../..###../...#.../#..#..#/#..#..#/.#.#.#./..###.."},
    {Icon::free,       ".#####./#.....#/#.....#/#.....#/.#####."},
    {Icon::retrig,     ".####./#....#/#...##/#..#.#/.###.."},
    {Icon::envelope,   "...#..#/....#.#/#######/....#.#/...#..#"},
    {Icon::tripletNote,".####/.#..#/.#..#/.#.../##.../##..."},
    {Icon::dottedNote, ".####/.#..#/.#.../.#.../##..#/##..."},
    {Icon::folder,     "###..../#######/#.....#/#.....#/#.....#/#######"},
    {Icon::keyboard,   "#########/##.#.#.##/##.#.#.##/##.#.#.##/#.#.#.#.#/#.#.#.#.#/#########"},
    {Icon::pencil,     ".....#./....#.#/...#.#./..#.#../.#.#.../##...../#......"},
    {Icon::expand,     "##...##/#.....#/......./......./......./#.....#/##...##"},
    {Icon::collapse,   "......./.##.##./.#...#./......./.#...#./.##.##./......."},
    {Icon::bypass,     "..#../#.#.#/#.#.#/#...#/.###."},
    {Icon::fold,       "#.#.#/.#.#./#.#.#"},
    {Icon::grip,       "#.#/#.#/#.#/#.#/#.#"},
    {Icon::record,     ".###./#####/#####/#####/.###."},
    {Icon::play,       "#..../##.../###../##.../#...."},
    {Icon::stop,       "#####/#####/#####/#####/#####"},
};

struct IconBitmap { int w = 0, h = 0; std::vector<std::string> rows; };
const IconBitmap& iconBitmap(Icon icon) {
    static std::array<IconBitmap, std::size_t(Icon::count)> table = [] {
        std::array<IconBitmap, std::size_t(Icon::count)> t {};
        for (const auto& s : iconSrc) {
            IconBitmap b; std::string row;
            for (const char* p = s.rows; ; ++p) {
                if (*p == '/' || *p == '\0') {
                    b.w = std::max<int>(b.w, int(row.size())); b.rows.push_back(row); row.clear();
                    if (*p == '\0') break;
                } else row.push_back(*p);
            }
            b.h = int(b.rows.size());
            t[std::size_t(s.icon)] = std::move(b);
        }
        return t;
    }();
    return table[std::size_t(icon)];
}

// ------------------------------------------------------------- knobs
std::unordered_map<std::string, juce::Image>& knobCache() {
    static std::unordered_map<std::string, juce::Image> cache;
    return cache;
}
constexpr float kPi = 3.14159265358979f;
}

int textWidth(const juce::String& text, int scale, bool bold, bool small) {
    int w = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        const Glyph* g = lookup(*it, small);
        if (!g) g = lookup('?', small);
        w += advance(*g, bold);
    }
    return std::max(0, w - 1) * scale;
}

void drawText(juce::Graphics& g, const juce::String& text, int x, int y, juce::Colour colour,
              int scale, bool bold, bool small) {
    if (text.isEmpty()) return;
    if (retro::captureText(text, x, y, colour, scale, bold, small)) return;   // replayed crisp over a retro screen
    auto& cache = textCache();
    if (cache.size() > 6000) cache.clear();
    const std::string key = text.toStdString() + "|" + std::to_string(colour.getARGB()) + "|" +
        std::to_string(scale) + (bold ? "b" : "n") + (small ? "s" : "r");
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, renderText(text, colour, scale, bold, small)).first;
    blit(g, it->second, x, y);
}

juce::String fitText(const juce::String& text, int maxWidth, int scale, bool bold, bool small) {
    if (textWidth(text, scale, bold, small) <= maxWidth) return text;
    juce::String s = text;
    while (s.length() > 1 && textWidth(s + "..", scale, bold, small) > maxWidth) s = s.dropLastCharacters(1);
    return s + "..";
}

void drawTextIn(juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                juce::Colour colour, juce::Justification j, int scale, bool bold, bool small) {
    const auto s = fitText(text, area.getWidth(), scale, bold, small);
    const int w = textWidth(s, scale, bold, small);
    int x = area.getX();
    if (j.testFlags(juce::Justification::horizontallyCentred)) x = area.getX() + (area.getWidth() - w) / 2;
    else if (j.testFlags(juce::Justification::right)) x = area.getRight() - w;
    const int y = area.getY() + (area.getHeight() - (small ? smallFontHeight : fontHeight) * scale) / 2;
    drawText(g, s, x, y, colour, scale, bold, small);
}

juce::Point<int> iconSize(Icon icon) { const auto& b = iconBitmap(icon); return {b.w, b.h}; }

void drawIcon(juce::Graphics& g, Icon icon, int x, int y, juce::Colour colour, juce::Colour accent) {
    const auto& b = iconBitmap(icon);
    for (int row = 0; row < b.h; ++row) {
        const auto& s = b.rows[std::size_t(row)];
        for (int col = 0; col < int(s.size()); ++col) {
            if (s[std::size_t(col)] == '#') px(g, x + col, y + row, colour);
            else if (s[std::size_t(col)] == '+') px(g, x + col, y + row, accent.isTransparent() ? colour : accent);
        }
    }
}

void drawIconCentred(juce::Graphics& g, Icon icon, juce::Rectangle<int> area, juce::Colour colour,
                     juce::Colour accent) {
    const auto sz = iconSize(icon);
    drawIcon(g, icon, area.getX() + (area.getWidth() - sz.x) / 2, area.getY() + (area.getHeight() - sz.y) / 2,
             colour, accent);
}

void hLine(juce::Graphics& g, int x, int y, int w, juce::Colour c) { g.setColour(c); g.fillRect(x, y, w, 1); }
void vLine(juce::Graphics& g, int x, int y, int h, juce::Colour c) { g.setColour(c); g.fillRect(x, y, 1, h); }
void frame(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c) {
    g.setColour(c);
    g.fillRect(r.getX(), r.getY(), r.getWidth(), 1);
    g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    g.fillRect(r.getX(), r.getY(), 1, r.getHeight());
    g.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight());
}

void bevelBox(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, juce::Colour light, juce::Colour dark) {
    if (r.isEmpty()) return;
    g.setColour(fill); g.fillRect(r);
    hLine(g, r.getX(), r.getY(), r.getWidth(), light);
    vLine(g, r.getX(), r.getY(), r.getHeight(), light);
    hLine(g, r.getX(), r.getBottom() - 1, r.getWidth(), dark);
    vLine(g, r.getRight() - 1, r.getY(), r.getHeight(), dark);
}

void wellBox(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill) {
    if (r.isEmpty()) return;
    g.setColour(fill); g.fillRect(r);
    hLine(g, r.getX(), r.getY(), r.getWidth(), pal::edgeDark);
    vLine(g, r.getX(), r.getY(), r.getHeight(), pal::edgeDark);
    hLine(g, r.getX(), r.getBottom() - 1, r.getWidth(), pal::edgeMid);
    vLine(g, r.getRight() - 1, r.getY(), r.getHeight(), pal::edgeMid);
}

void ditherFill(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour a, juce::Colour b, int phase) {
    g.setColour(a); g.fillRect(r);
    g.setColour(b);
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX() + ((y + phase) & 1); x < r.getRight(); x += 2) g.fillRect(x, y, 1, 1);
}

void pixelLine(juce::Graphics& g, int x0, int y0, int x1, int y1, juce::Colour c) {
    g.setColour(c);
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        g.fillRect(x0, y0, 1, 1);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void pixelPolyline(juce::Graphics& g, const std::vector<juce::Point<int>>& pts, juce::Colour c) {
    for (std::size_t i = 1; i < pts.size(); ++i) pixelLine(g, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, c);
}

juce::Colour mix(juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith(b, t); }

void drawDisc(juce::Graphics& g, float cx, float cy, float radius, juce::Colour c) {
    g.setColour(c);
    const int x0 = int(std::floor(cx - radius)), x1 = int(std::ceil(cx + radius));
    const int y0 = int(std::floor(cy - radius)), y1 = int(std::ceil(cy + radius));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const float dx = float(x) + 0.5f - cx, dy = float(y) + 0.5f - cy;
            if (dx * dx + dy * dy <= radius * radius) g.fillRect(x, y, 1, 1);
        }
}

void setPx(juce::Image& img, int x, int y, juce::Colour c) {
    if (x >= 0 && y >= 0 && x < img.getWidth() && y < img.getHeight()) img.setPixelAt(x, y, c);
}

void blit(juce::Graphics& g, const juce::Image& img, int x, int y) {
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    g.setOpacity(1.0f);
    g.drawImageAt(img, x, y, false);
}

const juce::Image& knobImage(int diameter, float t, const KnobStyle& style, float modLo, float modHi) {
    auto& cache = knobCache();
    if (cache.size() > 3000) cache.clear();
    t = juce::jlimit(0.0f, 1.0f, t);
    const int tq = int(t * 127.0f + 0.5f);
    const int ml = int(juce::jlimit(0.0f, 1.0f, modLo) * 127.0f + 0.5f);
    const int mh = int(juce::jlimit(0.0f, 1.0f, modHi) * 127.0f + 0.5f);
    const bool hasMod = mh > ml;
    const std::string key = std::to_string(diameter) + ":" + std::to_string(tq) + ":" + (style.bipolar ? "b" : "u") +
        (style.dim ? "d" : "n") + std::to_string(style.arc.getARGB()) + ":" + (hasMod ? std::to_string(ml) + "-" + std::to_string(mh) : "-");
    if (auto it = cache.find(key); it != cache.end()) return it->second;

    juce::Image img(juce::Image::ARGB, diameter, diameter, true);
    const float R = float(diameter) * 0.5f;
    const float bandIn = R - 2.0f, gapIn = R - 3.0f;
    const float bodyR = gapIn;
    const float tv = float(tq) / 127.0f;
    const float sweep = 270.0f, startDeg = 225.0f;
    const auto trackCol = style.dim ? pal::edgeDark : pal::edgeMid;
    const auto arcCol = style.dim ? pal::textMuted : style.arc;
    const auto bodyCol = style.dim ? pal::panelHi : pal::raised;
    const auto bodyHi = style.dim ? pal::raised : pal::raisedHi;
    const auto ptrCol = style.dim ? pal::textMuted : style.pointer;
    for (int j = 0; j < diameter; ++j)
        for (int i = 0; i < diameter; ++i) {
            const float dx = float(i) + 0.5f - R, dy = float(j) + 0.5f - R;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > R) continue;
            float deg = std::atan2(dx, -dy) * 180.0f / kPi;
            if (deg < 0) deg += 360.0f;
            float rel = deg - startDeg; if (rel < 0) rel += 360.0f;
            const bool inSweep = rel <= sweep;
            juce::Colour c;
            if (d >= bandIn) {
                if (!inSweep) { c = pal::edgeDark; }
                else {
                    bool lit;
                    if (style.bipolar) {
                        const float centre = sweep * 0.5f, cur = tv * sweep;
                        lit = cur >= centre ? (rel >= centre - 1.0f && rel <= cur + 0.5f)
                                            : (rel <= centre + 1.0f && rel >= cur - 0.5f);
                        if (std::abs(cur - centre) < 1.0f) lit = std::abs(rel - centre) <= 1.5f;
                    } else lit = rel <= tv * sweep + 0.5f && tq > 0 ? true : (tq == 0 && rel <= 1.5f);
                    c = lit ? arcCol : trackCol;
                }
            } else if (d >= gapIn) {
                c = pal::edgeDark;
                if (hasMod && inSweep) {
                    const float ra = float(ml) / 127.0f * sweep, rb = float(mh) / 127.0f * sweep;
                    if (rel >= ra - 0.5f && rel <= rb + 0.5f) c = style.dim ? pal::violetDim : style.modArc;
                }
            } else if (d < bodyR) {
                const float sh = dx + dy;
                if (d >= bodyR - 1.0f) c = sh < 0 ? pal::edgeLight : pal::edgeDark;
                else c = (sh < -bodyR * 0.35f) ? bodyHi : bodyCol;
                if (style.dim && d >= bodyR - 1.0f) c = sh < 0 ? pal::edgeMid : pal::edgeDark;
            } else c = pal::edgeDark;
            img.setPixelAt(i, j, c);
        }
    // pointer
    const float ang = (startDeg + tv * sweep) * kPi / 180.0f;
    const float r0 = bodyR * 0.25f, r1 = bodyR - 1.6f;
    const int thick = diameter >= 30 ? 2 : 1;
    for (float s = r0; s <= r1; s += 0.35f) {
        const float ex = R + std::sin(ang) * s, ey = R - std::cos(ang) * s;
        for (int ox = 0; ox < thick; ++ox)
            for (int oy = 0; oy < thick; ++oy)
                setPx(img, int(std::floor(ex - (thick - 1) * 0.5f)) + ox, int(std::floor(ey - (thick - 1) * 0.5f)) + oy,
                      s > r1 - 1.2f ? ptrCol : mix(ptrCol, bodyCol, 0.15f));
    }
    return cache.emplace(key, std::move(img)).first->second;
}

void drawWaveIcon(juce::Graphics& g, WaveIcon w, juce::Rectangle<int> r, juce::Colour c) {
    const int x0 = r.getX(), x1 = r.getRight() - 1, y0 = r.getY(), y1 = r.getBottom() - 1, mid = (y0 + y1) / 2;
    const int width = r.getWidth();
    std::vector<juce::Point<int>> pts;
    switch (w) {
        case WaveIcon::sine:
        case WaveIcon::rounded:
            for (int x = 0; x < width; ++x) {
                const float ph = float(x) / float(width - 1);
                float v = std::sin(ph * 2.0f * kPi);
                if (w == WaveIcon::rounded) v = std::tanh(v * 2.6f) / std::tanh(2.6f);
                pts.push_back({x0 + x, mid - int(std::round(v * float(y1 - y0) * 0.5f))});
            }
            break;
        case WaveIcon::saw:
            pts = {{x0, mid}, {x0 + width / 2 - 1, y0}, {x0 + width / 2, y1}, {x1, mid}}; break;
        case WaveIcon::triangle:
            pts = {{x0, mid}, {x0 + width / 4, y0}, {x0 + 3 * width / 4, y1}, {x1, mid}}; break;
        case WaveIcon::square:
            pts = {{x0, y1}, {x0, y0}, {x0 + width / 2, y0}, {x0 + width / 2, y1}, {x1, y1}}; break;
        case WaveIcon::pulse:
            pts = {{x0, y1}, {x0, y0}, {x0 + width / 4, y0}, {x0 + width / 4, y1}, {x1, y1}}; break;
        case WaveIcon::noise: {
            std::uint32_t s = 12345;
            for (int x = 0; x < width; ++x) { s = s * 1664525u + 1013904223u; pts.push_back({x0 + x, y0 + int((s >> 24) % unsigned(y1 - y0 + 1))}); }
            break;
        }
    }
    pixelPolyline(g, pts, c);
}

void drawSpinner(juce::Graphics& g, int cx, int cy, int radius, juce::Colour colour, double phase) {
    const int head = int(std::floor(phase * 8.0)) & 7;
    for (int i = 0; i < 8; ++i) {
        const float a = float(i) / 8.0f * 6.2831853f;
        const int x = cx + int(std::round(std::sin(a) * float(radius))), y = cy - int(std::round(std::cos(a) * float(radius)));
        const int age = (head - i + 8) & 7;
        g.setColour(colour.withAlpha(age == 0 ? 1.0f : std::max(0.12f, 1.0f - float(age) * 0.16f)));
        g.fillRect(x, y, age == 0 ? 2 : 1, age == 0 ? 2 : 1);
    }
}

}
