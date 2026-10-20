#!/usr/bin/env python3
"""Generate the Quanta MPC screen skin (TUI.json + Q-Links + filmstrip images) and a preview PNG.

Filmstrip rules (proven on the MPC by the other plugins in this repository): frames are square, at most 72 px,
128 frames (9216 px tall); numFrames is the last frame index (127). The parameter indices follow the plugin's Param enum
in DSP/QuantaEngine.h: 0 Particle, 1 Tone, 2 Density, 3 Glue, 4 Width, 5 Low End, 6 Air, 7 Mix, 8 Output, 9 Mono Bass,
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
ACCENT = (226, 96, 196)     # magenta, used for Particle, the preset name and active marks
VIOLET = (142, 110, 238)    # secondary accent: the display frame and the tab line
SILVER = (186, 190, 196)
LED_GREEN, LED_AMBER, LED_RED = (96, 214, 130), (232, 170, 60), (236, 80, 60)

P = {"particle": 0, "tone": 1, "density": 2, "glue": 3, "width": 4, "lowend": 5, "air": 6, "mix": 7, "output": 8,
     "monobass": 9, "preset": 10, "bypass": 11, "ab": 12, "inlevel": 13, "outlevel": 14, "gr": 15, "corr": 16}
KNOBS = [  # key, label, column position, cap, Q-Link order
    ("tone", "TONE", 330, SILVER), ("density", "DENSITY", 448, SILVER), ("glue", "GLUE", 566, SILVER),
    ("width", "WIDTH", 684, SILVER), ("lowend", "LOW END", 802, SILVER), ("air", "AIR", 920, SILVER),
    ("mix", "MIX", 1038, SILVER), ("output", "OUTPUT", 1156, SILVER),
]
PARTICLE = (60, 150)        # the main control: its own place on the left
VISUAL_BOX = (300, 256, 944, 150)  # the 3D stereo visual: the open band between the knobs and the meters
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
    rows, cols = 22, 46
    def height(x, z):   # the surface: two ripples that grow toward the viewer
        return 0.55 * math.sin(3.2 * math.pi * x + 2.2 * z) * (0.25 + z) + 0.25 * math.cos(2.0 * math.pi * x - 1.3 * z) * z
    def project(x, z):     # z = 1 is the front row (lowest on screen), z = 0 the far edge at the horizon
        depth = 2.2 + (1.0 - z) * 3.0
        return cx + x * 2.4 * fx / depth, horizon + (cam_h - height(x, z) * 0.6) * fy / depth
    lines = []
    for r in range(rows):           # rows run from far (z 0) to near (z 1)
        z = r / (rows - 1)
        lines.append([project(-1.0 + 2.0 * c / (cols - 1), z) for c in range(cols)] + [z])
    for r in range(rows):
        pts, z = lines[r][:-1], lines[r][-1]
        t = z
        for c in range(cols - 1):
            d.line([pts[c], pts[c + 1]], fill=rgba(mix(ACCENT, VIOLET, t), int(60 + 180 * t)), width=max(1, SS))
    for c in range(cols):           # the columns that give the surface its 3D shape
        for r in range(rows - 1):
            d.line([lines[r][c], lines[r + 1][c]], fill=rgba(mix(ACCENT, VIOLET, lines[r][-1]), int(50 + 150 * lines[r][-1])), width=max(1, SS))
    return im.resize((w, h), Image.LANCZOS)

def background():
    im = Image.new("RGB", (W, H), INK)
    d = ImageDraw.Draw(im)
    for y in range(H):                                   # soft vertical graphite gradient
        t = y / H
        d.line([0, y, W, y], fill=mix((46, 36, 74), (14, 13, 22), t))
    d.rounded_rectangle([14, 64, W - 14, H - 14], radius=14, fill=PANEL, outline=EDGE, width=2)
    d.text((34, 18), "QUANTA", font=font(34), fill=PRINT)
    d.text((212, 30), "stereo mix enhancement", font=font(16, False), fill=DIM)
    d.rounded_rectangle([14, 64, W - 14, 74], radius=4, fill=VIOLET)   # thin violet band under the header
    d.text((330, 92), "BALANCE", font=font(14), fill=DIM)
    d.text((60, 436), "METERS AND STEREO", font=font(14), fill=DIM)
    d.text((430, 436), "MONO BASS", font=font(14), fill=DIM)
    for key, text, cx, cap in KNOBS:                    # knob names under each knob
        d.text((cx, 196), text, font=font(13), fill=DIM, anchor="mm")
    d.text((PARTICLE[0] + 36, 226), "PARTICLE", font=font(13), fill=ACCENT, anchor="mm")
    d.line([300, 84, 1244, 84], fill=EDGE, width=1)
    vx, vy, vw, vh = VISUAL_BOX
    vis = visual3d(vw, vh)
    im.paste(vis.convert("RGB"), (vx, vy), vis.split()[3])
    d = ImageDraw.Draw(im)
    bx, by, bw, bh = PRESET_BOX
    d.rounded_rectangle([bx, by, bx + bw, by + bh], radius=8, fill=(10, 9, 16), outline=VIOLET, width=2)
    d.text((bx + 12, by + bh / 2), "PRESET", font=font(12), fill=DIM, anchor="lm")
    d.line([40, 420, 1244, 420], fill=EDGE, width=1)
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
        strip.paste(im.resize((KS, KS), Image.LANCZOS), (0, f * KS))
    return strip

def accent_knob_strip(): return knob_strip(ACCENT)

def meter_strip(kind):
    """a 72 px tile whose bar follows the frame (0..127): LED colours by level; correlation grows from the centre"""
    strip = Image.new("RGBA", (KS, KS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        v = min(1.0, f / NUMFRAMES)
        im = Image.new("RGBA", (KS * SS, KS * SS), rgba((12, 13, 15), 255))
        d = ImageDraw.Draw(im)
        x0, y0, x1, y1 = 10 * SS, 8 * SS, (KS - 10) * SS, (KS - 8) * SS
        d.rounded_rectangle([x0, y0, x1, y1], radius=4 * SS, outline=(60, 63, 70, 255), width=SS)
        inner_top, inner_bot = y0 + 4 * SS, y1 - 4 * SS
        if kind == "corr":
            mid = (inner_top + inner_bot) / 2
            span = (inner_bot - inner_top) / 2
            top = mid - span * abs(v - 0.5) * 2
            bottom = mid + 0
            color = LED_GREEN if v >= 0.5 else LED_AMBER
            if abs(v - 0.5) > 0.2 and v < 0.5:
                color = LED_RED
            if v > 0.5:
                d.rectangle([x0 + 4 * SS, mid - span * (v - 0.5) * 2, x1 - 4 * SS, mid], fill=rgba(color))
            else:
                d.rectangle([x0 + 4 * SS, mid, x1 - 4 * SS, mid + span * (0.5 - v) * 2], fill=rgba(color))
        else:
            fill_top = inner_bot - (inner_bot - inner_top) * v
            if kind == "gr":                                 # reduction: the bar grows as the compressor works
                color = LED_GREEN if v < 0.5 else LED_AMBER if v < 0.8 else LED_RED
            else:
                color = LED_GREEN if v < 0.75 else LED_AMBER if v < 0.92 else LED_RED
            if v > 0.0:
                d.rectangle([x0 + 4 * SS, fill_top, x1 - 4 * SS, inner_bot], fill=rgba(color))
        for k in range(1, 5):                                # printed ticks
            ty = inner_bot - (inner_bot - inner_top) * k / 5
            d.line([x0 - 3 * SS, ty, x0 - 1 * SS, ty], fill=(120, 124, 130, 255), width=SS)
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
        place(comps, text.title(), "qnKnob_silver", P[key], x, 110, CW, CH, touch=True)
    place(comps, "Particle", "qnKnob_accent", P["particle"], PARTICLE[0], PARTICLE[1], CW, CH, touch=True)

    # mono bass switch
    save(switch_images(True), "qn_mono_on.png"); save(switch_images(False), "qn_mono_off.png")
    defs.append({"key": "qnSwitch_mono", "value": definition(TOGGLE(), [btn_part("qn_mono_on.png", "qn_mono_off.png", 96, 40), focus((0, 0, 96, 40))])})
    place(comps, "Mono bass", "qnSwitch_mono", P["monobass"], 430, 470, 96, 40, touch=True)

    # read-only meters: the host reports them as parameters
    for kind, idx, x in (("in", P["inlevel"], 60), ("out", P["outlevel"], 150), ("gr", P["gr"], 240), ("corr", P["corr"], 330)):
        save(meter_strip(kind), f"qn_meter_{kind}.png")
        defs.append({"key": f"qnMeter_{kind}", "value": definition([], [strip(f"qn_meter_{kind}.png", 0, 0, KS), label("Value", 13, hexcol(DIM), (0, KS + 4, KS, 22))], ignore=True)})
        place(comps, f"Meter {kind}", f"qnMeter_{kind}", idx, x, 470, KS, KS)

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
        im.alpha_composite(knobs, (int(cx - KS / 2), 110))
    im.alpha_composite(accent, (PARTICLE[0] + 23, PARTICLE[1]))
    for kind, x in (("in", 60), ("out", 150), ("gr", 240), ("corr", 330)):
        im.alpha_composite(meter_strip(kind).crop((0, 80 * KS, KS, 81 * KS)), (x, 470))
    im.alpha_composite(switch_images(False), (430, 470))
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
