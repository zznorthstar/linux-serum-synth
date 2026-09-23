#!/usr/bin/env python3
"""
generate_placeholders.py
Comprehensive procedural asset generator for the 'Dark and Mysterious Pixel Art'
design system for Linux VST UI rendering.

Generates:
1. Rotary Knob Sprite Sheets (32x32, 48x48, 64x64, 128 frames).
2. Wavetable Visualizer & Filter Response Viewports.
3. Modulation Curve Editors (Envelope ADSR & LFO Decks).
4. Vertical Faders & Chunky Pixel Sliders.
5. Navigation Tabs, Button States, and Dropdown Selectors.
6. Sub & Noise Generator Modules.
7. Warp Mode Sub-Selectors & Popover Menus.
8. Master Volume, Output Limiter, Voicing & Preset Top Bar.
9. Mini Virtual Keyboard & Dedicated 24x48px Pitch/Mod Wheels.
"""

import math
import os
from PIL import Image, ImageDraw

# ==============================================================================
# COLOR PALETTE TOKENS (RGBA)
# ==============================================================================
COLOR_TRANSPARENT    = (0, 0, 0, 0)

# Base Surfaces & Voids
COLOR_BG_VOID        = (8, 6, 13, 255)       # #08060D
COLOR_BG_CHASSIS     = (16, 11, 27, 255)     # #100B1B
COLOR_PANEL          = (24, 17, 38, 255)     # #181126
COLOR_PANEL_RAISED   = (35, 24, 56, 255)     # #231838
COLOR_PANEL_SUNKEN   = (12, 9, 20, 255)      # #0C0914
COLOR_GRID           = (30, 21, 46, 255)     # #1E152E

# Chunky Pixel Borders & Bevels
COLOR_BORDER_DARK    = (10, 7, 16, 255)      # #0A0710
COLOR_BORDER_MED     = (49, 34, 77, 255)     # #31224D
COLOR_BORDER_LGT     = (77, 54, 120, 255)    # #4D3678

# Primary Interactive: Acid Green
COLOR_ACID_HOT       = (214, 255, 133, 255)  # #D6FF85
COLOR_ACID_BASE      = (57, 255, 20, 255)    # #39FF14
COLOR_ACID_DIM       = (36, 168, 18, 255)    # #24A812
COLOR_ACID_SHADOW    = (15, 56, 10, 255)     # #0F380A

# Secondary Interactive: Evil Purple
COLOR_PURPLE_HOT     = (216, 76, 255, 255)   # #D84CFF
COLOR_PURPLE_BASE    = (138, 31, 223, 255)   # #8A1FDF
COLOR_PURPLE_DIM     = (87, 20, 140, 255)    # #57148C
COLOR_PURPLE_SHADE   = (43, 10, 69, 255)     # #2B0A45

# Text & Indicators
COLOR_TEXT_BRIGHT    = (240, 255, 242, 255)  # #F0FFF2
COLOR_TEXT_NORMAL    = (166, 180, 168, 255)  # #A6B4A8
COLOR_TEXT_MUTED     = (89, 99, 90, 255)     # #59635A
COLOR_TEXT_ACID      = (99, 255, 70, 255)    # #63FF46

# Warning & Mod Depth Accents
COLOR_CRIMSON_BRIGHT = (255, 42, 85, 255)    # #FF2A55
COLOR_CRIMSON_DIM    = (138, 17, 42, 255)    # #8A112A
COLOR_AMBER_BRIGHT   = (255, 153, 0, 255)    # #FF9900
COLOR_AMBER_DIM      = (122, 73, 0, 255)     # #7A4900

# Keyboard Specific
COLOR_KEY_WHITE      = (205, 218, 212, 255)
COLOR_KEY_WHITE_SHD  = (160, 172, 166, 255)
COLOR_KEY_BLACK      = (18, 14, 28, 255)
COLOR_KEY_BLACK_TOP  = (38, 28, 56, 255)


# ==============================================================================
# BUILT-IN 5x7 HARD-PIXEL BITMAP FONT
# ==============================================================================
FONT_5X7 = {
    'A': ["01110","10001","10001","11111","10001","10001","10001"],
    'B': ["11110","10001","10001","11110","10001","10001","11110"],
    'C': ["01111","10000","10000","10000","10000","10000","01111"],
    'D': ["11110","10001","10001","10001","10001","10001","11110"],
    'E': ["11111","10000","10000","11110","10000","10000","11111"],
    'F': ["11111","10000","10000","11110","10000","10000","10000"],
    'G': ["01111","10000","10000","10111","10001","10001","01110"],
    'H': ["10001","10001","10001","11111","10001","10001","10001"],
    'I': ["11111","00100","00100","00100","00100","00100","11111"],
    'J': ["00001","00001","00001","00001","10001","10001","01110"],
    'K': ["10001","10010","10100","11000","10100","10010","10001"],
    'L': ["10000","10000","10000","10000","10000","10000","11111"],
    'M': ["10001","11011","10101","10101","10001","10001","10001"],
    'N': ["10001","11001","10101","10011","10001","10001","10001"],
    'O': ["01110","10001","10001","10001","10001","10001","01110"],
    'P': ["11110","10001","10001","11110","10000","10000","10000"],
    'Q': ["01110","10001","10001","10001","10101","10010","01101"],
    'R': ["11110","10001","10001","11110","10100","10010","10001"],
    'S': ["01111","10000","10000","01110","00001","00001","11110"],
    'T': ["11111","00100","00100","00100","00100","00100","00100"],
    'U': ["10001","10001","10001","10001","10001","10001","01110"],
    'V': ["10001","10001","10001","10001","10001","01010","00100"],
    'W': ["10001","10001","10001","10101","10101","11011","10001"],
    'X': ["10001","10001","01010","00100","01010","10001","10001"],
    'Y': ["10001","10001","01010","00100","00100","00100","00100"],
    'Z': ["11111","00001","00010","00100","01000","10000","11111"],
    '0': ["01110","10011","10101","10101","11001","10001","01110"],
    '1': ["00100","01100","00100","00100","00100","00100","01110"],
    '2': ["01110","10001","00001","00010","00100","01000","11111"],
    '3': ["11110","00001","00001","01110","00001","00001","11110"],
    '4': ["10001","10001","10001","11111","00001","00001","00001"],
    '5': ["11111","10000","11110","00001","00001","10001","01110"],
    '6': ["01110","10000","11110","10001","10001","10001","01110"],
    '7': ["11111","00001","00010","00100","01000","01000","01000"],
    '8': ["01110","10001","10001","01110","10001","10001","01110"],
    '9': ["01110","10001","10001","01111","00001","00001","01110"],
    ' ': ["00000","00000","00000","00000","00000","00000","00000"],
    ':': ["00000","00100","00000","00000","00100","00000","00000"],
    '-': ["00000","00000","00000","11111","00000","00000","00000"],
    '+': ["00000","00100","00100","11111","00100","00100","00000"],
    '.': ["00000","00000","00000","00000","00000","01100","01100"],
    '/': ["00001","00010","00010","00100","01000","01000","10000"],
    '<': ["00010","00100","01000","10000","01000","00100","00010"],
    '>': ["01000","00100","00010","00001","00010","00100","01000"],
    '(': ["00010","00100","01000","01000","01000","00100","00010"],
    ')': ["01000","00100","00010","00010","00010","00100","01000"],
    '[': ["01110","01000","01000","01000","01000","01000","01110"],
    ']': ["01110","00010","00010","00010","00010","00010","01110"],
    '%': ["11001","11010","00100","01000","01011","10011","00000"],
    '#': ["01010","01010","11111","01010","11111","01010","01010"],
    ',': ["00000","00000","00000","00000","00100","00100","01000"],
    '=': ["00000","11111","00000","11111","00000","00000","00000"],
    '*': ["00000","10101","01110","11111","01110","10101","00000"],
    '_': ["00000","00000","00000","00000","00000","00000","11111"],
    '^': ["00100","01010","10001","00000","00000","00000","00000"],
    '!': ["00100","00100","00100","00100","00000","00100","00000"],
    '?': ["01110","10001","00010","00100","00100","00000","00100"],
    ';': ["00000","00100","00000","00000","00100","00100","01000"],
    '~': ["01101","10010","00000","00000","00000","00000","00000"],
    '|': ["00100","00100","00100","00100","00100","00100","00100"],
    'v': ["00000","11111","01110","00100","00000","00000","00000"] # Down arrow
}

def draw_pixel_text(img: Image.Image, text: str, x: int, y: int, color: tuple, spacing: int = 1):
    """Draws pixel-perfect 5x7 bitmap text onto an image without anti-aliasing."""
    cursor_x = x
    for char in text:
        glyph = FONT_5X7.get(char, FONT_5X7.get(char.upper(), FONT_5X7[' ']))
        for row_idx, row in enumerate(glyph):
            for col_idx, bit in enumerate(row):
                if bit == '1':
                    px = cursor_x + col_idx
                    py = y + row_idx
                    if 0 <= px < img.width and 0 <= py < img.height:
                        img.putpixel((px, py), color)
        cursor_x += 5 + spacing


