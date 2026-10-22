// MIDI note to pad mapping. The MPC-style default (notes 36 to 51 on pads 1 to 16) is a common convention, NOT verified
// against an MPC X; it is kept in one place so it can be changed once the device's note map is confirmed.
#pragma once

namespace mc909 {

enum class PadMap { MpcDefault, Chromatic };

// Pad index 0..15 for a MIDI note, or -1 when the note has no pad under this map.
int padForNote(int note, PadMap map = PadMap::MpcDefault, int chromaticBase = 36);

}  // namespace mc909
