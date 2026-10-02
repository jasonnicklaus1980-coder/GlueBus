#!/usr/bin/env python3
"""Writes the demo sample: an original 8-bar loop at 96 BPM in A minor (kick, snare, hats, chords), 24-bit / 48 kHz.
   python3 tools/make_demo.py OUT.wav"""
import math, struct, sys, wave
sr, bpm, bars = 48000, 96.0, 8
beat = 60 / bpm; n = int(bars * 4 * beat * sr)
chords = [(220.0, 261.63, 329.63), (146.83, 174.61, 220.0), (164.81, 207.65, 246.94), (220.0, 261.63, 329.63)]   # Am Dm E Am
seed = 7
def noise():
    global seed
    seed = (seed * 1664525 + 1013904223) & 0xffffffff
    return (seed >> 9) / 4194304.0 - 1
frames = bytearray()
for i in range(n):
    t = i / sr; b = int(t / beat); tb = t - b * beat; e8 = int(t / (beat / 2)); te = t - e8 * beat / 2
    x = 0.0
    if b % 2 == 0: x += 0.45 * math.exp(-tb * 22) * math.sin(2 * math.pi * (48 + 90 * math.exp(-tb * 35)) * tb)
    else: x += 0.28 * math.exp(-tb * 18) * noise() + 0.1 * math.exp(-tb * 30) * math.sin(2 * math.pi * 190 * tb)
    x += 0.1 * math.exp(-te * (60 if e8 % 2 else 90)) * noise()
    c = chords[(b // 4) % 4]
    env = 0.6 + 0.4 * math.exp(-(t % (4 * beat)) * 1.5)
    for k, f in enumerate(c): x += 0.05 * env * (math.sin(2 * math.pi * f * t) + 0.3 * math.sin(2 * math.pi * 2 * f * t + k))
    bass = 0.18 * math.sin(2 * math.pi * c[0] / 2 * t) * (0.5 + 0.5 * math.exp(-tb * 4))
    l, r = x + bass, 0.94 * x + bass
    for v in (l, r): frames += struct.pack('<i', int(max(-1, min(1, v)) * 8388607))[:3]
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(2); w.setsampwidth(3); w.setframerate(sr); w.writeframes(bytes(frames))
print("wrote", sys.argv[1])
