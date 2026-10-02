#!/usr/bin/env python3
"""Generate the screen skins (GlueBus / RadioReady TUI.json pipeline) and previews for both plugins.
Da Sample Lab, three tabs: SAMPLE (waveform, browse, start/end, BPM, key, pitch, time, zoom), CHOP (chop method, FIND,
slice edit incl. filter / envelope / mute / choke, export), PADS (pad mode, notes, trigger, envelope).
The waveform is 128 filmstrip bars driven by read-only parameters (frame = state * 32 + height; state 0 outside
start/end, 1 inside, 2 selected slice, 3 slice marker), with two transparent drag strips over it: the top half moves
the start marker, the bottom half the end marker (SAMPLE: start / end, CHOP: the selected slice's start / end).
Da Sample Lab Capture, one tab: record, arm, threshold, max length, time, meter.
Parameter indices come from the built plugins (SL_ParamKey / CA_ParamKey): run make native first.  Needs Pillow.
Usage: python3 tools/make_skin.py [preview-dir sample-workdir]"""
import ctypes, json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4
LIB = os.path.join(ROOT, "build", "native", "dasamplelab.so")
CAPLIB = os.path.join(ROOT, "build", "native", "dasamplelab_capture.so")
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
FOCUS_COL = "ffe8392f"
TOGGLES = ("slrev", "slloop", "slmute", "arm")

def load_keys(path, prefix):
    lib = ctypes.CDLL(path)
    kf, cf = getattr(lib, prefix + "_ParamKey"), getattr(lib, prefix + "_ParamCount")
    kf.restype = ctypes.c_char_p; kf.argtypes = [ctypes.c_int]; cf.restype = ctypes.c_int
    return {kf(i).decode(): i for i in range(cf())}

class Skin:
    def __init__(self, name, ident, title, lib, prefix, pre):
        self.name, self.ident, self.title, self.lib, self.pre = name, ident, title, lib, pre
        self.dir = os.path.join(ROOT, "mpc", "skin", f"RadioReady Audio - VST - {name}"); self.out = os.path.join(self.dir, "Plugin Skins")
        self.P = load_keys(lib, prefix); self.defs, self.images, self.pages = {}, {}, []
    def img(self, name, im): self.images[name] = im; return name
    def defn(self, key, value): self.defs.setdefault(key, value); return key
    def page(self, name): pg = Page(self, name); self.pages.append(pg); return pg

