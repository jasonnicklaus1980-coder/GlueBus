#!/usr/bin/env python3
"""Generate the RadioReady EQ MPC screen skin (built with the GlueBus / SP1200 / ASR10 / EQ7 skin pipeline).

Three tabs, all with the same graph along the top (live analyzer behind the EQ curve) and the in/out meters on the right:
  EQ GAIN/FREQ  - 8 band panels (power, Freq, Gain, Q, Type, Slope, Channel); Q-Links 1-8 gains, 9-16 frequencies
  EQ Q/TYPE     - the same panels; Q-Links 1-8 Q, 9-16 filter types
  RADIO READY   - preset browser (prev/next, category, favourite, A/B, copy, undo, redo, reset), the QUICK RADIO READY
                  knobs, input/output gain, stereo mode, quality, auto gain, analyzer, curve view and bypass
The graph is made of read-only parameters that the plugin updates: 64 curve columns and 32 analyzer bars.

Rules proven on an MPC X: MPC slices filmstrips into square frames (image width = frame height), so tall or wide
controls use square frames with the drawing centred and the component square and centred on its box (MPC clips it);
numFrames = last frame index; skin folder "<manufacturer> - VST - <plugin>".
Output: mpc/skin/RadioReady Audio - VST - RadioReady EQ/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Requires Pillow. The title uses Great Vibes (SIL OFL, Google Fonts) from tools/fonts/ if present (else a plain italic).
Usage: python3 tools/make_skin.py [preview-dir]   (the preview renders both screens from the built native plugin)
"""
import ctypes, json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - RadioReady EQ")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
VERSION = "1.0.1.0"
TITLE = "Marcus And Moni Radio Ready EQ"          # the gold script title across the top of every tab
TITLE_H = 50
SCRIPT_FONTS = [os.path.join(ROOT, "tools", "fonts", n) for n in ("GreatVibes-Regular.woff", "GreatVibes-Regular.ttf")]
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]

