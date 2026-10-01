#pragma once
// PlaybackEngine: what the DAC reads from sample memory on each 26.04 kHz clock (both voices, stereo-linked).
//
// The SP-1200 plays one-shot samples from memory. The plugin is an insert on a live signal, so three playback
// schemes are offered, all built from memory reads at the DAC clock:
//   45>33 GRIT  the sampling clock runs at 26.04 kHz x ratio and the DAC replays at 26.04 kHz, so pitch is kept and
//               the effective sample rate changes: the hardware result of sampling a record at 45 rpm and tuning it
//               down (or up). The DAC re-clocks each word with drop-sample timing.
//   PITCH       drop-sample pitch shifting of the live signal: two read heads move through memory at the tuning ratio
//               (no interpolation); one plays alone at the exact rate and they crossfade only for 20 % of a 79 ms
//               window. The splicing is necessary for a continuous stream and is not a hardware feature.
//   REPLAY      the hardware's own behaviour: each detected hit starts a voice that plays memory from the hit at the
//               tuning ratio until the next hit. Tuned up, a hit plays faster and ends sooner (it can't read audio that
//               hasn't arrived: the voice stops there, like the end of a sample); tuned down, it plays slower and
//               longer. Best on drums.
// ALIAS BYPASS reads memory with linear interpolation instead (non-hardware, for comparison).
#include "SamplerMemory.h"

namespace sp
{
enum PlayMode { PM_GRIT, PM_PITCH, PM_REPLAY };

struct PlaybackEngine
{
    static constexpr float kWin = 2048.f, kXfade = 0.2f, kFade = 0.0038f;
    float delay = 0.f, mix = 0.f;               // PITCH
    struct Voice { bool on = false; double pos = 0; float gain = 0.f, step = 0.f; };
    Voice v, tail;                              // REPLAY (tail: the voice being cut by a retrigger)
    void reset() { delay = 0.f; mix = 0.f; v = Voice(); tail = Voice(); }

    static float readAt (const SamplerMemory& m, double pos, bool interp)
    {
        const long i = (long) std::floor (pos);
        if (! interp) return (float) m.read (i);
        const float f = (float) (pos - (double) i);
        return (float) m.read (i) + ((float) m.read (i + 1) - (float) m.read (i)) * f;
    }
    // REPLAY: a hit at memory index `at` (absolute, already including the pre-roll)
    void trigger (long at)
    {
        if (v.on) { tail = v; tail.step = -1.f / 26.f; }                       // ~1 ms fade of the cut voice
        v.on = true; v.pos = (double) at; v.gain = 1.f; v.step = 0.f;
    }
    // one DAC clock: codes (as float, fractional only when interpolating) for both voices
    void tick (const SamplerMemory* mem, int mode, double ratio, bool retune, bool interp, float out[2])
    {
        if (mode == PM_GRIT) { out[0] = (float) mem[0].latest(); out[1] = (float) mem[1].latest(); return; }
        if (mode == PM_PITCH)
        {
            const long newest = mem[0].w - 1;
            mix += ((retune ? 1.f : 0.f) - mix) * kFade;
            if (mix < 1e-4f) { mix = 0.f; out[0] = (float) mem[0].latest(); out[1] = (float) mem[1].latest(); return; }
            delay += 1.f - (float) ratio;
            while (delay < 0.f) delay += kWin;
            while (delay >= kWin) delay -= kWin;
            const float d2 = delay + kWin * 0.5f >= kWin ? delay - kWin * 0.5f : delay + kWin * 0.5f;
            const float tri = 1.f - std::fabs (2.f * delay / kWin - 1.f);
            const float g1 = clampf ((tri - 0.5f) / kXfade + 0.5f, 0.f, 1.f), g2 = 1.f - g1;
            for (int c = 0; c < 2; ++c)
            {
                const float direct = (float) mem[c].latest();
                const float h = readAt (mem[c], (double) newest - delay, interp) * g1 + readAt (mem[c], (double) newest - d2, interp) * g2;
                out[c] = direct + mix * (h - direct);
            }
            return;
        }
        // REPLAY
        out[0] = out[1] = 0.f;
        Voice* vs[2] { &v, &tail };
        for (Voice* vp : vs)
        {
            Voice& x = *vp;
            if (! x.on) continue;
            const long newest = mem[0].w - 1;
            if (x.pos >= (double) newest && x.step >= 0.f) x.step = -1.f / 52.f;   // caught up with the input: end (2 ms)
            if ((double) newest - x.pos > SamplerMemory::kLen - 4) { x.on = false; continue; }
            const float g = x.gain;
            for (int c = 0; c < 2; ++c) out[c] += readAt (mem[c], std::fmin (x.pos, (double) newest), interp) * g;
            x.pos += ratio;
            if (x.step != 0.f) { x.gain += x.step; if (x.gain <= 0.f) { x.on = false; x.gain = 0.f; } }
        }
    }
};
} // namespace sp
