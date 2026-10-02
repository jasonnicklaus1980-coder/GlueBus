#!/usr/bin/env python3
"""Generate the Da Maze Muncher MPC screen skin (GlueBus / RadioReady skin pipeline) and a preview.

One page: the maze on the left (175 cells, each a filmstrip showing what is in that cell), the side panel on the right
(score, high score, lives, level, message, D-pad, START, speed, beat, volume, pad map). Walls are drawn into the
background from src/maze.h, so the skin always matches the game.
Filmstrips follow the format proven on the MPC X: 128 square frames, numFrames 127, at most ~84 px wide, inside their box.
Every control is bound to "Parameter N"; indices come from the built plugin (MM_ParamKey), so run `make native` first.
Requires Pillow.  Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, re, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - Da Maze Muncher")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4
CELL, GX, GY = 34, 22, 50                      # maze cell size and origin
LIB = os.path.join(ROOT, "build", "native", "mazemuncher.so")

MAZE = re.findall(r'"([^"]*)"', open(os.path.join(ROOT, "src", "maze.h")).read().split("MAZE-BEGIN")[1].split("MAZE-END")[0])
MR, MC = len(MAZE), len(MAZE[0])

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
FOCUS_COL = "ffff4a4a"

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

def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.MM_ParamKey.restype = ctypes.c_char_p; lib.MM_ParamKey.argtypes = [ctypes.c_int]
    lib.MM_ParamCount.restype = ctypes.c_int
    return {lib.MM_ParamKey(i).decode(): i for i in range(lib.MM_ParamCount())}
P = load_keys()

# ---------- layout ----------
DEFS, IMAGES, PLACED, BG_ITEMS = {}, {}, [], []
def img(name, im): IMAGES[name] = im; return name
def defn(key, value): DEFS.setdefault(key, value); return key
def place(name, dkey, param, x, y, w, h, kind, extra=None, touch=True):
    PLACED.append(dict(name=name, dkey=dkey, param=param, x=x, y=y, w=w, h=h, kind=kind, extra=extra, touch=touch))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
def d_text(w, h, fs, col):
    return defn(f"mmText{w}x{h}_{fs}_{col}", definition([], [label("Value", fs, col, (0, 0, w, h))], ignore=True))
def text(name, key, x, y, w, h, fs, col="ff78ffc8"):
    place(name, d_text(w, h, fs, col), P[key], x, y, w, h, "text", fs, touch=False)
def keys(key, labels, xs, ys, w, h, fs=15):
    for i, (lab, x, y) in enumerate(zip(labels, xs, ys)):
        on, off = img(f"mm_{key}_{i}_on.png", key_image(True, w, h, lab, fs=fs)), img(f"mm_{key}_{i}_off.png", key_image(False, w, h, lab, fs=fs))
        k = defn(f"mmKey_{key}_{i}", definition([action("Mouse Down", "Q-Link")], [comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off,
                 "buttonId": i, "numButtonsInGroup": len(labels), "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))]))
        place(f"{key} {lab}", k, P[key], x, y, w, h, "key", (key, i, len(labels)))
def toggle(name, key, x, y, w, h, lab, red=False):
    on, off = img(f"mm_{key}_on.png", key_image(True, w, h, lab, red)), img(f"mm_{key}_off.png", key_image(False, w, h, lab, red))
    k = defn(f"mmToggle_{key}", definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [focus((0, 0, w, h)),
             comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data",
                  "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))]))
    place(name, k, P[key], x, y, w, h, "toggle", (key,))
def box(name, key, x, y, w, h, fs=16):
    img("mm_drag.png", Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)))
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "mm_drag.png", "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
    k = defn(f"mmBox{w}x{h}", definition(CTRL(), [drag, label("Value", fs, "ff78ffc8", (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))]))
    place(name, k, P[key], x, y, w, h, "box")
    BG_ITEMS.append(("lcd", x, y, w, h))

# maze cells
img("mm_cells.png", cell_strip())
defn("mmCell", definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "mm_cells.png", "numFrames": NUMFRAMES,
     "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, CELL, CELL)))], ignore=True))
k = 0
for r in range(MR):
    for c in range(MC):
        if MAZE[r][c] == "#": continue
        place(f"Cell {k + 1}", "mmCell", P[f"cell{k}"], GX + c * CELL, GY + r * CELL, CELL, CELL, "cell", touch=False); k += 1

# side panel
PX = 768
for i, (cap, key) in enumerate((("SCORE", "score"), ("HIGH SCORE", "high"))):
    x = PX + i * 248
    BG_ITEMS.append(("cap", x, 60, cap)); BG_ITEMS.append(("lcd", x, 82, 230, 46))
    text(cap, key, x, 82, 230, 46, 26)
for i, (cap, key) in enumerate((("LIVES", "lives"), ("LEVEL", "level"))):
    x = PX + i * 248
    BG_ITEMS.append(("cap", x, 146, cap)); BG_ITEMS.append(("lcd", x, 168, 230, 40))
    text(cap, key, x, 168, 230, 40, 22)
BG_ITEMS.append(("lcd", PX, 228, 478, 44))
text("Message", "msg", PX, 228, 478, 44, 22, "ffffce48")
# D-pad (one radio group: the direction the Muncher wants to go)
DX, DY, KW = PX + 26, 300, 76
keys("dir", ["\u25b2", "\u25bc", "\u25c0", "\u25b6"], [DX + KW, DX + KW, DX, DX + 2 * KW], [DY, DY + 2 * 62, DY + 62, DY + 62], KW - 6, 58, 22)
toggle("Start / Pause", "start", PX + 296, DY + 2, 176, 64, "START / PAUSE", red=True)
keys("speed", ["SLOW", "NORM", "FAST"], [PX + 296, PX + 296 + 60, PX + 296 + 120], [DY + 98] * 3, 56, 40, 12)
BG_ITEMS.append(("cap", PX + 296, DY + 78, "SPEED"))
toggle("Beat", "beat", PX + 296, DY + 160, 84, 40, "BEAT")
box("Volume", "volume", PX + 388, DY + 160, 84, 40, 14)
BG_ITEMS.append(("cap", PX + 388, DY + 140, "VOLUME"))
BG_ITEMS.append(("cap", PX + 296, DY + 140, "DRUMS"))

QL = ["dir", "start", "speed", "beat", "volume"]

def background():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 40], fill=(20, 24, 44)); d.line([0, 40, W, 40], fill=NEON, width=2)
    d.text((22, 6), "DA MAZE MUNCHER", font=font(24), fill=GOLD)
    d.text((312, 15), "eat every dot  \u00b7  dodge Kick, Snare, Hat & Clap  \u00b7  records scare the bugs", font=font(11, "DejaVuSans.ttf"), fill=DIM)
    tw = d.textlength("RadioReady Audio", font=font(12)); d.text((W - 22 - tw, 14), "RadioReady Audio", font=font(12), fill=DIM)
    # maze walls: filled blocks with a neon edge where they meet the corridors
    wall = lambda r, c: 0 <= r < MR and 0 <= c < MC and MAZE[r][c] == "#"
    s = 2; big = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0)); bd = ImageDraw.Draw(big)
    for r in range(MR):
        for c in range(MC):
            if not wall(r, c): continue
            x0, y0 = (GX + c * CELL) * s, (GY + r * CELL) * s; x1, y1 = x0 + CELL * s, y0 + CELL * s
            bd.rectangle([x0, y0, x1, y1], fill=rgba(WALL))
            ins = 5 * s
            if not wall(r - 1, c): bd.line([x0, y0 + ins, x1, y0 + ins], fill=rgba(NEON), width=2 * s)
            if not wall(r + 1, c): bd.line([x0, y1 - ins, x1, y1 - ins], fill=rgba(NEON), width=2 * s)
            if not wall(r, c - 1): bd.line([x0 + ins, y0, x0 + ins, y1], fill=rgba(NEON), width=2 * s)
            if not wall(r, c + 1): bd.line([x1 - ins, y0, x1 - ins, y1], fill=rgba(NEON), width=2 * s)
    for r in range(MR):                                              # carve the corridor margin back out of the blocks
        for c in range(MC):
            if wall(r, c): continue
            x0, y0 = (GX + c * CELL) * s, (GY + r * CELL) * s
            bd.rectangle([x0 - 4 * s if wall(r, c - 1) else x0, y0 - 4 * s if wall(r - 1, c) else y0,
                          x0 + CELL * s + (4 * s if wall(r, c + 1) else 0), y0 + CELL * s + (4 * s if wall(r + 1, c) else 0)], fill=rgba(BG))
    big = big.resize((W, H), Image.LANCZOS); im.paste(big, (0, 0), big); d = ImageDraw.Draw(im)
    for item in BG_ITEMS:
        if item[0] == "lcd":
            _, x, y, w, h = item; d.rounded_rectangle([x - 3, y - 3, x + w + 3, y + h + 3], radius=5, fill=(40, 46, 80)); d.rectangle([x, y, x + w, y + h], fill=LCD_BG)
        elif item[0] == "cap":
            _, x, y, cap = item; d.text((x, y), cap, font=font(13), fill=DIM)
    # pad map
    y0 = 524
    d.text((PX, y0), "PADS", font=font(13), fill=DIM)
    names = {10: "\u25b2", 5: "\u25c0", 6: "\u25bc", 7: "\u25b6", 13: "ST", 16: "ST"}
    for p in range(1, 17):
        r, c = divmod(p - 1, 4); x, y = PX + 52 + c * 30, y0 + 72 - r * 22
        on = p in names
        d.rounded_rectangle([x, y, x + 26, y + 18], radius=3, fill=(90, 110, 210) if on else (36, 40, 64))
        if on: text_c(d, x + 13, y + 9, names[p], font(10), PRINT)
    d.text((PX + 186, y0 + 4), "Plugin track pads (any bank):", font=font(12, "DejaVuSans.ttf"), fill=DIM)
    d.text((PX + 186, y0 + 24), "10 up  5 left  6 down  7 right", font=font(12, "DejaVuSans.ttf"), fill=PRINT)
    d.text((PX + 186, y0 + 44), "13 or 16 start / pause", font=font(12, "DejaVuSans.ttf"), fill=PRINT)
    d.text((PX + 186, y0 + 64), "Dot 10 \u00b7 record 50 \u00b7 bug 200+", font=font(12, "DejaVuSans.ttf"), fill=DIM)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    IMAGES["mm_bg.png"] = background()
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "mm_bg.png"}, bounds((0, 0, W, H)))]
    for pl in sorted(PLACED, key=lambda q: 1 if q["touch"] else 0):
        comps.append(comp(pl["name"], pl["dkey"], {"version": 1, "handleName": "Data"},
                          bounds((pl["x"], pl["y"], pl["w"], pl["h"]), focus="Yes" if pl["touch"] else "No", show="Hide" if pl["touch"] else "Show"),
                          {"version": 1, "map": [{"key": "Data", "value": f"Parameter {pl['param']}"}]}))
    DEFS["MM|GAME"] = definition([], comps, "ff0c0e1a")
    defs = [{"key": k, "value": v} for k, v in DEFS.items()]
    tabs = [{"version": 3, "tabName": "GAME", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "MM|GAME", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=2)
    qmap = {f"Q-Link {i + 1}": P[k] for i, k in enumerate(QL)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=2)
    total = 0
    for name, im in IMAGES.items():
        im.save(os.path.join(OUT, name), optimize=True); total += im.size[0] * im.size[1] * 4
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>radioready.vst.damazemuncher</identifier>\n"
        "\t<version>1.0.0.0</version>\n</plugincontent>\n")
    tallest = max(im.size[1] for im in IMAGES.values())
    print(f"skin written to {SKIN_DIR}: {len(IMAGES) + 2} files, {total / 1e6:.1f} MB decoded, tallest image {tallest} px")

# ---------- preview: the real plugin, mid-game ----------
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
    n = 512; OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)(); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    def run(sec):
        for _ in range(int(sec * 44100 / n)): e.processReplacing(fx, None, outs, n)
    e.setParameter(fx, P["start"], 1.0); run(2.6)
    for d, sec in ((3, 0.7), (0, 0.45), (2, 0.6), (0, 0.5), (3, 0.9)):   # steer with the on-screen D-pad
        e.setParameter(fx, P["dir"], d / 3.0); run(sec)
    vals = [e.getParameter(fx, i) for i in range(e.numParams)]
    txt = []
    for i in range(e.numParams):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); txt.append(b.value.decode("utf-8", "replace"))
    D(1)
    return vals, txt

def preview(outdir):
    vals, txt = plugin_state()
    os.makedirs(outdir, exist_ok=True)
    im = Image.open(os.path.join(OUT, "mm_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    cells = Image.open(os.path.join(OUT, "mm_cells.png")).convert("RGBA")
    for pl in PLACED:
        x, y, w, h = pl["x"], pl["y"], pl["w"], pl["h"]; p = pl["param"]; v = vals[p]; k = pl["kind"]
        if k == "cell":
            fr = round(v * NUMFRAMES); im.alpha_composite(cells.crop((0, fr * CELL, CELL, fr * CELL + CELL)), (x, y))
        elif k == "key":
            key, i, n = pl["extra"]; on = round(v * (n - 1)) == i
            im.alpha_composite(Image.open(os.path.join(OUT, f"mm_{key}_{i}_{'on' if on else 'off'}.png")).convert("RGBA"), (x, y))
        elif k == "toggle":
            key, = pl["extra"]
            im.alpha_composite(Image.open(os.path.join(OUT, f"mm_{key}_{'on' if v >= 0.5 else 'off'}.png")).convert("RGBA"), (x, y))
        elif k in ("text", "box"):
            s = txt[p] if k == "text" else txt[p].upper(); size = int(pl["extra"] * 0.85) if k == "text" else 14; f = font(size)
            text_c(d, x + w / 2, y + h / 2, s, f, (255, 206, 72) if p == P["msg"] else LCD_TXT)
    path = os.path.join(outdir, "skin-preview.png"); im.convert("RGB").save(path); print("preview:", path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
