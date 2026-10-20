#!/usr/bin/env python3
"""Generate the Quanta MPC screen skin (TUI.json + Q-Links + filmstrip images) and a preview PNG.

Filmstrip rules (proven on the MPC by the other plugins in this repository): frames are square, at most 72 px,
128 frames (9216 px tall); numFrames is the last frame index (127). The parameter indices follow the plugin's Param enum
in DSP/QuantaEngine.h: 0 Intensity (named Particle in the code), 1 Tone, 2 Density, 3 Glue, 4 Width, 5 Low End, 6 Air, 7 Mix, 8 Output, 9 Mono Bass,
10 Preset, 11 Bypass, 12 A/B (session switches), 13 In Level, 14 Out Level, 15 Glue GR, 16 Correlation (read-only meters).

Original artwork: charcoal and graphite surfaces, silver knobs, a warm accent for Particle. No third-party names or marks.
Usage: python3 tools/make_skin.py [preview-dir]
"""
import json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_NAME = "Quanta Audio - VST - Quanta"
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", SKIN_NAME)
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3
KS = 72                     # knob / meter filmstrip size
CW, CH = 118, 132           # knob cell: knob, then its value
ANG0, ANG1 = -135.0, 135.0
FONT_DIRS = ["/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/dejavu", "/Library/Fonts", "C:/Windows/Fonts"]

INK, PANEL, EDGE = (16, 14, 24), (30, 27, 42), (70, 60, 96)
PRINT, DIM, FAINT = (240, 238, 246), (168, 160, 184), (108, 100, 128)
ARC_TRACK = (44, 40, 60)      # the unlit part of a knob's value arc
ACCENT = (226, 96, 196)     # magenta, used for Particle, the preset name and active marks
VIOLET = (142, 110, 238)    # secondary accent: the display frame and the tab line
SILVER = (186, 190, 196)
LED_GREEN, LED_AMBER, LED_RED = (96, 214, 130), (232, 170, 60), (236, 80, 60)

P = {"particle": 0, "tone": 1, "density": 2, "glue": 3, "width": 4, "lowend": 5, "air": 6, "mix": 7, "output": 8,
     "monobass": 9, "preset": 10, "bypass": 11, "ab": 12, "inlevel": 13, "outlevel": 14, "gr": 15, "corr": 16}
KNOBS = [  # key, label, knob centre x, cap, Q-Link order: two rows of four under the stereo field
    ("tone", "TONE", 393, SILVER), ("density", "DENSITY", 561, SILVER), ("glue", "GLUE", 729, SILVER),
    ("width", "WIDTH", 896, SILVER), ("lowend", "LOW END", 393, SILVER), ("air", "AIR", 561, SILVER),
    ("mix", "MIX", 729, SILVER), ("output", "OUTPUT", 896, SILVER),
]
ROW_TOP = {"tone": 352, "density": 352, "glue": 352, "width": 352,
           "lowend": 472, "air": 472, "mix": 472, "output": 472}   # top of each knob cell
PARTICLE = (91, 112)        # the Intensity control: its cell, top left, centred in the left column
VISUAL_BOX = (334, 84, 621, 236)  # the 3D stereo visual: the centre, above the knob rows
MONO_POS = (40, 380)        # Mono Bass switch, left column
METER_POS = {"in": (1100, 112), "out": (1100, 222), "gr": (1100, 332), "corr": (1100, 442)}   # right column, stacked
PRESET_BOX = (560, 18, 420, 36)   # preset display in the header band; turn or drag to step through presets

def font(size, bold=True):
    names = ["LiberationSans-Bold.ttf", "DejaVuSans-Bold.ttf"] if bold else ["LiberationSans-Regular.ttf", "DejaVuSans.ttf"]
    for d in FONT_DIRS:
        for n in names:
            pth = os.path.join(d, n)
            if os.path.exists(pth):
                return ImageFont.truetype(pth, max(1, int(round(size))))
    return ImageFont.load_default()

