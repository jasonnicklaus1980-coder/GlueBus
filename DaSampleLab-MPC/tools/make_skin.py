#!/usr/bin/env python3
"""Generate the Da Sample Lab screen skin (GlueBus / RadioReady TUI.json pipeline) and previews.
Three tabs: SAMPLE (waveform, file, start/end, BPM, pitch, time), CHOP (chop method, slices, slice edit, export),
PADS (pad mode, notes, trigger, envelope). The waveform is 64 filmstrip bars driven by read-only parameters
(frame = state * 32 + height; state 0 outside start/end, 1 inside, 2 selected slice, 3 slice marker).
Parameter indices come from the built plugin (SL_ParamKey): build build/native/dasamplelab.so first.  Needs Pillow.
Usage: python3 tools/make_skin.py [preview-dir]"""
import ctypes, json, os, shutil, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - Da Sample Lab")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4
LIB = os.path.join(ROOT, "build", "native", "dasamplelab.so")
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

# ---------- palette ----------
BG      = (12, 14, 26)
WALL    = (24, 36, 96)
NEON    = (70, 140, 255)
PRINT   = (226, 230, 244)
DIM     = (140, 148, 176)
GOLD    = (255, 206, 72)
TEAL    = (40, 214, 186)
ENEMY   = [(255, 74, 74), (255, 118, 214), (72, 220, 255), (255, 170, 56)]   # Kick, Snare, Hat, Clap
SCARED  = (60, 80, 255)
LCD_BG, LCD_TXT = (8, 10, 18), (120, 255, 200)
FOCUS_COL = "ffe8392f"

