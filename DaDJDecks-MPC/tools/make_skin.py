#!/usr/bin/env python3
"""Generate the Da DJ Decks MPC screen skin (GlueBus / RadioReady skin pipeline).

Two decks and a mixer on one screen:
  deck (A left, B right): track selector + LOAD, now playing, BPM / pitch / elapsed / remaining, a progress bar,
    a spinning platter with 4 beat lights, pitch fader + range, SYNC, LOOP, NUDGE -/+, CUE and PLAY/PAUSE
  deck row 2: REC (samples the MPC input into the deck) + light, SP-12 mode, TRANSFORM + rate; drag on the platter to SCRATCH
  mixer: MPC IN and MASTER, per channel GAIN / HIGH / MID / LOW / FILTER knobs, meters and faders, crossfader + curve
  library line + RESCAN under deck A.
Readouts are plugin parameters the plugin updates; LED-style parts (progress, beat lights, meters) are one solid
colour per frame, so they draw right however MPC positions the frame.

MPC rules (proven on an MPC X): filmstrips are square frames (image width = frame height); tall or wide controls use
square frames centred on their box (MPC clips them); numFrames = last frame index; skin folder
"<manufacturer> - VST - <plugin>".
Output: mpc/skin/RadioReady Audio - VST - Da DJ Decks/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Requires Pillow. Title font: Great Vibes from tools/fonts/ if present.
Usage: python3 tools/make_skin.py [preview-dir]   (the preview plays the test tracks from `make test` in build/native)
"""
import ctypes, json, math, os, shutil, sys, time
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - Da DJ Decks")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
VERSION = "1.1.0.0"
TITLE = "Da DJ Decks"
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

# ---------- palette ----------
BG, PANEL, WELL = (16, 17, 21), (30, 32, 38), (8, 10, 13)
PRINT, PRINT_DIM = (220, 222, 228), (118, 124, 136)
GOLD, GOLD_DARK = (230, 184, 76), (150, 108, 30)
CYAN = (60, 210, 230)
DECK_COL = [(80, 170, 255), (255, 120, 90)]           # deck A blue, deck B orange
GREEN, AMBER, RED = (70, 200, 100), (236, 196, 60), (236, 70, 56)
LINE = (6, 7, 9)

# ---------- parameter indices (must match src/dadjdecks.cpp) ----------
(D_TRACK, D_LOAD, D_PLAY, D_CUE, D_PITCH, D_RANGE, D_SYNC, D_NUDGE_DN, D_NUDGE_UP, D_LOOP, D_GAIN, D_HIGH, D_MID, D_LOW,
 D_FILTER, D_FADER, D_LOADED, D_SCRATCH, D_TRANS, D_TRANS_RATE, D_REC, D_SP12,
 D_TIME, D_REMAIN, D_BPM, D_PLATTER, D_PROGRESS, D_VU, D_STATUS, D_BEAT, D_RECLIGHT, D_COUNT) = range(32)
def dp(d, w): return d * D_COUNT + w
M_XFADE = 2 * D_COUNT
M_CURVE, M_MASTER, M_INPUT, M_RESCAN, M_VU_L, M_VU_R, M_LIBRARY = range(M_XFADE + 1, M_XFADE + 8)
P_COUNT = M_LIBRARY + 1
VU_LO, VU_HI = -40.0, 6.0

# ---------- layout ----------
DECK_X = [12, 780]; DECK_W = 488
MIX_X, MIX_W = 508, 264
PL = 150                    # platter / pitch fader frames
NPROG, NVU, NMVU = 44, 16, 8
KN = 44                     # knob size
VF = 120                    # volume fader frames
XF = 200                    # crossfader frames

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
def solid_strip(sq, on_col, off_col, lit):
    """Square sq frames, each one solid colour: lit(v) decides, v = frame / 127."""
    strip = Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    for f in range(FRAMES):
        d.rectangle([0, f * sq, sq - 1, f * sq + sq - 1], fill=rgba(on_col if lit(f / (FRAMES - 1)) else off_col))
    return strip

