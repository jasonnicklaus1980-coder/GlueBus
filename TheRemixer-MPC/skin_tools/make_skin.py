#!/usr/bin/env python3
"""Builds the Plugin Skins folder for The Remixer (Phase 1 screen: recording).

The folder name must be "<manufacturer> - VST - <name>" from plugin.xml, or the MPC won't find it:
    mpc/skin/GlueBus - VST - The Remixer/Plugin Skins/   (TUI.json, Q-Links*.json, PNGs)
    mpc/skin/GlueBus - VST - The Remixer/version.xml

Structure follows the working skins in this repo (ASR-10, Da Lufs Plug): one tab, a full-size Image background,
Button (Instant) for Record, and Label (type Name or Value) bound to plugin parameters with "handle remapping".
Parameter numbers are the plugin's (src/remixer.cpp enum): 0 Record, 1 Peak L, 2 Peak R, 3 Clips, 4 Rec Time,
5 Dropouts, 6 Status.

Run from TheRemixer-MPC/: python3 skin_tools/make_skin.py
"""
import json, os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SKIN = os.path.join(ROOT, "mpc", "skin", "GlueBus - VST - The Remixer")
OUT = os.path.join(SKIN, "Plugin Skins")
W, H = 1280, 628

# palette: dark graphite, titanium text, one warm accent for recording
BG_TOP, BG_BOT = (24, 25, 28), (12, 12, 14)
TITLE = (226, 227, 224)
MUTED = (140, 142, 146)
ACCENT = (232, 84, 62)

def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:          # older Pillow
        return ImageFont.load_default()

def background(path):
    img = Image.new("RGB", (W, H), BG_TOP)
    d = ImageDraw.Draw(img)
    for y in range(H):                      # vertical gradient, very subtle
        t = y / (H - 1)
        c = tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3))
        d.line([(0, y), (W, y)], fill=c)
    d.text((60, 44), "THE REMIXER", font=font(44), fill=TITLE)
    d.text((60, 104), "RECORD  |  PHASE 1", font=font(18), fill=MUTED)
    d.line([(60, 140), (W - 60, 140)], fill=(60, 62, 66), width=2)
    img.save(path)

def button(path, on):
    w, h = 260, 120
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    fill = ACCENT if on else (44, 46, 50)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=14, fill=fill, outline=(90, 92, 96), width=2)
    d.text((w // 2, h // 2), "REC", font=font(30), fill=TITLE, anchor="mm")
    img.save(path)

# ---- TUI layout -------------------------------------------------------------------------------
TEXT_NAME = {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
             "style": "SemiBold", "height": 16.0}, "colour": "ffa6a8ac", "justification": "left verticallyCentred",
             "case": "Upper Case"}}
TEXT_VALUE = {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web",
              "style": "SemiBold", "height": 36.0}, "colour": "ffe4e6ec", "justification": "left verticallyCentred",
              "case": "Original"}}

def bounds(x, y, w, h):
    return {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
            "boundsType": "Absolute", "bounds": f"{x} {y} {w} {h}", "additionalInvalidatingHandles": []}

def remap(param):
    return {"version": 1, "map": [{"key": "Data", "value": f"Parameter {param}"}]}

def label(name, kind, param, x, y, w, h):
    style = TEXT_NAME if kind == "Name" else TEXT_VALUE
    data = dict(style, type=kind, handleName="Data")
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": "Label",
            "data": {"version": 1, **data}},
            "handle remapping": remap(param), "bounds": bounds(x, y, w, h)}

def record_button(x, y):
    return {"version": 2, "componentData": {"version": 1, "name": "Record", "type": "Button",
            "data": {"version": 2, "onImage": "rm_rec_on.png", "offImage": "rm_rec_off.png", "buttonId": 0,
                     "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"}},
            "handle remapping": remap(0), "bounds": bounds(x, y, 260, 120)}

def background_image():
    return {"version": 2, "componentData": {"version": 1, "name": "Background", "type": "Image",
            "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": "rm_bg.png"}},
            "handle remapping": {"version": 1, "map": []}, "bounds": bounds(0, 0, W, H)}

READOUTS = [  # (name, parameter, x, y)
    ("Peak L", 1, 420, 180),
    ("Peak R", 2, 420, 300),
    ("Clips", 3, 820, 180),
    ("Rec Time", 4, 820, 300),
    ("Dropouts", 5, 420, 420),
]

def main_screen():
    comps = [background_image(), record_button(60, 180)]
    comps.append(label("Status name", "Name", 6, 60, 330, 300, 24))
    comps.append(label("Status", "Value", 6, 60, 356, 300, 50))
    for name, param, x, y in READOUTS:
        comps.append(label(f"{name} name", "Name", param, x, y, 300, 24))
        comps.append(label(name, "Value", param, x, y + 26, 300, 50))
    return {"version": 1, "actions": [], "backgroundData": {"version": 1,
            "focussed": {"version": 1, "colour": "ff18181a", "image": ""},
            "unfocussed": {"version": 1, "colour": "ff18181a", "image": ""}},
            "ignoreMousePresses": "No", "disableCoarseDataWheel": "No", "repeats": [],
            "hideQLinkBounds": "No", "componentsData": comps}

def tui():
    return {"pageData": {
        "version": 1,
        "info": {"version": 1, "type": "CompleteDescription"},
        "componentDefinitions": {
            "version": 1,
            "importFiles": [
                "/usr/share/Akai/Content/Synths/Generic/Generic Knob Overlay.json",
                "/usr/share/Akai/Content/Synths/Generic/Generic Menu Overlay.json"],
            "localComponentDefinitions": [{"key": "RM|Main", "value": main_screen()}]},
        "tabs": [{"version": 3, "tabName": "RECORD", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                  "qlinkBoundsData": ["0 0 0 0"], "componentName": "RM|Main",
                  "initialSize": f"0 0 {W} {H}", "scale": 1.0}]}}

def qlinks():
    # same shape as the Da Lufs Plug and ASR-10 files: 4 Q-Links on the screen, pointing at this plugin's parameters
    screen = {"Q-Link 1": 0, "Q-Link 2": 4, "Q-Link 3": 1, "Q-Link 4": 2}
    return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": [
                {"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": screen}]},
            "Program Mode Q-Links": screen}

def qlinks_8by1():
    return qlinks()

def main():
    os.makedirs(OUT, exist_ok=True)
    background(os.path.join(OUT, "rm_bg.png"))
    button(os.path.join(OUT, "rm_rec_off.png"), on=False)
    button(os.path.join(OUT, "rm_rec_on.png"), on=True)
    with open(os.path.join(OUT, "TUI.json"), "w") as f:
        json.dump(tui(), f, indent=4)
    with open(os.path.join(OUT, "Q-Links.json"), "w") as f:
        json.dump(qlinks(), f, indent=4)
    with open(os.path.join(OUT, "Q-Links - 8by1.json"), "w") as f:
        json.dump(qlinks_8by1(), f, indent=4)
    with open(os.path.join(SKIN, "version.xml"), "w") as f:
        f.write("<?xml version='1.0' encoding='utf-8'?>\n"
                "<plugincontent version=\"1.0\">\n"
                "\t<identifier>gluebus.vst.theremixer</identifier>\n"
                "\t<version>0.1.0.0</version>\n"
                "</plugincontent>\n")
    print("wrote", os.path.relpath(OUT, ROOT))

if __name__ == "__main__":
    main()
