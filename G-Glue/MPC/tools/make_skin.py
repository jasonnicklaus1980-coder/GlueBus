#!/usr/bin/env python3
"""Generate the G-Glue MPC screen skin (TUI.json pipeline shared by the GlueBus / RadioReady MPC plugins) + preview.

One landscape page, in the desktop G-Glue look (charcoal brushed plate, screws, white print):
  left:  IN meter, analog gain-reduction needle meter, OUT meter; preset browser (PREV / PRESET / NEXT / FAV),
         LOAD SAVE DELETE A/B COPY, current preset + status lines, BYPASS
  right: 72 px knobs THRESHOLD MAKEUP ATTACK RELEASE RATIO / SC FILTER MIX INPUT OUTPUT + ANALOG switch, Q-Link legend

MPC rules (proven on an MPC X by the other plugins here): filmstrip frames are square (image width = frame height),
at most 72 px with 128 frames (9216 px strips); larger displays are split into square tiles that all follow the same
parameter. numFrames = last frame index. Skin folder = "<vendor> - VST - <plugin name>".
Parameter indices come from the built plugin (GG_ParamKey), so build/native/gglue.so must exist (make native).
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
VERSION = "1.0.0.0"
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]

def font(size, name="DejaVuSans-Bold.ttf"):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p): return ImageFont.truetype(p, int(size))
    return ImageFont.load_default()
def rgba(c, a=255): return (c[0], c[1], c[2], a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def hexcol(c, a=255): return "%02x%02x%02x%02x" % (a, c[0], c[1], c[2])
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)

# palette (same as the desktop GUI)
PLATE_T, PLATE_B = (44, 45, 49), (27, 28, 31)
PRINT, DIM, ACCENT, RED = (233, 233, 236), (154, 156, 163), (226, 160, 58), (224, 65, 47)
FACE, INK = (241, 234, 216), (29, 29, 31)

# ---------- parameter indices from the plugin ----------
def load_keys():
    lib = ctypes.CDLL(LIB)
    lib.GG_ParamKey.restype = ctypes.c_char_p; lib.GG_ParamKey.argtypes = [ctypes.c_int]; lib.GG_ParamCount.restype = ctypes.c_int
    return {lib.GG_ParamKey(i).decode(): i for i in range(lib.GG_ParamCount())}
P = load_keys()

# ---------- layout ----------
MX, MY, MTILE, MCOLS, MROWS = 84, 84, 72, 6, 2          # GR meter: 432 x 144 in 72 px tiles
MW, MH = MTILE * MCOLS, MTILE * MROWS
LTILE, LSEG = 24, 12                                     # LED meters: 24 x 144 in 24 px tiles, 12 segments of 4 dB
IN_X, OUT_X, LY = 44, MX + MW + 16, MY
KX0, KY = 604, (92, 292)                                  # knob grid
KW, KH, KS = 132, 160, 72                                 # knob cell, knob size
KNOBS = [["threshold", "makeup", "attack", "release", "ratio"], ["scfilter", "mix", "input", "output", None]]
CAPTION = {"threshold": "THRESHOLD", "makeup": "MAKEUP", "attack": "ATTACK", "release": "RELEASE", "ratio": "RATIO",
           "scfilter": "SC FILTER", "mix": "MIX", "input": "INPUT", "output": "OUTPUT", "analog": "ANALOG", "bypass": "BYPASS"}
SCALE = {"threshold": ["-30", "+10"], "makeup": ["0", "+24"], "attack": [".1", ".3", "1", "3", "10", "30"],
         "release": [".1", ".3", ".6", "1.2", "A"], "ratio": ["2", "4", "10"], "scfilter": ["OFF", "30", "60", "90", "120", "150", "200"],
         "mix": ["0", "100"], "input": ["-24", "+24"], "output": ["-24", "+24"]}
STEPS = {"attack": 6, "release": 5, "ratio": 3, "scfilter": 7}
ANG0, ANG1 = -135.0, 135.0
def ang_xy(cx, cy, r, deg):
    a = math.radians(deg - 90); return cx + r * math.cos(a), cy + r * math.sin(a)
def knob_centre(row, col): return KX0 + col * KW + KW / 2, KY[row] + 24 + KS / 2

# ---------- knob filmstrip ----------
def knob_strip():
    s = KS * SS; c = s / 2; R = s * 0.46
    base = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sh = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([c - R, c - R + s * 0.05, c + R, c + R + s * 0.05], fill=(0, 0, 0, 150))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(s * 0.03)))
    d = ImageDraw.Draw(base)
    for yy in range(int(c - R), int(c + R) + 1):                          # skirt: vertical gradient
        k = (yy - (c - R)) / (2 * R); dx = math.sqrt(max(0.0, R * R - (yy - c) ** 2))
        d.line([c - dx, yy, c + dx, yy], fill=rgba(mix((60, 61, 66), (16, 17, 19), k)))
    cap = Image.new("RGBA", (s, s), (0, 0, 0, 0)); cd = ImageDraw.Draw(cap); rc = R * 0.78
    for i in range(60, 0, -1):                                            # cap: radial highlight from top-left
        t = i / 60; col = mix((30, 31, 35), (96, 98, 106), (1 - t) ** 1.6)
        r = rc * t; ox, oy = c - rc * 0.28 * (1 - t), c - rc * 0.36 * (1 - t)
        cd.ellipse([ox - r, oy - r, ox + r, oy + r], fill=rgba(col))
    mask = Image.new("L", (s, s), 0); ImageDraw.Draw(mask).ellipse([c - rc, c - rc, c + rc, c + rc], fill=255)
    capm = Image.new("RGBA", (s, s), (0, 0, 0, 0)); capm.paste(cap, (0, 0), mask)
    strip = Image.new("RGBA", (KS, KS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        im = base.copy(); d = ImageDraw.Draw(im)
        deg = ANG0 + (ANG1 - ANG0) * f / NUMFRAMES
        for i in range(48):                                               # knurling turns with the knob
            a = deg + i * 7.5; x0, y0 = ang_xy(c, c, R * 0.85, a); x1, y1 = ang_xy(c, c, R * 0.99, a)
            d.line([x0, y0, x1, y1], fill=(0, 0, 0, 140), width=SS)
        im.alpha_composite(capm); d = ImageDraw.Draw(im)
        d.ellipse([c - rc, c - rc, c + rc, c + rc], outline=(255, 255, 255, 28), width=SS)
        x0, y0 = ang_xy(c, c, R * 0.42, deg); x1, y1 = ang_xy(c, c, R * 0.97, deg)
        d.line([x0, y0, x1, y1], fill=rgba(PRINT), width=int(SS * 3.2))
        strip.paste(im.resize((KS, KS), Image.LANCZOS), (0, f * KS))
    return strip

# ---------- GR needle meter (GrScale of GUI/NeedleBallistics.h) ----------
GR_DB = [0, 1, 2, 3, 5, 7, 10, 15, 20]
GR_POS = [1.0, 0.885, 0.775, 0.675, 0.53, 0.415, 0.285, 0.125, 0.0]
def gr_pos(db):
    if db <= 0: return 1.0
    if db >= 20: return 0.0
    for i in range(1, len(GR_DB)):
        if db <= GR_DB[i]:
            t = (db - GR_DB[i - 1]) / (GR_DB[i] - GR_DB[i - 1]); return GR_POS[i - 1] + t * (GR_POS[i] - GR_POS[i - 1])
ARC0, ARC1 = -0.86, 0.86
def meter_geometry(s):
    inner = (8 * s, 8 * s, MW * s - 8 * s, MH * s - 8 * s)
    ih = inner[3] - inner[1]
    pivot = ((inner[0] + inner[2]) / 2, inner[3] + ih * 0.30)
    return inner, ih, pivot
def arc_pt(pivot, r, pos):
    a = ARC0 + pos * (ARC1 - ARC0); return pivot[0] + r * math.sin(a), pivot[1] - r * math.cos(a)
def meter_face():
    s = 3; im = Image.new("RGBA", (MW * s, MH * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, MW * s - 1, MH * s - 1], radius=9 * s, fill=rgba((22, 23, 26)))
    inner, ih, pivot = meter_geometry(s)
    for yy in range(int(inner[1]), int(inner[3])):                      # warm dial, slightly darker at the bottom
        k = (yy - inner[1]) / ih; d.line([inner[0], yy, inner[2], yy], fill=rgba(mix(FACE, mix(FACE, (0, 0, 0), 0.18), k)))
    rs = ih * 1.02
    pts = [arc_pt(pivot, rs, i / 200) for i in range(201)]; d.line(pts, fill=rgba(INK), width=2 * s)
    red = [arc_pt(pivot, rs - 3 * s, i / 200 * gr_pos(10)) for i in range(201)]; d.line(red, fill=rgba(RED), width=4 * s)
    for db in (4, 6, 8, 9, 12.5, 17.5):
        p = gr_pos(db); d.line([arc_pt(pivot, rs, p), arc_pt(pivot, rs - 7 * s, p)], fill=rgba(INK), width=s)
    f = font(13 * s)
    for db, p in zip(GR_DB, GR_POS):
        d.line([arc_pt(pivot, rs + s, p), arc_pt(pivot, rs - 12 * s, p)], fill=rgba(INK), width=2 * s)
        x, y = arc_pt(pivot, rs + 13 * s, p); text_c(d, x, y, str(db), f, rgba(INK))
    text_c(d, MW * s / 2, inner[1] + ih * 0.60, "GAIN REDUCTION  dB", font(11 * s), rgba(INK))
    text_c(d, MW * s / 2, inner[1] + ih * 0.76, "G-GLUE", font(9 * s, "DejaVuSans.ttf"), rgba(mix(INK, FACE, 0.45)))
    return im, s
def meter_tiles():
    face, s = meter_face()
    inner, ih, pivot = meter_geometry(s)
    tiles = [Image.new("RGBA", (MTILE, MTILE * FRAMES), (0, 0, 0, 0)) for _ in range(MCOLS * MROWS)]
    for f in range(FRAMES):
        im = face.copy(); d = ImageDraw.Draw(im)
        pos = 1.0 - f / NUMFRAMES                                         # plugin value = 1 - GrScale::position
        tip = arc_pt(pivot, ih * 1.08, pos)
        d.line([(pivot[0] + 3 * s, pivot[1] + 2 * s), (tip[0] + 3 * s, tip[1] + 2 * s)], fill=(0, 0, 0, 50), width=3 * s)
        d.line([pivot, tip], fill=rgba((20, 20, 20)), width=2 * s)
        mid = (pivot[0] + (tip[0] - pivot[0]) * 0.88, pivot[1] + (tip[1] - pivot[1]) * 0.88)
        d.line([mid, tip], fill=rgba(RED), width=2 * s)
        # glass reflection
        gl = Image.new("RGBA", im.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
        for yy in range(int(inner[1]), int(inner[1] + ih * 0.42)):
            a = int(40 * (1 - (yy - inner[1]) / (ih * 0.42))); gd.line([inner[0], yy, inner[2], yy], fill=(255, 255, 255, a))
        im.alpha_composite(gl)
        # pivot cover strip along the bottom of the window
        ImageDraw.Draw(im).rectangle([inner[0], inner[3] - ih * 0.13, inner[2], inner[3]], fill=rgba((26, 26, 28)))
        small = im.resize((MW, MH), Image.LANCZOS)
        for r in range(MROWS):
            for c in range(MCOLS):
                tiles[r * MCOLS + c].paste(small.crop((c * MTILE, r * MTILE, (c + 1) * MTILE, (r + 1) * MTILE)), (0, f * MTILE))
    return tiles

# ---------- LED level meters ----------
def led_tiles():
    h = LTILE * 6
    tiles = [Image.new("RGBA", (LTILE, LTILE * FRAMES), (0, 0, 0, 0)) for _ in range(6)]
    seg_h = h / LSEG
    for f in range(FRAMES):
        db = -48 + 48 * f / NUMFRAMES
        im = Image.new("RGBA", (LTILE, h), rgba((12, 12, 14))); d = ImageDraw.Draw(im)
        for i in range(LSEG):
            top = -48 + 4 * (i + 1)
            col = (60, 207, 122) if top <= -12 else (ACCENT if top <= -4 else RED)
            lit = db >= top - 4 + 0.01
            y1 = h - i * seg_h - 2; y0 = y1 - seg_h + 3
            d.rectangle([4, y0, LTILE - 5, y1], fill=rgba(col if lit else mix(col, (0, 0, 0), 0.82)))
        for k in range(6): tiles[k].paste(im.crop((0, k * LTILE, LTILE, (k + 1) * LTILE)), (0, f * LTILE))
    return tiles

# ---------- buttons ----------
def button(lab, on, w, h, led=None, fs=15):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    off = s if on else 0
    d.rounded_rectangle([0, 2 * s, w * s - 1, h * s - 1], radius=6 * s, fill=(0, 0, 0, 140))
    face = Image.new("RGBA", im.size, (0, 0, 0, 0)); fd = ImageDraw.Draw(face)
    for yy in range(0, (h - 3) * s):
        k = yy / ((h - 3) * s); fd.line([0, yy + off, w * s, yy + off], fill=rgba(mix((60, 62, 67) if not on else (44, 45, 49), (30, 31, 35), k)))
    m = Image.new("L", im.size, 0); ImageDraw.Draw(m).rounded_rectangle([0, off, w * s - 1, (h - 3) * s + off], radius=6 * s, fill=255)
    im.paste(face, (0, 0), m); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, off, w * s - 1, (h - 3) * s + off], radius=6 * s, outline=(255, 255, 255, 40), width=s)
    tx = w * s / 2
    if led:
        r = 4.5 * s; lx, ly = 16 * s, (h - 3) * s / 2 + off
        if on:
            g = Image.new("RGBA", im.size, (0, 0, 0, 0)); ImageDraw.Draw(g).ellipse([lx - 3 * r, ly - 3 * r, lx + 3 * r, ly + 3 * r], fill=rgba(led, 130))
            im.alpha_composite(g.filter(ImageFilter.GaussianBlur(3 * s))); d = ImageDraw.Draw(im)
        d.ellipse([lx - r, ly - r, lx + r, ly + r], fill=rgba(led if on else mix(led, (0, 0, 0), 0.75)), outline=(0, 0, 0, 160), width=s)
        tx += 8 * s
    text_c(d, tx, (h - 3) * s / 2 + off, lab, font(fs * s), rgba(PRINT))
    return im.resize((w, h), Image.LANCZOS)
def icon_button(kind, w, h):
    im = button("", False, w, h); s = 4; big = im.resize((w * s, h * s), Image.LANCZOS); d = ImageDraw.Draw(big)
    cx, cy, r = w * s / 2, (h - 3) * s / 2, min(w, h) * s * 0.2
    if kind == "prev": d.polygon([(cx + r * 0.8, cy - r), (cx + r * 0.8, cy + r), (cx - r * 0.9, cy)], fill=rgba(PRINT))
    elif kind == "next": d.polygon([(cx - r * 0.8, cy - r), (cx - r * 0.8, cy + r), (cx + r * 0.9, cy)], fill=rgba(PRINT))
    else:
        pts = [(cx + (r * 1.25 if i % 2 == 0 else r * 0.5) * math.cos(math.radians(-90 + i * 36)),
                cy + (r * 1.25 if i % 2 == 0 else r * 0.5) * math.sin(math.radians(-90 + i * 36))) for i in range(10)]
        d.polygon(pts, fill=rgba(ACCENT))
    return big.resize((w, h), Image.LANCZOS)

# ---------- background ----------
def screw(d, x, y, r, ang):
    d.ellipse([x - r - 1, y - r, x + r + 1, y + r + 2], fill=(0, 0, 0))
    for i in range(int(r), 0, -1):
        t = i / r; d.ellipse([x - i, y - i, x + i, y + i], fill=mix((205, 206, 210), (92, 94, 101), t))
    for a in (ang, ang + 90):
        dx, dy = math.cos(math.radians(a)) * r * 0.75, math.sin(math.radians(a)) * r * 0.75
        d.line([x - dx, y - dy, x + dx, y + dy], fill=(38, 39, 43), width=3)
def background():
    random.seed(7)
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    for y in range(H): d.line([0, y, W, y], fill=mix(PLATE_T, PLATE_B, y / H))
    tex = Image.new("RGBA", (W, H), (0, 0, 0, 0)); td = ImageDraw.Draw(tex)
    for _ in range(W * H // 90):
        y = random.randrange(H); x = random.randrange(W); ln = random.randint(20, 180)
        td.line([x, y, min(W, x + ln), y], fill=(255, 255, 255, random.randint(4, 11)) if random.random() < 0.5 else (0, 0, 0, random.randint(5, 13)))
    im.paste(tex, (0, 0), tex); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W - 1, H - 1], outline=(0, 0, 0)); d.rectangle([1, 1, W - 2, H - 2], outline=(70, 72, 78))
    for x, y in ((16, 16), (W - 16, 16), (16, H - 16), (W - 16, H - 16)): screw(d, x, y, 8, random.randint(0, 90))
    # title
    title = "G - G L U E"; d.text((44, 16), title, font=font(30), fill=PRINT)
    d.text((44 + d.textlength(title, font=font(30)) + 18, 28), "B U S   C O M P R E S S O R", font=font(13), fill=DIM)
    for x0, x1 in ((24, 572), (590, W - 24)):
        d.line([x0, 66, x1, 66], fill=(10, 10, 12)); d.line([x0, 67, x1, 67], fill=(70, 72, 78))
    # meter wells and captions
    for x in (IN_X, OUT_X):
        d.rounded_rectangle([x - 3, LY - 3, x + LTILE + 2, LY + 6 * LTILE + 2], radius=4, fill=(8, 8, 10))
    text_c(d, IN_X + LTILE / 2, LY + 6 * LTILE + 14, "IN", font(11), DIM)
    text_c(d, OUT_X + LTILE / 2, LY + 6 * LTILE + 14, "OUT", font(11), DIM)
    # preset section panel
    d.rounded_rectangle([30, 270, 572, 474], radius=10, fill=(22, 23, 26), outline=(64, 66, 72))
    d.text((44, 256), "P R E S E T", font=font(11), fill=DIM)
    # knob scales and labels
    for row, keys in enumerate(KNOBS):
        for col, key in enumerate(keys):
            if not key: continue
            cx, cy = knob_centre(row, col)
            steps = STEPS.get(key, 11)
            for i in range(steps):
                a = ANG0 + (ANG1 - ANG0) * i / (steps - 1); major = key in STEPS or i % 5 == 0
                x0, y0 = ang_xy(cx, cy, KS / 2 + 3, a); x1, y1 = ang_xy(cx, cy, KS / 2 + (9 if major else 6), a)
                d.line([x0, y0, x1, y1], fill=PRINT if major else DIM, width=2 if major else 1)
            labs = SCALE[key]
            for i, t in enumerate(labs):
                a = ANG0 + (ANG1 - ANG0) * (i / (len(labs) - 1) if len(labs) > 1 else 0)
                x, y = ang_xy(cx, cy, KS / 2 + 20, a); text_c(d, x, y, t, font(11), DIM)
    # value windows under the knobs
    for row, keys in enumerate(KNOBS):
        for col, key in enumerate(keys):
            if key:
                x = KX0 + col * KW; y = KY[row]
                d.rounded_rectangle([x + 18, y + 128, x + KW - 18, y + 154], radius=5, fill=(10, 10, 12), outline=(60, 62, 68))
    # Q-Link legend
    d.rounded_rectangle([604, 480, W - 30, 592], radius=10, outline=(64, 66, 72))
    d.text((620, 468), "Q - L I N K S", font=font(11), fill=DIM)
    legend = [("1", "THRESHOLD"), ("2", "MAKEUP"), ("3", "ATTACK"), ("4", "RELEASE"), ("5", "RATIO"), ("6", "SC FILTER"),
              ("7", "MIX"), ("8", "INPUT"), ("9", "OUTPUT"), ("10", "ANALOG"), ("11", "BYPASS"), ("12", "PRESET")]
    for i, (n, t) in enumerate(legend):
        x = 624 + (i % 4) * 158; y = 496 + (i // 4) * 30
        d.text((x, y), n, font=font(13), fill=ACCENT); d.text((x + 28, y), t, font=font(13), fill=PRINT)
    d.text((44, 596), "Unofficial MPC build: VST2 for MPC OS (Gen1). Presets: /sdcard/G-Glue/Presets",
           font=font(11, "DejaVuSans.ttf"), fill=DIM)
    return im

# ---------- TUI.json helpers (GlueBus schema) ----------
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
def label(kind, h, colour, b, case="Original"):
    return comp(kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": hexcol(ACCENT), "backgroundInset": 1.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE = lambda: [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
def strip(img, x, y, sq, drag="Vertical"):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": drag, "handleName": "Data"}, bounds((x, y, sq, sq)))
def btn_part(on, off, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1,
                "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))

PLACED = []      # (kind, param, x, y, w, h, extra) for the preview
def place(comps, name, key, param, x, y, w, h, kind, extra=None, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((kind, param, x, y, w, h, extra))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    defs, comps = [], []
    save(background(), "gg_bg.png")
    comps.append(comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "gg_bg.png"}, bounds((0, 0, W, H))))

    # GR meter tiles + LED meters (read-only, follow one parameter each)
    for k, t in enumerate(meter_tiles()):
        save(t, f"gg_gr{k}.png")
        defs.append({"key": f"ggGr{k}", "value": definition([], [strip(f"gg_gr{k}.png", 0, 0, MTILE)], ignore=True)})
        place(comps, f"GR {k}", f"ggGr{k}", P["gr"], MX + (k % MCOLS) * MTILE, MY + (k // MCOLS) * MTILE, MTILE, MTILE, "tile", f"gg_gr{k}.png")
    for k, t in enumerate(led_tiles()):
        save(t, f"gg_led{k}.png")
        defs.append({"key": f"ggLed{k}", "value": definition([], [strip(f"gg_led{k}.png", 0, 0, LTILE)], ignore=True)})
        place(comps, f"In {k}", f"ggLed{k}", P["inmeter"], IN_X, LY + k * LTILE, LTILE, LTILE, "tile", f"gg_led{k}.png")
        place(comps, f"Out {k}", f"ggLed{k}", P["outmeter"], OUT_X, LY + k * LTILE, LTILE, LTILE, "tile", f"gg_led{k}.png")

    # knobs: filmstrip + name + value
    save(knob_strip(), "gg_knob.png")
    defs.append({"key": "ggKnob", "value": definition(CTRL(), [
        focus((0, 0, KW, KH)), strip("gg_knob.png", (KW - KS) / 2, 24, KS),
        label("Name", 15, hexcol(PRINT), (0, 104, KW, 20)),
        label("Value", 17, hexcol(ACCENT), (18, 128, KW - 36, 26))])})
    for row, keys in enumerate(KNOBS):
        for col, key in enumerate(keys):
            if key: place(comps, CAPTION[key], "ggKnob", P[key], KX0 + col * KW, KY[row], KW, KH, "knob", key, touch=True)

    # switches (ANALOG, BYPASS): LED buttons
    for key, colour, w, h in (("analog", ACCENT, 112, 46), ("bypass", RED, 200, 52)):
        save(button(CAPTION[key], True, w, h, colour), f"gg_{key}_on.png"); save(button(CAPTION[key], False, w, h, colour), f"gg_{key}_off.png")
        defs.append({"key": f"ggSw_{key}", "value": definition(TOGGLE(), [btn_part(f"gg_{key}_on.png", f"gg_{key}_off.png", w, h), focus((0, 0, w, h))])})
    place(comps, "ANALOG", "ggSw_analog", P["analog"], KX0 + 4 * KW + (KW - 112) / 2, KY[1] + 48, 112, 46, "switch", "analog", touch=True)
    place(comps, "BYPASS", "ggSw_bypass", P["bypass"], 201, 500, 200, 52, "switch", "bypass", touch=True)

    # momentary buttons: same picture on and off (each tap flips the value; the plugin treats any change as a tap)
    taps = {"prev": (48, 44, None), "next": (48, 44, None), "fav": (48, 44, None),
            "load": (96, 44, "LOAD"), "save": (96, 44, "SAVE"), "delete": (96, 44, "DELETE"), "ab": (96, 44, "A / B"), "copy": (96, 44, "COPY")}
    for key, (w, h, txt) in taps.items():
        im = icon_button(key, w, h) if txt is None else button(txt, False, w, h)
        save(im, f"gg_{key}.png")
        defs.append({"key": f"ggTap_{key}", "value": definition(TOGGLE(), [btn_part(f"gg_{key}.png", f"gg_{key}.png", w, h), focus((0, 0, w, h))])})
    place(comps, "PREV", "ggTap_prev", P["prev"], 44, 284, 48, 44, "tap", "prev", touch=True)
    place(comps, "NEXT", "ggTap_next", P["next"], 450, 284, 48, 44, "tap", "next", touch=True)
    place(comps, "FAV", "ggTap_fav", P["fav"], 508, 284, 48, 44, "tap", "fav", touch=True)
    for i, key in enumerate(("load", "save", "delete", "ab", "copy")):
        place(comps, key.upper(), f"ggTap_{key}", P[key], 44 + i * 104, 340, 96, 44, "tap", key, touch=True)
    # preset browser box: transparent drag area + the selected preset's name
    drag = Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)); save(drag, "gg_drag.png")
    defs.append({"key": "ggBrowse", "value": definition(CTRL(), [
        comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "gg_drag.png", "numFrames": NUMFRAMES, "invert": False,
             "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, 346, 44))),
        label("Value", 19, hexcol(PRINT), (0, 0, 346, 44)), focus((0, 0, 346, 44))])})
    place(comps, "PRESET", "ggBrowse", P["browse"], 98, 284, 346, 44, "box", None, touch=True)
    # readouts
    defs.append({"key": "ggPreset", "value": definition([], [label("Value", 22, hexcol(ACCENT), (0, 0, 528, 34))], ignore=True)})
    defs.append({"key": "ggStatus", "value": definition([], [label("Value", 15, hexcol(DIM), (0, 0, 528, 28))], ignore=True)})
    place(comps, "Current Preset", "ggPreset", P["preset"], 38, 394, 528, 34, "ro", (22, ACCENT))
    place(comps, "Status", "ggStatus", P["status"], 38, 432, 528, 28, "ro", (15, DIM))

    defs.append({"key": "GG|Main", "value": definition([], comps, "ff1b1c1f")})
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
    tallest = max(s[1] for s in sizes)
    print(f"skin: {len(sizes)} images, tallest {tallest} px, written to {SKIN_DIR}")

# ---------- preview: the real plugin (build/native/gglue.so) after a few seconds of drum-like audio ----------
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
    D(2, val=6)                                                    # program 7: Drum Punch
    t = 0
    for _ in range(int(48000 * 2.2 / n)):
        for i in range(n):
            s = ((t + i) % 24000) / 48000.0
            x = 0.9 * math.exp(-s * 18) * math.sin(2 * math.pi * 55 * s) + 0.25 * math.sin(2 * math.pi * 330 * (t + i) / 48000.0)
            IL[i] = IR[i] = x
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
    im = Image.open(os.path.join(OUT, "gg_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    knob = Image.open(os.path.join(OUT, "gg_knob.png")).convert("RGBA")
    for kind, param, x, y, w, h, extra in PLACED:
        v = vals[param]; fr = max(0, min(NUMFRAMES, round(v * NUMFRAMES)))
        if kind == "tile":
            st = Image.open(os.path.join(OUT, extra)).convert("RGBA"); im.alpha_composite(st.crop((0, fr * w, w, fr * w + h)), (int(x), int(y)))
        elif kind == "knob":
            im.alpha_composite(knob.crop((0, fr * KS, KS, fr * KS + KS)), (int(x + (KW - KS) / 2), int(y + 24)))
            d = ImageDraw.Draw(im)
            text_c(d, x + KW / 2, y + 114, CAPTION[extra], font(13), PRINT)
            text_c(d, x + KW / 2, y + 141, txt[param], font(14), ACCENT)
        elif kind == "switch":
            im.alpha_composite(Image.open(os.path.join(OUT, f"gg_{extra}_{'on' if v >= 0.5 else 'off'}.png")).convert("RGBA"), (int(x), int(y)))
        elif kind == "tap":
            im.alpha_composite(Image.open(os.path.join(OUT, f"gg_{extra}.png")).convert("RGBA"), (int(x), int(y)))
        elif kind == "box":
            d = ImageDraw.Draw(im); d.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(12, 12, 14), outline=(70, 72, 78))
            text_c(d, x + w / 2, y + h / 2, txt[param], font(16), PRINT)
        elif kind == "ro":
            fs, col = extra; d = ImageDraw.Draw(im); text_c(d, x + w / 2, y + h / 2, txt[param], font(fs * 0.8), col)
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "skin-preview.png"); im.convert("RGB").save(path); print("preview:", path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
