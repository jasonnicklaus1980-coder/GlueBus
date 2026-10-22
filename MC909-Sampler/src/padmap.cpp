#include "padmap.h"

namespace mc909 {

int padForNote(int note, PadMap map, int chromaticBase)
{
    if (map == PadMap::MpcDefault)
        return (note >= 36 && note <= 51) ? note - 36 : -1;          // assumed convention; see padmap.h
    const int p = note - chromaticBase;
    return (p >= 0 && p < 16) ? p : -1;
}

}  // namespace mc909