def platter_strip(col):
    size = PL; strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    s = 2; S = size * s
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(base)
    c = S / 2
    d.ellipse([2, 2, S - 3, S - 3], fill=rgba((52, 54, 60)))                     # platter rim
    d.ellipse([10, 10, S - 11, S - 11], fill=rgba((14, 14, 16)))                 # vinyl
    for r in range(int(S * 0.2), int(S * 0.47), 5):                              # grooves
        d.ellipse([c - r, c - r, c + r, c + r], outline=rgba((30, 30, 34)), width=1)
    d.ellipse([c - S * 0.17, c - S * 0.17, c + S * 0.17, c + S * 0.17], fill=rgba(col))  # label
    d.ellipse([c - 5, c - 5, c + 5, c + 5], fill=rgba((200, 200, 205)))          # spindle
    for f in range(FRAMES):
        ang = -360.0 * f / FRAMES
        im = base.copy(); dd = ImageDraw.Draw(im)
        a = math.radians(ang - 90)
        dd.line([c + math.cos(a) * S * 0.19, c + math.sin(a) * S * 0.19, c + math.cos(a) * S * 0.46, c + math.sin(a) * S * 0.46],
                fill=rgba((235, 235, 240)), width=4)                             # position marker
        a2 = math.radians(ang + 90)
        dd.ellipse([c + math.cos(a2) * S * 0.09 - 8, c + math.sin(a2) * S * 0.09 - 8, c + math.cos(a2) * S * 0.09 + 8, c + math.sin(a2) * S * 0.09 + 8],
                   fill=rgba(mix(col, (0, 0, 0), 0.5)))                         # label mark (shows the spin)
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return strip

