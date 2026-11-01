#!/usr/bin/env python3
"""Generate the ASR10 MPC screen skin in the style of the Ensoniq ASR-10's front panel: floppy drive, its TWO sliders
(Volume and Data Entry), a blue-green fluorescent display with the Edit buttons under it, and grey keys with red
LEDs. No keyboard or pads: this is an effect.

Editing works like the ASR-10: press an Edit button to choose a parameter, then move the Data Entry slider (the
plugin's Data Entry parameter is a proxy for the selected one; the display shows e.g. "TUNE +3"). Q-Links still
reach every parameter directly.

Built with the GlueBus / SP1200 skin pipeline. Rules proven on an MPC X with SP1200: MPC slices filmstrips into
square frames (so the fader strip uses square frames with the fader centred), and the skin folder is
"<manufacturer> - VST - <plugin>" = "GlueBus - VST - ASR10".
Output: mpc/skin/GlueBus - VST - ASR10/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Controls bind to "Parameter N" = VST parameter index (see src/asr10.cpp). Requires Pillow.
Usage: python3 tools/make_skin.py [preview.png]
"""
import json, math, os, random, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "GlueBus - VST - ASR10")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 4     # filmstrip frames; numFrames = last frame index (stock MPC strips); supersampling
VERSION = "1.2.0.0"

FONT_DIRS = ["/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]
def font(size, name="DejaVuSans-Bold.ttf"):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p): return ImageFont.truetype(p, size)
    return ImageFont.load_default()
