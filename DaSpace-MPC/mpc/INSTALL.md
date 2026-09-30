# Da Space 1.0.1

A spatial and reverb plugin for MPC with seven algorithms and 34 presets, including **High Sky**. It's a native MPC OS
VST2 insert effect. In the plugin browser it's **Da Space** by **RadioReady Audio**, and the screen's title reads
**Velvet Space** in blue script.

## Requirements
- A first-generation MPC OS standalone device (32-bit ARM: MPC X, Live / Live II, One, Key 61, Force).
- **Root shell access** (SSH). Installing plugins this way is unofficial, so back up first and use it at your own risk.

## Install with Terminus
1. Upload `DaSpace-1.0.1-mpc-armv7.zip` to `/tmp`.
2. Run `cd /tmp && unzip -o DaSpace-1.0.1-mpc-armv7.zip && sh DaSpace-1.0.1/install.sh`.
3. Answer `y`. MPC restarts; add **Da Space** as an insert effect.

On a track, keep Mix around 15–40%. On a send or return bus, set Mix to 100%.

## Algorithms
| Mode | Sound |
|---|---|
| **Room** | Natural room: early reflections plus a short, even tail |
| **Hall** | Concert hall: smooth, wide and deep |
| **Plate** | Classic plate: dense and bright, great on vocals and snares |
| **Classic** | "Legacy" comb/allpass reverb: grainy and vintage (no shimmer, like the old units) |
| **Ice** | Bright and glassy, with sparkling resonances ringing in the tail |
| **Meta** | Huge, lush and deeply modulated: wide pads of space |
| **Reflex** | Mostly early reflections: puts a sound in a room without a wash |

All seven are level-matched, so switching modes doesn't jump in volume.

## Controls
- **MIX:** dry stays at full level up to 50%, and wet is at full level from 50%.
- **PRE-DELAY:** 0–250 ms before the reverb starts. Longer pre-delay keeps vocals clear.
- **SIZE:** how big the space is.
- **DECAY:** 0.2–20 s (RT60). The tail really decays in the time you set; Reflex keeps it short.
- **DAMPING:** above this frequency the tail dies faster, which darkens it. At 20 kHz the highs last as long as the lows.
- **LOW CUT:** keeps bass and kick out of the reverb.
- **DIFFUSION:** low gives distinct echoes, high gives a smooth wash.
- **MODULATION:** movement and chorus in the tail. Hall and Meta sound lush with it high.
- **WIDTH:** 0 is mono and 150% is extra wide.
- **SHIMMER:** feeds an octave-up copy back into the tail (heavenly), in every mode except Classic.
- **COLOR:** darker or brighter tilt on the reverb.
- **DUCKING:** the reverb dips while the dry sound plays, then blooms in the gaps.
- **OUTPUT**, and the **IN / VERB** meters.
- **❄ FREEZE:** holds the current tail forever and ignores new input. Play over a frozen pad.

**Q-Links:**
| Q-Links 1–8 | Q-Links 9–16 |
|---|---|
| Mix, Pre-Delay, Size, Decay, Damping, Low Cut, Diffusion, Algorithm | Modulation, Width, Shimmer, Color, Ducking, Output, Freeze, Preset |

## Presets (34)
- **Init:** a hall starting point.
- **Rooms:** Small Room, Vocal Booth, Drum Room, Studio Room, Live Room.
- **Halls:** Concert Hall, Big Hall, Dark Hall, Bright Hall, Cathedral.
- **Plates:** Vocal Plate, Snare Plate, Bright Plate, Long Plate, Vintage Plate.
- **Classic:** Classic Room, Classic Hall, Classic Plate.
- **Ice:** Ice Crystal, Frozen Lake, Glass Keys.
- **Meta:** Meta Space, Wide Meta, Meta Pad Wash.
- **Reflex:** Reflex Small, Reflex Wide Room, Reflex Slap.
- **Creative:** High Sky, High Sky Vocal, Shimmer Heaven, Endless Pad, Ducked Vocal Space, Lo-Fi Dream.

**High Sky** is a huge, airy, bright "sky" space for atmosphere, width, movement and depth. Its settings:
- Meta algorithm
- 80 ms pre-delay, full size, 8.5 s decay
- 35% octave shimmer
- bright colour
- low cut at 400 Hz, so the bass stays dry
- 150% width, 60% modulation
- 35% ducking, so it blooms between phrases

**High Sky Vocal** is the same idea tuned to sit behind a lead vocal. Both are my interpretation of that sound. I can't
confirm the exact effect or settings used on any particular record, so adjust by ear.

## Uninstall
`sh /tmp/DaSpace-1.0.1/uninstall.sh` (unzip the package to `/tmp` again first).
