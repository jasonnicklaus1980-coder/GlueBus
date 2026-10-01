#!/usr/bin/env python3
"""Generate the SP1200 2.0 MPC screen skin (GlueBus skin pipeline, SP-1200 panel style).

Four pages (MPC screen tabs):
  SP-1200   ten sliders (INPUT DRIVE PITCH SAMPLE RATE BITS SSM ANALOG HISS OUTPUT MIX), output channel, tune mode,
            machine, decay / dynamic filter, bypass, signal-path line
  CIRCUIT   the signal path drawn stage by stage with each stage's settings and on/off switches
  NOISE     the noise sources and component variation
  ANALYZER  input and output spectrum (32 bands each), 4-tap oscilloscope, image/alias meter
Every control is bound to "Parameter N"; indices come from the built plugin (SP_ParamKey), so run `make native` first.
MPC rules (proven on an MPC X): square filmstrip frames, numFrames = last frame index, tall/wide boxes clip a square
frame centred on them. Requires Pillow.
Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, random, shutil, sys, time
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "GlueBus - VST - SP1200")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4     # filmstrip frames; value written to TUI.json (JV-880 convention); supersampling

FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]
def font(size, name="DejaVuSans-Bold.ttf"):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()
def italic_text(im, xy, s, size, fill, slant=0.22):
    """Bold text sheared into an italic (no oblique font file needed)."""
    f = font(size); b = ImageDraw.Draw(im).textbbox((0, 0), s, font=f)
    w, h = b[2] + int(size * slant) + 4, b[3] + 4
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(t).text((0, 0), s, font=f, fill=rgba(fill))
    t = t.transform((w, h), Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), resample=Image.BICUBIC)
    im.paste(t, xy, t)

# ---------- palette (SP-1200: graphite top plate, grey chassis, black slider caps, red LED readouts) ----------
CHASSIS = (104, 106, 108)
PLATE   = (44, 45, 48)
PLATE_HI = (58, 59, 62)
SLOT    = (14, 14, 15)
PRINT   = (226, 226, 220)        # white panel print
PRINT_DIM = (150, 151, 150)
RED     = (214, 40, 36)
LCD_BG, LCD_TXT = (70, 96, 40), (196, 232, 120)
NAME_COL, VALUE_COL, FOCUS_COL = "ffe2e2dc", "ffff4632", "ffff4632"


# ---------- drawing helpers ----------
def rgba(c, a=255): return (c[0], c[1], c[2], a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def spaced(s, n=1): return (" " * n).join(s)

def fader_frame(t):
    """One filmstrip frame: slot + black SP-style cap at position t (0 = bottom, 1 = top)."""
    s = SS; w, h = FW * s, FH * s
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    cx = w / 2
    d.rounded_rectangle([cx - 5 * s, TRAVEL0 * s - 12 * s, cx + 5 * s, TRAVEL1 * s + 12 * s], radius=5 * s, fill=rgba(SLOT))
    d.line([cx, TRAVEL0 * s - 8 * s, cx, TRAVEL1 * s + 8 * s], fill=rgba((40, 40, 42)), width=2 * s)
    cy = (TRAVEL1 + (TRAVEL0 - TRAVEL1) * t) * s
    cw, ch = 30 * s, 22 * s
    sh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([cx - cw + 2 * s, cy - ch + 6 * s, cx + cw + 2 * s, cy + ch + 6 * s], radius=4 * s, fill=(0, 0, 0, 150))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4 * s))); d = ImageDraw.Draw(im)
    cap = Image.new("RGBA", (w, h), (0, 0, 0, 0)); cd = ImageDraw.Draw(cap)
    for yy in range(int(cy - ch), int(cy + ch) + 1):           # ridged black cap, lit from above
        k = (yy - (cy - ch)) / (2 * ch)
        col = mix((74, 74, 78), (16, 16, 18), k)
        if int((yy - (cy - ch)) / (4 * s)) % 2 == 1: col = mix(col, (0, 0, 0), 0.25)
        cd.line([cx - cw, yy, cx + cw, yy], fill=rgba(col))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, fill=255)
    im.paste(cap, (0, 0), mask); d = ImageDraw.Draw(im)
    d.rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, outline=rgba((96, 96, 100)), width=s)
    d.rectangle([cx - cw + 3 * s, cy - 1.5 * s, cx + cw - 3 * s, cy + 1.5 * s], fill=rgba(PRINT))   # white index line
    return im.resize((FW, FH), Image.LANCZOS)

def fader_strip():
    strip = Image.new("RGBA", (FS, FS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(fader_frame(f / (FRAMES - 1)), ((FS - FW) // 2, f * FS + (FS - FH) // 2))
    return strip

def pushbutton(on, w=96, h=40):
    """SP-1200 style square grey key with a red LED."""
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba((20, 20, 22)))
    off = 2 * s if on else 0
    d.rounded_rectangle([3 * s, 3 * s + off, (w - 3) * s, (h - 5) * s + off], radius=3 * s, fill=rgba((150, 152, 152) if on else (176, 178, 176)))
    cx, cy, r = w * s / 2, h * s / 2 + off / 2, 5 * s
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([cx - 3 * r, cy - 3 * r, cx + 3 * r, cy + 3 * r], fill=(255, 60, 40, 170))
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(3 * s))); d = ImageDraw.Draw(im)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgba((255, 58, 40) if on else (96, 34, 30)))
    return im.resize((w, h), Image.LANCZOS)

def seg(label, on, w, h=40):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    fill, txt = ((24, 24, 26), PRINT) if on else ((150, 152, 152), (40, 40, 42))
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba(fill), outline=rgba((20, 20, 22)), width=s)
    if on: d.ellipse([9 * s, h * s / 2 - 4 * s, 17 * s, h * s / 2 + 4 * s], fill=rgba((255, 58, 40)))
    text_c(d, w * s / 2 + (6 * s if on else 0), h * s / 2, label, font(13 * s), rgba(txt))
    return im.resize((w, h), Image.LANCZOS)


# ---------- TUI.json (GlueBus schema) ----------
NOREMAP = {"version": 1, "map": []}
def bounds(b, focus="No", show="Show", visible="Always"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": " ".join(str(int(v)) for v in b), "additionalInvalidatingHandles": []}
def comp(name, typ, data, b, remap=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": remap or NOREMAP, "bounds": b}
def bind(p): return {"version": 1, "map": [{"key": "Data", "value": f"Parameter {P[p]}"}]}
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
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": FOCUS_COL, "backgroundInset": 2.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))


# ---------- parameter indices from the built plugin ----------
LIB = os.path.join(ROOT, "build", "native", "sp1200.so")
def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.SP_ParamKey.restype = ctypes.c_char_p; lib.SP_ParamKey.argtypes = [ctypes.c_int]
    lib.SP_ParamCount.restype = ctypes.c_int
    return {lib.SP_ParamKey(i).decode(): i for i in range(lib.SP_ParamCount())}
P = load_keys()

# ---------- layout ----------
# eight sliders, numbered 1-8 like the hardware's. PITCH and DECAY are what the SP-1200's sliders set in TUNE/DECAY mode.
SLIDERS = [("input", "INPUT", ["-24", "0", "+24"]), ("pitch", "PITCH", ["-12", "0", "+12"]),
           ("decay", "DECAY", ["20ms", "0.3s", "OFF"]), ("drive", "DRIVE", ["0", "+12", "+24"]),
           ("ssm", "SSM", ["-12", "0", "+18"]), ("hiss", "HISS", ["OFF", "HW", "+30"]),
           ("output", "OUTPUT", ["-24", "0", "+12"]), ("mix", "MIX", ["0", "", "100"])]
# slider position (0..1) of each printed mark, mirroring the plugin's mappings
def mark_pos(key, j):
    return {"input": [0, .5, 1], "drive": [0, .5, 1], "pitch": [0, .5, 1], "decay": [0, math.log(0.3 / 0.02) / math.log(4 / 0.02), 1],
            "ssm": [0, 12 / 30, 1], "hiss": [0, 60 / 90, 1], "output": [0, 24 / 36, 1], "mix": [0, .5, 1]}[key][j]
SX0, SPITCH, BOXW, BOXH = 26, 113, 100, 408
SLIDER_Y = 96
RIGHT_X = 948
TABS = ["SP-1200", "CIRCUIT", "NOISE", "ANALYZER"]

def panel_bg(title_right):
    random.seed(12)
    im = Image.new("RGB", (W, H), CHASSIS); px = im.load()
    for y in range(H):
        for x in range(W):
            n = random.randint(-3, 3); px[x, y] = (CHASSIS[0] + n, CHASSIS[1] + n, CHASSIS[2] + n)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 64], fill=(24, 24, 26)); d.rectangle([0, 64, W, 67], fill=RED)
    italic_text(im, (26, 8), "SP-1200", 40, PRINT)
    d.text((252, 14), spaced("SAMPLING  PERCUSSION"), font=font(13), fill=PRINT_DIM)
    d.text((252, 34), spaced("12-BIT  ·  26.04 kHz  ·  SIGNAL-PATH MODEL"), font=font(13), fill=RED)
    tw = d.textlength(spaced(title_right), font=font(16)); d.text((W - 28 - tw, 22), spaced(title_right), font=font(16), fill=PRINT)
    for sx, sy in [(6, 72), (W - 18, 72), (6, H - 16), (W - 18, H - 16)]:
        d.ellipse([sx, sy, sx + 11, sy + 11], fill=(150, 152, 152), outline=(60, 60, 62)); d.line([sx + 2, sy + 5, sx + 9, sy + 5], fill=(60, 60, 62), width=2)
    return im, d

def plate(d, x0, y0, x1, y1, title=None):
    d.rounded_rectangle([x0, y0, x1, y1], radius=6, fill=PLATE, outline=(20, 20, 22), width=2)
    d.rectangle([x0, y0, x1, y0 + 4], fill=PLATE_HI)
    if title: d.text((x0 + 12, y0 + 10), spaced(title), font=font(12), fill=PRINT)

def lcd_box(d, x, y, w, h, caption=None):
    d.rounded_rectangle([x, y, x + w, y + h], radius=4, fill=LCD_BG, outline=(20, 20, 22), width=3)
    if caption: text_c(d, x + w / 2, y - 10, caption, font(10), PRINT)

BG_ITEMS = {t: [] for t in TABS}   # (kind, ...) drawn into each tab's background
PLACED = {t: [] for t in TABS}
DEFS = {}
IMAGES = {}
def img(name, im): IMAGES[name] = im; return name
def defn(key, value):
    DEFS.setdefault(key, value); return key
def place(tab, name, dkey, param, x, y, w, h, kind, extra=None, touch=True):
    PLACED[tab].append(dict(name=name, dkey=dkey, param=param, x=x, y=y, w=w, h=h, kind=kind, extra=extra, touch=touch))

DRAG = "sp_drag.png"
def drag_strip(sq=16): return Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
def d_box(w, h, fs=17):
    key = f"spBox{w}x{h}"
    img(DRAG, drag_strip())
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": DRAG, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
    return defn(key, definition(CTRL(), [drag, label("Value", fs, "ffc4e878", (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))]))
def d_text(w, h, fs, col="ffc4e878"):
    return defn(f"spText{w}x{h}_{fs}", definition([], [label("Value", fs, col, (0, 0, w, h))], ignore=True))
def d_toggle():
    return "spToggle"
def d_seg(group, i, n, txt, w):
    key = f"spSeg_{group}_{i}"
    on, off = img(f"sp_seg_{group}_{i}_on.png", seg(txt, True, w)), img(f"sp_seg_{group}_{i}_off.png", seg(txt, False, w))
    return defn(key, definition([action("Mouse Down", "Q-Link")], [comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i,
                "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, 40)))]))
def d_strip(key, image, w, h, sq):
    return defn(key, definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": image, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((w - sq) / 2, (h - sq) / 2, sq, sq)))], ignore=True))

def box(tab, name, key, x, y, w=150, h=40, caption=None, fs=17):
    place(tab, name, d_box(w, h, fs), P[key], x, y, w, h, "box")
    BG_ITEMS[tab].append(("lcd", x, y, w, h, caption))
def toggle(tab, name, key, x, y, caption):
    place(tab, name, d_toggle(), P[key], x, y, 120, 52, "toggle")
    BG_ITEMS[tab].append(("cap", x + 60, y - 8, caption))
def radio(tab, key, options, x, y, w, cols, caption):
    for i, o in enumerate(options):
        r, c = divmod(i, cols)
        place(tab, f"{caption} {o}", d_seg(key, i, len(options), o, w), P[key], x + c * (w + 6), y + r * 46, w, 40, "seg", (key, i))
    BG_ITEMS[tab].append(("cap", x + (cols * (w + 6) - 6) / 2, y - 10, caption))
def text(tab, name, key, x, y, w, h, fs=15):
    place(tab, name, d_text(w, h, fs), P[key], x, y, w, h, "text", fs, touch=False)

# ---------- filmstrips for the analyzer ----------
LCD_DIM = (60, 84, 34)
def bars_strip(sq, inner_w, bar_w, gap, col):
    """Two bottom-anchored bars per frame (frame = a * 11 + b), drawn in the middle inner_w pixels."""
    s = Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(s)
    x0 = (sq - (2 * bar_w + gap)) // 2
    for f in range(FRAMES):
        a, b = divmod(f, 11)
        if a > 10: continue
        for k, lv in enumerate((a, b)):
            x = x0 + k * (bar_w + gap); y1 = f * sq + sq - 2
            d.rectangle([x, y1 - 1, x + bar_w - 1, y1], fill=rgba(LCD_DIM))
            if lv: d.rectangle([x, y1 - (sq - 6) * lv / 10, x + bar_w - 1, y1], fill=rgba(col))
    return s
def scope_strip(sq, col_w):
    """Two scope points per frame (levels 0..10, 5 = zero) drawn as segments from the centre line, in a col_w-wide column."""
    s = Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(s)
    x0 = (sq - col_w) // 2
    for f in range(FRAMES):
        a, b = divmod(f, 11)
        if a > 10: continue
        cy = f * sq + sq / 2
        d.line([x0, cy, x0 + col_w, cy], fill=rgba(LCD_DIM), width=1)
        for k, lv in enumerate((a, b)):
            x = x0 + k * col_w / 2 + 2; y = cy - (lv - 5) / 5 * (sq / 2 - 3)
            d.rectangle([x, min(cy, y), x + col_w / 2 - 4, max(cy, y)], fill=rgba(LCD_TXT))
            d.rectangle([x, y - 1.5, x + col_w / 2 - 4, y + 1.5], fill=rgba((235, 255, 170)))
    return s

# ================================================================= SP-1200 page
T = "SP-1200"
FW, FH = 76, 330
FS = FH
FY = 28
TRAVEL0, TRAVEL1 = 26, FH - 26
img("sp_fader.png", fader_strip())
defn("spSlider", definition(CTRL(), [focus((0, 0, BOXW, BOXH)), label("Name", 14, NAME_COL, (0, 2, BOXW, 22), case="Upper Case"),
     comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "sp_fader.png", "numFrames": NUMFRAMES, "invert": False,
          "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((BOXW - FS) // 2, FY, FS, FS))),
     label("Value", 12, VALUE_COL, (0, FY + FH + 10, BOXW, 24), case="Upper Case")]))
for i, (key, lab, _) in enumerate(SLIDERS):
    place(T, lab, "spSlider", P[key], SX0 + i * SPITCH, SLIDER_Y, BOXW, BOXH, "slider", key)
radio(T, "channel", ["OUT 1-2", "OUT 3-4", "OUT 5-6", "OUT 7-8"], RIGHT_X + 10, 100, 148, 2, "OUTPUT CHANNEL")
radio(T, "mode", ["45>33", "PITCH", "REPLAY"], RIGHT_X + 10, 210, 98, 3, "TUNE MODE")
radio(T, "machine", ["SP-1200", "SP-12", "S1200 REF"], RIGHT_X + 10, 274, 98, 3, "MACHINE")
box(T, "Pitch Range", "range", RIGHT_X + 10, 348, 98, 36, "PITCH RANGE", 13)
box(T, "Dyn Sweep", "sweep", RIGHT_X + 114, 348, 98, 36, "DYN SWEEP", 15)
box(T, "Dyn Floor", "floor", RIGHT_X + 218, 348, 96, 36, "DYN FLOOR", 15)
toggle(T, "Bypass", "bypass", RIGHT_X + 4, 412, "BYPASS")
box(T, "Quality", "quality", RIGHT_X + 150, 418, 164, 40, "QUALITY (CPU)", 16)
text(T, "Signal Path", "info", RIGHT_X + 10, 500, 304, 26, 13)
text(T, "Alias Meter", "aliastxt", RIGHT_X + 10, 536, 304, 26, 13)
BG_ITEMS[T].append(("lcd", RIGHT_X + 6, 490, 312, 82, None))

# ================================================================= CIRCUIT page
T = "CIRCUIT"
stages = [("ANALOG IN", 24), ("ADC", 270), ("MEMORY + PITCH", 516), ("DAC", 762), ("ANALOG OUT", 1008)]
for name, x in stages: BG_ITEMS[T].append(("stage", x, 84, 236, 420, name))
box(T, "Input Headroom", "headroom", 44, 140, 196, 40, "HEADROOM (RAILS)")
box(T, "Input Knee", "knee", 44, 210, 196, 40, "CLIP KNEE")
box(T, "Anti-Alias Filter", "aaf", 44, 280, 196, 40, "ANTI-ALIAS FILTER")
toggle(T, "Amp Stages", "ampson", 34, 350, "AMP STAGES")
toggle(T, "Filters", "filton", 156, 350, "FILTERS")
box(T, "Sample Rate", "srate", 290, 140, 196, 40, "SAMPLE RATE")
box(T, "Bits", "bits", 290, 210, 196, 40, "RESOLUTION")
box(T, "Quantizer", "qmode", 290, 280, 196, 40, "QUANTIZER (OFFSET TRIM)")
box(T, "Converter Noise", "convn", 290, 350, 196, 40, "CONVERTER NOISE")
box(T, "Tune Mode", "mode", 536, 140, 196, 40, "PLAYBACK")
box(T, "Pitch", "pitch", 536, 210, 196, 40, "PITCH")
box(T, "Pitch Range", "range", 536, 280, 196, 40, "PITCH RANGE")
box(T, "Alias", "alias", 536, 350, 196, 40, "ALIAS")
box(T, "Voice Level", "level", 782, 140, 196, 40, "LEVEL DAC (8-BIT)")
box(T, "S/H Settling", "settle", 782, 210, 196, 40, "S/H SETTLING")
box(T, "Reconstruction", "recon", 782, 280, 196, 40, "RECONSTRUCTION")
box(T, "Output Channel", "channel", 782, 350, 196, 40, "OUTPUT FILTER")
box(T, "SSM Character", "ssm", 1028, 140, 196, 40, "SSM2044 LEVEL")
box(T, "SSM Resonance", "ssmres", 1028, 210, 196, 40, "SSM RESONANCE")
box(T, "SSM Path", "ssmpath", 1028, 280, 196, 40, "SSM PATH")
box(T, "Output Headroom", "outhead", 1028, 350, 196, 40, "OUTPUT HEADROOM")
toggle(T, "SSM Stage", "ssmon", 1018, 402, "SSM STAGE")
toggle(T, "Analog Stages", "analogon", 30, 534, "ANALOG STAGES")
toggle(T, "Digital Stages", "digitalon", 164, 534, "DIGITAL STAGES")
box(T, "Quality", "quality", 320, 540, 180, 40, "QUALITY")
box(T, "Analog", "analog", 520, 540, 180, 40, "ANALOG (NONLINEARITY)")
text(T, "Signal Path", "info", 720, 548, 540, 26, 14)
BG_ITEMS[T].append(("lcd", 714, 538, 552, 46, None))

# ================================================================= NOISE page
T = "NOISE"
BG_ITEMS[T].append(("plate", 18, 80, 760, 610, "NOISE SOURCES (EACH AT ITS STAGE)"))
BG_ITEMS[T].append(("plate", 778, 80, 1262, 610, "HARDWARE UNIT"))
rows = [("hiss", "HISS (OUTPUT STAGE)"), ("anan", "ANALOG NOISE (CIRCUITS)"), ("convn", "CONVERTER NOISE"), ("dign", "DIGITAL NOISE"),
        ("hum", "HUM"), ("mains", "MAINS"), ("ground", "GROUND"), ("ncolor", "NOISE COLOR"), ("nlevel", "NOISE LEVEL (ALL)")]
for k, (key, cap) in enumerate(rows):
    r, c = divmod(k, 3)
    box(T, cap.title(), key, 46 + c * 240, 140 + r * 110, 200, 44, cap)
box(T, "Component Variation", "variation", 830, 140, 380, 44, "COMPONENT VARIATION (0 % = REFERENCE UNIT)")
box(T, "Unit", "unit", 830, 250, 380, 44, "UNIT (TOLERANCE SET)")
BG_ITEMS[T].append(("notes", 820, 330, ["0 %: idealised components, L = R.", "Higher: filter corners, DAC bit weights,",
                    "SSM trim and offset, gains, noise and", "S/H pedestal vary within part tolerances.", "Each UNIT is one repeatable set.",
                    "", "Defaults: noise calibrated to the", "published 90 dB (A) signal-to-noise."]))

# ================================================================= ANALYZER page
T = "ANALYZER"
SQ = 72
img("sp_specin.png", bars_strip(SQ, 66, 31, 4, (120, 200, 255)))
img("sp_specout.png", bars_strip(SQ, 66, 31, 4, LCD_TXT))
img("sp_scope.png", scope_strip(SQ, 34))
d_strip("spSpecIn", "sp_specin.png", SQ, SQ, SQ); d_strip("spSpecOut", "sp_specout.png", SQ, SQ, SQ); d_strip("spScope", "sp_scope.png", 36, SQ, SQ)
X0 = 64
for k in range(16):
    place(T, f"Spectrum In {k + 1}", "spSpecIn", P[f"specin{k}"], X0 + k * SQ, 96, SQ, SQ, "strip", ("sp_specin.png", SQ), touch=False)
    place(T, f"Spectrum Out {k + 1}", "spSpecOut", P[f"specout{k}"], X0 + k * SQ, 196, SQ, SQ, "strip", ("sp_specout.png", SQ), touch=False)
for k in range(32):
    place(T, f"Scope {k + 1}", "spScope", P[f"scope{k}"], X0 + k * 36, 318, 36, SQ, "strip", ("sp_scope.png", SQ), touch=False)
BG_ITEMS[T].append(("analyzer",))
radio(T, "tap", ["INPUT", "POST-ADC", "POST-DAC", "OUTPUT"], 64, 430, 140, 4, "SCOPE TAP")
box(T, "Scope Time", "scopems", 664, 430, 120, 40, "TIME")
toggle(T, "Analyzer", "analyzer", 800, 424, "ANALYZER")
toggle(T, "Freeze", "freeze", 930, 424, "FREEZE")
text(T, "Alias Meter", "aliastxt", 64, 530, 560, 28, 16)
text(T, "Signal Path", "info", 640, 530, 576, 28, 16)
BG_ITEMS[T].append(("lcd", 58, 516, 1162, 56, None))

QL = {
    "SP-1200": ["input", "pitch", "decay", "drive", "ssm", "hiss", "output", "mix", "channel", "mode", "machine", "range", "sweep", "floor", "quality", "srate"],
    "CIRCUIT": ["headroom", "knee", "aaf", "srate", "bits", "qmode", "convn", "pitch", "alias", "level", "settle", "recon", "ssm", "ssmres", "outhead", "analog"],
    "NOISE": ["hiss", "anan", "convn", "dign", "hum", "ground", "ncolor", "nlevel", "variation", "unit", "mains", "drive", "input", "output", "mix", "pitch"],
    "ANALYZER": ["tap", "scopems", "input", "drive", "pitch", "srate", "bits", "ssm", "analog", "hiss", "output", "mix", "channel", "mode", "alias", "recon"],
}

def background(tab):
    im, d = panel_bg({"SP-1200": "SLIDERS", "CIRCUIT": "CIRCUIT", "NOISE": "NOISE", "ANALYZER": "ANALYZER"}[tab])
    if tab == "SP-1200":
        plate(d, 14, 80, 930, 610)
        f_mark, f_num = font(10), font(20)
        for i, (key, lab, marks) in enumerate(SLIDERS):
            x = SX0 + i * SPITCH; cx = x + BOXW / 2
            y0, y1 = SLIDER_Y + FY + TRAVEL0, SLIDER_Y + FY + TRAVEL1
            for j in range(11):
                y = y1 + (y0 - y1) * j / 10; long_ = j in (0, 10)
                d.line([cx - 30 - (6 if long_ else 3), y, cx - 30, y], fill=PRINT if long_ else PRINT_DIM, width=1)
            for j, m in enumerate(marks):
                if not m: continue
                y = y1 + (y0 - y1) * mark_pos(key, j)
                tw = d.textlength(m, font=f_mark)
                d.text((cx - 39 - tw, y - 6), m, font=f_mark, fill=PRINT)
            text_c(d, cx, 592, str(i + 1), f_num, PRINT)
        d.line([24, 572, 920, 572], fill=(20, 20, 22), width=2)
        plate(d, RIGHT_X - 6, 80, 1266, 610)
    for item in BG_ITEMS[tab]:
        k = item[0]
        if k == "stage":
            _, x, y, w, h, name = item
            plate(d, x, y, x + w, y + h)
            text_c(d, x + w / 2, y + 22, name, font(15), PRINT)
        elif k == "plate":
            _, x0, y0, x1, y1, title = item; plate(d, x0, y0, x1, y1, title)
    if tab == "CIRCUIT":
        for i in range(len(stages) - 1):                              # signal flow arrows between the stages
            x = stages[i][1] + 236; d.polygon([(x + 2, 280), (x + 30, 294), (x + 2, 308)], fill=RED)
        lf = font(11)
        notes = {"ANALOG IN": ["NE5534-class buffer, TL084", "anti-alias LPF (15 kHz, leaky)"],
                 "ADC": ["SAR search on the 12-bit", "DAC ladder (same errors)"],
                 "MEMORY + PITCH": ["12-bit words; drop-sample", "reads at the 26.04 kHz clock"],
                 "DAC": ["12-bit x 8-bit level DAC,", "S/H, zero-order hold"],
                 "ANALOG OUT": ["SSM2044 on Out 1-2, op-amp", "LPF on 3-6, output stage"]}
        for name, x in stages:
            for j, ln in enumerate(notes[name]): text_c(d, x + 118, 112 + j * 0 + 0, "", lf, PRINT_DIM)
            for j, ln in enumerate(notes[name]): text_c(d, x + 118, 470 + j * 15, ln, lf, PRINT_DIM)
    if tab == "ANALYZER":
        plate(d, 18, 80, 1262, 498)
        for y0, lab in ((96, "IN"), (196, "OUT")):
            d.rectangle([X0 - 2, y0 - 2, X0 + 16 * SQ + 2, y0 + SQ + 2], fill=LCD_BG)
            text_c(d, X0 - 28, y0 + SQ / 2, lab, font(13), PRINT)
        d.rectangle([X0 - 2, 316, X0 + 32 * 36 + 2, 318 + SQ + 2], fill=LCD_BG)
        text_c(d, X0 - 28, 318 + SQ / 2, "SCOPE", font(10), PRINT)
        for k, f in enumerate(["40", "100", "250", "500", "1k", "2.5k", "5k", "10k", "20k"]):
            fr = {"40": 40, "100": 100, "250": 250, "500": 500, "1k": 1000, "2.5k": 2500, "5k": 5000, "10k": 10000, "20k": 20000}[f]
            x = X0 + 16 * SQ * math.log(fr / 40) / math.log(500)
            d.text((x - 6, 174), f, font=font(9), fill=PRINT_DIM); d.text((x - 6, 274), f, font=font(9), fill=PRINT_DIM)
        d.text((X0, 400), "Bars: 0 to -84 dB.  Scope: one tap, triggered on a rising zero crossing.", font=font(11), fill=PRINT_DIM)
    for item in BG_ITEMS[tab]:
        k = item[0]
        if k == "lcd":
            _, x, y, w, h, cap = item; lcd_box(d, x, y, w, h, cap)
        elif k == "cap":
            _, x, y, cap = item; text_c(d, x, y, cap, font(11), PRINT)
        elif k == "notes":
            _, x, y, lines = item
            for j, ln in enumerate(lines): d.text((x, y + j * 22), ln, font=font(13), fill=PRINT)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    pushbutton(True).save(os.path.join(OUT, "sp_btn_on.png")); pushbutton(False).save(os.path.join(OUT, "sp_btn_off.png"))
    defn("spToggle", definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
        focus((0, 0, 120, 52)),
        comp("Button", "Button", {"version": 2, "onImage": "sp_btn_on.png", "offImage": "sp_btn_off.png", "buttonId": 1,
             "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((12, 6, 96, 40)))]))
    defs = []
    for tab in TABS:
        bgname = f"sp_bg_{tab.lower().replace('-', '')}.png"
        IMAGES[bgname] = background(tab)
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, bounds((0, 0, W, H)))]
        for pl in sorted(PLACED[tab], key=lambda q: 1 if q["touch"] else 0):
            comps.append(comp(pl["name"], pl["dkey"], {"version": 1, "handleName": "Data"},
                              bounds((pl["x"], pl["y"], pl["w"], pl["h"]), focus="Yes" if pl["touch"] else "No", show="Hide" if pl["touch"] else "Show"),
                              {"version": 1, "map": [{"key": "Data", "value": f"Parameter {pl['param']}"}]}))
        DEFS[f"SP|{tab}"] = definition([], comps, "ff6a6c6e")
    for k, v in DEFS.items(): defs.append({"key": k, "value": v})
    tabs = [{"version": 3, "tabName": t, "fnKeyIndex": i, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": f"SP|{t}", "initialSize": f"0 0 {W} {H}", "scale": 1.0} for i, t in enumerate(TABS)]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=2)
    qmap = lambda keys: {f"Q-Link {i + 1}": P[k] for i, k in enumerate(keys)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": i + 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(QL[t])} for i, t in enumerate(TABS)]},
         "Program Mode Q-Links": qmap(QL["SP-1200"])}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=2)
    total = 0
    for name, im in IMAGES.items():
        im.save(os.path.join(OUT, name), optimize=True); total += im.size[0] * im.size[1] * 4
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>gluebus.vst.sp1200</identifier>\n"
        "\t<version>2.0.0.0</version>\n</plugincontent>\n")
    print(f"skin written to {SKIN_DIR}: {len(IMAGES) + 2} images, {total / 1e6:.1f} MB decoded")

# ---------- preview: the real plugin processing a drum loop ----------
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
    lib = ctypes.CDLL(LIB)
    cb = HOSTCB(lambda *a: 0); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0); D(12, val=1)
    D(2, val=6)                                                    # preset "SP-1200 Vinyl" (45>33, Out 3-4)
    n = 512; IL = (ctypes.c_float * n)(); IR = (ctypes.c_float * n)(); OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)()
    ins = (ctypes.POINTER(ctypes.c_float) * 2)(IL, IR); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    t = 0
    for blk in range(int(2.3 * 44100 / n)):
        for i in range(n):
            tt = (t % 22050) / 44100.0
            v = 0.7 * math.sin(2 * math.pi * (55 + 90 * math.exp(-tt * 25)) * tt) * math.exp(-tt * 9) + 0.15 * math.sin(2 * math.pi * 1250 * t / 44100) + 0.05 * math.sin(2 * math.pi * 9000 * t / 44100)
            IL[i] = IR[i] = v; t += 1
        e.processReplacing(fx, ins, outs, n)
    vals = [e.getParameter(fx, i) for i in range(e.numParams)]
    txt = []
    for i in range(e.numParams):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); txt.append(b.value.decode("utf-8", "replace"))
    names = []
    for i in range(e.numParams):
        b = ctypes.create_string_buffer(64); D(8, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); names.append(b.value.decode())
    D(1)
    return vals, txt, names

def preview(outdir):
    vals, txt, names = plugin_state()
    os.makedirs(outdir, exist_ok=True)
    cache = {}
    def im_(n):
        if n not in cache: cache[n] = Image.open(os.path.join(OUT, n)).convert("RGBA")
        return cache[n]
    sheets = []
    for tab in TABS:
        im = im_(f"sp_bg_{tab.lower().replace('-', '')}.png").copy(); d = ImageDraw.Draw(im)
        for pl in PLACED[tab]:
            x, y, w, h = int(pl["x"]), int(pl["y"]), int(pl["w"]), int(pl["h"]); p = pl["param"]; v = vals[p]
            k = pl["kind"]
            if k == "slider":
                strip = im_("sp_fader.png"); fr = round(v * NUMFRAMES); fx0 = (FS - FW) // 2
                im.alpha_composite(strip.crop((fx0, fr * FS, fx0 + FW, fr * FS + FH)), (int(x + (BOXW - FW) / 2), y + FY))
                text_c(d, x + w / 2, y + 13, names[p].upper()[:12], font(12), (226, 226, 220))
                text_c(d, x + w / 2, y + FY + FH + 22, txt[p].upper(), font(12), (255, 70, 50))
            elif k == "strip":
                strip, sq = pl["extra"]; fr = max(0, min(FRAMES - 1, round(v * NUMFRAMES)))
                cx0 = (sq - w) // 2
                im.alpha_composite(im_(strip).crop((cx0, fr * sq, cx0 + w, fr * sq + h)), (x, y))
            elif k == "seg":
                key, i = pl["extra"]; on = round(v * (len([q for q in PLACED[tab] if q["kind"] == "seg" and q["extra"][0] == key]) - 1)) == i
                im.alpha_composite(im_(f"sp_seg_{key}_{i}_{'on' if on else 'off'}.png"), (x, y))
            elif k == "toggle":
                im.alpha_composite(im_(f"sp_btn_{'on' if v >= 0.5 else 'off'}.png"), (x + 12, y + 6))
            elif k in ("box", "text"):
                s = txt[p].upper() if k == "box" else txt[p]
                size = 15 if k == "box" else int(pl["extra"] * 0.8); f = font(size)
                while d.textlength(s, font=f) > w - 8 and size > 7: size -= 1; f = font(size)
                text_c(d, x + w / 2, y + h / 2, s, f, LCD_TXT)
        path = os.path.join(outdir, f"skin-preview-{tab.lower().replace('-', '')}.png")
        im.convert("RGB").save(path); sheets.append(im.convert("RGB")); print("preview:", path)
    sheet = Image.new("RGB", (W * 2 + 10, H * 2 + 10), (0, 0, 0))
    for i, s in enumerate(sheets): sheet.paste(s, ((i % 2) * (W + 10), (i // 2) * (H + 10)))
    sheet.resize((sheet.size[0] // 2, sheet.size[1] // 2), Image.LANCZOS).save(os.path.join(outdir, "skin-preview-all.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
