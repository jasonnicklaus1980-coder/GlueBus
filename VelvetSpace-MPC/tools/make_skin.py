#!/usr/bin/env python3
"""Generate the Velvet Space MPC screen skin (GlueBus / RadioReady skin pipeline).

A night-sky panel: gold script title, preset browser (prev / preset / next), algorithm selector with a one-line
description, input and reverb meters, and two rows of big knobs:
  Mix, Pre-Delay, Size, Decay, Damping, Low Cut, Diffusion  /  Modulation, Width, Shimmer, Color, Ducking, Output + FREEZE
MPC rules (proven on an MPC X): square filmstrip frames, numFrames = last frame index, LED parts one solid colour per
frame, skin folder "<manufacturer> - VST - <plugin>".
Output: mpc/skin/RadioReady Audio - VST - Velvet Space/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Usage: python3 tools/make_skin.py [preview-dir]
"""
import ctypes, json, math, os, random, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "RadioReady Audio - VST - Velvet Space")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
VERSION = "1.0.2.0"
TITLE = "Velvet Space"
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

SKY_TOP, SKY_BOT = (8, 12, 30), (34, 42, 84)
PRINT, PRINT_DIM = (226, 230, 240), (150, 160, 190)
GOLD, GOLD_DARK = (230, 184, 76), (150, 108, 30)
ICE = (150, 210, 255)
PANEL = (16, 20, 40)
LINE = (4, 6, 14)
GREEN, AMBER, RED = (70, 200, 120), (236, 196, 60), (236, 70, 56)

(P_PRESET, P_PREV, P_NEXT, P_ALGO, P_MIX, P_PRE, P_SIZE, P_DECAY, P_DAMP, P_LOWCUT, P_DIFF, P_MOD, P_WIDTH,
 P_SHIMMER, P_COLOR, P_FREEZE, P_DUCK, P_OUT, P_INMETER, P_WETMETER, P_INFO, P_COUNT) = range(22)
KN = 92
ROW1 = [(P_MIX, "MIX"), (P_PRE, "PRE-DELAY"), (P_SIZE, "SIZE"), (P_DECAY, "DECAY"), (P_DAMP, "DAMPING"), (P_LOWCUT, "LOW CUT"), (P_DIFF, "DIFFUSION")]
ROW2 = [(P_MOD, "MODULATION"), (P_WIDTH, "WIDTH"), (P_SHIMMER, "SHIMMER"), (P_COLOR, "COLOR"), (P_DUCK, "DUCKING"), (P_OUT, "OUTPUT")]
KX0, KDX = 44, 162
KY1, KY2 = 196, 408
NSEG = 18

# title letters: polished blue (light top, deep band, bright bottom)
TITLE_STOPS = [(200, 235, 255), (110, 190, 255), (30, 90, 200), (90, 170, 255), (60, 130, 235)]
def script_font(size):
    for p in SCRIPT_FONTS:
        if os.path.exists(p): return ImageFont.truetype(p, size), True
    return font(int(size * 0.62), "DejaVuSerif-Bold.ttf"), False
