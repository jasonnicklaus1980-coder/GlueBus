# RadioReady EQ: design notes

## How the spec maps onto an MPC plugin
The requested plugin (VST3/AU/Standalone) was rebuilt as an **MPC OS VST2**, because that is the only plugin format
MPC's host loads. Every visible control is a plugin parameter wired to the DSP engine. Where the MPC can't do what the
spec asked for, here is what replaces it:

| Spec | On the MPC |
|---|---|
| Drag nodes on the curve | MPC skins have no free 2D drag. The graph has 8 touch strips (B1-B8): drag up/down for that band's gain. Frequency and Q are set with the boxes and Q-Links. |
| Preset search box | MPC plugin skins have no text entry. Instead there is a category filter, prev/next, a preset selector box and favourites. |
| Save/load user presets | MPC's own plugin preset save/load. Every setting, including the Quick section and modes, is a parameter. |
| Independent L/R | Each band's channel: Stereo, Left or Right (in Mid/Side mode: Mid or Side). |
| Mono / stereo / M/S | Stereo or Mid/Side mode. A mono input (L = R) is handled correctly, since Side bands then do nothing. |
| Oversampling | Zero Latency or HQ 2x (63-tap linear-phase halfband, 31 samples reported to the host). |

## DSP (src/EQEngine.*, src/Biquad.h)
- **Filters:** RBJ cookbook biquads, 64-bit transposed direct form II.
  - Pass filters: 12 dB/oct is one section using the band Q; 24 and 48 dB/oct are Butterworth cascades of 2 and 4
    sections.
- **Smoothing:**
  - Frequency and Q glide geometrically, gain glides linearly, over about 20 ms. Coefficients are recomputed every 16
    samples.
  - Changing type, slope, channel or stereo mode is a structural change. The old filter keeps running and crossfades
    out over 10 ms.
  - Input/output gain moves over 10 ms, and bypass is a linear 10 ms crossfade.
- **Transparency:** off or 0 dB bands are skipped. Init, Quick at Amount 0% and settled bypass are all bit-exact
  (tested).
- **QUICK RADIO READY:** 9 fixed filters, all scaled by Amount × macro:
  - Low-End: HP 12→32 Hz, +2 dB shelf at 70 Hz, −1.5 dB at 250 Hz
  - Vocal: −2 dB at 320 Hz, +2.5 dB at 3.2 kHz
  - Air: +3 dB shelf at 11 kHz, −1.2 dB Q3 at 6.8 kHz
  - Punch: +1.8 dB at 120 Hz, +1.5 dB at 2 kHz

  If the combined curve would exceed +4 dB anywhere, all the gains are scaled down together. The section's output is
  then level-matched by the pink-loudness estimate below, so it shapes tone without adding level.
- **Loudness estimate:** the average power gain over 48 log-spaced points from 40 Hz to 12 kHz (equal weight per
  octave, like pink noise). It drives:
  - the Quick section's compensation
  - Auto Gain
  - preset gain staging, done offline by tools/gen_presets.py

  The test checks it against real band-limited pink noise: all presets land within ±0.2 dB.
- **HQ 2x:**
  - Upsampling uses a polyphase halfband FIR (Kaiser β 7.5). It is flat to 18 kHz at 44.1 kHz, −0.75 dB at 20 kHz, and
    flat to the top at 48 kHz and above.
  - The engine then runs at 2×fs, then the signal is decimated.
  - Switching quality crossfades between the two paths.
  - `initialDelay` is updated and the host is told (audioMasterIOChanged).

## Plugin (src/radioready.cpp)
- **Thread-safe state changes:** preset loads, undo/redo, A/B and reset write the whole state inside a sequence
  counter. The audio thread reads the parameters at the start of each 64-sample chunk and skips a chunk that overlaps a
  write, then glides to the new state.
- **Undo:** one step is recorded once the controls have been still for 400 ms (32 steps). Preset loads, A/B switches
  and resets are recorded as steps too. History is guarded by a spinlock the audio thread only try-locks.
- **Momentary buttons** (Prev, Next, Copy, Undo, Redo, Reset) act on the press, spring back to 0 and tell MPC. A
  restored project therefore never re-triggers them.
- **Restore order:** Preset is parameter 0. When MPC restores parameters in index order, the preset loads first and
  the saved band values then override it (tested).
- **Display:** curve, analyzer and meters are read-only parameters pushed to MPC only when they move (curve ≤ 20/s,
  analyzer ~14/s with a 2 dB threshold, meters ≤ 25/s with a 1 dB threshold).
- **Q-Links:** `getParameter` returns the exact normalised value MPC sent, which avoids stuck stepped controls.
- **Favourites:** one list for all instances, kept in `/sdcard/vst/radioready.favorites` (one preset name per line).

## Preset gain staging
`tools/gen_presets.py` sets each preset's Output Gain to minus the pink loudness of its bands. Side-only bands are left
out, because a mono-compatible mix carries its energy in the Mid. Quick settings are level-matched inside the plugin.
Measured on band-limited pink noise, the worst case is 0.14 dB.

## Skin
- **Title:** a gold script "Marcus And Moni Radio Ready EQ" bar across the top of every tab.
- **Tabs:** EQ GAIN/FREQ and EQ Q/TYPE share one band screen with different Q-Link maps; RADIO READY has its own.
- **Touch strips:** 8 invisible vertical drag areas over the graph, one per band gain, placed above the untouchable
  curve/analyzer columns.
- **Graph:** 64 curve columns and 32 analyzer bars, each a filmstrip bound to a read-only parameter.
- **Memory:** about 64 MB decoded, less than EQ7. The value boxes are drawn in the backgrounds, and their drag
  controls use a transparent 16 px strip.
