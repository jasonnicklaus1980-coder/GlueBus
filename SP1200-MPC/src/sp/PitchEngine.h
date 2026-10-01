#pragma once
// PitchEngine: tuning the way the SP-1200 does it.
//
// Documented: the SP-1200 changes pitch by changing the playback rate through sample memory ("drop-sample
// pitch-shifting"): the DAC keeps running at the 26.04 kHz clock, and each clock reads the memory at a position that
// advances by the tuning ratio. Tuned up, words are skipped; tuned down, words are repeated. There is no interpolation,
// so the result aliases differently at every setting: the plugin does the same arithmetic, so the aliasing is a
// consequence of the read pattern, not an added effect.
//   ratio = 2^(semitones / 12) (equal-tempered, the owner's manual's semitone tuning)
//   RANGE Hardware = -8 .. +7 semitones (owner's manual: "7 semitones up and 8 down"); Extended = -12 .. +12.
#include "Dsp.h"

namespace sp
{
struct PitchEngine
{
    static int clampSemis (float st, bool hardware) { const int s = (int) std::lround (st); return hardware ? clampi (s, -8, 7) : clampi (s, -12, 12); }
    static double ratio (int semis) { return std::exp2 (semis / 12.0); }
};
} // namespace sp
