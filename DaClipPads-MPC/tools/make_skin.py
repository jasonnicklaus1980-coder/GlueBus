#!/usr/bin/env python3
"""Generate the Da Clip Pads MPC screen skin (GlueBus / RadioReady skin pipeline) and preview images.

Six pages (MPC screen tabs), made for the touchscreen: big pads, big buttons, drag boxes, no tiny controls.
  CLIPS     4x4 clip grid (clip 1 bottom-left like pad A01) with name, waveform thumbnail, state + progress ring,
            BPM / length and mode / pitch / volume lines; scenes, pad mode, quantise, tempo, STOP ALL
  CHOP      waveform with slice markers, CHOPS / MODE / CHOP, slice edit, 16 slice pads, PAD ASSIGN, FLIP + locks
  EDIT      file browser + LOAD, big waveform (drag the top half for START, the bottom half for END), points,
            fades, pitch, stretch, trigger / quantise / follow, scene membership, REVERSE / NORMALIZE / TRIM ...
  MIX       16 channel strips: name, pan, level ladder (drag), mute, state
  FX        selected clip FX, SAMPLER stage, master FX
  SETTINGS  tempo / sync / MIDI / quality, factory presets, kits
Everything is bound to plugin parameters; the plugin updates the readouts. Parameter indices come from the built
plugin (DCP_ParamKey), so run `make native` first.

MPC rules (proven on an MPC X): filmstrips are square frames (image width = frame height); a control whose box is
not square gets a square frame centred on it (MPC clips it); numFrames = last frame index; LED parts use one solid
colour per frame; skin folder "<manufacturer> - VST - <plugin>".
Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, shutil, sys, time
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_NAME = "RadioReady Audio - VST - Da Clip Pads"
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", SKIN_NAME)
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
VERSION = "1.0.0.0"
TITLE = "Da Clip Pads"
TITLE_H = 48
SCRIPT_FONTS = [os.path.join(ROOT, "tools", "fonts", n) for n in ("GreatVibes-Regular.woff", "GreatVibes-Regular.ttf")]
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]
TABS = ["CLIPS", "CHOP", "EDIT", "MIX", "FX", "SETTINGS"]

def font(size, name="DejaVuSans-Bold.ttf"):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()
def rgba(c, a=255): return (c[0], c[1], c[2], a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def text_l(d, x, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((x - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def hexcol(c): return "ff%02x%02x%02x" % c

# ---------- palette (MPC-like: near-black, high contrast, one accent per job) ----------
BG, PANEL, PANEL2, WELL = (14, 15, 18), (28, 30, 36), (36, 39, 46), (7, 8, 10)
PRINT, PRINT_DIM = (228, 230, 236), (128, 134, 146)
GOLD, GOLD_DARK = (232, 186, 78), (150, 108, 30)
RED, GREEN, CYAN, AMBER, ORANGE, BLUE, MAGENTA, PURPLE = (236, 64, 56), (70, 206, 104), (64, 208, 232), (240, 190, 60), (245, 130, 50), (80, 150, 255), (236, 90, 200), (150, 110, 240)
WAVE = (72, 206, 214)
LINE = (5, 6, 8)
STATE_COL = [(70, 74, 84), (150, 156, 168), AMBER, GREEN, CYAN, ORANGE, RED]

# ---------- parameter indices from the plugin ----------
LIB = os.path.join(ROOT, "build", "native", "daclippads.so")
def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.DCP_ParamKey.restype = ctypes.c_char_p; lib.DCP_ParamKey.argtypes = [ctypes.c_int]
    lib.DCP_ParamCount.restype = ctypes.c_int
    return {lib.DCP_ParamKey(i).decode(): i for i in range(lib.DCP_ParamCount())}
P = load_keys()
def p(key): return P[key]

# ---------- title ----------
GOLD_STOPS = [(255, 238, 160), (236, 190, 70), (168, 116, 22), (240, 204, 96), (196, 146, 40)]
def script_font(size):
    for f in SCRIPT_FONTS:
        if os.path.exists(f): return ImageFont.truetype(f, size), True
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
def new_strip(sq): return Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0))
def solid_strip(sq, colour_of):
    """Square sq frames, each one solid colour: colour_of(v) -> rgb or None (transparent), v = frame / 127."""
    s = new_strip(sq); d = ImageDraw.Draw(s)
    for f in range(FRAMES):
        c = colour_of(f / NUMFRAMES)
        if c is not None: d.rectangle([0, f * sq, sq - 1, f * sq + sq - 1], fill=rgba(c))
    return s

def cell_state_strip(sq):
    """Clip state + progress ring: frame = state * 18 + progress (0..17)."""
    s = new_strip(sq)
    for f in range(FRAMES):
        st, pr = divmod(f, 18)
        if st > 6: continue
        k = SS; S = sq * k; im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        col = STATE_COL[st]; c = S / 2; r = S * 0.44; wdt = int(S * 0.09)
        d.ellipse([c - r, c - r, c + r, c + r], outline=rgba((40, 43, 50)), width=wdt)
        if st in (3, 4, 5, 6):
            d.arc([c - r, c - r, c + r, c + r], -90, -90 + 360 * (pr + 1) / 18, fill=rgba(col), width=wdt)
        elif st == 2:
            for a in range(0, 360, 30): d.arc([c - r, c - r, c + r, c + r], a, a + 15, fill=rgba(col), width=wdt)
        elif st == 1:
            d.ellipse([c - r, c - r, c + r, c + r], outline=rgba(mix(col, (0, 0, 0), 0.4)), width=wdt)
        i = S * 0.18
        if st == 0: d.line([c - i, c, c + i, c], fill=rgba(col), width=k * 3); d.line([c, c - i, c, c + i], fill=rgba(col), width=k * 3)
        elif st in (1, 5): d.rectangle([c - i * 0.8, c - i * 0.8, c + i * 0.8, c + i * 0.8], fill=rgba(col))
        elif st == 2: text_c(d, c, c, "Q", font(int(S * 0.34)), rgba(col))
        elif st == 3: d.polygon([(c - i * 0.7, c - i), (c - i * 0.7, c + i), (c + i, c)], fill=rgba(col))
        elif st == 4:
            d.arc([c - i, c - i, c + i, c + i], 30, 330, fill=rgba(col), width=k * 3)
            d.polygon([(c + i * 0.55, c - i * 0.95), (c + i * 1.35, c - i * 0.35), (c + i * 0.45, c - i * 0.1)], fill=rgba(col))
        elif st == 6: text_c(d, c, c, "M", font(int(S * 0.34)), rgba(col))
        s.paste(im.resize((sq, sq), Image.LANCZOS), (0, f * sq))
    return s

def bars_strip(sq, bar_w, gap, col, levels=11, dim=None):
    """Two mirrored level bars per frame: frame = a * 11 + b (0..10 each)."""
    s = new_strip(sq); d = ImageDraw.Draw(s)
    x0 = (sq - (2 * bar_w + gap)) // 2
    for f in range(FRAMES):
        a, b = divmod(f, levels)
        if a >= levels: continue
        for h, lv in enumerate((a, b)):
            x = x0 + h * (bar_w + gap); cy = f * sq + sq / 2
            half = max(0.5, (sq / 2 - 1) * lv / (levels - 1))
            d.rectangle([x, cy - 0.5, x + bar_w - 1, cy + 0.5], fill=rgba(dim or mix(col, (0, 0, 0), 0.6)))
            if lv > 0: d.rectangle([x, cy - half, x + bar_w - 1, cy + half], fill=rgba(col))
    return s

def knob_strip(size, col, bipolar=False):
    s = new_strip(size)
    for f in range(FRAMES):
        t = f / NUMFRAMES; k = SS; S = size * k
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        c = S / 2; r = S * 0.44; a0, a1 = 135, 405; a = a0 + (a1 - a0) * t
        d.arc([c - r, c - r, c + r, c + r], a0, a1, fill=rgba((52, 56, 64)), width=int(S * 0.09))
        start = 270 if bipolar else a0
        lo, hi = min(a, start), max(a, start)
        if hi - lo > 1: d.arc([c - r, c - r, c + r, c + r], lo, hi, fill=rgba(col), width=int(S * 0.09))
        rb = S * 0.31
        d.ellipse([c - rb, c - rb, c + rb, c + rb], fill=rgba((50, 53, 61)), outline=rgba((10, 10, 12)), width=k)
        ang = math.radians(a)
        d.line([c + math.cos(ang) * S * 0.07, c + math.sin(ang) * S * 0.07, c + math.cos(ang) * S * 0.28, c + math.sin(ang) * S * 0.28],
               fill=rgba((244, 244, 248)), width=int(S * 0.06))
        s.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return s

def drag_strip(sq=16): return new_strip(sq)

def pill(text, on, col, w, h, size=13, dark_on=True, sub=None):
    k = SS; im = Image.new("RGBA", (w * k, h * k), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * k - 1, h * k - 1], radius=7 * k, fill=rgba(col if on else (46, 49, 57)), outline=rgba((8, 9, 11)), width=2 * k)
    d.line([6 * k, 2 * k, w * k - 6 * k, 2 * k], fill=rgba((255, 255, 255), 40), width=k)
    fg = rgba((14, 16, 18) if on and dark_on else (col if not on else PRINT))
    if sub:
        text_c(d, w * k / 2, h * k * 0.40, text, font(size * k), fg)
        text_c(d, w * k / 2, h * k * 0.74, sub, font(int(size * 0.62) * k), fg)
    elif text: text_c(d, w * k / 2, h * k / 2, text, font(size * k), fg)
    return im.resize((w, h), Image.LANCZOS)

def pad_images(w, h):
    off = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    on = Image.new("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(on)
    d.rounded_rectangle([1, 1, w - 2, h - 2], radius=10, fill=(255, 255, 255, 56), outline=(255, 255, 255, 230), width=3)
    return on, off

# ---------- TUI.json helpers ----------
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

# ---------- build ----------
DEFS = {}            # key -> definition
IMAGES = {}          # file -> PIL image (saved at the end)
READOUT = {}         # def key -> (font h, colour, just)
PLACED = {t: [] for t in TABS}
BG_DRAW = {t: [] for t in TABS}   # background drawing callbacks per tab
QL = {}

def img(name, im): IMAGES[name] = im; return name
def defn(key, value):
    if key not in DEFS: DEFS[key] = value
    return key

def d_readout(key, fh, col, w, h, just="horizontallyCentred verticallyCentred", case="Original"):
    READOUT[key] = (fh, col, just, case)
    return defn(key, definition([], [label(fh, hexcol(col), (0, 0, w, h), just, case)], ignore=True))
def d_box(key, w, h, fs, col=PRINT, orient="Vertical"):
    READOUT[key] = (fs, col, "horizontallyCentred verticallyCentred", "Upper Case")
    img("cp_drag.png", drag_strip())
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "cp_drag.png", "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": orient, "handleName": "Data"}, bounds((0, 0, w, h)))
    return defn(key, definition(CTRL(), [drag, label(fs, hexcol(col), (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))]))
def d_drag(key, w, h, orient):
    img("cp_drag.png", drag_strip())
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "cp_drag.png", "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": orient, "handleName": "Data"}, bounds((0, 0, w, h)))
    return defn(key, definition(CTRL(), [drag, focus((0, 0, w, h))]))
def d_btn(key, txt, w, h, col, fs=13, dark_on=True, sub=None):
    on = img(f"cp_b_{key}_on.png", pill(txt, True, col, w, h, fs, dark_on, sub)); off = img(f"cp_b_{key}_off.png", pill(txt, False, col, w, h, fs, dark_on, sub))
    return defn(f"cpBtn_{key}", definition(TOGGLE(), [button_part(on, off, w, h), focus((0, 0, w, h))]))
def d_strip(key, image, w, h, sq, ctrl=False, orient="Vertical"):
    parts = [strip_part(image, w, h, sq, orient)]
    if ctrl: parts.append(focus((0, 0, w, h)))
    return defn(key, definition(CTRL() if ctrl else [], parts, ignore=not ctrl))

def place(tab, name, dkey, param, x, y, w, h, kind, extra=None, touch=False):
    PLACED[tab].append(dict(name=name, dkey=dkey, param=param, x=x, y=y, w=w, h=h, kind=kind, extra=extra, touch=touch))

# ---- shared definitions / images
KN = 52
img("cp_knob.png", knob_strip(KN, CYAN)); img("cp_knobg.png", knob_strip(KN, GOLD)); img("cp_knobb.png", knob_strip(KN, CYAN, True))
img("cp_knobp.png", knob_strip(44, CYAN, True))
d_strip("cpKnob", "cp_knob.png", KN, KN, KN, True); d_strip("cpKnobG", "cp_knobg.png", KN, KN, KN, True); d_strip("cpKnobB", "cp_knobb.png", KN, KN, KN, True)
d_strip("cpPan", "cp_knobp.png", 44, 44, 44, True)
CELL_SQ = 50
img("cp_state.png", cell_state_strip(CELL_SQ)); d_strip("cpState", "cp_state.png", CELL_SQ, CELL_SQ, CELL_SQ)
TH = 36
img("cp_thumb.png", bars_strip(TH, 15, 3, WAVE)); d_strip("cpThumb", "cp_thumb.png", TH, TH, TH)
WV = 72
img("cp_wave.png", bars_strip(WV, 33, 3, WAVE)); d_strip("cpWave", "cp_wave.png", WV, WV, WV)
MARK_COL = [(20, 22, 26), (40, 92, 98), GREEN, (250, 250, 250), RED, AMBER, ORANGE, MAGENTA, PURPLE, GOLD]
img("cp_mark.png", solid_strip(36, lambda v: MARK_COL[max(0, min(9, round(v * 9)))])); d_strip("cpMark", "cp_mark.png", 36, 12, 36)
SCENE_LED = [(44, 46, 52), (60, 96, 170), AMBER, GREEN]
img("cp_sled.png", solid_strip(18, lambda v: SCENE_LED[max(0, min(3, round(v * 3)))])); d_strip("cpSceneLed", "cp_sled.png", 18, 18, 18)
for k in range(4):
    img(f"cp_beat{k}.png", solid_strip(16, lambda v, k=k: (GOLD if k == 0 else PRINT) if int(v * 4 - 1e-9) == k and v > 0 else (40, 42, 48)))
    d_strip(f"cpBeat{k}", f"cp_beat{k}.png", 16, 16, 16)
NMET = 20
for k in range(NMET):
    db = -48 + 54 * (k + 0.5) / NMET; c = GREEN if db < -12 else (AMBER if db < -3 else RED)
    img(f"cp_met{k}.png", solid_strip(14, lambda v, db=db, c=c: c if -48 + 54 * v >= db else mix(c, (8, 9, 12), 0.85)))
    d_strip(f"cpMet{k}", f"cp_met{k}.png", 14, 8, 14)
NLAD = 16
for k in range(NLAD):
    thr = (k + 0.5) / NLAD; db = -60 + 66 * thr; c = GREEN if db < -6 else (AMBER if db < 2 else RED)
    img(f"cp_lad{k}.png", solid_strip(30, lambda v, thr=thr, c=c: c if v >= thr else mix(c, (8, 9, 12), 0.86)))
    d_strip(f"cpLad{k}", f"cp_lad{k}.png", 30, 10, 30)
CW, CH = 192, 128
on, off = pad_images(CW, CH); img("cp_pad_on.png", on); img("cp_pad_off.png", off)
defn("cpPad", definition(TOGGLE(), [button_part("cp_pad_on.png", "cp_pad_off.png", CW, CH)]))
SPW, SPH = 146, 66
on, off = pad_images(SPW, SPH); img("cp_spad_on.png", on); img("cp_spad_off.png", off)
defn("cpSPad", definition(TOGGLE(), [button_part("cp_spad_on.png", "cp_spad_off.png", SPW, SPH)]))

# readouts
d_readout("cpName", 16, PRINT, CW - 70, 22, "left verticallyCentred")
d_readout("cpInfo", 15, CYAN, CW - 16, 20, "left verticallyCentred", "Upper Case")
d_readout("cpInfoB", 12, PRINT_DIM, CW - 16, 18, "left verticallyCentred")
d_readout("cpTransport", 15, PRINT, 330, 24, "right verticallyCentred")
d_readout("cpStatus", 15, GOLD, W - 24, 24, "left verticallyCentred")
d_readout("cpSelName", 14, PRINT, 700, 22, "left verticallyCentred")
d_readout("cpLibInfo", 13, PRINT_DIM, 520, 22, "right verticallyCentred")
d_readout("cpSliceInfo", 14, CYAN, 520, 22, "right verticallyCentred")
d_readout("cpMixName", 11, PRINT, 74, 18)
d_readout("cpMixVal", 12, PRINT_DIM, 74, 18)
d_readout("cpKnobVal", 12, CYAN, 96, 16)

def header(tab):
    """Title bar with transport + beat LEDs, status line at the bottom: on every page."""
    place(tab, "Transport", "cpTransport", p("transport"), W - 450, 12, 330, 24, "ro")
    for k in range(4): place(tab, f"Beat {k + 1}", f"cpBeat{k}", p("beatled"), W - 108 + k * 24, 16, 16, 16, "seg", ("cp_beat%d.png" % k, 16))
    place(tab, "Status", "cpStatus", p("status"), 14, H - 28, W - 24, 24, "ro")

def box(tab, name, key, param, x, y, w, h, fs=15, caption=None, col=PRINT, orient="Vertical"):
    w, h = int(round(w)), int(round(h))
    dk = d_box(f"cpBox{w}x{h}_{fs}_{col[0]}", w, h, fs, col, orient)
    place(tab, name, dk, param, x, y, w, h, "box", dk, touch=True)
    BG_DRAW[tab].append(("box", x, y, w, h, caption))
def button(tab, name, key, txt, param, x, y, w, h, col, fs=13, dark_on=True, sub=None):
    w, h = int(round(w)), int(round(h))
    dk = d_btn(key, txt, w, h, col, fs, dark_on, sub)
    place(tab, name, dk, param, x, y, w, h, "btn", key, touch=True)
def knob(tab, name, param, x, y, caption, kind="cpKnob", valw=96):
    place(tab, name, kind, param, x, y, KN, KN, "strip", ({"cpKnob": "cp_knob.png", "cpKnobG": "cp_knobg.png", "cpKnobB": "cp_knobb.png"}[kind], KN), touch=True)
    place(tab, name + " Value", "cpKnobVal", param, x + KN / 2 - valw / 2, y + KN + 2, valw, 16, "ro")
    BG_DRAW[tab].append(("cap", x + KN / 2, y - 9, caption))
def cap(tab, x, y, s, col=PRINT_DIM, size=10): BG_DRAW[tab].append(("capc", x, y, s, col, size))
def panel(tab, x, y, w, h, title=None, col=GOLD_DARK): BG_DRAW[tab].append(("panel", x, y, w, h, title, col))

def waveform(tab, top_key, bottom_key, top_caption, bottom_caption):
    x0, y0 = 64, 128
    panel(tab, x0 - 8, y0 - 8, 16 * WV + 16, WV + 34)
    for b in range(16): place(tab, f"Wave {b + 1}", "cpWave", p(f"wave{b}"), x0 + b * WV, y0, WV, WV, "strip", ("cp_wave.png", WV))
    for j in range(32): place(tab, f"Marker {j + 1}", "cpMark", p(f"mark{j}"), x0 + j * 36, y0 + WV + 4, 36, 12, "seg", ("cp_mark.png", 36))
    dt = d_drag("cpWaveDragTop", 16 * WV, WV // 2, "Horizontal"); db = d_drag("cpWaveDragBot", 16 * WV, WV // 2, "Horizontal")
    place(tab, top_caption, dt, p(top_key), x0, y0, 16 * WV, WV // 2, "none", touch=True)
    place(tab, bottom_caption, db, p(bottom_key), x0, y0 + WV // 2, 16 * WV, WV // 2, "none", touch=True)
    BG_DRAW[tab].append(("wavecaps", x0, y0, top_caption, bottom_caption))

# ================================================================= CLIPS
T = "CLIPS"; header(T)
GX, GY = 10, 58
def cell_xy(c): r, col = divmod(c, 4); return GX + col * (CW + 8), GY + (3 - r) * (CH + 6)
for c in range(16):
    x, y = cell_xy(c)
    BG_DRAW[T].append(("cell", x, y, c))
    place(T, f"Clip {c + 1} Name", "cpName", p(f"cname{c}"), x + 40, y + 6, CW - 98, 22, "ro")
    place(T, f"Clip {c + 1} State", "cpState", p(f"cstat{c}"), x + CW - CELL_SQ - 6, y + 6, CELL_SQ, CELL_SQ, "strip", ("cp_state.png", CELL_SQ))
    for k in range(4): place(T, f"Clip {c + 1} Wave {k + 1}", "cpThumb", p(f"thumb{c * 4 + k}"), x + 10 + k * TH, y + 32, TH, TH, "strip", ("cp_thumb.png", TH))
    place(T, f"Clip {c + 1} Info", "cpInfo", p(f"cinfo{c}"), x + 8, y + 76, CW - 16, 20, "ro")
    place(T, f"Clip {c + 1} Info 2", "cpInfoB", p(f"cinfob{c}"), x + 8, y + 100, CW - 16, 18, "ro")
    place(T, f"Pad {c + 1}", "cpPad", p(f"pad{c}"), x, y, CW, CH, "pad", touch=True)
RX = 4 * (CW + 8) + 18; RW = W - RX - 10
panel(T, RX - 6, 56, RW + 12, 536)
cap(T, RX + RW / 2, 66, "SCENES")
for s in range(4):
    bx = RX + s * (RW + 8) / 4
    button(T, f"Scene {s + 1}", f"scene{s}", f"SCENE {s + 1}", p(f"scene{s}"), bx, 76, (RW - 24) / 4, 56, BLUE, 14)
    place(T, f"Scene {s + 1} Light", "cpSceneLed", p(f"sceneled{s}"), bx + 6, 80, 18, 18, "seg", ("cp_sled.png", 18))
bw4 = (RW - 24) / 4
button(T, "Store Scene", "store", "STORE", p("scenestore"), RX, 140, bw4, 40, AMBER, 12)
box(T, "Scene Quantize", "sceneq", p("sceneq"), RX + (bw4 + 8), 150, bw4, 30, 12, "SCENE Q")
box(T, "Scene Follow", "scenefollow", p("scenefollow"), RX + 2 * (bw4 + 8), 150, bw4, 30, 12, "FOLLOW")
box(T, "Scene Length", "scenebars", p("scenebars"), RX + 3 * (bw4 + 8), 150, bw4, 30, 12, "EVERY")
cap(T, RX + RW / 2, 196, "PAD MODE")
for k, (key, txt) in enumerate((("pmplay", "PLAY"), ("pmstop", "STOP"), ("pmselect", "SELECT"), ("pmmute", "MUTE"))):
    button(T, f"Pad Mode {txt}", f"pm{txt}", txt, p(key), RX + k * (bw4 + 8), 206, bw4, 44, [GREEN, ORANGE, CYAN, RED][k], 14)
box(T, "Quantize", "quant", p("quant"), RX, 276, bw4, 36, 14, "QUANTIZE")
box(T, "Swing", "swing", p("swing"), RX + (bw4 + 8), 276, bw4, 36, 14, "SWING")
box(T, "Tempo", "tempo", p("tempo"), RX + 2 * (bw4 + 8), 276, bw4, 36, 14, "TEMPO")
button(T, "Sync", "sync", "SYNC", p("sync"), RX + 3 * (bw4 + 8), 272, bw4, 40, CYAN, 12, sub="TO HOST")
button(T, "Retrigger All", "retrig", "RETRIG", p("retrigall"), RX, 326, bw4, 44, PRINT, 12, sub="ALL")
button(T, "Restart", "restart", "RESTART", p("restart"), RX + (bw4 + 8), 326, bw4, 44, PRINT, 12, sub="NEXT BAR")
button(T, "Global Mute", "muteall", "MUTE", p("muteall"), RX + 2 * (bw4 + 8), 326, bw4, 44, RED, 12, sub="ALL")
button(T, "Flip Selected", "flipq", "FLIP", p("flip"), RX + 3 * (bw4 + 8), 326, bw4, 44, GOLD, 14, sub="SELECTED")
for c, key in enumerate(("meterl", "meterr")):
    for k in range(NMET): place(T, f"Meter {'LR'[c]} {k}", f"cpMet{k}", p(key), RX + 20 + k * ((RW - 20) / NMET), 382 + c * 12, 14, 8, "seg", (f"cp_met{k}.png", 14))
    cap(T, RX + 8, 386 + c * 12, "LR"[c], PRINT_DIM, 9)
button(T, "Stop All", "stopall", "STOP ALL", p("stopall"), RX, 414, RW, 78, RED, 26, dark_on=False)
box(T, "Selected Clip", "sel", p("sel"), RX, 516, RW - 110, 36, 14, "SELECTED")
button(T, "Play Selected", "preview", "\u25B6 / \u25A0", p("preview"), RX + RW - 100, 512, 100, 40, GREEN, 15)
place(T, "Selected Info", "cpSelName", p("selname"), RX, 560, RW, 22, "ro")
QL[T] = [p(f"cvol{c}") for c in range(16)]

# ================================================================= EDIT
T = "EDIT"; header(T)
box(T, "Clip", "sel", p("sel"), 12, 66, 250, 40, 15, "CLIP")
box(T, "Sample", "file", p("file"), 272, 66, 470, 40, 14, "SAMPLE (drag to browse /sdcard/Clips)")
button(T, "Load", "load", "LOAD", p("load"), 752, 64, 110, 44, GREEN, 16)
button(T, "Clear", "clear", "CLEAR", p("clear"), 870, 64, 100, 44, RED, 14, dark_on=False)
button(T, "Rescan", "rescan", "RESCAN", p("rescan"), 978, 64, 100, 44, PRINT, 13)
button(T, "Play", "preview2", "\u25B6 / \u25A0", p("preview"), 1086, 64, 90, 44, GREEN, 15)
box(T, "Zoom", "zoom", p("zoom"), 1186, 66, 84, 40, 14, "ZOOM")
place(T, "Selected Info", "cpSelName", p("selname"), 14, 108, 700, 18, "ro")
place(T, "Library", "cpLibInfo", p("libinfo"), W - 540, 108, 520, 18, "ro")
waveform(T, "start", "end", "DRAG: START", "DRAG: END")
row = [("start", "START"), ("end", "END"), ("lstart", "LOOP START"), ("lend", "LOOP END"), ("fadein", "FADE IN"), ("fadeout", "FADE OUT"), ("xfade", "LOOP XFADE"), ("cent", "FINE TUNE")]
bw = (W - 24 - 7 * 8) / 8
for k, (key, capt) in enumerate(row): box(T, capt.title(), key, p(key), 12 + k * (bw + 8), 252, bw, 40, 15, capt)
row = [("mode", "TRIGGER"), ("cquant", "LAUNCH Q"), ("semi", "PITCH"), ("pmode", "PITCH MODE"), ("slen", "STRETCH TO"), ("stype", "STRETCH MODE"), ("follow", "FOLLOW"), ("ftime", "AFTER")]
for k, (key, capt) in enumerate(row): box(T, capt.title(), key, p(key), 12 + k * (bw + 8), 320, bw, 40, 15, capt)
box(T, "Follow Scene", "fscene", p("fscene"), 12, 388, bw, 40, 15, "LAUNCH SCENE")
for s in range(4): button(T, f"In Scene {s + 1}", f"insc{s}", f"SCENE {s + 1}", p(f"inscene{s}"), 12 + (s + 1) * (bw + 8), 384, bw, 44, BLUE, 13)
box(T, "MIDI Note", "note", p("note"), 12 + 5 * (bw + 8), 388, bw, 40, 15, "MIDI NOTE")
button(T, "Learn", "learn", "LEARN", p("learn"), 12 + 6 * (bw + 8), 384, bw, 44, MAGENTA, 13, sub="MIDI NOTE")
button(T, "Sampler Stage", "vint", "SAMPLER", p("vintage"), 12 + 7 * (bw + 8), 384, bw, 44, GOLD, 13, sub="STAGE")
edits = [("reverse", "REVERSE", PURPLE, None), ("normalize", "NORMALIZE", CYAN, None), ("trim", "TRIM", PRINT, "SILENCE"), ("autofade", "FADE", PRINT, "5 / 30 MS"),
         ("autoxf", "CROSSFADE", PRINT, "LOOP 25 MS"), ("seq", "REARRANGED", AMBER, "SLICES")]
for k, (key, txt, col, sub) in enumerate(edits): button(T, txt.title(), f"e_{key}", txt, p(key), 12 + k * (bw + 8), 452, bw, 52, col, 14, sub=sub)
button(T, "Flip", "flipe", "FLIP", p("flip"), 12 + 6 * (bw + 8), 452, bw, 52, GOLD, 18)
button(T, "Undo", "undoe", "UNDO", p("undo"), 12 + 7 * (bw + 8), 452, bw, 52, PRINT, 14, sub="FLIP")
cap(T, W / 2, 528, "Pads: tap to launch.  MPC pads / MIDI: clip 1 = C1 (36) ... clip 16 = D#2 (51), change with MIDI NOTE or LEARN.", PRINT_DIM, 11)
QL[T] = [p(k) for k in ("start", "end", "lstart", "lend", "fadein", "fadeout", "xfade", "zoom", "semi", "cent", "slen", "stype", "mode", "cquant", "follow", "ftime")]

# ================================================================= CHOP
T = "CHOP"; header(T)
box(T, "Clip", "sel", p("sel"), 12, 66, 250, 40, 15, "CLIP")
box(T, "Chops", "chopn", p("chopn"), 272, 66, 140, 40, 15, "CHOPS")
box(T, "Chop Mode", "chopmode", p("chopmode"), 420, 66, 150, 40, 15, "DETECT")
button(T, "Chop", "chop", "CHOP", p("chop"), 580, 64, 120, 44, GOLD, 18)
box(T, "Zoom", "zoom", p("zoom"), 710, 66, 90, 40, 14, "ZOOM")
place(T, "Slice Info", "cpSliceInfo", p("sliceinfo"), W - 470, 76, 450, 22, "ro")
place(T, "Selected Info", "cpSelName", p("selname"), 14, 108, 700, 18, "ro")
waveform(T, "slicepos", "slice", "DRAG: MOVE", "DRAG: PICK")
row = [("slice", "SLICE"), ("slicepos", "SLICE START"), ("slicepitch", "SLICE PITCH"), ("slicebank", "PAD BANK"), ("slicenote", "SLICE NOTE (MIDI)")]
bw = 180
for k, (key, capt) in enumerate(row): box(T, capt.title(), key, p(key), 12 + k * (bw + 8), 252, bw, 40, 15, capt)
button(T, "Slice Reverse", "srev", "REVERSE", p("slicerev"), 12 + 5 * (bw + 8), 248, 130, 44, PURPLE, 14, sub="SLICE")
button(T, "Pad Assign", "assign", "PAD ASSIGN", p("assign"), W - 12 - 170, 248, 170, 44, GREEN, 15, sub="TO EMPTY CLIPS")
for k in range(16):
    r, c = divmod(k, 8)
    x, y = 12 + c * (SPW + 11), 318 + r * (SPH + 8)
    BG_DRAW[T].append(("spad", x, y, k))
    place(T, f"Slice Pad {k + 1}", "cpSPad", p(f"spad{k}"), x, y, SPW, SPH, "spad", touch=True)
panel(T, 8, 470, W - 16, 122, "FLIP")
button(T, "Flip", "flipc", "FLIP", p("flip"), 20, 494, 200, 84, GOLD, 30)
button(T, "Undo", "undoc", "UNDO", p("undo"), 232, 494, 130, 84, PRINT, 18, sub="AGAIN = REDO")
for k, (key, txt) in enumerate((("lockpitch", "PITCH"), ("lockrev", "REVERSE"), ("lockpos", "START/END"), ("lockfilt", "FILTER"), ("lockslices", "SLICES"))):
    button(T, f"Lock {txt}", f"lock{k}", "LOCK", p(key), 380 + k * 132, 494, 124, 84, RED, 14, dark_on=False, sub=txt)
button(T, "Rearranged", "seqc", "REARRANGE", p("seq"), 380 + 5 * 132, 494, 170, 84, AMBER, 15, sub="ON / OFF")
QL[T] = [p(k) for k in ("slicepos", "slice", "chopn", "slicepitch", "start", "end", "semi", "cent", "zoom", "chopmode", "slicebank", "slicenote", "cut", "res", "rsend", "dsend")]

# ================================================================= MIX
T = "MIX"; header(T)
SW = (W - 16) / 16
for c in range(16):
    x = 8 + c * SW
    BG_DRAW[T].append(("strip", x, SW, c))
    place(T, f"Clip {c + 1} Name", "cpMixName", p(f"cname{c}"), x + (SW - 74) / 2, 82, 74, 18, "ro")
    place(T, f"Clip {c + 1} Pan", "cpPan", p(f"cpan{c}"), x + (SW - 44) / 2, 108, 44, 44, "strip", ("cp_knobp.png", 44), touch=True)
    place(T, f"Clip {c + 1} Pan Value", "cpMixVal", p(f"cpan{c}"), x + (SW - 74) / 2, 154, 74, 18, "ro")
    for k in range(NLAD): place(T, f"Clip {c + 1} Level {k}", f"cpLad{k}", p(f"cvol{c}"), x + (SW - 30) / 2, 404 - k * 14, 30, 10, "seg", (f"cp_lad{k}.png", 30))
    dk = d_drag("cpLadDrag", 56, 236, "Vertical")
    place(T, f"Clip {c + 1} Volume", dk, p(f"cvol{c}"), x + (SW - 56) / 2, 180, 56, 236, "none", touch=True)
    place(T, f"Clip {c + 1} Volume Value", "cpMixVal", p(f"cvol{c}"), x + (SW - 74) / 2, 420, 74, 18, "ro")
    button(T, f"Clip {c + 1} Mute", "mute", "M", p(f"cmute{c}"), x + (SW - 56) / 2, 446, 56, 44, RED, 18, dark_on=False)
    place(T, f"Clip {c + 1} State", "cpState", p(f"cstat{c}"), x + (SW - CELL_SQ) / 2, 500, CELL_SQ, CELL_SQ, "strip", ("cp_state.png", CELL_SQ))
QL[T] = [p(f"cpan{c}") for c in range(16)]

# ================================================================= FX
T = "FX"; header(T)
panel(T, 8, 58, 400, 534, "CLIP FX (SELECTED CLIP)")
box(T, "Clip", "sel", p("sel"), 20, 92, 376, 40, 15, None)
box(T, "Filter Type", "filt", p("filt"), 20, 160, 180, 40, 15, "FILTER")
button(T, "Sampler Stage", "vintfx", "SAMPLER", p("vintage"), 212, 156, 184, 44, GOLD, 14, sub="STAGE ON THIS CLIP")
for k, (key, capt) in enumerate((("cut", "CUTOFF"), ("res", "RESONANCE"), ("drive", "DRIVE"))): knob(T, capt.title(), p(key), 40 + k * 128, 238, capt)
for k, (key, capt) in enumerate((("rsend", "REVERB SEND"), ("dsend", "DELAY SEND"))): knob(T, capt.title(), p(key), 40 + k * 128, 346, capt)
panel(T, 416, 58, 420, 534, "SAMPLER")
button(T, "Sampler On", "smpon", "SAMPLER", p("smpon"), 428, 92, 180, 44, GOLD, 15, sub="ON / OFF")
box(T, "Sampler Preset", "smppreset", p("smppreset"), 618, 96, 206, 40, 15, None)
for k, (key, capt) in enumerate((("bits", "BIT DEPTH"), ("rate", "SAMPLE RATE"), ("aa", "ANTI-ALIAS"), ("quantize", "QUANTIZE"),
                                 ("sat", "SATURATION"), ("noise", "NOISE"), ("crackle", "CRACKLE"), ("smpout", "OUTPUT"))):
    r, c = divmod(k, 4); knob(T, capt.title(), p(key), 438 + c * 100, 190 + r * 120, capt, "cpKnobG")
cap(T, 626, 450, "Sample-rate reduction, bit depth, quantisation error,", PRINT_DIM, 10)
cap(T, 626, 466, "zero-order-hold DAC steps, saturation and noise.", PRINT_DIM, 10)
cap(T, 626, 482, "Presets are named after a sound, not a circuit.", PRINT_DIM, 10)
panel(T, 844, 58, 428, 534, "MASTER")
mfx = [("msat", "SATURATION", "cpKnobG"), ("cthresh", "THRESHOLD", "cpKnob"), ("cratio", "RATIO", "cpKnob"), ("cattack", "ATTACK", "cpKnob"), ("crelease", "RELEASE", "cpKnob"),
       ("cmakeup", "MAKEUP", "cpKnob"), ("eqlow", "EQ LOW", "cpKnobB"), ("eqmid", "EQ MID", "cpKnobB"), ("eqmidf", "MID FREQ", "cpKnob"), ("eqhigh", "EQ HIGH", "cpKnobB"),
       ("revsize", "REV SIZE", "cpKnob"), ("revdamp", "REV DAMP", "cpKnob"), ("revret", "REVERB", "cpKnobG"), ("dlyfb", "FEEDBACK", "cpKnob"), ("dlytone", "DLY TONE", "cpKnob"),
       ("dlyret", "DELAY", "cpKnobG"), ("master", "MASTER", "cpKnobG")]
for k, (key, capt, kk) in enumerate(mfx):
    r, c = divmod(k, 5); knob(T, capt.title(), p(key), 858 + c * 82, 100 + r * 112, capt, kk, valw=80)
box(T, "Delay Time", "dlytime", p("dlytime"), 858 + 2 * 82 - 10, 100 + 3 * 112 + 10, 150, 36, 14, "DELAY TIME")
button(T, "Limiter", "limit", "LIMITER", p("limit"), 858 + 4 * 82 - 30, 100 + 3 * 112 + 6, 110, 44, GREEN, 13)
QL[T] = [p(k) for k in ("cut", "res", "drive", "rsend", "dsend", "bits", "rate", "sat", "noise", "msat", "cthresh", "cratio", "eqlow", "eqhigh", "revret", "master")]

# ================================================================= SETTINGS
T = "SETTINGS"; header(T)
panel(T, 8, 58, 620, 534, "PLAY")
rows = [("tempo", "MASTER TEMPO"), ("swing", "SWING"), ("quant", "QUANTIZE"), ("sceneq", "SCENE QUANTIZE"), ("hostfollow", "WHEN MPC PLAYS / STOPS"),
        ("quality", "QUALITY (CPU)"), ("midich", "MIDI CHANNEL"), ("velsens", "VELOCITY SENS"), ("slicenote", "SLICE NOTE (FIRST SLICE)"), ("note", "SELECTED CLIP NOTE")]
for k, (key, capt) in enumerate(rows):
    r, c = divmod(k, 2); box(T, capt.title(), key, p(key), 24 + c * 300, 100 + r * 84, 280, 42, 16, capt)
button(T, "Sync", "sync2", "SYNC TO HOST", p("sync"), 24, 520, 280, 50, CYAN, 16)
button(T, "Learn", "learn2", "LEARN NOTE", p("learn"), 324, 520, 280, 50, MAGENTA, 16, sub="FOR THE SELECTED CLIP")
panel(T, 636, 58, 636, 534, "PRESETS + KITS")
box(T, "Factory Preset", "preset", p("preset"), 652, 100, 470, 44, 15, "FACTORY PRESET (keeps your samples)")
button(T, "Load Preset", "presetload", "LOAD", p("presetload"), 1132, 98, 124, 48, GREEN, 16)
box(T, "Kit", "kit", p("kit"), 652, 196, 470, 44, 15, "KIT (/sdcard/Clips/Kits)")
button(T, "Load Kit", "kitload", "LOAD KIT", p("kitload"), 1132, 194, 124, 48, GREEN, 14)
button(T, "Save Kit", "kitsave", "SAVE KIT", p("kitsave"), 652, 256, 200, 50, GOLD, 16, sub="NEW KIT FROM THIS SETUP")
button(T, "Rescan", "rescan2", "RESCAN", p("rescan"), 866, 256, 160, 50, PRINT, 15, sub="SAMPLES + KITS")
place(T, "Library", "cpLibInfo", p("libinfo"), 652, 318, 604, 22, "ro")
BG_DRAW[T].append(("settingsnotes",))
QL[T] = [p(k) for k in ("tempo", "swing", "quant", "sceneq", "velsens", "quality", "midich", "slicenote", "note", "hostfollow", "preset", "kit", "scenefollow", "scenebars", "master", "revret")]

# ---------- backgrounds ----------
def background(tab):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    for y in range(TITLE_H): d.line([0, y, W, y], fill=mix((9, 10, 12), (22, 24, 28), y / TITLE_H))
    d.line([0, TITLE_H, W, TITLE_H], fill=GOLD_DARK)
    gold_text(im, 190, TITLE_H / 2 + 2, TITLE, 40)
    d = ImageDraw.Draw(im)
    text_c(d, 470, TITLE_H / 2, tab, font(18), PRINT_DIM)
    d.rounded_rectangle([4, H - 32, W - 4, H - 4], radius=6, fill=WELL)
    for item in BG_DRAW[tab]:
        k = item[0]
        if k == "panel":
            _, x, y, w, h, title, col = item
            d.rounded_rectangle([x, y, x + w, y + h], radius=8, fill=PANEL, outline=LINE, width=2)
            d.rectangle([x + 2, y, x + w - 2, y + 3], fill=col)
            if title: text_l(d, x + 12, y + 18, title, font(12), GOLD)
    for item in BG_DRAW[tab]:
        k = item[0]
        if k == "cell":
            _, x, y, c = item
            d.rounded_rectangle([x, y, x + CW, y + CH], radius=10, fill=PANEL2, outline=LINE, width=2)
            d.rounded_rectangle([x + 6, y + 6, x + 36, y + 28], radius=5, fill=WELL)
            text_c(d, x + 21, y + 17, "%02d" % (c + 1), font(13), GOLD)
            d.rounded_rectangle([x + 8, y + 30, x + 12 + 4 * TH, y + 34 + TH], radius=4, fill=WELL)
        elif k == "spad":
            _, x, y, n = item
            d.rounded_rectangle([x, y, x + SPW, y + SPH], radius=9, fill=PANEL2, outline=LINE, width=2)
            text_c(d, x + SPW / 2, y + SPH / 2, str(n + 1), font(22), PRINT)
        elif k == "box":
            _, x, y, w, h, capt = item
            d.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(46, 49, 57), outline=LINE, width=2)
            if capt: text_c(d, x + w / 2, y - 9, capt, font(10), PRINT_DIM)
        elif k == "cap":
            _, x, y, capt = item; text_c(d, x, y, capt, font(10), PRINT_DIM)
        elif k == "capc":
            _, x, y, s, col, size = item; text_c(d, x, y, s, font(size), col)
        elif k == "wavecaps":
            _, x0, y0, top, bot = item
            d.rectangle([x0, y0, x0 + 16 * WV, y0 + WV], fill=WELL)
            d.line([x0, y0 + WV / 2, x0 + 16 * WV, y0 + WV / 2], fill=(34, 40, 44))
            text_c(d, x0 / 2 + 2, y0 + WV * 0.25, "\u25C0\u25B6", font(12), PRINT_DIM); text_c(d, x0 / 2 + 2, y0 + WV * 0.75, "\u25C0\u25B6", font(12), PRINT_DIM)
            text_c(d, (x0 + 16 * WV + W) / 2, y0 + WV * 0.25, top.split(": ")[1], font(10), PRINT_DIM)
            text_c(d, (x0 + 16 * WV + W) / 2, y0 + WV * 0.75, bot.split(": ")[1], font(10), PRINT_DIM)
            # marker legend
            leg = [("START", 3), ("END", 4), ("LOOP", 5), ("SLICE", 8), ("SELECTED", 9), ("PLAYING", 7)]
            lx = x0
            for name, st in leg:
                d.rectangle([lx, y0 + WV + 22, lx + 10, y0 + WV + 30], fill=MARK_COL[st]); text_l(d, lx + 14, y0 + WV + 26, name, font(9), PRINT_DIM); lx += 90
        elif k == "strip":
            _, x, sw, c = item
            d.rounded_rectangle([x + 2, 58, x + sw - 2, 590], radius=7, fill=PANEL if c % 2 == 0 else PANEL2, outline=LINE, width=2)
            text_c(d, x + sw / 2, 70, "%02d" % (c + 1), font(13), GOLD)
            d.rounded_rectangle([x + (sw - 38) / 2, 176, x + (sw + 38) / 2, 418], radius=5, fill=WELL)
            text_c(d, x + sw / 2, 562, "PAN / LEVEL", font(7), PRINT_DIM)
        elif k == "settingsnotes":
            notes = ["MIDI: MPC pads play clips from their notes. Defaults (pad bank A):",
                     "clip 1 = C1 (36), clip 2 = C#1 (37) ... clip 16 = D#2 (51).",
                     "Change one on EDIT > MIDI NOTE, or press LEARN and hit a pad.",
                     "Slices of the selected clip: SLICE NOTE and up (default C3 = 60).",
                     "",
                     "Samples: WAV files in /sdcard/Clips (sub-folders too).",
                     "Kits and the project keep file names, settings, slices,",
                     "scenes and MIDI notes; the audio stays in your WAV files.",
                     "",
                     "Da Clip Pads 1.0.0  -  RadioReady Audio"]
            for i, s in enumerate(notes): text_l(d, 652, 372 + i * 20, s, font(12), PRINT_DIM if s else PRINT_DIM)
    return im

# ---------- TUI.json ----------
def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    defs = []
    for tab in TABS:
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": f"cp_bg_{tab.lower()}.png"}, bounds((0, 0, W, H)))]
        # read-only parts first, touch parts last (on top)
        for pl in sorted(PLACED[tab], key=lambda q: 1 if q["touch"] else 0):
            comps.append(comp(pl["name"], pl["dkey"], {"version": 1, "handleName": "Data"},
                              bounds((pl["x"], pl["y"], pl["w"], pl["h"]), focus="Yes" if pl["touch"] else "No", show="Hide" if pl["touch"] else "Show"),
                              bindp(pl["param"])))
        IMAGES[f"cp_bg_{tab.lower()}.png"] = background(tab)
        DEFS[f"CP|{tab.title()}"] = definition([], comps, "ff0e0f12")
    for k, v in DEFS.items(): defs.append({"key": k, "value": v})
    tabs = [{"version": 3, "tabName": name, "fnKeyIndex": i, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": f"CP|{name.title()}", "initialSize": f"0 0 {W} {H}", "scale": 1.0} for i, name in enumerate(TABS)]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=2)
    qmap = lambda ids: {f"Q-Link {i + 1}": pp for i, pp in enumerate(ids)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": i + 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(QL[t])} for i, t in enumerate(TABS)]},
         "Program Mode Q-Links": qmap(QL["CLIPS"])}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=2)
    total = 0
    for name, im in IMAGES.items():
        im.save(os.path.join(OUT, name), optimize=True); total += im.size[0] * im.size[1] * 4
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>daclippads.vst.daclippads</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print(f"skin written to {SKIN_DIR}: {len(IMAGES)} images, {total / 1e6:.1f} MB decoded, {sum(len(v) for v in PLACED.values())} controls")

# ---------- preview: the real plugin playing the demo kit ----------
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

def plugin_states():
    os.environ["CLIPS_FOLDER"] = os.path.join(ROOT, "test", "demo", "Clips")
    lib = ctypes.CDLL(LIB)
    cb = HOSTCB(lambda *a: 0); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0)
    n = 512; OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)()
    outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    def run(sec, sleep=False):
        for _ in range(int(sec * 44100 / n)):
            e.processReplacing(fx, None, outs, n)
            if sleep: time.sleep(0.002)
    kit = open(os.path.join(os.environ["CLIPS_FOLDER"], "Kits", "Demo Kit.dcpkit"), "rb").read()
    buf = ctypes.create_string_buffer(kit)
    D(24, val=len(kit), ptr=ctypes.cast(buf, ctypes.c_void_p))
    run(1.5, True)
    setn = lambda key, v: e.setParameter(fx, P[key], v)
    setn("scene1", 1.0); run(0.1); setn("quant", 0.0)
    run(4.3)
    setn("pad8", 1.0); run(0.25)
    setn("sel", 0.0); setn("chopn", 1 / 3); setn("chop", 1.0); run(0.1)
    setn("slice", 3 / 31); setn("zoom", 0.0)
    run(0.3)
    vals = [e.getParameter(fx, i) for i in range(e.numParams)]
    txt = []
    for i in range(e.numParams):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); txt.append(b.value.decode("utf-8", "replace"))
    D(1)
    return vals, txt

def preview(outdir):
    vals, txt = plugin_states()
    os.makedirs(outdir, exist_ok=True)
    cache = {}
    def im_(n):
        if n not in cache: cache[n] = Image.open(os.path.join(OUT, n)).convert("RGBA")
        return cache[n]
    def frame(strip, sq, v, w, h):
        fr = max(0, min(FRAMES - 1, round(v * NUMFRAMES))); cx0, cy0 = int((sq - w) // 2), int((sq - h) // 2)
        return strip.crop((cx0, fr * sq + cy0, cx0 + int(w), fr * sq + cy0 + int(h)))
    sheets = []
    for tab in TABS:
        im = im_(f"cp_bg_{tab.lower()}.png").copy(); d = ImageDraw.Draw(im)
        for pl in PLACED[tab]:
            x, y, w, h = int(round(pl["x"])), int(round(pl["y"])), int(round(pl["w"])), int(round(pl["h"])); v = vals[pl["param"]]; kind = pl["kind"]
            if kind in ("seg", "strip"):
                strip, sq = pl["extra"]
                ww, hh = min(w, sq), min(h, sq)
                im.alpha_composite(frame(im_(strip), sq, v, ww, hh), (x + (w - ww) // 2, y + (h - hh) // 2))
            elif kind == "btn": im.alpha_composite(im_(f"cp_b_{pl['extra']}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
            elif kind in ("ro", "box"):
                fh, col, just, case = READOUT[pl["dkey"]]
                s = txt[pl["param"]]; s = s.upper() if case == "Upper Case" else s
                size = int(fh * 0.8); f = font(size)
                while d.textlength(s, font=f) > w - 8 and size > 7: size -= 1; f = font(size)
                if just.startswith("left"): text_l(d, x + 2, y + h / 2, s, f, col)
                elif just.startswith("right"):
                    tw = d.textlength(s, font=f); text_l(d, x + w - tw - 2, y + h / 2, s, f, col)
                else: text_c(d, x + w / 2, y + h / 2, s, f, col)
        path = os.path.join(outdir, f"skin-preview-{tab.lower()}.png")
        im.convert("RGB").save(path); sheets.append(im.convert("RGB"))
        print("preview:", path)
    # contact sheet (2 x 3)
    sheet = Image.new("RGB", (W * 2 + 10, H * 3 + 20), (0, 0, 0))
    for i, s in enumerate(sheets): sheet.paste(s, ((i % 2) * (W + 10), (i // 2) * (H + 10)))
    sheet.resize((sheet.size[0] // 2, sheet.size[1] // 2), Image.LANCZOS).save(os.path.join(outdir, "skin-preview-all.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
