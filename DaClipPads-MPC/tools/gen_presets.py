#!/usr/bin/env python3
"""Factory presets for Da Clip Pads -> src/PresetSystem/factory.inc (compiled in) and presets/factory.json (docs).

A preset is a performance setup, not samples: tempo / swing / quantise, the SAMPLER stage, the master FX and how
each row of pads behaves (trigger mode, launch quantise, pitch mode, stretch, filter, sends, follow actions).
Loading one keeps the loaded samples, their start/end points, slices, volume, pan and MIDI notes.

Rows (MPC pad layout, clip 1 = bottom left like pad A01):
  row 0 = clips 1-4   row 1 = clips 5-8   row 2 = clips 9-12   row 3 = clips 13-16
Keys: p.<param key> (global), all.<clip field>, row<R>.<clip field> - the same format as saved state.
"""
import json, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# sampler stage presets (index into the plugin's list) and their values, so the preset text is explicit
SMP = {
    "Clean":     dict(smppreset=1, bits=24, rate=48000, aa=1.0, quantize=0.0, sat=0.0, noise=0.0, crackle=0.0, smpout=0),
    "12-Bit":    dict(smppreset=2, bits=12, rate=32000, aa=1.0, quantize=1.0, sat=0.10, noise=0.0, crackle=0.0, smpout=0),
    "SP Style":  dict(smppreset=3, bits=12, rate=26040, aa=0.0, quantize=1.0, sat=0.25, noise=0.06, crackle=0.0, smpout=0),
    "MPC Style": dict(smppreset=4, bits=12, rate=40000, aa=0.7, quantize=1.0, sat=0.35, noise=0.03, crackle=0.0, smpout=0),
    "Vinyl":     dict(smppreset=5, bits=16, rate=44100, aa=1.0, quantize=0.5, sat=0.30, noise=0.20, crackle=0.45, smpout=0),
    "Dusty":     dict(smppreset=6, bits=12, rate=22050, aa=0.8, quantize=1.0, sat=0.40, noise=0.30, crackle=0.20, smpout=0),
    "Crushed":   dict(smppreset=7, bits=6, rate=8000, aa=0.0, quantize=1.0, sat=0.60, noise=0.08, crackle=0.0, smpout=-3),
}
# clip field enums (must match src/ClipEngine/Clip.h)
ONESHOT, LOOP, GATE, TOGGLE = 0, 1, 2, 3
RESAMPLE, STRETCH = 0, 1
SL = dict(off=0, auto=1, q=2, h=3, bar=4, bar2=5, bar4=6, bar8=7)
DRUMS, INSTR, VOCALS, LOOPS = 0, 1, 2, 3
FOFF, LP, HP, BP = 0, 1, 2, 3
FA = dict(off=0, stop=1, repeat=2, next=3, prev=4, random=5, rscene=6, cont=7, scene=8)
FT = dict(end=0, beat=1, beat2=2, bar=3, bar2=4, bar4=5, bar8=6, bar16=7)
# clip quant: 0 = global, else Quant + 1: None=1, 1/4=2, 1/8=3, 1/8T=4, 1/16=5, 1/16T=6, 1 Bar=7, 2 Bars=8, 4 Bars=9
CQ = dict(glob=0, none=1, q4=2, q8=3, q8t=4, q16=5, q16t=6, bar=7, bar2=8, bar4=9)
# global quant: None=0, 1/4=1, 1/8=2, 1/8T=3, 1/16=4, 1/16T=5, 1 Bar=6, 2 Bars=7, 4 Bars=8
GQ = dict(none=0, q4=1, q8=2, q8t=3, q16=4, q16t=5, bar=6, bar2=7, bar4=8)
DLY = {"1/16": 0, "1/8T": 1, "1/8": 2, "1/8D": 3, "1/4": 4, "1/4D": 5, "1/2": 6, "1 Bar": 7}

BASE_MASTER = dict(msat=0.0, cthresh=-12, cratio=1.0, cattack=10, crelease=150, cmakeup=0, eqlow=0, eqmid=0, eqmidf=1000, eqhigh=0,
                   revsize=0.5, revdamp=0.5, revret=0.5, dlytime=3, dlyfb=0.35, dlytone=0.5, dlyret=0.5, limit=1)