def font(size, name="DejaVuSans-Bold.ttf"):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()
def rgba(c, a=255): return (c[0], c[1], c[2], a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def hexcol(c): return "ff%02x%02x%02x" % c

# ---------- palette: dark studio, gold accents ----------
BG, GRAPH, PANEL, BOX = (18, 20, 25), (12, 15, 21), (28, 31, 38), (40, 44, 53)
GRID, GRID_HI = (32, 38, 50), (60, 68, 84)
PRINT, PRINT_DIM = (222, 224, 230), (124, 130, 144)
GOLD, GOLD_DARK = (230, 184, 76), (150, 108, 30)
CURVE, SPEC = (240, 196, 92), (84, 92, 110)
BANDCOL = [(236, 86, 86), (242, 146, 62), (232, 208, 70), (120, 206, 110), (70, 196, 206), (96, 146, 242), (172, 118, 232), (226, 110, 184)]
LINE = (8, 9, 12)

# ---------- parameter indices (must match src/radioready.cpp) ----------
P_PRESET, P_CATEGORY, P_FAV, P_PREV, P_NEXT, P_BAND0 = 0, 1, 2, 3, 4, 5
def bp(b, w): return P_BAND0 + b * 7 + w          # b = 0..7; w: 0 on, 1 type, 2 freq, 3 gain, 4 q, 5 slope, 6 channel
(P_IN, P_OUT, P_MODE, P_HQ, P_AUTOGAIN, P_BYPASS, P_AMOUNT, P_LOWEND, P_VOCAL, P_AIR, P_PUNCH, P_ANALYZER, P_VIEW, P_AB,
 P_COPY, P_UNDO, P_REDO, P_RESET, P_CURVE0) = range(61, 80)
NCURVE, P_SPEC0, NSPEC, P_METER0, P_LEVEL, P_COUNT = 64, 143, 32, 175, 179, 180

# ---------- layout ----------
COLW, GX, GY, GH = 16, 24, TITLE_H + 10, 190       # graph: 64 columns of 16 px; square frames of GH
GW = COLW * NCURVE                                 # 1024
CURVE_DB, SPEC_FLOOR = 18.0, -84.0
RX0, RX1 = GX + GW + 20, W - 12                    # right column: title + meters
MH, MW = 140, 12                                   # meter drawing (square frames of MH)
MET_Y = GY + 6
BOT = GY + GH + 34                                 # bottom area starts (y 284)
PANEL_W, PANEL_X0, PANEL_H = 152, 16, H - 8 - BOT
BOX_W, BOX_H, SQ_BOX = 104, 30, 104
KB, KS = 118, 78                                   # big / small knob sizes (square frames)

def col_x(i): return GX + i * COLW
def hz_x(hz): return GX + (math.log(hz / 20.0) / math.log(1000.0) * (NCURVE - 1) + 0.5) * COLW
def db_y(db): return GY + GH / 2 - db / CURVE_DB * (GH / 2 - 10)
def panel_x(b): return PANEL_X0 + b * (PANEL_W + 4)
def row_y(k): return BOT + 52 + k * 47

# ---------- gold script title ----------
GOLD_STOPS = [(255, 238, 160), (236, 190, 70), (168, 116, 22), (240, 204, 96), (196, 146, 40)]
def script_font(size):
    for p in SCRIPT_FONTS:
        if os.path.exists(p): return ImageFont.truetype(p, size), True
    print("note: Great Vibes not found in tools/fonts/, using a plain italic for the title")
    return font(int(size * 0.62), "DejaVuSerif-Bold.ttf"), False
def gold_text(im, cx, cy, text, size):
    f, script = script_font(size)
    b = ImageDraw.Draw(im).textbbox((0, 0), text, font=f); w, h = b[2] - b[0], b[3] - b[1]
    pad = 14; mask = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
    ImageDraw.Draw(mask).text((pad - b[0], pad - b[1]), text, font=f, fill=255)
    if not script: mask = mask.transform(mask.size, Image.AFFINE, (1, 0.25, -0.25 * mask.size[1] / 2, 0, 1, 0), resample=Image.BICUBIC)
    grad = Image.new("RGB", mask.size); gd = ImageDraw.Draw(grad)
    for y in range(mask.size[1]):
        k = y / max(1, mask.size[1] - 1) * (len(GOLD_STOPS) - 1); i = min(len(GOLD_STOPS) - 2, int(k)); u = k - i
        gd.line([0, y, mask.size[0], y], fill=tuple(int(GOLD_STOPS[i][j] + (GOLD_STOPS[i + 1][j] - GOLD_STOPS[i][j]) * u) for j in range(3)))
    x0, y0 = int(cx - mask.size[0] / 2), int(cy - mask.size[1] / 2)
    im.paste(Image.new("RGB", mask.size, (0, 0, 0)), (x0 + 2, y0 + 3), mask.filter(ImageFilter.GaussianBlur(2.5)))
    im.paste(Image.new("RGB", mask.size, (120, 84, 16)), (x0, y0), mask.filter(ImageFilter.GaussianBlur(6)).point(lambda v: v // 3))
    im.paste(grad, (x0, y0), mask)

# ---------- filmstrips ----------
def strip_of(size, draw):
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(draw(f / (FRAMES - 1)), (0, f * size))
    return strip

def curve_strip():
    strip = Image.new("RGBA", (GH, GH * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    x0 = (GH - COLW) // 2
    for f in range(FRAMES):
        dbv = -CURVE_DB + 2 * CURVE_DB * f / (FRAMES - 1)
        oy = f * GH; y0 = GH / 2; yv = GH / 2 - dbv / CURVE_DB * (GH / 2 - 10)
        top, bot = (yv, y0) if yv < y0 else (y0, yv)
        if abs(yv - y0) > 1: d.rectangle([x0, oy + top, x0 + COLW - 1, oy + bot], fill=rgba(CURVE, 36))
        d.rectangle([x0, oy + yv - 1.5, x0 + COLW - 1, oy + yv + 1.5], fill=rgba(CURVE, 255))
    return strip

def spec_strip():
    strip = Image.new("RGBA", (GH, GH * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    bw = 2 * COLW; x0 = (GH - bw) // 2
    for f in range(FRAMES):
        t = f / (FRAMES - 1)
        if t <= 0: continue
        oy = f * GH; top = GH - 4 - t * (GH - 12)
        for y in range(int(top), GH - 3):
            k = (y - top) / max(1, GH - 4 - top)
            d.line([x0 + 1, oy + y, x0 + bw - 2, oy + y], fill=rgba(mix((120, 130, 152), SPEC, k), 90))
    return strip

def meter_strip():
    """Square MH frames, a MW bar in the middle: -60 .. +6 dBFS; green, amber from -12, red above 0."""
    def draw(t):
        im = Image.new("RGBA", (MH, MH), (0, 0, 0, 0)); d = ImageDraw.Draw(im); x0 = (MH - MW) // 2
        d.rectangle([x0, 0, x0 + MW - 1, MH - 1], fill=rgba((6, 7, 10)))
        lvl = -60 + 66 * t; y_of = lambda v: MH - 1 - (v + 60) / 66 * (MH - 2)
        if t > 0:
            for y in range(int(y_of(lvl)), MH - 1):
                v = -60 + (MH - 1 - y) / (MH - 2) * 66
                c = (80, 206, 110) if v < -12 else ((236, 190, 70) if v < 0 else (236, 72, 60))
                if y % 3 != 2: d.line([x0 + 1, y, x0 + MW - 2, y], fill=rgba(c))
        return im
    return strip_of(MH, draw)

def knob_strip(size, col):
    def draw(t):
        s = SS; S = size * s; im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        c = S / 2; r = S * 0.46; a0, a1 = 135, 405; a = a0 + (a1 - a0) * t
        d.arc([c - r, c - r, c + r, c + r], a0, a1, fill=rgba((46, 50, 60)), width=int(S * 0.06))
        if t > 0.004: d.arc([c - r, c - r, c + r, c + r], a0, a, fill=rgba(col), width=int(S * 0.06))
        rb = S * 0.36
        d.ellipse([c - rb, c - rb, c + rb, c + rb], fill=rgba((34, 37, 44)), outline=rgba((12, 13, 16)), width=2 * s)
        ri = S * 0.31
        d.ellipse([c - ri, c - ri, c + ri, c + ri], fill=rgba((52, 56, 66)))
        ang = math.radians(a)
        d.line([c + math.cos(ang) * S * 0.10, c + math.sin(ang) * S * 0.10, c + math.cos(ang) * S * 0.30, c + math.sin(ang) * S * 0.30],
               fill=rgba(col), width=int(S * 0.045))
        return im.resize((size, size), Image.LANCZOS)
    return strip_of(size, draw)

def box_img(w, h, fill=BOX, outline=(62, 68, 82), radius=4):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=radius * s, fill=rgba(fill), outline=rgba(outline), width=s)
    return im.resize((w, h), Image.LANCZOS)
def drag_strip(sq=16):
    """Fully transparent frames: the value boxes are drawn in the background, this only makes them drag-to-change
    (a transparent strip may be stretched to any box shape, and costs almost no memory)."""
    return Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0))
def draw_boxes(im, tab):
    for (t, kind, p, x, y, w, h, extra) in PLACED:
        if t == tab and kind in ("box", "wide", "mid"): im.alpha_composite(box_img(int(round(w)), int(round(h))), (int(round(x)), int(round(y))))

def power(on, col, w=36, h=36):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=5 * s, fill=rgba(BOX if on else (26, 28, 34)), outline=rgba((62, 68, 82)), width=s)
    c = col if on else (84, 90, 104); cx, cy, r = w * s / 2, h * s / 2 + s, 8 * s
    d.arc([cx - r, cy - r, cx + r, cy + r], 300, 240, fill=rgba(c), width=3 * s)
    d.line([cx, cy - r - 2 * s, cx, cy - 1 * s], fill=rgba(c), width=3 * s)
    return im.resize((w, h), Image.LANCZOS)

def pill(text, on, col, w, h, size=12):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=min(h * s // 2, 8 * s), fill=rgba(col if on else BOX), outline=rgba((62, 68, 82)), width=s)
    if text: text_c(d, w * s / 2, h * s / 2, text, font(size * s), rgba((18, 18, 22) if on else PRINT))
    return im.resize((w, h), Image.LANCZOS)

# ---------- backgrounds ----------
def title_bar(im):
    d = ImageDraw.Draw(im)
    for y in range(TITLE_H):                                        # dark bar with a thin gold rule under it
        d.line([0, y, W, y], fill=mix((12, 13, 18), (24, 26, 33), y / TITLE_H))
    d.line([0, TITLE_H, W, TITLE_H], fill=GOLD_DARK); d.line([0, TITLE_H + 1, W, TITLE_H + 1], fill=(70, 52, 18))
    gold_text(im, W / 2, TITLE_H / 2 + 2, "   ".join(TITLE.split(" ")), 37)   # script fonts set words tight: widen the gaps
def graph_and_meters(im):
    title_bar(im)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([GX - 8, GY - 4, GX + GW + 8, GY + GH + 20], radius=6, fill=GRAPH, outline=LINE, width=2)
    for v in (-12, -6, 6, 12):
        d.line([GX, db_y(v), GX + GW, db_y(v)], fill=GRID, width=1)
        d.text((GX + GW - 24, db_y(v) - 12), f"{v:+d}", font=font(9), fill=PRINT_DIM)
    d.line([GX, db_y(0), GX + GW, db_y(0)], fill=GRID_HI, width=1); d.text((GX + GW - 24, db_y(0) - 12), "0", font=font(9), fill=PRINT_DIM)
    for hz, lab in ((20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"), (2000, "2k"),
                    (5000, "5k"), (10000, "10k"), (20000, "20k")):
        x = hz_x(hz)
        if 20 < hz < 20000: d.line([x, GY, x, GY + GH], fill=GRID, width=1)
        text_c(d, min(max(x, GX + 10), GX + GW - 12), GY + GH + 8, lab, font(9), PRINT_DIM)
    d.rounded_rectangle([RX0 - 8, GY - 4, RX1, GY + GH + 20], radius=6, fill=PANEL, outline=LINE, width=2)
    d = ImageDraw.Draw(im)
    for k, x in enumerate(meter_x()):
        text_c(d, x + MW / 2, MET_Y + MH + 10, "LR"[k % 2], font(9), PRINT_DIM)
    text_c(d, (meter_x()[0] + meter_x()[1] + MW) / 2, MET_Y + MH + 24, "IN", font(10), PRINT)
    text_c(d, (meter_x()[2] + meter_x()[3] + MW) / 2, MET_Y + MH + 24, "OUT", font(10), PRINT)
    text_c(d, (RX0 + RX1) / 2 - 4, MET_Y + MH + 38, "LEVEL COMP", font(8), PRINT_DIM)
    for v in (0, -12, -24, -48):
        y = MET_Y + MH - 1 - (v + 60) / 66 * (MH - 2)
        text_c(d, (RX0 + RX1) / 2 - 4, y, f"{v}", font(8), PRINT_DIM)
    return d
def meter_x():
    c = (RX0 + RX1) / 2 - 4
    return [int(c - 58), int(c - 38), int(c + 26), int(c + 46)]

def bg_eq():
    im = Image.new("RGB", (W, H), BG); graph_and_meters(im); d = ImageDraw.Draw(im)
    for b in range(8):
        x = panel_x(b)
        d.rounded_rectangle([x, BOT, x + PANEL_W, BOT + PANEL_H], radius=6, fill=PANEL, outline=LINE)
        d.rectangle([x + 1, BOT, x + PANEL_W - 1, BOT + 3], fill=BANDCOL[b])
        text_c(d, x + 98, BOT + 26, f"BAND {b + 1}", font(13), BANDCOL[b])
        for k, lab in enumerate(("FREQ", "GAIN", "Q", "TYPE", "SLOPE", "CHAN")):
            text_c(d, x + 22, row_y(k) + BOX_H / 2, lab, font(9), PRINT_DIM)
    im = im.convert("RGBA"); draw_boxes(im, "eq")
    return im.convert("RGB")

# RADIO READY page areas
PA_X, PA_W = 16, 452          # presets
QA_X, QA_W = 476, 404         # quick section
OA_X = 888                    # output section
OA_W = W - 12 - OA_X
def bg_rr():
    im = Image.new("RGB", (W, H), BG); graph_and_meters(im); d = ImageDraw.Draw(im)
    for x, w, name in ((PA_X, PA_W, "PRESETS"), (QA_X, QA_W, "QUICK RADIO READY"), (OA_X, OA_W, "LEVELS & MODES")):
        d.rounded_rectangle([x, BOT, x + w, BOT + PANEL_H], radius=6, fill=PANEL, outline=LINE)
        d.rectangle([x + 1, BOT, x + w - 1, BOT + 3], fill=GOLD_DARK)
        text_c(d, x + w / 2, BOT + 20, name, font(13), GOLD)
    text_c(d, PA_X + 50, BOT + 116, "CATEGORY", font(9), PRINT_DIM)
    notes = ["EQ is one step of a radio-ready record: balance, arrangement,",
             "compression and mastering still matter. Presets are starting",
             "points - trust your ears and check on several systems.",
             "Save your own: MPC's plugin preset save (every setting is kept)."]
    for i, s in enumerate(notes): d.text((PA_X + 14, BOT + 240 + i * 17), s, font=font(10, "DejaVuSans.ttf"), fill=PRINT_DIM)
    # quick knob captions
    for (cx, cy, size, lab) in quick_knobs():
        text_c(d, cx, cy + size / 2 + 27, lab, font(10), PRINT)
    for (cx, cy, size, lab) in io_knobs():
        text_c(d, cx, cy + size / 2 + 27, lab, font(10), PRINT)
    im = im.convert("RGBA"); draw_boxes(im, "rr")
    return im.convert("RGB")

def quick_knobs():   # (centre x, centre y, size, caption) for Amount, Low-End, Vocal, Air, Punch
    x0, y0 = QA_X, BOT
    return [(x0 + 88, y0 + 150, KB, "AMOUNT"),
            (x0 + 220, y0 + 92, KS, "LOW-END CONTROL"), (x0 + 336, y0 + 92, KS, "VOCAL CLARITY"),
            (x0 + 220, y0 + 232, KS, "AIR & PRESENCE"), (x0 + 336, y0 + 232, KS, "PUNCH")]
def io_knobs():
    return [(OA_X + 70, BOT + 92, KS, "INPUT"), (OA_X + 176, BOT + 92, KS, "OUTPUT")]

# ---------- TUI.json (GlueBus schema) ----------
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
def label(kind, h, colour, b, case="Original", name=None):
    return comp(name or kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": hexcol(GOLD), "backgroundInset": 1.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE = lambda: [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
def strip_part(img, box_w, box_h, sq):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((box_w - sq) / 2, (box_h - sq) / 2, sq, sq)))
def button_part(on_img, off_img, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": 1, "numButtonsInGroup": 1,
                "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))

PLACED = []   # (tab, kind, param, x, y, w, h, extra) - used by the preview
def place(comps, tab, name, key, param, x, y, w, h, kind, extra=None, touch=True):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((tab, kind, param, x, y, w, h, extra))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    save(curve_strip(), "rr_curve.png"); save(spec_strip(), "rr_spec.png"); save(meter_strip(), "rr_meter.png")
    save(drag_strip(), "rr_drag.png")
    save(knob_strip(KB, GOLD), "rr_knob_big.png"); save(knob_strip(KS, GOLD), "rr_knob.png")
    defs = []
    defs.append({"key": "rrCurveCol", "value": definition([], [strip_part("rr_curve.png", COLW, GH, GH)], ignore=True)})
    defs.append({"key": "rrSpecBar", "value": definition([], [strip_part("rr_spec.png", 2 * COLW, GH, GH)], ignore=True)})
    defs.append({"key": "rrMeter", "value": definition([], [strip_part("rr_meter.png", MW, MH, MH)], ignore=True)})
    defs.append({"key": "rrReadout", "value": definition([], [label("Value", 13, hexcol(PRINT), (0, 0, 150, 18), case="Upper Case")], ignore=True)})
    for key, w, h, fs in (("rrBox", BOX_W, BOX_H, 14), ("rrWide", 290, 36, 17), ("rrMid", 172, 32, 14)):
        drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "rr_drag.png", "numFrames": NUMFRAMES, "invert": False,
                    "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
        defs.append({"key": key, "value": definition(CTRL(), [drag, label("Value", fs, hexcol(PRINT), (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))])})
    for key, img, size in (("rrKnobBig", "rr_knob_big.png", KB), ("rrKnob", "rr_knob.png", KS)):
        defs.append({"key": key, "value": definition(CTRL(), [
            strip_part(img, size, size, size), label("Value", 13, hexcol(PRINT), (0, size - 4, size, 20), case="Upper Case"),
            focus((0, 0, size, size + 16))])})
    for b in range(8):
        save(power(True, BANDCOL[b]), f"rr_pwr_{b}_on.png"); save(power(False, BANDCOL[b]), f"rr_pwr_{b}_off.png")
        defs.append({"key": f"rrPower{b}", "value": definition(TOGGLE(), [button_part(f"rr_pwr_{b}_on.png", f"rr_pwr_{b}_off.png", 36, 36), focus((0, 0, 36, 36))])})
    # buttons with fixed text (momentary actions and on/off switches)
    btns = {"prev": ("\u25C0", 40, 36, GOLD, 16), "next": ("\u25B6", 40, 36, GOLD, 16), "fav": ("\u2605", 40, 36, GOLD, 18),
            "copy": ("COPY", 76, 32, GOLD, 11), "undo": ("UNDO", 76, 32, GOLD, 11), "redo": ("REDO", 76, 32, GOLD, 11),
            "reset": ("RESET", 76, 32, GOLD, 11), "auto": ("AUTO GAIN", 150, 32, GOLD, 11), "byp": ("BYPASS", 150, 32, (224, 92, 80), 11)}
    for k, (txt, w, h, col, fs) in btns.items():
        save(pill(txt, True, col, w, h, fs), f"rr_{k}_on.png"); save(pill(txt, False, col, w, h, fs), f"rr_{k}_off.png")
        defs.append({"key": f"rrBtn_{k}", "value": definition(TOGGLE(), [button_part(f"rr_{k}_on.png", f"rr_{k}_off.png", w, h), focus((0, 0, w, h))])})
    # two-state switches that show their value (A/B, stereo mode, quality, curve view)
    for k, w in (("sw", 150), ("ab", 76)):
        save(pill("", True, (104, 84, 40), w, 32), f"rr_{k}_on.png"); save(pill("", False, GOLD, w, 32), f"rr_{k}_off.png")
        defs.append({"key": f"rrSwitch_{k}", "value": definition(TOGGLE(), [
            button_part(f"rr_{k}_on.png", f"rr_{k}_off.png", w, 32), label("Value", 13, hexcol(PRINT), (0, 0, w, 32), case="Upper Case"),
            focus((0, 0, w, 32))])})

    def common(tab, bg):
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bg}, bounds((0, 0, W, H)))]
        for i in range(NSPEC):
            place(comps, tab, f"Spectrum {i}", "rrSpecBar", P_SPEC0 + i, col_x(2 * i), GY, 2 * COLW, GH, "spec", touch=False)
        for i in range(NCURVE):
            place(comps, tab, f"Curve {i}", "rrCurveCol", P_CURVE0 + i, col_x(i), GY, COLW, GH, "curve", touch=False)
        for k, x in enumerate(meter_x()):
            place(comps, tab, f"Meter {k}", "rrMeter", P_METER0 + k, x, MET_Y, MW, MH, "meter", touch=False)
        place(comps, tab, "Level Comp", "rrReadout", P_LEVEL, (RX0 + RX1) / 2 - 79, MET_Y + MH + 44, 150, 18, "readout", touch=False)
        return comps

    eq = common("eq", "rr_bg_eq.png")
    for b in range(8):
        x = panel_x(b)
        place(eq, "eq", f"Band {b + 1} On", f"rrPower{b}", bp(b, 0), x + 8, BOT + 9, 36, 36, "power", b)
        for k, w in enumerate((2, 3, 4, 1, 5, 6)):
            place(eq, "eq", f"Band {b + 1} {('Freq', 'Gain', 'Q', 'Type', 'Slope', 'Channel')[k]}", "rrBox", bp(b, w), x + 42, row_y(k), BOX_W, BOX_H, "box")
    defs.append({"key": "RR|EQ", "value": definition([], eq, "ff121419")})

    rr = common("rr", "rr_bg_rr.png")
    y1 = BOT + 44
    place(rr, "rr", "Prev", "rrBtn_prev", P_PREV, PA_X + 12, y1, 40, 36, "btn", "prev")
    place(rr, "rr", "Preset", "rrWide", P_PRESET, PA_X + 58, y1, 290, 36, "wide")
    place(rr, "rr", "Next", "rrBtn_next", P_NEXT, PA_X + 354, y1, 40, 36, "btn", "next")
    place(rr, "rr", "Favorite", "rrBtn_fav", P_FAV, PA_X + 400, y1, 40, 36, "btn", "fav")
    place(rr, "rr", "Category", "rrMid", P_CATEGORY, PA_X + 96, BOT + 100, 172, 32, "mid")
    y3 = BOT + 162
    place(rr, "rr", "A/B", "rrSwitch_ab", P_AB, PA_X + 12, y3, 76, 32, "switch", "ab")
    for k, (nm, p) in enumerate((("copy", P_COPY), ("undo", P_UNDO), ("redo", P_REDO), ("reset", P_RESET))):
        place(rr, "rr", nm.title(), f"rrBtn_{nm}", p, PA_X + 96 + k * 86, y3, 76, 32, "btn", nm)
    for (cx, cy, size, lab), p in zip(quick_knobs(), (P_AMOUNT, P_LOWEND, P_VOCAL, P_AIR, P_PUNCH)):
        place(rr, "rr", lab.title(), "rrKnobBig" if size == KB else "rrKnob", p, cx - size / 2, cy - size / 2, size, size + 16, "knob", size)
    for (cx, cy, size, lab), p in zip(io_knobs(), (P_IN, P_OUT)):
        place(rr, "rr", lab.title(), "rrKnob", p, cx - size / 2, cy - size / 2, size, size + 16, "knob", size)
    gx = [OA_X + 16, OA_X + 16 + 160]
    place(rr, "rr", "Stereo Mode", "rrSwitch_sw", P_MODE, gx[0], BOT + 170, 150, 32, "switch", "sw")
    place(rr, "rr", "Quality", "rrSwitch_sw", P_HQ, gx[1], BOT + 170, 150, 32, "switch", "sw")
    place(rr, "rr", "Auto Gain", "rrBtn_auto", P_AUTOGAIN, gx[0], BOT + 212, 150, 32, "btn", "auto")
    place(rr, "rr", "Curve View", "rrSwitch_sw", P_VIEW, gx[1], BOT + 212, 150, 32, "switch", "sw")
    place(rr, "rr", "Analyzer", "rrMid", P_ANALYZER, gx[0] - 11, BOT + 254, 172, 32, "mid")
    place(rr, "rr", "Bypass", "rrBtn_byp", P_BYPASS, gx[1], BOT + 254, 150, 32, "btn", "byp")
    defs.append({"key": "RR|Quick", "value": definition([], rr, "ff121419")})
    save(bg_eq(), "rr_bg_eq.png"); save(bg_rr(), "rr_bg_rr.png")          # after placing: the value boxes are drawn in

    tabs = [{"version": 3, "tabName": name, "fnKeyIndex": i, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": cn, "initialSize": f"0 0 {W} {H}", "scale": 1.0}
            for i, (name, cn) in enumerate((("EQ GAIN/FREQ", "RR|EQ"), ("EQ Q/TYPE", "RR|EQ"), ("RADIO READY", "RR|Quick")))]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)

    ql_a = [bp(b, 3) for b in range(8)] + [bp(b, 2) for b in range(8)]
    ql_b = [bp(b, 4) for b in range(8)] + [bp(b, 1) for b in range(8)]
    ql_c = [P_AMOUNT, P_LOWEND, P_VOCAL, P_AIR, P_PUNCH, P_IN, P_OUT, P_PRESET,
            P_CATEGORY, P_MODE, P_HQ, P_AUTOGAIN, P_ANALYZER, P_AB, P_VIEW, P_BYPASS]
    qmap = lambda ids: {f"Q-Link {i + 1}": p for i, p in enumerate(ids)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [
             {"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql_a)},
             {"Tab": 2, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql_b)},
             {"Tab": 3, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql_c)}]},
         "Program Mode Q-Links": qmap(ql_c)}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>radioready.vst.radioreadyeq</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview: both screens rendered from the real plugin (build/native/radioready.so) ----------
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

