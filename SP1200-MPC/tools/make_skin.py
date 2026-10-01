#!/usr/bin/env python3
"""Generate the SP1200 2.0 MPC screen skin (GlueBus skin pipeline), drawn in the style of the SP-1200's front panel:
blue-grey body, light top strip, section titles over a blue rule, cream square keys with red LEDs, black sliders over
ruled lines, a teal display.

Two pages (MPC screen tabs):
  PERFORM  Output (1-2 / 3-4 / 5-6 / 7-8), Tune Mode, Machine, the display and Bypass; the eight sliders
           (INPUT PITCH DECAY DRIVE SSM HISS OUTPUT MIX), numbered 1-8 like the hardware's
  SETUP    sample rate / bits / pitch range, the Out 1-2 dynamic filter, character (analog, variation, unit), system
Everything else (per-stage circuit settings, individual noise sources, analyzer) stays available as plugin parameters
and in the presets. Every control is bound to "Parameter N"; indices come from the built plugin (SP_ParamKey), so run
`make native` first. MPC rules (proven on an MPC X): square filmstrip frames, numFrames = last frame index.
Requires Pillow.  Usage: python3 tools/make_skin.py [preview-dir]
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

# ---------- palette (SP-1200 front panel) ----------
BODY    = (76, 82, 102)          # blue-grey body
STRIP   = (172, 176, 186)        # light top strip
INK     = (40, 44, 58)           # dark print on the strip
PRINT   = (232, 232, 236)        # light print on the body
PRINT_DIM = (176, 180, 194)
RULE    = (66, 136, 220)         # blue section rules
CREAM, CREAM_DN = (232, 228, 212), (204, 199, 182)
RED     = (226, 48, 44)
SLOT    = (14, 14, 16)
LCD_BG, LCD_TXT = (118, 176, 164), (22, 44, 44)
VALUE_COL, FOCUS_COL, LCD_COL = "ffffffff", "ffe2302c", "ff162c2c"

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

# Slider graphics: only the filmstrip format already proven on the MPC X (Da Clip Pads, Da Lufs Plug): 128 square
# frames, numFrames 127, at most 84 px wide (10 752 px tall), the knob fully inside its parent box. A 250 px wide
# strip (32 000 px) lost its upper frames on the device, and 64-frame half strips were sliced wrongly. So each
# slider is three stacked 84 px pieces of one fader drawing, each its own 128-frame strip bound to the same value.
PIECE, NPIECES = 84, 3
def fader_pieces():
    strips = [Image.new("RGBA", (PIECE, PIECE * FRAMES), (0, 0, 0, 0)) for _ in range(NPIECES)]
    x0 = (PIECE - FW) // 2
    for f in range(FRAMES):
        fr = fader_frame(f / (FRAMES - 1))
        for k in range(NPIECES): strips[k].paste(fr.crop((0, k * PIECE, FW, (k + 1) * PIECE)), (x0, f * PIECE))
    return strips

def key_image(on, w, h, label=None, red=False):
    """A square SP-1200 key: an LED above a cream (or red) key, the label printed under it."""
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    cx = w * s / 2
    kw, kt, kb = 18 * s, 14 * s, 44 * s                          # key half-width, top, bottom
    if not red:
        r = 3.5 * s
        if on:
            g = Image.new("RGBA", im.size, (0, 0, 0, 0))
            ImageDraw.Draw(g).ellipse([cx - 3 * r, 6 * s - 3 * r, cx + 3 * r, 6 * s + 3 * r], fill=(255, 60, 40, 160))
            im.alpha_composite(g.filter(ImageFilter.GaussianBlur(2 * s))); d = ImageDraw.Draw(im)
        d.ellipse([cx - r, 6 * s - r, cx + r, 6 * s + r], fill=rgba((255, 64, 44) if on else (92, 40, 44)))
    d.rounded_rectangle([cx - kw + 2 * s, kt + 3 * s, cx + kw + 2 * s, kb + 3 * s], radius=3 * s, fill=(0, 0, 0, 90))
    off = 2 * s if on else 0
    face = (RED if on else (150, 34, 32)) if red else (CREAM_DN if on else CREAM)
    d.rounded_rectangle([cx - kw, kt + off, cx + kw, kb + off], radius=3 * s, fill=rgba(face), outline=rgba(mix(face, (0, 0, 0), 0.35)), width=s)
    if red and on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle([cx - kw, kt, cx + kw, kb], radius=3 * s, fill=(255, 70, 50, 110))
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(5 * s))); d = ImageDraw.Draw(im)
    if label: text_c(d, cx, (kb + h * s) / 2 + 2 * s, label, font(11 * s, "DejaVuSans.ttf"), rgba(PRINT))
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
FW, FH = 76, 252                 # 3 pieces x 84 px
FS = FH
FY = 24
TRAVEL0, TRAVEL1 = 24, FH - 24
TABS = ["PERFORM", "SETUP"]
SLIDERS = [("input", "INPUT"), ("pitch", "PITCH"), ("decay", "DECAY"), ("drive", "DRIVE"),
           ("ssm", "SSM"), ("hiss", "HISS"), ("output", "OUTPUT"), ("mix", "MIX")]
SPITCH, BOXW = 140, 100
SX0 = (W - 8 * SPITCH) // 2 + (SPITCH - BOXW) // 2
SLIDER_Y, BOXH = 250, FY + FH
KEY_W, KEY_H = 64, 66

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
    key = f"spBox{w}x{h}_{fs}"
    img(DRAG, drag_strip())
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": DRAG, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
    return defn(key, definition(CTRL(), [drag, label("Value", fs, LCD_COL, (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))]))
def d_text(w, h, fs, col=LCD_COL):
    return defn(f"spText{w}x{h}_{fs}", definition([], [label("Value", fs, col, (0, 0, w, h))], ignore=True))
def d_key(group, i, n, txt):
    key = f"spKey_{group}_{i}"
    on, off = img(f"sp_key_{group}_{i}_on.png", key_image(True, KEY_W, KEY_H, txt)), img(f"sp_key_{group}_{i}_off.png", key_image(False, KEY_W, KEY_H, txt))
    return defn(key, definition([action("Mouse Down", "Q-Link")], [comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i,
                "numButtonsInGroup": n, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, KEY_W, KEY_H)))]))

def section(tab, x0, x1, y, title): BG_ITEMS[tab].append(("section", x0, x1, y, title))
def box(tab, name, key, x, y, w=186, h=44, caption=None, fs=17):
    place(tab, name, d_box(w, h, fs), P[key], x, y, w, h, "box")
    BG_ITEMS[tab].append(("lcd", x, y, w, h, caption))
def keys(tab, key, options, x, y, pitch=KEY_W + 6):
    for i, o in enumerate(options):
        place(tab, f"{key} {o}", d_key(key, i, len(options), o), P[key], x + i * pitch, y, KEY_W, KEY_H, "key", (key, i))
def red_key(tab, name, key, x, y):
    place(tab, name, "spRedKey", P[key], x, y, KEY_W, KEY_H, "redkey")
def text(tab, name, key, x, y, w, h, fs=15):
    place(tab, name, d_text(w, h, fs), P[key], x, y, w, h, "text", fs, touch=False)

# ================================================================= PERFORM page
T = "PERFORM"
section(T, 24, 300, 54, "Output")
keys(T, "channel", ["1-2", "3-4", "5-6", "7-8"], 24, 92)
section(T, 330, 538, 54, "Tune Mode")
keys(T, "mode", ["45>33", "Pitch", "Replay"], 330, 92)
section(T, 568, 776, 54, "Machine")
keys(T, "machine", ["SP-1200", "SP-12", "S1200"], 568, 92)
section(T, 806, 1256, 54, "Master Control")
BG_ITEMS[T].append(("lcd", 806, 98, 360, 52, None))
text(T, "Display", "info", 812, 103, 348, 42, 14)
red_key(T, "Bypass", "bypass", 1188, 92)
section(T, 24, 1256, 180, "Performance")
for k, st in enumerate(fader_pieces()): img(f"sp_fader{k}.png", st)
fknob = lambda k: comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": f"sp_fader{k}.png", "numFrames": NUMFRAMES,
                       "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((BOXW - PIECE) // 2, FY + k * PIECE, PIECE, PIECE)))
defn("spSlider", definition(CTRL(), [focus((0, 0, BOXW, BOXH))] + [fknob(k) for k in range(NPIECES)] + [
     label("Value", 14, VALUE_COL, (0, 0, BOXW, 22), case="Upper Case")]))
for i, (key, lab) in enumerate(SLIDERS):
    place(T, lab, "spSlider", P[key], SX0 + i * SPITCH, SLIDER_Y - FY, BOXW, BOXH, "slider", key)

# ================================================================= SETUP page
T = "SETUP"
section(T, 24, 620, 54, "Sample")
box(T, "Sample Rate", "srate", 24, 120, caption="Sample Rate")
box(T, "Bits", "bits", 229, 120, caption="Bits")
box(T, "Pitch Range", "range", 434, 120, caption="Pitch Range")
section(T, 660, 1256, 54, "Dynamic Filter  (Out 1-2)")
box(T, "Dyn Sweep", "sweep", 660, 120, caption="Sweep")
box(T, "Dyn Floor", "floor", 865, 120, caption="Floor")
box(T, "SSM Resonance", "ssmres", 1070, 120, caption="Resonance")
section(T, 24, 620, 230, "Character")
box(T, "Analog", "analog", 24, 296, caption="Analog")
box(T, "Component Variation", "variation", 229, 296, caption="Variation")
box(T, "Unit", "unit", 434, 296, caption="Unit")
section(T, 660, 1256, 230, "System")
box(T, "Quality", "quality", 660, 296, caption="Quality (CPU)")
box(T, "Noise Level", "nlevel", 865, 296, caption="Noise Level")
box(T, "Reconstruction", "recon", 1070, 296, caption="Reconstruction")
section(T, 24, 1256, 406, "Display")
BG_ITEMS[T].append(("lcd", 24, 450, 1232, 64, None))
text(T, "Display", "info", 32, 460, 1216, 44, 18)

QL = {
    "PERFORM": ["input", "pitch", "decay", "drive", "ssm", "hiss", "output", "mix", "channel", "mode", "machine", "bypass"],
    "SETUP": ["srate", "bits", "range", "sweep", "floor", "ssmres", "analog", "variation", "unit", "quality", "nlevel", "recon"],
}

def panel_bg(title_right):
    random.seed(12)
    im = Image.new("RGB", (W, H), BODY); px = im.load()
    for y in range(H):
        for x in range(W):
            n = random.randint(-2, 2); px[x, y] = (BODY[0] + n, BODY[1] + n, BODY[2] + n)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 40], fill=STRIP); d.line([0, 40, W, 40], fill=mix(STRIP, (0, 0, 0), 0.3), width=2)
    d.text((24, 6), "SP1200", font=font(24), fill=INK)
    d.text((150, 15), spaced("12 BIT  SAMPLING  PERCUSSION", 2), font=font(9), fill=INK)
    tw = d.textlength(spaced(title_right), font=font(13)); d.text((W - 24 - tw, 13), spaced(title_right), font=font(13), fill=INK)
    d.rectangle([0, H - 26, W, H], fill=STRIP); d.line([0, H - 26, W, H - 26], fill=mix(STRIP, (0, 0, 0), 0.3), width=2)
    d.text((24, H - 20), "GlueBus", font=font(12, "DejaVuSans.ttf"), fill=INK)
    return im, d

def background(tab):
    im, d = panel_bg(tab)
    if tab == "PERFORM":
        y0, y1 = SLIDER_Y + TRAVEL0, SLIDER_Y + TRAVEL1
        x0, x1 = SX0 - 20, SX0 + 7 * SPITCH + BOXW + 20
        for j in range(11):                                         # ruled lines behind the sliders
            y = y1 + (y0 - y1) * j / 10
            d.line([x0, y, x1, y], fill=mix(BODY, PRINT, 0.5 if j in (0, 5, 10) else 0.28), width=1)
        for i, (key, lab) in enumerate(SLIDERS):
            cx = SX0 + i * SPITCH + BOXW / 2
            text_c(d, cx, SLIDER_Y + FH + 12, lab, font(15, "DejaVuSans.ttf"), PRINT)   # printed on the panel, no pads
            text_c(d, cx, SLIDER_Y + FH + 36, str(i + 1), font(13), PRINT_DIM)
        text_c(d, 1188 + KEY_W / 2, 92 + KEY_H - 8, "Bypass", font(11, "DejaVuSans.ttf"), PRINT)
    for item in BG_ITEMS[tab]:
        k = item[0]
        if k == "section":
            _, x0, x1, y, title = item
            d.text((x0, y), title, font=font(17, "DejaVuSans.ttf"), fill=PRINT)
            d.line([x0, y + 26, x1, y + 26], fill=RULE, width=2)
        elif k == "lcd":
            _, x, y, w, hh, cap = item
            d.rounded_rectangle([x - 4, y - 4, x + w + 4, y + hh + 4], radius=4, fill=(24, 26, 32))
            d.rectangle([x, y, x + w, y + hh], fill=LCD_BG)
            if cap: d.text((x, y - 26), cap, font=font(13, "DejaVuSans.ttf"), fill=PRINT_DIM)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    img("sp_red_on.png", key_image(True, KEY_W, KEY_H, red=True)); img("sp_red_off.png", key_image(False, KEY_W, KEY_H, red=True))
    defn("spRedKey", definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
        focus((0, 0, KEY_W, KEY_H)),
        comp("Button", "Button", {"version": 2, "onImage": "sp_red_on.png", "offImage": "sp_red_off.png", "buttonId": 1,
             "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, KEY_W, KEY_H)))]))
    defs = []
    for tab in TABS:
        bgname = f"sp_bg_{tab.lower()}.png"
        IMAGES[bgname] = background(tab)
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, bounds((0, 0, W, H)))]
        for pl in sorted(PLACED[tab], key=lambda q: 1 if q["touch"] else 0):
            comps.append(comp(pl["name"], pl["dkey"], {"version": 1, "handleName": "Data"},
                              bounds((pl["x"], pl["y"], pl["w"], pl["h"]), focus="Yes" if pl["touch"] else "No", show="Hide" if pl["touch"] else "Show"),
                              {"version": 1, "map": [{"key": "Data", "value": f"Parameter {pl['param']}"}]}))
        DEFS[f"SP|{tab}"] = definition([], comps, "ff4c5266")
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
         "Program Mode Q-Links": qmap(QL["PERFORM"])}
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
        im = im_(f"sp_bg_{tab.lower()}.png").copy(); d = ImageDraw.Draw(im)
        for pl in PLACED[tab]:
            x, y, w, h = int(pl["x"]), int(pl["y"]), int(pl["w"]), int(pl["h"]); p = pl["param"]; v = vals[p]
            k = pl["kind"]
            if k == "slider":
                fr = round(v * NUMFRAMES)
                for k2 in range(NPIECES):
                    im.alpha_composite(im_(f"sp_fader{k2}.png").crop((0, fr * PIECE, PIECE, fr * PIECE + PIECE)), (int(x + (BOXW - PIECE) / 2), y + FY + k2 * PIECE))
                text_c(d, x + w / 2, y + 11, txt[p].upper(), font(12), (255, 255, 255))
            elif k == "key":
                key, i = pl["extra"]; n = len([q for q in PLACED[tab] if q["kind"] == "key" and q["extra"][0] == key])
                on = round(v * (n - 1)) == i
                im.alpha_composite(im_(f"sp_key_{key}_{i}_{'on' if on else 'off'}.png"), (x, y))
            elif k == "redkey":
                im.alpha_composite(im_(f"sp_red_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
            elif k in ("box", "text"):
                s = txt[p].upper() if k == "box" else txt[p]
                size = 15 if k == "box" else int(pl["extra"] * 0.8); f = font(size)
                while d.textlength(s, font=f) > w - 8 and size > 7: size -= 1; f = font(size)
                text_c(d, x + w / 2, y + h / 2, s, f, LCD_TXT)
        path = os.path.join(outdir, f"skin-preview-{tab.lower()}.png")
        im.convert("RGB").save(path); sheets.append(im.convert("RGB")); print("preview:", path)
    sheet = Image.new("RGB", (W, H * len(sheets) + 10 * (len(sheets) - 1)), (0, 0, 0))
    for i, s in enumerate(sheets): sheet.paste(s, (0, i * (H + 10)))
    sheet.resize((sheet.size[0] // 2, sheet.size[1] // 2), Image.LANCZOS).save(os.path.join(outdir, "skin-preview-all.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