def draw_pixel_box(img: Image.Image, x0: int, y0: int, x1: int, y1: int, fill: tuple, border: tuple, shadow: tuple = None):
    """Draws a beveled rectangular panel with 1px hard pixel borders."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if x == x0 or y == y0:
                img.putpixel((x, y), border)
            elif x == x1 or y == y1:
                img.putpixel((x, y), shadow if shadow else border)
            else:
                img.putpixel((x, y), fill)


# ==============================================================================
# 1. ROTARY KNOB SPRITE SHEETS (128 FRAMES)
# ==============================================================================
KNOB_TOTAL_FRAMES = 128
KNOB_START_ANGLE  = -135.0
KNOB_END_ANGLE    = 135.0

def render_knob_frame(size: int, frame_idx: int) -> Image.Image:
    frame = Image.new("RGBA", (size, size), COLOR_TRANSPARENT)
    center = (size - 1) / 2.0
    radius = (size / 2.0) - 2.0
    inner_radius = radius - 2.0

    for y in range(size):
        for x in range(size):
            dx = x - center
            dy = y - center
            dist = math.sqrt(dx * dx + dy * dy)

            if dist <= radius:
                if dist > inner_radius:
                    if (dx + dy) < 0:
                        frame.putpixel((x, y), COLOR_BORDER_LGT)
                    else:
                        frame.putpixel((x, y), COLOR_BORDER_DARK)
                elif dist > inner_radius - 1.5:
                    frame.putpixel((x, y), COLOR_PURPLE_DIM)
                else:
                    if (x + y) % 2 == 0:
                        frame.putpixel((x, y), COLOR_PANEL)
                    else:
                        frame.putpixel((x, y), COLOR_BG_CHASSIS)

    progress = frame_idx / float(KNOB_TOTAL_FRAMES - 1)
    angle_deg = KNOB_START_ANGLE + progress * (KNOB_END_ANGLE - KNOB_START_ANGLE)
    angle_rad = math.radians(angle_deg)

    sin_a = math.sin(angle_rad)
    cos_a = -math.cos(angle_rad)

    r_start = max(2.0, inner_radius * 0.3)
    r_end = inner_radius - 1.0

    steps = int(math.ceil(r_end - r_start)) * 2
    for s in range(steps + 1):
        t = s / float(steps)
        r = r_start + t * (r_end - r_start)
        px = int(round(center + r * sin_a))
        py = int(round(center + r * cos_a))

        if 0 <= px < size and 0 <= py < size:
            if t > 0.8:
                frame.putpixel((px, py), COLOR_ACID_HOT)
            else:
                frame.putpixel((px, py), COLOR_ACID_BASE)

    return frame


def generate_knob_sprite_sheets(output_dir: str):
    sizes = [32, 48, 64]
    for size in sizes:
        sheet_w = size
        sheet_h = size * KNOB_TOTAL_FRAMES
        sheet = Image.new("RGBA", (sheet_w, sheet_h), COLOR_TRANSPARENT)
        for i in range(KNOB_TOTAL_FRAMES):
            frame = render_knob_frame(size, i)
            sheet.paste(frame, (0, i * size))
        out_path = os.path.join(output_dir, f"knob_{size}x{size}_128frames.png")
        sheet.save(out_path, "PNG")
        print(f"Generated Knob Sheet: {out_path}")


# ==============================================================================
# 2. WAVETABLE & FILTER RESPONSE VIEWPORTS
# ==============================================================================
def generate_wavetable_visualizer(output_path: str, width: int = 180, height: int = 96):
    img = Image.new("RGBA", (width, height), COLOR_BG_VOID)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    for y in range(8, height - 8, 8):
        for x in range(8, width - 8, 8):
            img.putpixel((x, y), COLOR_GRID)

    cy = height // 2 + 4
    for x in range(4, width - 4, 4):
        img.putpixel((x, cy), COLOR_BORDER_MED)

    points = []
    for x in range(6, width - 6):
        t = (x - 6) / float(width - 12) * 2.0 * math.pi
        val = math.sin(t) * 0.65 + math.sin(3.0 * t) * 0.25 + ((x % 32) / 32.0 - 0.5) * 0.15
        py = int(round(cy - val * (height * 0.32)))
        points.append((x, py))

    for (x, py) in points:
        y_start = min(py + 1, cy)
        y_end = max(py, cy)
        for y_fill in range(y_start, y_end):
            if (x + y_fill) % 2 == 0:
                img.putpixel((x, y_fill), COLOR_ACID_SHADOW)

    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        img.putpixel((x0, y0), COLOR_ACID_HOT if y0 < cy - 15 else COLOR_ACID_BASE)
        if abs(y1 - y0) > 1:
            step_y = 1 if y1 > y0 else -1
            for y_interp in range(y0 + step_y, y1, step_y):
                img.putpixel((x0, y_interp), COLOR_ACID_BASE)

    draw_pixel_text(img, "OSC A: WT SAW-MORPH", 6, 4, COLOR_TEXT_ACID)
    draw_pixel_text(img, "2D", width - 20, 4, COLOR_TEXT_MUTED)

    img.save(output_path, "PNG")
    print(f"Generated Wavetable Viewport: {output_path}")


def generate_filter_viewport(output_path: str, width: int = 180, height: int = 96):
    img = Image.new("RGBA", (width, height), COLOR_BG_VOID)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    for x in (24, 60, 105, 150):
        for y in range(4, height - 4, 4):
            img.putpixel((x, y), COLOR_GRID)

    cutoff_x = 105
    cutoff_y = 28
    base_y = height - 16

    points = []
    for x in range(6, width - 6):
        if x < cutoff_x - 20:
            py = 42
        elif x < cutoff_x:
            t = (x - (cutoff_x - 20)) / 20.0
            py = int(42 - math.sin(t * math.pi * 0.5) * (42 - cutoff_y))
        else:
            dx = (x - cutoff_x)
            py = int(min(base_y, cutoff_y + dx * dx * 0.055))
        points.append((x, py))

    for (x, py) in points:
        for y_fill in range(py + 1, base_y):
            if (x + y_fill) % 3 == 0:
                img.putpixel((x, y_fill), COLOR_ACID_SHADOW)

    for i in range(len(points) - 1):
        x0, y0 = points[i]
        x1, y1 = points[i + 1]
        img.putpixel((x0, y0), COLOR_ACID_BASE)
        if abs(y1 - y0) > 1:
            step_y = 1 if y1 > y0 else -1
            for y_interp in range(y0 + step_y, y1, step_y):
                img.putpixel((x0, y_interp), COLOR_ACID_BASE)

    for ny in range(cutoff_y - 2, cutoff_y + 3):
        for nx in range(cutoff_x - 2, cutoff_x + 3):
            if nx in (cutoff_x - 2, cutoff_x + 2) or ny in (cutoff_y - 2, cutoff_y + 2):
                img.putpixel((nx, ny), COLOR_ACID_HOT)
            else:
                img.putpixel((nx, ny), COLOR_BG_CHASSIS)

    draw_pixel_text(img, "FLT: MG 24DB", 6, 4, COLOR_TEXT_ACID)
    draw_pixel_text(img, "CUTOFF: 1.2KHZ", 6, height - 12, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "RES: 4.8", width - 56, height - 12, COLOR_PURPLE_HOT)

    img.save(output_path, "PNG")
    print(f"Generated Filter Viewport: {output_path}")


# ==============================================================================
# 3. MODULATION CURVE & ENVELOPE EDITORS (ENV / LFO)
# ==============================================================================
def generate_envelope_editor(output_path: str, width: int = 360, height: int = 120):
    img = Image.new("RGBA", (width, height), COLOR_BG_VOID)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    for y in range(16, height - 16, 16):
        for x in range(16, width - 16, 16):
            img.putpixel((x, y), COLOR_GRID)

    bottom_y = height - 16
    nodes = [
        (16, bottom_y),
        (64, 28),
        (140, 68),
        (260, 68),
        (330, bottom_y)
    ]

    curve_points = []
    for seg_idx in range(len(nodes) - 1):
        x0, y0 = nodes[seg_idx]
        x1, y1 = nodes[seg_idx + 1]
        steps = x1 - x0
        for s in range(steps):
            t = s / float(steps)
            if seg_idx == 0:
                yt = y0 + math.pow(t, 1.4) * (y1 - y0)
            elif seg_idx in (1, 3):
                yt = y0 + (1.0 - math.pow(1.0 - t, 1.6)) * (y1 - y0)
            else:
                yt = float(y0)
            curve_points.append((x0 + s, int(round(yt))))

    for (x, py) in curve_points:
        for yf in range(py + 1, bottom_y):
            if (x + yf) % 2 == 0:
                img.putpixel((x, yf), COLOR_ACID_SHADOW)

    for (x, py) in curve_points:
        img.putpixel((x, py), COLOR_ACID_BASE)

    for sy in range(20, bottom_y, 4):
        img.putpixel((140, sy), COLOR_PURPLE_BASE)
        img.putpixel((260, sy), COLOR_PURPLE_BASE)

    for (nx, ny) in nodes[1:]:
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if abs(dx) == 2 or abs(dy) == 2:
                    img.putpixel((nx + dx, ny + dy), COLOR_ACID_HOT)
                else:
                    img.putpixel((nx + dx, ny + dy), COLOR_BG_VOID)

    draw_pixel_text(img, "ENV 1: AMP [ADSR]", 8, 6, COLOR_TEXT_ACID)
    draw_pixel_text(img, "A: 15MS", 16, height - 12, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "D: 280MS", 85, height - 12, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "S: -4.5DB", 175, height - 12, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "R: 450MS", 270, height - 12, COLOR_TEXT_NORMAL)

    img.save(output_path, "PNG")
    print(f"Generated Envelope Editor: {output_path}")


def generate_lfo_editor(output_path: str, width: int = 360, height: int = 120):
    img = Image.new("RGBA", (width, height), COLOR_BG_VOID)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    mid_y = height // 2
    for x in range(6, width - 6, 2):
        img.putpixel((x, mid_y), COLOR_BORDER_MED)

    lfo_nodes = [
        (16, mid_y),
        (75, 24),
        (135, mid_y),
        (195, height - 26),
        (255, 34),
        (330, mid_y)
    ]

    curve_points = []
    for i in range(len(lfo_nodes) - 1):
        x0, y0 = lfo_nodes[i]
        x1, y1 = lfo_nodes[i + 1]
        steps = x1 - x0
        for s in range(steps):
            t = s / float(steps)
            val = (1.0 - math.cos(t * math.pi)) * 0.5
            py = int(round(y0 + val * (y1 - y0)))
            curve_points.append((x0 + s, py))

    for (x, py) in curve_points:
        y_a = min(py, mid_y)
        y_b = max(py, mid_y)
        for yf in range(y_a, y_b):
            if (x + yf) % 2 == 0:
                img.putpixel((x, yf), COLOR_PURPLE_SHADE)

    for (x, py) in curve_points:
        img.putpixel((x, py), COLOR_PURPLE_HOT)

    for (nx, ny) in lfo_nodes:
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                if abs(dx) == 2 or abs(dy) == 2:
                    img.putpixel((nx + dx, ny + dy), COLOR_ACID_HOT)
                else:
                    img.putpixel((nx + dx, ny + dy), COLOR_BG_VOID)

    draw_pixel_text(img, "LFO 1: 1/8 SYNC [TRI-MORPH]", 8, 6, COLOR_TEXT_ACID)
    draw_pixel_text(img, "TRIG: ENV", width - 80, 6, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "SMOOTH: 0.0MS", 16, height - 12, COLOR_TEXT_MUTED)

    img.save(output_path, "PNG")
    print(f"Generated LFO Editor: {output_path}")


# ==============================================================================
# 4. VERTICAL FADERS & SLIDERS (TRACK & THUMB)
# ==============================================================================
def generate_fader_components(output_dir: str):
    tw, th = 12, 100
    track = Image.new("RGBA", (tw, th), COLOR_TRANSPARENT)

    for y in range(th):
        for x in range(tw):
            track.putpixel((x, y), COLOR_BG_CHASSIS)

    for y in range(2, th - 2):
        for x in range(3, 9):
            track.putpixel((x, y), COLOR_PANEL_SUNKEN)

    for y in range(2, th - 2):
        track.putpixel((3, y), COLOR_BORDER_DARK)
        track.putpixel((8, y), COLOR_BORDER_MED)

    for y in range(4, th - 4):
        track.putpixel((5, y), COLOR_BORDER_DARK)

    for y in range(10, th - 10, 10):
        track.putpixel((10, y), COLOR_BORDER_MED)
        track.putpixel((11, y), COLOR_BORDER_MED)

    track.putpixel((10, 30), COLOR_ACID_DIM)
    track.putpixel((11, 30), COLOR_ACID_BASE)

    track_path = os.path.join(output_dir, "fader_track_12x100.png")
    track.save(track_path, "PNG")
    print(f"Generated Fader Track: {track_path}")

    pw, ph = 18, 12
    thumb = Image.new("RGBA", (pw, ph), COLOR_TRANSPARENT)
    draw_pixel_box(thumb, 0, 0, pw - 1, ph - 1, COLOR_PANEL_RAISED, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    for y in (3, 8):
        for x in range(3, pw - 3):
            thumb.putpixel((x, y), COLOR_BORDER_MED)

    for x in range(1, pw - 1):
        thumb.putpixel((x, 5), COLOR_ACID_HOT)
        thumb.putpixel((x, 6), COLOR_ACID_BASE)

    thumb_path = os.path.join(output_dir, "fader_thumb_18x12.png")
    thumb.save(thumb_path, "PNG")
    print(f"Generated Fader Thumb: {thumb_path}")


# ==============================================================================
# 5. TABS, BUTTONS & SELECTORS
# ==============================================================================
def generate_tabs_and_buttons(output_dir: str):
    tab_w, tab_h = 76, 24
    total_w = tab_w * 5
    tabs_img = Image.new("RGBA", (total_w, tab_h), COLOR_TRANSPARENT)
    tab_names = ["OSC", "MIX", "FX", "MATRIX", "GLOBAL"]

    for idx, name in enumerate(tab_names):
        x0 = idx * tab_w
        x1 = x0 + tab_w - 1
        is_active = (idx == 0)

        fill_color = COLOR_PANEL_RAISED if is_active else COLOR_BG_CHASSIS
        border_top = COLOR_BORDER_LGT if is_active else COLOR_BORDER_MED

        draw_pixel_box(tabs_img, x0, 0, x1, tab_h - 1, fill_color, border_top, COLOR_BORDER_DARK)

        if is_active:
            for x in range(x0 + 2, x1 - 1):
                tabs_img.putpixel((x, 1), COLOR_ACID_BASE)
            draw_pixel_text(tabs_img, name, x0 + (tab_w - len(name) * 6) // 2, 9, COLOR_TEXT_BRIGHT)
        else:
            draw_pixel_text(tabs_img, name, x0 + (tab_w - len(name) * 6) // 2, 9, COLOR_TEXT_MUTED)

    tabs_path = os.path.join(output_dir, "tabs_main_nav_380x24.png")
    tabs_img.save(tabs_path, "PNG")
    print(f"Generated Navigation Tabs: {tabs_path}")

    btn_w, btn_h = 64, 24
    btn_sheet = Image.new("RGBA", (btn_w, btn_h * 4), COLOR_TRANSPARENT)

    states = [
        ("NORMAL", COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK, COLOR_TEXT_NORMAL),
        ("HOVER",  COLOR_PANEL_RAISED, COLOR_PURPLE_HOT, COLOR_BORDER_DARK, COLOR_TEXT_BRIGHT),
        ("PRESSED",COLOR_PURPLE_SHADE, COLOR_BORDER_DARK, COLOR_BORDER_LGT, COLOR_PURPLE_HOT),
        ("ON",     COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK, COLOR_ACID_HOT),
    ]

    for s_idx, (label, fill, b_lgt, b_drk, txt_c) in enumerate(states):
        y0 = s_idx * btn_h
        y1 = y0 + btn_h - 1
        draw_pixel_box(btn_sheet, 0, y0, btn_w - 1, y1, fill, b_lgt, b_drk)

        if label == "ON":
            for py in range(y0 + 9, y0 + 15):
                for px in range(6, 10):
                    btn_sheet.putpixel((px, py), COLOR_ACID_BASE)

        draw_pixel_text(btn_sheet, label, 16, y0 + 9, txt_c)

    btn_path = os.path.join(output_dir, "button_states_64x96.png")
    btn_sheet.save(btn_path, "PNG")
    print(f"Generated Button States: {btn_path}")

    sel_w, sel_h = 80, 20
    sel_img = Image.new("RGBA", (sel_w, sel_h), COLOR_TRANSPARENT)
    draw_pixel_box(sel_img, 0, 0, sel_w - 1, sel_h - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(sel_img, "ANALOG", 6, 7, COLOR_TEXT_BRIGHT)
    draw_pixel_text(sel_img, "V", sel_w - 12, 7, COLOR_ACID_BASE)

    sel_path = os.path.join(output_dir, "selector_dropdown_80x20.png")
    sel_img.save(sel_path, "PNG")
    print(f"Generated Dropdown Selector: {sel_path}")


# ==============================================================================
# 6. SUB & NOISE GENERATOR PANELS
# ==============================================================================
def generate_sub_noise_panels(output_dir: str):
    w, h = 150, 96

    # --- SUB OSCILLATOR PANEL ---
    sub_img = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(sub_img, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Power Switch LED
    draw_pixel_box(sub_img, 6, 6, 16, 16, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 15):
        for px in range(8, 15):
            sub_img.putpixel((px, py), COLOR_ACID_BASE)
    sub_img.putpixel((11, 11), COLOR_ACID_HOT)

    draw_pixel_text(sub_img, "SUB OSC", 22, 8, COLOR_TEXT_ACID)

    # Sunken Waveform Selector Box (Shows 4 shapes: Sine, Tri, Saw, Pulse)
    draw_pixel_box(sub_img, 6, 22, w - 7, 50, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # 4 wave slot icons
    wave_slots = ["SINE", "TRI", "SAW", "SQ"]
    slot_w = (w - 14) // 4
    for idx, wave_name in enumerate(wave_slots):
        sx0 = 7 + idx * slot_w
        sx1 = sx0 + slot_w - 1
        is_sel = (idx == 0) # SINE selected
        if is_sel:
            draw_pixel_box(sub_img, sx0, 23, sx1, 49, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            draw_pixel_text(sub_img, wave_name, sx0 + 3, 33, COLOR_ACID_HOT)
        else:
            draw_pixel_text(sub_img, wave_name, sx0 + 4, 33, COLOR_TEXT_MUTED)

    # Sub tuning & Level Readout Box
    draw_pixel_box(sub_img, 6, 56, 68, 74, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(sub_img, "OCT: -1", 10, 62, COLOR_TEXT_BRIGHT)

    draw_pixel_box(sub_img, 76, 56, w - 7, 74, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(sub_img, "LVL: 80%", 80, 62, COLOR_TEXT_BRIGHT)

    # Footer direct out toggle
    draw_pixel_text(sub_img, "DIRECT OUT: ON", 10, 82, COLOR_PURPLE_HOT)

    sub_path = os.path.join(output_dir, "panel_sub_osc_150x96.png")
    sub_img.save(sub_path, "PNG")
    print(f"Generated Sub Oscillator Panel: {sub_path}")

    # --- NOISE GENERATOR PANEL ---
    noise_img = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(noise_img, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Power switch LED (ON)
    draw_pixel_box(noise_img, 6, 6, 16, 16, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 15):
        for px in range(8, 15):
            noise_img.putpixel((px, py), COLOR_PURPLE_BASE)
    noise_img.putpixel((11, 11), COLOR_PURPLE_HOT)

    draw_pixel_text(noise_img, "NOISE OSC", 22, 8, COLOR_PURPLE_HOT)

    # Sunken Sample Selector Bar
    draw_pixel_box(noise_img, 6, 22, w - 7, 44, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(noise_img, "<", 10, 30, COLOR_ACID_BASE)
    draw_pixel_text(noise_img, "ACID_VINYL_02", 22, 30, COLOR_TEXT_BRIGHT)
    draw_pixel_text(noise_img, ">", w - 16, 30, COLOR_ACID_BASE)

    # Mini control readouts
    draw_pixel_box(noise_img, 6, 50, 72, 68, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(noise_img, "PTCH: +12", 10, 56, COLOR_TEXT_NORMAL)

    draw_pixel_box(noise_img, 78, 50, w - 7, 68, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(noise_img, "LVL: 45%", 82, 56, COLOR_TEXT_NORMAL)

    # Loop and One-Shot switches
    draw_pixel_box(noise_img, 6, 74, 72, 90, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(noise_img, "LOOP: ON", 10, 80, COLOR_ACID_HOT)

    draw_pixel_box(noise_img, 78, 74, w - 7, 90, COLOR_PANEL, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(noise_img, "KEY TRK", 82, 80, COLOR_TEXT_MUTED)

    noise_path = os.path.join(output_dir, "panel_noise_osc_150x96.png")
    noise_img.save(noise_path, "PNG")
    print(f"Generated Noise Oscillator Panel: {noise_path}")


# ==============================================================================
# 7. WARP MODE SELECTORS & POPOVER MENUS
# ==============================================================================
def generate_warp_components(output_dir: str):
    # 1. Compact Warp Mode Trigger Button (76 x 20 px)
    bw, bh = 76, 20
    w_btn = Image.new("RGBA", (bw, bh), COLOR_TRANSPARENT)
    draw_pixel_box(w_btn, 0, 0, bw - 1, bh - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(w_btn, "FM (B)", 6, 7, COLOR_ACID_BASE)
    draw_pixel_text(w_btn, "V", bw - 12, 7, COLOR_ACID_HOT)

    w_btn_path = os.path.join(output_dir, "warp_mode_button_76x20.png")
    w_btn.save(w_btn_path, "PNG")
    print(f"Generated Warp Mode Button: {w_btn_path}")

    # 2. Popover Menu Dropdown (116 x 144 px)
    mw, mh = 116, 144
    menu = Image.new("RGBA", (mw, mh), COLOR_BG_VOID)
    draw_pixel_box(menu, 0, 0, mw - 1, mh - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    warp_modes = [
        "OFF",
        "SYNC (NO SLV)",
        "BEND (+/-)",
        "PWM",
        "ASYM (+/-)",
        "FLIP",
        "MIRROR",
        "FM (FROM B)",  # Highlighted active item
        "AM (FROM B)",
        "RM (FROM B)"
    ]

    item_h = 14
    for idx, mode in enumerate(warp_modes):
        y0 = 2 + idx * item_h
        y1 = y0 + item_h - 1
        is_active = (mode == "FM (FROM B)")

        if is_active:
            # Active highlighted item row
            draw_pixel_box(menu, 2, y0, mw - 3, y1, COLOR_PANEL_RAISED, COLOR_PURPLE_BASE, COLOR_BORDER_DARK)
            # Active Acid Green indicator pip
            for py in range(y0 + 4, y0 + 10):
                for px in range(4, 7):
                    menu.putpixel((px, py), COLOR_ACID_BASE)
            draw_pixel_text(menu, mode, 12, y0 + 4, COLOR_ACID_HOT)
        else:
            draw_pixel_text(menu, mode, 12, y0 + 4, COLOR_TEXT_NORMAL)

    menu_path = os.path.join(output_dir, "warp_menu_dropdown_116x144.png")
    menu.save(menu_path, "PNG")
    print(f"Generated Warp Menu Dropdown: {menu_path}")


# ==============================================================================
# 8. MASTER SECTION, LIMITER, TUNING & PRESET BAR
# ==============================================================================
def generate_master_and_top_bar(output_dir: str):
    # Top Bar Header Strip (420 x 34 px)
    w, h = 420, 34
    top_bar = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(top_bar, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # 1. Preset Navigation Box (Left side: 160 px wide)
    draw_pixel_box(top_bar, 4, 4, 164, h - 5, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    # Prev button '<'
    draw_pixel_box(top_bar, 6, 6, 18, h - 7, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(top_bar, "<", 9, 14, COLOR_ACID_BASE)
    # Preset Name
    draw_pixel_text(top_bar, "EVIL_BASS_01", 24, 14, COLOR_TEXT_BRIGHT)
    # Next button '>'
    draw_pixel_box(top_bar, 150, 6, 162, h - 7, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(top_bar, ">", 154, 14, COLOR_ACID_BASE)

    # 2. Voicing / Polyphony Counter LCD (80 x 24 px)
    draw_pixel_box(top_bar, 172, 4, 252, h - 5, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(top_bar, "VOICES", 176, 7, COLOR_TEXT_MUTED)
    draw_pixel_text(top_bar, "08/16", 176, 17, COLOR_TEXT_ACID)

    # 3. Master Tune Display (55 x 24 px)
    draw_pixel_box(top_bar, 258, 4, 313, h - 5, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(top_bar, "TUNE", 262, 7, COLOR_TEXT_MUTED)
    draw_pixel_text(top_bar, "440HZ", 262, 17, COLOR_TEXT_NORMAL)

    # 4. Limiter / Clipping Peak LEDs
    # Signal Activity LEDs (Green dots)
    for ly in (10, 16, 22):
        top_bar.putpixel((326, ly), COLOR_ACID_BASE)
        top_bar.putpixel((327, ly), COLOR_ACID_HOT)
        top_bar.putpixel((332, ly), COLOR_ACID_BASE)
        top_bar.putpixel((333, ly), COLOR_ACID_HOT)

    # Dual Clip Warning LEDs in Glowing Crimson (#FF2A55)
    draw_pixel_box(top_bar, 342, 8, 350, 16, COLOR_CRIMSON_DIM, COLOR_BORDER_DARK)
    top_bar.putpixel((346, 12), COLOR_CRIMSON_BRIGHT) # L Clip

    draw_pixel_box(top_bar, 342, 18, 350, 26, COLOR_CRIMSON_DIM, COLOR_BORDER_DARK)
    top_bar.putpixel((346, 22), COLOR_CRIMSON_BRIGHT) # R Clip

    draw_pixel_text(top_bar, "CLIP", 355, 14, COLOR_CRIMSON_BRIGHT)

    # 5. Master Level Knob placeholder rim (32x32 area)
    draw_pixel_text(top_bar, "VOL", 385, 14, COLOR_TEXT_BRIGHT)

    top_bar_path = os.path.join(output_dir, "panel_master_section_420x34.png")
    top_bar.save(top_bar_path, "PNG")
    print(f"Generated Master Top Bar: {top_bar_path}")


# ==============================================================================
# 9. MINI VIRTUAL KEYBOARD & DEDICATED 24x48px PITCH/MOD WHEELS
# ==============================================================================
def generate_keyboard_and_wheels(output_dir: str):
    # Dedicated Pitch Bend Wheel (24 x 48 px)
    pw_w, pw_h = 24, 48
    pitch_wheel = Image.new("RGBA", (pw_w, pw_h), COLOR_TRANSPARENT)
    draw_pixel_box(pitch_wheel, 0, 0, pw_w - 1, pw_h - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Horizontal rubber tactile ridges
    for y in range(6, pw_h - 6, 4):
        if abs(y - pw_h // 2) > 2:
            for x in range(3, pw_w - 3):
                pitch_wheel.putpixel((x, y), COLOR_BORDER_MED)

    # Center spring-return zero detent (Acid Green highlight)
    for x in range(2, pw_w - 2):
        pitch_wheel.putpixel((x, pw_h // 2), COLOR_ACID_BASE)
        pitch_wheel.putpixel((x, pw_h // 2 + 1), COLOR_ACID_HOT)

    draw_pixel_text(pitch_wheel, "P", (pw_w - 5) // 2, pw_h - 9, COLOR_TEXT_MUTED)

    pw_path = os.path.join(output_dir, "wheel_pitch_24x48.png")
    pitch_wheel.save(pw_path, "PNG")
    print(f"Generated Pitch Wheel: {pw_path}")

    # Dedicated Modulation Wheel (24 x 48 px)
    mod_wheel = Image.new("RGBA", (pw_w, pw_h), COLOR_TRANSPARENT)
    draw_pixel_box(mod_wheel, 0, 0, pw_w - 1, pw_h - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Gripping notches
    for y in range(6, pw_h - 10, 4):
        for x in range(3, pw_w - 3):
            mod_wheel.putpixel((x, y), COLOR_BORDER_MED)

    # Mod resting bar (at bottom: 0 position, or slightly raised in Evil Purple)
    for x in range(2, pw_w - 2):
        mod_wheel.putpixel((x, pw_h - 12), COLOR_PURPLE_HOT)
        mod_wheel.putpixel((x, pw_h - 11), COLOR_PURPLE_BASE)

    draw_pixel_text(mod_wheel, "M", (pw_w - 5) // 2, pw_h - 9, COLOR_TEXT_MUTED)

    mw_path = os.path.join(output_dir, "wheel_mod_24x48.png")
    mod_wheel.save(mw_path, "PNG")
    print(f"Generated Mod Wheel: {mw_path}")

    # Virtual Keyboard (480 x 48 px, 2 Octaves)
    kw, kh = 480, 48
    kb_img = Image.new("RGBA", (kw, kh), COLOR_BG_VOID)

    white_key_w = 34
    black_key_w = 20
    black_key_h = 28

    # 14 White keys
    for i in range(14):
        x0 = i * white_key_w
        x1 = x0 + white_key_w - 1

        # Test active note on key index 5 (F3)
        is_active = (i == 5)
        k_fill = COLOR_ACID_BASE if is_active else COLOR_KEY_WHITE
        k_shd  = COLOR_ACID_DIM if is_active else COLOR_KEY_WHITE_SHD

        for y in range(kh):
            for x in range(x0, x1 + 1):
                if x == x0 or y == 0:
                    kb_img.putpixel((x, y), COLOR_BORDER_MED)
                elif x == x1 or y == kh - 1:
                    kb_img.putpixel((x, y), COLOR_BORDER_DARK)
                elif y > kh - 6:
                    kb_img.putpixel((x, y), k_shd)
                else:
                    kb_img.putpixel((x, y), k_fill)

    # 10 Black keys
    black_positions = [0, 1, 3, 4, 5, 7, 8, 10, 11, 12]
    for b_idx in black_positions:
        x0 = (b_idx + 1) * white_key_w - (black_key_w // 2)
        x1 = x0 + black_key_w - 1

        for y in range(black_key_h):
            for x in range(x0, x1 + 1):
                if x == x0 or y == 0:
                    kb_img.putpixel((x, y), COLOR_BORDER_MED)
                elif x == x1 or y == black_key_h - 1:
                    kb_img.putpixel((x, y), COLOR_BORDER_DARK)
                elif y < 4:
                    kb_img.putpixel((x, y), COLOR_KEY_BLACK_TOP)
                else:
                    kb_img.putpixel((x, y), COLOR_KEY_BLACK)

    kb_path = os.path.join(output_dir, "keyboard_octaves_480x48.png")
    kb_img.save(kb_path, "PNG")
    print(f"Generated Virtual Keyboard: {kb_path}")

    # Unified Bottom Footer Dock (540 x 48 px: Wheels + 6px gap + Keyboard)
    dock_w = 24 + 4 + 24 + 8 + 480
    dock_img = Image.new("RGBA", (dock_w, kh), COLOR_BG_CHASSIS)
    dock_img.paste(pitch_wheel, (0, 0))
    dock_img.paste(mod_wheel, (28, 0))
    dock_img.paste(kb_img, (60, 0))

    dock_path = os.path.join(output_dir, "panel_bottom_dock_540x48.png")
    dock_img.save(dock_path, "PNG")
    print(f"Generated Bottom Footer Dock: {dock_path}")


# ==============================================================================
# 10. FX RACK COMPONENTS (SIDEBAR, ADD DROPDOWN, DISTORTION, COMPRESSOR, DELAY)
# ==============================================================================
def generate_fx_sidebar(output_path: str, width: int = 120, height: int = 340):
    img = Image.new("RGBA", (width, height), COLOR_BG_CHASSIS)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    draw_pixel_text(img, "FX ROUTING", 8, 8, COLOR_TEXT_ACID)

    tabs = ["MAIN", "BUS 1", "BUS 2"]
    tw = (width - 12) // 3
    for idx, tname in enumerate(tabs):
        tx0 = 6 + idx * tw
        tx1 = tx0 + tw - 1
        is_active = (idx == 0)
        fill = COLOR_PANEL_RAISED if is_active else COLOR_BG_CHASSIS
        border = COLOR_BORDER_LGT if is_active else COLOR_BORDER_MED
        draw_pixel_box(img, tx0, 22, tx1, 38, fill, border, COLOR_BORDER_DARK)
        if is_active:
            for x in range(tx0 + 2, tx1 - 1):
                img.putpixel((x, 23), COLOR_ACID_BASE)
            draw_pixel_text(img, tname, tx0 + 3, 28, COLOR_TEXT_BRIGHT)
        else:
            draw_pixel_text(img, tname, tx0 + 3, 28, COLOR_TEXT_MUTED)

    slot_names = [
        ("DISTORTION", True, COLOR_ACID_BASE),
        ("COMPRESSOR", True, COLOR_PURPLE_BASE),
        ("DELAY", True, COLOR_ACID_BASE),
        ("REVERB", False, COLOR_PURPLE_SHADE),
        ("EMPTY", False, COLOR_BORDER_MED)
    ]

    for s_idx, (name, is_on, bar_c) in enumerate(slot_names):
        sy0 = 46 + s_idx * 48
        sy1 = sy0 + 42
        draw_pixel_box(img, 6, sy0, width - 7, sy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

        if name != "EMPTY":
            led_c = COLOR_ACID_BASE if is_on else COLOR_PURPLE_SHADE
            draw_pixel_box(img, 10, sy0 + 6, 17, sy0 + 13, COLOR_PANEL, COLOR_BORDER_DARK, COLOR_BORDER_MED)
            for py in range(sy0 + 8, sy0 + 12):
                for px in range(12, 16):
                    img.putpixel((px, py), led_c)
            if is_on:
                img.putpixel((13, sy0 + 9), COLOR_ACID_HOT)

            for dy in (0, 3, 6):
                img.putpixel((21, sy0 + 7 + dy), COLOR_BORDER_LGT)
                img.putpixel((23, sy0 + 7 + dy), COLOR_BORDER_LGT)

            txt_c = COLOR_TEXT_BRIGHT if is_on else COLOR_TEXT_MUTED
            draw_pixel_text(img, name, 28, sy0 + 7, txt_c)

            draw_pixel_box(img, 28, sy0 + 22, width - 14, sy0 + 26, COLOR_BG_VOID, COLOR_BORDER_DARK)
            fill_len = int((width - 44) * (0.85 if is_on else 0.0))
            for fx in range(29, 29 + fill_len):
                for fy in range(sy0 + 23, sy0 + 26):
                    img.putpixel((fx, fy), bar_c)
        else:
            draw_pixel_text(img, "[EMPTY SLOT]", 16, sy0 + 18, COLOR_BORDER_MED)

    draw_pixel_box(img, 6, height - 34, width - 7, height - 10, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(img, "+ ADD FX", 22, height - 24, COLOR_ACID_HOT)

    img.save(output_path, "PNG")
    print(f"Generated FX Sidebar: {output_path}")


def generate_fx_add_menu(output_path: str, width: int = 140, height: int = 260):
    img = Image.new("RGBA", (width, height), COLOR_BG_VOID)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    draw_pixel_text(img, "ADD EFFECT", 8, 6, COLOR_TEXT_MUTED)

    fx_list = [
        "BODE", "CHORUS", "COMPRESSOR", "CONVOLVE",
        "DELAY", "DISTORTION", "EQUALIZER", "FILTER",
        "FLANGER", "HYPER/DIM", "PHASER", "REVERB",
        "SPLIT L/H", "SPLIT L/M/H", "SPLIT MB", "UTILITY"
    ]

    row_h = 14
    for idx, fx_name in enumerate(fx_list):
        ry0 = 18 + idx * row_h
        ry1 = ry0 + row_h - 1
        is_hovered = (fx_name == "DISTORTION")

        if is_hovered:
            draw_pixel_box(img, 2, ry0, width - 3, ry1, COLOR_PANEL, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            for py in range(ry0 + 4, ry0 + 10):
                for px in range(4, 7):
                    img.putpixel((px, py), COLOR_ACID_BASE)
            draw_pixel_text(img, fx_name, 12, ry0 + 4, COLOR_ACID_HOT)
        else:
            draw_pixel_text(img, fx_name, 12, ry0 + 4, COLOR_TEXT_NORMAL)

    img.save(output_path, "PNG")
    print(f"Generated FX Add Menu: {output_path}")


def generate_fx_distortion(output_path: str, width: int = 420, height: int = 110):
    img = Image.new("RGBA", (width, height), COLOR_BG_CHASSIS)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Header
    draw_pixel_box(img, 6, 6, 14, 14, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 13):
        for px in range(8, 13):
            img.putpixel((px, py), COLOR_ACID_BASE)
    img.putpixel((10, 10), COLOR_ACID_HOT)

    for dy in (0, 3, 6):
        img.putpixel((18, 7 + dy), COLOR_BORDER_LGT)
        img.putpixel((20, 7 + dy), COLOR_BORDER_LGT)

    draw_pixel_text(img, "DISTORTION", 26, 7, COLOR_TEXT_ACID)

    for row in range(2):
        for col in range(2):
            sq_x0 = 114 + col * 7
            sq_y0 = 6 + row * 7
            sq_active = (row == 1 and col == 0)
            sq_fill = COLOR_ACID_BASE if sq_active else COLOR_PANEL_SUNKEN
            draw_pixel_box(img, sq_x0, sq_y0, sq_x0 + 5, sq_y0 + 5, sq_fill, COLOR_BORDER_DARK)

    draw_pixel_box(img, 134, 4, 250, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "TUBE/LIN FOLD", 138, 8, COLOR_TEXT_BRIGHT)
    draw_pixel_text(img, "V", 240, 8, COLOR_ACID_BASE)

    draw_pixel_box(img, width - 36, 4, width - 22, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "S", width - 32, 8, COLOR_TEXT_MUTED)
    draw_pixel_box(img, width - 18, 4, width - 4, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "M", width - 14, 8, COLOR_TEXT_MUTED)

    # Transfer Curve Display
    cx0, cy0, cx1, cy1 = 8, 24, 108, 100
    draw_pixel_box(img, cx0, cy0, cx1, cy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for gx in range(cx0 + 12, cx1, 16):
        for gy in range(cy0 + 12, cy1, 16):
            img.putpixel((gx, gy), COLOR_GRID)

    mid_x = (cx0 + cx1) // 2
    mid_y = (cy0 + cy1) // 2
    for x in range(cx0 + 4, cx1 - 3, 2):
        img.putpixel((x, mid_y), COLOR_BORDER_MED)
    for y in range(cy0 + 4, cy1 - 3, 2):
        img.putpixel((mid_x, y), COLOR_BORDER_MED)

    curve_pts = []
    for x in range(cx0 + 4, cx1 - 3):
        norm_x = (x - mid_x) / 36.0
        norm_y = math.tanh(norm_x * 2.2) * 0.85
        py = int(round(mid_y - norm_y * 28.0))
        curve_pts.append((x, py))

    for (x, py) in curve_pts:
        y_a = min(py, mid_y)
        y_b = max(py, mid_y)
        for yf in range(y_a, y_b):
            if (x + yf) % 2 == 0:
                img.putpixel((x, yf), COLOR_ACID_SHADOW)

    for (x, py) in curve_pts:
        img.putpixel((x, py), COLOR_ACID_BASE)
        if py < mid_y - 12 or py > mid_y + 12:
            img.putpixel((x, py), COLOR_ACID_HOT)

# ==============================================================================
# 10. MODULAR FX TEMPLATE FACTORY (ALL 14 SERUM FX MODULES)
# ==============================================================================
def create_fx_card_base(title: str, mode_str: str = None, active_routing_tile: int = -1, width: int = 420, height: int = 110) -> Image.Image:
    """Standardized 420x110 FX chassis with power LED, drag pips, title, mode, and solo/mute."""
    img = Image.new("RGBA", (width, height), COLOR_BG_CHASSIS)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Power LED toggle (ON)
    draw_pixel_box(img, 6, 6, 14, 14, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 13):
        for px in range(8, 13):
            img.putpixel((px, py), COLOR_ACID_BASE)
    img.putpixel((10, 10), COLOR_ACID_HOT)

    # Drag handle (2x3 pixel dots)
    for dy in (0, 3, 6):
        img.putpixel((18, 7 + dy), COLOR_BORDER_LGT)
        img.putpixel((20, 7 + dy), COLOR_BORDER_LGT)

    # Title
    draw_pixel_text(img, title, 26, 7, COLOR_TEXT_ACID)

    # Optional 4-square routing icon
    if active_routing_tile >= 0:
        for row in range(2):
            for col in range(2):
                idx = row * 2 + col
                sq_x0 = 108 + col * 7
                sq_y0 = 6 + row * 7
                sq_fill = COLOR_ACID_BASE if (idx == active_routing_tile) else COLOR_PANEL_SUNKEN
                draw_pixel_box(img, sq_x0, sq_y0, sq_x0 + 5, sq_y0 + 5, sq_fill, COLOR_BORDER_DARK)

    # Mode Dropdown
    if mode_str:
        dropdown_x0 = 126 if active_routing_tile >= 0 else 116
        dropdown_w = len(mode_str) * 6 + 18
        draw_pixel_box(img, dropdown_x0, 4, dropdown_x0 + dropdown_w, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        draw_pixel_text(img, mode_str, dropdown_x0 + 4, 8, COLOR_TEXT_BRIGHT)
        draw_pixel_text(img, "V", dropdown_x0 + dropdown_w - 9, 8, COLOR_ACID_BASE)

    # Solo & Mute toggles on top right
    draw_pixel_box(img, width - 36, 4, width - 22, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "S", width - 32, 8, COLOR_TEXT_MUTED)
    draw_pixel_box(img, width - 18, 4, width - 4, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "M", width - 14, 8, COLOR_TEXT_MUTED)

    return img


def draw_card_knob(img: Image.Image, kx: int, ky: int, r: int, label: str, val_str: str, angle: float = 0.4, is_hero: bool = False):
    """Draws a pixel knob placeholder with directional bevel, indicator notch, and labels."""
    for y in range(ky - r, ky + r + 1):
        for x in range(kx - r, kx + r + 1):
            d = math.hypot(x - kx, y - ky)
            if d <= r:
                if d > r - 2:
                    img.putpixel((x, y), COLOR_BORDER_LGT if (x + y) < (kx + ky) else COLOR_BORDER_DARK)
                else:
                    img.putpixel((x, y), COLOR_PANEL_SUNKEN)
    nx = int(round(kx + (r - 3) * math.sin(angle)))
    ny = int(round(ky - (r - 3) * math.cos(angle)))
    img.putpixel((nx, ny), COLOR_ACID_HOT if is_hero else COLOR_ACID_BASE)
    draw_pixel_text(img, label, kx - len(label) * 3, ky - r - 9, COLOR_TEXT_MUTED)
    draw_pixel_text(img, val_str, kx - len(val_str) * 3, ky + r + 3, COLOR_TEXT_BRIGHT)


# 1. Distortion & Waveshaper
def generate_fx_distortion(output_path: str):
    img = create_fx_card_base("DISTORTION", "TUBE/LIN FOLD", active_routing_tile=2)
    cx0, cy0, cx1, cy1 = 8, 24, 108, 100
    draw_pixel_box(img, cx0, cy0, cx1, cy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for gx in range(cx0 + 12, cx1, 16):
        for gy in range(cy0 + 12, cy1, 16):
            img.putpixel((gx, gy), COLOR_GRID)

    mid_x, mid_y = (cx0 + cx1) // 2, (cy0 + cy1) // 2
    for x in range(cx0 + 4, cx1 - 3, 2): img.putpixel((x, mid_y), COLOR_BORDER_MED)
    for y in range(cy0 + 4, cy1 - 3, 2): img.putpixel((mid_x, y), COLOR_BORDER_MED)

    curve_pts = []
    for x in range(cx0 + 4, cx1 - 3):
        norm_x = (x - mid_x) / 36.0
        norm_y = math.tanh(norm_x * 2.2) * 0.85
        py = int(round(mid_y - norm_y * 28.0))
        curve_pts.append((x, py))

    for (x, py) in curve_pts:
        y_a, y_b = min(py, mid_y), max(py, mid_y)
        for yf in range(y_a, y_b):
            if (x + yf) % 2 == 0: img.putpixel((x, yf), COLOR_ACID_SHADOW)
        img.putpixel((x, py), COLOR_ACID_HOT if (py < mid_y - 12 or py > mid_y + 12) else COLOR_ACID_BASE)

    draw_pixel_text(img, "WAVESHAPER", cx0 + 4, cy0 + 4, COLOR_TEXT_ACID)
    draw_card_knob(img, 136, 60, 14, "CUTOFF", "2.4K")
    draw_card_knob(img, 196, 60, 14, "RES", "3.2")
    draw_card_knob(img, 268, 58, 18, "DRIVE", "+14DB", angle=0.85, is_hero=True)
    draw_card_knob(img, 340, 60, 14, "MIX", "100%")
    draw_card_knob(img, 388, 60, 14, "OUTPUT", "0.0DB")
    img.save(output_path, "PNG")
    print(f"Generated Distortion Module: {output_path}")


# 2. Compressor & Multiband Compressor
def generate_fx_compressor(output_path: str):
    img = create_fx_card_base("COMPRESSOR", "MULTIBAND ON")
    # Multiband & Horizontal GR Display
    mx0, my0, mx1, my1 = 8, 24, 114, 102
    draw_pixel_box(img, mx0, my0, mx1, my1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Top horizontal GR meter (-24dB to 0dB)
    draw_pixel_text(img, "GR", mx0 + 4, my0 + 4, COLOR_TEXT_MUTED)
    draw_pixel_box(img, mx0 + 20, my0 + 4, mx1 - 4, my0 + 10, COLOR_BG_VOID, COLOR_BORDER_DARK)
    for gx in range(mx0 + 21, mx0 + 60):
        for gy in range(my0 + 5, my0 + 10):
            img.putpixel((gx, gy), COLOR_CRIMSON_BRIGHT if gx > mx0 + 48 else COLOR_CRIMSON_DIM)

    # 3-band vertical ladders
    bands = ["LOW", "MID", "HIGH"]
    band_w = (mx1 - mx0 - 6) // 3
    for b_idx, b_name in enumerate(bands):
        bx0 = mx0 + 4 + b_idx * band_w
        level_fill = [28, 36, 22][b_idx]
        for y_step in range(40):
            my = my1 - 16 - y_step
            mc = COLOR_CRIMSON_BRIGHT if y_step > 35 else (COLOR_AMBER_BRIGHT if y_step > 26 else COLOR_ACID_BASE)
            col = mc if y_step < level_fill else COLOR_PANEL
            for px in range(bx0, bx0 + 6): img.putpixel((px, my), col)
        draw_pixel_text(img, b_name[0], bx0 + 4, my1 - 10, COLOR_TEXT_MUTED)

    draw_card_knob(img, 144, 60, 13, "THRESH", "-18DB", angle=-0.5)
    draw_card_knob(img, 196, 60, 13, "RATIO", "4:1", angle=0.2)
    draw_card_knob(img, 248, 60, 13, "ATTACK", "15MS", angle=-0.2)
    draw_card_knob(img, 300, 60, 13, "REL", "120MS", angle=0.4)
    draw_card_knob(img, 352, 60, 13, "GAIN", "+3.5DB", angle=0.3)
    draw_card_knob(img, 398, 60, 13, "MIX", "100%", angle=0.9)
    img.save(output_path, "PNG")
    print(f"Generated Compressor Module: {output_path}")


# 3. Delay
def generate_fx_delay(output_path: str):
    img = create_fx_card_base("DELAY", "PING-PONG SYNC")
    dx0, dy0, dx1, dy1 = 8, 24, 114, 102
    draw_pixel_box(img, dx0, dy0, dx1, dy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Sync grid & tap echo stems
    draw_pixel_text(img, "STEREO PING-PONG", dx0 + 4, dy0 + 4, COLOR_TEXT_ACID)
    for x in range(dx0 + 16, dx1 - 4, 18):
        for y in range(dy0 + 16, dy1 - 4, 4): img.putpixel((x, y), COLOR_GRID)

    # Alternating L/R impulse stems decaying exponentially
    taps = [(dx0 + 20, 48, True), (dx0 + 38, 38, False), (dx0 + 56, 30, True), (dx0 + 74, 22, False), (dx0 + 92, 14, True)]
    for (tx, h, is_left) in taps:
        stem_c = COLOR_ACID_BASE if is_left else COLOR_PURPLE_HOT
        for y in range(dy1 - 8 - h, dy1 - 8):
            img.putpixel((tx, y), stem_c)
        img.putpixel((tx, dy1 - 8 - h), COLOR_ACID_HOT if is_left else COLOR_TEXT_BRIGHT)
        draw_pixel_text(img, "L" if is_left else "R", tx - 2, dy1 - 6, COLOR_TEXT_MUTED)

    draw_card_knob(img, 144, 60, 13, "TIME L", "1/8D", angle=0.2)
    draw_card_knob(img, 196, 60, 13, "TIME R", "1/4", angle=0.5)
    draw_card_knob(img, 248, 60, 13, "FEEDBK", "55%", angle=0.4)
    draw_card_knob(img, 300, 60, 13, "X-FEED", "30%", angle=0.1)
    draw_card_knob(img, 352, 60, 13, "DAMP", "6.5K", angle=-0.1)
    draw_card_knob(img, 398, 60, 13, "MIX", "45%", angle=0.3)
    img.save(output_path, "PNG")
    print(f"Generated Delay Module: {output_path}")


# 4. Reverb
def generate_fx_reverb(output_path: str):
    img = create_fx_card_base("REVERB", "HALL / PLATE")
    rx0, ry0, rx1, ry1 = 8, 24, 114, 102
    draw_pixel_box(img, rx0, ry0, rx1, ry1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "IMPULSE DECAY", rx0 + 4, ry0 + 4, COLOR_TEXT_ACID)

    # Early reflection spikes + exponential diffuse tail
    base_y = ry1 - 10
    early_spikes = [(rx0 + 12, 38), (rx0 + 18, 30), (rx0 + 26, 26), (rx0 + 34, 22)]
    for (sx, sh) in early_spikes:
        for y in range(base_y - sh, base_y): img.putpixel((sx, y), COLOR_ACID_HOT)

    # Diffuse cloud envelope
    for x in range(rx0 + 36, rx1 - 4):
        t = (x - (rx0 + 36)) / float(rx1 - rx0 - 40)
        env_h = int(24 * math.exp(-t * 2.8))
        for y in range(base_y - env_h, base_y):
            if (x + y) % 2 == 0:
                img.putpixel((x, y), COLOR_PURPLE_SHADE if t > 0.4 else COLOR_PURPLE_BASE)
        img.putpixel((x, base_y - env_h), COLOR_PURPLE_HOT)

    draw_pixel_text(img, "2.8 SEC", rx0 + 4, ry1 - 8, COLOR_TEXT_MUTED)
    draw_card_knob(img, 144, 60, 13, "PRE-DLY", "20MS", angle=-0.4)
    draw_card_knob(img, 196, 60, 13, "SIZE", "75%", angle=0.6)
    draw_card_knob(img, 248, 60, 13, "DECAY", "2.8S", angle=0.5)
    draw_card_knob(img, 300, 60, 13, "DAMP", "4.0K", angle=0.2)
    draw_card_knob(img, 352, 60, 13, "WIDTH", "120%", angle=0.7)
    draw_card_knob(img, 398, 60, 13, "MIX", "35%", angle=-0.1)
    img.save(output_path, "PNG")
    print(f"Generated Reverb Module: {output_path}")


# 5. Equalizer
def generate_fx_equalizer(output_path: str):
    img = create_fx_card_base("EQUALIZER", "4-BAND PARAMETRIC")
    eq_x0, eq_y0, eq_x1, eq_y1 = 8, 24, 150, 102
    draw_pixel_box(img, eq_x0, eq_y0, eq_x1, eq_y1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # 0dB Axis & Frequency Grids (100Hz, 1kHz, 10kHz)
    mid_y = (eq_y0 + eq_y1) // 2
    for x in range(eq_x0 + 4, eq_x1 - 3, 2): img.putpixel((x, mid_y), COLOR_BORDER_MED)
    for gx in (eq_x0 + 32, eq_x0 + 72, eq_x0 + 114):
        for gy in range(eq_y0 + 4, eq_y1 - 3, 4): img.putpixel((gx, gy), COLOR_GRID)

    # Composite EQ curve with 4 draggable node squares
    nodes = [(eq_x0 + 20, mid_y - 12), (eq_x0 + 55, mid_y + 14), (eq_x0 + 95, mid_y - 18), (eq_x0 + 130, mid_y - 8)]
    for x in range(eq_x0 + 4, eq_x1 - 4):
        rel_x = x - eq_x0
        # Multi-bell synthetic curve
        b1 = -12.0 * math.exp(-math.pow((rel_x - 20) / 16.0, 2))
        b2 = 14.0 * math.exp(-math.pow((rel_x - 55) / 18.0, 2))
        b3 = -18.0 * math.exp(-math.pow((rel_x - 95) / 20.0, 2))
        b4 = -8.0 * math.exp(-math.pow((rel_x - 130) / 22.0, 2))
        py = int(round(mid_y + b1 + b2 + b3 + b4))
        img.putpixel((x, py), COLOR_ACID_BASE)

    # 4 Node Handles
    for (nx, ny) in nodes:
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                img.putpixel((nx + dx, ny + dy), COLOR_ACID_HOT if (abs(dx) == 2 or abs(dy) == 2) else COLOR_BG_VOID)

    draw_pixel_text(img, "4-BAND EQ", eq_x0 + 4, eq_y0 + 4, COLOR_TEXT_ACID)
    draw_card_knob(img, 185, 60, 13, "B1 GAIN", "+4DB", angle=0.4)
    draw_card_knob(img, 230, 60, 13, "B2 FREQ", "450HZ", angle=-0.2)
    draw_card_knob(img, 275, 60, 13, "B2 GAIN", "-3DB", angle=-0.3)
    draw_card_knob(img, 320, 60, 13, "B3 FREQ", "3.2K", angle=0.5)
    draw_card_knob(img, 365, 60, 13, "B3 GAIN", "+6DB", angle=0.6)
    draw_card_knob(img, 402, 60, 11, "HPF", "35HZ", angle=-0.8)
    img.save(output_path, "PNG")
    print(f"Generated Equalizer Module: {output_path}")


# 6. Filter (FX)
def generate_fx_filter(output_path: str):
    img = create_fx_card_base("FILTER", "BANDPASS 12DB")
    fx0, fy0, fx1, fy1 = 8, 24, 114, 102
    draw_pixel_box(img, fx0, fy0, fx1, fy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Resonant bandpass curve
    peak_x, peak_y = fx0 + 58, fy0 + 16
    for x in range(fx0 + 4, fx1 - 4):
        dx = (x - peak_x) / 14.0
        py = int(min(fy1 - 6, peak_y + dx * dx * 16.0))
        for yf in range(py, fy1 - 6):
            if (x + yf) % 2 == 0: img.putpixel((x, yf), COLOR_ACID_SHADOW)
        img.putpixel((x, py), COLOR_ACID_BASE)

    # Center node handle
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            img.putpixel((peak_x + dx, peak_y + dy), COLOR_ACID_HOT if (abs(dx) == 2 or abs(dy) == 2) else COLOR_BG_VOID)

    draw_pixel_text(img, "CUT: 1.85K", fx0 + 4, fy0 + 4, COLOR_TEXT_ACID)
    draw_card_knob(img, 144, 60, 13, "CUTOFF", "1.85K", angle=0.3)
    draw_card_knob(img, 196, 60, 13, "RES", "4.2", angle=0.5)
    draw_card_knob(img, 248, 60, 13, "DRIVE", "+6DB", angle=0.2)
    draw_card_knob(img, 300, 60, 13, "PAN", "C", angle=0.0)
    draw_card_knob(img, 352, 60, 13, "FAT", "100%", angle=0.8)
    draw_card_knob(img, 398, 60, 13, "MIX", "100%", angle=0.9)
    img.save(output_path, "PNG")
    print(f"Generated Filter Module: {output_path}")


# 7. Splitter (L/H, L/M/H, MB)
def generate_fx_splitter(output_path: str):
    img = create_fx_card_base("SPLITTER", "SPLIT L/M/H")
    sx0, sy0, sx1, sy1 = 8, 24, 160, 102
    draw_pixel_box(img, sx0, sy0, sx1, sy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    # Split boundaries at 210 Hz and 1000 Hz
    s1_x = sx0 + 48
    s2_x = sx0 + 104

    # 3 Color-coded split bands
    for x in range(sx0 + 4, sx1 - 4):
        for y in range(sy0 + 16, sy1 - 16):
            if (x + y) % 3 == 0:
                if x < s1_x:
                    img.putpixel((x, y), COLOR_PURPLE_SHADE)
                elif x < s2_x:
                    img.putpixel((x, y), COLOR_PURPLE_BASE)
                else:
                    img.putpixel((x, y), COLOR_PURPLE_HOT)

    # Vertical split boundary bars
    for y in range(sy0 + 4, sy1 - 4):
        img.putpixel((s1_x, y), COLOR_ACID_BASE)
        img.putpixel((s1_x + 1, y), COLOR_ACID_HOT)
        img.putpixel((s2_x, y), COLOR_ACID_BASE)
        img.putpixel((s2_x + 1, y), COLOR_ACID_HOT)

    draw_pixel_text(img, "BAND SPLIT", sx0 + 4, sy0 + 4, COLOR_TEXT_ACID)
    draw_pixel_text(img, "210HZ", s1_x - 14, sy1 - 10, COLOR_TEXT_BRIGHT)
    draw_pixel_text(img, "1000HZ", s2_x - 18, sy1 - 10, COLOR_TEXT_BRIGHT)

    # Solo/Mute toggle buttons for each band
    draw_pixel_text(img, "L [S]", sx0 + 6, sy0 + 20, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "M [S]", s1_x + 6, sy0 + 20, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "H [S]", s2_x + 6, sy0 + 20, COLOR_TEXT_MUTED)

    draw_card_knob(img, 198, 60, 13, "GAIN L", "0.0DB", angle=0.0)
    draw_card_knob(img, 252, 60, 13, "GAIN M", "+1.5DB", angle=0.2)
    draw_card_knob(img, 306, 60, 13, "GAIN H", "-2.0DB", angle=-0.3)
    draw_card_knob(img, 360, 60, 13, "X-OVER", "210/1K", angle=0.1)
    draw_card_knob(img, 402, 60, 11, "MSTR", "0.0DB", angle=0.0)
    img.save(output_path, "PNG")
    print(f"Generated Splitter Module: {output_path}")


# 8. Chorus
def generate_fx_chorus(output_path: str):
    img = create_fx_card_base("CHORUS", "4-VOICE DIM")
    cx0, cy0, cx1, cy1 = 8, 24, 114, 102
    draw_pixel_box(img, cx0, cy0, cx1, cy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "MULTI-PHASE LFO", cx0 + 4, cy0 + 4, COLOR_TEXT_ACID)

    # 4 Out-of-phase sine curves
    colors = [COLOR_PURPLE_DIM, COLOR_PURPLE_BASE, COLOR_PURPLE_HOT, COLOR_ACID_BASE]
    mid_y = (cy0 + cy1) // 2 + 4
    for idx, col in enumerate(colors):
        phase_offset = idx * (math.pi / 2.0)
        for x in range(cx0 + 4, cx1 - 4):
            t = (x - cx0) / 14.0 + phase_offset
            py = int(round(mid_y + math.sin(t) * 16.0))
            img.putpixel((x, py), col)

    draw_card_knob(img, 144, 60, 13, "RATE", "0.5HZ", angle=-0.3)
    draw_card_knob(img, 196, 60, 13, "DEPTH", "65%", angle=0.4)
    draw_card_knob(img, 248, 60, 13, "VOICES", "4", angle=0.0)
    draw_card_knob(img, 300, 60, 13, "DELAY", "12MS", angle=-0.2)
    draw_card_knob(img, 352, 60, 13, "FEEDBK", "30%", angle=0.1)
    draw_card_knob(img, 398, 60, 13, "MIX", "50%", angle=0.0)
    img.save(output_path, "PNG")
    print(f"Generated Chorus Module: {output_path}")


# 9. Flanger
def generate_fx_flanger(output_path: str):
    img = create_fx_card_base("FLANGER", "THROUGH-ZERO")
    fx0, fy0, fx1, fy1 = 8, 24, 114, 102
    draw_pixel_box(img, fx0, fy0, fx1, fy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "COMB NOTCHES", fx0 + 4, fy0 + 4, COLOR_TEXT_ACID)

    # Comb notch series
    for x in range(fx0 + 4, fx1 - 4):
        t = (x - fx0) / 8.0
        py = int(fy0 + 18 + (1.0 - math.pow(math.cos(t), 4.0)) * 40.0)
        img.putpixel((x, min(fy1 - 6, py)), COLOR_PURPLE_HOT)

    draw_card_knob(img, 144, 60, 13, "RATE", "0.25HZ", angle=-0.6)
    draw_card_knob(img, 196, 60, 13, "DEPTH", "80%", angle=0.6)
    draw_card_knob(img, 248, 60, 13, "FEEDBK", "+75%", angle=0.7)
    draw_card_knob(img, 300, 60, 13, "PHASE", "90 DEG", angle=0.2)
    draw_card_knob(img, 352, 60, 13, "COLOR", "WARM", angle=-0.1)
    draw_card_knob(img, 398, 60, 13, "MIX", "50%", angle=0.0)
    img.save(output_path, "PNG")
    print(f"Generated Flanger Module: {output_path}")


# 10. Phaser
def generate_fx_phaser(output_path: str):
    img = create_fx_card_base("PHASER", "8-STAGE ALLPASS")
    px0, py0, px1, py1 = 8, 24, 114, 102
    draw_pixel_box(img, px0, py0, px1, py1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "ALLPASS POLES", px0 + 4, py0 + 4, COLOR_TEXT_ACID)

    # 4 Sweep notches
    for notch_x in (px0 + 20, px0 + 44, px0 + 68, px0 + 92):
        for x in range(notch_x - 10, notch_x + 11):
            if px0 + 4 <= x < px1 - 4:
                dist = abs(x - notch_x)
                py = int(py0 + 22 + (1.0 - dist / 10.0) * 36.0)
                img.putpixel((x, py), COLOR_ACID_BASE)

    draw_card_knob(img, 144, 60, 13, "RATE", "0.8HZ", angle=-0.1)
    draw_card_knob(img, 196, 60, 13, "DEPTH", "70%", angle=0.5)
    draw_card_knob(img, 248, 60, 13, "FREQ", "1.2KHZ", angle=0.3)
    draw_card_knob(img, 300, 60, 13, "FEEDBK", "60%", angle=0.4)
    draw_card_knob(img, 352, 60, 13, "STAGES", "8 POLE", angle=0.0)
    draw_card_knob(img, 398, 60, 13, "MIX", "50%", angle=0.0)
    img.save(output_path, "PNG")
    print(f"Generated Phaser Module: {output_path}")


# 11. Hyper / Dimension
def generate_fx_hyper_dimension(output_path: str):
    img = create_fx_card_base("HYPER / DIM", "UNISON SPREAD")
    hx0, hy0, hx1, hy1 = 8, 24, 114, 102
    draw_pixel_box(img, hx0, hy0, hx1, hy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "STEREO MATRIX", hx0 + 4, hy0 + 4, COLOR_TEXT_ACID)

    # Micro-detune cluster dots
    center_x, center_y = (hx0 + hx1) // 2, (hy0 + hy1) // 2 + 4
    cluster = [(-22, -10), (-14, 6), (-6, -4), (0, 0), (8, 5), (16, -8), (24, 7)]
    for (dx, dy) in cluster:
        for py in range(center_y + dy - 1, center_y + dy + 2):
            for px in range(center_x + dx - 1, center_x + dx + 2):
                img.putpixel((px, py), COLOR_ACID_HOT)

    draw_card_knob(img, 144, 60, 13, "HYP RATE", "0.5HZ", angle=-0.3)
    draw_card_knob(img, 196, 60, 13, "DETUNE", "25 CENT", angle=0.2)
    draw_card_knob(img, 248, 60, 13, "UNISON", "4 VOX", angle=0.0)
    draw_card_knob(img, 300, 60, 13, "DIM SIZE", "65%", angle=0.4)
    draw_card_knob(img, 352, 60, 13, "DIM MIX", "40%", angle=-0.1)
    draw_card_knob(img, 398, 60, 13, "MASTER", "100%", angle=0.9)
    img.save(output_path, "PNG")
    print(f"Generated Hyper/Dimension Module: {output_path}")


# 12. Bode (Frequency Shifter)
def generate_fx_bode(output_path: str):
    img = create_fx_card_base("BODE", "LINEAR FREQ SHIFT")
    bx0, by0, bx1, by1 = 8, 24, 114, 102
    draw_pixel_box(img, bx0, by0, bx1, by1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "SHIFT SPECTRUM", bx0 + 4, by0 + 4, COLOR_TEXT_ACID)

    # Shifted sidebands
    mid_x = (bx0 + bx1) // 2
    for x in range(bx0 + 6, bx1 - 6):
        h1 = int(28 * math.exp(-math.pow((x - (mid_x - 18)) / 8.0, 2)))
        h2 = int(28 * math.exp(-math.pow((x - (mid_x + 22)) / 8.0, 2)))
        for y in range(by1 - 10 - (h1 + h2), by1 - 10):
            img.putpixel((x, y), COLOR_ACID_BASE)

    draw_pixel_text(img, "-250HZ", bx0 + 6, by1 - 8, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "+350HZ", bx1 - 42, by1 - 8, COLOR_TEXT_MUTED)

    draw_card_knob(img, 144, 60, 13, "SHIFT", "+120HZ", angle=0.3)
    draw_card_knob(img, 196, 60, 13, "FEEDBK", "35%", angle=0.1)
    draw_card_knob(img, 248, 60, 13, "DRIVE", "0.0DB", angle=0.0)
    draw_card_knob(img, 300, 60, 13, "DELAY", "15MS", angle=-0.2)
    draw_card_knob(img, 352, 60, 13, "LOW CUT", "80HZ", angle=-0.6)
    draw_card_knob(img, 398, 60, 13, "MIX", "100%", angle=0.9)
    img.save(output_path, "PNG")
    print(f"Generated Bode Module: {output_path}")


# 13. Convolve
def generate_fx_convolve(output_path: str):
    img = create_fx_card_base("CONVOLVE", "SPRING IR SAMPLE")
    cx0, cy0, cx1, cy1 = 8, 24, 114, 102
    draw_pixel_box(img, cx0, cy0, cx1, cy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "IR WAVEFORM", cx0 + 4, cy0 + 4, COLOR_TEXT_ACID)

    # Impulse waveform sample
    mid_y = (cy0 + cy1) // 2 + 4
    for x in range(cx0 + 4, cx1 - 4):
        rel_x = x - (cx0 + 4)
        if rel_x < 8:
            py = mid_y # Pre-delay gap
        else:
            t = (rel_x - 8) / 3.0
            amp = math.exp(-t * 0.12) * 20.0
            py = int(mid_y + math.sin(t * 4.0) * amp)
        img.putpixel((x, py), COLOR_PURPLE_HOT)

    draw_card_knob(img, 144, 60, 13, "PRE-DLY", "8MS", angle=-0.5)
    draw_card_knob(img, 196, 60, 13, "DECAY", "1.6S", angle=0.2)
    draw_card_knob(img, 248, 60, 13, "DAMP", "5.0K", angle=0.1)
    draw_card_knob(img, 300, 60, 13, "LO-CUT", "120HZ", angle=-0.4)
    draw_card_knob(img, 352, 60, 13, "HI-CUT", "8.5K", angle=0.4)
    draw_card_knob(img, 398, 60, 13, "MIX", "40%", angle=-0.1)
    img.save(output_path, "PNG")
    print(f"Generated Convolve Module: {output_path}")


# 14. Utility
def generate_fx_utility(output_path: str):
    img = create_fx_card_base("UTILITY", "STEREO TOOL")
    ux0, uy0, ux1, uy1 = 8, 24, 114, 102
    draw_pixel_box(img, ux0, uy0, ux1, uy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "PHASE & WIDTH", ux0 + 4, uy0 + 4, COLOR_TEXT_ACID)

    # Goniometer / Lissajous vector display
    mid_x, mid_y = (ux0 + ux1) // 2, (uy0 + uy1) // 2 + 4
    for i in range(120):
        t = i / 120.0 * 2.0 * math.pi
        px = int(round(mid_x + math.sin(t) * 22.0 + math.sin(t * 3.0) * 6.0))
        py = int(round(mid_y + math.cos(t) * 16.0))
        img.putpixel((px, py), COLOR_ACID_BASE)

    # Phase Invert and DC buttons
    draw_pixel_box(img, ux0 + 4, uy1 - 14, ux0 + 36, uy1 - 4, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "INV L", ux0 + 6, uy1 - 12, COLOR_TEXT_MUTED)
    draw_pixel_box(img, ux0 + 40, uy1 - 14, ux0 + 72, uy1 - 4, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "INV R", ux0 + 42, uy1 - 12, COLOR_TEXT_MUTED)

    draw_card_knob(img, 144, 60, 13, "GAIN L", "0.0DB", angle=0.0)
    draw_card_knob(img, 196, 60, 13, "GAIN R", "0.0DB", angle=0.0)
    draw_card_knob(img, 248, 60, 13, "PAN", "C", angle=0.0)
    draw_card_knob(img, 300, 60, 13, "WIDTH", "140%", angle=0.6)
    draw_card_knob(img, 352, 60, 13, "BASS MONO", "120HZ", angle=-0.3)
    draw_card_knob(img, 398, 60, 13, "MSTR", "0.0DB", angle=0.0)
    img.save(output_path, "PNG")
    print(f"Generated Utility Module: {output_path}")


# ==============================================================================
# 11. FX SPLITTER ROUTING CONTAINERS (L/H, L/M/H, MID/SIDE)
# ==============================================================================
def generate_fx_splitter_containers(output_dir: str):
    # 1. 3-Band L/M/H Splitter Container (600 x 120 px)
    w, h = 600, 120
    img3 = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(img3, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Top Header Strip
    draw_pixel_box(img3, 6, 6, 14, 14, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 13):
        for px in range(8, 13): img3.putpixel((px, py), COLOR_ACID_BASE)
    img3.putpixel((10, 10), COLOR_ACID_HOT)

    for dy in (0, 3, 6):
        img3.putpixel((18, 7 + dy), COLOR_BORDER_LGT)
        img3.putpixel((20, 7 + dy), COLOR_BORDER_LGT)

    draw_pixel_text(img3, "SPLITTER CONTAINER: L/M/H", 26, 7, COLOR_TEXT_ACID)

    # Crossover selector boxes
    draw_pixel_box(img3, 210, 4, 290, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img3, "X-OVER 1: 210HZ", 214, 8, COLOR_TEXT_BRIGHT)

    draw_pixel_box(img3, 296, 4, 386, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img3, "X-OVER 2: 1000HZ", 300, 8, COLOR_TEXT_BRIGHT)

    draw_card_knob(img3, 440, 11, 8, "WIDTH", "100%", angle=0.0)
    draw_card_knob(img3, 490, 11, 8, "MIX", "100%", angle=0.8)
    draw_card_knob(img3, 540, 11, 8, "MSTR", "0.0DB", angle=0.0)

    draw_pixel_box(img3, w - 36, 4, w - 22, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img3, "S", w - 32, 8, COLOR_TEXT_MUTED)
    draw_pixel_box(img3, w - 18, 4, w - 4, 18, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img3, "M", w - 14, 8, COLOR_TEXT_MUTED)

    # Top Crossover Bar (y: 22 to 34 px)
    draw_pixel_box(img3, 6, 22, w - 7, 34, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    s1_x, s2_x = 200, 400
    for x in range(7, w - 7):
        for y in range(23, 34):
            if (x + y) % 3 == 0:
                if x < s1_x:
                    img3.putpixel((x, y), COLOR_PURPLE_SHADE)
                elif x < s2_x:
                    img3.putpixel((x, y), COLOR_PURPLE_BASE)
                else:
                    img3.putpixel((x, y), COLOR_PURPLE_HOT)

    for (cx, label) in [(s1_x, "210 HZ"), (s2_x, "1000 HZ")]:
        for y in range(22, 35):
            img3.putpixel((cx, y), COLOR_ACID_BASE)
            img3.putpixel((cx + 1, y), COLOR_ACID_HOT)
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                if abs(dx) + abs(dy) <= 3:
                    img3.putpixel((cx + dx, 28 + dy), COLOR_ACID_HOT)

    # 3 Parallel Processing Lanes (y: 38 to 114 px)
    lanes = [
        ("LOW (< 210 HZ)", 6, 198, COLOR_PURPLE_HOT, "COMPRESSOR (MB)", COLOR_PURPLE_BASE, "0.0DB"),
        ("MIDS (210 - 1000 HZ)", 204, 396, COLOR_ACID_BASE, "DISTORTION (TUBE)", COLOR_ACID_BASE, "+1.5DB"),
        ("HIGHS (> 1000 HZ)", 402, 594, COLOR_ACID_HOT, "CHORUS (DIM 4)", COLOR_ACID_HOT, "-1.0DB")
    ]

    for (lane_title, lx0, lx1, badge_c, fx_name, bar_c, gain_str) in lanes:
        draw_pixel_box(img3, lx0, 38, lx1, 114, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_LGT)
        draw_pixel_box(img3, lx0 + 2, 40, lx1 - 2, 54, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
        draw_pixel_text(img3, lane_title, lx0 + 6, 44, badge_c)

        draw_pixel_box(img3, lx1 - 32, 42, lx1 - 18, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img3, "S", lx1 - 29, 44, COLOR_TEXT_MUTED)
        draw_pixel_box(img3, lx1 - 16, 42, lx1 - 2, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img3, "M", lx1 - 13, 44, COLOR_TEXT_MUTED)

        draw_pixel_box(img3, lx0 + 4, 58, lx1 - 48, 110, COLOR_BG_VOID, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        for py in range(62, 67):
            for px in range(lx0 + 8, lx0 + 13): img3.putpixel((px, py), COLOR_ACID_BASE)
        draw_pixel_text(img3, fx_name, lx0 + 16, 62, COLOR_TEXT_BRIGHT)

        draw_pixel_box(img3, lx0 + 8, 76, lx1 - 54, 82, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        fill_w = int((lx1 - lx0 - 62) * 0.75)
        for fx in range(lx0 + 9, lx0 + 9 + fill_w):
            for fy in range(77, 82): img3.putpixel((fx, fy), bar_c)

        draw_pixel_text(img3, "INSERT FX +", lx0 + 16, 94, COLOR_TEXT_MUTED)
        draw_card_knob(img3, lx1 - 24, 76, 12, "GAIN", gain_str, angle=0.2)
        draw_card_knob(img3, lx1 - 24, 102, 6, "PAN", "C", angle=0.0)

    p3 = os.path.join(output_dir, "panel_fx_splitter_lmh_container_600x120.png")
    img3.save(p3, "PNG")
    p_generic = os.path.join(output_dir, "fx_splitter_container_600x120.png")
    img3.save(p_generic, "PNG")
    print(f"Generated Splitter L/M/H Container: {p3}")

    # 2. 2-Band L/H Splitter Container (600 x 120 px)
    img2 = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(img2, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    draw_pixel_box(img2, 6, 6, 14, 14, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 13):
        for px in range(8, 13): img2.putpixel((px, py), COLOR_ACID_BASE)
    img2.putpixel((10, 10), COLOR_ACID_HOT)

    for dy in (0, 3, 6):
        img2.putpixel((18, 7 + dy), COLOR_BORDER_LGT)
        img2.putpixel((20, 7 + dy), COLOR_BORDER_LGT)

    draw_pixel_text(img2, "SPLITTER CONTAINER: L/H", 26, 7, COLOR_TEXT_ACID)

    draw_pixel_box(img2, 240, 4, 340, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img2, "CROSSOVER: 350HZ", 244, 8, COLOR_TEXT_BRIGHT)

    draw_card_knob(img2, 450, 11, 8, "WIDTH", "100%", angle=0.0)
    draw_card_knob(img2, 500, 11, 8, "MIX", "100%", angle=0.8)
    draw_card_knob(img2, 550, 11, 8, "MSTR", "0.0DB", angle=0.0)

    draw_pixel_box(img2, 6, 22, w - 7, 34, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    split_x = 300
    for x in range(7, w - 7):
        for y in range(23, 34):
            if (x + y) % 3 == 0:
                img2.putpixel((x, y), COLOR_PURPLE_SHADE if x < split_x else COLOR_PURPLE_HOT)

    for y in range(22, 35):
        img2.putpixel((split_x, y), COLOR_ACID_BASE)
        img2.putpixel((split_x + 1, y), COLOR_ACID_HOT)
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            if abs(dx) + abs(dy) <= 3:
                img2.putpixel((split_x + dx, 28 + dy), COLOR_ACID_HOT)

    lanes2 = [
        ("LOW LANE (< 350 HZ)", 6, 296, COLOR_PURPLE_HOT, "COMPRESSOR (TIGHT)", COLOR_PURPLE_BASE, "0.0DB"),
        ("HIGH LANE (> 350 HZ)", 304, 594, COLOR_ACID_BASE, "REVERB / FLANGER", COLOR_ACID_BASE, "+0.5DB")
    ]

    for (lane_title, lx0, lx1, badge_c, fx_name, bar_c, gain_str) in lanes2:
        draw_pixel_box(img2, lx0, 38, lx1, 114, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_LGT)
        draw_pixel_box(img2, lx0 + 2, 40, lx1 - 2, 54, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
        draw_pixel_text(img2, lane_title, lx0 + 6, 44, badge_c)

        draw_pixel_box(img2, lx1 - 32, 42, lx1 - 18, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img2, "S", lx1 - 29, 44, COLOR_TEXT_MUTED)
        draw_pixel_box(img2, lx1 - 16, 42, lx1 - 2, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img2, "M", lx1 - 13, 44, COLOR_TEXT_MUTED)

        draw_pixel_box(img2, lx0 + 4, 58, lx1 - 64, 110, COLOR_BG_VOID, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        for py in range(62, 67):
            for px in range(lx0 + 8, lx0 + 13): img2.putpixel((px, py), COLOR_ACID_BASE)
        draw_pixel_text(img2, fx_name, lx0 + 16, 62, COLOR_TEXT_BRIGHT)

        draw_pixel_box(img2, lx0 + 8, 76, lx1 - 74, 82, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        fill_w = int((lx1 - lx0 - 82) * 0.70)
        for fx in range(lx0 + 9, lx0 + 9 + fill_w):
            for fy in range(77, 82): img2.putpixel((fx, fy), bar_c)

        draw_pixel_text(img2, "INSERT FX +", lx0 + 16, 94, COLOR_TEXT_MUTED)
        draw_card_knob(img2, lx1 - 32, 74, 13, "GAIN", gain_str, angle=0.2)
        draw_card_knob(img2, lx1 - 32, 102, 6, "PAN", "C", angle=0.0)

    p2 = os.path.join(output_dir, "panel_fx_splitter_lh_container_600x120.png")
    img2.save(p2, "PNG")
    print(f"Generated Splitter L/H Container: {p2}")

    # 3. Mid/Side Splitter Container (600 x 120 px)
    img_ms = Image.new("RGBA", (w, h), COLOR_BG_CHASSIS)
    draw_pixel_box(img_ms, 0, 0, w - 1, h - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    draw_pixel_box(img_ms, 6, 6, 14, 14, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for py in range(8, 13):
        for px in range(8, 13): img_ms.putpixel((px, py), COLOR_ACID_BASE)
    img_ms.putpixel((10, 10), COLOR_ACID_HOT)

    for dy in (0, 3, 6):
        img_ms.putpixel((18, 7 + dy), COLOR_BORDER_LGT)
        img_ms.putpixel((20, 7 + dy), COLOR_BORDER_LGT)

    draw_pixel_text(img_ms, "SPLITTER CONTAINER: MID/SIDE", 26, 7, COLOR_TEXT_ACID)

    draw_pixel_box(img_ms, 240, 4, 340, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img_ms, "MATRIX: M/S ENCODE", 244, 8, COLOR_TEXT_BRIGHT)

    draw_card_knob(img_ms, 450, 11, 8, "M/S BAL", "0%", angle=0.0)
    draw_card_knob(img_ms, 500, 11, 8, "WIDTH", "125%", angle=0.5)
    draw_card_knob(img_ms, 550, 11, 8, "MSTR", "0.0DB", angle=0.0)

    draw_pixel_box(img_ms, 6, 22, w - 7, 34, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img_ms, "MONO SUM (L+R)", 50, 26, COLOR_TEXT_BRIGHT)
    draw_pixel_text(img_ms, "STEREO DIFFERENCE (L-R)", 350, 26, COLOR_TEXT_BRIGHT)
    for y in range(22, 35): img_ms.putpixel((300, y), COLOR_ACID_BASE)

    lanes_ms = [
        ("MID LANE (SUM)", 6, 296, COLOR_ACID_BASE, "EQUALIZER / COMP", COLOR_ACID_BASE, "+1.0DB"),
        ("SIDE LANE (DIFF)", 304, 594, COLOR_PURPLE_HOT, "DELAY / REVERB", COLOR_PURPLE_HOT, "0.0DB")
    ]

    for (lane_title, lx0, lx1, badge_c, fx_name, bar_c, gain_str) in lanes_ms:
        draw_pixel_box(img_ms, lx0, 38, lx1, 114, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_LGT)
        draw_pixel_box(img_ms, lx0 + 2, 40, lx1 - 2, 54, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
        draw_pixel_text(img_ms, lane_title, lx0 + 6, 44, badge_c)

        draw_pixel_box(img_ms, lx1 - 32, 42, lx1 - 18, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img_ms, "S", lx1 - 29, 44, COLOR_TEXT_MUTED)
        draw_pixel_box(img_ms, lx1 - 16, 42, lx1 - 2, 52, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img_ms, "M", lx1 - 13, 44, COLOR_TEXT_MUTED)

        draw_pixel_box(img_ms, lx0 + 4, 58, lx1 - 64, 110, COLOR_BG_VOID, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        for py in range(62, 67):
            for px in range(lx0 + 8, lx0 + 13): img_ms.putpixel((px, py), COLOR_ACID_BASE)
        draw_pixel_text(img_ms, fx_name, lx0 + 16, 62, COLOR_TEXT_BRIGHT)

        draw_pixel_box(img_ms, lx0 + 8, 76, lx1 - 74, 82, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
        fill_w = int((lx1 - lx0 - 82) * 0.70)
        for fx in range(lx0 + 9, lx0 + 9 + fill_w):
            for fy in range(77, 82): img_ms.putpixel((fx, fy), bar_c)

        draw_pixel_text(img_ms, "INSERT FX +", lx0 + 16, 94, COLOR_TEXT_MUTED)
        draw_card_knob(img_ms, lx1 - 32, 74, 13, "GAIN", gain_str, angle=0.2)
        draw_card_knob(img_ms, lx1 - 32, 102, 6, "PAN", "C", angle=0.0)

    p_ms = os.path.join(output_dir, "panel_fx_splitter_ms_container_600x120.png")
    img_ms.save(p_ms, "PNG")
    print(f"Generated Splitter Mid/Side Container: {p_ms}")


# ==============================================================================
# 12. MODULATION MATRIX UI COMPONENTS
# ==============================================================================
def generate_matrix_grid_panel(output_path: str, width: int = 960, height: int = 360):
    img = Image.new("RGBA", (width, height), COLOR_BG_CHASSIS)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)

    # Top Header Strip
    draw_pixel_text(img, "MODULATION MATRIX", 10, 8, COLOR_TEXT_ACID)

    draw_pixel_box(img, 180, 5, 310, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "FILTER: ALL ROUTES", 184, 9, COLOR_TEXT_MUTED)

    draw_pixel_box(img, 320, 5, 410, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "ACTIVE: 07/32", 324, 9, COLOR_TEXT_BRIGHT)

    # Pagination
    draw_pixel_box(img, 420, 5, 510, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "<", 424, 9, COLOR_ACID_BASE)
    draw_pixel_text(img, "PAGE 01/04", 436, 9, COLOR_TEXT_ACID)
    draw_pixel_text(img, ">", 502, 9, COLOR_ACID_BASE)

    # Master Invert / Clear All buttons
    draw_pixel_box(img, width - 130, 5, width - 70, 19, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "INVERT ALL", width - 126, 9, COLOR_TEXT_MUTED)
    draw_pixel_box(img, width - 64, 5, width - 10, 19, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(img, "CLEAR", width - 54, 9, COLOR_CRIMSON_BRIGHT)

    # Column Headers (y: 24 to 38 px)
    cols = [
        ("SOURCE", 10, 130),
        ("->", 134, 148),
        ("DESTINATION", 152, 330),
        ("TYPE", 334, 394),
        ("CURVE", 398, 468),
        ("AMOUNT", 472, 652),
        ("POLARITY", 656, 736),
        ("SCALE/AUX", 740, 830),
        ("BYPASS", 834, 894),
        ("DEL", 898, 940)
    ]

    draw_pixel_box(img, 6, 24, width - 20, 38, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for (cname, cx0, cx1) in cols:
        draw_pixel_text(img, cname, cx0 + 4, 28, COLOR_TEXT_MUTED)
        for y in range(25, 38):
            img.putpixel((cx1, y), COLOR_BORDER_DARK)

    # 8 Rows (y: 40 to 352 px, each 38px tall)
    matrix_rows = [
        ("LFO 1", "FILTER CUTOFF", "LINEAR", "S-CURVE", 45, "BIPOLAR", "100%", True),
        ("ENV 2", "OSC A WT POS", "LINEAR", "EXP", 78, "UNIPOLAR", "100%", True),
        ("MACRO 1", "DIST DRIVE", "LINEAR", "LINEAR", 62, "UNIPOLAR", "100%", True),
        ("VELOCITY", "AMP LEVEL", "LINEAR", "LOG", 35, "UNIPOLAR", "100%", True),
        ("LFO 2", "OSC A PAN", "LINEAR", "S-CURVE", -50, "BIPOLAR", "100%", True),
        ("MOD WHEEL", "REVERB MIX", "LINEAR", "LINEAR", 90, "UNIPOLAR", "100%", False),
        ("KEY TRACK", "FILTER RES", "LINEAR", "LINEAR", 25, "UNIPOLAR", "100%", True),
        ("[ASSIGN SRC]", "[ASSIGN DEST]", "-", "-", 0, "-", "-", False)
    ]

    for r_idx, (src, dest, rtype, curve, amt, pol, aux, is_on) in enumerate(matrix_rows):
        ry0 = 40 + r_idx * 38
        ry1 = ry0 + 36
        is_empty = (src == "[ASSIGN SRC]")

        row_bg = COLOR_PANEL_SUNKEN if (r_idx % 2 == 0) else COLOR_BG_VOID
        draw_pixel_box(img, 6, ry0, width - 20, ry1, row_bg, COLOR_BORDER_DARK, COLOR_BORDER_MED)

        # Source Box
        sx0, sx1 = cols[0][1], cols[0][2] - 4
        s_bg = COLOR_BG_VOID if is_empty else COLOR_PANEL_RAISED
        s_txt_c = COLOR_TEXT_MUTED if is_empty else COLOR_TEXT_BRIGHT
        draw_pixel_box(img, sx0, ry0 + 7, sx1, ry1 - 7, s_bg, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img, src, sx0 + 4, ry0 + 13, s_txt_c)
        if not is_empty:
            draw_pixel_text(img, "V", sx1 - 8, ry0 + 13, COLOR_ACID_BASE)

        # Arrow
        draw_pixel_text(img, "->", cols[1][1] + 2, ry0 + 13, COLOR_PURPLE_HOT if not is_empty else COLOR_TEXT_MUTED)

        # Destination Box
        dx0, dx1 = cols[2][1], cols[2][2] - 4
        draw_pixel_box(img, dx0, ry0 + 7, dx1, ry1 - 7, s_bg, COLOR_BORDER_MED, COLOR_BORDER_DARK)
        draw_pixel_text(img, dest, dx0 + 4, ry0 + 13, s_txt_c)
        if not is_empty:
            draw_pixel_text(img, "V", dx1 - 8, ry0 + 13, COLOR_ACID_BASE)

        # Type
        draw_pixel_text(img, rtype, cols[3][1] + 4, ry0 + 13, COLOR_TEXT_MUTED)

        # Curve
        cx0, cx1 = cols[4][1], cols[4][2] - 4
        if not is_empty:
            draw_pixel_box(img, cx0, ry0 + 8, cx1, ry1 - 8, COLOR_PANEL, COLOR_BORDER_MED, COLOR_BORDER_DARK)
            draw_pixel_text(img, curve[:5], cx0 + 4, ry0 + 13, COLOR_PURPLE_HOT)
        else:
            draw_pixel_text(img, "-", cx0 + 12, ry0 + 13, COLOR_TEXT_MUTED)

        # Amount Horizontal Slider
        ax0, ax1 = cols[5][1], cols[5][2] - 8
        slider_w = ax1 - ax0 - 45
        if not is_empty:
            draw_pixel_box(img, ax0, ry0 + 11, ax0 + slider_w, ry0 + 23, COLOR_BG_VOID, COLOR_BORDER_DARK, COLOR_BORDER_MED)
            center_x = ax0 + slider_w // 2
            for ty in range(ry0 + 9, ry0 + 25): img.putpixel((center_x, ty), COLOR_BORDER_LGT)

            fill_len = int((slider_w // 2) * (abs(amt) / 100.0))
            if amt >= 0:
                for fx in range(center_x, center_x + fill_len):
                    for fy in range(ry0 + 13, ry0 + 22): img.putpixel((fx, fy), COLOR_ACID_BASE)
            else:
                for fx in range(center_x - fill_len, center_x):
                    for fy in range(ry0 + 13, ry0 + 22): img.putpixel((fx, fy), COLOR_CRIMSON_BRIGHT)

            amt_str = f"+{amt}%" if amt > 0 else f"{amt}%"
            draw_pixel_text(img, amt_str, ax0 + slider_w + 6, ry0 + 13, COLOR_ACID_HOT if amt > 0 else COLOR_CRIMSON_BRIGHT)
        else:
            draw_pixel_text(img, "0.0%", ax0 + 16, ry0 + 13, COLOR_TEXT_MUTED)

        # Polarity
        px0, px1 = cols[6][1], cols[6][2] - 4
        if not is_empty:
            pol_code = "BI" if pol == "BIPOLAR" else "UNI"
            pol_c = COLOR_ACID_BASE if pol == "BIPOLAR" else COLOR_PURPLE_BASE
            draw_pixel_box(img, px0, ry0 + 8, px1, ry1 - 8, COLOR_PANEL_RAISED, pol_c, COLOR_BORDER_DARK)
            draw_pixel_text(img, pol_code, px0 + 8, ry0 + 13, COLOR_TEXT_BRIGHT)
        else:
            draw_pixel_text(img, "-", px0 + 12, ry0 + 13, COLOR_TEXT_MUTED)

        # Scale/Aux
        draw_pixel_text(img, aux, cols[7][1] + 6, ry0 + 13, COLOR_TEXT_MUTED if is_empty else COLOR_TEXT_BRIGHT)

        # Bypass / Power
        bx0, bx1 = cols[8][1], cols[8][2] - 4
        if not is_empty:
            led_c = COLOR_ACID_BASE if is_on else COLOR_PURPLE_SHADE
            draw_pixel_box(img, bx0 + 6, ry0 + 10, bx0 + 22, ry1 - 10, COLOR_PANEL, COLOR_BORDER_DARK, COLOR_BORDER_MED)
            for py in range(ry0 + 12, ry1 - 12):
                for px in range(bx0 + 10, bx0 + 19):
                    img.putpixel((px, py), led_c)
            if is_on: img.putpixel((bx0 + 14, (ry0 + ry1) // 2), COLOR_ACID_HOT)
        else:
            draw_pixel_text(img, "-", bx0 + 10, ry0 + 13, COLOR_TEXT_MUTED)

        # Delete button 'X'
        if not is_empty:
            del_x = cols[9][1] + 4
            draw_pixel_box(img, del_x, ry0 + 9, del_x + 16, ry1 - 9, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
            draw_pixel_text(img, "X", del_x + 5, ry0 + 13, COLOR_CRIMSON_DIM)

    # Right Vertical Scrollbar
    sb_x0, sb_x1 = width - 16, width - 8
    draw_pixel_box(img, sb_x0, 24, sb_x1, height - 8, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_box(img, sb_x0 + 1, 34, sb_x1 - 1, 120, COLOR_PANEL_RAISED, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    for gy in (65, 75, 85):
        for gx in range(sb_x0 + 2, sb_x1 - 2):
            img.putpixel((gx, gy), COLOR_BORDER_MED)

    img.save(output_path, "PNG")
    print(f"Generated Modulation Matrix Master Panel: {output_path}")


def generate_matrix_slot_components(output_dir: str):
    # 1. Slot Selector Button (130 x 22 px)
    sw, sh = 130, 22
    slot_btn = Image.new("RGBA", (sw, sh), COLOR_TRANSPARENT)
    draw_pixel_box(slot_btn, 0, 0, sw - 1, sh - 1, COLOR_PANEL_RAISED, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(slot_btn, "LFO 1", 8, 8, COLOR_TEXT_BRIGHT)
    draw_pixel_text(slot_btn, "V", sw - 14, 8, COLOR_ACID_BASE)

    p_slot = os.path.join(output_dir, "matrix_slot_selector_130x22.png")
    slot_btn.save(p_slot, "PNG")
    print(f"Generated Matrix Slot Selector: {p_slot}")

    # 2. Source/Destination Dropdown Menu (140 x 196 px)
    mw, mh = 140, 196
    menu = Image.new("RGBA", (mw, mh), COLOR_BG_VOID)
    draw_pixel_box(menu, 0, 0, mw - 1, mh - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    draw_pixel_text(menu, "SELECT ROUTE SRC", 6, 6, COLOR_TEXT_MUTED)

    src_list = [
        "LFO 1", "LFO 2", "LFO 3", "LFO 4",
        "ENV 1", "ENV 2", "ENV 3", "ENV 4",
        "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4",
        "MOD WHEEL", "PITCH BEND", "VELOCITY"
    ]

    row_h = 11
    for idx, sname in enumerate(src_list):
        ry0 = 18 + idx * 11
        ry1 = ry0 + 10
        is_sel = (sname == "LFO 1")
        if is_sel:
            draw_pixel_box(menu, 2, ry0, mw - 3, ry1, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            for py in range(ry0 + 3, ry0 + 8):
                for px in range(4, 7): menu.putpixel((px, py), COLOR_ACID_BASE)
            draw_pixel_text(menu, sname, 12, ry0 + 2, COLOR_ACID_HOT)
        else:
            draw_pixel_text(menu, sname, 12, ry0 + 2, COLOR_TEXT_NORMAL)

    p_menu = os.path.join(output_dir, "matrix_slot_dropdown_140x196.png")
    menu.save(p_menu, "PNG")
    print(f"Generated Matrix Slot Dropdown: {p_menu}")


def generate_matrix_slider_components(output_dir: str):
    # 1. Single Horizontal Depth Slider (140 x 16 px)
    sw, sh = 140, 16
    slider = Image.new("RGBA", (sw, sh), COLOR_TRANSPARENT)
    draw_pixel_box(slider, 0, 0, sw - 1, sh - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)

    center_x = sw // 2
    for y in range(2, sh - 2):
        slider.putpixel((center_x, y), COLOR_BORDER_LGT)

    fill_len = int((sw // 2 - 8) * 0.65)
    for x in range(center_x, center_x + fill_len):
        for y in range(3, sh - 3):
            slider.putpixel((x, y), COLOR_ACID_BASE)

    tx = center_x + fill_len
    draw_pixel_box(slider, tx - 3, 1, tx + 3, sh - 2, COLOR_PANEL_RAISED, COLOR_ACID_HOT, COLOR_BORDER_DARK)

    p_slider = os.path.join(output_dir, "matrix_slider_horizontal_140x16.png")
    slider.save(p_slider, "PNG")
    print(f"Generated Matrix Slider Track: {p_slider}")

    # 2. Bipolar Demo Strip (Positive + Negative States, 140 x 36 px)
    bipo = Image.new("RGBA", (sw, 36), COLOR_TRANSPARENT)
    # State 1: +75% Unipolar
    draw_pixel_box(bipo, 0, 0, sw - 1, 15, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for y in range(2, 14): bipo.putpixel((center_x, y), COLOR_BORDER_LGT)
    fill_pos = int((sw // 2 - 8) * 0.75)
    for x in range(center_x, center_x + fill_pos):
        for y in range(3, 13): bipo.putpixel((x, y), COLOR_ACID_BASE)
    draw_pixel_box(bipo, center_x + fill_pos - 2, 1, center_x + fill_pos + 2, 14, COLOR_PANEL_RAISED, COLOR_ACID_HOT, COLOR_BORDER_DARK)
    draw_pixel_text(bipo, "+75%", 6, 4, COLOR_ACID_HOT)

    # State 2: -50% Negative
    draw_pixel_box(bipo, 0, 20, sw - 1, 35, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    for y in range(22, 34): bipo.putpixel((center_x, y), COLOR_BORDER_LGT)
    fill_neg = int((sw // 2 - 8) * 0.50)
    for x in range(center_x - fill_neg, center_x):
        for y in range(23, 33): bipo.putpixel((x, y), COLOR_CRIMSON_BRIGHT)
    draw_pixel_box(bipo, center_x - fill_neg - 2, 21, center_x - fill_neg + 2, 34, COLOR_PANEL_RAISED, COLOR_CRIMSON_BRIGHT, COLOR_BORDER_DARK)
    draw_pixel_text(bipo, "-50%", sw - 32, 24, COLOR_CRIMSON_BRIGHT)

    p_bipo = os.path.join(output_dir, "matrix_slider_bipolar_demo_140x36.png")
    bipo.save(p_bipo, "PNG")
    print(f"Generated Matrix Bipolar Slider Demo: {p_bipo}")


def generate_matrix_curve_and_polarity_icons(output_dir: str):
    # 1. 4 Curve Type Icons (Linear, Exponential, Logarithmic, S-Curve) -> 76 x 20 px
    cw, ch = 76, 20
    c_img = Image.new("RGBA", (cw, ch), COLOR_TRANSPARENT)

    # Icon 0: LINEAR
    draw_pixel_box(c_img, 0, 0, 17, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    for i in range(12):
        c_img.putpixel((3 + i, 15 - i), COLOR_ACID_BASE)

    # Icon 1: EXPONENTIAL
    draw_pixel_box(c_img, 19, 0, 36, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    for i in range(12):
        t = i / 11.0
        ey = int(15 - math.pow(t, 2.5) * 11)
        c_img.putpixel((22 + i, ey), COLOR_ACID_BASE)

    # Icon 2: LOGARITHMIC
    draw_pixel_box(c_img, 38, 0, 55, 19, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    for i in range(12):
        t = i / 11.0
        ly = int(15 - math.pow(t, 0.4) * 11)
        c_img.putpixel((41 + i, ly), COLOR_ACID_BASE)

    # Icon 3: S-CURVE
    draw_pixel_box(c_img, 57, 0, 74, 19, COLOR_PANEL_SUNKEN, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    for i in range(12):
        t = i / 11.0
        sy = int(15 - (1.0 - math.cos(t * math.pi)) * 0.5 * 11)
        c_img.putpixel((60 + i, sy), COLOR_ACID_HOT)

    p_curve = os.path.join(output_dir, "matrix_curve_icons_76x20.png")
    c_img.save(p_curve, "PNG")
    print(f"Generated Matrix Curve Icons: {p_curve}")

    # 2. Polarity Toggles (UNI & BI) -> 76 x 20 px
    pw, ph = 76, 20
    p_img = Image.new("RGBA", (pw, ph), COLOR_TRANSPARENT)

    # Button 0: UNIPOLAR
    draw_pixel_box(p_img, 0, 0, 36, 19, COLOR_PANEL_RAISED, COLOR_PURPLE_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(p_img, "UNI", 6, 7, COLOR_TEXT_BRIGHT)
    draw_pixel_text(p_img, ">", 26, 7, COLOR_PURPLE_HOT)

    # Button 1: BIPOLAR
    draw_pixel_box(p_img, 39, 0, 75, 19, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(p_img, "BI", 45, 7, COLOR_ACID_HOT)
    draw_pixel_text(p_img, "<", 58, 7, COLOR_ACID_BASE)
    draw_pixel_text(p_img, ">", 68, 7, COLOR_ACID_BASE)

    p_pol = os.path.join(output_dir, "matrix_polarity_toggles_76x20.png")
    p_img.save(p_pol, "PNG")
    print(f"Generated Matrix Polarity Toggles: {p_pol}")


# ==============================================================================
# 14. WAVETABLE EDITOR UI COMPONENTS
# ==============================================================================

def generate_wt_draw_canvas(output_path: str, width: int = 600, height: int = 240):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    # Background chassis & sunken void
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_BG_VOID, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Top Toolbar / Header (height 22)
    draw_pixel_box(img, 1, 1, width - 2, 22, COLOR_PANEL, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "WT 2D CANVAS", 8, 8, COLOR_TEXT_BRIGHT)

    # Tool Buttons in Header: [DRAW] [LINE] [ARC] [SMTH] [NORM]
    tools = [("DRAW", True), ("LINE", False), ("ARC", False), ("SMTH", False), ("NORM", False)]
    bx = 110
    for name, active in tools:
        bw = len(name) * 6 + 8
        if active:
            draw_pixel_box(img, bx, 4, bx + bw, 18, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            draw_pixel_text(img, name, bx + 4, 7, COLOR_ACID_HOT)
        else:
            draw_pixel_box(img, bx, 4, bx + bw, 18, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
            draw_pixel_text(img, name, bx + 4, 7, COLOR_TEXT_MUTED)
        bx += bw + 4

    # Right info: FRAME 064 / 256 | 2048 SPL
    draw_pixel_text(img, "FRAME: 064/256", width - 180, 8, COLOR_TEXT_ACID)
    draw_pixel_text(img, "2048 SPL", width - 70, 8, COLOR_TEXT_MUTED)

    # Drawing area boundaries
    x_min, x_max = 36, width - 16
    y_top, y_bot = 28, height - 24
    y_mid = (y_top + y_bot) // 2  # ~ 122

    # Draw grid lines inside canvas
    # Amplitude rails: +1.0, 0.0, -1.0
    for y in [y_top + 10, y_mid, y_bot - 10]:
        col = COLOR_BORDER_LGT if y == y_mid else COLOR_GRID
        for x in range(x_min, x_max, 2 if y != y_mid else 1):
            img.putpixel((x, y), col)

    # Left Y-scale labels
    draw_pixel_text(img, "+1.0", 6, y_top + 7, COLOR_TEXT_MUTED)
    draw_pixel_text(img, " 0.0", 6, y_mid - 3, COLOR_TEXT_NORMAL)
    draw_pixel_text(img, "-1.0", 6, y_bot - 13, COLOR_TEXT_MUTED)

    # Phase vertical lines (0, 90, 180, 270, 360 deg)
    phases = [
        (0.00, "0"),
        (0.25, "90"),
        (0.50, "180"),
        (0.75, "270"),
        (1.00, "360")
    ]
    for frac, deg_lbl in phases:
        gx = int(x_min + frac * (x_max - x_min))
        if gx < width - 1:
            for y in range(y_top + 4, y_bot - 4, 3):
                img.putpixel((gx, y), COLOR_GRID)
            # Bottom degree labels
            draw_pixel_text(img, deg_lbl, max(0, gx - len(deg_lbl) * 3), height - 16, COLOR_TEXT_MUTED)

    # Single-cycle waveform with 1px stepped rendering
    disp_w = x_max - x_min
    disp_h = (y_bot - 10) - (y_top + 10)
    amplitude = disp_h * 0.44

    prev_px = None
    prev_py = None

    for i in range(disp_w + 1):
        frac = i / disp_w
        rad = frac * 2.0 * math.pi
        val = 0.85 * math.sin(rad) - 0.28 * math.sin(2 * rad) + 0.18 * math.sin(3 * rad)
        val = max(-1.0, min(1.0, val))

        cur_x = x_min + i
        cur_y = int(y_mid - val * amplitude)

        if prev_px is not None:
            dy = cur_y - prev_py
            step_y = 1 if dy >= 0 else -1
            if dy == 0:
                img.putpixel((cur_x, cur_y), COLOR_ACID_BASE)
            else:
                for yk in range(prev_py, cur_y + step_y, step_y):
                    img.putpixel((cur_x, yk), COLOR_ACID_BASE)
            if abs(val) > 0.75:
                img.putpixel((cur_x, cur_y), COLOR_ACID_HOT)
        else:
            img.putpixel((cur_x, cur_y), COLOR_ACID_BASE)

        prev_px = cur_x
        prev_py = cur_y

    # Draggable node handles at inflection points
    node_fracs = [0.14, 0.40, 0.65, 0.88]
    for nf in node_fracs:
        rad = nf * 2.0 * math.pi
        val = 0.85 * math.sin(rad) - 0.28 * math.sin(2 * rad) + 0.18 * math.sin(3 * rad)
        nx = int(x_min + nf * disp_w)
        ny = int(y_mid - val * amplitude)
        draw_pixel_box(img, nx - 2, ny - 2, nx + 2, ny + 2, COLOR_BG_VOID, COLOR_ACID_HOT)
        img.putpixel((nx, ny), COLOR_ACID_BASE)

    # Hover reticle & Coordinate overlay
    hx = int(x_min + 0.40 * disp_w)
    rad_h = 0.40 * 2.0 * math.pi
    val_h = 0.85 * math.sin(rad_h) - 0.28 * math.sin(2 * rad_h) + 0.18 * math.sin(3 * rad_h)
    hy = int(y_mid - val_h * amplitude)

    for y in range(y_top + 4, y_bot - 4, 4):
        img.putpixel((hx, y), COLOR_PURPLE_DIM)

    draw_pixel_box(img, hx + 8, hy - 20, hx + 104, hy - 4, COLOR_PANEL_SUNKEN, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(img, f"X:0819 Y:{val_h:+.2f}", hx + 12, hy - 16, COLOR_ACID_HOT)

    img.save(output_path, "PNG")
    print(f"Generated Wavetable 2D Canvas: {output_path}")


def generate_wt_fft_bins(output_path: str, width: int = 600, height: int = 120):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_BG_VOID, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Header bar
    draw_pixel_box(img, 1, 1, width - 2, 17, COLOR_PANEL, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "FFT HARMONIC BINS (1-64)", 8, 6, COLOR_TEXT_BRIGHT)
    draw_pixel_text(img, "BIN #07: AMP 82% | PHS +45 DEG", width - 210, 6, COLOR_ACID_HOT)

    # Section 1: Harmonic Amplitudes (y = 20 to 72)
    # Section 2: Harmonic Phases (y = 78 to 116)
    x_left = 34
    x_right = width - 14
    total_w = x_right - x_left
    num_bins = 64
    bin_pitch = total_w / num_bins
    bar_w = 5

    amp_bot = 72
    amp_max_h = 48

    # Left scales
    draw_pixel_text(img, "100%", 4, 22, COLOR_TEXT_MUTED)
    draw_pixel_text(img, " 50%", 4, 44, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "  0%", 4, 66, COLOR_TEXT_MUTED)

    # Baseline for amplitudes
    for x in range(x_left, x_right):
        img.putpixel((x, amp_bot + 1), COLOR_BORDER_MED)

    for b in range(num_bins):
        n = b + 1
        formant = math.exp(-((n - 7) ** 2) / 8.0) * 0.75 + math.exp(-((n - 15) ** 2) / 12.0) * 0.45
        amp_norm = min(1.0, (1.0 / (n ** 0.55)) * 0.6 + formant)
        if n % 2 == 0 and n > 8:
            amp_norm *= 0.4

        bar_h = int(amp_norm * amp_max_h)
        bar_x = int(x_left + b * bin_pitch)
        bar_y = amp_bot - bar_h

        is_active = (n == 7)
        if is_active:
            draw_pixel_box(img, bar_x, bar_y, bar_x + bar_w - 1, amp_bot, COLOR_ACID_BASE, COLOR_ACID_HOT)
            img.putpixel((bar_x + bar_w // 2, bar_y - 3), COLOR_ACID_HOT)
            img.putpixel((bar_x + bar_w // 2, bar_y - 4), COLOR_ACID_HOT)
        else:
            draw_pixel_box(img, bar_x, bar_y, bar_x + bar_w - 1, amp_bot, COLOR_PURPLE_BASE, COLOR_PURPLE_HOT)

    # Phase section divider & labels
    phs_mid = 98
    phs_max_h = 16
    draw_pixel_text(img, "+180", 4, 82, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "   0", 4, 95, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "-180", 4, 108, COLOR_TEXT_MUTED)

    for x in range(x_left, x_right):
        img.putpixel((x, phs_mid), COLOR_BORDER_LGT)

    for b in range(num_bins):
        n = b + 1
        phs_val = math.sin(n * 0.7) * 0.65
        if n == 7:
            phs_val = 0.25
        stem_h = int(phs_val * phs_max_h)
        bx = int(x_left + b * bin_pitch) + bar_w // 2

        is_active = (n == 7)
        stem_col = COLOR_ACID_BASE if is_active else COLOR_PURPLE_DIM
        node_col = COLOR_ACID_HOT if is_active else COLOR_PURPLE_BASE

        target_y = phs_mid - stem_h
        step = 1 if target_y >= phs_mid else -1
        for yk in range(phs_mid, target_y + step, step):
            img.putpixel((bx, yk), stem_col)
        img.putpixel((bx - 1, target_y), node_col)
        img.putpixel((bx + 1, target_y), node_col)
        img.putpixel((bx, target_y), node_col)
        img.putpixel((bx, target_y - 1), node_col)
        img.putpixel((bx, target_y + 1), node_col)

    img.save(output_path, "PNG")
    print(f"Generated Wavetable FFT Bins: {output_path}")


def generate_wt_frame_strip(output_path: str, width: int = 600, height: int = 48):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_BG_CHASSIS, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Left scroll button [ < ]
    btn_w = 20
    draw_pixel_box(img, 2, 2, btn_w + 1, height - 3, COLOR_PANEL_RAISED, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    draw_pixel_text(img, "<", 8, height // 2 - 4, COLOR_TEXT_BRIGHT)

    # Right scroll button [ > ]
    rx0 = width - btn_w - 2
    draw_pixel_box(img, rx0, 2, width - 3, height - 3, COLOR_PANEL_RAISED, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    draw_pixel_text(img, ">", rx0 + 6, height // 2 - 4, COLOR_TEXT_BRIGHT)

    # 10 Thumbnail slots between x = 26 and x = rx0 - 4
    strip_w = (rx0 - 4) - 26
    num_thumbs = 10
    slot_w = 48
    gap = (strip_w - num_thumbs * slot_w) // (num_thumbs - 1)

    frame_indices = [60, 61, 62, 63, 64, 65, 66, 67, 68, 69]
    active_frame = 64

    for idx, fnum in enumerate(frame_indices):
        sx0 = 26 + idx * (slot_w + gap)
        sx1 = sx0 + slot_w - 1
        sy0 = 3
        sy1 = height - 4
        is_active = (fnum == active_frame)

        if is_active:
            draw_pixel_box(img, sx0, sy0, sx1, sy1, COLOR_PANEL_SUNKEN, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            draw_pixel_box(img, sx0 + 1, sy0 + 1, sx0 + 22, sy0 + 9, COLOR_ACID_BASE, COLOR_ACID_BASE)
            draw_pixel_text(img, f"{fnum:03d}", sx0 + 2, sy0 + 2, COLOR_BG_VOID)
        else:
            draw_pixel_box(img, sx0, sy0, sx1, sy1, COLOR_PANEL_SUNKEN, COLOR_BORDER_DARK, COLOR_BORDER_MED)
            draw_pixel_text(img, f"{fnum:03d}", sx0 + 3, sy0 + 3, COLOR_TEXT_MUTED)

        mid_y = (sy0 + sy1) // 2 + 3
        inner_w = slot_w - 6
        morph_ratio = (fnum - 50) / 30.0

        prev_wx, prev_wy = None, None
        w_color = COLOR_ACID_BASE if is_active else COLOR_PURPLE_BASE

        for px in range(inner_w):
            frac = px / inner_w
            rad = frac * 2.0 * math.pi
            sine_part = math.sin(rad)
            saw_part = (2.0 * frac - 1.0)
            wave_val = (1.0 - morph_ratio) * sine_part + morph_ratio * saw_part
            wave_val = max(-1.0, min(1.0, wave_val))

            wy = int(mid_y - wave_val * 11)
            wx = sx0 + 3 + px

            if prev_wx is not None:
                dy = wy - prev_wy
                step = 1 if dy >= 0 else -1
                for yk in range(prev_wy, wy + step, step):
                    img.putpixel((wx, yk), w_color)
            else:
                img.putpixel((wx, wy), w_color)

            prev_wx, prev_wy = wx, wy

    img.save(output_path, "PNG")
    print(f"Generated Wavetable Frame Strip: {output_path}")


def generate_wt_formula_and_morph_components(output_dir: str):
    # 1. Formula Prompt Input Field (300 x 24 px)
    fw, fh = 300, 24
    formula = Image.new("RGBA", (fw, fh), COLOR_TRANSPARENT)
    draw_pixel_box(formula, 0, 0, fw - 1, fh - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Prompt symbol "> "
    draw_pixel_text(formula, ">", 6, 8, COLOR_ACID_BASE)

    # Math formula text
    formula_str = "y = sin(x*pi) + 0.3*cos(3*x*pi)"
    draw_pixel_text(formula, formula_str, 16, 8, COLOR_TEXT_BRIGHT)

    # Blinking / glowing terminal square cursor (5x7)
    cx = 16 + len(formula_str) * 6 + 2
    for cy in range(8, 15):
        for cx_i in range(cx, cx + 5):
            formula.putpixel((cx_i, cy), COLOR_ACID_HOT)

    # Right [OK] / [EVAL] execution button
    draw_pixel_box(formula, fw - 32, 2, fw - 3, fh - 3, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(formula, "EVAL", fw - 29, 8, COLOR_ACID_HOT)

    p_formula = os.path.join(output_dir, "wt_formula_input_300x24.png")
    formula.save(p_formula, "PNG")
    print(f"Generated WT Formula Input Field: {p_formula}")

    # 2. Morph Mode Selector Button (160 x 22 px)
    mw, mh = 160, 22
    morph_sel = Image.new("RGBA", (mw, mh), COLOR_TRANSPARENT)
    draw_pixel_box(morph_sel, 0, 0, mw - 1, mh - 1, COLOR_PANEL_RAISED, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    draw_pixel_text(morph_sel, "MORPH: SPECTRAL", 8, 7, COLOR_TEXT_BRIGHT)
    draw_pixel_text(morph_sel, "v", mw - 14, 7, COLOR_ACID_BASE)

    p_morph_sel = os.path.join(output_dir, "wt_morph_mode_selector_160x22.png")
    morph_sel.save(p_morph_sel, "PNG")
    print(f"Generated WT Morph Mode Selector: {p_morph_sel}")

    # 3. Morph Mode Dropdown Menu (160 x 90 px)
    dw, dh = 160, 90
    dropdown = Image.new("RGBA", (dw, dh), COLOR_TRANSPARENT)
    draw_pixel_box(dropdown, 0, 0, dw - 1, dh - 1, COLOR_PANEL, COLOR_BORDER_LGT, COLOR_BORDER_DARK)
    draw_pixel_text(dropdown, "SELECT MORPH MODE", 8, 6, COLOR_TEXT_MUTED)

    modes = [
        "CROSSFADE",
        "SPECTRAL",
        "MORPH - SPECTRAL",
        "MORPH - ZERO PHASE"
    ]
    row_h = 16
    for idx, mname in enumerate(modes):
        ry0 = 18 + idx * row_h
        ry1 = ry0 + row_h - 2
        is_sel = (mname == "SPECTRAL")

        if is_sel:
            draw_pixel_box(dropdown, 2, ry0, dw - 3, ry1, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
            for py in range(ry0 + 4, ry0 + 9):
                for px in range(5, 8): dropdown.putpixel((px, py), COLOR_ACID_BASE)
            draw_pixel_text(dropdown, mname, 14, ry0 + 3, COLOR_ACID_HOT)
        else:
            draw_pixel_text(dropdown, mname, 14, ry0 + 3, COLOR_TEXT_NORMAL)

    p_dropdown = os.path.join(output_dir, "wt_morph_dropdown_160x90.png")
    dropdown.save(p_dropdown, "PNG")
    print(f"Generated WT Morph Mode Dropdown: {p_dropdown}")


# ==============================================================================
# 15. GLOBAL & ADVANCED PAGE UI COMPONENTS
# ==============================================================================

def generate_global_unison_graph(output_path: str, width: int = 180, height: int = 80):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Header
    draw_pixel_text(img, "UNISON SPREAD", 6, 5, COLOR_TEXT_BRIGHT)
    draw_pixel_text(img, "16 VOICES", width - 62, 5, COLOR_TEXT_ACID)

    # Graph display area
    x_min, x_max = 10, width - 10
    disp_w = x_max - x_min
    y_top, y_bot = 18, height - 14
    y_mid = (y_top + y_bot) // 2

    # Center axis
    for x in range(x_min, x_max):
        img.putpixel((x, y_mid), COLOR_BORDER_LGT)

    # Dotted rails at top and bottom
    for x in range(x_min, x_max, 3):
        img.putpixel((x, y_top + 4), COLOR_GRID)
        img.putpixel((x, y_bot - 4), COLOR_GRID)

    # 16 Voices calculation
    num_voices = 16
    voice_pts = []
    for v_idx in range(num_voices):
        u = (v_idx - (num_voices - 1) / 2.0) / ((num_voices - 1) / 2.0)
        spread_val = math.copysign(abs(u) ** 1.35, u)
        vx = int(x_min + 4 + (v_idx / (num_voices - 1)) * (disp_w - 8))
        vy = int(y_mid - spread_val * (y_mid - y_top - 6))
        voice_pts.append((vx, vy))

        step_y = 1 if vy >= y_mid else -1
        for yk in range(y_mid, vy + step_y, step_y):
            img.putpixel((vx, yk), COLOR_PURPLE_DIM)

    # Stepped connecting curve in glowing Acid Green
    for i in range(len(voice_pts) - 1):
        x0, y0 = voice_pts[i]
        x1, y1 = voice_pts[i + 1]
        for px in range(x0, x1 + 1):
            t = (px - x0) / max(1, (x1 - x0))
            py = int(y0 + t * (y1 - y0))
            img.putpixel((px, py), COLOR_ACID_BASE)

    # Voice marker nodes
    for vx, vy in voice_pts:
        img.putpixel((vx - 1, vy), COLOR_ACID_HOT)
        img.putpixel((vx + 1, vy), COLOR_ACID_HOT)
        img.putpixel((vx, vy - 1), COLOR_ACID_HOT)
        img.putpixel((vx, vy + 1), COLOR_ACID_HOT)
        img.putpixel((vx, vy), COLOR_TEXT_BRIGHT)

    # Footer metrics
    draw_pixel_text(img, "DETUNE: 24CT", 6, height - 10, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "BLEND: 75%", width - 58, height - 10, COLOR_TEXT_MUTED)

    img.save(output_path, "PNG")
    print(f"Generated Global Unison Graph: {output_path}")


def generate_global_checkbox_assets(output_dir: str):
    # 1. Single 16x16 Checked Box
    cb = Image.new("RGBA", (16, 16), COLOR_TRANSPARENT)
    draw_pixel_box(cb, 0, 0, 15, 15, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    chk_coords = [(3, 8), (4, 9), (5, 10), (6, 11), (7, 10), (8, 8), (9, 6), (10, 4), (11, 3)]
    for cx, cy in chk_coords:
        cb.putpixel((cx, cy), COLOR_ACID_BASE)
        cb.putpixel((cx, cy + 1), COLOR_ACID_HOT)

    p_cb = os.path.join(output_dir, "global_checkbox_16x16.png")
    cb.save(p_cb, "PNG")
    print(f"Generated Global Checkbox (16x16): {p_cb}")

    # 2. Dual State Strip (32x16 px) [Unchecked | Checked]
    strip = Image.new("RGBA", (32, 16), COLOR_TRANSPARENT)
    draw_pixel_box(strip, 0, 0, 15, 15, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_box(strip, 16, 0, 31, 15, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    for cx, cy in chk_coords:
        strip.putpixel((16 + cx, cy), COLOR_ACID_BASE)
        strip.putpixel((16 + cx, cy + 1), COLOR_ACID_HOT)

    p_strip = os.path.join(output_dir, "global_checkbox_states_32x16.png")
    strip.save(p_strip, "PNG")
    print(f"Generated Global Checkbox States (32x16): {p_strip}")


def generate_global_preferences_panel(output_path: str, width: int = 320, height: int = 240):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_PANEL, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Header
    draw_pixel_box(img, 1, 1, width - 2, 20, COLOR_PANEL_RAISED, COLOR_BORDER_DARK, COLOR_BORDER_LGT)
    draw_pixel_text(img, "GLOBAL PREFERENCES & ENGINE", 8, 7, COLOR_TEXT_BRIGHT)

    settings = [
        ("ENABLE MPE (MIDI POLY EXP)", True, "CHECKBOX"),
        ("LIMIT VOICES TO HOST BUFFER", True, "CHECKBOX"),
        ("WARM ANALOG OSC DRIFT", True, "CHECKBOX"),
        ("HQ NOISE RESAMPLING", False, "CHECKBOX"),
        ("OVERSAMPLING:", "2x (HQ)", "DROPDOWN"),
        ("UI SCALING:", "100%", "DROPDOWN"),
        ("AUDIO BUFFER:", "128 SPL", "ENTRY"),
        ("AUTHOR PRESET TAG:", "ZYG-ZXG", "ENTRY")
    ]

    chk_coords = [(2, 6), (3, 7), (4, 8), (5, 9), (6, 8), (7, 6), (8, 4), (9, 3)]

    for idx, (label, val, stype) in enumerate(settings):
        ry = 26 + idx * 26
        if stype == "CHECKBOX":
            draw_pixel_box(img, 12, ry + 2, 25, ry + 15, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
            if val is True:
                for cx, cy in chk_coords:
                    img.putpixel((12 + cx, ry + 2 + cy), COLOR_ACID_BASE)
                    img.putpixel((12 + cx, ry + 2 + cy + 1), COLOR_ACID_HOT)
            draw_pixel_text(img, label, 32, ry + 5, COLOR_TEXT_BRIGHT if val else COLOR_TEXT_NORMAL)
        elif stype == "DROPDOWN":
            draw_pixel_text(img, label, 12, ry + 5, COLOR_TEXT_NORMAL)
            draw_pixel_box(img, 170, ry + 2, width - 14, ry + 17, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
            draw_pixel_text(img, val, 176, ry + 6, COLOR_ACID_HOT)
            draw_pixel_text(img, "v", width - 24, ry + 6, COLOR_ACID_BASE)
        elif stype == "ENTRY":
            draw_pixel_text(img, label, 12, ry + 5, COLOR_TEXT_NORMAL)
            draw_pixel_box(img, 170, ry + 2, width - 14, ry + 17, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
            draw_pixel_text(img, val, 176, ry + 6, COLOR_TEXT_BRIGHT)

    img.save(output_path, "PNG")
    print(f"Generated Global Preferences Panel: {output_path}")


def generate_global_curve_editor(output_path: str, width: int = 120, height: int = 120):
    img = Image.new("RGBA", (width, height), COLOR_TRANSPARENT)
    draw_pixel_box(img, 0, 0, width - 1, height - 1, COLOR_BG_VOID, COLOR_BORDER_MED, COLOR_BORDER_DARK)

    # Header
    draw_pixel_box(img, 1, 1, width - 2, 16, COLOR_PANEL, COLOR_BORDER_DARK, COLOR_BORDER_MED)
    draw_pixel_text(img, "PORTAMENTO", 6, 5, COLOR_TEXT_BRIGHT)

    # Graph Area
    x_min, x_max = 16, width - 12
    y_top, y_bot = 24, height - 18
    disp_w = x_max - x_min
    disp_h = y_bot - y_top

    # Axes
    for y in range(y_top, y_bot + 1):
        img.putpixel((x_min, y), COLOR_BORDER_LGT)
    for x in range(x_min, x_max + 1):
        img.putpixel((x, y_bot), COLOR_BORDER_LGT)

    # 50% dotted grid lines
    mid_x = x_min + disp_w // 2
    mid_y = y_top + disp_h // 2
    for y in range(y_top + 2, y_bot - 2, 3):
        img.putpixel((mid_x, y), COLOR_GRID)
    for x in range(x_min + 2, x_max - 2, 3):
        img.putpixel((x, mid_y), COLOR_GRID)

    # Transfer Curve
    prev_px, prev_py = None, None
    for px in range(disp_w + 1):
        frac = px / disp_w
        curve_val = frac ** 2.2
        cur_x = x_min + px
        cur_y = int(y_bot - curve_val * disp_h)

        if prev_px is not None:
            dy = cur_y - prev_py
            step_y = 1 if dy >= 0 else -1
            if dy == 0:
                img.putpixel((cur_x, cur_y), COLOR_ACID_BASE)
            else:
                for yk in range(prev_py, cur_y + step_y, step_y):
                    img.putpixel((cur_x, yk), COLOR_ACID_BASE)
            if frac > 0.8:
                img.putpixel((cur_x, cur_y), COLOR_ACID_HOT)
        else:
            img.putpixel((cur_x, cur_y), COLOR_ACID_BASE)
        prev_px, prev_py = cur_x, cur_y

    # Draggable Curvature Node
    node_x = x_min + int(0.55 * disp_w)
    node_y = int(y_bot - (0.55 ** 2.2) * disp_h)
    draw_pixel_box(img, node_x - 2, node_y - 2, node_x + 2, node_y + 2, COLOR_BG_VOID, COLOR_ACID_HOT)
    img.putpixel((node_x, node_y), COLOR_ACID_BASE)

    # Curve Mode Badge
    draw_pixel_box(img, width - 30, height - 14, width - 4, height - 3, COLOR_PANEL_RAISED, COLOR_ACID_BASE, COLOR_BORDER_DARK)
    draw_pixel_text(img, "EXP", width - 26, height - 12, COLOR_ACID_HOT)

    # Labels
    draw_pixel_text(img, "IN", x_min + 2, height - 12, COLOR_TEXT_MUTED)
    draw_pixel_text(img, "OUT", 2, y_top + 2, COLOR_TEXT_MUTED)

    img.save(output_path, "PNG")
    print(f"Generated Global Curve Editor: {output_path}")


def generate_global_tuning_and_pitch_bend(output_dir: str):
    # 1. Pitch Bend Box (80 x 24 px)
    pb = Image.new("RGBA", (80, 24), COLOR_TRANSPARENT)
    draw_pixel_box(pb, 0, 0, 79, 23, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(pb, "PB:", 6, 8, COLOR_TEXT_MUTED)
    draw_pixel_text(pb, "+2/-2", 24, 8, COLOR_ACID_HOT)
    draw_pixel_text(pb, "^", 66, 4, COLOR_ACID_BASE)
    draw_pixel_text(pb, "v", 66, 12, COLOR_ACID_BASE)

    p_pb = os.path.join(output_dir, "global_pitch_bend_box_80x24.png")
    pb.save(p_pb, "PNG")
    print(f"Generated Global Pitch Bend Box: {p_pb}")

    # 2. Tuning Box (140 x 24 px)
    tb = Image.new("RGBA", (140, 24), COLOR_TRANSPARENT)
    draw_pixel_box(tb, 0, 0, 139, 23, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(tb, "TUNE:", 6, 8, COLOR_TEXT_MUTED)
    draw_pixel_text(tb, "A = 440.0 HZ", 42, 8, COLOR_TEXT_BRIGHT)
    draw_pixel_text(tb, "<", 120, 8, COLOR_PURPLE_HOT)
    draw_pixel_text(tb, ">", 128, 8, COLOR_PURPLE_HOT)

    p_tb = os.path.join(output_dir, "global_tuning_entry_140x24.png")
    tb.save(p_tb, "PNG")
    print(f"Generated Global Tuning Entry: {p_tb}")

    # 3. Master Tuning Section Panel (240 x 60 px)
    pnl = Image.new("RGBA", (240, 60), COLOR_TRANSPARENT)
    draw_pixel_box(pnl, 0, 0, 239, 59, COLOR_PANEL, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_box(pnl, 1, 1, 238, 16, COLOR_PANEL_RAISED, COLOR_BORDER_DARK, COLOR_BORDER_LGT)
    draw_pixel_text(pnl, "PITCH BEND & MASTER TUNING", 8, 5, COLOR_TEXT_BRIGHT)

    # Row 1: PB Limits
    draw_pixel_text(pnl, "PB RANGE:", 8, 24, COLOR_TEXT_NORMAL)
    draw_pixel_box(pnl, 70, 21, 140, 35, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(pnl, "+02 / -02 ST", 74, 25, COLOR_ACID_HOT)

    # Row 2: Master Tune
    draw_pixel_text(pnl, "MASTER:", 8, 42, COLOR_TEXT_NORMAL)
    draw_pixel_box(pnl, 70, 39, 140, 53, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(pnl, "440.00 HZ", 78, 43, COLOR_TEXT_BRIGHT)

    # Right side: Fine tune offset
    draw_pixel_box(pnl, 148, 21, 232, 53, COLOR_PANEL_SUNKEN, COLOR_BORDER_MED, COLOR_BORDER_DARK)
    draw_pixel_text(pnl, "FINE OFFSET", 154, 25, COLOR_TEXT_MUTED)
    draw_pixel_text(pnl, "+0.0 CT", 168, 38, COLOR_ACID_HOT)

    p_pnl = os.path.join(output_dir, "panel_global_tuning_section_240x60.png")
    pnl.save(p_pnl, "PNG")
    print(f"Generated Global Tuning Section Panel: {p_pnl}")


# ==============================================================================
# MAIN ENTRYPOINT
# ==============================================================================
def main():
    output_dir = os.path.dirname(os.path.abspath(__file__))
    print(f"=== Generating Full Front-Page, FX & Matrix Suite in: {output_dir} ===")

    # 1. Knobs
    generate_knob_sprite_sheets(output_dir)

    # 2. Viewports (Wavetable & Filter)
    generate_wavetable_visualizer(os.path.join(output_dir, "panel_wavetable_2d_180x96.png"))
    generate_filter_viewport(os.path.join(output_dir, "panel_filter_response_180x96.png"))

    # 3. Modulation Curve Editors
    generate_envelope_editor(os.path.join(output_dir, "panel_env_editor_360x120.png"))
    generate_lfo_editor(os.path.join(output_dir, "panel_lfo_editor_360x120.png"))

    # 4. Faders
    generate_fader_components(output_dir)

    # 5. Tabs & Buttons
    generate_tabs_and_buttons(output_dir)

    # 6. Sub & Noise Generator Panels
    generate_sub_noise_panels(output_dir)

    # 7. Warp Mode Selectors & Menus
    generate_warp_components(output_dir)

    # 8. Master Top Bar & Output Section
    generate_master_and_top_bar(output_dir)

    # 9. Virtual Keyboard & Wheels
    generate_keyboard_and_wheels(output_dir)

    # 10. FX Sidebar & Add Menu
    generate_fx_sidebar(os.path.join(output_dir, "panel_fx_sidebar_120x340.png"))
    generate_fx_add_menu(os.path.join(output_dir, "fx_add_dropdown_140x260.png"))

    # 11. Complete Modular Suite of All 14 FX Modules
    generate_fx_distortion(os.path.join(output_dir, "panel_fx_distortion_420x110.png"))
    generate_fx_compressor(os.path.join(output_dir, "panel_fx_compressor_420x110.png"))
    generate_fx_delay(os.path.join(output_dir, "panel_fx_delay_420x110.png"))
    generate_fx_reverb(os.path.join(output_dir, "panel_fx_reverb_420x110.png"))
    generate_fx_equalizer(os.path.join(output_dir, "panel_fx_equalizer_420x110.png"))
    generate_fx_filter(os.path.join(output_dir, "panel_fx_filter_420x110.png"))
    generate_fx_splitter(os.path.join(output_dir, "panel_fx_splitter_420x110.png"))
    generate_fx_chorus(os.path.join(output_dir, "panel_fx_chorus_420x110.png"))
    generate_fx_flanger(os.path.join(output_dir, "panel_fx_flanger_420x110.png"))
    generate_fx_phaser(os.path.join(output_dir, "panel_fx_phaser_420x110.png"))
    generate_fx_hyper_dimension(os.path.join(output_dir, "panel_fx_hyper_dim_420x110.png"))
    generate_fx_bode(os.path.join(output_dir, "panel_fx_bode_420x110.png"))
    generate_fx_convolve(os.path.join(output_dir, "panel_fx_convolve_420x110.png"))
    generate_fx_utility(os.path.join(output_dir, "panel_fx_utility_420x110.png"))

    # 12. FX Splitter Routing Containers (L/M/H, L/H, M/S)
    generate_fx_splitter_containers(output_dir)

    # 13. Modulation Matrix UI Suite
    p_mat = os.path.join(output_dir, "panel_matrix_grid_960x360.png")
    generate_matrix_grid_panel(p_mat)
    generate_matrix_grid_panel(os.path.join(output_dir, "matrix_grid_panel_960x360.png"))

    generate_matrix_slot_components(output_dir)
    generate_matrix_slider_components(output_dir)
    generate_matrix_curve_and_polarity_icons(output_dir)

    # 14. Wavetable Editor UI Suite
    p_wt_canvas = os.path.join(output_dir, "wt_draw_canvas_600x240.png")
    generate_wt_draw_canvas(p_wt_canvas)
    generate_wt_draw_canvas(os.path.join(output_dir, "panel_wt_draw_canvas_600x240.png"))

    p_wt_fft = os.path.join(output_dir, "wt_fft_bins_600x120.png")
    generate_wt_fft_bins(p_wt_fft)
    generate_wt_fft_bins(os.path.join(output_dir, "panel_wt_fft_bins_600x120.png"))

    p_wt_strip = os.path.join(output_dir, "wt_frame_strip_600x48.png")
    generate_wt_frame_strip(p_wt_strip)
    generate_wt_frame_strip(os.path.join(output_dir, "panel_wt_frame_strip_600x48.png"))

    generate_wt_formula_and_morph_components(output_dir)

    # 15. Global & Advanced Page UI Suite
    p_unison = os.path.join(output_dir, "global_unison_graph_180x80.png")
    generate_global_unison_graph(p_unison)
    generate_global_unison_graph(os.path.join(output_dir, "panel_global_unison_graph_180x80.png"))

    generate_global_checkbox_assets(output_dir)

    p_pref = os.path.join(output_dir, "panel_global_preferences_320x240.png")
    generate_global_preferences_panel(p_pref)
    generate_global_preferences_panel(os.path.join(output_dir, "global_preferences_panel_320x240.png"))

    p_curve = os.path.join(output_dir, "global_curve_editor_120x120.png")
    generate_global_curve_editor(p_curve)
    generate_global_curve_editor(os.path.join(output_dir, "panel_global_curve_editor_120x120.png"))

    generate_global_tuning_and_pitch_bend(output_dir)

    print("\n=== Complete Serum VST Asset Suite (Front, FX, Matrix, WT & Global) Successfully Generated! ===")


if __name__ == "__main__":
    main()
