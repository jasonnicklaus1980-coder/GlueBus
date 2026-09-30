#!/usr/bin/env python3
"""Generate the EQ7 MPC screen skin: a 7-band parametric EQ laid out like MPC's own visual EQs (no pads).

One screen: a large graph along the top (live analyzer behind the EQ curve), the Output fader with Analyzer and
Bypass switches on the right, and seven band panels underneath. Each panel has a power button and drag-to-change
value boxes for Freq, Gain and Q, then Type and Slope. Two tabs show the same screen with different Q-Link maps:
"GAIN / FREQ" (Q-Links 1-7 gains, 8-14 freqs) and "Q / TYPE" (1-7 Qs, 8-14 types); 15 Output, 16 Bypass on both.

Graph: 64 curve columns and 32 analyzer bars, each a filmstrip bound to one of the plugin's read-only parameters,
which the plugin updates (the curve when a band changes, the analyzer ~14 times a second when the level moves).

Built with the GlueBus / SP1200 / ASR10 skin pipeline. Rules proven on an MPC X with SP1200: MPC slices filmstrips into
square frames (image width = frame height), so tall or wide controls use square frames with the drawing centred and
the control square and centred on its box (MPC clips it to the box); skin folder "<manufacturer> - VST - <plugin>".
Output: mpc/skin/GlueBus - VST - EQ7/{version.xml, Plugin Skins/{TUI.json, Q-Links*.json, *.png}}
Requires Pillow. Usage: python3 tools/make_skin.py [preview.png]
"""
import json, math, os, shutil, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN_DIR = os.path.join(ROOT, "mpc", "skin", "GlueBus - VST - EQ7")
OUT = os.path.join(SKIN_DIR, "Plugin Skins")
W, H = 1280, 628
FRAMES, NUMFRAMES, SS = 128, 127, 3     # filmstrip frames; numFrames = last frame index (stock MPC strips); supersampling
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
def hexcol(c): return "ff%02x%02x%02x" % c

# ---------- palette ----------
BG      = (22, 26, 33)
GRAPH   = (16, 22, 32)
GRID    = (34, 44, 60)
GRID_HI = (58, 72, 94)
PRINT   = (212, 220, 232)
PRINT_DIM = (118, 130, 148)
CURVE   = (112, 186, 246)
SPEC    = (70, 84, 104)
PANEL   = (32, 38, 48)
BOX     = (44, 52, 66)
BANDCOL = [(236, 86, 86), (242, 152, 62), (232, 208, 70), (112, 208, 112), (70, 198, 206), (96, 144, 242), (176, 116, 232)]
FOCUS_COL = hexcol(CURVE)

# ---------- parameter indices (must match src/eq7.cpp) ----------
def bp(b, w): return b * 6 + w                   # b = 0..6; w: 0 on, 1 type, 2 freq, 3 gain, 4 q, 5 slope
P_OUTPUT, P_BYPASS, P_ANALYZER, P_CURVE0, NCURVE, P_SPEC0, NSPEC = 42, 43, 44, 45, 64, 109, 32

# ---------- layout ----------
COLW = 17                                        # curve column width; analyzer bars are 2 columns wide
GX, GY, GH = 24, 14, 250                         # graph origin and height (square frames of GH: keep it modest)
GW = COLW * NCURVE                               # 1088
CURVE_DB, SPEC_FLOOR = 18.0, -84.0
OUT_X = GX + GW + 20                             # output section on the right of the graph
FADER_W, FADER_H = 44, 190                       # output fader drawing (square frames of FADER_H)
PANEL_Y, PANEL_H, PANEL_W, PANEL_X0 = 282, 332, 156, 16
BOX_W, BOX_H = 104, 30                           # value boxes
TBOX_W = 136                                     # type / slope boxes
SQ_BOX = 136                                     # square frame for the box strips (>= widest box)

def col_x(i): return GX + i * COLW
def hz_x(hz): return GX + (math.log(hz / 20.0) / math.log(1000.0) * (NCURVE - 1) + 0.5) * COLW
def db_y(db): return GY + GH / 2 - db / CURVE_DB * (GH / 2 - 10)
def panel_x(b): return PANEL_X0 + b * (PANEL_W + 4)

