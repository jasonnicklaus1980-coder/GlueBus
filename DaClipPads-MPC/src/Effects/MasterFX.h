#pragma once
// Master bus: saturation -> 3-band EQ -> compressor -> master volume -> safety limiter, plus the two send
// effects (reverb, tempo-synced delay) whose returns join before the saturation.
// Kept light for the MPC's ARM CPU: a 4-line feedback-delay-network reverb, a ping-pong delay and a
// feed-forward compressor. Every section skips its work when it is set to neutral.
// All buffers are allocated in prepare() (called from the host thread), never while audio runs.
#include "Filters.h"
#include <cstdlib>

namespace cp
{
struct MasterParams
{
    float sat = 0.f;                                             // 0..1
    float eqLow = 0.f, eqMid = 0.f, eqMidHz = 1000.f, eqHigh = 0.f; // dB
    float thresh = 0.f, ratio = 1.f, attackMs = 10.f, releaseMs = 150.f, makeup = 0.f;
    float revSize = 0.5f, revDamp = 0.5f, revReturn = 0.f;
    double delayBeats = 0.75; float fb = 0.35f, tone = 0.5f, dlyReturn = 0.f;
    float volDb = 0.f;
    bool limiter = true;
};

inline double delayBeatsFor (int idx, double bpb)
{
    static const double k[] { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, -1.0 };
    const double b = k[clampi (idx, 0, 7)];
    return b < 0 ? bpb : b;
}
inline const char* delayName (int idx) { static const char* const k[] { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1 Bar" }; return k[clampi (idx, 0, 7)]; }

// ------------------------------------------------------------------------------------------------ reverb
struct Reverb
{
    static constexpr int kLines = 4;
    float* line[kLines] {}; int cap[kLines] {}; int len[kLines] {}; int w[kLines] {};
    float* ap[4] {}; int apLen[4] {}; int apW[4] {};
    OnePole damp[kLines];
    float g[kLines] {};
    float sr = 44100.f, lastSize = -1.f, lastDamp = -1.f;
    void free_() { for (auto& p : line) { std::free (p); p = nullptr; } for (auto& p : ap) { std::free (p); p = nullptr; } }
    ~Reverb() { free_(); }
    void prepare (float rate)
    {
        free_(); sr = rate;
        static const double base[kLines] { 37.1, 43.7, 53.3, 61.9 };
        for (int i = 0; i < kLines; ++i) { cap[i] = (int) (base[i] * 0.001 * 2.1 * sr) + 4; line[i] = (float*) std::calloc ((size_t) cap[i], sizeof (float)); w[i] = 0; }
        static const double apMs[4] { 3.2, 2.4, 8.6, 6.3 };
        for (int i = 0; i < 4; ++i) { apLen[i] = std::max (8, (int) (apMs[i] * 0.001 * sr)); ap[i] = (float*) std::calloc ((size_t) apLen[i], sizeof (float)); apW[i] = 0; }
        lastSize = -1.f;
    }
    bool ok() const { for (int i = 0; i < kLines; ++i) if (line[i] == nullptr) return false; for (auto p : ap) if (p == nullptr) return false; return true; }
    void clear() { for (int i = 0; i < kLines; ++i) if (line[i]) std::memset (line[i], 0, sizeof (float) * (size_t) cap[i]); for (int i = 0; i < 4; ++i) if (ap[i]) std::memset (ap[i], 0, sizeof (float) * (size_t) apLen[i]); }
    void setup (float size, float dmp)
    {
        if (size == lastSize && dmp == lastDamp) return;
        lastSize = size; lastDamp = dmp;
        static const double base[kLines] { 37.1, 43.7, 53.3, 61.9 };
        const double scale = 0.5 + 1.5 * clampf (size, 0.f, 1.f);
        const double rt = 0.4 + 5.6 * clampf (size, 0.f, 1.f) * clampf (size, 0.f, 1.f) + 0.6 * clampf (size, 0.f, 1.f);
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = std::min (cap[i] - 1, (int) (base[i] * 0.001 * scale * sr));
            g[i] = (float) std::pow (10.0, -3.0 * len[i] / (rt * sr));
            damp[i].set (12000.f - 10500.f * clampf (dmp, 0.f, 1.f), sr);
        }
    }
    float allpass (int i, float x)
    {
        float* b = ap[i]; const float y = b[apW[i]];
        const float v = x + 0.6f * y; b[apW[i]] = v; if (++apW[i] >= apLen[i]) apW[i] = 0;
        return y - 0.6f * v;
    }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)       // adds to out
    {
        for (int s = 0; s < n; ++s)
        {
            const float xl = allpass (1, allpass (0, inL[s])), xr = allpass (3, allpass (2, inR[s]));
            float d[kLines];
            for (int i = 0; i < kLines; ++i) { int r = w[i] - len[i]; if (r < 0) r += cap[i]; d[i] = damp[i].lp (line[i][r]) * g[i]; }
            // Householder-like mix (Hadamard / 2)
            const float h0 = 0.5f * (d[0] + d[1] + d[2] + d[3]), h1 = 0.5f * (d[0] - d[1] + d[2] - d[3]);
            const float h2 = 0.5f * (d[0] + d[1] - d[2] - d[3]), h3 = 0.5f * (d[0] - d[1] - d[2] + d[3]);
            const float fb[kLines] { h0 + xl, h1 + xr, h2 + xl, h3 + xr };
            for (int i = 0; i < kLines; ++i) { line[i][w[i]] = fb[i]; if (++w[i] >= cap[i]) w[i] = 0; }
            outL[s] += (d[0] + d[2]) * 0.6f; outR[s] += (d[1] + d[3]) * 0.6f;
        }
    }
};

// ------------------------------------------------------------------------------------------------ delay
struct Delay
{
    float* buf[2] {}; int cap = 0, w = 0;
    double cur = -1.0;
    OnePole tone[2];
    float sr = 44100.f;
    ~Delay() { std::free (buf[0]); std::free (buf[1]); }
    void prepare (float rate)
    {
        std::free (buf[0]); std::free (buf[1]);
        sr = rate; cap = (int) (4.2 * sr); w = 0; cur = -1.0;
        buf[0] = (float*) std::calloc ((size_t) cap, sizeof (float)); buf[1] = (float*) std::calloc ((size_t) cap, sizeof (float));
    }
    bool ok() const { return buf[0] != nullptr && buf[1] != nullptr; }
    void clear() { if (ok()) { std::memset (buf[0], 0, sizeof (float) * (size_t) cap); std::memset (buf[1], 0, sizeof (float) * (size_t) cap); } }
    float read (int c, double d) const
    {
        double p = w - d; while (p < 0) p += cap;
        const int i = (int) p; const float f = (float) (p - i); const int j = i + 1 >= cap ? 0 : i + 1;
        return buf[c][i] + (buf[c][j] - buf[c][i]) * f;
    }
    // ping-pong: the input enters the left line, each line feeds the other
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, double frames, float fb, float toneAmt)
    {
        const double target = clampd (frames, 16.0, cap - 4.0);
        if (cur < 0) cur = target;
        tone[0].set (1500.f + 16000.f * toneAmt * toneAmt, sr); tone[1].a = tone[0].a;
        for (int s = 0; s < n; ++s)
        {
            cur += (target - cur) * 0.0005;                                           // glide on tempo changes
            const float dl = read (0, cur), dr = read (1, cur);
            const float in = 0.5f * (inL[s] + inR[s]);
            buf[0][w] = in + tone[0].lp (dr) * fb;
            buf[1][w] = tone[1].lp (dl) * fb;
            if (++w >= cap) w = 0;
            outL[s] += dl; outR[s] += dr;
        }
    }
};