def title_text(im, cx, cy, text, size):
    f, script = script_font(size)
    b = ImageDraw.Draw(im).textbbox((0, 0), text, font=f); w, h = b[2] - b[0], b[3] - b[1]
    pad = 14; mask = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
    ImageDraw.Draw(mask).text((pad - b[0], pad - b[1]), text, font=f, fill=255)
    grad = Image.new("RGB", mask.size); gd = ImageDraw.Draw(grad)
    for y in range(mask.size[1]):
        k = y / max(1, mask.size[1] - 1) * (len(TITLE_STOPS) - 1); i = min(len(TITLE_STOPS) - 2, int(k)); u = k - i
        gd.line([0, y, mask.size[0], y], fill=tuple(int(TITLE_STOPS[i][j] + (TITLE_STOPS[i + 1][j] - TITLE_STOPS[i][j]) * u) for j in range(3)))
    x0, y0 = int(cx - mask.size[0] / 2), int(cy - mask.size[1] / 2)
    im.paste(Image.new("RGB", mask.size, (0, 0, 0)), (x0 + 2, y0 + 3), mask.filter(ImageFilter.GaussianBlur(2.5)))
    im.paste(Image.new("RGB", mask.size, (30, 80, 170)), (x0, y0), mask.filter(ImageFilter.GaussianBlur(6)).point(lambda v: v // 2))
    im.paste(grad, (x0, y0), mask)

def knob_strip(size):
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        t = f / (FRAMES - 1); s = SS; S = size * s
        im = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        c = S / 2; r = S * 0.45; a0, a1 = 135, 405; a = a0 + (a1 - a0) * t
        d.arc([c - r, c - r, c + r, c + r], a0, a1, fill=rgba((40, 48, 80)), width=int(S * 0.06))
        if t > 0.004: d.arc([c - r, c - r, c + r, c + r], a0, a, fill=rgba(ICE), width=int(S * 0.06))
        rb = S * 0.35
        d.ellipse([c - rb, c - rb, c + rb, c + rb], fill=rgba((30, 36, 62)), outline=rgba((6, 8, 18)), width=2 * s)
        d.ellipse([c - rb * 0.85, c - rb * 0.85, c + rb * 0.85, c + rb * 0.85], fill=rgba((44, 52, 86)))
        ang = math.radians(a)
        d.line([c + math.cos(ang) * S * 0.08, c + math.sin(ang) * S * 0.08, c + math.cos(ang) * S * 0.31, c + math.sin(ang) * S * 0.31],
               fill=rgba(GOLD), width=int(S * 0.045))
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return strip
def solid_strip(sq, on, off, lit):
    strip = Image.new("RGBA", (sq, sq * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    for f in range(FRAMES): d.rectangle([0, f * sq, sq - 1, f * sq + sq - 1], fill=rgba(on if lit(f / (FRAMES - 1)) else off))
    return strip
def pill(text, on, col, w, h, size=12):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=8 * s, fill=rgba(col if on else (30, 36, 62)), outline=rgba((6, 8, 18)), width=2 * s)
    d.line([6 * s, 2 * s, w * s - 6 * s, 2 * s], fill=rgba((255, 255, 255), 50), width=s)
    if text: text_c(d, w * s / 2, h * s / 2, text, font(size * s), rgba((10, 14, 30) if on else col))
    return im.resize((w, h), Image.LANCZOS)

NOREMAP = {"version": 1, "map": []}
def bounds(b, focus="No", show="Show", visible="Always"):
    return {"version": 2, "acceptsHWFocus": focus, "showWhenDataModelInvalid": show, "whenVisible": visible,
            "boundsType": "Absolute", "bounds": " ".join(str(int(round(v))) for v in b), "additionalInvalidatingHandles": []}
def comp(name, typ, data, b, remap=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data}, "handle remapping": remap or NOREMAP, "bounds": b}
def bindp(i): return {"version": 1, "map": [{"key": "Data", "value": f"Parameter {i}"}]}
def action(on, handler, extra=""):
    return {"version": 2, "onAction": on, "handler": handler, "handleName": "" if handler == "Show Overlay" else "Data", "additionalData": extra, "handle remapping": NOREMAP}
def bgdata(col="0"): return {"version": 1, "focussed": {"version": 1, "colour": col, "image": ""}, "unfocussed": {"version": 1, "colour": col, "image": ""}}
def definition(actions, parts, bgcol="0", ignore=False):
    return {"version": 4, "actions": actions, "backgroundData": bgdata(bgcol), "ignoreMousePresses": ignore,
            "disableCoarseDataWheel": False, "repeats": 1, "hideQLinkBounds": True, "componentsData": parts}
def label(h, colour, b, case="Original"):
    return comp("Value", "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": case}, "type": "Value", "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": hexcol(GOLD), "backgroundInset": 1.0, "outlineThickness": 2.0}, bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE = lambda: [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
def strip_part(img, bw, bh, sq):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((bw - sq) / 2, (bh - sq) / 2, sq, sq)))
def button_part(on, off, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))

