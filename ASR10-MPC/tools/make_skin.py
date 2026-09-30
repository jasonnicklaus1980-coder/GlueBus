#!/usr/bin/env python3
"""Generate the ASR10 MPC screen skin: an ASR-10 style panel with seven vertical sliders and mode switches (no pads).

Built with the GlueBus / SP1200 skin pipeline (same TUI.json schema, filmstrip controls, Q-Link maps). Proven on an
MPC X with SP1200: MPC slices filmstrips into square frames, so the fader strip uses square frames with the fader
centred. The skin folder is "<manufacturer> - VST - <plugin>" = "GlueBus - VST - ASR10".
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
VERSION = "1.0.0.0"

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
def spaced(s, n=1): return (" " * n).join(s)
def italic_text(im, xy, s, size, fill, slant=0.22):
    """Bold text sheared into an italic (no oblique font file needed)."""
    f = font(size); b = ImageDraw.Draw(im).textbbox((0, 0), s, font=f)
    w, h = b[2] + int(size * slant) + 4, b[3] + 4
    t = Image.new("RGBA", (w, h), (0, 0, 0, 0)); ImageDraw.Draw(t).text((0, 0), s, font=f, fill=rgba(fill))
    t = t.transform((w, h), Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), resample=Image.BICUBIC)
    im.paste(t, xy, t)

# ---------- palette (ASR-10: charcoal body, black panel, grey keys, red LEDs, blue-green fluorescent display) ----------
BODY    = (52, 53, 56)
PANEL   = (30, 31, 33)
PANEL_HI = (44, 45, 48)
SLOT    = (10, 10, 11)
PRINT   = (224, 225, 222)
PRINT_DIM = (140, 142, 142)
VFD_BG, VFD_TXT = (8, 22, 22), (92, 236, 214)
LED     = (255, 52, 40)
KEY     = (178, 180, 178)
NAME_COL, VALUE_COL, FOCUS_COL = "ffe0e1de", "ff5cecd6", "ff5cecd6"

# ---------- parameter indices (must match src/asr10.cpp) ----------
P = dict(input=0, tune=1, fine=2, fc1=3, fc2=4, mix=5, volume=6, rate=7, fmode=8, tmode=9, bypass=10)

def lpos(f, lo, hi): return math.log(f / lo) / math.log(hi / lo)
DB = [(0, "-24"), (12 / 36, "-12"), (24 / 36, "0"), (1, "+12")]
# the seven sliders, left to right: (param, panel label, [(position 0..1, scale label)], number of fine ticks)
SLIDERS = [("input", "INPUT", DB, 12),
           ("tune", "TUNE", [((st + 12) / 24, f"{st:+d}" if st else "0") for st in (-12, -6, 0, 6, 12)], 24),
           ("fine", "FINE", [(0, "-50"), (.25, "-25"), (.5, "0"), (.75, "+25"), (1, "+50")], 20),
           ("fc1", "FILTER 1", [(0, "100"), (lpos(1000, 100, 20000), "1k"), (lpos(10000, 100, 20000), "10k"), (1, "OPEN")], 10),
           ("fc2", "FILTER 2", [(0, "20"), (lpos(100, 20, 20000), "100"), (lpos(1000, 20, 20000), "1k"),
                                (lpos(10000, 20, 20000), "10k"), (1, "OPEN")], 10),
           ("mix", "MIX", [(0, "0"), (.5, "50"), (1, "100")], 10),
           ("volume", "VOLUME", DB, 12)]
SX0, SPITCH, BOXW, BOXH = 30, 114, 110, 408       # slider component boxes
FW, FH = 76, 330                                  # fader artwork (slot + cap) inside each frame
FS = FH                                           # square frames (MPC slices filmstrips by the image width), fader centred
FY = 28                                           # fader top inside the box
TRAVEL0, TRAVEL1 = 26, FH - 26                    # cap centre travel (top, bottom) inside the frame
SLIDER_Y = 104
PLATE_X1 = SX0 + len(SLIDERS) * SPITCH + 4
RIGHT_X = PLATE_X1 + 26

# switch groups on the right: (param, title, options, x, y, button width, columns)
SEG_W = 188
GROUPS = [("rate", "SAMPLE  RATE", ["30 kHz", "44.1 kHz"], RIGHT_X + 10, 92, SEG_W, 2),
          ("tmode", "TUNE  MODE", ["PITCH", "RATE"], RIGHT_X + 10, 176, SEG_W, 2),
          ("fmode", "FILTER  MODE", ["LP2 / HP2", "LP3 / HP1", "LP2 / LP2", "LP3 / LP1"], RIGHT_X + 10, 260, SEG_W, 2)]
BYPASS_XY = (RIGHT_X + 4, 392)
QLINKS = ["input", "tune", "fine", "fc1", "fc2", "mix", "volume", "rate", "fmode", "tmode", "bypass"]

def seg_xy(g, i):
    key, title, opts, x, y, w, cols = g
    return x + (i % cols) * (w + 6), y + 24 + (i // cols) * 46

# ---------- drawing ----------
def fader_frame(t):
    """One fader drawing (FW x FH): slot + grey ASR-style cap at position t (0 = bottom, 1 = top)."""
    s = SS; w, h = FW * s, FH * s
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    cx = w / 2
    d.rounded_rectangle([cx - 5 * s, TRAVEL0 * s - 12 * s, cx + 5 * s, TRAVEL1 * s + 12 * s], radius=5 * s, fill=rgba(SLOT))
    d.line([cx, TRAVEL0 * s - 8 * s, cx, TRAVEL1 * s + 8 * s], fill=rgba((36, 36, 38)), width=2 * s)
    cy = (TRAVEL1 + (TRAVEL0 - TRAVEL1) * t) * s
    cw, ch = 30 * s, 20 * s
    sh = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([cx - cw + 2 * s, cy - ch + 6 * s, cx + cw + 2 * s, cy + ch + 6 * s], radius=4 * s, fill=(0, 0, 0, 160))
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(4 * s))); d = ImageDraw.Draw(im)
    cap = Image.new("RGBA", (w, h), (0, 0, 0, 0)); cd = ImageDraw.Draw(cap)
    for yy in range(int(cy - ch), int(cy + ch) + 1):           # light grey cap, lit from above
        k = (yy - (cy - ch)) / (2 * ch)
        cd.line([cx - cw, yy, cx + cw, yy], fill=rgba(mix((214, 216, 214), (132, 134, 134), k)))
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, fill=255)
    im.paste(cap, (0, 0), mask); d = ImageDraw.Draw(im)
    d.rounded_rectangle([cx - cw, cy - ch, cx + cw, cy + ch], radius=4 * s, outline=rgba((70, 70, 72)), width=s)
    d.rectangle([cx - cw + 3 * s, cy - 1.5 * s, cx + cw - 3 * s, cy + 1.5 * s], fill=rgba((24, 24, 26)))   # dark index line
    return im.resize((FW, FH), Image.LANCZOS)

def fader_strip():
    strip = Image.new("RGBA", (FS, FS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(fader_frame(f / (FRAMES - 1)), ((FS - FW) // 2, f * FS + (FS - FH) // 2))
    return strip

def key_button(on, w=110, h=40):
    """ASR-10 style grey key with a red LED."""
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba((16, 16, 18)))
    off = 2 * s if on else 0
    d.rounded_rectangle([3 * s, 3 * s + off, (w - 3) * s, (h - 5) * s + off], radius=3 * s, fill=rgba(mix(KEY, (0, 0, 0), .12) if on else KEY))
    cx, cy, r = w * s / 2, h * s / 2 + off / 2, 5 * s
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse([cx - 3 * r, cy - 3 * r, cx + 3 * r, cy + 3 * r], fill=(255, 60, 40, 170))
        im.alpha_composite(g.filter(ImageFilter.GaussianBlur(3 * s))); d = ImageDraw.Draw(im)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgba(LED if on else (90, 30, 26)))
    return im.resize((w, h), Image.LANCZOS)

def seg(label, on, w, h=40):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    fill, txt = ((16, 16, 18), PRINT) if on else (KEY, (30, 30, 32))
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba(fill), outline=rgba((12, 12, 14)), width=s)
    if on: d.ellipse([10 * s, h * s / 2 - 4 * s, 18 * s, h * s / 2 + 4 * s], fill=rgba(LED))
    text_c(d, w * s / 2 + (6 * s if on else 0), h * s / 2, label, font(14 * s), rgba(txt))
    return im.resize((w, h), Image.LANCZOS)

def background():
    random.seed(10)
    im = Image.new("RGB", (W, H), BODY); px = im.load()
    for y in range(H):
        for x in range(W):
            n = random.randint(-2, 2); px[x, y] = (BODY[0] + n, BODY[1] + n, BODY[2] + n)
    d = ImageDraw.Draw(im)
    # header: name plate
    d.rectangle([0, 0, W, 64], fill=(18, 18, 20)); d.rectangle([0, 64, W, 66], fill=VFD_TXT)
    italic_text(im, (26, 8), "ASR-10", 40, PRINT)
    d.text((222, 14), spaced("ADVANCED  SAMPLING  RECORDER"), font=font(13), fill=PRINT_DIM)
    d.text((222, 34), spaced("16-BIT  ·  30 / 44.1 kHz  ·  OTTO"), font=font(13), fill=VFD_TXT)
    tw = d.textlength(spaced("SLIDERS"), font=font(16)); d.text((W - 28 - tw, 22), spaced("SLIDERS"), font=font(16), fill=PRINT)
    # slider panel
    d.rounded_rectangle([14, 80, PLATE_X1, 610], radius=6, fill=PANEL, outline=(12, 12, 14), width=2)
    d.rectangle([14, 80, PLATE_X1, 84], fill=PANEL_HI)
    f_mark = font(10)
    for i, (key, lab, marks, ticks) in enumerate(SLIDERS):
        x = SX0 + i * SPITCH; cx = x + BOXW / 2
        y0, y1 = SLIDER_Y + FY + TRAVEL0, SLIDER_Y + FY + TRAVEL1
        for j in range(ticks + 1):
            y = y1 + (y0 - y1) * j / ticks
            d.line([cx - 34, y, cx - 30, y], fill=PRINT_DIM, width=1); d.line([cx + 30, y, cx + 34, y], fill=PRINT_DIM, width=1)
        for pos, m in marks:
            y = y1 + (y0 - y1) * pos
            d.line([cx - 38, y, cx - 30, y], fill=PRINT, width=1); d.line([cx + 30, y, cx + 38, y], fill=PRINT, width=1)
            d.text((cx + 42, y - 6), m, font=f_mark, fill=PRINT)
    d.line([26, 572, PLATE_X1 - 12, 572], fill=(12, 12, 14), width=2)
    d.text((34, 582), spaced("DATA  ENTRY"), font=font(12), fill=PRINT_DIM)
    # right panel: switch groups, bypass, fluorescent display
    d.rounded_rectangle([RIGHT_X - 10, 80, 1266, 610], radius=6, fill=PANEL, outline=(12, 12, 14), width=2)
    for key, title, opts, x, y, w, cols in GROUPS: d.text((x, y), spaced(title), font=font(13), fill=PRINT)
    d.text((BYPASS_XY[0] + 130, BYPASS_XY[1] + 14), spaced("BYPASS"), font=font(13), fill=PRINT)
    vx0, vy0 = RIGHT_X + 4, 452
    d.rounded_rectangle([vx0, vy0, 1252, 596], radius=4, fill=VFD_BG, outline=(4, 4, 6), width=3)
    lf = font(14, "DejaVuSansMono-Bold.ttf")
    lines = ["30k : BAND-LIMITED 13.4 kHz", "44.1: FULL RANGE", "OTTO: LINEAR INTERPOLATION",
             "FILTER: 4 POLES, NO RESONANCE", "TUNE -12..+12 ST, FINE CENTS"]
    for k, t in enumerate(lines): d.text((vx0 + 14, vy0 + 12 + k * 26), t, font=lf, fill=VFD_TXT)
    for sx, sy in [(4, 72), (W - 16, 72), (4, H - 16), (W - 16, H - 16)]:
        d.ellipse([sx, sy, sx + 11, sy + 11], fill=(120, 122, 122), outline=(40, 40, 42)); d.line([sx + 2, sy + 5, sx + 9, sy + 5], fill=(40, 40, 42), width=2)
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
def label(kind, h, colour, b, case="Original", name=None):
    return comp(name or kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": FOCUS_COL, "backgroundInset": 2.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    defs = []

    fader_strip().save(os.path.join(OUT, "asr_fader.png"), optimize=True)
    defs.append({"key": "asrSlider", "value": definition(
        [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")],
        [focus((0, 0, BOXW, BOXH)),
         label("Name", 15, NAME_COL, (0, 2, BOXW, 22), case="Upper Case"),
         comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "asr_fader.png", "numFrames": NUMFRAMES, "invert": False,
              "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((BOXW - FS) // 2, FY, FS, FS))),
         label("Value", 20, VALUE_COL, (0, FY + FH + 8, BOXW, 28), case="Upper Case")])})

    key_button(True).save(os.path.join(OUT, "asr_btn_on.png")); key_button(False).save(os.path.join(OUT, "asr_btn_off.png"))
    defs.append({"key": "asrToggle", "value": definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
        focus((0, 0, 122, 52)),
        comp("Button", "Button", {"version": 2, "onImage": "asr_btn_on.png", "offImage": "asr_btn_off.png", "buttonId": 1,
             "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((6, 6, 110, 40)))])})

    for g in GROUPS:
        key, title, opts, x, y, w, cols = g
        for i, o in enumerate(opts):
            typ, on, off = f"asrSeg_{key}_{i}", f"asr_seg_{key}_{i}_on.png", f"asr_seg_{key}_{i}_off.png"
            seg(o, True, w).save(os.path.join(OUT, on)); seg(o, False, w).save(os.path.join(OUT, off))
            defs.append({"key": typ, "value": definition([action("Mouse Down", "Q-Link")], [
                comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i, "numButtonsInGroup": len(opts),
                     "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, 40)))])})

    background().save(os.path.join(OUT, "asr_bg.png"), optimize=True)
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "asr_bg.png"}, bounds((0, 0, W, H)))]
    for i, (key, lab, _, _) in enumerate(SLIDERS):
        comps.append(comp(lab, "asrSlider", {"version": 1, "handleName": "Data"},
                          bounds((SX0 + i * SPITCH, SLIDER_Y, BOXW, BOXH), focus="Yes", show="Hide"), bind(key)))
    for g in GROUPS:
        key, title, opts, x, y, w, cols = g
        for i, o in enumerate(opts):
            sx, sy = seg_xy(g, i)
            comps.append(comp(f"{title} {o}", f"asrSeg_{key}_{i}", {"version": 1, "handleName": "Data"},
                              bounds((sx, sy, w, 40), focus="Yes" if i == 0 else "No", show="Hide"), bind(key)))
    comps.append(comp("Bypass", "asrToggle", {"version": 1, "handleName": "Data"}, bounds((*BYPASS_XY, 122, 52), focus="Yes", show="Hide"), bind("bypass")))
    defs.append({"key": "ASR|Sliders", "value": definition([], comps, "ff343538")})
    tabs = [{"version": 3, "tabName": "Sliders", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "ASR|Sliders", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]

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

# ---------- preview (typical values; MPC draws the live name/value labels itself) ----------
PREVIEW = dict(input=24 / 36, tune=12 / 24, fine=.5, fc1=lpos(6000, 100, 20000), fc2=lpos(80, 20, 20000), mix=1.0, volume=24 / 36)
PREVIEW_TXT = dict(input="+0.0", tune="0", fine="+0", fc1="6.0 KHZ", fc2="80 HZ", mix="100", volume="+0.0")
PREVIEW_ON = dict(rate=0, tmode=0, fmode=0)
def preview(path):
    im = Image.open(os.path.join(OUT, "asr_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    strip = Image.open(os.path.join(OUT, "asr_fader.png"))
    for i, (key, lab, _, _) in enumerate(SLIDERS):
        x = SX0 + i * SPITCH; fr = round(PREVIEW[key] * (FRAMES - 1)); fx0 = (FS - FW) // 2
        im.alpha_composite(strip.crop((fx0, fr * FS, fx0 + FW, fr * FS + FH)), (int(x + (BOXW - FW) / 2), SLIDER_Y + FY))
        text_c(d, x + BOXW / 2, SLIDER_Y + 13, lab, font(14), PRINT)
        text_c(d, x + BOXW / 2, SLIDER_Y + FY + FH + 22, PREVIEW_TXT[key], font(17), VFD_TXT)
    for g in GROUPS:
        key = g[0]
        for i in range(len(g[2])):
            im.alpha_composite(Image.open(os.path.join(OUT, f"asr_seg_{key}_{i}_{'on' if i == PREVIEW_ON[key] else 'off'}.png")), seg_xy(g, i))
    im.alpha_composite(Image.open(os.path.join(OUT, "asr_btn_off.png")), (BYPASS_XY[0] + 6, BYPASS_XY[1] + 6))
    im.convert("RGB").save(path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