# ---------- filmstrips ----------
def curve_strip():
    """Square GH frames, the COLW column in the middle; frame f = response -18 .. +18 dB (a line, faint fill to 0 dB)."""
    strip = Image.new("RGBA", (GH, GH * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    x0 = (GH - COLW) // 2
    for f in range(FRAMES):
        db = -CURVE_DB + 2 * CURVE_DB * f / (FRAMES - 1)
        oy = f * GH; y0 = GH / 2; yv = GH / 2 - db / CURVE_DB * (GH / 2 - 10)
        top, bot = (yv, y0) if yv < y0 else (y0, yv)
        if abs(yv - y0) > 1: d.rectangle([x0, oy + top, x0 + COLW - 1, oy + bot], fill=rgba(CURVE, 34))
        d.rectangle([x0, oy + yv - 1.5, x0 + COLW - 1, oy + yv + 1.5], fill=rgba(CURVE, 255))
    return strip

def spec_strip():
    """Square GH frames, a 2-column bar in the middle, rising from the bottom: -84 .. 0 dBFS."""
    strip = Image.new("RGBA", (GH, GH * FRAMES), (0, 0, 0, 0)); d = ImageDraw.Draw(strip)
    bw = 2 * COLW; x0 = (GH - bw) // 2
    for f in range(FRAMES):
        t = f / (FRAMES - 1)
        if t <= 0: continue
        oy = f * GH; top = GH - 4 - t * (GH - 12)
        for y in range(int(top), GH - 3):                           # brighter towards the top of the bar
            k = (y - top) / max(1, GH - 4 - top)
            d.line([x0 + 1, oy + y, x0 + bw - 2, oy + y], fill=rgba(mix((104, 120, 144), SPEC, k), 88))   # soft, behind the curve
    return strip

def fader_strip():
    size = FADER_H
    strip = Image.new("RGBA", (size, size * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        t = f / (FRAMES - 1); s = SS
        im = Image.new("RGBA", (FADER_W * s, FADER_H * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
        cx = FADER_W * s / 2; y0, y1 = 16 * s, (FADER_H - 16) * s
        d.rounded_rectangle([cx - 3 * s, y0 - 8 * s, cx + 3 * s, y1 + 8 * s], radius=3 * s, fill=rgba((8, 10, 14)))
        cy = y1 + (y0 - y1) * t
        d.rectangle([cx - 3 * s, cy, cx + 3 * s, y1 + 8 * s], fill=rgba(CURVE, 160))
        d.ellipse([cx - 11 * s, cy - 11 * s, cx + 11 * s, cy + 11 * s], fill=rgba(CURVE), outline=rgba((20, 30, 44)), width=2 * s)
        im = im.resize((FADER_W, FADER_H), Image.LANCZOS)
        strip.paste(im, ((size - FADER_W) // 2, f * size))
    return strip

def box_strip(w, h):
    """Square SQ_BOX frames, all identical: a flat value box (the plugin's value is drawn on it by a label)."""
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=4 * s, fill=rgba(BOX), outline=rgba((60, 70, 88)), width=s)
    frame = Image.new("RGBA", (SQ_BOX, SQ_BOX), (0, 0, 0, 0))
    frame.paste(im.resize((w, h), Image.LANCZOS), ((SQ_BOX - w) // 2, (SQ_BOX - h) // 2))
    strip = Image.new("RGBA", (SQ_BOX, SQ_BOX * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES): strip.paste(frame, (0, f * SQ_BOX))
    return strip

def power(on, col, w=40, h=40):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=5 * s, fill=rgba(BOX if on else (30, 34, 42)), outline=rgba((60, 70, 88)), width=s)
    c = col if on else (84, 92, 106); cx, cy, r = w * s / 2, h * s / 2 + s, 9 * s
    d.arc([cx - r, cy - r, cx + r, cy + r], 300, 240, fill=rgba(c), width=3 * s)
    d.line([cx, cy - r - 2 * s, cx, cy - 1 * s], fill=rgba(c), width=3 * s)
    return im.resize((w, h), Image.LANCZOS)

def pill(text, on, col, w=110, h=30):
    s = SS; im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1], radius=h * s // 2, fill=rgba(col if on else BOX), outline=rgba((60, 70, 88)), width=s)
    text_c(d, w * s / 2, h * s / 2, text, font(12 * s), rgba((16, 20, 28) if on else PRINT_DIM))
    return im.resize((w, h), Image.LANCZOS)

# ---------- background ----------
def background():
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.rounded_rectangle([GX - 8, GY - 6, GX + GW + 8, GY + GH + 20], radius=6, fill=GRAPH, outline=(10, 12, 18), width=2)
    for db in (-12, -6, 6, 12):
        d.line([GX, db_y(db), GX + GW, db_y(db)], fill=GRID, width=1)
        d.text((GX + GW - 24, db_y(db) - 12), f"{db:+d}", font=font(9), fill=PRINT_DIM)
    d.line([GX, db_y(0), GX + GW, db_y(0)], fill=GRID_HI, width=1); d.text((GX + GW - 24, db_y(0) - 12), "0", font=font(9), fill=PRINT_DIM)
    for hz, lab in ((20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"), (2000, "2k"),
                    (5000, "5k"), (10000, "10k"), (20000, "20k")):
        x = hz_x(hz)
        if 20 < hz < 20000: d.line([x, GY, x, GY + GH], fill=GRID, width=1)
        text_c(d, min(max(x, GX + 10), GX + GW - 12), GY + GH + 8, lab, font(9), PRINT_DIM)
    # output section
    ox = OUT_X
    d.rounded_rectangle([ox - 8, GY - 6, W - 12, GY + GH + 20], radius=6, fill=PANEL, outline=(10, 12, 18), width=2)
    text_c(d, (ox + W - 12) / 2 - 4, GY + 10, "OUTPUT", font(11), PRINT)
    # band panels
    for b in range(7):
        x = panel_x(b)
        d.rounded_rectangle([x, PANEL_Y, x + PANEL_W, PANEL_Y + PANEL_H], radius=6, fill=PANEL, outline=(10, 12, 18))
        d.rectangle([x + 1, PANEL_Y, x + PANEL_W - 1, PANEL_Y + 3], fill=BANDCOL[b])
        text_c(d, x + 100, PANEL_Y + 30, f"BAND {b + 1}", font(13), BANDCOL[b])
        for k, lab in enumerate(("FREQ", "GAIN", "Q")):
            text_c(d, x + 22, PANEL_Y + 78 + k * 40, lab, font(9), PRINT_DIM)
        text_c(d, x + PANEL_W / 2, PANEL_Y + 204, "TYPE", font(9), PRINT_DIM)
        text_c(d, x + PANEL_W / 2, PANEL_Y + 262, "SLOPE (CUTS)", font(9), PRINT_DIM)
    x = panel_x(7)
    tw = d.textlength("EQ7", font=font(26))
    d.rounded_rectangle([x, PANEL_Y, W - 12, PANEL_Y + PANEL_H], radius=6, fill=PANEL, outline=(10, 12, 18))
    d.text(((x + W - 12 - tw) / 2, PANEL_Y + 20), "EQ7", font=font(26), fill=PRINT)
    text_c(d, (x + W - 12) / 2, PANEL_Y + 58, "7-BAND", font(10), PRINT_DIM)
    text_c(d, (x + W - 12) / 2, PANEL_Y + 72, "PARAMETRIC", font(10), PRINT_DIM)
    text_c(d, (x + W - 12) / 2, PANEL_Y + 120, "ANALYZER", font(9), PRINT_DIM)
    text_c(d, (x + W - 12) / 2, PANEL_Y + 186, "BYPASS", font(9), PRINT_DIM)
    return im

# ---------- TUI.json (GlueBus schema) ----------
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
def label(kind, h, colour, b, case="Original", name=None):
    return comp(name or kind, "Label", {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": float(h)},
                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": case}, "type": kind, "handleName": "Data"}, bounds(b))
def focus(b):
    return comp("Focus", "Focus", {"version": 1, "backgroundColour": "00000000", "outlineColour": FOCUS_COL, "backgroundInset": 1.0, "outlineThickness": 2.0},
                bounds(b, visible="WhenFocussed"))
CTRL = lambda: [action("Mouse Down", "Q-Link"), action("Double Click", "Show Overlay", "knob overlay"), action("Enter Pressed", "Show Overlay", "knob overlay")]
def strip_part(img, box_w, box_h, sq):   # a square filmstrip centred on a box (MPC clips it to the box)
    return comp("Knob", "Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": img, "numFrames": NUMFRAMES, "invert": False,
                "dragOrientation": "Vertical", "handleName": "Data"}, bounds(((box_w - sq) / 2, (box_h - sq) / 2, sq, sq)))

def build():
    if os.path.isdir(SKIN_DIR): shutil.rmtree(SKIN_DIR)
    os.makedirs(OUT)
    defs = []
    curve_strip().save(os.path.join(OUT, "eq_curve.png"), optimize=True)
    spec_strip().save(os.path.join(OUT, "eq_spec.png"), optimize=True)
    fader_strip().save(os.path.join(OUT, "eq_fader.png"), optimize=True)
    box_strip(BOX_W, BOX_H).save(os.path.join(OUT, "eq_box.png"), optimize=True)
    box_strip(TBOX_W, BOX_H).save(os.path.join(OUT, "eq_tbox.png"), optimize=True)

    # read-only graph columns (not touchable)
    defs.append({"key": "eqCurveCol", "value": definition([], [strip_part("eq_curve.png", COLW, GH, GH)], ignore=True)})
    defs.append({"key": "eqSpecBar", "value": definition([], [strip_part("eq_spec.png", 2 * COLW, GH, GH)], ignore=True)})
    # value boxes: a drag-to-change filmstrip with the plugin's value drawn on it
    for key, img, w in (("eqBox", "eq_box.png", BOX_W), ("eqTBox", "eq_tbox.png", TBOX_W)):
        defs.append({"key": key, "value": definition(CTRL(), [
            strip_part(img, w, BOX_H, SQ_BOX),
            label("Value", 14, hexcol(PRINT), (0, 0, w, BOX_H), case="Upper Case"),
            focus((0, 0, w, BOX_H))])})
    defs.append({"key": "eqFader", "value": definition(CTRL(), [
        focus((0, 0, 110, FADER_H + 30)),
        strip_part("eq_fader.png", 110, FADER_H, FADER_H),
        label("Value", 14, hexcol(PRINT), (0, FADER_H + 4, 110, 22), case="Upper Case")])})
    for b in range(7):
        power(True, BANDCOL[b]).save(os.path.join(OUT, f"eq_pwr_{b}_on.png")); power(False, BANDCOL[b]).save(os.path.join(OUT, f"eq_pwr_{b}_off.png"))
        defs.append({"key": f"eqPower{b}", "value": definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
            comp("Button", "Button", {"version": 2, "onImage": f"eq_pwr_{b}_on.png", "offImage": f"eq_pwr_{b}_off.png", "buttonId": 1,
                 "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, 40, 40))),
            focus((0, 0, 40, 40))])})
    for key, on_txt, off_txt, col in (("ana", "ANALYZER", "ANALYZER", CURVE), ("byp", "BYPASSED", "ACTIVE", (224, 96, 86))):
        pill(on_txt, True, col).save(os.path.join(OUT, f"eq_{key}_on.png")); pill(off_txt, False, col).save(os.path.join(OUT, f"eq_{key}_off.png"))
        defs.append({"key": f"eqPill_{key}", "value": definition([action("Mouse Down", "Q-Link"), action("Enter Pressed", "Toggle Switch")], [
            comp("Button", "Button", {"version": 2, "onImage": f"eq_{key}_on.png", "offImage": f"eq_{key}_off.png", "buttonId": 1,
                 "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}, bounds((0, 0, 110, 30))),
            focus((0, 0, 110, 30))])})

    background().save(os.path.join(OUT, "eq_bg.png"), optimize=True)
    comps = [comp("Background", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": "eq_bg.png"}, bounds((0, 0, W, H)))]
    for i in range(NSPEC):                          # analyzer first (behind), then the curve
        comps.append(comp(f"Spectrum {i}", "eqSpecBar", {"version": 1, "handleName": "Data"}, bounds((col_x(2 * i), GY, 2 * COLW, GH)), bindp(P_SPEC0 + i)))
    for i in range(NCURVE):
        comps.append(comp(f"Curve {i}", "eqCurveCol", {"version": 1, "handleName": "Data"}, bounds((col_x(i), GY, COLW, GH)), bindp(P_CURVE0 + i)))
    comps.append(comp("Output", "eqFader", {"version": 1, "handleName": "Data"},
                      bounds((OUT_X + (W - 12 - OUT_X - 110) / 2 - 4, GY + 24, 110, FADER_H + 30), focus="Yes", show="Hide"), bindp(P_OUTPUT)))
    for b in range(7):
        x = panel_x(b)
        comps.append(comp(f"Band {b + 1} On", f"eqPower{b}", {"version": 1, "handleName": "Data"},
                          bounds((x + 8, PANEL_Y + 10, 40, 40), focus="Yes", show="Hide"), bindp(bp(b, 0))))
        for k, w in enumerate((2, 3, 4)):            # freq, gain, q
            comps.append(comp(f"Band {b + 1} {('Freq', 'Gain', 'Q')[k]}", "eqBox", {"version": 1, "handleName": "Data"},
                              bounds((x + 42, PANEL_Y + 63 + k * 40, BOX_W, BOX_H), focus="Yes", show="Hide"), bindp(bp(b, w))))
        comps.append(comp(f"Band {b + 1} Type", "eqTBox", {"version": 1, "handleName": "Data"},
                          bounds((x + (PANEL_W - TBOX_W) / 2, PANEL_Y + 216, TBOX_W, BOX_H), focus="Yes", show="Hide"), bindp(bp(b, 1))))
        comps.append(comp(f"Band {b + 1} Slope", "eqTBox", {"version": 1, "handleName": "Data"},
                          bounds((x + (PANEL_W - TBOX_W) / 2, PANEL_Y + 274, TBOX_W, BOX_H), focus="Yes", show="Hide"), bindp(bp(b, 5))))
    x = panel_x(7); cx = (x + W - 12) / 2
    comps.append(comp("Analyzer", "eqPill_ana", {"version": 1, "handleName": "Data"}, bounds((cx - 55, PANEL_Y + 134, 110, 30), focus="Yes", show="Hide"), bindp(P_ANALYZER)))
    comps.append(comp("Bypass", "eqPill_byp", {"version": 1, "handleName": "Data"}, bounds((cx - 55, PANEL_Y + 200, 110, 30), focus="Yes", show="Hide"), bindp(P_BYPASS)))
    defs.append({"key": "EQ7|Main", "value": definition([], comps, "ff161a21")})
    tabs = [{"version": 3, "tabName": name, "fnKeyIndex": i, "fnKeySubIndex": 0, "qlinkBoundsData": ["0 0 0 0"],
             "componentName": "EQ7|Main", "initialSize": f"0 0 {W} {H}", "scale": 1.0} for i, name in enumerate(("GAIN / FREQ", "Q / TYPE"))]

    tui = {"pageData": {"version": 1,
        "componentDefinitions": {"version": 2, "importFiles": ["/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                                                                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
                                 "localComponentDefinitions": defs},
        "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=4)

    ql_a = [bp(b, 3) for b in range(7)] + [bp(b, 2) for b in range(7)] + [P_OUTPUT, P_BYPASS]      # gains, freqs
    ql_b = [bp(b, 4) for b in range(7)] + [bp(b, 1) for b in range(7)] + [P_OUTPUT, P_BYPASS]      # Qs, types
    qmap = lambda ids: {f"Q-Link {i + 1}": p for i, p in enumerate(ids)}
    q = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
         "Screen Mode Q-Links": {"version": 4, "map": [
             {"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql_a)},
             {"Tab": 2, "SubTab": 1, "Bank Direction": "Column", "Q-Links": qmap(ql_b)}]},
         "Program Mode Q-Links": qmap(ql_a)}
    for fn in ("Q-Links.json", "Q-Links - 8by1.json"): json.dump(q, open(os.path.join(OUT, fn), "w"), indent=4)
    open(os.path.join(SKIN_DIR, "version.xml"), "w").write(
        "<?xml version='1.0' encoding='utf-8'?>\n<plugincontent version=\"1.0\">\n\t<identifier>gluebus.vst.eq7</identifier>\n"
        f"\t<version>{VERSION}</version>\n</plugincontent>\n")
    print("skin written to", SKIN_DIR)

# ---------- preview: a "Kick Punch"-like curve over a drum-loop-like spectrum ----------
def preview(path):
    def resp(hz):
        u = math.log(hz / 20) / math.log(1000)
        return 4 * math.exp(-((u - .16) / .07) ** 2) - 5 * math.exp(-((u - .41) / .08) ** 2) + 4 * math.exp(-((u - .8) / .07) ** 2) \
               - 18 / (1 + math.exp(min(50.0, (hz - 30) / 4)))
    def spec(i):
        u = i / (NSPEC - 1); return -18 - 30 * u - 8 * math.sin(i * 1.7) ** 2 + 12 * math.exp(-((u - .15) / .1) ** 2)
    im = Image.open(os.path.join(OUT, "eq_bg.png")).convert("RGBA"); d = ImageDraw.Draw(im)
    sp = Image.open(os.path.join(OUT, "eq_spec.png")); cv = Image.open(os.path.join(OUT, "eq_curve.png"))
    for i in range(NSPEC):
        fr = round((spec(i) - SPEC_FLOOR) / -SPEC_FLOOR * (FRAMES - 1)); x0 = (GH - 2 * COLW) // 2
        im.alpha_composite(sp.crop((x0, fr * GH, x0 + 2 * COLW, (fr + 1) * GH)), (col_x(2 * i), GY))
    for i in range(NCURVE):
        hz = 20 * 1000 ** (i / (NCURVE - 1)); db = max(-CURVE_DB, min(CURVE_DB, resp(hz)))
        fr = round((db + CURVE_DB) / (2 * CURVE_DB) * (FRAMES - 1)); x0 = (GH - COLW) // 2
        im.alpha_composite(cv.crop((x0, fr * GH, x0 + COLW, (fr + 1) * GH)), (col_x(i), GY))
    fd = Image.open(os.path.join(OUT, "eq_fader.png")); fr = round(.5 * (FRAMES - 1)); fx0 = (FADER_H - FADER_W) // 2
    fxp = OUT_X + (W - 12 - OUT_X - 110) / 2 - 4
    im.alpha_composite(fd.crop((fx0, fr * FADER_H, fx0 + FADER_W, (fr + 1) * FADER_H)), (int(fxp + 55 - FADER_W / 2), GY + 24))
    text_c(d, fxp + 55, GY + 24 + FADER_H + 15, "+0.0 DB", font(12), PRINT)
    box = Image.open(os.path.join(OUT, "eq_box.png")).crop(((SQ_BOX - BOX_W) // 2, (SQ_BOX - BOX_H) // 2, (SQ_BOX + BOX_W) // 2, (SQ_BOX + BOX_H) // 2))
    tbox = Image.open(os.path.join(OUT, "eq_tbox.png")).crop(((SQ_BOX - TBOX_W) // 2, (SQ_BOX - BOX_H) // 2, (SQ_BOX + TBOX_W) // 2, (SQ_BOX + BOX_H) // 2))
    vals = [("28 HZ", "+0.0 DB", "0.71", "LOW CUT", "24 DB"), ("60 HZ", "+4.0 DB", "1.20", "PEAK", "24 DB"), ("350 HZ", "-5.0 DB", "1.50", "PEAK", "24 DB"),
            ("1.00 KHZ", "+0.0 DB", "1.00", "PEAK", "24 DB"), ("3.50 KHZ", "+4.0 DB", "1.40", "PEAK", "24 DB"), ("6.00 KHZ", "+0.0 DB", "1.00", "PEAK", "24 DB"),
            ("10.0 KHZ", "+0.0 DB", "0.71", "HIGH SHELF", "24 DB")]
    for b in range(7):
        x = panel_x(b)
        im.alpha_composite(Image.open(os.path.join(OUT, f"eq_pwr_{b}_on.png")), (x + 8, PANEL_Y + 10))
        for k in range(3):
            im.alpha_composite(box, (x + 42, PANEL_Y + 63 + k * 40)); text_c(d, x + 42 + BOX_W / 2, PANEL_Y + 63 + k * 40 + 15, vals[b][k], font(12), PRINT)
        for k, yy in ((3, 216), (4, 274)):
            im.alpha_composite(tbox, (int(x + (PANEL_W - TBOX_W) / 2), PANEL_Y + yy)); text_c(d, x + PANEL_W / 2, PANEL_Y + yy + 15, vals[b][k], font(12), PRINT)
    x = panel_x(7); cx = (x + W - 12) / 2
    im.alpha_composite(Image.open(os.path.join(OUT, "eq_ana_on.png")), (int(cx - 55), PANEL_Y + 134))
    im.alpha_composite(Image.open(os.path.join(OUT, "eq_byp_off.png")), (int(cx - 55), PANEL_Y + 200))
    im.convert("RGB").save(path)

if __name__ == "__main__":
    build()
    if len(sys.argv) > 1: preview(sys.argv[1])