# ---------- cell sprites (frame order = enum Frame in src/Game.h) ----------
def sprite(frame):
    s = SS; n = CELL * s
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c = n / 2
    def eyes(cx, cy, dx, dy, r=3.2, white=(255, 255, 255), pupil=(20, 20, 40)):
        for ex in (-5.5, 5.5):
            x, y = cx + ex * s, cy
            d.ellipse([x - r * s, y - r * s * 1.2, x + r * s, y + r * s * 1.2], fill=rgba(white))
            d.ellipse([x + dx * 1.4 * s - 1.6 * s, y + dy * 1.6 * s - 1.6 * s, x + dx * 1.4 * s + 1.6 * s, y + dy * 1.6 * s + 1.6 * s], fill=rgba(pupil))
    def muncher(dirn, mouth, scale=1.0, alpha=255):
        # the Muncher: a teal pad-shaped body with a slot mouth on the side it faces
        h = 12.5 * s * scale
        d.rounded_rectangle([c - h, c - h, c + h, c + h], radius=5 * s * scale, fill=rgba(TEAL, alpha), outline=rgba(mix(TEAL, (0, 0, 0), 0.4), alpha), width=s)
        dx, dy = [(0, -1), (0, 1), (-1, 0), (1, 0)][dirn]
        mw, md = (9 if mouth == 0 else 9) * s * scale, (6 if mouth == 0 else 1.2) * s * scale
        mx, my = c + dx * (h - md / 2 - 1.5 * s * scale), c + dy * (h - md / 2 - 1.5 * s * scale)
        if dx: d.rectangle([mx - md / 2, my - mw / 2, mx + md / 2, my + mw / 2], fill=rgba((10, 30, 30), alpha))
        else: d.rectangle([mx - mw / 2, my - md / 2, mx + mw / 2, my + md / 2], fill=rgba((10, 30, 30), alpha))
        if scale > 0.6: eyes(c - dx * 3 * s, c - dy * 3 * s - (2 * s if dx else 0), dx, dy, r=2.6 * scale)
    def bug(col, face=(255, 255, 255), scared=False, flash=False):
        # a glitch bug: a rounded hexagon body, antennae and little legs
        r = 12.5 * s
        pts = [(c + r * math.cos(math.radians(a)), c + r * 0.95 * math.sin(math.radians(a))) for a in range(30, 390, 60)]
        d.polygon(pts, fill=rgba(col))
        for sx in (-1, 1):
            d.line([c + sx * 4 * s, c - 10 * s, c + sx * 8 * s, c - 15 * s], fill=rgba(col), width=2 * s)
            d.ellipse([c + sx * 8 * s - 2 * s, c - 15 * s - 2 * s, c + sx * 8 * s + 2 * s, c - 15 * s + 2 * s], fill=rgba(col))
            for k in (-1, 1):
                d.line([c + sx * 9 * s, c + k * 4 * s, c + sx * 15 * s, c + k * 7 * s], fill=rgba(col), width=2 * s)
        if scared:
            mc = (255, 80, 80) if flash else (230, 230, 255)
            for k in range(4): d.line([c - 7 * s + k * 4 * s, c + 5 * s + (k % 2) * 2 * s, c - 3 * s + k * 4 * s, c + 5 * s + ((k + 1) % 2) * 2 * s], fill=rgba(mc), width=s * 2)
            for ex in (-5, 5): d.ellipse([c + ex * s - 2 * s, c - 4 * s - 2 * s, c + ex * s + 2 * s, c - 4 * s + 2 * s], fill=rgba(mc))
        else: eyes(c, c - 2 * s, 0, 0.4)
    if frame == 1: d.ellipse([c - 3 * s, c - 3 * s, c + 3 * s, c + 3 * s], fill=rgba((255, 236, 200)))
    elif frame in (2, 3):                                    # power record: a small vinyl
        r = (11 if frame == 2 else 9) * s; col = (30, 30, 34) if frame == 2 else (60, 60, 70)
        d.ellipse([c - r, c - r, c + r, c + r], fill=rgba(col), outline=rgba((120, 120, 130)), width=s)
        for k in (0.75, 0.55): d.ellipse([c - r * k, c - r * k, c + r * k, c + r * k], outline=rgba((70, 70, 80)), width=s)
        lr = 4 * s; d.ellipse([c - lr, c - lr, c + lr, c + lr], fill=rgba((255, 120, 40) if frame == 2 else (150, 90, 60)))
        d.ellipse([c - s, c - s, c + s, c + s], fill=rgba((10, 10, 10)))
    elif 4 <= frame < 12: muncher((frame - 4) // 2, (frame - 4) % 2)
    elif 12 <= frame < 16: bug(ENEMY[frame - 12])
    elif frame == 16: bug(SCARED, scared=True)
    elif frame == 17: bug((240, 240, 255), scared=True, flash=True)
    elif frame == 18: eyes(c, c, 0, 0, r=3.6)
    elif 19 <= frame < 24:                                   # Muncher caught: shrinks and sparks
        k = frame - 19
        muncher(UP_ := 0, 0, scale=1.0 - k * 0.18, alpha=255 - k * 40)
        for a in range(0, 360, 45):
            rr = (6 + k * 3) * s
            x, y = c + rr * math.cos(math.radians(a + k * 20)), c + rr * math.sin(math.radians(a + k * 20))
            d.ellipse([x - 1.5 * s, y - 1.5 * s, x + 1.5 * s, y + 1.5 * s], fill=rgba(GOLD, 255 - k * 30))
    elif frame == 24:                                        # bonus: a gold eighth note
        d.ellipse([c - 8 * s, c + 2 * s, c + 1 * s, c + 9 * s], fill=rgba(GOLD))
        d.rectangle([c - 0.5 * s, c - 11 * s, c + 1.5 * s, c + 6 * s], fill=rgba(GOLD))
        d.polygon([(c + 1.5 * s, c - 11 * s), (c + 9 * s, c - 5 * s), (c + 9 * s, c - 1 * s), (c + 1.5 * s, c - 6 * s)], fill=rgba(GOLD))
    elif frame == 25: d.rectangle([2 * s, c - 2 * s, n - 2 * s, c + 2 * s], fill=rgba((255, 140, 200)))
    return im.resize((CELL, CELL), Image.LANCZOS)

def cell_strip():
    strip = Image.new("RGBA", (CELL, CELL * FRAMES), (0, 0, 0, 0))
    for f in range(26): strip.paste(sprite(f), (0, f * CELL))
    return strip

def key_image(on, w, h, label, red=False, fs=15):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    face = ((255, 70, 60) if on else (150, 36, 32)) if red else ((90, 110, 210) if on else (44, 50, 84))
    d.rounded_rectangle([2 * s, 3 * s, (w - 1) * s, (h - 1) * s], radius=6 * s, fill=(0, 0, 0, 120))
    off = s if on else 0
    d.rounded_rectangle([0, off, (w - 3) * s, (h - 4) * s + off], radius=6 * s, fill=rgba(face), outline=rgba(mix(face, (255, 255, 255), 0.35)), width=s)
    if on and not red:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle([0, 0, (w - 3) * s, (h - 4) * s], radius=6 * s, outline=(150, 190, 255, 200), width=3 * s)
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(3 * s))); d = ImageDraw.Draw(im)
    text_c(d, (w - 3) * s / 2, ((h - 4) * s + off) / 2 + off / 2, label, font(fs * s), rgba(PRINT))
    return im.resize((w, h), Image.LANCZOS)