def vfader_strip(size, width, center_line=False, cap_col=(220, 222, 228)):
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        t = f / (FRAMES - 1); s = SS
        im = Image.new("RGBA", (width * s, size * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        cx = width * s / 2; y0, y1 = 10 * s, (size - 10) * s
        d.rounded_rectangle([cx - 3 * s, y0, cx + 3 * s, y1], radius=3 * s, fill=rgba((4, 4, 6)))
        if center_line: d.line([cx - 12 * s, (y0 + y1) / 2, cx + 12 * s, (y0 + y1) / 2], fill=rgba(GOLD), width=s)
        cy = y1 + (y0 - y1) * t
        d.rounded_rectangle([cx - 13 * s, cy - 8 * s, cx + 13 * s, cy + 8 * s], radius=3 * s, fill=rgba(cap_col), outline=rgba((10, 10, 12)), width=s)
        d.line([cx - 11 * s, cy, cx + 11 * s, cy], fill=rgba((20, 20, 24)), width=2 * s)
        im = im.resize((width, size), Image.LANCZOS)
        strip.paste(im, ((size - width) // 2, f * size))
    return strip

def xfader_strip():
    size = XF; strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0)); h = 40
    for f in range(FRAMES):
        t = f / (FRAMES - 1); s = SS
        im = Image.new("RGBA", (size * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        cy = h * s / 2; x0, x1 = 14 * s, (size - 14) * s
        d.rounded_rectangle([x0, cy - 3 * s, x1, cy + 3 * s], radius=3 * s, fill=rgba((4, 4, 6)))
        d.line([(x0 + x1) / 2, cy - 12 * s, (x0 + x1) / 2, cy + 12 * s], fill=rgba(GOLD), width=s)
        cx = x0 + (x1 - x0) * t
        d.rounded_rectangle([cx - 9 * s, cy - 15 * s, cx + 9 * s, cy + 15 * s], radius=3 * s, fill=rgba((220, 222, 228)), outline=rgba((10, 10, 12)), width=s)
        d.line([cx, cy - 13 * s, cx, cy + 13 * s], fill=rgba((20, 20, 24)), width=2 * s)
        strip.paste(im.resize((size, h), Image.LANCZOS), (0, f * size + (size - h) // 2))
    return strip

def knob_strip(size, col):
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        t = f / (FRAMES - 1); s = SS; S = size * s
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        c = S / 2; r = S * 0.45; a0, a1 = 135, 405; a = a0 + (a1 - a0) * t
        d.arc([c - r, c - r, c + r, c + r], a0, a1, fill=rgba((50, 54, 62)), width=int(S * 0.08))
        mid = 270
        lo, hi = (min(a, mid), max(a, mid))
        if abs(a - mid) > 1: d.arc([c - r, c - r, c + r, c + r], lo, hi, fill=rgba(col), width=int(S * 0.08))   # from the centre (EQ style)
        rb = S * 0.33
        d.ellipse([c - rb, c - rb, c + rb, c + rb], fill=rgba((48, 51, 58)), outline=rgba((10, 10, 12)), width=s)
        ang = math.radians(a)
        d.line([c + math.cos(ang) * S * 0.08, c + math.sin(ang) * S * 0.08, c + math.cos(ang) * S * 0.3, c + math.sin(ang) * S * 0.3],
               fill=rgba((240, 240, 244)), width=int(S * 0.06))
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return strip

def drag_strip(sq=16): return Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0))

def pill(text, on, col, w, h, size=12, dark_on=True):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=6 * s, fill=rgba(col if on else (44, 47, 54)), outline=rgba((10, 11, 13)), width=2 * s)
    d.line([5 * s, 2 * s, w * s - 5 * s, 2 * s], fill=rgba((255, 255, 255), 40), width=s)
    if text: text_c(d, w * s / 2, h * s / 2, text, font(size * s), rgba((14, 16, 18) if on and dark_on else (col if not on else PRINT)))
    return im.resize((w, h), Image.LANCZOS)

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
def strip_part(img, box_w, box_h, sq, orient="Vertical"):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": orient, "handleName": "Data"}, bounds(((box_w - sq) / 2, (box_h - sq) / 2, sq, sq)))
def button_part(on_img, off_img, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on_img, "offImage": off_img, "buttonId": 1, "numButtonsInGroup": 1,
                "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))

PLACED = []; READOUTS = {}; BOXES = []
def place(comps, name, key, param, x, y, w, h, kind, extra=None, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((kind, param, x, y, w, h, extra))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    defs = []
    def readout(key, fh, col, w, h, just="horizontallyCentred verticallyCentred"):
        READOUTS[key] = (fh, col, w, h, just)
        defs.append({"key": key, "value": definition([], [label(fh, hexcol(col), (0, 0, w, h), just)], ignore=True)})
    def box(key, w, h, fs):
        READOUTS[key] = (fs, PRINT, w, h, "")
        drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "dj_drag.png", "numFrames": NUMFRAMES, "invert": False,
                    "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
        defs.append({"key": key, "value": definition(CTRL(), [drag, label(fs, hexcol(PRINT), (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))])})
    def btn(key, txt, w, h, col, fs, dark_on=True):
        save(pill(txt, True, col, w, h, fs, dark_on), f"dj_{key}_on.png"); save(pill(txt, False, col, w, h, fs, dark_on), f"dj_{key}_off.png")
        defs.append({"key": f"djBtn_{key}", "value": definition(TOGGLE(), [button_part(f"dj_{key}_on.png", f"dj_{key}_off.png", w, h), focus((0, 0, w, h))])})

    save(drag_strip(), "dj_drag.png")
    for d in range(2): save(platter_strip(DECK_COL[d]), f"dj_platter{d}.png")
    save(vfader_strip(PL, 60, center_line=True), "dj_pitch.png")
    save(vfader_strip(VF, 50), "dj_fader.png")
    save(xfader_strip(), "dj_xfader.png")
    for d in range(2): save(knob_strip(KN, DECK_COL[d]), f"dj_knob{d}.png")
    save(knob_strip(KN, GOLD), "dj_knobm.png")
    # LED strips (solid frames): progress (per deck colour), beat lights, meters
    for d in range(2):
        for k in range(NPROG):
            save(solid_strip(12, DECK_COL[d], mix(DECK_COL[d], (8, 9, 12), 0.82), lambda v, k=k: v >= (k + 0.5) / NPROG), f"dj_prog{d}_{k}.png")
    for k in range(4):
        save(solid_strip(14, GOLD if k == 0 else (240, 240, 240), (40, 40, 46), lambda v, k=k: int(v * 4 - 1e-9) == k and v > 0), f"dj_beat{k}.png")
    for k in range(NVU):
        lo = VU_LO + (VU_HI - VU_LO) * k / NVU; mid = lo + (VU_HI - VU_LO) / NVU / 2
        c = GREEN if mid < -9 else (AMBER if mid < 0 else RED)
        save(solid_strip(12, c, mix(c, (8, 9, 12), 0.85), lambda v, mid=mid: VU_LO + (VU_HI - VU_LO) * v >= mid), f"dj_vu{k}.png")
    for k in range(NMVU):
        lo = VU_LO + (VU_HI - VU_LO) * k / NMVU; mid = lo + (VU_HI - VU_LO) / NMVU / 2
        c = GREEN if mid < -9 else (AMBER if mid < 0 else RED)
        save(solid_strip(12, c, mix(c, (8, 9, 12), 0.85), lambda v, mid=mid: VU_LO + (VU_HI - VU_LO) * v >= mid), f"dj_mvu{k}.png")

    for d in range(2): defs.append({"key": f"djPlatter{d}", "value": definition([], [strip_part(f"dj_platter{d}.png", PL, PL, PL)], ignore=True)})
    defs.append({"key": "djPitch", "value": definition(CTRL(), [strip_part("dj_pitch.png", 60, PL, PL), focus((0, 0, 60, PL))])})
    defs.append({"key": "djFader", "value": definition(CTRL(), [strip_part("dj_fader.png", 50, VF, VF), focus((0, 0, 50, VF))])})
    defs.append({"key": "djXfader", "value": definition(CTRL(), [strip_part("dj_xfader.png", XF, 40, XF, "Horizontal"), focus((0, 0, XF, 40))])})
    for key, img in (("djKnob0", "dj_knob0.png"), ("djKnob1", "dj_knob1.png"), ("djKnobM", "dj_knobm.png")):
        defs.append({"key": key, "value": definition(CTRL(), [strip_part(img, KN, KN, KN), focus((0, 0, KN, KN))])})
    for d in range(2):
        for k in range(NPROG): defs.append({"key": f"djProg{d}_{k}", "value": definition([], [strip_part(f"dj_prog{d}_{k}.png", 9, 12, 12)], ignore=True)})
    for k in range(4): defs.append({"key": f"djBeat{k}", "value": definition([], [strip_part(f"dj_beat{k}.png", 26, 8, 14)], ignore=True)})
    for k in range(NVU): defs.append({"key": f"djVu{k}", "value": definition([], [strip_part(f"dj_vu{k}.png", 12, 11, 12)], ignore=True)})
    for k in range(NMVU): defs.append({"key": f"djMvu{k}", "value": definition([], [strip_part(f"dj_mvu{k}.png", 12, 9, 12)], ignore=True)})
    readout("djNow", 17, PRINT, 460, 24); readout("djBpm", 34, CYAN, 130, 42); readout("djPct", 20, PRINT, 110, 30)
    readout("djTime", 22, PRINT, 100, 30); readout("djLib", 14, PRINT_DIM, 330, 22)
    box("djTrack", 330, 34, 15); box("djLoop", 100, 36, 14); box("djRange", 84, 26, 13)
    for d in range(2):
        btn(f"load{d}", "LOAD", 92, 34, DECK_COL[d], 13)
        btn(f"cue{d}", "CUE", 140, 64, AMBER, 20)
        btn(f"play{d}", "\u25B6 \u275A\u275A", 140, 64, GREEN, 22)
        btn(f"sync{d}", "SYNC", 100, 36, DECK_COL[d], 14)
        btn(f"ndn{d}", "\u25C0 NUDGE", 100, 34, (190, 192, 200), 11)
        btn(f"nup{d}", "NUDGE \u25B6", 100, 34, (190, 192, 200), 11)
    btn("rescan", "RESCAN", 110, 26, GOLD, 11)
    for d in range(2):
        btn(f"rec{d}", "\u25CF REC", 84, 40, RED, 13, dark_on=False)
        btn(f"sp{d}", "SP-12", 84, 40, (214, 196, 150), 13)
        btn(f"tr{d}", "TRANSFORM", 110, 40, DECK_COL[d], 12)
    save(solid_strip(14, RED, (60, 20, 18), lambda v: v >= 0.5), "dj_reclight.png")
    defs.append({"key": "djRecLight", "value": definition([], [strip_part("dj_reclight.png", 14, 14, 14)], ignore=True)})
    box("djRate", 84, 40, 14)
    defs.append({"key": "djScratch", "value": definition(CTRL(), [
        comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "dj_drag.png", "numFrames": NUMFRAMES, "invert": False,
             "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, PL, PL))), focus((0, 0, PL, PL))])})
    save(pill("", True, (104, 84, 40), 70, 24), "dj_curve_on.png"); save(pill("", False, GOLD, 70, 24), "dj_curve_off.png")
    defs.append({"key": "djCurve", "value": definition(TOGGLE(), [button_part("dj_curve_on.png", "dj_curve_off.png", 70, 24),
                 label(11, hexcol(PRINT), (0, 0, 70, 24), case="Upper Case"), focus((0, 0, 70, 24))])})

    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "dj_bg.png"}, bounds((0, 0, W, H)))]
    for d in range(2):
        X = DECK_X[d]
        place(comps, f"{'AB'[d]} Track", "djTrack", dp(d, D_TRACK), X + 46, 64, 330, 34, "box", "djTrack", touch=True); BOXES.append((X + 46, 64, 330, 34))
        place(comps, f"{'AB'[d]} Load", f"djBtn_load{d}", dp(d, D_LOAD), X + 384, 64, 92, 34, "btn", f"load{d}", touch=True)
        place(comps, f"{'AB'[d]} Now", "djNow", dp(d, D_STATUS), X + 14, 104, 460, 24, "ro", "djNow")
        place(comps, f"{'AB'[d]} BPM", "djBpm", dp(d, D_BPM), X + 14, 134, 130, 42, "ro", "djBpm")
        place(comps, f"{'AB'[d]} Pitch Pct", "djPct", dp(d, D_PITCH), X + 150, 140, 110, 30, "ro", "djPct")
        place(comps, f"{'AB'[d]} Time", "djTime", dp(d, D_TIME), X + 266, 140, 100, 30, "ro", "djTime")
        place(comps, f"{'AB'[d]} Remain", "djTime", dp(d, D_REMAIN), X + 372, 140, 104, 30, "ro", "djTime")
        for k in range(NPROG):
            place(comps, f"{'AB'[d]} Pos {k}", f"djProg{d}_{k}", dp(d, D_PROGRESS), X + 14 + k * 10.5, 194, 9, 12, "seg", (f"dj_prog{d}_{k}.png", 12))
        place(comps, f"{'AB'[d]} Platter", f"djPlatter{d}", dp(d, D_PLATTER), X + 14, 236, PL, PL, "strip", (f"dj_platter{d}.png", PL))
        for k in range(4):
            place(comps, f"{'AB'[d]} Beat {k}", f"djBeat{k}", dp(d, D_BEAT), X + 16 + k * 37, 220, 26, 8, "seg", (f"dj_beat{k}.png", 14))
        place(comps, f"{'AB'[d]} Sync", f"djBtn_sync{d}", dp(d, D_SYNC), X + 182, 236, 100, 36, "btn", f"sync{d}", touch=True)
        place(comps, f"{'AB'[d]} Loop", "djLoop", dp(d, D_LOOP), X + 290, 236, 100, 36, "box", "djLoop", touch=True); BOXES.append((X + 290, 236, 100, 36))
        place(comps, f"{'AB'[d]} Nudge -", f"djBtn_ndn{d}", dp(d, D_NUDGE_DN), X + 182, 290, 100, 34, "btn", f"ndn{d}", touch=True)
        place(comps, f"{'AB'[d]} Nudge +", f"djBtn_nup{d}", dp(d, D_NUDGE_UP), X + 290, 290, 100, 34, "btn", f"nup{d}", touch=True)
        place(comps, f"{'AB'[d]} Pitch", "djPitch", dp(d, D_PITCH), X + 412, 216, 60, PL, "strip", ("dj_pitch.png", PL), touch=True)
        place(comps, f"{'AB'[d]} Range", "djRange", dp(d, D_RANGE), X + 400, 372, 84, 26, "box", "djRange", touch=True); BOXES.append((X + 400, 372, 84, 26))
        place(comps, f"{'AB'[d]} Cue", f"djBtn_cue{d}", dp(d, D_CUE), X + 14, 420, 140, 64, "btn", f"cue{d}", touch=True)
        place(comps, f"{'AB'[d]} Play", f"djBtn_play{d}", dp(d, D_PLAY), X + 166, 420, 140, 64, "btn", f"play{d}", touch=True)
        place(comps, f"{'AB'[d]} Scratch", "djScratch", dp(d, D_SCRATCH), X + 14, 236, PL, PL, "none", None, touch=True)   # drag the platter
        place(comps, f"{'AB'[d]} Rec", f"djBtn_rec{d}", dp(d, D_REC), X + 14, 500, 84, 40, "btn", f"rec{d}", touch=True)
        place(comps, f"{'AB'[d]} Rec Light", "djRecLight", dp(d, D_RECLIGHT), X + 104, 513, 14, 14, "seg", ("dj_reclight.png", 14))
        place(comps, f"{'AB'[d]} SP-12", f"djBtn_sp{d}", dp(d, D_SP12), X + 126, 500, 84, 40, "btn", f"sp{d}", touch=True)
        place(comps, f"{'AB'[d]} Transform", f"djBtn_tr{d}", dp(d, D_TRANS), X + 218, 500, 110, 40, "btn", f"tr{d}", touch=True)
        place(comps, f"{'AB'[d]} Transform Rate", "djRate", dp(d, D_TRANS_RATE), X + 336, 500, 84, 40, "box", "djRate", touch=True); BOXES.append((X + 336, 500, 84, 40))
    place(comps, "Library", "djLib", M_LIBRARY, DECK_X[0] + 14, 574, 330, 22, "ro", "djLib")
    place(comps, "Rescan", "djBtn_rescan", M_RESCAN, DECK_X[0] + 360, 572, 110, 26, "btn", "rescan", touch=True)
    # mixer
    cxs = [MIX_X + 58, MIX_X + MIX_W - 58]
    place(comps, "MPC In", "djKnobM", M_INPUT, cxs[0] - KN / 2, 70, KN, KN, "strip", ("dj_knobm.png", KN), touch=True)
    place(comps, "Master", "djKnobM", M_MASTER, cxs[1] - KN / 2, 70, KN, KN, "strip", ("dj_knobm.png", KN), touch=True)
    for k in range(NMVU):
        for c, p in enumerate((M_VU_L, M_VU_R)):
            place(comps, f"Master VU {c} {k}", f"djMvu{k}", p, MIX_X + MIX_W / 2 - 16 + c * 20, 124 - (k + 1) * 8.5, 12, 7, "seg", (f"dj_mvu{k}.png", 12))
    for d in range(2):
        for r, w in enumerate((D_GAIN, D_HIGH, D_MID, D_LOW, D_FILTER)):
            place(comps, f"{'AB'[d]} {('Gain', 'High', 'Mid', 'Low', 'Filter')[r]}", f"djKnob{d}", dp(d, w), cxs[d] - KN / 2, 146 + r * 58, KN, KN,
                  "strip", (f"dj_knob{d}.png", KN), touch=True)
        for k in range(NVU):
            place(comps, f"{'AB'[d]} VU {k}", f"djVu{k}", dp(d, D_VU), MIX_X + MIX_W / 2 - 16 + d * 20, 424 - (k + 1) * 17.5 + 4, 12, 11, "seg", (f"dj_vu{k}.png", 12))
        place(comps, f"{'AB'[d]} Volume", "djFader", dp(d, D_FADER), cxs[d] - 25, 438, 50, VF, "strip", ("dj_fader.png", VF), touch=True)
    place(comps, "Crossfader", "djXfader", M_XFADE, MIX_X + (MIX_W - XF) / 2, 572, XF, 40, "strip", ("dj_xfader.png", XF), touch=True)
    place(comps, "Curve", "djCurve", M_CURVE, MIX_X + MIX_W / 2 - 35, 540, 70, 24, "switch", None, touch=True)

    save(background(), "dj_bg.png")
    defs.append({"key": "DJ|Main", "value": definition([], comps, "ff101115")})
    tabs = [{"version": 3, "tabName": name, "fnKeyIndex": i, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "DJ|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0} for i, name in enumerate(("DECKS", "SCRATCH"))]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)
    ql = [dp(0, D_PITCH), dp(0, D_FADER), dp(0, D_FILTER), dp(0, D_LOW), dp(1, D_PITCH), dp(1, D_FADER), dp(1, D_FILTER), dp(1, D_LOW),
          M_XFADE, dp(0, D_MID), dp(0, D_HIGH), dp(0, D_TRACK), dp(1, D_MID), dp(1, D_HIGH), dp(1, D_TRACK), M_MASTER]
    ql2 = [dp(0, D_SCRATCH), dp(1, D_SCRATCH), M_XFADE, dp(0, D_FADER), dp(1, D_FADER), dp(0, D_PITCH), dp(1, D_PITCH), M_MASTER,
           dp(0, D_FILTER), dp(1, D_FILTER), dp(0, D_TRANS), dp(1, D_TRANS), dp(0, D_TRANS_RATE), dp(1, D_TRANS_RATE), dp(0, D_SP12), dp(1, D_SP12)]
    qmap = lambda ids: {f"Q-Link {i + 1}": p for i, p in enumerate(ids)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql)},
                                                       {"Tab": 2, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql2)}]},
         "Program Mode Q-Links": qmap(ql)}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>dadjdecks.vst.dadjdecks</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

def background():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    for y in range(TITLE_H):
        d.line([0, y, W, y], fill=mix((10, 11, 14), (22, 24, 28), y / TITLE_H))
    d.line([0, TITLE_H, W, TITLE_H], fill=GOLD_DARK); d.line([0, TITLE_H + 1, W, TITLE_H + 1], fill=(70, 52, 18))
    gold_text(im, W / 2, TITLE_H / 2 + 2, "   ".join(TITLE.split(" ")), 38)
    d = ImageDraw.Draw(im)
    for dk in range(2):
        X = DECK_X[dk]; col = DECK_COL[dk]
        d.rounded_rectangle([X, 58, X + DECK_W, H - 8], radius=8, fill=PANEL, outline=LINE, width=2)
        d.rectangle([X + 1, 58, X + DECK_W - 1, 61], fill=col)
        text_c(d, X + 26, 81, "AB"[dk], font(26), col)
        d.rounded_rectangle([X + 10, 102, X + DECK_W - 10, 130], radius=4, fill=WELL)
        d.rounded_rectangle([X + 10, 132, X + DECK_W - 10, 188], radius=4, fill=WELL)
        for x, lab in ((X + 79, "BPM"), (X + 205, "PITCH"), (X + 316, "ELAPSED"), (X + 424, "REMAIN")):
            text_c(d, x, 181, lab, font(8), PRINT_DIM)
        d.ellipse([X + 10, 232, X + 18 + PL, 240 + PL], fill=(22, 23, 27))           # platter shadow
        text_c(d, X + 442, 208, "PITCH  +", font(9), PRINT_DIM)
        text_c(d, X + 442, 216 + PL + 1, "-", font(12), PRINT_DIM)
        text_c(d, X + 340, 382, "RANGE", font(9), PRINT_DIM)
        text_c(d, X + 378, 548, "RATE", font(8), PRINT_DIM)
        text_c(d, X + 89, 392, "DRAG TO SCRATCH", font(8), PRINT_DIM)
    d.rounded_rectangle([MIX_X, 58, MIX_X + MIX_W, H - 8], radius=8, fill=(24, 26, 31), outline=LINE, width=2)
    d.rectangle([MIX_X + 1, 58, MIX_X + MIX_W - 1, 61], fill=GOLD_DARK)
    cxs = [MIX_X + 58, MIX_X + MIX_W - 58]
    text_c(d, cxs[0], 124, "MPC IN", font(9), PRINT_DIM); text_c(d, cxs[1], 124, "MASTER", font(9), PRINT_DIM)
    for dk in range(2):
        text_c(d, cxs[dk], 138, "AB"[dk], font(12), DECK_COL[dk])
        for r, lab in enumerate(("GAIN", "HIGH", "MID", "LOW", "FILTER")):
            text_c(d, cxs[dk], 146 + r * 58 + KN + 6, lab, font(8), PRINT_DIM)
        d.rounded_rectangle([cxs[dk] - 30, 436, cxs[dk] + 30, 438 + VF + 2], radius=4, fill=(18, 19, 23))
    d.rounded_rectangle([MIX_X + MIX_W / 2 - 22, 128, MIX_X + MIX_W / 2 + 20, 430], radius=4, fill=WELL)
    d.rounded_rectangle([MIX_X + MIX_W / 2 - 22, 52 + 6, MIX_X + MIX_W / 2 + 20, 126], radius=4, fill=WELL)
    for bx, by, bw, bh in BOXES:
        d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=5, fill=(44, 47, 54), outline=(10, 11, 13), width=2)
    return im

# ---------- preview: plays the test tracks through the real plugin ----------
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
    os.environ["DJ_FOLDER"] = os.path.join(ROOT, "build", "native", "djtest")
    lib = ctypes.CDLL(os.path.join(ROOT, "build", "native", "dadjdecks.so"))
    cb = HOSTCB(lambda *a: 2400); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0)
    n = 512; L = (ctypes.c_float * n)(); R = (ctypes.c_float * n)(); OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)()
    ins = (ctypes.POINTER(ctypes.c_float) * 2)(L, R); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    def run(sec, sleep=False):
        for _ in range(int(sec * 44100 / n)):
            e.processReplacing(fx, ins, outs, n)
            if sleep: time.sleep(0.002)
    run(0.5, True)
    count = 6
    try: count = len([1 for r, ds, fs in os.walk(os.environ["DJ_FOLDER"]) for f in fs if f.lower().endswith(".wav")])
    except Exception: pass
    e.setParameter(fx, dp(0, D_TRACK), 0.5 / count); e.setParameter(fx, dp(0, D_LOAD), 1.0)
    e.setParameter(fx, dp(1, D_TRACK), 1.5 / count); e.setParameter(fx, dp(1, D_LOAD), 1.0)
    run(1.5, True)
    e.setParameter(fx, dp(0, D_PLAY), 1.0); run(6.3)
    e.setParameter(fx, dp(1, D_SYNC), 1.0); run(0.05); e.setParameter(fx, dp(1, D_PLAY), 1.0)
    e.setParameter(fx, dp(0, D_LOW), 0.62); e.setParameter(fx, dp(1, D_HIGH), 0.7); e.setParameter(fx, dp(1, D_FILTER), 0.64)
    e.setParameter(fx, M_XFADE, 0.42); e.setParameter(fx, dp(1, D_LOOP), 0.6)
    e.setParameter(fx, dp(1, D_TRANS), 1.0); e.setParameter(fx, dp(0, D_SP12), 1.0); e.setParameter(fx, dp(0, D_PITCH), 0.5 - 5 / 24)
    run(3.17)
    vals = [e.getParameter(fx, i) for i in range(P_COUNT)]
    txt = []
    for i in range(P_COUNT):
        buf = ctypes.create_string_buffer(128); D(7, idx=i, ptr=ctypes.cast(buf, ctypes.c_void_p)); txt.append(buf.value.decode())
    D(1)
    return vals, txt