BASE_CLIP = dict(mode=LOOP, quant=CQ["glob"], pmode=RESAMPLE, slen=SL["off"], stype=LOOPS, semis=0, reverse=0, filt=FOFF, cutoff=20000,
                 res=0, drive=0, rsend=0, dsend=0, vintage=1, follow=FA["off"], ftime=FT["end"], fadein=0, fadeout=0, xfade=5)

# the usual pad roles; presets start from one of these and change what they need
ROLES = {
    "break":  dict(mode=LOOP, quant=CQ["bar"], pmode=STRETCH, slen=SL["auto"], stype=DRUMS),
    "loop":   dict(mode=LOOP, quant=CQ["bar"], pmode=STRETCH, slen=SL["auto"], stype=LOOPS),
    "keys":   dict(mode=LOOP, quant=CQ["bar"], pmode=STRETCH, slen=SL["auto"], stype=INSTR),
    "shot":   dict(mode=ONESHOT, quant=CQ["q16"], pmode=RESAMPLE, slen=SL["off"], stype=DRUMS),
    "stab":   dict(mode=ONESHOT, quant=CQ["q8"], pmode=RESAMPLE, slen=SL["off"], stype=INSTR, rsend=0.15),
    "vocal":  dict(mode=GATE, quant=CQ["q16"], pmode=STRETCH, slen=SL["off"], stype=VOCALS, rsend=0.25, dsend=0.15),
    "fx":     dict(mode=TOGGLE, quant=CQ["bar"], pmode=STRETCH, slen=SL["auto"], stype=LOOPS, rsend=0.3, dsend=0.2),
    "sploop": dict(mode=LOOP, quant=CQ["bar"], pmode=RESAMPLE, slen=SL["auto"], stype=LOOPS),   # pitch follows tempo
    "spshot": dict(mode=ONESHOT, quant=CQ["q16"], pmode=RESAMPLE, slen=SL["off"], stype=DRUMS),
}

PRESETS = []
def preset(cat, name, tempo, swing, quant, smp, rows, master=None, glob=None, allc=None, desc=""):
    g = dict(tempo=tempo, swing=swing, quant=GQ[quant], smpon=1 if smp else 0)
    g.update(SMP[smp or "Clean"])
    m = dict(BASE_MASTER); m.update(master or {}); g.update(m)
    g.update(dict(sceneq=0, scenefollow=0, scenebars=2))
    g.update(glob or {})
    lines = [f"p.{k}={v}" for k, v in g.items()]
    base = dict(BASE_CLIP); base.update(allc or {})
    lines += [f"all.{k}={v}" for k, v in base.items()]
    for r, row in enumerate(rows):
        role, extra = (row, {}) if isinstance(row, str) else row
        d = dict(ROLES[role]); d.update(extra)
        lines += [f"row{r}.{k}={v}" for k, v in d.items()]
    PRESETS.append(dict(category=cat, name=name, description=desc, text="\n".join(lines)))

# ---------------- Boom Bap
preset("Boom Bap", "Dusty Breaks 90", 90, 56, "bar", "SP Style", ["break", "sploop", "spshot", "stab"],
       dict(msat=0.2, cratio=3, cthresh=-14, cmakeup=3, eqlow=2), desc="SP-style 12-bit breaks, loops pitched with the tempo")
preset("Boom Bap", "45 On 33", 88, 55, "bar", "SP Style", [("sploop", dict(semis=-5)), ("sploop", dict(semis=-5)), ("spshot", dict(semis=-5)), ("stab", dict(semis=-5))],
       dict(msat=0.25, cratio=3, cmakeup=3), desc="Everything down 5 semitones in resample mode: the classic 45-at-33 slow-down")
preset("Boom Bap", "Chop Shop 93", 93, 58, "q16", "MPC Style", ["shot", "shot", ("stab", dict(quant=CQ["q16"])), "break"],
       dict(cratio=4, cthresh=-16, cmakeup=4, eqlow=1.5), desc="Chops on the first three rows, 1/16 launch, a break on top")
