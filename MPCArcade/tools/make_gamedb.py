#!/usr/bin/env python3
"""Build the library's game database (gamedb.tsv) from this MAME build's -listxml and data/genres.txt.
   python3 tools/make_gamedb.py listxml.xml out/gamedb.tsv
Columns (tab separated): name parent romof description year manufacturer driver genre rotate width height status
savestate players buttons controls isbios. Devices and non-runnable machines are left out."""
import os, sys
import xml.etree.ElementTree as E

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src, dst = sys.argv[1], sys.argv[2]
drvGenre, nameGenre = {}, {}
for line in open(os.path.join(ROOT, "data", "genres.txt"), encoding="utf-8"):
    line = line.strip()
    if not line or line.startswith("#"): continue
    key, genre = line.split(" ", 1)
    if key.startswith("@"): drvGenre[key[1:]] = genre
    else: nameGenre[key] = genre

clean = lambda s: (s or "").replace("\t", " ").replace("\n", " ").strip()
rows, missingGenre = [], []
machines = {m.get("name"): m for m in E.parse(src).getroot().findall("machine")}
for name, m in machines.items():
    if m.get("isdevice") == "yes" or m.get("runnable") == "no": continue
    parent = m.get("cloneof") or ""
    driver = os.path.splitext(os.path.basename(m.get("sourcefile") or ""))[0]
    genre = nameGenre.get(name) or nameGenre.get(parent) or drvGenre.get(driver) or "Action"
    if name not in nameGenre and parent not in nameGenre and driver not in drvGenre: missingGenre.append(name)
    disp = m.find("display")
    rotate, w, h = (int(disp.get("rotate", "0")), int(disp.get("width", "0") or 0), int(disp.get("height", "0") or 0)) if disp is not None else (0, 0, 0)
    drv = m.find("driver")
    status = (drv.get("status", "good") if drv is not None else "good")[0]
    savestate = "1" if drv is not None and drv.get("savestate") == "supported" else "0"
    inp = m.find("input")
    players, buttons, controls = 0, 0, []
    if inp is not None:
        players = int(inp.get("players", "0"))
        for c in inp.findall("control"):
            buttons = max(buttons, int(c.get("buttons", "0") or 0))
            t = c.get("type", "") + (c.get("ways", "") if c.get("type") in ("joy", "doublejoy") else "")
            if t not in controls: controls.append(t)
    rows.append([name, parent, m.get("romof") or "", clean(m.findtext("description")), clean(m.findtext("year")),
                 clean(m.findtext("manufacturer")), driver, genre, str(rotate), str(w), str(h), status, savestate,
                 str(players), str(buttons), ",".join(controls), "1" if m.get("isbios") == "yes" else "0"])
rows.sort(key=lambda r: r[0])
# ROM checksums for identifying misnamed zips: "crc set nroms" per ROM a set needs (its own + parent's + BIOS)
crcs = []
for r in rows:
    m = machines[r[0]]
    roms = [x for x in m.findall("rom") if x.get("crc") and x.get("status") != "nodump"]
    for x in roms: crcs.append(f"{x.get('crc')}\t{r[0]}\t{len(roms)}")
crcs.sort()
os.makedirs(os.path.dirname(os.path.abspath(dst)), exist_ok=True)
with open(dst, "w", encoding="utf-8") as f:
    for r in rows: f.write("\t".join(r) + "\n")
with open(os.path.join(os.path.dirname(os.path.abspath(dst)), "crcdb.tsv"), "w") as f: f.write("\n".join(crcs) + "\n")
parents = sum(1 for r in rows if not r[1] and r[16] == "0")
print(f"{dst}: {len(crcs)} ROM checksums, {len(rows)} sets ({parents} parents); default genre used for {len(missingGenre)}: {' '.join(missingGenre[:12])}")