def rgba(c, a=255): return (int(c[0]), int(c[1]), int(c[2]), a)
def mix(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def hexcol(c, a=255): return "%02x%02x%02x%02x" % (a, c[0], c[1], c[2])
def ang_xy(cx, cy, r, deg):
    a = math.radians(deg - 90); return cx + r * math.cos(a), cy + r * math.sin(a)

def visual3d(w, h):
    """a 3D wireframe of a stereo field: a surface rippling toward the viewer, drawn in perspective with depth shading"""
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    W2, H2 = w * SS, h * SS
    d.rounded_rectangle([0, 0, W2 - 1, H2 - 1], radius=12 * SS, fill=(12, 10, 20, 255), outline=rgba(VIOLET, 255), width=2 * SS)
    cx, horizon, fx, fy, cam_h = W2 / 2, H2 * 0.22, W2 * 0.40, H2 * 1.30, 1.0
    rows, cols = 30, 60
    def height(x, z):   # the surface: two ripples that grow toward the viewer
        return 0.55 * math.sin(3.2 * math.pi * x + 2.2 * z) * (0.25 + z) + 0.25 * math.cos(2.0 * math.pi * x - 1.3 * z) * z
    def project(x, z):     # z = 1 is the front row (lowest on screen), z = 0 the far edge at the horizon
        depth = 2.2 + (1.0 - z) * 3.0
        return cx + x * 2.4 * fx / depth, horizon + (cam_h - height(x, z) * 0.8) * fy / depth
    lines = []
    for r in range(rows):           # rows run from far (z 0) to near (z 1)
        z = r / (rows - 1)
        lines.append([project(-1.0 + 2.0 * c / (cols - 1), z) for c in range(cols)] + [z])
    glow = Image.new("RGBA", im.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(glow)   # a soft halo behind the mesh
    for r in range(rows):
        pts, z = lines[r][:-1], lines[r][-1]
        for c in range(cols - 1):
            gd.line([pts[c], pts[c + 1]], fill=rgba(mix(ACCENT, VIOLET, z), 90), width=5 * SS)
    for c in range(cols):
        for r in range(rows - 1):
            gd.line([lines[r][c], lines[r + 1][c]], fill=rgba(mix(ACCENT, VIOLET, lines[r][-1]), 70), width=5 * SS)
    im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
    d = ImageDraw.Draw(im)
    for r in range(rows):
        pts, z = lines[r][:-1], lines[r][-1]
        for c in range(cols - 1):
            d.line([pts[c], pts[c + 1]], fill=rgba(mix(ACCENT, VIOLET, z), int(70 + 170 * z)), width=max(1, SS))
    for c in range(cols):           # the columns that give the surface its 3D shape
        for r in range(rows - 1):
            d.line([lines[r][c], lines[r + 1][c]], fill=rgba(mix(ACCENT, VIOLET, lines[r][-1]), int(60 + 140 * lines[r][-1])), width=max(1, SS))
    return im.resize((w, h), Image.LANCZOS)

def tracked(d, x, y, text, size, fill, track=0.0, anchor="la"):
    """text with letter spacing (in px) between characters, for headings and the wordmark"""
    f = font(size)
    for ch in text:
        d.text((x, y), ch, font=f, fill=fill, anchor=anchor)
        x += d.textlength(ch, font=f) + track
    return x

def background():
    im = Image.new("RGB", (W, H), INK)
    d = ImageDraw.Draw(im)
    for y in range(H):                                   # soft vertical graphite gradient
        t = y / H
        d.line([0, y, W, y], fill=mix((46, 36, 74), (14, 13, 22), t))
    d.rounded_rectangle([14, 64, W - 14, H - 14], radius=14, fill=PANEL, outline=EDGE, width=2)
    tracked(d, 34, 16, "QUANTA", 32, PRINT, track=3.0)
    tracked(d, 212, 30, "stereo mix enhancement", 15, DIM, track=0.4)
    d.rectangle([14, 64, W - 14, 66], fill=VIOLET)                    # hairline accent under the header
    tracked(d, 334, 332, "BALANCE", 11, DIM, track=2.0)               # section headings share one style and baseline
    tracked(d, 40, 356, "MONO BASS", 11, DIM, track=2.0)
    tracked(d, 1100, 88, "METERS", 11, DIM, track=2.0)
    for key, text, cx, cap in KNOBS:                    # knob names under each knob
        d.text((cx, ROW_TOP[key] + 86), text, font=font(13), fill=DIM, anchor="mm")
    d.text((PARTICLE[0] + CW / 2, PARTICLE[1] + 86), "INTENSITY", font=font(13), fill=ACCENT, anchor="mm")
    d.line([302, 84, 302, 600], fill=EDGE, width=1)     # column dividers
    d.line([1084, 84, 1084, 600], fill=EDGE, width=1)
    vx, vy, vw, vh = VISUAL_BOX
    vis = visual3d(vw, vh)
    im.paste(vis.convert("RGB"), (vx, vy), vis.split()[3])
    d = ImageDraw.Draw(im)
    d.text((VISUAL_BOX[0] + 14, VISUAL_BOX[1] + 10), "STEREO FIELD", font=font(12), fill=DIM)
    bx, by, bw, bh = PRESET_BOX
    d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=8, fill=(10, 9, 16), outline=VIOLET, width=2)
    d.text((bx + 12, by + bh / 2), "PRESET", font=font(12), fill=DIM, anchor="lm")
    return im

def knob_strip(cap):
    """one filmstrip: a knurled-edge silver knob with a pointer, 128 frames at 72 px"""
    s = KS * SS; c = s / 2; R = s * 0.44
    base = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sh = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([c - R, c - R + s * 0.05, c + R, c + R + s * 0.05], fill=(0, 0, 0, 170))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(s * 0.03)))
    d = ImageDraw.Draw(base)
    for yy in range(int(c - R), int(c + R) + 1):          # cylinder side, lit from above
        k = min(1.0, max(0.0, (yy - (c - R)) / (2 * R)))
        dx = math.sqrt(max(0.0, R * R - (yy - c) ** 2))
        d.line([c - dx, yy, c + dx, yy], fill=rgba(mix((120, 124, 130), (44, 46, 50), k)))
    rt = R * 0.84
    face_col = mix(cap, (255, 255, 255), 0.10) if cap == SILVER else cap
    arc_col = ACCENT if cap == ACCENT else VIOLET
    strip = Image.new("RGBA", (KS, KS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        im = base.copy(); dd = ImageDraw.Draw(im)
        deg = ANG0 + (ANG1 - ANG0) * f / NUMFRAMES
        for i in range(72):                                # knurling turns with the knob
            a = deg + i * 5
            x0, y0 = ang_xy(c, c, R * 0.90, a); x1, y1 = ang_xy(c, c, R * 0.99, a)
            dd.line([x0, y0, x1, y1], fill=(0, 0, 0, 80), width=max(1, SS // 2))
        for i in range(40, 0, -1):                         # flat face with a slight dome
            t = i / 40; r = rt * t
            dd.ellipse([c - r, c - r - rt * 0.05 * (1 - t), c + r, c + r - rt * 0.05 * (1 - t)],
                       fill=rgba(mix(mix(face_col, (0, 0, 0), 0.25), mix(face_col, (255, 255, 255), 0.22), (1 - t) ** 1.8)))
        x0, y0 = ang_xy(c, c, rt * 0.12, deg); x1, y1 = ang_xy(c, c, rt * 0.92, deg)
        dd.line([x0, y0, x1, y1], fill=rgba((18, 18, 20)), width=int(SS * 2.2))
        # value arc just outside the knob: a faint track over the whole sweep, lit up to the current value
        ra = R * 1.14; box = [c - ra, c - ra, c + ra, c + ra]
        dd.arc(box, ANG0 - 90, ANG1 - 90, fill=rgba(ARC_TRACK), width=int(SS * 1.6))
        if deg > ANG0 + 0.5:
            dd.arc(box, ANG0 - 90, deg - 90, fill=rgba(arc_col), width=int(SS * 2.0))
        strip.paste(im.resize((KS, KS), Image.LANCZOS), (0, f * KS))
    return strip

def accent_knob_strip(): return knob_strip(ACCENT)

def meter_strip(kind):
    """a 72 px tile of segmented LEDs that light up with the frame (0..127): green, amber, then red as the level rises.
    Correlation is bipolar: its segments grow from the centre, amber below zero and green above."""
    strip = Image.new("RGBA", (KS, KS * FRAMES), (0, 0, 0, 0))
    N = 12                                                  # segments per bar
    for f in range(FRAMES):
        v = min(1.0, f / NUMFRAMES)
        im = Image.new("RGBA", (KS * SS, KS * SS), rgba((12, 11, 18), 255))
        d = ImageDraw.Draw(im)
        x0, y0, x1, y1 = 8 * SS, 6 * SS, (KS - 8) * SS, (KS - 6) * SS
        d.rounded_rectangle([x0, y0, x1, y1], radius=5 * SS, fill=rgba((18, 17, 26), 255), outline=rgba((70, 62, 96), 255), width=SS)
        d.line([x0 + 5 * SS, y0 + 2 * SS, x1 - 5 * SS, y0 + 2 * SS], fill=rgba((110, 98, 150), 255), width=SS)   # top highlight
        bx0, bx1 = x0 + 7 * SS, x1 - 7 * SS
        by0, by1 = y0 + 6 * SS, y1 - 6 * SS
        seg = (by1 - by0) / N
        gap = 1.2 * SS
        for i in range(N):                                  # i = 0 is the bottom segment
            frac = (i + 0.5) / N
            top, bot = by1 - (i + 1) * seg + gap / 2, by1 - i * seg - gap / 2
            if kind == "corr":
                lit = (v >= 0.5 and 0.5 <= frac <= v) or (v < 0.5 and v <= frac < 0.5)
                color = LED_GREEN if v >= 0.5 else LED_AMBER if v >= 0.25 else LED_RED
            else:
                lit = frac <= v
                color = LED_GREEN if frac < 0.70 else LED_AMBER if frac < 0.90 else LED_RED
                if kind == "gr":                            # reduction: the same colours, but the bar grows as the compressor works
                    color = LED_GREEN if frac < 0.5 else LED_AMBER if frac < 0.8 else LED_RED
            fill = rgba(color) if lit else rgba(mix(color, (26, 25, 34), 0.86))
            d.rounded_rectangle([bx0, top, bx1, bot], radius=SS, fill=fill)
        strip.paste(im.resize((KS, KS), Image.LANCZOS), (0, f * KS))
    return strip

def switch_images(on):
    w, h = 96, 40
    im = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=h * SS // 2, fill=(14, 15, 17, 255), outline=EDGE + (255,), width=SS)
    cx = (w * SS - h * SS) + h * SS / 2 if on else h * SS / 2
    d.ellipse([cx - h * SS * 0.42, h * SS * 0.08, cx + h * SS * 0.42, h * SS * 0.92], fill=rgba(ACCENT if on else SILVER))
    return im.resize((w, h), Image.LANCZOS)

def tile_button(label, w, h, on):
    im = Image.new("RGBA", (w, h), rgba(INK)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([1, 1, w - 2, h - 2], radius=6, fill=rgba(PANEL), outline=rgba(EDGE), width=1)
    d.text((w / 2, h / 2), label, font=font(14), fill=rgba(ACCENT if on else PRINT), anchor="mm")
    return im

# ---------------------------------------------------------------- TUI.json helpers (same component format as G-Glue)
NOREMAP = {"version": 1, "remap": []}
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
def label(kind, h, colour, b, just="horizontallyCentred verticallyCentred", case="Original"):
    return comp(kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": just, "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": hexcol(PRINT, 200), "backgroundInset": 1.0, "outlineThickness": 1.5},
                bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
TOGGLE = lambda: [action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")]
def strip(img, x, y, sq, frames=NUMFRAMES):
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": frames, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds((x, y, sq, sq)))
def btn_part(on, off, w, h):
    return comp("Button", "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1,
                "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, w, h)))
def place(comps, name, key, param, x, y, w, h, touch=False):
    comps.append(comp(name, key, {"version": 1, "handleName": "Data"},
                      bounds((x, y, w, h), focus="Yes" if touch else "No", show="Hide" if touch else "Show"), bindp(param)))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    save = lambda im, n: im.save(os.path.join(OUT, n), optimize=True)
    defs, comps = [], []
    save(background(), "qn_bg.png")
    comps.append(comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "qn_bg.png"}, bounds((0, 0, W, H))))

    # knobs: one filmstrip per cap colour
    for cap_name, cap in (("silver", SILVER), ("accent", ACCENT)):
        save(knob_strip(cap), f"qn_knob_{cap_name}.png")
        defs.append({"key": f"qnKnob_{cap_name}", "value": definition(CTRL(), [
            focus((0, 0, CW, CH)), strip(f"qn_knob_{cap_name}.png", (CW - KS) / 2, 0, KS),
            label("Value", 15, hexcol(PRINT), (0, KS + 6, CW, 26))])})
    for key, text, cx, cap in KNOBS:
        x = cx - CW / 2
        place(comps, text.title(), "qnKnob_silver", P[key], x, ROW_TOP[key], CW, CH, touch=True)
    place(comps, "Intensity", "qnKnob_accent", P["particle"], PARTICLE[0], PARTICLE[1], CW, CH, touch=True)

    # mono bass switch
    save(switch_images(True), "qn_mono_on.png"); save(switch_images(False), "qn_mono_off.png")
    defs.append({"key": "qnSwitch_mono", "value": definition(TOGGLE(), [btn_part("qn_mono_on.png", "qn_mono_off.png", 96, 40), focus((0, 0, 96, 40))])})
    place(comps, "Mono bass", "qnSwitch_mono", P["monobass"], MONO_POS[0], MONO_POS[1], 96, 40, touch=True)

    # read-only meters: the host reports them as parameters
    for kind, idx in (("in", P["inlevel"]), ("out", P["outlevel"]), ("gr", P["gr"]), ("corr", P["corr"])):
        save(meter_strip(kind), f"qn_meter_{kind}.png")
        defs.append({"key": f"qnMeter_{kind}", "value": definition([], [strip(f"qn_meter_{kind}.png", 0, 0, KS), label("Value", 13, hexcol(DIM), (0, KS + 4, KS, 22))], ignore=True)})
        place(comps, f"Meter {kind}", f"qnMeter_{kind}", idx, METER_POS[kind][0], METER_POS[kind][1], KS, KS)

    # header switches: Bypass and A/B (session switches, not saved with the project)
    save(tile_button("BYPASS", 120, 36, True), "qn_byp_on.png"); save(tile_button("BYPASS", 120, 36, False), "qn_byp_off.png")
    save(tile_button("B", 64, 36, True), "qn_ab_b.png"); save(tile_button("A", 64, 36, False), "qn_ab_a.png")
    defs.append({"key": "qnSwitch_byp", "value": definition(TOGGLE(), [btn_part("qn_byp_on.png", "qn_byp_off.png", 120, 36), focus((0, 0, 120, 36))])})
    place(comps, "Bypass", "qnSwitch_byp", P["bypass"], 1076, 18, 120, 36, touch=True)
    defs.append({"key": "qnSwitch_ab", "value": definition(TOGGLE(), [btn_part("qn_ab_b.png", "qn_ab_a.png", 64, 36), focus((0, 0, 64, 36))])})
    place(comps, "A/B", "qnSwitch_ab", P["ab"], 1000, 18, 64, 36, touch=True)

    # preset browser: a drag strip (as G-Glue uses) whose value label shows the preset name the host reports
    bx, by, bw, bh = PRESET_BOX
    save(Image.new("RGBA", (16, 16 * FRAMES), (0, 0, 0, 0)), "qn_drag.png")
    defs.append({"key": "qnBrowse", "value": definition(CTRL(), [
        comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "qn_drag.png", "numFrames": NUMFRAMES, "invert": False,
             "dragOrientation": "Vertical", "handleName": "Data"}, bounds((0, 0, bw, bh))),
        label("Value", 18, hexcol(PRINT), (90, 2, bw - 100, bh - 4), "horizontallyLeft verticallyCentred"), focus((0, 0, bw, bh))])})
    place(comps, "Preset", "qnBrowse", P["preset"], bx, by, bw, bh, touch=True)

    defs.append({"key": "QN|Main", "value": definition([], comps, "ff141416")})
    tabs = [{"version": 3, "tabName": "QUANTA", "fnKeyIndex": 0, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "QN|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0}]
    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)

    ql = ["particle", "tone", "density", "glue", "width", "lowend", "air", "mix", "output", "monobass"]
    qmap = {f"Q-Link {i + 1}": P[k] for i, k in enumerate(ql)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap}]},
         "Program Mode Q-Links": qmap}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"):
        json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>quantaaudio.vst.quanta</identifier>\n"
        "\t<version>1.0.0.0</version>\n</plugincontent>\n")

    sizes = [Image.open(os.path.join(OUT, f)).size for f in os.listdir(OUT) if f.endswith(".png")]
    strips = [s for s in sizes if s[1] > H]
    bad = [s for s in strips if s[1] != s[0] * FRAMES or s[0] > 72]
    print(f"skin: {len(sizes)} images, {len(strips)} filmstrips, rule violations: {len(bad)}")
    return len(bad) == 0

def preview(outdir):
    """a still of the skin's layout, rendered from the same images (not a device screenshot)"""
    os.makedirs(outdir, exist_ok=True)
    im = background().convert("RGBA")
    knobs = knob_strip(SILVER).crop((0, 64 * KS, KS, 65 * KS))
    accent = knob_strip(ACCENT).crop((0, 96 * KS, KS, 97 * KS))
    for key, text, cx, cap in KNOBS:
        im.alpha_composite(knobs, (int(cx - KS / 2), ROW_TOP[key]))
    im.alpha_composite(accent, (PARTICLE[0] + 23, PARTICLE[1]))
    for kind in ("in", "out", "gr", "corr"):
        im.alpha_composite(meter_strip(kind).crop((0, 80 * KS, KS, 81 * KS)), METER_POS[kind])
    im.alpha_composite(switch_images(False), MONO_POS)
    im.alpha_composite(tile_button("A", 64, 36, False), (1000, 18))
    im.alpha_composite(tile_button("BYPASS", 120, 36, False), (1076, 18))
    d = ImageDraw.Draw(im)
    bx, by, bw, bh = PRESET_BOX
    d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=8, fill=None, outline=rgba(VIOLET), width=2)
    d.text((bx + 102, by + bh / 2), "Custom", font=font(18), fill=rgba(ACCENT), anchor="lm")   # the host fills the real name
    im.convert("RGB").save(os.path.join(outdir, "quanta-skin-preview.png"), optimize=True)

if __name__ == "__main__":
    ok = build()
    if len(sys.argv) > 1: preview(sys.argv[1])
    sys.exit(0 if ok else 1)