preset("Boom Bap", "Kick Snare Swing", 92, 62, "q16", "MPC Style", ["shot", "shot", "stab", "loop"],
       dict(cratio=4, cthresh=-18, cmakeup=5), desc="Heavy swing on the 1/16 grid for finger drumming the one-shots")
preset("Boom Bap", "Filtered Bass Loop", 90, 55, "bar", "12-Bit", ["break", ("loop", dict(filt=LP, cutoff=900, res=0.3)), "spshot", "stab"],
       dict(cratio=3, cmakeup=2, eqlow=2.5), desc="Row 2 loops low-passed at 900 Hz: play the bass line out of any loop")
preset("Boom Bap", "Golden Era Kit", 95, 57, "bar", "SP Style", ["break", "sploop", "spshot", ("vocal", dict(mode=ONESHOT))],
       dict(msat=0.3, cratio=2.5, cmakeup=2, revret=0.3, revsize=0.3), desc="Breaks, SP loops, one-shots and vocal scratches/cuts")
# ---------------- Hip-Hop
preset("Hip-Hop", "Trap Bounce 140", 140, 50, "bar", "Clean", ["break", "loop", ("shot", dict(quant=CQ["q16t"])), "vocal"],
       dict(cratio=4, cthresh=-14, cmakeup=4, eqlow=3, eqhigh=1.5), desc="Half-time loops, triplet one-shots for hats and rolls")
preset("Hip-Hop", "Drill Slides 142", 142, 50, "bar", "Clean", ["break", "keys", ("shot", dict(quant=CQ["q16t"])), ("stab", dict(pmode=RESAMPLE))],
       dict(cratio=3.5, cmakeup=3, eqlow=2.5), desc="Triplet feel, dark keys stretched to tempo")
preset("Hip-Hop", "West Coast Funk 96", 96, 58, "bar", "12-Bit", ["break", "keys", "stab", ("loop", dict(filt=HP, cutoff=400))],
       dict(msat=0.15, cratio=3, cmakeup=3, eqhigh=2), desc="Funky loops, stabs and a high-passed lead row")
preset("Hip-Hop", "Memphis Tape 70", 70, 54, "bar", "Dusty", ["break", "sploop", "spshot", "vocal"],
       dict(msat=0.35, cratio=3, cmakeup=3, eqlow=3, eqhigh=-3), desc="Slow, dark and saturated")
preset("Hip-Hop", "Sample Flip 86", 86, 55, "bar", "MPC Style", ["loop", "loop", "shot", ("fx", dict(follow=FA["random"], ftime=FT["bar2"]))],
       dict(cratio=3, cmakeup=3), desc="Top row picks a random clip every 2 bars")
preset("Hip-Hop", "Arena Drums 150", 150, 50, "bar", "Clean", ["break", "loop", "shot", "fx"],
       dict(msat=0.2, cratio=5, cthresh=-20, cmakeup=6, eqlow=2, eqhigh=2, revret=0.4, revsize=0.7), desc="Big compressed drums with a long room")
# ---------------- Lo-Fi
preset("Lo-Fi", "Late Night Study", 78, 58, "bar", "Vinyl", ["break", "keys", "stab", "vocal"],
       dict(eqhigh=-4, eqlow=1.5, cratio=2.5, cmakeup=2, revret=0.35), dict(), dict(filt=LP, cutoff=7000), desc="Vinyl crackle and a gentle top-end roll-off")
preset("Lo-Fi", "Tape Warble Keys", 75, 56, "bar", "Dusty", ["break", ("keys", dict(cents=-15)), ("stab", dict(cents=-15)), "fx"],
       dict(msat=0.3, eqhigh=-5, cratio=2, revret=0.4), desc="Keys detuned a little flat, dusty 22 kHz sampler")
preset("Lo-Fi", "Rainy Loops", 72, 60, "bar", "Vinyl", ["break", "keys", "keys", ("fx", dict(rsend=0.6))],
       dict(eqhigh=-3, revret=0.6, revsize=0.8, revdamp=0.7), desc="Washed loops, lots of dark reverb")
