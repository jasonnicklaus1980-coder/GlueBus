#pragma once
// CHOP: splits a clip's play region (START..END) into 4, 8, 16 or 32 slices.
//  - Transient: the strongest onsets found at load time (Sample::onset) become slice starts, at least 30 ms and
//    a third of an equal slice apart. If the audio has fewer clear hits than slices, the largest gaps are split
//    evenly, so there are always exactly N slices.
//  - Equal: N equal slices (loops that are already in time).
// Slices are stored normalised (0..1 of the sample) in ClipSettings, so they survive sample-rate changes.
#include "../ClipEngine/Clip.h"

namespace cp
{
enum ChopMode { CH_TRANSIENT, CH_EQUAL };
inline int chopCount (int idx) { static const int k[] { 4, 8, 16, 32 }; return k[clampi (idx, 0, 3)]; }

inline int chop (ClipSettings& c, const Sample* s, int n, int mode)
{
    if (s == nullptr || n < 2) return 0;
    n = clampi (n, 2, kMaxSlices);
    const double F = (double) s->frames;
    const double a = std::fmin ((float) c.start, (float) c.end) * F, b = std::fmax ((float) c.start, (float) c.end) * F;
    if (b - a < n * 64) return 0;
    double pos[kMaxSlices + 1]; int k = 0;
    pos[k++] = a;
    if (mode == CH_TRANSIENT)
    {
        const double minGap = std::fmax (0.03 * s->rate, (b - a) / (n * 3.0));
        // strongest first
        int order[kMaxOnsets]; int no = 0;
        for (int i = 0; i < s->onsetCount; ++i) if (s->onset[i] > a + minGap && s->onset[i] < b - minGap) order[no++] = i;
        for (int i = 1; i < no; ++i) for (int j = i; j > 0 && s->onsetStrength[order[j]] > s->onsetStrength[order[j - 1]]; --j) { const int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
        for (int i = 0; i < no && k < n; ++i)
        {
            const double p = (double) s->onset[order[i]];
            bool ok = true;
            for (int j = 0; j < k; ++j) if (std::fabs (pos[j] - p) < minGap) { ok = false; break; }
            if (ok) pos[k++] = p;
        }
    }
    pos[k++] = b;
    for (int i = 1; i < k; ++i) for (int j = i; j > 0 && pos[j] < pos[j - 1]; --j) { const double t = pos[j]; pos[j] = pos[j - 1]; pos[j - 1] = t; }
    if (mode == CH_EQUAL) { k = n + 1; for (int i = 0; i <= n; ++i) pos[i] = a + (b - a) * i / n; }
    while (k < n + 1)                                                              // split the largest gap
    {
        int g = 0; for (int i = 1; i + 1 < k; ++i) if (pos[i + 1] - pos[i] > pos[g + 1] - pos[g]) g = i;
        for (int j = k; j > g + 1; --j) pos[j] = pos[j - 1];
        pos[g + 1] = (pos[g] + pos[g + 2]) * 0.5; ++k;
    }
    for (int i = 0; i <= n; ++i) c.slice[i] = (float) (pos[i] / F);
    for (int i = n + 1; i <= kMaxSlices; ++i) c.slice[i] = (float) (b / F);
    for (int i = 0; i < kMaxSlices; ++i) { c.seq[i] = i; c.slicePitch[i] = 0; }
    c.sliceRev = 0; c.seqOn = 0;
    c.sliceCount = n;
    return n;
}

// Moves the start of slice k (k >= 1) between its neighbours; slice 0's start is the clip START.
inline void moveSlice (ClipSettings& c, int k, float to)
{
    const int n = c.sliceCount;
    if (k < 1 || k >= n) return;
    const float lo = c.slice[k - 1] + 1e-4f, hi = c.slice[k + 1] - 1e-4f;
    if (hi > lo) c.slice[k] = clampf (to, lo, hi);
}
} // namespace cp
