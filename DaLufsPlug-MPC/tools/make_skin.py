#!/usr/bin/env python3
"""Generate the Da Lufs Plug MPC screen skin (GlueBus / RadioReady skin pipeline).

A classic hardware-style loudness meter: a tall LED bar on the left (momentary loudness relative to the target, +-18 LU,
target arrow at 0), big teal digital readouts (Momentary + max, Range, True Peak with an OVER light, Integrated,
Short Term + max, elapsed time), Reset / Pause, two status lines, a platform section (Spotify, Apple Music, YouTube ...
with target and true-peak ceiling, gain to target, peak headroom) and a 60-second short-term history against the target.
Everything shown is a plugin parameter; the readouts are pushed by the plugin when they change.

MPC rules (proven on an MPC X): filmstrips are square frames (image width = frame height); tall controls are split into
square sections; numFrames = last frame index; skin folder "<manufacturer> - VST - <plugin>".
Output: mpc/skin/RadioReady Audio - VST - Da Lufs Plug/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Requires Pillow (+ numpy for the preview). Title font: Great Vibes from tools/fonts/ if present.
Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - Da Lufs Plug")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
VERSION = "1.0.3.0"
TITLE = "Da Lufs Plug"
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
def text_r(d, rx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((rx - (b[2] - b[0]) - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def hexcol(c): return "ff%02x%02x%02x" % c

# ---------- palette: brushed dark metal, teal digits, gold title ----------
BG, CASE = (22, 23, 26), (34, 36, 40)
WELL, WELL_HI = (6, 14, 14), (14, 30, 30)
TEAL, TEAL_DIM = (48, 226, 186), (30, 120, 104)
PRINT, PRINT_DIM = (214, 218, 224), (120, 126, 136)
GOLD, GOLD_DARK = (230, 184, 76), (150, 108, 30)
RED, AMBER = (240, 70, 56), (236, 196, 60)
LINE = (6, 7, 9)

# ---------- parameter indices (must match src/dalufsplug.cpp) ----------
(P_PLATFORM, P_TARGET, P_CEIL, P_PREV, P_NEXT, P_RESET, P_PAUSE, P_M, P_S, P_I, P_LRA, P_TP, P_MAXM, P_MAXS, P_TIME,
 P_GAIN, P_TPHEAD, P_TPOVER, P_STATUS, P_TPSTATUS, P_REL, P_HIST0) = range(22)
NHIST = 60; P_COUNT = P_HIST0 + NHIST
REL, HIST_LO, HIST_HI = 18.0, -12.2, 12.0

# ---------- layout ----------
MX, MY, MW, SEC = 64, 62, 36, 132          # LED bar: 4 square sections of SEC (bottom -18..-9 .. top +9..+18 LU)
AX, AW = 168, 352                          # column A: momentary, range, true peak, reset/pause, status
BX, BW = 532, 368                          # column B: integrated, short term, time
CX, CW = 912, 352                          # column C: platform
HX, HY, HCOL, HH = 540, 526, 12, 84        # history: 60 columns of 12 px, square frames of HH

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
def led_col(v):                             # colour by loudness vs target: green, yellow towards 0, red above
    if v > 0: return RED
    if v > -6: return mix((196, 214, 40), AMBER, (v + 6) / 6)
    return mix((40, 170, 70), (196, 214, 40), max(0.0, (v + 18) / 12))
def meter_section(k):
    """Section k (0 = bottom): frame f is the whole meter at v = -18 + 36 f / 127; this section draws its 9 LU slice."""
    lo = -REL + 9 * k
    strip = Image.new("RGBA", (SEC, SEC * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    x0 = (SEC - MW) // 2
    for f in range(FRAMES):
        v = -REL + 2 * REL * f / (FRAMES - 1); oy = f * SEC
        d.rectangle([x0, oy, x0 + MW - 1, oy + SEC - 1], fill=rgba((8, 9, 10)))
        for y in range(SEC):                                   # y = 0 is the top of this section
            lv = lo + 9 * (SEC - 1 - y) / (SEC - 1)
            lit = f > 0 and lv <= v
            c = led_col(lv)
            if y % 4 == 3: continue                            # LED segment gaps
            d.line([x0 + 3, oy + y, x0 + MW - 4, oy + y], fill=rgba(c if lit else mix(c, (8, 9, 10), 0.86)))
    return strip

def hist_strip():
    """Square HH frames, a HCOL column: frame 0 blank, else a bar from the target line (middle) to v LU (+-12)."""
    strip = Image.new("RGBA", (HH, HH * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    x0 = (HH - HCOL) // 2; mid = HH / 2
    for f in range(1, FRAMES):
        v = HIST_LO + (HIST_HI - HIST_LO) * f / (FRAMES - 1); oy = f * HH
        y = mid - v / HIST_HI * (mid - 3)
        if abs(v) <= 0.5: d.rectangle([x0 + 1, oy + mid - 2, x0 + HCOL - 2, oy + mid + 1], fill=rgba(GOLD)); continue
        c = RED if v > 0 else TEAL
        top, bot = (y, mid) if v > 0 else (mid, y)
        d.rectangle([x0 + 1, oy + top, x0 + HCOL - 2, oy + bot], fill=rgba(c, 170))
        d.rectangle([x0 + 1, oy + (top if v > 0 else bot) - 1, x0 + HCOL - 2, oy + (top if v > 0 else bot) + 1], fill=rgba(mix(c, (255, 255, 255), 0.4)))
    return strip

def led_strip(size=18):
    """TP OVER light: frames < 64 dark, >= 64 red (bound to a 0/1 parameter)."""
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        s = SS; im = Image.new("RGBA", (size * s, size * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        on = f >= 64; c = RED if on else (60, 20, 18)
        d.ellipse([s, s, size * s - s, size * s - s], fill=rgba(c), outline=rgba((10, 10, 10)), width=s)
        if on: d.ellipse([size * s * 0.3, size * s * 0.25, size * s * 0.55, size * s * 0.45], fill=rgba((255, 180, 170)))
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return strip

def drag_strip(sq=16): return Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0))

def pill(text, on, col, w, h, size=12):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=6 * s, fill=rgba(col if on else (46, 49, 55)), outline=rgba((12, 13, 15)), width=2 * s)
    d.line([4 * s, 2 * s, w * s - 4 * s, 2 * s], fill=rgba((255, 255, 255), 40), width=s)
    if text: text_c(d, w * s / 2, h * s / 2, text, font(size * s), rgba((14, 16, 18) if on else PRINT))
    return im.resize((w, h), Image.LANCZOS)

# ---------- background ----------
def well(d, box, caption=None, cap_font=15):
    x0, y0, x1, y1 = box
    d.rounded_rectangle([x0 - 3, y0 - 3, x1 + 3, y1 + 3], radius=5, fill=(52, 55, 60))          # bevel
    d.rounded_rectangle([x0, y0, x1, y1], radius=4, fill=WELL, outline=LINE, width=2)
    for i in range(10):                                                                         # inner glow at the top
        d.line([x0 + 3, y0 + 3 + i, x1 - 3, y0 + 3 + i], fill=mix(WELL_HI, WELL, i / 10))
    if caption: text_c(d, (x0 + x1) / 2, y0 + 16, caption, font(cap_font), TEAL_DIM)

BOXES = {}
def background():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    for y in range(H):                                                                          # brushed case
        d.line([0, y, W, y], fill=mix(CASE, BG, (y % 7) / 14 + 0.3 * y / H))
    for y in range(TITLE_H):
        d.line([0, y, W, y], fill=mix((12, 13, 16), (24, 26, 30), y / TITLE_H))
    d.line([0, TITLE_H, W, TITLE_H], fill=GOLD_DARK); d.line([0, TITLE_H + 1, W, TITLE_H + 1], fill=(70, 52, 18))
    gold_text(im, W / 2, TITLE_H / 2 + 2, "   ".join(TITLE.split(" ")), 37)
    d = ImageDraw.Draw(im)
    # LED bar well + scale + target arrow
    well(d, (MX - 10, MY - 6, MX + MW + 10, MY + 4 * SEC + 6))
    for v in (18, 9, 0, -9, -18):
        y = MY + (REL - v) / (2 * REL) * (4 * SEC)
        d.line([MX - 18, y, MX - 12, y], fill=PRINT_DIM)
        text_r(d, MX - 20, y, f"{v:+d}" if v else "0", font(12), TEAL if v == 0 else PRINT_DIM)
    ya = MY + 2 * SEC
    d.polygon([(MX + MW + 14, ya), (MX + MW + 30, ya - 10), (MX + MW + 30, ya + 10)], fill=GOLD)
    text_c(d, MX + MW / 2 + 8, MY + 4 * SEC + 18, "LU vs TARGET", font(9), PRINT_DIM)
    # boxes
    for key, box, cap in (("mom", (AX, 60, AX + AW, 190), "Momentary"), ("rng", (AX, 200, AX + AW, 302), "Range"),
                          ("tp", (AX, 312, AX + AW, 418), "True Peak (dBTP)"), ("status", (AX, 482, AX + AW, 612), None),
                          ("int", (BX, 60, BX + BW, 250), "Integrated"), ("st", (BX, 260, BX + BW, 420), "Short Term"),
                          ("time", (BX, 430, BX + BW, 506), None), ("hist", (BX, 516, W - 16, 612), None)):
        well(d, box, cap, 17 if key in ("int", "st", "mom") else 15); BOXES[key] = box
    text_r(d, AX + AW - 12, 172, "LUFS", font(16), TEAL); text_r(d, AX + AW - 12, 286, "LU", font(16), TEAL)
    text_r(d, BX + BW - 12, 230, "LUFS", font(18), TEAL); text_r(d, BX + BW - 12, 402, "LUFS", font(18), TEAL)
    text_c(d, AX + 44, 172, "max", font(10), TEAL_DIM); text_c(d, BX + 44, 402, "max", font(10), TEAL_DIM)
    text_c(d, AX + AW / 2, 492, "STATUS", font(10), TEAL_DIM)
    # history
    hx0, hy0, hx1, hy1 = BOXES["hist"]
    d.text((hx0 + 10, hy0 + 4), "SHORT TERM, LAST 60 s, vs TARGET", font=font(9), fill=TEAL_DIM)
    mid = HY + HH / 2
    d.line([HX, mid, HX + NHIST * HCOL, mid], fill=GOLD_DARK)
    for v in (6, -6): d.line([HX, mid - v / HIST_HI * (HH / 2 - 3), HX + NHIST * HCOL, mid - v / HIST_HI * (HH / 2 - 3)], fill=(22, 40, 38))
    text_r(d, hx1 - 6, mid, "0", font(9), GOLD); text_r(d, hx1 - 6, mid - 6 / HIST_HI * (HH / 2 - 3), "+6", font(8), PRINT_DIM)
    text_r(d, hx1 - 6, mid + 6 / HIST_HI * (HH / 2 - 3), "-6", font(8), PRINT_DIM)
    # platform panel
    d.rounded_rectangle([CX, 60, CX + CW, 506], radius=6, fill=(28, 30, 34), outline=LINE, width=2)
    d.rectangle([CX + 1, 60, CX + CW - 1, 63], fill=GOLD_DARK)
    text_c(d, CX + CW / 2, 80, "STREAMING PLATFORM", font(13), GOLD)
    text_c(d, CX + 90, 160, "TARGET (LUFS)", font(10), PRINT_DIM); text_c(d, CX + CW - 90, 160, "PEAK CEILING (dBTP)", font(10), PRINT_DIM)
    for box in ((CX + 12, 94, CX + CW - 12, 136), (CX + 12, 170, CX + 168, 206), (CX + CW - 168, 170, CX + CW - 12, 206)): pass
    well(d, (CX + 16, 236, CX + CW - 16, 318), "Gain to Target", 13); well(d, (CX + 16, 334, CX + CW - 16, 404), "Peak Headroom", 13)
    notes = ["Targets are playback references: you don't", "have to master down to them. Louder masters", "are simply turned down; mind the peaks.",
             "Play the whole song, then read Integrated."]
    for i, s in enumerate(notes): text_c(d, CX + CW / 2, 426 + i * 18, s, font(10, "DejaVuSans.ttf"), PRINT_DIM)
    return im

# ---------- TUI.json ----------
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
def label(h, colour, b, just="horizontallyCentred verticallyCentred", case="Original"):
    return comp("Value", "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": just, "case": case}, "type": "Value", "handleName": "Data"}, bounds(b))
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

PLACED = []
def place(comps, name, key, param, x, y, w, h, kind, extra=None, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((kind, param, x, y, w, h, extra))

READOUTS = {}   # key -> (font height, colour, w, h, justification)
def readout_def(defs, key, fh, col, w, h, just="horizontallyCentred verticallyCentred"):
    READOUTS[key] = (fh, col, w, h, just)
    defs.append({"key": key, "value": definition([], [label(fh, hexcol(col), (0, 0, w, h), just)], ignore=True)})

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    for k in range(4): save(meter_section(k), f"lm_bar{k}.png")
    save(hist_strip(), "lm_hist.png"); save(led_strip(), "lm_led.png"); save(drag_strip(), "lm_drag.png")
    defs = []
    for k in range(4): defs.append({"key": f"lmBar{k}", "value": definition([], [strip_part(f"lm_bar{k}.png", MW, SEC, SEC)], ignore=True)})
    defs.append({"key": "lmHist", "value": definition([], [strip_part("lm_hist.png", HCOL, HH, HH)], ignore=True)})
    defs.append({"key": "lmLed", "value": definition([], [strip_part("lm_led.png", 18, 18, 18)], ignore=True)})
    readout_def(defs, "lmBig", 118, TEAL, BW - 20, 150); readout_def(defs, "lmMid", 92, TEAL, BW - 20, 110)
    readout_def(defs, "lmMom", 70, TEAL, AW - 20, 86); readout_def(defs, "lmRng", 60, TEAL, AW - 20, 70)
    readout_def(defs, "lmTP", 60, TEAL, AW - 20, 70); readout_def(defs, "lmTime", 50, TEAL, BW - 20, 64)
    readout_def(defs, "lmMax", 22, TEAL, 110, 26); readout_def(defs, "lmStatus", 22, TEAL, AW - 20, 30)
    readout_def(defs, "lmStatus2", 17, PRINT, AW - 20, 26); readout_def(defs, "lmGain", 44, GOLD, CW - 40, 50)
    readout_def(defs, "lmHead", 34, TEAL, CW - 40, 40)
    for key, w, h, fs in (("lmPlat", CW - 24 - 96, 42, 19), ("lmBox", 156, 36, 17)):
        drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "lm_drag.png", "numFrames": NUMFRAMES, "invert": False,
                    "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
        READOUTS[key] = (fs, PRINT, w, h, "")
        defs.append({"key": key, "value": definition(CTRL(), [drag, label(fs, hexcol(PRINT), (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))])})
    btns = {"prev": ("\u25C0", 42, 42, GOLD, 16), "next": ("\u25B6", 42, 42, GOLD, 16),
            "reset": ("RESET", 166, 44, (220, 222, 226), 15), "pause": ("PAUSE", 166, 44, AMBER, 15)}
    for k, (txt, w, h, col, fs) in btns.items():
        save(pill(txt, True, col, w, h, fs), f"lm_{k}_on.png"); save(pill(txt, False, col, w, h, fs), f"lm_{k}_off.png")
        defs.append({"key": f"lmBtn_{k}", "value": definition(TOGGLE(), [button_part(f"lm_{k}_on.png", f"lm_{k}_off.png", w, h), focus((0, 0, w, h))])})

    im = background()
    # value boxes are drawn in the background (the controls on top are transparent drag areas)
    d = ImageDraw.Draw(im)
    for box in ((CX + 12 + 48, 94, CX + CW - 12 - 48, 136), (CX + 12, 172, CX + 168, 208), (CX + CW - 168, 172, CX + CW - 12, 208)):
        d.rounded_rectangle(box, radius=5, fill=(44, 47, 53), outline=(12, 13, 15), width=2)
    save(im, "lm_bg.png")

    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "lm_bg.png"}, bounds((0, 0, W, H)))]
    for k in range(4):   # section 0 (bottom) .. 3 (top)
        place(comps, f"Meter {k}", f"lmBar{k}", P_REL, MX, MY + (3 - k) * SEC, MW, SEC, "bar", k)
    place(comps, "Momentary", "lmMom", P_M, AX + 10, 84, AW - 20, 86, "ro", "lmMom")
    place(comps, "Momentary Max", "lmMax", P_MAXM, AX + 62, 159, 110, 26, "ro", "lmMax")
    place(comps, "Range", "lmRng", P_LRA, AX + 10, 222, AW - 20, 70, "ro", "lmRng")
    place(comps, "True Peak", "lmTP", P_TP, AX + 10, 336, AW - 20, 70, "ro", "lmTP")
    place(comps, "TP Over", "lmLed", P_TPOVER, AX + AW - 32, 318, 18, 18, "led")
    place(comps, "Reset", "lmBtn_reset", P_RESET, AX + 4, 428, 166, 44, "btn", "reset", touch=True)
    place(comps, "Pause", "lmBtn_pause", P_PAUSE, AX + AW - 170, 428, 166, 44, "btn", "pause", touch=True)
    place(comps, "Status", "lmStatus", P_STATUS, AX + 10, 510, AW - 20, 30, "ro", "lmStatus")
    place(comps, "TP Status", "lmStatus2", P_TPSTATUS, AX + 10, 552, AW - 20, 26, "ro", "lmStatus2")
    place(comps, "Integrated", "lmBig", P_I, BX + 10, 86, BW - 20, 150, "ro", "lmBig")
    place(comps, "Short Term", "lmMid", P_S, BX + 10, 286, BW - 20, 110, "ro", "lmMid")
    place(comps, "Short Term Max", "lmMax", P_MAXS, BX + 62, 389, 110, 26, "ro", "lmMax")
    place(comps, "Time", "lmTime", P_TIME, BX + 10, 436, BW - 20, 64, "ro", "lmTime")
    for i in range(NHIST):
        place(comps, f"History {i}", "lmHist", P_HIST0 + i, HX + i * HCOL, HY, HCOL, HH, "hist")
    place(comps, "Prev", "lmBtn_prev", P_PREV, CX + 12, 94, 42, 42, "btn", "prev", touch=True)
    place(comps, "Platform", "lmPlat", P_PLATFORM, CX + 60, 94, CW - 24 - 96, 42, "box", "lmPlat", touch=True)
    place(comps, "Next", "lmBtn_next", P_NEXT, CX + CW - 54, 94, 42, 42, "btn", "next", touch=True)
    place(comps, "Target", "lmBox", P_TARGET, CX + 12, 172, 156, 36, "box", "lmBox", touch=True)
    place(comps, "Ceiling", "lmBox", P_CEIL, CX + CW - 168, 172, 156, 36, "box", "lmBox", touch=True)
    place(comps, "Gain to Target", "lmGain", P_GAIN, CX + 20, 260, CW - 40, 50, "ro", "lmGain")
    place(comps, "Peak Headroom", "lmHead", P_TPHEAD, CX + 20, 358, CW - 40, 40, "ro", "lmHead")
    defs.append({"key": "LM|Main", "value": definition([], comps, "ff161719")})

    tabs = [{"version": 3, "tabName": "LOUDNESS", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "LM|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)
    ql = [P_PLATFORM, P_TARGET, P_CEIL, P_PAUSE]
    qmap = {f"Q-Link {i + 1}": p for i, p in enumerate(ql)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>dalufsplug.vst.dalufsplug</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview: rendered from the real plugin (build/native/dalufsplug.so) after 70 s of music-like noise ----------
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

def plugin_state():
    import numpy as np
    lib = ctypes.CDLL(os.path.join(ROOT, "build", "native", "dalufsplug.so"))
    cb = HOSTCB(lambda *a: 2400); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    sr = 44100; D(10, opt=float(sr)); D(2, val=0)
    rng = np.random.default_rng(4); n = sr * 70
    spec = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n, 1 / sr); f[0] = 1
    x = np.fft.irfft(spec / np.sqrt(f), n)                                       # pink noise
    t = np.arange(n) / sr
    env = 10 ** ((-3 * (t < 20) + 2.5 * ((t > 40) & (t < 55)) + 1.2 * np.sin(t * 2.1)) / 20)   # verse / chorus / beat
    x = (x / np.sqrt(np.mean(x ** 2)) * 10 ** (-13.5 / 20) * env).astype(np.float32)
    x[:: int(sr * 0.5)] *= 3.0
    out = np.zeros(4096, np.float32)
    P = ctypes.POINTER(ctypes.c_float)
    for pos in range(0, n - 4096, 4096):
        blk = np.ascontiguousarray(x[pos:pos + 4096])
        ins = (P * 2)(blk.ctypes.data_as(P), blk.ctypes.data_as(P)); outs = (P * 2)(out.ctypes.data_as(P), out.ctypes.data_as(P))
        e.processReplacing(fx, ins, outs, 4096)
    vals = [e.getParameter(fx, i) for i in range(P_COUNT)]
    txt = []
    for i in range(P_COUNT):
        buf = ctypes.create_string_buffer(64); D(7, idx=i, ptr=ctypes.cast(buf, ctypes.c_void_p)); txt.append(buf.value.decode())
    D(1)
    return vals, txt

def preview(outdir):
    vals, txt = plugin_state()
    os.makedirs(outdir, exist_ok=True)
    img = lambda n: Image.open(os.path.join(OUT, n)).convert("RGBA")
    im = img("lm_bg.png"); d = ImageDraw.Draw(im)
    def frame(strip, sq, v, w, h):
        fr = max(0, min(FRAMES - 1, round(v * NUMFRAMES))); cx0, cy0 = (sq - w) // 2, (sq - h) // 2
        return strip.crop((cx0, fr * sq + cy0, cx0 + w, fr * sq + cy0 + h))
    bars = [img(f"lm_bar{k}.png") for k in range(4)]; hist = img("lm_hist.png"); led = img("lm_led.png")
    for (kind, p, x, y, w, h, extra) in PLACED:
        x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h)); v = vals[p]
        if kind == "bar": im.alpha_composite(frame(bars[extra], SEC, v, MW, SEC), (x, y))
        elif kind == "hist": im.alpha_composite(frame(hist, HH, v, HCOL, HH), (x, y))
        elif kind == "led": im.alpha_composite(frame(led, 18, v, 18, 18), (x, y))
        elif kind == "btn": im.alpha_composite(img(f"lm_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
        elif kind in ("ro", "box"):
            fh, col, _, _, _ = READOUTS[extra]
            s = txt[p].upper() if kind == "box" else txt[p]
            size = int(fh * 0.72)
            f = font(size, "DejaVuSans.ttf" if kind == "ro" and fh >= 34 else "DejaVuSans-Bold.ttf")
            while d.textlength(s, font=f) > w - 6 and size > 8: size -= 1; f = font(size)
            text_c(d, x + w / 2, y + h / 2, s, f, col)
    im.convert("RGB").save(os.path.join(outdir, "skin-preview.png"))
    print("preview:", os.path.join(outdir, "skin-preview.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