preset("Lo-Fi", "Cassette Chops", 84, 57, "q8", "Dusty", ["shot", "shot", "stab", "loop"],
       dict(msat=0.4, eqhigh=-4, eqlow=1), desc="8th-note chops through a dusty, saturated sampler")
preset("Lo-Fi", "Bedroom Beats", 82, 55, "bar", "12-Bit", ["break", "keys", "shot", "vocal"],
       dict(eqhigh=-2, cratio=2.5, cmakeup=2, dlyret=0.3, dlytime=DLY["1/8D"]), desc="Clean 12-bit, a dotted delay on vocals and stabs")
preset("Lo-Fi", "Vinyl Sim 45", 80, 56, "bar", "Vinyl", [("sploop", dict(semis=-2)), ("sploop", dict(semis=-2)), "spshot", "stab"],
       dict(eqhigh=-3, msat=0.2), desc="Loops a whole step down in resample mode, vinyl noise")
# ---------------- Soul
preset("Soul", "Chipmunk Soul", 88, 56, "bar", "MPC Style", [("sploop", dict(semis=5)), ("sploop", dict(semis=7)), ("stab", dict(semis=5)), "vocal"],
       dict(msat=0.2, cratio=3, cmakeup=3), desc="Sped-up soul: loops up 5 and 7 semitones in resample mode")
preset("Soul", "Motown Stabs", 96, 55, "q8", "12-Bit", ["break", "loop", ("stab", dict(rsend=0.3)), ("stab", dict(rsend=0.3, semis=-12))],
       dict(revret=0.5, revsize=0.4), desc="Horn and string stabs, an octave-down row")
preset("Soul", "Gospel Chords", 72, 54, "bar", "Clean", ["break", "keys", "keys", "vocal"],
       dict(revret=0.6, revsize=0.7, cratio=2, cmakeup=2), dict(), dict(rsend=0.2), desc="Long chord loops and vocals with a hall")
preset("Soul", "Northern Soul 120", 120, 52, "bar", "Vinyl", ["break", "loop", "stab", "vocal"],
       dict(eqhigh=-1.5, msat=0.2), desc="Up-tempo loops through vinyl")
preset("Soul", "Soul Flip Chain", 90, 56, "bar", "MPC Style", [("loop", dict(follow=FA["next"], ftime=FT["bar4"])), ("loop", dict(follow=FA["next"], ftime=FT["bar4"])), "stab", "vocal"],
       dict(cratio=3, cmakeup=3), desc="Loops on rows 1-2 hand over to the next clip every 4 bars")
preset("Soul", "Warm Rhodes", 84, 55, "bar", "12-Bit", ["break", ("keys", dict(filt=LP, cutoff=4500, res=0.15)), "stab", "vocal"],
       dict(msat=0.25, eqlow=1, eqhigh=-2), desc="Warm, rounded keys")
# ---------------- R&B
preset("R&B", "Slow Jam 68", 68, 54, "bar", "Clean", ["break", "keys", "stab", "vocal"],
       dict(revret=0.5, revsize=0.6, cratio=3, cmakeup=3, eqhigh=1.5), desc="Smooth and wide, vocals with reverb and delay")
preset("R&B", "90s Swing 100", 100, 64, "q16", "MPC Style", ["break", "keys", "shot", "vocal"],
       dict(cratio=3.5, cmakeup=3), desc="New-jack swing: 64 % swing on the 1/16 grid")
preset("R&B", "Neo Soul Pocket", 88, 60, "bar", "12-Bit", ["break", ("keys", dict(cents=-10)), "stab", "vocal"],
       dict(msat=0.15, cratio=2.5, cmakeup=2), desc="Lazy swing, keys slightly flat")
preset("R&B", "Vocal Chops", 95, 55, "q16", "Clean", [("vocal", dict(mode=ONESHOT)), ("vocal", dict(mode=ONESHOT, semis=12)), ("vocal", dict(mode=ONESHOT, semis=-5)), "keys"],
       dict(dlyret=0.4, dlytime=DLY["1/8"], revret=0.4), desc="Three rows of vocal chops, one up an octave, one down a fourth")