class Page:
    def __init__(self, skin, name): self.skin, self.name, self.comps, self.bg, self.q = skin, name, [], [], []
    def place(self, label, dkey, param, x, y, w, h, touch=True):
        self.comps.append(comp(label, dkey, {"version": 1, "handleName": "Data"},
                               bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"),
                               {"version": 1, "map": [{"key": "Data", "value": f"Parameter {param}"}]}))
    def text(self, label, key, x, y, w, h, fs, col):
        k = self.skin.defn(f"slText{w}x{h}_{fs}_{col}", definition([], [label_ (fs, col, (0, 0, w, h))], ignore=True))
        self.place(label, k, self.skin.P[key], x, y, w, h, touch=False)
    def drag_knob(self, w, h, orient):
        self.skin.img("sl_drag.png", Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)))
        return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "sl_drag.png", "numFrames": NUMFRAMES, "invert": False,
                    "dragOrientation": orient, "handleName": "Data"}, bounds((0, 0, w, h)))
    def box(self, label, key, x, y, w=134, h=74):
        k = self.skin.defn(f"slBox{w}x{h}", definition([action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"),
                 action("Enter Pressed", "Show Overlay", "knob overlay")], [self.drag_knob(w, h - 26, "Vertical"), label_ (17, "fff5af28", (0, 0, w, h - 26)), focus((0, 0, w, h - 26))]))
        self.place(label, k, self.skin.P[key], x, y + 26, w, h - 26)
        self.bg.append(("box", x, y, w, h, label.upper()))
        if len(self.q) < 16: self.q.append(self.skin.P[key])
    def button(self, label, key, x, y, w, h, lab, red=False):
        pre = self.skin.pre
        on, off = self.skin.img(f"{pre}_{key}_on.png", key_image(True, w, h, lab, red)), self.skin.img(f"{pre}_{key}_off.png", key_image(False, w, h, lab, red))
        acts = [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
        k = self.skin.defn(f"slKey_{key}", definition(acts, [focus((0, 0, w, h)), comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off,
                 "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))]))
        self.place(label, k, self.skin.P[key], x, y, w, h)
    def wave(self, x, y, w, h, top_key, bot_key):
        # 128 bars; each bar = two mirrored 72 px filmstrip cells driven by the same parameter (the proven 72 px frame size)
        sk = self.skin; bw, half = w // 128, h // 2
        top, bot = sk.img("sl_wave_top.png", wave_strip (bw - 1, half, True)), sk.img("sl_wave_bot.png", wave_strip (bw - 1, half, False))
        def cell(name, strip):
            return sk.defn(f"{name}{bw}", definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": strip, "numFrames": NUMFRAMES,
                           "invert": False, "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, bw - 1, half)))], ignore=True))
        kt, kb = cell("slWaveT", top), cell("slWaveB", bot)
        for i in range(128):
            self.place(f"Wave {i + 1}", kt, sk.P[f"wave{i}"], x + i * bw, y, bw - 1, half, touch=False)
            self.place(f"Wave {i + 1} ", kb, sk.P[f"wave{i}"], x + i * bw, y + half, bw - 1, half, touch=False)
        # drag strips over the waveform: drag left / right on the top half to move the start marker, bottom half the end
        ww = bw * 128
        kd = sk.defn(f"slWaveDrag{ww}x{half}", definition([action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay")],
                     [self.drag_knob(ww, half, "Horizontal"), focus((0, 0, ww, half))]))
        self.place(f"Drag {top_key}", kd, sk.P[top_key], x, y, ww, half)
        self.place(f"Drag {bot_key}", kd, sk.P[bot_key], x, y + half, ww, half)
        self.bg.append(("wave", x - 6, y - 6, ww + 10, h + 12, top_key, bot_key))
    def meter(self, key, x, y, w, h):
        strip = self.skin.img(f"{self.skin.pre}_meter.png", meter_strip(w, h))
        k = self.skin.defn(f"caMeter{w}x{h}", definition([], [comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": strip, "numFrames": NUMFRAMES,
                           "invert": False, "dragOrientation": "Horizontal", "handleName": "Data"}, bounds((0, 0, w, h)))], ignore=True))
        self.place("Meter", k, self.skin.P[key], x, y, w, h, touch=False)

def label_(h, colour, b):
    return comp("Value", "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": "Original"}, "type": "Value", "handleName": "Data"}, bounds(b))

def key_image(on, w, h, lab, red, fs=15):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    face = (ACCENT if on else (150, 36, 32)) if red else ((96, 100, 116) if on else (52, 54, 62))
    off = s if on else 0
    d.rounded_rectangle([0, off, (w - 2) * s, (h - 3) * s + off], radius=8 * s, fill=rgba(face), outline=rgba(mix(face, (255, 255, 255), 0.3)), width=s)
    text_c(d, (w - 2) * s / 2, ((h - 3) * s) / 2 + off, lab, font(fs * s), rgba(PRINT))
    return im.resize((w, h), Image.LANCZOS)

def wave_strip(bw, h, top):
    # top cells grow up from their bottom edge, bottom cells grow down from their top edge
    strip = Image.new("RGBA", (bw, h * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    cols = [(70, 72, 82), (56, 140, 230), ACCENT, (240, 240, 245)]
    for f in range(FRAMES):
        state, hh = divmod(f, 32)
        y0 = f * h
        amp = max(1, int((h - 2) * hh / 31))
        col = cols[1] if state == 3 else cols[min(state, 3)]
        if top: d.rectangle([0, y0 + h - amp, bw - 1, y0 + h - 1], fill=rgba(col))
        else: d.rectangle([0, y0, bw - 1, y0 + amp - 1], fill=rgba(col))
        if state == 3:                                       # a slice starts here: gold line (+ flag on top)
            d.rectangle([0, y0, 1, y0 + h - 1], fill=rgba(GOLD))
            if top: d.polygon([(0, y0), (6, y0), (0, y0 + 8)], fill=rgba(GOLD))
    return strip

def meter_strip(w, h):
    # input level, -60..0 dB: green, gold above -12 dB, red above -3 dB
    strip = Image.new("RGBA", (w, h * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    seg = 8
    for f in range(FRAMES):
        lit = w * min(f, NUMFRAMES) / NUMFRAMES; y0 = f * h
        for sx in range(0, w, seg):
            db = -60 + 60 * sx / w
            c = (40, 200, 120) if db < -12 else (GOLD if db < -3 else ACCENT)
            d.rectangle([sx, y0 + 2, sx + seg - 3, y0 + h - 3], fill=rgba(c if sx < lit else mix(c, BG, 0.82)))
    return strip

# ================= Da Sample Lab
SL = Skin("Da Sample Lab", "radioready.vst.dasamplelab", "DA SAMPLE LAB", LIB, "SL", "sl")
x0, dx = 40, 146
def col(i): return x0 + i * dx
# ---------------- SAMPLE
pg = SL.page("SAMPLE")
pg.wave(64, 64, 1152, 144, "start", "end")
pg.text("Info", "info", 40, 220, 760, 30, 17, "ffececf0")
pg.text("Analysis", "analysis", 800, 220, 440, 30, 20, "fff5af28")
pg.box("Browse", "source", col(0), 258)
pg.box("Sample", "file", col(1), 258, w=280)
for i, (lab, key) in enumerate((("Start", "start"), ("End", "end"), ("BPM", "bpm"), ("Pitch", "pitch"), ("Fine", "fine"))):
    pg.box(lab, key, col(i + 2) + 146, 258)
for i, (lab, key) in enumerate((("Time Mode", "tmode"), ("Sync", "sync"), ("Speed", "speed"), ("Target Key", "targetkey"), ("Zoom", "zoom"), ("Level", "gain"))):
    pg.box(lab, key, col(i), 346)
pg.button("Load", "load", col(6), 372, 134, 48, "LOAD", red=True)
pg.button("Play Sample", "audition", col(7), 372, 134, 48, "\u25b6 PLAY")
pg.button("BPM Half", "bpmhalf", col(0), 440, 134, 44, "BPM \u00f7 2")
pg.button("BPM Double", "bpmdouble", col(1), 440, 134, 44, "BPM \u00d7 2")
pg.button("Latest Capture", "latest", col(2), 440, 134, 44, "LATEST")
pg.bg.append(("hint", col(3) + 10, 446, "BROWSE: SampleLab folder, Recent recordings (MPC's own), internal drive or USB."))
pg.bg.append(("hint", col(3) + 10, 464, "LATEST loads the newest take from the Da Sample Lab Capture insert effect."))
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")
# ---------------- CHOP
pg = SL.page("CHOP")
pg.wave(64, 58, 1152, 144, "slstart", "slend")
pg.text("Slice Info", "sliceinfo", 40, 208, 1200, 28, 18, "fff5af28")
for i, (lab, key) in enumerate((("Chop By", "chopmode"), ("Sensitivity", "sens"), ("Slices", "count"))):
    pg.box(lab, key, col(i), 238)
pg.button("Chop", "chop", col(3), 264, 134, 48, "CHOP", red=True)
pg.button("Find", "find", col(4), 264, 134, 48, "FIND")
pg.box("Zoom", "zoom", col(5), 238)
for i, (lab, key) in enumerate((("Slice", "slice"), ("Slice Start", "slstart"), ("Slice End", "slend"), ("Slice Level", "slvol"), ("Slice Pitch", "slpitch"),
                                ("Filter", "slfilter"), ("Slice Attack", "slattack"), ("Slice Release", "slrelease"))):
    pg.box(lab, key, col(i), 320)
pg.box("Choke Group", "slchoke", col(0), 402)
for i, (lab, key, t) in enumerate((("Reverse", "slrev", "REVERSE"), ("Loop", "slloop", "LOOP"), ("Mute", "slmute", "MUTE"), ("Play Slice", "slplay", "▶ SLICE"))):
    pg.button(lab, key, col(i + 1), 428, 134, 48, t)
for i, (lab, key, t) in enumerate((("Move Left", "slleft", "◀ MOVE"), ("Move Right", "slright", "MOVE ▶"), ("Split", "split", "SPLIT"),
                                   ("Merge", "merge", "MERGE"), ("Export Slice", "export", "EXPORT"), ("Export All", "exportall", "EXPORT ALL"))):
    pg.button(lab, key, col(i), 490, 134, 44, t)
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")
# ---------------- PADS
pg = SL.page("PADS")
for i, (lab, key) in enumerate((("Pad Mode", "padmode"), ("First Pad Note", "basenote"), ("Trigger", "playmode"), ("Voices", "poly"),
                                ("Velocity", "velsens"), ("Attack", "attack"), ("Release", "release"), ("Level", "gain"))):
    pg.box(lab, key, col(i), 70)
pg.text("Pad Info", "padinfo", 40, 170, 1200, 30, 18, "fff5af28")
pg.bg.append(("pads",))
pg.text("Status", "status", 40, 580, 1200, 32, 17, "ff9698a4")

# ================= Da Sample Lab Capture
CA = Skin("Da Sample Lab Capture", "radioready.vst.dasamplelabcapture", "DA SAMPLE LAB CAPTURE", CAPLIB, "CA", "ca")
pg = CA.page("CAPTURE")
pg.text("Time", "time", 340, 80, 600, 80, 56, "ffececf0")
pg.meter("meter", 140, 186, 1000, 30)
pg.bg.append(("meterscale", 140, 220, 1000))
pg.button("Record", "record", 240, 270, 380, 96, "● REC  /  ■ STOP", red=True)
pg.button("Arm", "arm", 660, 270, 380, 96, "ARM: START ON SIGNAL")
pg.box("Threshold", "threshold", 474, 392)
pg.box("Max Length", "maxlen", 670, 392)
pg.text("Status", "status", 40, 490, 1200, 32, 19, "fff5af28")
pg.bg.append(("hint", 40, 540, "Insert on any track, a submix or the master: audio passes through untouched. Takes are 24-bit WAVs in "
              "/sdcard/SampleLab/Samples/Captures. Then in Da Sample Lab: tap LATEST."))

def background(skin, page):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.text((40, 14), skin.title, font=font(26), fill=PRINT)
    tx = 48 + d.textlength(skin.title, font=font(26))
    d.text((tx, 22), page.name, font=font(15), fill=ACCENT)
    tw = d.textlength("RadioReady Audio", font=font(12)); d.text((W - 40 - tw, 22), "RadioReady Audio", font=font(12), fill=DIM)
    small = font(11, "DejaVuSans.ttf")
    for item in page.bg:
        if item[0] == "wave":
            _, x, y, w, h, tk, bk = item; d.rounded_rectangle([x, y, x + w, y + h], radius=10, fill=(8, 9, 12), outline=(46, 47, 54))
            d.line([x + 4, y + h / 2, x + w - 4, y + h / 2], fill=(34, 35, 42))
            name = {"start": "START", "end": "END", "slstart": "SLICE START", "slend": "SLICE END"}
            hint = f"drag \u25c0\u25b6 on the waveform:  top half = {name[tk]},  bottom half = {name[bk]}"
            d.text((W - 190 - d.textlength(hint, font=small), 25), hint, font=small, fill=DIM)
        elif item[0] == "box":
            _, x, y, w, h, cap = item
            d.text((x + 4, y + 2), cap, font=font(12), fill=DIM)
            d.rounded_rectangle([x, y + 24, x + w, y + h], radius=8, fill=PANEL2)
        elif item[0] == "hint":
            _, x, y, t = item; d.text((x, y), t, font=small, fill=DIM)
        elif item[0] == "meterscale":
            _, x, y, w = item
            for db in (-60, -48, -36, -24, -12, -6, -3, 0):
                px = x + w * (db + 60) / 60; d.line([px, y, px, y + 4], fill=DIM)
                text_c(d, min(max(px, x + 12), x + w - 8), y + 14, f"{db}", small, DIM)
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
                        d.text((x + 6, y + 4), str(pad), font=small, fill=DIM)
                        text_c(d, x + 31, y + 38, f"S{sl}", font(16), PRINT)
            d.text((40, 548), "Slices mode: each pad plays its slice (notes from FIRST PAD NOTE up). Chromatic: the selected slice on every pad, C3 = original pitch.",
                   font=font(12, "DejaVuSans.ttf"), fill=DIM)
    return im

def build(skin):
    if os.path.isdir(skin.dir): shutil.rmtree(skin.dir)
    os.makedirs(skin.out)
    tabs, qmaps = [], []
    for ti, page in enumerate(skin.pages):
        bgname = f"{skin.pre}_bg_{page.name.lower()}.png"
        skin.images[bgname] = background(skin, page)
        comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, bounds((0, 0, W, H)))] + page.comps
        key = f"{skin.pre.upper()}|{page.name}"
        skin.defs[key] = definition([], comps, "ff111113")
        tabs.append({"version": 3, "tabName": page.name, "fnKeyIndex": ti, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
                     "componentName": key, "initialSize": f"0 0 {W} {H}", "scale": 1.0})
        qmaps.append({"Tab": ti + 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": {f"Q-Link {i + 1}": p for i, p in enumerate(page.q)}})
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": [{"key": k, "value": v} for k, v in skin.defs.items()]},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(skin.out, "TUI.json"), "w"), indent=2)
    prog = {f"Q-Link {i + 1}": p for i, p in enumerate(sum((pg.q for pg in skin.pages[:2]), [])[:16])}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"}, "Screen Mode Q-Links": {"version": 4, "map": qmaps}, "Program Mode Q-Links": prog}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(skin.out, fn), "w"), indent=2)
    for name, im in skin.images.items(): im.save(os.path.join(skin.out, name), optimize=True)
    open(os.path.join(skin.dir, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>" + skin.ident + "</identifier>\n"
        "\t<version>1.1.0.0</version>\n</plugincontent>\n")
    tallest = max(im.size[1] for im in skin.images.values())
    frame = max(im.size[1] // FRAMES for n, im in skin.images.items() if im.size[1] > H)
    print(f"{skin.name} skin: {len(skin.images) + 3} files, tallest image {tallest} px, largest filmstrip frame {frame} px")

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

KEEP = []                      # ctypes callbacks + libraries must outlive the plugin instances
def open_plugin(path, workdir, nin):
    os.environ["SAMPLELAB_ROOT"] = workdir
    lib = ctypes.CDLL(path)
    cb = HOSTCB(lambda *a: 0); KEEP.append((lib, cb)); lib.VSTPluginMain.restype = ctypes.POINTER(AEffect); lib.VSTPluginMain.argtypes = [HOSTCB]
    fx = lib.VSTPluginMain(cb); e = fx.contents
    D = lambda op, idx=0, val=0, ptr=None, opt=0.0: e.dispatcher(fx, op, idx, val, ptr, opt)
    D(10, opt=44100.0); D(12, val=1)
    n = 512; OL = (ctypes.c_float * n)(); OR = (ctypes.c_float * n)(); outs = (ctypes.POINTER(ctypes.c_float) * 2)(OL, OR)
    IL = (ctypes.c_float * n)(); IR = (ctypes.c_float * n)(); ins = (ctypes.POINTER(ctypes.c_float) * 2)(IL, IR)
    t = [0]
    def run(k=8, amp=0.0):
        for _ in range(k):
            for i in range(n): IL[i] = IR[i] = amp * math.sin(2 * math.pi * 220 * (t[0] + i) / 44100)
            t[0] += n
            e.processReplacing(fx, ins if nin else None, outs, n)
    def disp(i):
        b = ctypes.create_string_buffer(256); D(7, idx=i, ptr=ctypes.cast(b, ctypes.c_void_p)); return b.value.decode("utf-8", "replace")
    def snap(): return [e.getParameter(fx, i) for i in range(e.numParams)], [disp(i) for i in range(e.numParams)]
    return fx, e, D, run, disp, snap

def sampler_states(workdir):
    import time
    P = SL.P; fx, e, D, run, disp, snap = open_plugin(LIB, workdir, 0)
    time.sleep(2.7); run()
    e.setParameter(fx, P["file"], 0.0); e.setParameter(fx, P["load"], 1.0)
    for _ in range(200):
        run(); time.sleep(0.05)
        if disp(P["status"]).startswith("Loaded"): break
    e.setParameter(fx, P["slice"], 4 / 63.0); e.setParameter(fx, P["slfilter"], 0.3); e.setParameter(fx, P["slchoke"], 0.25); run(); time.sleep(0.2); run()
    whole = snap()
    e.setParameter(fx, P["zoom"], 1.0); run(); time.sleep(0.3); run()
    zoomed = snap()
    D(1)
    return {"SAMPLE": whole, "CHOP": zoomed, "PADS": whole}

def capture_states(workdir):
    import time
    P = CA.P; fx, e, D, run, disp, snap = open_plugin(CAPLIB, workdir, 2)
    time.sleep(2.7); run(amp=0.3)
    e.setParameter(fx, P["record"], 1.0)
    run(160, amp=0.3); time.sleep(0.3); run(4, amp=0.3)
    st = snap()
    e.setParameter(fx, P["record"], 0.0); run(8); time.sleep(0.5); run(8)
    D(1)
    return {"CAPTURE": st}

def preview(skin, states, outdir):
    os.makedirs(outdir, exist_ok=True)
    pre = skin.pre
    for page in skin.pages:
        vals, txt = states[page.name]
        im = Image.open(os.path.join(skin.out, f"{pre}_bg_{page.name.lower()}.png")).convert("RGBA"); d = ImageDraw.Draw(im)
        for c in page.comps:
            b = [int(v) for v in c["bounds"]["bounds"].split()]; x, y, w, h = b
            param = int(c["handle remapping"]["map"][0]["value"].split()[1]); v = vals[param]; key = c["componentData"]["type"]
            if key.startswith("slWaveDrag"): continue
            if key.startswith("slWave") or key.startswith("caMeter"):
                name = f"{pre}_meter.png" if key.startswith("caMeter") else ("sl_wave_top.png" if key.startswith("slWaveT") else "sl_wave_bot.png")
                strip = Image.open(os.path.join(skin.out, name)).convert("RGBA")
                fr = round(v * NUMFRAMES); im.alpha_composite(strip.crop((0, fr * h, w, fr * h + h)), (x, y))
            elif key.startswith("slKey"):
                k2 = key[len("slKey_"):]
                im.alpha_composite(Image.open(os.path.join(skin.out, f"{pre}_{k2}_{'on' if (v >= 0.5 and k2 in TOGGLES) else 'off'}.png")).convert("RGBA"), (x, y))
            elif key.startswith("slBox"):
                text_c(d, x + w / 2, y + h / 2, txt[param], font(15), GOLD)
            elif key.startswith("slText"):
                fs = int(key.split("_")[1]); col = key.split("_")[2]
                text_c(d, x + w / 2, y + h / 2, txt[param], font(int(fs * 0.85)), tuple(int(col[i:i + 2], 16) for i in (2, 4, 6)))
        tag = "capture" if pre == "ca" else page.name.lower()
        path = os.path.join(outdir, f"skin-preview-{tag}.png"); im.convert("RGB").save(path); print("preview:", path)

if __name__ == "__main__":
    for sk in (SL, CA): build(sk)
    if len(sys.argv) > 2:
        preview(SL, sampler_states(sys.argv[2]), sys.argv[1])
        preview(CA, capture_states(sys.argv[2]), sys.argv[1])
