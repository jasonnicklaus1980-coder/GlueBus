#!/usr/bin/env python3
"""Generate the SP1200 MPC screen skin: the SP-1200 front panel as eight vertical sliders (no pads).

Built with the GlueBus skin pipeline (same TUI.json schema, filmstrip controls, Q-Link maps).
Output: mpc/skin/GlueBus - VST - SP1200/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Each slider is a filmstrip control dragged vertically and bound to "Parameter N" = VST parameter index
(see src/sp1200.cpp). Requires Pillow.
Usage: python3 tools/make_skin.py [preview.png]
"""
import json, math, os, random, shutil, sys
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

# ---------- parameter indices (must match src/sp1200.cpp) ----------
P = dict(input=0, tune=1, decay=2, channel=3, sweep=4, floor=5, mix=6, volume=7, mode=8, bypass=9)

# the eight sliders, left to right: (param, panel label, scale marks bottom->top)
SLIDERS = [("input", "INPUT", ["-24", "-12", "0", "+12"]),
           ("tune", "TUNE", ["-12", "-8", "-4", "0", "+4", "+7"]),
           ("decay", "DECAY", ["20ms", "", "", "OFF"]),
           ("channel", "OUTPUT", ["1-2", "3-4", "5-6", "7-8"]),
           ("sweep", "DYN SWEEP", ["1ms", "", "", "250"]),
           ("floor", "DYN FLOOR", ["100", "", "", "2k"]),
           ("mix", "MIX", ["0", "", "", "100"]),
           ("volume", "VOLUME", ["-24", "-12", "0", "+12"])]
