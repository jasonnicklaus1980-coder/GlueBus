#pragma once
// One clip: its settings (edited from the screen, restored with the project) and its voices (audio thread).
//
// Positions are "logical": 0 .. L frames from the start of the play region in playing order. REVERSE mirrors the
// logical axis onto the sample, and slice REARRANGE maps logical time onto the slices in a new order, so looping,
// fades and the stretcher work the same way in every case. Nothing here modifies the sample itself.
//
// Clicks: every start ramps in (at least 0.5 ms), every stop ramps out (FADE OUT, at least 5 ms), a retrigger
// fades the old voice out over 4 ms while the new one starts, and a loop wrap crossfades (XFADE, at least 2 ms)
// between the audio after the loop end and the loop start.
#include "../AudioEngine/Sample.h"
#include "../TimeStretch/Grains.h"
#include "../Transport/Transport.h"

namespace cp
{
constexpr int kClips = 16;
constexpr int kMaxSlices = 32;

enum Mode { M_ONESHOT, M_LOOP, M_GATE, M_TOGGLE, M_COUNT };
enum PitchMode { PM_RESAMPLE, PM_STRETCH };
enum StretchLen { SL_OFF, SL_AUTO, SL_1_4, SL_1_2, SL_1BAR, SL_2BAR, SL_4BAR, SL_8BAR, SL_COUNT };
enum Follow { FA_OFF, FA_STOP, FA_REPEAT, FA_NEXT, FA_PREV, FA_RANDOM, FA_RANDOM_SCENE, FA_CONTINUE, FA_SCENE, FA_COUNT };
enum FollowTime { FT_END, FT_1BEAT, FT_2BEAT, FT_1BAR, FT_2BAR, FT_4BAR, FT_8BAR, FT_16BAR, FTIME_COUNT };

inline const char* modeName (int m) { static const char* const k[] { "One Shot", "Loop", "Gate", "Toggle" }; return k[clampi (m, 0, M_COUNT - 1)]; }
inline const char* stretchLenName (int s) { static const char* const k[] { "Off", "Auto", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars" }; return k[clampi (s, 0, SL_COUNT - 1)]; }
inline const char* followName (int f)
{
    static const char* const k[] { "Off", "Stop", "Repeat", "Next Clip", "Prev Clip", "Random", "Rnd in Scene", "Continue", "Launch Scene" };
    return k[clampi (f, 0, FA_COUNT - 1)];
}
inline const char* followTimeName (int f) { static const char* const k[] { "At End", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }; return k[clampi (f, 0, FTIME_COUNT - 1)]; }
inline double followBeats (int f, double bpb)
{
    switch (f) { case FT_1BEAT: return 1; case FT_2BEAT: return 2; case FT_1BAR: return bpb; case FT_2BAR: return 2 * bpb;
                 case FT_4BAR: return 4 * bpb; case FT_8BAR: return 8 * bpb; case FT_16BAR: return 16 * bpb; default: return 0; }
}

// ------------------------------------------------------------------------------------------------ settings
struct ClipSettings
{
    char file[256] {};                               // relative to the clips folder (or absolute); UI thread only
    Rel<int> mode { M_LOOP }, quant { 0 };           // quant: 0 = global, else Quant + 1
    Rel<float> start { 0.f }, end { 1.f }, lstart { 0.f }, lend { 1.f };
    Rel<float> fadeIn { 0.f }, fadeOut { 0.f }, xfade { 5.f };            // ms
    Rel<int> reverse { 0 }, semis { 0 }, cents { 0 }, pmode { PM_RESAMPLE }, slen { SL_OFF }, stype { ST_LOOPS };
    Rel<float> vol { 0.f }, pan { 0.f };             // dB, -1..1
    Rel<int> mute { 0 }, normalize { 0 };
    Rel<int> filt { 0 }; Rel<float> cutoff { 20000.f }, res { 0.f }, drive { 0.f }, rsend { 0.f }, dsend { 0.f };
    Rel<int> vintage { 1 };                          // through the SAMPLER stage
    Rel<int> follow { FA_OFF }, ftime { FT_END }, fscene { 0 };
    Rel<int> note { 36 };
    Rel<int> sliceCount { 0 };
    Rel<float> slice[kMaxSlices + 1];                // slice k = [slice[k], slice[k+1]) of the sample, normalised
    Rel<int> sliceRev { 0 };                         // bit per slice
    Rel<int> slicePitch[kMaxSlices];                 // semitones
    Rel<int> seqOn { 0 };
    Rel<int> seq[kMaxSlices];                        // REARRANGE: playing order of the slices

    void resetSound()                                // everything but the file and the MIDI note
    {
        mode = M_LOOP; quant = 0; start = 0.f; end = 1.f; lstart = 0.f; lend = 1.f; fadeIn = 0.f; fadeOut = 0.f; xfade = 5.f;
        reverse = 0; semis = 0; cents = 0; pmode = PM_RESAMPLE; slen = SL_OFF; stype = ST_LOOPS; vol = 0.f; pan = 0.f; mute = 0;
        normalize = 0; filt = 0; cutoff = 20000.f; res = 0.f; drive = 0.f; rsend = 0.f; dsend = 0.f; vintage = 1;
        follow = FA_OFF; ftime = FT_END; fscene = 0; sliceCount = 0; sliceRev = 0; seqOn = 0;
        for (int k = 0; k <= kMaxSlices; ++k) slice[k] = 0.f;
        for (int k = 0; k < kMaxSlices; ++k) { slicePitch[k] = 0; seq[k] = k; }
    }
    ClipSettings() { resetSound(); }
    void copySound (const ClipSettings& o)
    {
        mode = o.mode; quant = o.quant; start = o.start; end = o.end; lstart = o.lstart; lend = o.lend; fadeIn = o.fadeIn;
        fadeOut = o.fadeOut; xfade = o.xfade; reverse = o.reverse; semis = o.semis; cents = o.cents; pmode = o.pmode; slen = o.slen;
        stype = o.stype; vol = o.vol; pan = o.pan; mute = o.mute; normalize = o.normalize; filt = o.filt; cutoff = o.cutoff; res = o.res;
        drive = o.drive; rsend = o.rsend; dsend = o.dsend; vintage = o.vintage; follow = o.follow; ftime = o.ftime; fscene = o.fscene;
        sliceCount = o.sliceCount; sliceRev = o.sliceRev; seqOn = o.seqOn;
        for (int k = 0; k <= kMaxSlices; ++k) slice[k] = o.slice[k];
        for (int k = 0; k < kMaxSlices; ++k) { slicePitch[k] = o.slicePitch[k]; seq[k] = o.seq[k]; }
    }
    bool loops() const { return mode != M_ONESHOT; }
};

// ------------------------------------------------------------------------------------------------ region
struct Region
{
    const Sample* s = nullptr;
    long a = 0, b = 0;                 // play region in sample frames [a, b)
    double L = 0;                      // b - a
    bool rev = false, loop = false;
    double ls = 0, le = 0;             // loop, logical
    double fadeIn = 0, fadeOut = 0, xfade = 0, declick = 0;
    float gain = 1.f;
    int nseq = 0;                      // REARRANGE
    double seqOut[kMaxSlices + 1] {}, seqSrc[kMaxSlices] {};
    double seqFade = 0;
};

inline double msToFrames (double ms, double sr) { return ms * 0.001 * sr; }

// Builds the region for a clip (slice < 0) or for one slice of it.
inline bool buildRegion (const ClipSettings& c, const Sample* s, double sr, Region& R, int slice = -1)
{
    R = Region();
    if (s == nullptr || s->frames < 64) return false;
    R.s = s;
    const long n = s->frames;
    const float st = c.start, en = c.end;
    long a = (long) std::lround (std::fmin (st, en) * n), b = (long) std::lround (std::fmax (st, en) * n);
    const int nsl = c.sliceCount;
    bool rev = c.reverse != 0;
    if (slice >= 0 && nsl >= 1 && slice < nsl)
    {
        a = (long) std::lround ((float) c.slice[slice] * n); b = (long) std::lround ((float) c.slice[slice + 1] * n);
        if ((c.sliceRev >> slice) & 1) rev = ! rev;
    }
    a = std::max (0L, std::min (a, n - 64)); b = std::max (a + 64, std::min (b, n));
    R.a = a; R.b = b; R.L = (double) (b - a); R.rev = rev;
    R.loop = slice < 0 && c.loops();
    long lsP = (long) std::lround ((float) c.lstart * n), leP = (long) std::lround ((float) c.lend * n);
    lsP = std::max (a, std::min (lsP, b)); leP = std::max (a, std::min (leP, b));
    if (leP - lsP < 64) { lsP = a; leP = b; }
    if (! rev) { R.ls = (double) (lsP - a); R.le = (double) (leP - a); }
    else { R.ls = (double) (b - leP); R.le = (double) (b - lsP); }
    R.declick = msToFrames (0.5, sr);
    R.fadeIn = slice >= 0 ? 0.0 : msToFrames ((float) c.fadeIn, sr);                 // the envelope already declicks
    R.fadeOut = std::max (msToFrames (2.0, sr), msToFrames (slice >= 0 ? 3.0 : (float) c.fadeOut, sr));
    R.xfade = std::min (std::max (msToFrames (2.0, sr), msToFrames ((float) c.xfade, sr)), (R.le - R.ls) * 0.5);
    R.fadeIn = std::min (R.fadeIn, R.L * 0.5); R.fadeOut = std::min (R.fadeOut, R.L * 0.5);
    R.gain = (c.normalize && s->peak > 1e-4f) ? std::fmin (16.f, 0.98f / s->peak) : 1.f;
    // REARRANGE: logical slices in a new order (whole region, loops over all of it)
    if (slice < 0 && c.seqOn && nsl >= 2)
    {
        double bnd[kMaxSlices + 1]; int nb = 0;
        for (int k = 0; k <= nsl; ++k)
        {
            double p = (double) std::lround ((float) c.slice[k] * n);
            p = std::max ((double) a, std::min (p, (double) b)) - a;           // relative to the region
            bnd[nb++] = rev ? R.L - p : p;
        }
        if (rev) for (int i = 0; i < nb / 2; ++i) { const double t = bnd[i]; bnd[i] = bnd[nb - 1 - i]; bnd[nb - 1 - i] = t; }
        double out = 0; int k = 0;
        for (int i = 0; i < nsl; ++i)
        {
            const int src = clampi (c.seq[i], 0, nsl - 1);
            const int li = rev ? nsl - 1 - src : src;                            // slice index in logical order
            const double len = bnd[li + 1] - bnd[li];
            if (len < 32) continue;
            R.seqOut[k] = out; R.seqSrc[k] = bnd[li]; out += len; ++k;
        }
        if (k >= 2) { R.nseq = k; R.seqOut[k] = out; R.L = out; R.ls = 0; R.le = out; R.seqFade = msToFrames (1.5, sr); }
    }
    return true;
}

// Cubic (Hermite) or linear read of a sample at a fractional frame; silence outside the sample.
inline void readFrame (const Sample* s, double p, bool cubic, float& l, float& r)
{
    const long i = (long) std::floor (p);
    const float f = (float) (p - i);
    auto at = [&] (long k, int ch) -> float { return (k >= 0 && k < s->frames) ? s->pcm[2 * k + ch] * (1.f / 32768.f) : 0.f; };
    if (! cubic)
    {
        l = at (i, 0) + (at (i + 1, 0) - at (i, 0)) * f;
        r = at (i, 1) + (at (i + 1, 1) - at (i, 1)) * f;
        return;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        const float xm = at (i - 1, ch), x0 = at (i, ch), x1 = at (i + 1, ch), x2 = at (i + 2, ch);
        const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
        const float y = ((c3 * f + c2) * f + c1) * f + x0;
        (ch == 0 ? l : r) = y;
    }
}

// logical position -> sample frame (and the rearrange edge fade)
inline double physical (const Region& R, double q, float& g)
{
    g = 1.f;
    if (R.nseq > 0)
    {
        int lo = 0, hi = R.nseq - 1;
        while (lo < hi) { const int mid = (lo + hi + 1) / 2; if (R.seqOut[mid] <= q) lo = mid; else hi = mid - 1; }
        const double into = q - R.seqOut[lo], left = R.seqOut[lo + 1] - q;
        const double e = std::fmin (into, left);
        if (e < R.seqFade) g = (float) std::fmax (0.0, e / R.seqFade);
        q = R.seqSrc[lo] + into;
    }
    return R.rev ? (double) (R.b - 1) - q : (double) R.a + q;
}

// ------------------------------------------------------------------------------------------------ voice
struct Voice
{
    bool on = false, tail = false, releasing = false, firstPass = true;
    double pos = 0;                    // logical (resample: play head, stretch: anchor)
    float env = 0.f, envStep = 0.f, envTop = 1.f;
    GrainSet grains;
    int slice = -1;                    // slice voices
    void start (double at, const Region& R)
    {
        on = true; tail = false; releasing = false; firstPass = at < R.fadeIn;
        pos = at; env = 0.f; envTop = 1.f; envStep = (float) (1.0 / std::max (1.0, R.declick));
        grains.reset();
    }
    void release (double frames)
    {
        if (! on) return;
        releasing = true;
        envStep = -(float) (std::max (env, 1e-3f) / std::max (1.0, frames));
    }
};

struct Rates
{
    double step = 1.0;                 // resample: frames per output sample
    bool stretch = false;
    double T = 1.0, P = 1.0;           // stretch: anchor speed, grain read speed
    double grainLen = 2000.0;
    int grains = 2;
    bool cubic = true;
};

enum { RV_ENDED = 1, RV_WRAPPED = 2 };

// Renders (adds) m samples of one voice. A loop wrap hands the old read position to `spawnTail` for the crossfade,
// with the index of the next sample (the caller renders the tail from there on).
template <typename SpawnTail>
inline int renderVoice (Voice& v, const Region& R, const Rates& x, float* outL, float* outR, int m, SpawnTail&& spawnTail)
{
    int flags = 0;
    if (! v.on || R.s == nullptr) return 0;
    const Sample* s = R.s;
    const bool loop = R.loop && ! v.tail;
    const HannTable& H = hann();
    for (int i = 0; i < m; ++i)
    {
        // envelope (declick, release, crossfade)
        v.env += v.envStep;
        if (v.envStep > 0.f && v.env >= v.envTop) { v.env = v.envTop; v.envStep = 0.f; }
        if (v.env <= 0.f && v.envStep < 0.f) { v.on = false; flags |= RV_ENDED; break; }
        const double q = v.pos;
        float g = v.env * R.gain;
        if (v.firstPass && q < R.fadeIn) g *= (float) (q / R.fadeIn);
        if (! R.loop || v.tail)
        {
            if (q >= R.L) { v.on = false; flags |= RV_ENDED; break; }
            if (! v.tail && R.L - q < R.fadeOut) g *= (float) ((R.L - q) / R.fadeOut);
        }
        float l = 0.f, r = 0.f;
        if (! x.stretch)
        {
            float sg; const double p = physical (R, q, sg);
            readFrame (s, p, x.cubic, l, r);
            g *= sg;
            v.pos += x.step;
        }
        else
        {
            if (v.grains.due (x.grainLen, x.grains)) v.grains.spawn (q);
            float sl = 0.f, sr = 0.f, ws = 0.f;
            for (Grain& gr : v.grains.g)
            {
                if (! gr.on) continue;
                const double ph = gr.age / x.grainLen;
                if (ph >= 1.0) { gr.on = false; continue; }
                const float w = H.at (ph);
                double gq = gr.start + gr.age * x.P;
                if (loop && gq >= R.le) gq = R.ls + fmodd (gq - R.ls, R.le - R.ls);
                if (gq < R.L || loop)
                {
                    float sg; const double p = physical (R, gq, sg);
                    float a, b; readFrame (s, p, x.cubic, a, b);
                    sl += a * w * sg; sr += b * w * sg;
                }
                ws += w;
                gr.age += 1.0;
            }
            v.grains.sinceSpawn += 1.0;
            const float inv = 1.f / std::fmax (ws, 1e-3f);
            l = sl * inv; r = sr * inv;
            v.pos += x.T;
        }
        outL[i] += l * g; outR[i] += r * g;
        if (loop && v.pos >= R.le)
        {
            if (! x.stretch)
            {
                spawnTail (v, R.xfade, i + 1);                                 // old position fades out past the loop end
                v.env = 0.f; v.envStep = (float) (1.0 / std::max (1.0, R.xfade)); v.envTop = 1.f;
                if (v.releasing) v.on = false;
            }
            v.pos = R.ls + fmodd (v.pos - R.ls, R.le - R.ls);
            v.firstPass = false;
            flags |= RV_WRAPPED;
        }
        if (! v.on) break;
    }
    return flags;
}
} // namespace cp