def preview(outdir):
    vals, txt = plugin_state()
    os.makedirs(outdir, exist_ok=True)
    cache = {}
    def img(n):
        if n not in cache: cache[n] = Image.open(os.path.join(OUT, n)).convert("RGBA")
        return cache[n]
    im = img("dj_bg.png").copy(); d = ImageDraw.Draw(im)
    def frame(strip, sq, v, w, h):
        fr = max(0, min(FRAMES - 1, round(v * NUMFRAMES))); cx0, cy0 = (sq - w) // 2, (sq - h) // 2
        return strip.crop((cx0, fr * sq + cy0, cx0 + w, fr * sq + cy0 + h))
    for (kind, p, x, y, w, h, extra) in PLACED:
        x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h)); v = vals[p]
        if kind in ("seg", "strip"): im.alpha_composite(frame(img(extra[0]), extra[1], v, w, h), (x, y))
        elif kind == "btn": im.alpha_composite(img(f"dj_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
        elif kind == "switch":
            im.alpha_composite(img(f"dj_curve_{'on' if v >= 0.5 else 'off'}.png"), (x, y)); text_c(d, x + w / 2, y + h / 2, txt[p].upper(), font(9), PRINT)
        elif kind in ("ro", "box"):
            fh, col, _, _, _ = READOUTS[extra]
            s = txt[p].upper() if kind == "box" else txt[p]
            size = int(fh * 0.72); f = font(size)
            while d.textlength(s, font=f) > w - 6 and size > 7: size -= 1; f = font(size)
            text_c(d, x + w / 2, y + h / 2, s, f, col)
    im.convert("RGB").save(os.path.join(outdir, "skin-preview.png"))
    print("preview:", os.path.join(outdir, "skin-preview.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