def plugin_state(program):
    lib = ctypes.CDLL(os.path.join(ROOT, "build", "native", "radioready.so"))
    cb = HOSTCB(lambda *a: 2400); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    sr = 44100; D(10, opt=float(sr)); D(12, val=1); D(2, val=program)
    import random; random.seed(3); n = 512
    L = (ctypes.c_float * n)(); R = (ctypes.c_float * n)(); OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)()
    ins = (ctypes.POINTER(ctypes.c_float) * 2)(L, R); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    b = [0.0] * 7; t = 0
    for blk in range(int(1.5 * sr / n)):                       # a beat-like signal: pink-ish noise + a decaying 55 Hz "808"
        for i in range(n):
            w = random.uniform(-1, 1)
            b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.969 * b[2] + w * 0.153852
            b[3] = 0.8665 * b[3] + w * 0.3104856; b[4] = 0.55 * b[4] + w * 0.5329522
            k = (t % 22050) / sr; x = 0.05 * sum(b[:5]) + 0.45 * math.exp(-k * 5) * math.sin(2 * math.pi * 55 * k)
            L[i] = R[i] = x; t += 1
        e.processReplacing(fx, ins, outs, n)
    vals = [e.getParameter(fx, i) for i in range(P_COUNT)]
    txt = []
    for i in range(P_COUNT):
        buf = ctypes.create_string_buffer(64); D(7, idx=i, ptr=ctypes.cast(buf, ctypes.c_void_p)); txt.append(buf.value.decode())
    D(1)
    return vals, txt