PLACED = []; BOXES = []
def place(comps, name, key, param, x, y, w, h, kind, extra=None, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"}, bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))
    PLACED.append((kind, param, x, y, w, h, extra))

def background():
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    for y in range(H): d.line([0, y, W, y], fill=mix(SKY_TOP, SKY_BOT, (y / H) ** 1.4))
    rnd = random.Random(7)
    for _ in range(260):                                                   # stars
        x, y = rnd.uniform(0, W), rnd.uniform(TITLE_H, H); r = rnd.choice((0.6, 0.8, 1.0, 1.4)); b = rnd.randint(110, 240)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(b, b, min(255, b + 15)))
    glow = Image.new("L", (W, H), 0); gd = ImageDraw.Draw(glow)               # soft "high sky" aurora
    for i in range(6): gd.ellipse([200 + i * 90, 250 - i * 12, 1100 - i * 40, 480 + i * 10], outline=40 - i * 5, width=30)
    im.paste(Image.new("RGB", (W, H), (90, 130, 200)), (0, 0), glow.filter(ImageFilter.GaussianBlur(40)))
    d = ImageDraw.Draw(im)
    for y in range(TITLE_H): d.line([0, y, W, y], fill=mix((6, 8, 18), (16, 20, 38), y / TITLE_H))
    d.line([0, TITLE_H, W, TITLE_H], fill=(60, 120, 210)); d.line([0, TITLE_H + 1, W, TITLE_H + 1], fill=(20, 40, 90))
    title_text(im, W / 2, TITLE_H / 2 + 2, "   ".join(TITLE.split(" ")), 40)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([12, 60, W - 12, 146], radius=10, fill=PANEL, outline=LINE, width=2)
    text_c(d, 262, 72, "PRESET", font(9), PRINT_DIM); text_c(d, 648, 72, "ALGORITHM", font(9), PRINT_DIM)
    text_c(d, 1182, 72, "IN      VERB", font(9), PRINT_DIM)
    for (x, y, w, h) in BOXES: d.rounded_rectangle([x, y, x + w, y + h], radius=6, fill=(30, 36, 62), outline=(6, 8, 18), width=2)
    for row, ky in ((ROW1, KY1), (ROW2, KY2)):
        for i, (p, lab) in enumerate(row):
            cx = KX0 + i * KDX + KN / 2
            text_c(d, cx, ky - 16, lab, font(12), GOLD)
    return im

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    save(knob_strip(KN), "ds_knob.png"); save(Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)), "ds_drag.png")
    defs = []
    defs.append({"key": "dsKnob", "value": definition(CTRL(), [strip_part("ds_knob.png", KN, KN, KN), label(15, hexcol(PRINT), (-20, KN + 2, KN + 40, 22), case="Upper Case"), focus((0, 0, KN, KN + 24))])})
    for key, w, h, fs in (("dsPreset", 380, 46, 20), ("dsAlgo", 230, 46, 20)):
        drag = comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "ds_drag.png", "numFrames": NUMFRAMES, "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, w, h)))
        defs.append({"key": key, "value": definition(CTRL(), [drag, label(fs, hexcol(PRINT), (0, 0, w, h), case="Upper Case"), focus((0, 0, w, h))])})
    defs.append({"key": "dsInfo", "value": definition([], [label(14, hexcol(ICE), (0, 0, 300, 46))], ignore=True)})
    for k, (txt, w, h, col, fs) in {"prev": ("\u25C0", 46, 46, GOLD, 18), "next": ("\u25B6", 46, 46, GOLD, 18), "freeze": ("\u2744 FREEZE", 150, 70, ICE, 17)}.items():
        save(pill(txt, True, col, w, h, fs), f"ds_{k}_on.png"); save(pill(txt, False, col, w, h, fs), f"ds_{k}_off.png")
        defs.append({"key": f"dsBtn_{k}", "value": definition(TOGGLE(), [button_part(f"ds_{k}_on.png", f"ds_{k}_off.png", w, h), focus((0, 0, w, h))])})
    for k in range(NSEG):
        v0 = -60 + 66 * (k + 0.5) / NSEG; c = GREEN if v0 < -12 else (AMBER if v0 < 0 else RED)
        save(solid_strip(12, c, mix(c, (8, 10, 20), 0.85), lambda v, v0=v0: -60 + 66 * v >= v0), f"ds_led{k}.png")
        defs.append({"key": f"dsLed{k}", "value": definition([], [strip_part(f"ds_led{k}.png", 12, 2, 12)], ignore=True)})

    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "ds_bg.png"}, bounds((0, 0, W, H)))]
    place(comps, "Prev", "dsBtn_prev", P_PREV, 26, 80, 46, 46, "btn", "prev", True)
    place(comps, "Preset", "dsPreset", P_PRESET, 78, 80, 380, 46, "box", ("dsPreset", 20), True); BOXES.append((78, 80, 380, 46))
    place(comps, "Next", "dsBtn_next", P_NEXT, 464, 80, 46, 46, "btn", "next", True)
    place(comps, "Algorithm", "dsAlgo", P_ALGO, 533, 80, 230, 46, "box", ("dsAlgo", 20), True); BOXES.append((533, 80, 230, 46))
    place(comps, "Info", "dsInfo", P_INFO, 776, 80, 300, 46, "ro", ("dsInfo", 14))
    for k in range(NSEG):
        for c, p in enumerate((P_INMETER, P_WETMETER)):
            place(comps, f"Meter {c} {k}", f"dsLed{k}", p, 1160 + c * 34, 136 - (k + 1) * 3.3, 12, 2, "seg", (f"ds_led{k}.png", 12))
    for row, ky in ((ROW1, KY1), (ROW2, KY2)):
        for i, (p, lab) in enumerate(row):
            place(comps, lab.title(), "dsKnob", p, KX0 + i * KDX, ky, KN, KN + 24, "knob", None, True)
    place(comps, "Freeze", "dsBtn_freeze", P_FREEZE, KX0 + 6 * KDX - 29, KY2 + 10, 150, 70, "btn", "freeze", True)
    save(background(), "ds_bg.png")
    defs.append({"key": "DS|Main", "value": definition([], comps, "ff080c1e")})
    tabs = [{"version": 3, "tabName": "SPACE", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"], "componentName": "DS|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]
    tui = {"pageData": {"version": 1, "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
            "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"], "localComponentDefinitions": defs},
            "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)
    ql = [P_MIX, P_PRE, P_SIZE, P_DECAY, P_DAMP, P_LOWCUT, P_DIFF, P_ALGO, P_MOD, P_WIDTH, P_SHIMMER, P_COLOR, P_DUCK, P_OUT, P_FREEZE, P_PRESET]
    qmap = {f"Q-Link {i + 1}": p for i, p in enumerate(ql)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]}, "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write("<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>velvetspace.vst.velvetspace</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview from the real plugin (High Sky on noise bursts) ----------
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
def preview(outdir):
    lib = ctypes.CDLL(os.path.join(ROOT, "build", "native", "velvetspace.so"))
    cb = HOSTCB(lambda *a: 2400); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0); D(2, val=28)
    n = 512; L = (ctypes.c_float * n)(); R = (ctypes.c_float * n)(); io = (ctypes.POINTER(ctypes.c_float) * 2)(L, R)
    rnd = random.Random(1)
    for b in range(120):
        for i in range(n): L[i] = R[i] = (rnd.uniform(-0.3, 0.3) if b % 40 < 6 else 0.0)
        e.processReplacing(fx, io, io, n)
    vals = [e.getParameter(fx, i) for i in range(P_COUNT)]; txt = []
    for i in range(P_COUNT):
        buf = ctypes.create_string_buffer(128); D(7, idx=i, ptr=ctypes.cast(buf, ctypes.c_void_p)); txt.append(buf.value.decode())
    D(1)
    os.makedirs(outdir, exist_ok=True)
    img = lambda nm: Image.open(os.path.join(OUT, nm)).convert("RGBA")
    im = img("ds_bg.png"); d = ImageDraw.Draw(im); knob = img("ds_knob.png")
    def frame(strip, sq, v, w, h):
        fr = max(0, min(FRAMES - 1, round(v * NUMFRAMES))); cx0, cy0 = (sq - w) // 2, (sq - h) // 2
        return strip.crop((cx0, fr * sq + cy0, cx0 + w, fr * sq + cy0 + h))
    for (kind, p, x, y, w, h, extra) in PLACED:
        x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h)); v = vals[p]
        if kind == "knob":
            im.alpha_composite(frame(knob, KN, v, KN, KN), (x, y)); text_c(d, x + KN / 2, y + KN + 13, txt[p].upper(), font(12), PRINT)
        elif kind == "seg": im.alpha_composite(frame(img(extra[0]), extra[1], v, w, h), (x, y))
        elif kind == "btn": im.alpha_composite(img(f"ds_{extra}_{'on' if v >= 0.5 else 'off'}.png"), (x, y))
        elif kind == "box": text_c(d, x + w / 2, y + h / 2, txt[p].upper(), font(int(extra[1] * 0.75)), PRINT)
        elif kind == "ro": text_c(d, x + w / 2, y + h / 2, txt[p], font(11, "DejaVuSans.ttf"), ICE)
    im.convert("RGB").save(os.path.join(outdir, "skin-preview.png")); print("preview:", os.path.join(outdir, "skin-preview.png"))

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
