#!/usr/bin/env python3
"""Build an off-device test MAME folder: gamedb, fake ROM zips (whose member CRCs match real sets, so the zip
identifier can be tested), a stand-in 'mame' that answers -verifyroms / -bench, and placeholder screenshots."""
import os, shutil, struct, sys, zipfile, zlib
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
dst = sys.argv[1]
shutil.rmtree(dst, ignore_errors=True)
for d in ("system/run", "roms", "bios", "screenshots", "import", "config/pads", "logs"): os.makedirs(os.path.join(dst, d), exist_ok=True)
shutil.copy(os.path.join(ROOT, "build/gamedb.tsv"), os.path.join(dst, "system"))
shutil.copy(os.path.join(ROOT, "build/crcdb.tsv"), os.path.join(dst, "system"))
crcs = {}
for line in open(os.path.join(ROOT, "build/crcdb.tsv")):
    c, s, n = line.split()
    crcs.setdefault(s, []).append(int(c, 16))

def forge(target):
    """4 bytes whose CRC32 (as a whole file) equals target: CRC is affine, solve for the last 4 bytes."""
    base = b"MPCARCADE-TEST:"
    # brute force over a 32-bit linear system: build the matrix from unit vectors
    def crc(data): return zlib.crc32(data) & 0xffffffff
    zero = crc(base + b"\0\0\0\0")
    cols = [crc(base + (1 << i).to_bytes(4, "little")) ^ zero for i in range(32)]
    # solve cols * x = target ^ zero over GF(2)
    want = target ^ zero
    rows = [(cols[i], 1 << i) for i in range(32)]
    basis = []  # gaussian elimination on (value, combo)
    for v, comb in rows:
        for bv, bc in basis:
            if v ^ bv < v: v ^= bv; comb ^= bc
        if v: basis.append((v, comb)); basis.sort(reverse=True)
    x, w = 0, want
    for bv, bc in basis:
        if w ^ bv < w: w ^= bv; x ^= bc
    assert w == 0
    data = base + x.to_bytes(4, "little"); assert crc(data) == target
    return data

def fakezip(path, sets, n=None):
    with zipfile.ZipFile(path, "w") as z:
        k = 0
        for s in sets:
            for c in crcs[s][: n or len(crcs[s])]:
                z.writestr(f"rom{k:03d}.bin", forge(c)); k += 1

games = ["pacman", "puckman", "mspacman", "galaga", "dkong", "sf2", "ffight", "kof98", "outrun", "robotron", "1942", "mslug", "tmnt", "joust", "gridlee", "polepos", "frogger", "ddragon"]
for g in games: fakezip(os.path.join(dst, "roms", g + ".zip"), [g], 3)
open(os.path.join(dst, "roms", "notarom.zip"), "wb").write(b"PK\5\6" + b"\0" * 18)
fakezip(os.path.join(dst, "import", "Street Fighter II CE.zip"), ["sf2ce"])
fakezip(os.path.join(dst, "import", "bubblebobble.zip"), ["bublbobl"])
# stand-in mame: -verifyroms NAME / -bench N NAME
open(os.path.join(dst, "system", "mame"), "w").write('''#!/bin/sh
# test stand-in for MAME: answers -verifyroms and -bench like MAME 0.242 does
for a; do last=$a; done
case " $* " in
  *" -verifyroms "*) set -- $*; while [ "$1" != "-verifyroms" ]; do shift; done; n=$2
     case $n in kof98|mslug) echo "$n    : 214-p1.p1 (2097152 bytes) - NOT FOUND"; echo "romset $n is bad"; exit 1;;
                ddragon) echo "ddragon  : 21j-1-5.26 (32768 bytes) - INCORRECT CHECKSUM:"; echo "romset ddragon is bad"; exit 1;;
                *) echo "romset $n is good"; exit 0;; esac;;
  *" -bench "*) case $last in sf2) echo "Average speed: 87.31% (19 seconds)";; outrun) echo "Average speed: 61.02% (19 seconds)";; *) echo "Average speed: 412.50% (19 seconds)";; esac;;
  *) echo "mame stand-in: $*" ;;
esac
''')
os.chmod(os.path.join(dst, "system", "mame"), 0o755)
# placeholder screenshots (clearly test art)
f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
for i, (g, w, h) in enumerate([("sf2", 384, 224), ("galaga", 224, 288), ("pacman", 224, 288), ("outrun", 320, 224), ("ffight", 384, 224), ("1942", 224, 256)]):
    im = Image.new("RGB", (w, h), (20 + i * 30 % 200, 30, 80 + i * 25)); d = ImageDraw.Draw(im)
    for k in range(0, w, 16): d.line([k, 0, k, h], fill=(60, 60 + i * 20, 120))
    for k in range(0, h, 16): d.line([0, k, w, k], fill=(60, 60 + i * 20, 120))
    d.rectangle([w // 4, h // 3, w * 3 // 4, h * 2 // 3], fill=(230, 200, 60))
    d.text((10, 10), "TEST ART: " + g, font=f, fill=(255, 255, 255))
    os.makedirs(os.path.join(dst, "screenshots", g), exist_ok=True)
    im.save(os.path.join(dst, "screenshots", g, "0000.png"))
print("test root ready:", dst)