def preview(outdir, program=1):
    vals, txt = plugin_state(program)
    os.makedirs(outdir, exist_ok=True)
    img = lambda n: Image.open(os.path.join(OUT, n)).convert("RGBA")
    def frame(strip, sq, v, w, h):
        fr = round(v * NUMFRAMES); fr = max(0, min(FRAMES - 1, fr))
        cx0, cy0 = (sq - w) // 2, (sq - h) // 2
        return strip.crop((cx0, fr * sq + cy0, cx0 + w, fr * sq + cy0 + h))
    cache = {}
    def st(n):
        if n not in cache: cache[n] = img(n)
        return cache[n]
    for tab, bgname, fn in (("eq", "rr_bg_eq.png", "skin-preview-eq.png"), ("rr", "rr_bg_rr.png", "skin-preview-radioready.png")):
        im = img(bgname); d = ImageDraw.Draw(im); f = lambda s: font(s)
        for (t, kind, p, x, y, w, h, extra) in PLACED:
            if t != tab: continue
            x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h)); v = vals[p]
            if kind == "curve": im.alpha_composite(frame(st("rr_curve.png"), GH, v, COLW, GH), (x, y))
            elif kind == "spec": im.alpha_composite(frame(st("rr_spec.png"), GH, v, 2 * COLW, GH), (x, y))
            elif kind == "meter": im.alpha_composite(frame(st("rr_meter.png"), MH, v, MW, MH), (x, y))
            elif kind == "readout": text_c(d, x + w / 2, y + h / 2, txt[p].upper(), f(11), PRINT)
            elif kind == "power": im.alpha_composite(st(f"rr_pwr_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
            elif kind in ("box", "wide", "mid"):
                text_c(d, x + w / 2, y + h / 2, txt[p].upper(), f(15 if kind == "wide" else 12), PRINT)
            elif kind == "knob":
                im.alpha_composite(frame(st("rr_knob_big.png" if extra == KB else "rr_knob.png"), extra, v, extra, extra), (x, y))
                text_c(d, x + w / 2, y + extra + 6, txt[p].upper(), f(11), PRINT)
            elif kind == "btn": im.alpha_composite(st(f"rr_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
            elif kind == "switch":
                im.alpha_composite(st(f"rr_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
                text_c(d, x + w / 2, y + h / 2, txt[p].upper(), f(12), PRINT)
        im.convert("RGB").save(os.path.join(outdir, fn))
        print("preview:", os.path.join(outdir, fn))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
