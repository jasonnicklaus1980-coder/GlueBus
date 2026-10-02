#!/usr/bin/env python3
"""Generate the MPC Arcade plugin's screen skin (same TUI.json pipeline as the GlueBus / Da Maze Muncher plugins).
One page: the arcade marquee, the game picker (Q-Link 1), LIBRARY / PLAY / RESUME keys, status and details lines.
Parameter indices come from the built plugin (MA_ParamKey): run `make native` first.  Needs Pillow.
Usage: python3 tools/make_skin.py [preview-dir]"""
import ctypes, json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - MPC Arcade")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4
LIB = os.path.join(ROOT, "build", "native", "mpcarcade.so")
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


BG, PRINT, DIM, ACCENT, GOLD, LCD_BG, LCD_TXT = (17, 17, 19), (236, 236, 240), (150, 152, 164), (232, 57, 47), (245, 175, 40), (8, 9, 12), (236, 236, 240)

def key_image(on, w, h, label, sub, primary):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    face = (ACCENT if on else (170, 40, 34)) if primary else ((86, 90, 104) if on else (52, 54, 62))
    d.rounded_rectangle([2 * s, 4 * s, (w - 1) * s, (h - 1) * s], radius=12 * s, fill=(0, 0, 0, 120))
    off = 2 * s if on else 0
    d.rounded_rectangle([0, off, (w - 3) * s, (h - 5) * s + off], radius=12 * s, fill=rgba(face), outline=rgba(mix(face, (255, 255, 255), 0.3)), width=s)
    text_c(d, (w - 3) * s / 2, (h - 5) * s * 0.42 + off, label, font(30 * s), rgba(PRINT))
    text_c(d, (w - 3) * s / 2, (h - 5) * s * 0.75 + off, sub, font(13 * s, "DejaVuSans.ttf"), rgba(mix(PRINT, face, 0.25)))
    return im.resize((w, h), Image.LANCZOS)

def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.MA_ParamKey.restype = ctypes.c_char_p; lib.MA_ParamKey.argtypes = [ctypes.c_int]
    lib.MA_ParamCount.restype = ctypes.c_int
    return {lib.MA_ParamKey(i).decode(): i for i in range(lib.MA_ParamCount())}
P = load_keys()
DEFS, IMAGES, COMPS = {}, {}, []
def img(name, im): IMAGES[name] = im; return name
def defn(key, value): DEFS.setdefault(key, value); return key
def place(name, dkey, param, x, y, w, h, touch=True):
    COMPS.append(comp(name, dkey, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"),
                      {"version": 1, "map": [{"key": "Data", "value": f"Parameter {param}"}]}))

def text(name, key, x, y, w, h, fs, col):
    k = defn(f"maText{w}x{h}_{fs}_{col}", definition([], [label("Value", fs, col, (0, 0, w, h))], ignore=True))
    place(name, k, P[key], x, y, w, h, touch=False)
def button(name, key, x, y, w, h, lab, sub, primary=False):
    on, off = img(f"ma_{key}_on.png", key_image(True, w, h, lab, sub, primary)), img(f"ma_{key}_off.png", key_image(False, w, h, lab, sub, primary))
    k = defn(f"maKey_{key}", definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [focus((0, 0, w, h)),
             comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data",
                  "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))]))
    place(name, k, P[key], x, y, w, h)
def picker(name, key, x, y, w, h):
    img("ma_drag.png", Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)))
    drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "ma_drag.png", "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Horizontal", "handleName": "Data"}, bounds((0, 0, w, h)))
    k = defn("maPicker", definition([action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
             action("Enter Pressed", "Show Overlay", "knob overlay")], [drag, label("Value", 30, "ffececf0", (0, 0, w, h)), focus((0, 0, w, h))]))
    place(name, k, P[key], x, y, w, h)