def mono(size): return font(size, "DejaVuSansMono-Bold.ttf")
def rgba(c, a=255): return (c[0], c[1], c[2], a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def text_c(d, cx, cy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f); d.text((cx - (b[2] - b[0]) / 2 - b[0], cy - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)
def spaced(s, n=1): return (" " * n).join(s)
def slanted(im, xy, s, f, fill, slant=0.18):
    """Text sheared into an italic (no oblique font file needed)."""
    b = ImageDraw.Draw(im).textbbox((0, 0), s, font=f)
    w, h = b[2] + b[3] // 3 + 8, b[3] + 6
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(t).text((0, 0), s, font=f, fill=rgba(fill))
    t = t.transform((w, h), Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), resample=Image.BICUBIC)
    im.paste(t, xy, t)

# ---------- palette (ASR-10: black panel, white print, grey keys with red LEDs, blue-green fluorescent display) ----------
PANEL   = (24, 24, 26)
PANEL_EDGE = (8, 8, 9)
PRINT   = (226, 227, 224)
PRINT_DIM = (136, 138, 138)
VFD_BG, VFD_TXT, VFD_DIM = (6, 18, 18), (96, 238, 216), (40, 110, 100)
LED, LED_OFF = (255, 50, 38), (84, 26, 22)
KEY     = (176, 178, 176)
NAME_COL, VFD_COL, FOCUS_COL = "ffe2e3e0", "ff60eed8", "ff60eed8"

# ---------- parameter indices (must match src/asr10.cpp) ----------
P = dict(input=0, tune=1, fine=2, fc1=3, fc2=4, mix=5, volume=6, rate=7, fmode=8, tmode=9, bypass=10, data=11, edit=12)

# ---------- layout ----------
PANEL_BOTTOM = H                         # the panel fills the screen (no keyboard)
FW, FH = 76, 330                          # fader artwork (slot + cap) inside each frame
FS = FH                                   # square frames (MPC slices filmstrips by the image width), fader centred
TRAVEL0, TRAVEL1 = 26, FH - 26            # cap centre travel inside the frame
# the ASR-10's two sliders: (param, label, show value under it, x); component boxes are SLIDER_W x SLIDER_H
SLIDERS = [("volume", "VOLUME", True, 222), ("data", "DATA ENTRY", False, 346)]
SLIDER_Y, SLIDER_W, SLIDER_H = 100, 110, 408
FY = 34                                   # fader top inside the box
VFD = (486, 100, 474, 116)                # fluorescent display: x, y, w, h
KEY_W, KEY_H = 70, 34                     # small ASR keys
EDIT_X, EDIT_Y, EDIT_GAP = 486, 300, 10.8 # Edit buttons row (under the display)
EDIT_LABELS = ["INPUT", "TUNE", "FINE", "FILTER 1", "FILTER 2", "MIX"]
# option groups: (param, title, [option labels], x, y, key width, gap)
GROUPS = [("fmode", "FILTER  MODE", ["LP2/HP2", "LP3/HP1", "LP2/LP2", "LP3/LP1"], 486, 440, 100, 24.7),
          ("rate", "SAMPLE  RATE", ["30 kHz", "44.1 kHz"], 1000, 150, 108, 24),
          ("tmode", "TUNE  MODE", ["PITCH", "RATE"], 1000, 250, 108, 24)]
BYPASS = (1000, 350, 108)                 # x, y, key width
QLINKS = ["data", "edit", "volume", "input", "tune", "fine", "fc1", "fc2", "mix", "rate", "fmode", "tmode", "bypass"]

def edit_x(i): return EDIT_X + i * (KEY_W + EDIT_GAP)
def group_x(g, i): return g[3] + i * (g[5] + g[6])

# ---------- drawing ----------
def fader_frame(t):
    """The Data Entry slider (FW x FH): slot + black ridged cap with a white line at position t (0 = bottom)."""
    s = SS; w, h = FW * s, FH * s
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    cx = w / 2
    d.rounded_rectangle([cx - 5 * s, TRAVEL0 * s - 12 * s, cx + 5 * s, TRAVEL1 * s + 12 * s], radius=5 * s, fill=rgba((4, 4, 5)))
    d.line([cx, TRAVEL0 * s - 8 * s, cx, TRAVEL1 * s + 8 * s], fill=rgba((40, 40, 42)), width=2 * s)
    cy = (TRAVEL1 + (TRAVEL0 - TRAVEL1) * t) * s
    cw, ch = 28 * s, 22 * s
    sh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([cx - cw + 2 * s, cy - ch + 6 * s, cx + cw + 2 * s, cy + ch + 6 * s], radius=4 * s, fill=(0, 0, 0, 170))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4 * s))); d = ImageDraw.Draw(im)
    cap = Image.new("RGBA", (w, h), (0, 0, 0, 0)); cd = ImageDraw.Draw(cap)
    for yy in range(int(cy - ch), int(cy + ch) + 1):
        k = (yy - (cy - ch)) / (2 * ch)
        col = mix((78, 78, 82), (14, 14, 16), k)
        if int((yy - (cy - ch)) / (4 * s)) % 2 == 1: col = mix(col, (0, 0, 0), 0.25)
        cd.line([cx - cw, yy, cx + cw, yy], fill=rgba(col))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, fill=255)
    im.paste(cap, (0, 0), mask); d = ImageDraw.Draw(im)
    d.rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, outline=rgba((96, 96, 100)), width=s)
    d.rectangle([cx - cw + 3 * s, cy - 1.5 * s, cx + cw - 3 * s, cy + 1.5 * s], fill=rgba(PRINT))
    return im.resize((FW, FH), Image.LANCZOS)