# ---------- TUI.json (GlueBus schema) ----------
NOREMAP = {"version": 1, "map": []}
def bounds(b, focus="No", show="Show", visible="Always"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": " ".join(str(int(v)) for v in b), "additionalInvalidatingHandles": []}
def comp(name, typ, data, b, remap=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": remap or NOREMAP, "bounds": b}
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



BG, PANEL, PANEL2, PRINT, DIM, ACCENT, GOLD, TEAL = (17, 17, 19), (30, 31, 35), (42, 43, 49), (236, 236, 240), (150, 152, 164), (232, 57, 47), (245, 175, 40), (40, 200, 180)

def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.SL_ParamKey.restype = ctypes.c_char_p; lib.SL_ParamKey.argtypes = [ctypes.c_int]
    lib.SL_ParamCount.restype = ctypes.c_int
    return {lib.SL_ParamKey(i).decode(): i for i in range(lib.SL_ParamCount())}
P = load_keys()
DEFS, IMAGES = {}, {}
TABS = {}                      # tab name -> (components, background items, qlinks)
def img(name, im): IMAGES[name] = im; return name
def defn(key, value): DEFS.setdefault(key, value); return key

class Page:
    def __init__(self, name): self.name, self.comps, self.bg, self.q = name, [], [], []
    def place(self, label, dkey, param, x, y, w, h, touch=True):
        self.comps.append(comp(label, dkey, {"version": 1, "handleName": "Data"},
                               bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"),
                               {"version": 1, "map": [{"key": "Data", "value": f"Parameter {param}"}]}))
    def text(self, label, key, x, y, w, h, fs, col):
        k = defn(f"slText{w}x{h}_{fs}_{col}", definition([], [label_ (fs, col, (0, 0, w, h))], ignore=True))
        self.place(label, k, P[key], x, y, w, h, touch=False)
    def box(self, label, key, x, y, w=134, h=74):
        img("sl_drag.png", Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)))
        drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "sl_drag.png", "numFrames": NUMFRAMES, "invert": False,
                    "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h - 26)))
        k = defn(f"slBox{w}x{h}", definition([action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
                 action("Enter Pressed", "Show Overlay", "knob overlay")], [drag, label_ (17, "fff5af28", (0, 0, w, h - 26)), focus((0, 0, w, h - 26))]))
        self.place(label, k, P[key], x, y + 26, w, h - 26)
        self.bg.append(("box", x, y, w, h, label.upper()))
        if len(self.q) < 16: self.q.append(P[key])
    def button(self, label, key, x, y, w, h, lab, red=False, toggle=False):
        on, off = img(f"sl_{key}_on.png", key_image(True, w, h, lab, red)), img(f"sl_{key}_off.png", key_image(False, w, h, lab, red))
        acts = [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
        k = defn(f"slKey_{key}", definition(acts, [focus((0, 0, w, h)), comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off,
                 "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))]))
        self.place(label, k, P[key], x, y, w, h)
    def wave(self, x, y, w, h=144):
        # each bar = two mirrored 72 px filmstrip cells driven by the same parameter (the proven 72 px frame size)
        bw, half = w // 64, h // 2
        top, bot = img("sl_wave_top.png", wave_strip (bw - 2, half, True)), img("sl_wave_bot.png", wave_strip (bw - 2, half, False))
        kt = defn(f"slWaveT{bw}", definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": top, "numFrames": NUMFRAMES,
                  "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, bw - 2, half)))], ignore=True))
        kb = defn(f"slWaveB{bw}", definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": bot, "numFrames": NUMFRAMES,
                  "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, bw - 2, half)))], ignore=True))
        for i in range(64):
            self.place(f"Wave {i + 1}", kt, P[f"wave{i}"], x + i * bw, y, bw - 2, half, touch=False)
            self.place(f"Wave {i + 1} ", kb, P[f"wave{i}"], x + i * bw, y + half, bw - 2, half, touch=False)
        self.bg.append(("wave", x - 6, y - 6, bw * 64 + 10, h + 12))

def label_(h, colour, b):
    return comp("Value", "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": "Original"}, "type": "Value", "handleName": "Data"}, bounds(b))

def key_image(on, w, h, lab, red):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    face = (ACCENT if on else (150, 36, 32)) if red else ((96, 100, 116) if on else (52, 54, 62))
    off = s if on else 0
    d.rounded_rectangle([0, off, (w - 2) * s, (h - 3) * s + off], radius=8 * s, fill=rgba(face), outline=rgba(mix(face, (255, 255, 255), 0.3)), width=s)
    text_c(d, (w - 2) * s / 2, ((h - 3) * s) / 2 + off, lab, font(15 * s), rgba(PRINT))
    return im.resize((w, h), Image.LANCZOS)

def wave_strip(bw, h, top):
    # top cells grow up from their bottom edge, bottom cells grow down from their top edge
    strip = Image.new("RGBA", (bw, h * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    cols = [(70, 72, 82), (56, 140, 230), ACCENT, (240, 240, 245)]
    for f in range(FRAMES):
        state, hh = divmod(f, 32)
        y0 = f * h
        amp = max(1, int((h - 2) * hh / 31))
        if top: d.rectangle([1, y0 + h - amp, bw - 2, y0 + h - 1], fill=rgba(cols[min(state, 3)]))
        else: d.rectangle([1, y0, bw - 2, y0 + amp - 1], fill=rgba(cols[min(state, 3)]))
        if state == 3:                                       # a slice starts here: blue bar + gold line
            if top: d.rectangle([1, y0 + h - amp, bw - 2, y0 + h - 1], fill=rgba(cols[1]))
            else: d.rectangle([1, y0, bw - 2, y0 + amp - 1], fill=rgba(cols[1]))
            d.rectangle([0, y0, 1, y0 + h - 1], fill=rgba(GOLD))
            if top: d.polygon([(0, y0), (7, y0), (0, y0 + 9)], fill=rgba(GOLD))
    return strip

pages = []
# ---------------- SAMPLE
pg = Page("SAMPLE"); pages.append(pg)
pg.wave(40, 70, 1216)
pg.text("Info", "info", 40, 230, 760, 30, 17, "ffececf0")
pg.text("Analysis", "analysis", 820, 230, 420, 30, 20, "fff5af28")
x0, y0, dx = 40, 310, 146
for i, (lab, key) in enumerate((("Sample", "file"), ("Start", "start"), ("End", "end"), ("BPM", "bpm"), ("Pitch", "pitch"), ("Fine", "fine"), ("Level", "gain"))):
    pg.box(lab, key, x0 + i * dx, y0, 134 if key != "file" else 134)
for i, (lab, key) in enumerate((("Time Mode", "tmode"), ("Sync", "sync"), ("Speed", "speed"))):
    pg.box(lab, key, x0 + i * dx, y0 + 96)
pg.button("Load", "load", x0 + 3 * dx, y0 + 122, 134, 48, "LOAD", red=True)
pg.button("Play Sample", "audition", x0 + 4 * dx, y0 + 122, 134, 48, "\u25b6 PLAY")
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")
# ---------------- CHOP
pg = Page("CHOP"); pages.append(pg)
pg.wave(40, 64, 1216)
pg.text("Slice Info", "sliceinfo", 40, 218, 1200, 28, 18, "fff5af28")
for i, (lab, key) in enumerate((("Chop By", "chopmode"), ("Sensitivity", "sens"), ("Slices", "count"))):
    pg.box(lab, key, x0 + i * dx, 266)
pg.button("Chop", "chop", x0 + 3 * dx, 292, 134, 48, "CHOP", red=True)
for i, (lab, key) in enumerate((("Slice", "slice"), ("Slice Start", "slstart"), ("Slice End", "slend"), ("Slice Level", "slvol"), ("Slice Pitch", "slpitch"))):
    pg.box(lab, key, x0 + i * dx, 360)
pg.button("Reverse", "slrev", x0 + 5 * dx, 386, 134, 48, "REVERSE")
pg.button("Loop", "slloop", x0 + 6 * dx, 386, 134, 48, "LOOP")
pg.button("Play Slice", "slplay", x0 + 7 * dx, 386, 134, 48, "\u25b6 SLICE")
for i, (lab, key, t) in enumerate((("Move Left", "slleft", "\u25c0 MOVE"), ("Move Right", "slright", "MOVE \u25b6"), ("Split", "split", "SPLIT"),
                                   ("Merge", "merge", "MERGE"), ("Export Slice", "export", "EXPORT"), ("Export All", "exportall", "EXPORT ALL"))):
    pg.button(lab, key, x0 + i * dx, 470, 134, 48, t)
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")
# ---------------- PADS
pg = Page("PADS"); pages.append(pg)
for i, (lab, key) in enumerate((("Pad Mode", "padmode"), ("First Pad Note", "basenote"), ("Trigger", "playmode"), ("Voices", "poly"),
                                ("Velocity", "velsens"), ("Attack", "attack"), ("Release", "release"), ("Level", "gain"))):
    pg.box(lab, key, x0 + i * dx, 70)
pg.text("Pad Info", "padinfo", 40, 170, 1200, 30, 18, "fff5af28")
pg.bg.append(("pads",))
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")

def background(page):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.text((40, 14), "DA SAMPLE LAB", font=font(26), fill=PRINT)
    d.text((300, 22), page.name, font=font(15), fill=ACCENT)
    tw = d.textlength("RadioReady Audio", font=font(12)); d.text((W - 40 - tw, 22), "RadioReady Audio", font=font(12), fill=DIM)
    for item in page.bg:
        if item[0] == "wave":
            _, x, y, w, h = item; d.rounded_rectangle([x, y, x + w, y + h], radius=10, fill=(8, 9, 12), outline=(46, 47, 54))
        elif item[0] == "box":
            _, x, y, w, h, cap = item
            d.text((x + 4, y + 2), cap, font=font(12), fill=DIM)
            d.rounded_rectangle([x, y + 24, x + w, y + h], radius=8, fill=PANEL2)
        elif item[0] == "pads":
            # how the 64 slices sit on the pads: banks A-D, pad 1 bottom-left like the MPC
            for bank in range(4):
                bx, by = 40 + bank * 300, 216
                d.text((bx, by), f"PAD BANK {chr(65 + bank)}", font=font(13), fill=DIM)
                for r in range(4):
                    for c in range(4):
                        pad = (3 - r) * 4 + c + 1; sl = bank * 16 + pad
                        x, y = bx + c * 68, by + 24 + r * 74
                        d.rounded_rectangle([x, y, x + 62, y + 66], radius=8, fill=PANEL2)
                        d.text((x + 6, y + 4), str(pad), font=font(11, "DejaVuSans.ttf"), fill=DIM)
                        text_c(d, x + 31, y + 38, f"S{sl}", font(16), PRINT)
            d.text((40, 548), "Slices mode: each pad plays its slice (notes from FIRST PAD NOTE up). Chromatic: the selected slice on every pad, C3 = original pitch.",
                   font=font(12, "DejaVuSans.ttf"), fill=DIM)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    tabs, qmaps = [], []
    for ti, page in enumerate(pages):
        bgname = f"sl_bg_{page.name.lower()}.png"
        IMAGES[bgname] = background(page)
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, bounds((0, 0, W, H)))] + page.comps
        key = f"SL|{page.name}"
        DEFS[key] = definition([], comps, "ff111113")
        tabs.append({"version": 3, "tabName": page.name, "fnKeyIndex": ti, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
                     "componentName": key, "initialSize": f"0 0 {W} {H}", "scale": 1.0})
        qmaps.append({"Tab": ti + 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": {f"Q-Link {i + 1}": p for i, p in enumerate(page.q)}})
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": [{"key": k, "value": v} for k, v in DEFS.items()]},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=2)
    prog = {f"Q-Link {i + 1}": p for i, p in enumerate((pages[0].q + pages[1].q)[:16])}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"}, "Screen Mode Q-Links": {"version": 4, "map": qmaps}, "Program Mode Q-Links": prog}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=2)
    for name, im in IMAGES.items(): im.save(os.path.join(OUT, name), optimize=True)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>radioready.vst.dasamplelab</identifier>\n"
        "\t<version>1.0.0.0</version>\n</plugincontent>\n")
    tallest = max(im.size[1] for im in IMAGES.values())
    print(f"skin: {len(IMAGES) + 3} files, tallest image {tallest} px")

# ---------- previews: the real plugin with a sample loaded and chopped ----------
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
    os.environ["SAMPLELAB_ROOT"] = workdir
    lib = ctypes.CDLL(LIB)
    cb = HOSTCB(lambda *a: 0); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0); D(12, val=1)
    n = 512; OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)(); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    def run(k=8):
        for _ in range(k): e.processReplacing(fx, None, outs, n)
    def disp(i):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); return b.value.decode("utf-8", "replace")
    time.sleep(2.7); run()
    e.setParameter(fx, P["file"], 0.0); e.setParameter(fx, P["load"], 1.0)
    for _ in range(200):
        run(); time.sleep(0.05)
        if disp(P["status"]).startswith("Loaded"): break
    e.setParameter(fx, P["slice"], 4 / 63.0); run(); time.sleep(0.2); run()
    vals = [e.getParameter(fx, i) for i in range(e.numParams)]
    txt = [disp(i) for i in range(e.numParams)]
    D(1)
    return vals, txt

def preview(outdir, workdir):
    vals, txt = plugin_state(workdir)
    os.makedirs(outdir, exist_ok=True)
    for page in pages:
        im = Image.open(os.path.join(OUT, f"sl_bg_{page.name.lower()}.png")).convert("RGBA"); d = ImageDraw.Draw(im)
        for c in page.comps:
            b = [int(v) for v in c["bounds"]["bounds"].split()]; x, y, w, h = b
            param = int(c["handle remapping"]["map"][0]["value"].split()[1]); v = vals[param]; key = c["componentData"]["type"]
            if key.startswith("slWave"):
                strip = Image.open(os.path.join(OUT, "sl_wave_top.png" if key.startswith("slWaveT") else "sl_wave_bot.png")).convert("RGBA")
                fr = round(v * NUMFRAMES); im.alpha_composite(strip.crop((0, fr * h, w, fr * h + h)), (x, y))
            elif key.startswith("slKey"):
                k2 = key[len("slKey_"):]
                im.alpha_composite(Image.open(os.path.join(OUT, f"sl_{k2}_{'on' if (v >= 0.5 and k2 in ('slrev', 'slloop')) else 'off'}.png")).convert("RGBA"), (x, y))
            elif key.startswith("slBox"):
                text_c(d, x + w / 2, y + h / 2, txt[param], font(15), GOLD)
            elif key.startswith("slText"):
                fs = int(key.split("_")[1]); col = key.split("_")[2]
                text_c(d, x + w / 2, y + h / 2, txt[param], font(int(fs * 0.85)), tuple(int(col[i:i + 2], 16) for i in (2, 4, 6)))
        path = os.path.join(outdir, f"skin-preview-{page.name.lower()}.png"); im.convert("RGB").save(path); print("preview:", path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 2: preview(sys.argv[1], sys.argv[2])
