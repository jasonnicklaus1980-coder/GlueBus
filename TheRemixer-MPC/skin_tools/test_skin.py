"""Structural checks for The Remixer skin: file layout, names, images, bindings and Q-Links.
These catch format and binding mistakes. They cannot show how the MPC draws the screen; that needs the device.
Usage: python3 skin_tools/test_skin.py [skin folder]"""
import json, os, re, sys
import xml.etree.ElementTree as ET
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SKIN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "mpc", "skin", "GlueBus - VST - The Remixer")
PLUGINS = 7                               # parameters 0..6 in src/remixer.cpp
fails = 0

def check(ok, msg):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + msg)
    if not ok:
        fails += 1

def bounds(s):
    return [int(v) for v in s.split()]

print("1. folder and names")
plugin_xml = ET.parse(os.path.join(ROOT, "mpc", "plugin.xml")).getroot()
man, name = plugin_xml.get("manufacturer"), plugin_xml.get("name")
folder = os.path.basename(SKIN)
check(folder == f"{man} - VST - {name}", f"skin folder '{folder}' is '<manufacturer> - VST - <name>' from plugin.xml")
check(os.path.isfile(os.path.join(SKIN, "version.xml")), "version.xml present")
ident = ET.parse(os.path.join(SKIN, "version.xml")).getroot().findtext("identifier")
check(bool(ident), f"version.xml identifier: {ident}")

D = os.path.join(SKIN, "Plugin Skins")
tui = json.load(open(os.path.join(D, "TUI.json")))["pageData"]
defs = {x["key"]: x["value"] for x in tui["componentDefinitions"]["localComponentDefinitions"]}
tab = tui["tabs"][0]
W, H = map(int, tab["initialSize"].split()[2:4])

print("2. tab and screen")
check(tab["componentName"] in defs, f"tab root '{tab['componentName']}' exists")
check((W, H) == (1280, 628), f"screen {W}x{H} is the MPC X screen size")

print("3. images exist and button images match their bounds")
root = defs[tab["componentName"]]
for c in root["componentsData"]:
    cd = c["componentData"]
    if cd["type"] == "Image":
        check(os.path.isfile(os.path.join(D, cd["data"]["image"])), f"background {cd['data']['image']} exists")
    if cd["type"] == "Button":
        for key in ("onImage", "offImage"):
            img = os.path.join(D, cd["data"][key])
            check(os.path.isfile(img), f"{cd['name']}: {cd['data'][key]} exists")
            if os.path.isfile(img):
                bw, bh = bounds(c["bounds"]["bounds"])[2:4]
                check(Image.open(img).size == (bw, bh), f"{cd['data'][key]} is {bw}x{bh}, the button bounds")

print("4. every control inside the screen and bound to a real parameter")
bound = {}
for c in root["componentsData"]:
    cd = c["componentData"]
    x, y, w, h = bounds(c["bounds"]["bounds"])
    if cd["type"] == "Image":
        continue
    check(0 <= x and 0 <= y and x + w <= W and y + h <= H, f"{cd['name']}: inside the screen")
    p = c["handle remapping"]["map"][0]["value"]
    n = int(p.split()[-1])
    check(0 <= n < PLUGINS, f"{cd['name']} binds to {p}")
    bound.setdefault(n, []).append(cd["name"])
print("     parameters on screen:", {k: v for k, v in sorted(bound.items())})
check(bound.get(0) == ["Record"], "Record (parameter 0) is the only button on the screen")
check(all(t == "Name" or t == "Value" for t in
          [c["componentData"]["data"]["type"] for c in root["componentsData"] if c["componentData"]["type"] == "Label"]),
      "labels show a parameter name or value")

print("5. Q-Links point at this plugin's parameters")
for fn in ("Q-Links.json", "Q-Links - 8by1.json"):
    q = json.load(open(os.path.join(D, fn)))
    targets = [v for m in q["Screen Mode Q-Links"]["map"] for v in m["Q-Links"].values()]
    check(all(0 <= t < PLUGINS for t in targets), f"{fn}: targets {sorted(set(targets))} are parameters 0..{PLUGINS - 1}")

print("\n" + ("SKIN CHECKS PASSED" if not fails else f"{fails} SKIN CHECK(S) FAILED"))
sys.exit(1 if fails else 0)