// ------------------------------------------------------------------------------------------------ master chain
struct MasterChain
{
    float sr = 44100.f;
    Biquad lo[2], mid[2], hi[2];
    float eqKey[4] { 1e9f, 1e9f, 1e9f, 1e9f };
    float grDb = 0.f;                    // compressor gain reduction (<= 0)
    float lim = 1.f;
    float volSm = 1.f;
    void prepare (float rate) { sr = rate; eqKey[0] = 1e9f; grDb = 0.f; lim = 1.f; for (int c = 0; c < 2; ++c) { lo[c].reset(); mid[c].reset(); hi[c].reset(); } }
    void process (float* L, float* R, int n, const MasterParams& p)
    {
        const bool eqOn = std::fabs (p.eqLow) > 0.05f || std::fabs (p.eqMid) > 0.05f || std::fabs (p.eqHigh) > 0.05f;
        if (eqOn && (eqKey[0] != p.eqLow || eqKey[1] != p.eqMid || eqKey[2] != p.eqMidHz || eqKey[3] != p.eqHigh))
        {
            eqKey[0] = p.eqLow; eqKey[1] = p.eqMid; eqKey[2] = p.eqMidHz; eqKey[3] = p.eqHigh;
            for (int c = 0; c < 2; ++c) { lo[c].shelf (100.0, sr, p.eqLow, false); mid[c].peak (p.eqMidHz, sr, 0.7, p.eqMid); hi[c].shelf (8000.0, sr, p.eqHigh, true); }
        }
        const float satAmt = clampf (p.sat, 0.f, 1.f), drive = 1.f + 3.f * satAmt, satNorm = 1.f / tanhApprox (drive);
        const bool comp = p.ratio > 1.01f;
        const float atk = std::exp (-1.f / (std::fmax (0.1f, p.attackMs) * 0.001f * sr)), rel = std::exp (-1.f / (std::fmax (5.f, p.releaseMs) * 0.001f * sr));
        const float slope = 1.f - 1.f / std::fmax (1.f, p.ratio), knee = 6.f, makeup = dbToGain (p.makeup);
        const float vol = dbToGain (p.volDb), volK = 1.f - std::exp (-1.f / (0.01f * sr));
        const float ceil = 0.966f, limRel = std::exp (-1.f / (0.08f * sr));
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (satAmt > 0.f) { l += (tanhApprox (l * drive) * satNorm - l) * satAmt; r += (tanhApprox (r * drive) * satNorm - r) * satAmt; }
            if (eqOn) { l = hi[0].tick (mid[0].tick (lo[0].tick (l))); r = hi[1].tick (mid[1].tick (lo[1].tick (r))); }
            if (comp)
            {
                const float lev = gainToDb (std::fmax (std::fabs (l), std::fabs (r)));
                const float over = lev - p.thresh;
                float want = 0.f;
                if (over > knee * 0.5f) want = -over * slope;
                else if (over > -knee * 0.5f) { const float t = over + knee * 0.5f; want = -slope * t * t / (2.f * knee); }
                grDb = want < grDb ? want + (grDb - want) * atk : want + (grDb - want) * rel;
                const float g = dbToGain (grDb) * makeup;
                l *= g; r *= g;
            }
            volSm += (vol - volSm) * volK;
            l *= volSm; r *= volSm;
            if (p.limiter)
            {
                const float pk = std::fmax (std::fabs (l), std::fabs (r));
                const float want = pk > ceil ? ceil / pk : 1.f;
                lim = want < lim ? want : want + (lim - want) * limRel;
                l = clampf (l * lim, -ceil, ceil); r = clampf (r * lim, -ceil, ceil);
            }
            L[i] = l; R[i] = r;
        }
    }
};
} // namespace cp