def fader_strip():
    strip = Image.new("RGBA", (FS, FS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(fader_frame(f / (FRAMES - 1)), ((FS - FW) // 2, f * FS + (FS - FH) // 2))
    return strip

def asr_key(on, w=KEY_W, h=KEY_H):
    """Small grey ASR-10 key with a red LED at its top edge; label is printed on the panel above it."""
    s = SS; im = Image.new("RGBA", (w * s, (h + 10) * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    lx, ly, r = w * s / 2, 4 * s, 3.2 * s                          # LED above the key
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([lx - 3 * r, ly - 3 * r, lx + 3 * r, ly + 3 * r], fill=(255, 60, 40, 160))
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(2 * s))); d = ImageDraw.Draw(im)
    d.ellipse([lx - r, ly - r, lx + r, ly + r], fill=rgba(LED if on else LED_OFF))
    y0 = 10 * s; off = 2 * s if on else 0
    d.rounded_rectangle([0, y0, w * s - 1, (h + 10) * s - 1], radius=3 * s, fill=rgba((8, 8, 9)))
    top = mix(KEY, (0, 0, 0), 0.10) if on else KEY
    d.rounded_rectangle([2 * s, y0 + 2 * s + off, (w - 2) * s, (h + 10 - 4) * s + off], radius=2 * s, fill=rgba(top))
    d.line([4 * s, y0 + 4 * s + off, (w - 4) * s, y0 + 4 * s + off], fill=rgba(mix(top, (255, 255, 255), .35)), width=s)
    return im.resize((w, h + 10), Image.LANCZOS)

def floppy(d, x, y):
    d.rounded_rectangle([x, y, x + 178, y + 96], radius=4, fill=(14, 14, 15), outline=(40, 40, 42), width=2)
    d.rounded_rectangle([x + 16, y + 30, x + 162, y + 44], radius=3, fill=(2, 2, 2), outline=(52, 52, 54))
    d.rectangle([x + 126, y + 64, x + 158, y + 78], fill=(60, 60, 62), outline=(90, 90, 92))       # eject button
    d.ellipse([x + 20, y + 68, x + 28, y + 76], fill=(70, 28, 24))                               # drive LED
    d.text((x + 36, y + 66), "DISK", font=font(11), fill=PRINT_DIM)

def background():
    random.seed(9)
    im = Image.new("RGB", (W, H), PANEL); px = im.load()
    for y in range(PANEL_BOTTOM):
        for x in range(W):
            n = random.randint(-2, 2); px[x, y] = (PANEL[0] + n, PANEL[1] + n, PANEL[2] + n)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 4], fill=(44, 44, 46))
    floppy(d, 24, 210)
    d.text((26, 326), spaced("3.5\"  DISK  DRIVE"), font=font(11), fill=PRINT_DIM)
    # slider scales (Volume, Data Entry)
    for key, lab, show, sx in SLIDERS:
        cx = sx + SLIDER_W / 2
        y0, y1 = SLIDER_Y + FY + TRAVEL0, SLIDER_Y + FY + TRAVEL1
        for j in range(21):
            y = y1 + (y0 - y1) * j / 20; long_ = j % 5 == 0
            d.line([cx - 34 - (6 if long_ else 0), y, cx - 30, y], fill=PRINT if long_ else PRINT_DIM, width=1)
            d.line([cx + 30, y, cx + 34 + (6 if long_ else 0), y], fill=PRINT if long_ else PRINT_DIM, width=1)
        if key == "volume":
            for pos, m in ((0, "-24"), (12 / 36, "-12"), (24 / 36, "0"), (1, "+12")):
                d.text((cx + 42, y1 + (y0 - y1) * pos - 6), m, font=font(10), fill=PRINT)
    # fluorescent display with a bezel; live text is drawn by MPC (labels bound to the plugin)
    vx, vy, vw, vh = VFD
    d.rounded_rectangle([vx - 10, vy - 10, vx + vw + 10, vy + vh + 10], radius=6, fill=(4, 4, 5), outline=(52, 52, 54), width=2)
    d.rectangle([vx, vy, vx + vw, vy + vh], fill=VFD_BG)
    for yy in range(vy, vy + vh, 3): d.line([vx, yy, vx + vw, yy], fill=(8, 22, 22))              # faint VFD grid
    d.line([vx + 12, vy + 68, vx + vw - 12, vy + 68], fill=VFD_DIM, width=1)
    # Edit buttons: labels printed above
    f_lab = font(11)
    d.text((EDIT_X, EDIT_Y - 38), spaced("EDIT") + "   ·   " + "select, then move DATA ENTRY", font=f_lab, fill=PRINT_DIM)
    for i, lab in enumerate(EDIT_LABELS): text_c(d, edit_x(i) + KEY_W / 2, EDIT_Y - 12, lab, f_lab, PRINT)
    # option groups
    for g in GROUPS:
        key, title, opts, x, y, kw, gap = g
        d.text((x, y - 44), spaced(title), font=font(12), fill=PRINT)
        for i, o in enumerate(opts): text_c(d, group_x(g, i) + kw / 2, y - 12, o, f_lab, PRINT)
    bx, by, bw = BYPASS
    d.text((bx, by - 44), spaced("BYPASS"), font=font(12), fill=PRINT)
    # name plate (drawn, not the Ensoniq logo)
    slanted(im, (996, 452), "ASR-10", font(54, "DejaVuSans.ttf"), PRINT)
    sub = spaced("ADVANCED  SAMPLING  RECORDER"); tw = d.textlength(sub, font=font(10))
    d.text((W - 24 - tw, 526), sub, font=font(10), fill=PRINT_DIM)
    d.line([486, 366, 960, 366], fill=(46, 46, 48), width=1)
    return im

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
def label(kind, h, colour, b, case="Original", name=None, just="horizontallyCentred verticallyCentred"):
    return comp(name or kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": just, "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def bound_label(name, param, h, colour, b, kind="Value", case="Upper Case", just="horizontallyCentred verticallyCentred"):
    c = label(kind, h, colour, b, case=case, name=name, just=just); c["handle remapping"] = bind(param); return c
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": FOCUS_COL, "backgroundInset": 2.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))
def key_group_defs(key, n, w, defs):
    """One ASR key per option of a choice parameter (buttonId = option index), on/off images per key width."""
    on, off = f"asr_key_{w}_on.png", f"asr_key_{w}_off.png"
    if not os.path.exists(os.path.join(OUT, on)):
        asr_key(True, w).save(os.path.join(OUT, on)); asr_key(False, w).save(os.path.join(OUT, off))
    for i in range(n):
        defs.append({"key": f"asrKey_{key}_{i}", "value": definition([action("Mouse Down", "Q-Link")], [
            comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i, "numButtonsInGroup": n,
                 "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, KEY_H + 10)))])})

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    defs = []

    fader_strip().save(os.path.join(OUT, "asr_fader.png"), optimize=True)
    sw, sh = SLIDER_W, SLIDER_H
    for with_value in (False, True):          # Data Entry (the display shows its value) / Volume (value under it)
        parts = [focus((0, 0, sw, sh)),
                 label("Name", 14, NAME_COL, (0, 4, sw, 22), case="Upper Case"),
                 comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "asr_fader.png", "numFrames": NUMFRAMES, "invert": False,
                      "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((sw - FS) // 2, FY, FS, FS)))]
        if with_value: parts.append(label("Value", 18, VFD_COL, (0, FY + FH + 6, sw, 28), case="Upper Case"))
        defs.append({"key": "asrSliderV" if with_value else "asrSlider", "value": definition(
            [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")],
            parts)})

    key_group_defs("edit", len(EDIT_LABELS), KEY_W, defs)
    for g in GROUPS: key_group_defs(g[0], len(g[2]), g[5], defs)
    asr_key(True, BYPASS[2]).save(os.path.join(OUT, "asr_bypass_on.png")); asr_key(False, BYPASS[2]).save(os.path.join(OUT, "asr_bypass_off.png"))
    defs.append({"key": "asrBypass", "value": definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
        comp("Button", "Button", {"version": 2, "onImage": "asr_bypass_on.png", "offImage": "asr_bypass_off.png", "buttonId": 1,
             "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, BYPASS[2], KEY_H + 10)))])})

    background().save(os.path.join(OUT, "asr_bg.png"), optimize=True)
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "asr_bg.png"}, bounds((0, 0, W, H)))]
    for key, lab, show, sx in SLIDERS:
        comps.append(comp(lab.title(), "asrSliderV" if show else "asrSlider", {"version": 1, "handleName": "Data"},
                          bounds((sx, SLIDER_Y, SLIDER_W, SLIDER_H), focus="Yes", show="Hide"), bind(key)))
    # the fluorescent display: selected parameter + value, then the modes
    vx, vy, vw, vh = VFD
    comps.append(bound_label("Display", "data", 34, VFD_COL, (vx + 14, vy + 10, vw - 28, 52)))
    third = (vw - 28) // 3
    for k, prm in enumerate(("rate", "fmode", "tmode")):
        comps.append(bound_label(f"Display {prm}", prm, 17, VFD_COL, (vx + 14 + k * third, vy + 76, third, 30)))
    for i in range(len(EDIT_LABELS)):
        comps.append(comp(f"Edit {EDIT_LABELS[i]}", f"asrKey_edit_{i}", {"version": 1, "handleName": "Data"},
                          bounds((edit_x(i), EDIT_Y - 10, KEY_W, KEY_H + 10), focus="Yes" if i == 0 else "No", show="Hide"), bind("edit")))
    for g in GROUPS:
        key, title, opts, x, y, kw, gap = g
        for i, o in enumerate(opts):
            comps.append(comp(f"{title} {o}", f"asrKey_{key}_{i}", {"version": 1, "handleName": "Data"},
                              bounds((group_x(g, i), y - 10, kw, KEY_H + 10), focus="Yes" if i == 0 else "No", show="Hide"), bind(key)))
    bx, by, bw = BYPASS
    comps.append(comp("Bypass", "asrBypass", {"version": 1, "handleName": "Data"}, bounds((bx, by - 10, bw, KEY_H + 10), focus="Yes", show="Hide"), bind("bypass")))
    defs.append({"key": "ASR|Panel", "value": definition([], comps, "ff18181a")})
    tabs = [{"version": 3, "tabName": "ASR-10", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "ASR|Panel", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]

    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)

    qmap = {f"Q-Link {i + 1}": P[k] for i, k in enumerate(QLINKS)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>gluebus.vst.asr10</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview (typical state; MPC draws the live labels itself) ----------
def preview(path):
    im = Image.open(os.path.join(OUT, "asr_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    strip = Image.open(os.path.join(OUT, "asr_fader.png"))
    fx0 = (FS - FW) // 2
    for key, lab, show, sx in SLIDERS:
        fr = round((24 / 36 if key == "volume" else 0.5) * (FRAMES - 1))
        im.alpha_composite(strip.crop((fx0, fr * FS, fx0 + FW, fr * FS + FH)), (int(sx + (SLIDER_W - FW) / 2), SLIDER_Y + FY))
        text_c(d, sx + SLIDER_W / 2, SLIDER_Y + 15, lab, font(13), PRINT)
        if show: text_c(d, sx + SLIDER_W / 2, SLIDER_Y + FY + FH + 20, "+0.0", font(16), VFD_TXT)
    vx, vy, vw, vh = VFD
    text_c(d, vx + vw / 2, vy + 36, "TUNE 0", font(30), VFD_TXT)
    third = (vw - 28) // 3
    for k, t in enumerate(("30 KHZ", "LP2 / HP2", "PITCH")): text_c(d, vx + 14 + k * third + third / 2, vy + 91, t, font(15), VFD_TXT)
    on = lambda w, a: Image.open(os.path.join(OUT, f"asr_key_{w}_{'on' if a else 'off'}.png"))
    for i in range(len(EDIT_LABELS)): im.alpha_composite(on(KEY_W, i == 1), (int(edit_x(i)), EDIT_Y - 10))
    sel = dict(fmode=0, rate=0, tmode=0)
    for g in GROUPS:
        for i in range(len(g[2])): im.alpha_composite(on(g[5], i == sel[g[0]]), (int(group_x(g, i)), g[4] - 10))
    im.alpha_composite(Image.open(os.path.join(OUT, "asr_bypass_off.png")), (BYPASS[0], BYPASS[1] - 10))
    im.convert("RGB").save(path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