GX, GY, GW, GH = 60, 150, 1160, 96
picker("Game", "game", GX, GY, GW, GH)
text("Details", "details", GX, GY + GH + 6, GW, 34, 17, "ff9698a4")
BW, BH, BY = 360, 150, 330
button("Open Library", "library", 60, BY, BW, BH, "LIBRARY", "browse, add games, settings")
button("Play Game", "play", 460, BY, BW, BH, "PLAY", "the game above", primary=True)
button("Resume Last", "resume", 860, BY, BW, BH, "RESUME", "the last game you played")
text("Status", "status", 60, 508, 1160, 40, 20, "fff5af28")

def background():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    for y in range(110):                                             # marquee
        c = mix((44, 14, 14), BG, y / 110); d.line([0, y, W, y], fill=c)
    d.rounded_rectangle([60, 28, 112, 80], radius=10, fill=ACCENT)
    d.polygon([(78, 40), (78, 68), (100, 54)], fill=PRINT)
    d.text((128, 26), "MPC ARCADE", font=font(42), fill=PRINT)
    d.text((132, 80), "MAME arcade games on your MPC", font=font(15, "DejaVuSans.ttf"), fill=DIM)
    tw = d.textlength("RadioReady Audio", font=font(13)); d.text((W - 60 - tw, 42), "RadioReady Audio", font=font(13), fill=DIM)
    d.text((GX, GY - 28), "GAME  \u00b7  turn Q-Link 1 or drag", font=font(14), fill=DIM)
    d.rounded_rectangle([GX - 4, GY - 4, GX + GW + 4, GY + GH + 4], radius=14, fill=(46, 47, 54)); d.rounded_rectangle([GX, GY, GX + GW, GY + GH], radius=12, fill=LCD_BG)
    for i, x in enumerate((GX + 22, GX + GW - 22)):
        d.text((x - 8, GY + 30), "\u25c0" if i == 0 else "\u25b6", font=font(26), fill=(70, 72, 82))
    d.text((60, 566), "Tap a key, then tap it again within 6 s to start. The MPC app closes while you play (save your project first) "
           "and opens again when you leave the arcade.", font=font(13, "DejaVuSans.ttf"), fill=DIM)
    d.text((60, 590), "In a game: hold the EXIT pad (or the top-right corner of the screen) to return to the library.", font=font(13, "DejaVuSans.ttf"), fill=DIM)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    IMAGES["ma_bg.png"] = background()
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "ma_bg.png"}, bounds((0, 0, W, H)))] + COMPS
    DEFS["MA|MAIN"] = definition([], comps, "ff111113")
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": [{"key": k, "value": v} for k, v in DEFS.items()]},
        "info": {"version": 1, "type": "CompleteDescription"},
        "tabs": [{"version": 3, "tabName": "ARCADE", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
                  "componentName": "MA|MAIN", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=2)
    qmap = {"Q-Link 1": P["game"], "Q-Link 2": P["library"], "Q-Link 3": P["play"], "Q-Link 4": P["resume"]}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=2)
    for name, im in IMAGES.items(): im.save(os.path.join(OUT, name), optimize=True)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>radioready.vst.mpcarcade</identifier>\n"
        "\t<version>1.0.0.0</version>\n</plugincontent>\n")
    print(f"skin written to {SKIN_DIR}: {len(IMAGES) + 3} files")

def preview(outdir):
    os.makedirs(outdir, exist_ok=True)
    im = Image.open(os.path.join(OUT, "ma_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    for key, x in (("library", 60), ("play", 460), ("resume", 860)):
        im.alpha_composite(Image.open(os.path.join(OUT, f"ma_{key}_off.png")).convert("RGBA"), (x, BY))
    text_c(d, GX + GW / 2, GY + GH / 2, "\u2605 Street Fighter II: The World Warrior", font(30), PRINT)
    text_c(d, GX + GW / 2, GY + GH + 23, "Capcom \u00b7 1991 \u00b7 Fighting", font(17), DIM)
    text_c(d, W / 2, 528, "12 games ready \u00b7 Q-Link 1 picks one", font(20), GOLD)
    p = os.path.join(outdir, "skin-preview.png"); im.convert("RGB").save(p); print("preview:", p)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