preset("R&B", "Late Night Groove", 80, 57, "bar", "Vinyl", ["break", "keys", "stab", "vocal"],
       dict(eqhigh=-1, revret=0.45), desc="Soft vinyl texture")
preset("R&B", "Glossy 2000s", 92, 52, "bar", "Clean", ["break", "loop", "shot", "vocal"],
       dict(cratio=4, cthresh=-18, cmakeup=5, eqhigh=3, eqlow=2), desc="Bright, compressed, clean")
# ---------------- Electronic
preset("Electronic", "House 124", 124, 54, "bar", "Clean", ["break", "loop", ("shot", dict(quant=CQ["q4"])), "fx"],
       dict(cratio=3, cmakeup=3, eqlow=2), desc="Four-to-the-floor loops, 1/4-quantised hits")
preset("Electronic", "Techno Tools 130", 130, 50, "bar", "Clean", ["break", ("loop", dict(filt=HP, cutoff=200)), "shot", ("fx", dict(dsend=0.4))],
       dict(cratio=4, cthresh=-16, cmakeup=4, dlytime=DLY["1/8D"], dlyfb=0.5), desc="Tools and a dotted delay")
preset("Electronic", "Garage Skip 132", 132, 62, "q16", "12-Bit", ["break", "keys", "shot", "vocal"],
       dict(cratio=3, cmakeup=3), desc="2-step swing")
preset("Electronic", "Jungle Breaks 170", 170, 50, "bar", "SP Style", [("break", dict(stype=DRUMS)), ("sploop", dict(semis=3)), "spshot", "fx"],
       dict(msat=0.2, cratio=3, cmakeup=3), desc="Breaks stretched to 170, one row pitched up in resample mode")
preset("Electronic", "Downtempo 100", 100, 55, "bar", "Vinyl", ["break", "keys", "stab", "fx"],
       dict(revret=0.55, revsize=0.7, dlyret=0.3), desc="Slow, spacious")
preset("Electronic", "Synthwave 110", 110, 50, "bar", "Clean", ["break", "keys", "stab", "fx"],
       dict(revret=0.6, revsize=0.9, dlyret=0.35, dlytime=DLY["1/4D"]), desc="Big reverb and dotted-quarter delay")
# ---------------- Experimental
preset("Experimental", "Random Machine", 100, 50, "bar", "12-Bit", [("loop", dict(follow=FA["random"], ftime=FT["bar"])), ("loop", dict(follow=FA["random"], ftime=FT["bar"])), ("shot", dict(follow=FA["random"], ftime=FT["beat"])), ("fx", dict(follow=FA["random"], ftime=FT["bar2"]))],
       desc="Every clip hands over to a random clip: press one pad and let it run")
preset("Experimental", "Reverse World", 90, 54, "bar", "Dusty", ["loop", "keys", "stab", "vocal"], dict(revret=0.6, revsize=0.8), None, dict(reverse=1),
       desc="Every clip reversed")
preset("Experimental", "Stutter Grid", 120, 50, "q16", "Crushed", [("loop", dict(mode=GATE)), ("loop", dict(mode=GATE)), ("shot", dict(mode=GATE)), ("fx", dict(mode=GATE))],
       desc="Gate mode everywhere: hold pads to stutter, 1/16 launch")
preset("Experimental", "Octave Ladder", 95, 50, "bar", "12-Bit", [("keys", dict(semis=-12)), ("keys", dict(semis=-5)), ("keys", dict(semis=7)), ("keys", dict(semis=12))],
       dict(revret=0.5), desc="Same idea across rows at -12, -5, +7, +12 semitones (time kept)")
preset("Experimental", "Crushed Noise", 110, 50, "q8", "Crushed", ["break", "loop", "shot", "fx"], dict(msat=0.5, eqlow=3),
       desc="6-bit / 8 kHz destruction")
preset("Experimental", "Band Pass Sweep", 100, 50, "bar", "Clean", ["break", ("loop", dict(filt=BP, cutoff=1200, res=0.6)), ("keys", dict(filt=BP, cutoff=600, res=0.5)), "fx"],
       dict(dlyret=0.4), desc="Resonant band-pass rows: sweep CUTOFF on a Q-Link")
