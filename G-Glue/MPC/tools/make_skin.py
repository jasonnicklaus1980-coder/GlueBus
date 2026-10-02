#!/usr/bin/env python3
"""Generate the G-Glue MPC screen skin: a premium large-format-console look (TUI.json pipeline of the GlueBus MPC
plugins) + a preview rendered from the real plugin.

Design (console-inspired, original artwork; no third-party names, logos or graphics):
  toolbar   black bar: G-GLUE logo, recessed preset display with < > and favourite, LOAD, A/B COPY SAVE DELETE
  module 1  METERS & I/O: precision LED ladders with printed dB scales, backlit black-face gain-reduction meter in a
            chrome bezel; INPUT / MIX / OUTPUT knobs; square illuminated ANALOG and BYPASS switches
  module 2  COMPRESSOR: THRESHOLD MAKE-UP RATIO / ATTACK RELEASE S/C HPF, knurled knobs with colour-coded caps and
            printed scales, value windows, Q-Link numbers beside every control
  footer    status line and current preset / A-B slot
MPC rules (proven on an MPC X by the other plugins here): filmstrip frames are square, at most 72 px with 128 frames
(9216 px strips); bigger displays are square tiles following one parameter; numFrames = last frame index;
skin folder "<vendor> - VST - <plugin name>". Indices come from the built plugin: run `make native` first.
Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, random, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "G-Glue Audio - VST - G-Glue Bus Compressor")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
LIB = os.path.join(ROOT, "build", "native", "gglue.so")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4
VERSION = "1.1.0.0"
FONT_DIRS = ["/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]

def font(size, bold=True):
    names = ["LiberationSans-Bold.ttf", "DejaVuSans-Bold.ttf", "Arial Bold.ttf"] if bold else ["LiberationSans-Regular.ttf", "DejaVuSans.ttf", "Arial.ttf"]
    for d in FONT_DIRS:
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p): return ImageFont.truetype(p, max(1, int(round(size))))
    return ImageFont.load_default()
def rgba(c, a=255): return (int(c[0]), int(c[1]), int(c[2]), a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def hexcol(c, a=255): return "%02x%02x%02x%02x" % (a, c[0], c[1], c[2])
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def spaced(d, x, y, s, f, fill, track=1.0, anchor="l"):
    """letter-spaced silkscreen text; anchor l / c; returns the width"""
    widths = [d.textlength(ch, font=f) for ch in s]
    total = sum(widths) + track * (len(s) - 1)
    if anchor == "c": x -= total / 2
    for ch, w in zip(s, widths):
        d.text((x, y), ch, font=f, fill=fill); x += w + track
    return total

# ---------------------------------------------------------------- palette (console)
EDGE, SEAM = (104, 110, 118), (9, 9, 10)
SLATE_T, SLATE_B = (66, 71, 78), (47, 51, 57)               # console slate blue-grey
FRAME = (196, 200, 206)                                     # white legend frames
PRINT, DIM, FAINT = (236, 236, 238), (150, 152, 158), (96, 98, 104)
LED_RED, LED_AMBER, LED_GREEN = (255, 58, 38), (255, 178, 42), (72, 224, 112)
CAPS = {"red": (196, 50, 42), "blue": (46, 96, 172), "grey": (160, 162, 166), "green": (58, 138, 84),
        "yellow": (220, 174, 44), "black": (30, 30, 32)}

# ---------------------------------------------------------------- parameters (from the plugin)
def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.GG_ParamKey.restype = ctypes.c_char_p; lib.GG_ParamKey.argtypes = [ctypes.c_int]; lib.GG_ParamCount.restype = ctypes.c_int
    return {lib.GG_ParamKey(i).decode(): i for i in range(lib.GG_ParamCount())}
P = load_keys()

# ---------------------------------------------------------------- layout
TOOL_H, FOOT_Y = 60, 598
MOD1 = (12, 70, 600, 590)               # METERS & I/O
MOD2 = (612, 70, 1268, 590)             # COMPRESSOR
MTILE, MCOLS, MROWS = 72, 6, 2
MW, MH = MTILE * MCOLS, MTILE * MROWS
MX, MY = 96, 112                          # GR meter
LTILE, LROWS, LSEG = 24, 6, 16            # LED ladders: 24 x 144, 16 segments of 3 dB
IN_X, OUT_X, LY = 36, 552, MY
KS = 72                                   # knob filmstrip size
CW, CH = 150, 160                         # knob component box: knob at (39, 26), value window at y 124..148
KNOB_Y0 = 26
# key: (caption, cap colour, x centre, box top y, Q-Link number)
KNOBS = {
    "threshold": ("THRESHOLD", "red", 721, 140, 1), "makeup": ("MAKE-UP", "grey", 939, 140, 2), "ratio": ("RATIO", "black", 1157, 140, 5),
    "attack": ("ATTACK", "blue", 721, 374, 3), "release": ("RELEASE", "blue", 939, 374, 4), "scfilter": ("S/C HPF", "green", 1157, 374, 6),
    "input": ("INPUT", "grey", 112, 314, 8), "mix": ("MIX", "yellow", 306, 314, 7), "output": ("OUTPUT", "grey", 500, 314, 9)}
SCALE = {"threshold": ["-30", "-20", "-10", "0", "+10"], "makeup": ["0", "6", "12", "18", "24"], "attack": [".1", ".3", "1", "3", "10", "30"],
         "release": [".1", ".3", ".6", "1.2", "A"], "ratio": ["2", "4", "10"], "scfilter": ["OFF", "30", "60", "90", "120", "150", "200"],
         "mix": ["0", "25", "50", "75", "100"], "input": ["-24", "-12", "0", "+12", "+24"], "output": ["-24", "-12", "0", "+12", "+24"]}
STEPS = {"attack": 6, "release": 5, "ratio": 3, "scfilter": 7}
UNITS = {"threshold": "dB", "makeup": "dB", "attack": "ms", "release": "s", "ratio": "", "scfilter": "Hz", "mix": "%", "input": "dB", "output": "dB"}
SWITCHES = {"analog": ("ANALOG", LED_AMBER, 214, 498, 10), "bypass": ("BYPASS", LED_RED, 398, 498, 11)}   # square 60 px caps
SW = 60
ANG0, ANG1 = -140.0, 140.0
def ang_xy(cx, cy, r, deg):
    a = math.radians(deg - 90); return cx + r * math.cos(a), cy + r * math.sin(a)
def knob_box(key):
    _, _, cx, top, _ = KNOBS[key]; return cx - CW / 2, top
def knob_centre(key):
    x, y = knob_box(key); return x + CW / 2, y + KNOB_Y0 + KS / 2

# toolbar controls
TB = {"prev": (284, 12, 40, 36), "browse": (330, 10, 420, 40), "next": (756, 12, 40, 36), "fav": (802, 12, 40, 36),
      "load": (852, 12, 76, 36), "ab": (968, 12, 64, 36), "copy": (1038, 12, 70, 36), "save": (1114, 12, 70, 36), "delete": (1190, 12, 76, 36)}

# ---------------------------------------------------------------- knobs (one filmstrip per cap colour)
def knob_strip(cap):
    """large-format console knob: smooth ribbed grey body, flat coloured cap, white pointer line"""
    s = KS * SS; c = s / 2; R = s * 0.41
    base = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sh = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([c - R * 1.02, c - R * 0.96 + s * 0.05, c + R * 1.02, c + R * 1.06 + s * 0.05], fill=(0, 0, 0, 210))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(s * 0.03)))
    d = ImageDraw.Draw(base)
    for yy in range(int(c - R), int(c + R) + 1):                # cylinder side: grey, lit from above
        k = min(1.0, max(0.0, (yy - (c - R)) / (2 * R))); dx = math.sqrt(max(0.0, R * R - (yy - c) ** 2))
        d.line([c - dx, yy, c + dx, yy], fill=rgba(mix((92, 95, 100), (24, 25, 28), k ** 0.9)))
    rt = R * 0.90                                               # flat top face
    top = Image.new("RGBA", (s, s), (0, 0, 0, 0)); td = ImageDraw.Draw(top)
    for i in range(50, 0, -1):
        t = i / 50; r = rt * t
        td.ellipse([c - r, c - r - rt * 0.04 * (1 - t), c + r, c + r - rt * 0.04 * (1 - t)], fill=rgba(mix((40, 42, 46), (74, 77, 82), (1 - t) ** 1.6)))
    rc = R * 0.64                                               # large flat coloured cap
    col = CAPS[cap]
    capim = Image.new("RGBA", (s, s), (0, 0, 0, 0)); cd = ImageDraw.Draw(capim)
    cd.ellipse([c - rc - SS, c - rc - SS, c + rc + SS, c + rc + SS], fill=rgba(mix(col, (0, 0, 0), 0.55)))
    for i in range(40, 0, -1):
        t = i / 40; r = rc * t
        cd.ellipse([c - r, c - r - rc * 0.05 * (1 - t), c + r, c + r - rc * 0.05 * (1 - t)],
                   fill=rgba(mix(mix(col, (0, 0, 0), 0.12), mix(col, (255, 255, 255), 0.30), (1 - t) ** 2.2)))
    line_col = (24, 24, 26) if cap in ("grey", "yellow") else PRINT
    strip = Image.new("RGBA", (KS, KS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        im = base.copy(); d = ImageDraw.Draw(im)
        deg = ANG0 + (ANG1 - ANG0) * f / NUMFRAMES
        for i in range(90):                                     # fine ribs on the side (turn with the knob)
            a = deg + i * 4
            x0, y0 = ang_xy(c, c, R * 0.92, a); x1, y1 = ang_xy(c, c, R * 0.995, a)
            d.line([x0, y0, x1, y1], fill=(0, 0, 0, 90), width=max(1, int(SS * 0.6)))
        im.alpha_composite(top); im.alpha_composite(capim)
        d = ImageDraw.Draw(im)
        d.ellipse([c - rt, c - rt, c + rt, c + rt], outline=(255, 255, 255, 34), width=SS)
        x0, y0 = ang_xy(c, c, rc * 1.06, deg); x1, y1 = ang_xy(c, c, rt * 0.97, deg)      # pointer on the grey top
        d.line([x0, y0, x1, y1], fill=rgba(PRINT), width=int(SS * 2.4))
        x0, y0 = ang_xy(c, c, rc * 0.10, deg); x1, y1 = ang_xy(c, c, rc * 0.94, deg)      # and across the cap
        d.line([x0, y0, x1, y1], fill=rgba(line_col), width=int(SS * 2.0))
        strip.paste(im.resize((KS, KS), Image.LANCZOS), (0, f * KS))
    return strip

# ---------------------------------------------------------------- gain-reduction meter (GrScale of GUI/NeedleBallistics.h)
GR_DB = [0, 1, 2, 3, 5, 7, 10, 15, 20]
GR_POS = [1.0, 0.885, 0.775, 0.675, 0.53, 0.415, 0.285, 0.125, 0.0]
def gr_pos(db):
    if db <= 0: return 1.0
    if db >= 20: return 0.0
    for i in range(1, len(GR_DB)):
        if db <= GR_DB[i]:
            t = (db - GR_DB[i - 1]) / (GR_DB[i] - GR_DB[i - 1]); return GR_POS[i - 1] + t * (GR_POS[i] - GR_POS[i - 1])
ARC0, ARC1 = -0.80, 0.80
def meter_geom(s):
    bez = 9 * s
    inner = (bez, bez, MW * s - bez, MH * s - bez)
    ih = inner[3] - inner[1]
    pivot = ((inner[0] + inner[2]) / 2, inner[3] + ih * 0.46)
    return inner, ih, pivot
def arc_pt(pivot, r, pos):
    a = ARC0 + pos * (ARC1 - ARC0); return pivot[0] + r * math.sin(a), pivot[1] - r * math.cos(a)
def meter_face(s=3):
    im = Image.new("RGBA", (MW * s, MH * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for i in range(MH * s):                                     # chrome bezel, lit from the top
        k = i / (MH * s); d.line([0, i, MW * s, i], fill=rgba(mix((214, 216, 220), (70, 72, 76), k ** 0.7)))
    m = Image.new("L", im.size, 0); ImageDraw.Draw(m).rounded_rectangle([0, 0, MW * s - 1, MH * s - 1], radius=12 * s, fill=255)
    clip = Image.new("RGBA", im.size, (0, 0, 0, 0)); clip.paste(im, (0, 0), m); im = clip; d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, MW * s - 1, MH * s - 1], radius=12 * s, outline=(0, 0, 0, 255), width=s)
    d.rounded_rectangle([4 * s, 4 * s, MW * s - 4 * s, MH * s - 4 * s], radius=9 * s, fill=rgba((16, 16, 18)))
    inner, ih, pivot = meter_geom(s)
    face = Image.new("RGBA", im.size, (0, 0, 0, 0)); fd = ImageDraw.Draw(face)       # backlit black face
    fd.rounded_rectangle(inner, radius=5 * s, fill=rgba((14, 14, 15)))
    glow = Image.new("RGBA", im.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(glow)
    gx, gy = (inner[0] + inner[2]) / 2, inner[1] + ih * 0.55
    gd.ellipse([gx - MW * s * 0.36, gy - ih * 0.62, gx + MW * s * 0.36, gy + ih * 0.62], fill=(120, 86, 40, 70))
    face.alpha_composite(glow.filter(ImageFilter.GaussianBlur(ih * 0.25)))
    mk = Image.new("L", im.size, 0); ImageDraw.Draw(mk).rounded_rectangle(inner, radius=5 * s, fill=255)
    im.paste(face, (0, 0), mk); d = ImageDraw.Draw(im)
    rs = ih * 1.22
    ink = (236, 230, 214)
    d.line([arc_pt(pivot, rs, i / 240) for i in range(241)], fill=rgba(ink), width=2 * s)
    d.line([arc_pt(pivot, rs + 4 * s, i / 240 * gr_pos(10)) for i in range(241)], fill=rgba((226, 54, 40)), width=4 * s)
    for db in (4, 6, 8, 9, 12.5, 17.5):
        p = gr_pos(db); d.line([arc_pt(pivot, rs, p), arc_pt(pivot, rs - 7 * s, p)], fill=rgba(ink), width=s)
    f = font(14 * s)
    for db, p in zip(GR_DB, GR_POS):
        d.line([arc_pt(pivot, rs + 2 * s, p), arc_pt(pivot, rs - 13 * s, p)], fill=rgba(ink), width=2 * s)
        x, y = arc_pt(pivot, rs + 15 * s, p)
        text_c(d, x, y, str(db), f, rgba((226, 54, 40) if db >= 10 else ink))
    text_c(d, (inner[0] + inner[2]) / 2, inner[1] + ih * 0.63, "GAIN  REDUCTION", font(10 * s), rgba(ink))
    text_c(d, (inner[0] + inner[2]) / 2, inner[1] + ih * 0.75, "dB", font(9 * s, False), rgba(mix(ink, (0, 0, 0), 0.35)))
    return im, s
def meter_tiles():
    face, s = meter_face()
    inner, ih, pivot = meter_geom(s)
    tiles = [Image.new("RGBA", (MTILE, MTILE * FRAMES), (0, 0, 0, 0)) for _ in range(MCOLS * MROWS)]
    mk = Image.new("L", face.size, 0); ImageDraw.Draw(mk).rounded_rectangle(inner, radius=5 * s, fill=255)
    zero = Image.new("L", face.size, 0)
    global GLASS                                                # soft glass sheen across the upper face
    gl = Image.new("RGBA", face.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
    gd.ellipse([inner[0] - (inner[2] - inner[0]) * 0.1, inner[1] - ih * 0.9, inner[2] * 0.75, inner[1] + ih * 0.32], fill=(255, 255, 255, 20))
    gl = gl.filter(ImageFilter.GaussianBlur(ih * 0.12))
    GLASS = Image.composite(gl, Image.new("RGBA", face.size, (0, 0, 0, 0)), mk)
    for f in range(FRAMES):
        im = face.copy()
        ndl = Image.new("RGBA", im.size, (0, 0, 0, 0)); nd = ImageDraw.Draw(ndl)
        pos = 1.0 - f / NUMFRAMES                               # plugin value = 1 - GrScale::position
        tip = arc_pt(pivot, ih * 1.32, pos)
        nd.line([(pivot[0] + 4 * s, pivot[1] + 3 * s), (tip[0] + 4 * s, tip[1] + 3 * s)], fill=(0, 0, 0, 120), width=3 * s)
        nd.line([pivot, tip], fill=rgba((244, 240, 230)), width=int(2.2 * s))
        mid = (pivot[0] + (tip[0] - pivot[0]) * 0.9, pivot[1] + (tip[1] - pivot[1]) * 0.9)
        nd.line([mid, tip], fill=rgba((255, 84, 52)), width=int(2.4 * s))
        im.paste(ndl, (0, 0), Image.composite(ndl.split()[3], zero, mk))
        d = ImageDraw.Draw(im)
        d.rectangle([inner[0], inner[3] - ih * 0.14, inner[2], inner[3]], fill=rgba((8, 8, 9)))          # pivot housing
        d.line([inner[0], inner[3] - ih * 0.14, inner[2], inner[3] - ih * 0.14], fill=rgba((60, 60, 64)), width=s)
        im.alpha_composite(GLASS)
        small = im.resize((MW, MH), Image.LANCZOS)
        for r in range(MROWS):
            for c in range(MCOLS):
                tiles[r * MCOLS + c].paste(small.crop((c * MTILE, r * MTILE, (c + 1) * MTILE, (r + 1) * MTILE)), (0, f * MTILE))
    return tiles

# ---------------------------------------------------------------- LED ladders (-48 .. 0 dBFS, 3 dB per segment)
def led_colour(top): return LED_GREEN if top <= -12 else (LED_AMBER if top <= -3 else LED_RED)
def led_tiles():
    h = LTILE * LROWS; s = 4
    tiles = [Image.new("RGBA", (LTILE, LTILE * FRAMES), (0, 0, 0, 0)) for _ in range(LROWS)]
    pitch = h / LSEG
    for f in range(FRAMES):
        db = -48 + 48 * f / NUMFRAMES
        im = Image.new("RGBA", (LTILE * s, h * s), rgba((6, 6, 7))); d = ImageDraw.Draw(im)
        for i in range(LSEG):
            top = -48 + 3 * (i + 1); col = led_colour(top)
            lit = db >= top - 3 + 0.01
            y1 = (h - i * pitch - 1.5) * s; y0 = y1 - (pitch - 2.5) * s
            if lit:
                g = Image.new("RGBA", im.size, (0, 0, 0, 0)); ImageDraw.Draw(g).rectangle([3 * s, y0 - s, (LTILE - 3) * s, y1 + s], fill=rgba(col, 110))
                im.alpha_composite(g.filter(ImageFilter.GaussianBlur(2 * s))); d = ImageDraw.Draw(im)
                d.rectangle([5 * s, y0, (LTILE - 5) * s, y1], fill=rgba(mix(col, (255, 255, 255), 0.18)))
            else:
                d.rectangle([5 * s, y0, (LTILE - 5) * s, y1], fill=rgba(mix(col, (0, 0, 0), 0.84)))
        small = im.resize((LTILE, h), Image.LANCZOS)
        for k in range(LROWS): tiles[k].paste(small.crop((0, k * LTILE, LTILE, (k + 1) * LTILE)), (0, f * LTILE))
    return tiles

# ---------------------------------------------------------------- switches and toolbar buttons
def square_switch(on, led):
    """console push switch: a square translucent coloured cap that lights up from behind"""
    s = SS; w = SW; im = Image.new("RGBA", (w * s, w * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, w * s - 1], radius=4 * s, fill=rgba((6, 6, 7)))           # bezel cut-out
    off = int(1.5 * s) if on else 0
    x0, y0, x1, y1 = 6 * s, 6 * s + off, (w - 6) * s, (w - 6) * s + off
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0)); ImageDraw.Draw(g).rounded_rectangle([x0 - 3 * s, y0 - 3 * s, x1 + 3 * s, y1 + 3 * s], radius=4 * s, fill=rgba(led, 150))
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(4 * s))); d = ImageDraw.Draw(im)
    top_c = mix(led, (255, 255, 255), 0.45) if on else mix(led, (0, 0, 0), 0.62)
    bot_c = led if on else mix(led, (0, 0, 0), 0.80)
    cap = Image.new("RGBA", im.size, (0, 0, 0, 0)); cd = ImageDraw.Draw(cap)
    for yy in range(y0, y1):
        k = (yy - y0) / (y1 - y0); cd.line([x0, yy, x1, yy], fill=rgba(mix(top_c, bot_c, k)))
    mk = Image.new("L", im.size, 0); ImageDraw.Draw(mk).rounded_rectangle([x0, y0, x1, y1], radius=3 * s, fill=255)
    im.paste(cap, (0, 0), mk); d = ImageDraw.Draw(im)
    d.rounded_rectangle([x0, y0, x1, y1], radius=3 * s, outline=(0, 0, 0, 180), width=s)
    d.rounded_rectangle([x0 + 3 * s, y0 + 2 * s, x1 - 3 * s, y0 + 8 * s], radius=2 * s, fill=(255, 255, 255, 70 if on else 26))   # gloss
    return im.resize((w, w), Image.LANCZOS)

def tool_button(lab, w, h, icon=None, accent=None):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba((4, 4, 5)))
    face = Image.new("RGBA", im.size, (0, 0, 0, 0)); fd = ImageDraw.Draw(face)
    for yy in range(s, (h - 1) * s):
        k = yy / (h * s); fd.line([s, yy, (w - 1) * s, yy], fill=rgba(mix((66, 67, 71), (34, 35, 38), k)))
    mk = Image.new("L", im.size, 0); ImageDraw.Draw(mk).rounded_rectangle([s, s, (w - 1) * s, (h - 1) * s], radius=3 * s, fill=255)
    im.paste(face, (0, 0), mk); d = ImageDraw.Draw(im)
    d.line([3 * s, s + 1, (w - 3) * s, s + 1], fill=(255, 255, 255, 50), width=s)
    cx, cy, r = w * s / 2, h * s / 2, min(w, h) * s * 0.2
    if icon == "prev": d.polygon([(cx + r * 0.8, cy - r), (cx + r * 0.8, cy + r), (cx - r * 0.9, cy)], fill=rgba(PRINT))
    elif icon == "next": d.polygon([(cx - r * 0.8, cy - r), (cx - r * 0.8, cy + r), (cx + r * 0.9, cy)], fill=rgba(PRINT))
    elif icon == "fav":
        pts = [(cx + (r * 1.3 if i % 2 == 0 else r * 0.52) * math.cos(math.radians(-90 + i * 36)),
                cy + (r * 1.3 if i % 2 == 0 else r * 0.52) * math.sin(math.radians(-90 + i * 36))) for i in range(10)]
        d.polygon(pts, fill=rgba(LED_AMBER))
    else:
        f = font(12 * s); spaced(d, cx, cy - 7 * s, lab, f, rgba(accent or PRINT), track=1.2 * s, anchor="c")
    return im.resize((w, h), Image.LANCZOS)

# ---------------------------------------------------------------- background
def screw(d, x, y, r, ang):
    d.ellipse([x - r - 1.5, y - r - 1, x + r + 1.5, y + r + 2], fill=(4, 4, 5))
    for i in range(int(r * 4), 0, -1):
        t = i / (r * 4); rr = r * t; d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=mix((40, 41, 44), (122, 124, 130), (1 - t) ** 0.6))
    for a in (ang, ang + 90):
        dx, dy = math.cos(math.radians(a)) * r * 0.7, math.sin(math.radians(a)) * r * 0.7
        d.line([x - dx, y - dy, x + dx, y + dy], fill=(10, 10, 11), width=2)

def module(im, box, title):
    x0, y0, x1, y1 = box; d = ImageDraw.Draw(im)
    d.rectangle([x0 - 2, y0 - 2, x1 + 2, y1 + 2], fill=SEAM)
    face = Image.new("RGB", (x1 - x0, y1 - y0)); fd = ImageDraw.Draw(face)
    for y in range(y1 - y0): fd.line([0, y, x1 - x0, y], fill=mix(SLATE_T, SLATE_B, y / (y1 - y0)))
    random.seed(x0)
    tex = Image.new("RGBA", face.size, (0, 0, 0, 0)); td = ImageDraw.Draw(tex)
    for _ in range(face.size[0] * face.size[1] // 60):           # vertical brushing
        x = random.randrange(face.size[0]); y = random.randrange(face.size[1]); ln = random.randint(10, 90)
        td.line([x, y, x, min(face.size[1], y + ln)], fill=(255, 255, 255, random.randint(2, 5)) if random.random() < 0.5 else (0, 0, 0, random.randint(3, 7)))
    face.paste(tex, (0, 0), tex)
    im.paste(face, (x0, y0)); d = ImageDraw.Draw(im)
    d.line([x0, y0, x1 - 1, y0], fill=EDGE); d.line([x0, y0, x0, y1 - 1], fill=(66, 68, 72))
    d.line([x0, y1 - 1, x1 - 1, y1 - 1], fill=(20, 20, 22)); d.line([x1 - 1, y0, x1 - 1, y1 - 1], fill=(22, 22, 24))
    for sx, sy in ((x0 + 12, y0 + 12), (x1 - 12, y0 + 12), (x0 + 12, y1 - 12), (x1 - 12, y1 - 12)):
        screw(d, sx, sy, 5, random.randint(0, 90))
    f = font(12); tw = spaced(d, (x0 + x1) / 2, y0 + 12, title, f, PRINT, track=2.2, anchor="c")
    cy = y0 + 19
    d.line([x0 + 30, cy, (x0 + x1) / 2 - tw / 2 - 12, cy], fill=FAINT)
    d.line([(x0 + x1) / 2 + tw / 2 + 12, cy, x1 - 30, cy], fill=FAINT)

def qbadge(d, x, y, n):
    f = font(9); w = 12 + 6 * (len(str(n)) - 1)
    d.rounded_rectangle([x, y, x + w, y + 12], radius=3, outline=FAINT, fill=(26, 26, 28))
    text_c(d, x + w / 2 + 0.5, y + 6, str(n), f, DIM)

def background():
    im = Image.new("RGB", (W, H), (20, 20, 22)); d = ImageDraw.Draw(im)
    for y in range(TOOL_H): d.line([0, y, W, y], fill=mix((34, 34, 37), (12, 12, 13), y / TOOL_H))           # toolbar
    d.line([0, TOOL_H, W, TOOL_H], fill=(96, 98, 104)); d.line([0, TOOL_H + 1, W, TOOL_H + 1], fill=(0, 0, 0))
    spaced(d, 22, 13, "G-GLUE", font(27), PRINT, track=3.5)
    spaced(d, 24, 43, "BUS  COMPRESSOR", font(9), DIM, track=2.2)
    bx, by, bw, bh = TB["browse"]                                                                           # preset display
    d.rounded_rectangle([bx - 2, by - 2, bx + bw + 2, by + bh + 2], radius=5, fill=(0, 0, 0))
    disp = Image.new("RGB", (bw, bh)); dd = ImageDraw.Draw(disp)
    for y in range(bh): dd.line([0, y, bw, y], fill=mix((10, 12, 14), (22, 25, 28), y / bh))
    im.paste(disp, (bx, by)); d = ImageDraw.Draw(im)
    d.line([bx + 4, by + 1, bx + bw - 4, by + 1], fill=(0, 0, 0)); d.line([bx, by + bh, bx + bw, by + bh], fill=(60, 62, 66))
    spaced(d, bx + 8, by + 3, "PRESET", font(7), FAINT, track=1.2)
    qbadge(d, bx + bw - 22, by + 3, 12)
    d.line([948, 14, 948, 46], fill=(56, 58, 62))
    module(im, MOD1, "METERS  &  I / O"); module(im, MOD2, "COMPRESSOR")
    d = ImageDraw.Draw(im)
    for x, side in ((IN_X, 1), (OUT_X, 1)):                                                                 # LED ladders
        d.rounded_rectangle([x - 4, LY - 4, x + LTILE + 3, LY + LTILE * LROWS + 3], radius=3, fill=(2, 2, 3), outline=(70, 72, 76))
        for db in range(0, -49, -3):
            y = LY + LTILE * LROWS * (-db / 48.0)
            tx = x + LTILE + 6 if side > 0 else x - 6
            major = db in (0, -6, -12, -24, -36, -48)
            ln = 4 if major else 2
            d.line([tx, y, tx + ln * side, y], fill=PRINT if major else FAINT)
            if major:
                f = font(9, False); lab = str(db); lw = d.textlength(lab, font=f)
                d.text((tx + 6 if side > 0 else tx - 6 - lw, y - 6), lab, font=f, fill=PRINT if db >= -12 else DIM)
    spaced(d, IN_X + LTILE / 2, LY + LTILE * LROWS + 12, "IN", font(10), PRINT, track=1.5, anchor="c")
    spaced(d, OUT_X + LTILE / 2, LY + LTILE * LROWS + 12, "OUT", font(10), PRINT, track=1.5, anchor="c")
    d.rounded_rectangle([MX - 3, MY - 3, MX + MW + 2, MY + MH + 3], radius=14, fill=(0, 0, 0))
    spaced(d, MX + MW / 2, MY + MH + 12, "COMPRESSION", font(10), DIM, track=2.0, anchor="c")
    for key, (cap, colour, cx, top, q) in KNOBS.items():                                                    # knobs
        kx, ky = knob_centre(key)
        tw = spaced(d, kx, top - 22, cap, font(12), PRINT, track=1.6, anchor="c")
        qbadge(d, kx + tw / 2 + 8, top - 21, q)
        steps = STEPS.get(key, 11)
        for i in range(steps):                                  # printed dot scale
            a = ANG0 + (ANG1 - ANG0) * i / (steps - 1)
            major = key in STEPS or i % 5 == 0
            x, y = ang_xy(kx, ky, KS / 2 + 8, a); r = 2.2 if major else 1.4
            d.ellipse([x - r, y - r, x + r, y + r], fill=PRINT if major else DIM)
        labs = SCALE[key]
        for i, t in enumerate(labs):
            a = ANG0 + (ANG1 - ANG0) * i / (len(labs) - 1)
            x, y = ang_xy(kx, ky, KS / 2 + 24, a)
            text_c(d, x, y, t, font(10, False), PRINT)
        if UNITS[key]: text_c(d, kx, ky + KS / 2 + 18, UNITS[key], font(9, False), DIM)
        bx0, by0 = knob_box(key)
        d.rounded_rectangle([bx0 + 25, by0 + 124, bx0 + CW - 25, by0 + 148], radius=3, fill=(6, 7, 8), outline=(74, 76, 80))
        d.line([bx0 + 27, by0 + 125, bx0 + CW - 27, by0 + 125], fill=(0, 0, 0))
    def frame(x0, y0, x1, y1):
        d.rounded_rectangle([x0, y0, x1, y1], radius=6, outline=FRAME, width=1)
    frame(634, 102, 1246, 330); frame(634, 338, 1246, 566)          # compressor rows
    frame(32, 288, 580, 468); frame(32, 474, 580, 566)              # I/O knobs, switches
    for key, (cap, led, cx, y, q) in SWITCHES.items():                                                      # switches
        tw = spaced(d, cx, y - 22, cap, font(12), PRINT, track=1.6, anchor="c")
        qbadge(d, cx + tw / 2 + 8, y - 21, q)
        d.rounded_rectangle([cx - SW / 2 - 3, y - 3, cx + SW / 2 + 3, y + SW + 3], radius=7, fill=(70, 72, 77))
        d.rounded_rectangle([cx - SW / 2 - 2, y - 2, cx + SW / 2 + 2, y + SW + 2], radius=6, fill=(14, 14, 15))
    d.rectangle([0, FOOT_Y, W, H], fill=(12, 12, 13)); d.line([0, FOOT_Y, W, FOOT_Y], fill=(0, 0, 0))     # footer
    d.line([0, FOOT_Y + 1, W, FOOT_Y + 1], fill=(56, 58, 62))
    d.line([760, FOOT_Y + 6, 760, H - 6], fill=(44, 46, 50))

    return im

# ---------------------------------------------------------------- TUI.json helpers (GlueBus schema)
NOREMAP = {"version": 1, "map": []}
def bounds(b, focus="No", show="Show", visible="Always"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": " ".join(str(int(round(v))) for v in b), "additionalInvalidatingHandles": []}
def comp(name, typ, data, b, remap=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": remap or NOREMAP, "bounds": b}
def bindp(i): return {"version": 1, "map": [{"key": "Data", "value": f"Parameter {i}"}]}
def action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data",
            "additionalData": extra, "handle remapping": NOREMAP}
def bgdata(col="0"): return {"version": 1, "focussed": {"version": 1, "colour": col, "image": ""}, "unfocussed": {"version": 1, "colour": col, "image": ""}}
def definition(actions, parts, bgcol="0", ignore=False):
    return {"version": 4, "actions": actions, "backgroundData": bgdata(bgcol), "ignoreMousePresses": ignore,
            "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True, "componentsData": parts}
def label(kind, h, colour, b, just="horizontallyCentred verticallyCentred", case="Original"):
    return comp(kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": just, "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": hexcol(PRINT, 200), "backgroundInset": 1.0, "outlineThickness": 1.5},
                bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE = lambda: [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
def strip(img, x, y, sq):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds((x, y, sq, sq)))
def btn_part(on, off, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1,
                "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))

PLACED = []
def place(comps, name, key, param, x, y, w, h, kind, extra=None, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((kind, param, x, y, w, h, extra))

READOUTS = {"status": (16, FOOT_Y + 2, 736, 26, 14, DIM, "left verticallyCentred"),
            "preset": (770, FOOT_Y + 2, 494, 26, 15, PRINT, "right verticallyCentred")}

def build():
    PLACED.clear()
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    defs, comps = [], []
    save(background(), "gg_bg.png")
    comps.append(comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "gg_bg.png"}, bounds((0, 0, W, H))))

    for k, t in enumerate(meter_tiles()):
        save(t, f"gg_gr{k}.png")
        defs.append({"key": f"ggGr{k}", "value": definition([], [strip(f"gg_gr{k}.png", 0, 0, MTILE)], ignore=True)})
        place(comps, f"GR {k}", f"ggGr{k}", P["gr"], MX + (k % MCOLS) * MTILE, MY + (k // MCOLS) * MTILE, MTILE, MTILE, "tile", f"gg_gr{k}.png")
    for k, t in enumerate(led_tiles()):
        save(t, f"gg_led{k}.png")
        defs.append({"key": f"ggLed{k}", "value": definition([], [strip(f"gg_led{k}.png", 0, 0, LTILE)], ignore=True)})
        place(comps, f"In {k}", f"ggLed{k}", P["inmeter"], IN_X, LY + k * LTILE, LTILE, LTILE, "tile", f"gg_led{k}.png")
        place(comps, f"Out {k}", f"ggLed{k}", P["outmeter"], OUT_X, LY + k * LTILE, LTILE, LTILE, "tile", f"gg_led{k}.png")

    for colour in sorted({v[1] for v in KNOBS.values()}):
        save(knob_strip(colour), f"gg_knob_{colour}.png")
        defs.append({"key": f"ggKnob_{colour}", "value": definition(CTRL(), [
            focus((0, 0, CW, CH)), strip(f"gg_knob_{colour}.png", (CW - KS) / 2, KNOB_Y0, KS),
            label("Value", 15, hexcol(PRINT), (25, 124, CW - 50, 24))])})
    for key, (cap, colour, cx, top, q) in KNOBS.items():
        x, y = knob_box(key)
        place(comps, cap, f"ggKnob_{colour}", P[key], x, y, CW, CH, "knob", (key, colour), touch=True)

    for key, (cap, led, cx, y, q) in SWITCHES.items():
        save(square_switch(True, led), f"gg_{key}_on.png"); save(square_switch(False, led), f"gg_{key}_off.png")
        defs.append({"key": f"ggSw_{key}", "value": definition(TOGGLE(), [btn_part(f"gg_{key}_on.png", f"gg_{key}_off.png", SW, SW), focus((0, 0, SW, SW))])})
        place(comps, cap, f"ggSw_{key}", P[key], cx - SW / 2, y, SW, SW, "switch", key, touch=True)

    # toolbar taps: same picture on and off (each tap flips the value; the plugin treats any change as a tap)
    labels = {"load": ("LOAD", (255, 196, 90)), "ab": ("A / B", None), "copy": ("COPY", None), "save": ("SAVE", None), "delete": ("DELETE", None)}
    for key in ("prev", "next", "fav", "load", "ab", "copy", "save", "delete"):
        x, y, w, h = TB[key]
        lab, accent = labels.get(key, ("", None))
        save(tool_button(lab, w, h, icon=key if key in ("prev", "next", "fav") else None, accent=accent), f"gg_{key}.png")
        defs.append({"key": f"ggTap_{key}", "value": definition(TOGGLE(), [btn_part(f"gg_{key}.png", f"gg_{key}.png", w, h), focus((0, 0, w, h))])})
        place(comps, key.upper(), f"ggTap_{key}", P[key], x, y, w, h, "tap", key, touch=True)
    save(Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)), "gg_drag.png")
    bx, by, bw, bh = TB["browse"]
    defs.append({"key": "ggBrowse", "value": definition(CTRL(), [
        comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "gg_drag.png", "numFrames": NUMFRAMES, "invert": False,
             "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, bw, bh))),
        label("Value", 20, hexcol(PRINT), (0, 4, bw, bh - 4)), focus((0, 0, bw, bh))])})
    place(comps, "PRESET", "ggBrowse", P["browse"], bx, by, bw, bh, "box", None, touch=True)
    for key, (x, y, w, h, fs, col, just) in READOUTS.items():
        defs.append({"key": f"ggRo_{key}", "value": definition([], [label("Value", fs, hexcol(col), (0, 0, w, h), just)], ignore=True)})
        place(comps, key, f"ggRo_{key}", P[key], x, y, w, h, "ro", (fs, col, just))

    defs.append({"key": "GG|Main", "value": definition([], comps, "ff141416")})
    tabs = [{"version": 3, "tabName": "G-GLUE", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "GG|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)
    ql = ["threshold", "makeup", "attack", "release", "ratio", "scfilter", "mix", "input", "output", "analog", "bypass", "browse"]
    qmap = {f"Q-Link {i + 1}": P[k] for i, k in enumerate(ql)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>gglueaudio.vst.gglue</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    sizes = [Image.open(os.path.join(OUT, f)).size for f in os.listdir(OUT) if f.endswith(".png")]
    strips = [s for s in sizes if s[1] > H]
    bad = [s for s in strips if s[1] != s[0] * FRAMES or s[0] > 72]
    print(f"skin: {len(sizes)} images, {len(strips)} filmstrips, tallest {max(s[1] for s in sizes)} px, rule violations: {len(bad)}")

# ---------------------------------------------------------------- preview from the real plugin
class AEffect(ctypes.Structure): pass
DISP = ctypes.CFUNCTYPE(ctypes.c_ssize_t, ctypes.POINTER(AEffect), ctypes.c_int32, ctypes.c_int32, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_float)
PROC = ctypes.CFUNCTYPE(None, ctypes.POINTER(AEffect), ctypes.POINTER(ctypes.POINTER(ctypes.c_float)), ctypes.POINTER(ctypes.POINTER(ctypes.c_float)), ctypes.c_int32)
SETP = ctypes.CFUNCTYPE(None, ctypes.POINTER(AEffect), ctypes.c_int32, ctypes.c_float)
GETP = ctypes.CFUNCTYPE(ctypes.c_float, ctypes.POINTER(AEffect), ctypes.c_int32)
AEffect._fields_ = [("magic", ctypes.c_int32), ("dispatcher", DISP), ("process", PROC), ("setParameter", SETP), ("getParameter", GETP),
                    ("numPrograms", ctypes.c_int32), ("numParams", ctypes.c_int32), ("numInputs", ctypes.c_int32), ("numOutputs", ctypes.c_int32),
                    ("flags", ctypes.c_int32), ("resvd1", ctypes.c_ssize_t), ("resvd2", ctypes.c_ssize_t), ("initialDelay", ctypes.c_int32),
                    ("realQualities", ctypes.c_int32), ("offQualities", ctypes.c_int32), ("ioRatio", ctypes.c_float), ("object", ctypes.c_void_p),
                    ("user", ctypes.c_void_p), ("uniqueID", ctypes.c_int32), ("version", ctypes.c_int32), ("processReplacing", PROC)]
HOSTCB = ctypes.CFUNCTYPE(ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_float)

def plugin_state(workdir):
    import time
    os.environ["GGLUE_PRESET_DIR"] = os.path.join(workdir, "Presets")
    lib = ctypes.CDLL(LIB)
    cb = HOSTCB(lambda *a: 0)
    lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=48000.0); D(12, val=1)
    n = 512
    IL, IR, OL, OR = [(ctypes.c_float * n)() for _ in range(4)]
    ins = (ctypes.POINTER(ctypes.c_float) * 2)(IL, IR); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    time.sleep(2.7)
    D(2, val=12)                                                   # program 13: Hip-Hop Glue (analog on)
    t = 0
    for _ in range(int(48000 * 2.3 / n)):
        for i in range(n):
            s = ((t + i) % 24000) / 48000.0
            IL[i] = IR[i] = 0.95 * math.exp(-s * 16) * math.sin(2 * math.pi * 52 * s) + 0.3 * math.sin(2 * math.pi * 220 * (t + i) / 48000.0)
        t += n
        e.processReplacing(fx, ins, outs, n)
    vals = [e.getParameter(fx, i) for i in range(e.numParams)]
    txt = []
    for i in range(e.numParams):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); txt.append(b.value.decode("utf-8", "replace"))
    D(1)
    return vals, txt

def preview(outdir):
    import tempfile
    vals, txt = plugin_state(tempfile.mkdtemp())
    im = Image.open(os.path.join(OUT, "gg_bg.png")).convert("RGBA")
    for kind, param, x, y, w, h, extra in PLACED:
        v = vals[param]; fr = max(0, min(NUMFRAMES, round(v * NUMFRAMES)))
        d = ImageDraw.Draw(im)
        if kind == "tile":
            st = Image.open(os.path.join(OUT, extra)).convert("RGBA"); im.alpha_composite(st.crop((0, fr * w, w, fr * w + h)), (int(x), int(y)))
        elif kind == "knob":
            key, colour = extra
            st = Image.open(os.path.join(OUT, f"gg_knob_{colour}.png")).convert("RGBA")
            im.alpha_composite(st.crop((0, fr * KS, KS, fr * KS + KS)), (int(x + (CW - KS) / 2), int(y + KNOB_Y0)))
            d = ImageDraw.Draw(im); text_c(d, x + CW / 2, y + 136, txt[param], font(13), PRINT)
        elif kind == "switch":
            im.alpha_composite(Image.open(os.path.join(OUT, f"gg_{extra}_{'on' if v >= 0.5 else 'off'}.png")).convert("RGBA"), (int(x), int(y)))
        elif kind == "tap":
            im.alpha_composite(Image.open(os.path.join(OUT, f"gg_{extra}.png")).convert("RGBA"), (int(x), int(y)))
        elif kind == "box":
            text_c(d, x + w / 2, y + h / 2 + 2, txt[param], font(17), PRINT)
        elif kind == "ro":
            fs, col, just = extra; f = font(fs * 0.8, bold=False); s = txt[param]
            tw = d.textlength(s, font=f)
            tx = x if just.startswith("left") else (x + w - tw if just.startswith("right") else x + (w - tw) / 2)
            d.text((tx, y + h / 2 - fs * 0.45), s, font=f, fill=col)
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "skin-preview.png"); im.convert("RGB").save(path); print("preview:", path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