TUNE_POS = lambda st: (st + 12) / 19.0            # slider position of a tuning step (mirrors the VST mapping)
SX0, SPITCH, BOXW, BOXH = 36, 114, 110, 408       # slider component boxes
FW, FH = 76, 330                                  # fader filmstrip frame
FY = 28                                           # fader top inside the box
TRAVEL0, TRAVEL1 = 26, FH - 26                    # cap centre travel (top, bottom) inside the frame
SLIDER_Y = 104
RIGHT_X = 964

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
    strip = Image.new("RGBA", (FW, FH * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(fader_frame(f / (FRAMES - 1)), (0, f * FH))
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

def background():
    random.seed(12)
    im = Image.new("RGB", (W, H), CHASSIS); px = im.load()
    for y in range(H):
        for x in range(W):
            n = random.randint(-3, 3); px[x, y] = (CHASSIS[0] + n, CHASSIS[1] + n, CHASSIS[2] + n)
    d = ImageDraw.Draw(im)
    # header: name plate
    d.rectangle([0, 0, W, 64], fill=(24, 24, 26)); d.rectangle([0, 64, W, 67], fill=RED)
    italic_text(im, (26, 8), "SP-1200", 40, PRINT)
    d.text((252, 14), spaced("SAMPLING  PERCUSSION"), font=font(13), fill=PRINT_DIM)
    d.text((252, 34), spaced("12-BIT  ·  26.04 kHz"), font=font(13), fill=RED)
    tw = d.textlength(spaced("SLIDERS"), font=font(16)); d.text((W - 28 - tw, 22), spaced("SLIDERS"), font=font(16), fill=PRINT)
    # graphite slider plate
    d.rounded_rectangle([18, 80, 940, 610], radius=6, fill=PLATE, outline=(20, 20, 22), width=2)
    d.rectangle([18, 80, 940, 84], fill=PLATE_HI)
    f_mark, f_num = font(10), font(22)
    for i, (key, lab, marks) in enumerate(SLIDERS):
        x = SX0 + i * SPITCH; cx = x + BOXW / 2
        y0, y1 = SLIDER_Y + FY + TRAVEL0, SLIDER_Y + FY + TRAVEL1
        if key == "tune":        # one tick per semitone, long ticks at the printed values
            for st in range(-12, 8):
                y = y1 + (y0 - y1) * TUNE_POS(st); long_ = st in (-12, -8, -4, 0, 4, 7)
                d.line([cx - 30 - (8 if long_ else 4), y, cx - 30, y], fill=PRINT if long_ else PRINT_DIM, width=1)
                d.line([cx + 30, y, cx + 30 + (8 if long_ else 4), y], fill=PRINT if long_ else PRINT_DIM, width=1)
                if long_: d.text((cx + 42, y - 6), f"{st:+d}" if st else "0", font=f_mark, fill=PRINT)
        else:
            nm = len(marks)
            for j in range(11):
                y = y1 + (y0 - y1) * j / 10; long_ = j in (0, 10)
                d.line([cx - 30 - (8 if long_ else 4), y, cx - 30, y], fill=PRINT if long_ else PRINT_DIM, width=1)
                d.line([cx + 30, y, cx + 30 + (8 if long_ else 4), y], fill=PRINT if long_ else PRINT_DIM, width=1)
            for j, m in enumerate(marks):
                if not m: continue
                tpos = j / (nm - 1)
                if key in ("input", "volume"): tpos = {0: 0, 1: 12 / 36, 2: 24 / 36, 3: 1}[j]
                y = y1 + (y0 - y1) * tpos
                d.text((cx + 42, y - 6), m, font=f_mark, fill=PRINT)
        # slider number under the plate, like the numbered sliders over the play keys
        text_c(d, cx, 590, str(i + 1), f_num, PRINT)
    d.line([30, 568, 928, 568], fill=(20, 20, 22), width=2)
    # right-hand panel: mode, bypass, output channel chart
    d.rounded_rectangle([RIGHT_X - 8, 80, 1262, 610], radius=6, fill=PLATE, outline=(20, 20, 22), width=2)
    d.text((RIGHT_X + 10, 94), spaced("TUNE  MODE"), font=font(13), fill=PRINT)
    d.text((RIGHT_X + 10, 216), spaced("BYPASS"), font=font(13), fill=PRINT)
    d.rounded_rectangle([RIGHT_X + 6, 330, 1246, 594], radius=4, fill=LCD_BG, outline=(20, 20, 22), width=3)
    lf = font(14, "DejaVuSansMono-Bold.ttf")
    lines = ["OUTPUT CHANNELS", "", "1-2  SSM2044 DYN VCF", "3-4  FIXED  7.5 kHz", "5-6  FIXED  10 kHz", "7-8  UNFILTERED", "",
             "TUNE  -12 .. +7 ST", "45>33: RATE ONLY", "PITCH: DROP-SAMPLE"]
    for k, t in enumerate(lines): d.text((RIGHT_X + 20, 342 + k * 24), t, font=lf, fill=LCD_TXT)
    for sx, sy in [(6, 72), (W - 18, 72), (6, H - 16), (W - 18, H - 16)]:
        d.ellipse([sx, sy, sx + 11, sy + 11], fill=(150, 152, 152), outline=(60, 60, 62)); d.line([sx + 2, sy + 5, sx + 9, sy + 5], fill=(60, 60, 62), width=2)
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

MODE_OPTS, MODE_W = ["45>33 GRIT", "PITCH"], 136
QLINKS = ["input", "tune", "decay", "channel", "sweep", "floor", "mix", "volume", "mode", "bypass"]

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    defs = []

    fader_strip().save(os.path.join(OUT, "sp_fader.png"), optimize=True)
    defs.append({"key": "spSlider", "value": definition(
        [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")],
        [focus((0, 0, BOXW, BOXH)),
         label("Name", 15, NAME_COL, (0, 2, BOXW, 22), case="Upper Case"),
         comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "sp_fader.png", "numFrames": NUMFRAMES, "invert": False,
              "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((BOXW - FW) / 2, FY, FW, FH))),
         label("Value", 20, VALUE_COL, (0, FY + FH + 8, BOXW, 28), case="Upper Case")])})

    pushbutton(True).save(os.path.join(OUT, "sp_btn_on.png")); pushbutton(False).save(os.path.join(OUT, "sp_btn_off.png"))
    defs.append({"key": "spToggle", "value": definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
        focus((0, 0, 120, 52)),
        comp("Button", "Button", {"version": 2, "onImage": "sp_btn_on.png", "offImage": "sp_btn_off.png", "buttonId": 1,
             "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((12, 6, 96, 40)))])})

    for i, o in enumerate(MODE_OPTS):
        typ, on, off = f"spSeg_mode_{i}", f"sp_seg_mode_{i}_on.png", f"sp_seg_mode_{i}_off.png"
        seg(o, True, MODE_W).save(os.path.join(OUT, on)); seg(o, False, MODE_W).save(os.path.join(OUT, off))
        defs.append({"key": typ, "value": definition([action("Mouse Down", "Q-Link")], [
            comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": i, "numButtonsInGroup": len(MODE_OPTS),
                 "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, MODE_W, 40)))])})

    background().save(os.path.join(OUT, "sp_bg.png"), optimize=True)
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "sp_bg.png"}, bounds((0, 0, W, H)))]
    for i, (key, lab, _) in enumerate(SLIDERS):
        comps.append(comp(lab, "spSlider", {"version": 1, "handleName": "Data"},
                          bounds((SX0 + i * SPITCH, SLIDER_Y, BOXW, BOXH), focus="Yes", show="Hide"), bind(key)))
    for i, o in enumerate(MODE_OPTS):
        comps.append(comp(f"Mode {o}", f"spSeg_mode_{i}", {"version": 1, "handleName": "Data"},
                          bounds((RIGHT_X + 8 + i * (MODE_W + 6), 118, MODE_W, 40), focus="Yes" if i == 0 else "No", show="Hide"), bind("mode")))
    comps.append(label("Value", 18, VALUE_COL, (RIGHT_X + 8, 164, 2 * MODE_W + 6, 26), case="Upper Case", name="Mode Value"))
    comps[-1]["handle remapping"] = bind("mode")
    comps.append(comp("Bypass", "spToggle", {"version": 1, "handleName": "Data"}, bounds((RIGHT_X + 4, 238, 120, 52), focus="Yes", show="Hide"), bind("bypass")))
    defs.append({"key": "SP|Sliders", "value": definition([], comps, "ff6a6c6e")})
    tabs = [{"version": 3, "tabName": "Sliders", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "SP|Sliders", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]

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
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>gluebus.vst.sp1200</identifier>\n"
        "\t<version>1.0.1.0</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview (typical values; MPC draws the live name/value labels itself) ----------
PREVIEW = dict(input=24 / 36, tune=TUNE_POS(-5), decay=1.0, channel=1 / 3, sweep=.55, floor=.30, mix=1.0, volume=24 / 36)
PREVIEW_TXT = dict(input="+0.0", tune="-5", decay="OFF", channel="OUT 3-4", sweep="12 MS", floor="250 HZ", mix="100", volume="+0.0")
def preview(path):
    im = Image.open(os.path.join(OUT, "sp_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    strip = Image.open(os.path.join(OUT, "sp_fader.png"))
    for i, (key, lab, _) in enumerate(SLIDERS):
        x = SX0 + i * SPITCH; fr = round(PREVIEW[key] * (FRAMES - 1))
        im.alpha_composite(strip.crop((0, fr * FH, FW, (fr + 1) * FH)), (int(x + (BOXW - FW) / 2), SLIDER_Y + FY))
        text_c(d, x + BOXW / 2, SLIDER_Y + 13, lab, font(14), (226, 226, 220))
        text_c(d, x + BOXW / 2, SLIDER_Y + FY + FH + 22, PREVIEW_TXT[key], font(17), (255, 70, 50))
    for i in range(len(MODE_OPTS)):
        im.alpha_composite(Image.open(os.path.join(OUT, f"sp_seg_mode_{i}_{'on' if i == 0 else 'off'}.png")), (RIGHT_X + 8 + i * (MODE_W + 6), 118))
    text_c(d, RIGHT_X + 8 + MODE_W + 3, 177, "45>33 GRIT", font(16), (255, 70, 50))
    im.alpha_composite(Image.open(os.path.join(OUT, "sp_btn_off.png")), (RIGHT_X + 16, 244))
    im.convert("RGB").save(path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