# ---------------- Vintage Sampler
preset("Vintage Sampler", "SP Style 26k", 92, 56, "bar", "SP Style", ["break", "sploop", "spshot", "stab"], dict(msat=0.2),
       desc="12-bit, 26.04 kHz, no anti-alias filter: gritty aliasing")
preset("Vintage Sampler", "MPC Style 40k", 92, 56, "bar", "MPC Style", ["break", "sploop", "spshot", "stab"], dict(cratio=3, cmakeup=2),
       desc="12-bit 40 kHz with some filtering and warmth")
preset("Vintage Sampler", "12-Bit Clean", 95, 54, "bar", "12-Bit", ["break", "loop", "shot", "stab"], desc="12-bit / 32 kHz with the anti-alias filter on")
preset("Vintage Sampler", "Dusty Crate", 88, 57, "bar", "Dusty", ["break", "sploop", "spshot", "vocal"], desc="22 kHz, noise and saturation")
preset("Vintage Sampler", "Vinyl Transfer", 90, 55, "bar", "Vinyl", ["break", "loop", "shot", "vocal"], desc="16-bit with hiss and crackle")
preset("Vintage Sampler", "Clean Digital", 100, 50, "bar", "Clean", ["break", "loop", "shot", "vocal"], desc="Sampler stage transparent")
# ---------------- Live Performance
preset("Live Performance", "Scene Jam", 95, 55, "bar", "MPC Style", ["break", "loop", "shot", "fx"],
       dict(cratio=3, cmakeup=3), dict(scenefollow=0), desc="Store scenes from what's playing and launch them on the bar")
preset("Live Performance", "Auto Arrange", 95, 55, "bar", "MPC Style", ["break", "loop", "shot", "fx"],
       dict(cratio=3, cmakeup=3), dict(scenefollow=1, scenebars=3), desc="Scenes follow each other every 8 bars")
preset("Live Performance", "DJ Transitions", 124, 50, "bar4", "Clean", [("loop", dict(quant=CQ["bar4"])), ("loop", dict(quant=CQ["bar4"])), ("fx", dict(quant=CQ["bar"])), "vocal"],
       dict(cratio=3, cmakeup=2, eqlow=1.5), desc="Loops launch on 4-bar phrases")
preset("Live Performance", "Finger Drum", 96, 57, "none", "MPC Style", [("shot", dict(quant=CQ["none"])), ("shot", dict(quant=CQ["none"])), ("shot", dict(quant=CQ["none"])), "loop"],
       dict(cratio=4, cthresh=-16, cmakeup=4), desc="No quantise on one-shots: play them live")
preset("Live Performance", "Loop Toggle Set", 100, 52, "bar", "Clean", [("loop", dict(mode=TOGGLE)), ("loop", dict(mode=TOGGLE)), ("keys", dict(mode=TOGGLE)), ("fx", dict(mode=TOGGLE))],
       desc="Press to start, press again to stop, on the bar")
preset("Live Performance", "Build And Drop", 128, 50, "bar", "Clean", ["break", ("loop", dict(filt=HP, cutoff=20)), "shot", ("fx", dict(rsend=0.5, dsend=0.4))],
       dict(cratio=4, cmakeup=4, dlyret=0.5, revret=0.6, dlytime=DLY["1/8"]), desc="Sweep row 2's high-pass up for builds, drop back to 20 Hz")

assert len(PRESETS) >= 50, len(PRESETS)

def c_str(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
with open(os.path.join(ROOT, "src", "PresetSystem", "factory.inc"), "w") as f:
    f.write("// Generated by tools/gen_presets.py - do not edit.\n")
    for p in PRESETS:
        f.write(f"{{ {c_str(p['category'])}, {c_str(p['name'])}, {c_str(p['text'])} }},\n")
os.makedirs(os.path.join(ROOT, "presets"), exist_ok=True)
with open(os.path.join(ROOT, "presets", "factory.json"), "w") as f:
    json.dump([dict(category=p["category"], name=p["name"], description=p["description"],
                    settings=dict(l.split("=", 1) for l in p["text"].split("\n"))) for p in PRESETS], f, indent=2)
print(f"{len(PRESETS)} presets written")
