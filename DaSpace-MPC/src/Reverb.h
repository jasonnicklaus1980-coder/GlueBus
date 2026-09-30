#pragma once
// Da Space - the reverb engine. Seven algorithms on shared building blocks, all sample-rate independent:
//   Room, Hall  : early reflections + an 8-line feedback delay network (Householder matrix, modulated lines,
//                 in-loop damping, per-line gains from the decay time: the tail decays at the set RT60)
//   Plate       : a plate tank after Dattorro (4 input diffusers, two cross-fed branches with modulated allpasses)
//   Classic     : the legacy comb + allpass reverb (8 damped combs, 4 allpasses per side), grainy and vintage (no shimmer)
//   Ice         : bright FDN through a high-passed input, with a bank of glassy resonators on the tail
//   Meta        : a huge, deeply modulated FDN with extra output diffusion - wide, lush pads of space
//   Reflex      : mostly early reflections with a short tail - places sounds in a room without a wash
// Shared: pre-delay, low cut into the tank, octave-up shimmer in the feedback, freeze, colour tilt, width,
// ducking (wet dips under the dry signal), mix and output. Floats for speed on the MPC's ARM CPU.
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sp
{
enum Algo { A_ROOM, A_HALL, A_PLATE, A_CLASSIC, A_ICE, A_META, A_REFLEX, A_COUNT };

inline float flushf (float x) { return std::fabs (x) < 1e-20f ? 0.f : x; }

// ------------------------------------------------------------------------------------------------ blocks
struct Line                                    // power-of-two circular delay line
{
    float* buf = nullptr; int mask = 0, w = 0;
    bool alloc (int minLen)
    {
        int n = 1; while (n < minLen + 4) n <<= 1;
        std::free (buf); buf = (float*) std::calloc ((size_t) n, sizeof (float));
        mask = n - 1; w = 0; return buf != nullptr;
    }
    void release() { std::free (buf); buf = nullptr; }
    void clear() { if (buf) std::memset (buf, 0, sizeof (float) * (size_t) (mask + 1)); w = 0; }
    inline void push (float x) { buf[w] = x; w = (w + 1) & mask; }
    inline float tap (int d) const { return buf[(w - 1 - d) & mask]; }
    inline float readCubic (float d) const      // d samples ago, 4-point Hermite (far less HF loss than linear)
    {
        const int i = (int) d; const float t = d - (float) i;
        const float y0 = buf[(w - i) & mask], y1 = buf[(w - 1 - i) & mask], y2 = buf[(w - 2 - i) & mask], y3 = buf[(w - 3 - i) & mask];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }
    inline float read (float d) const           // d samples ago (fractional, linear)
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = buf[(w - 1 - i) & mask], b = buf[(w - 2 - i) & mask];
        return a + (b - a) * f;
    }
};

struct Allpass                                 // Schroeder allpass on a delay line
{
    Line l; float len = 1;
    inline float run (float x, float g, float mod = 0.f)
    {
        const float d = mod != 0.f ? l.readCubic (len + mod - 1.f) : l.tap ((int) len - 1);
        const float v = x + g * d;
        l.push (flushf (v));
        return d - g * v;
    }
};

struct OnePole { float z = 0; inline float lp (float x, float a) { z = flushf (x + (z - x) * a); return z; } };

inline float coefLP (float hz, float fs) { return std::exp (-2.f * 3.14159265f * hz / fs); }
inline float rtGain (float delaySec, float rt60) { return std::pow (10.f, -3.f * delaySec / rt60); }

struct Params
{
    int algo = A_HALL;
    float mix = 0.3f, predelayMs = 20, size = 0.6f, decay = 2.5f, dampHz = 9000, lowCutHz = 120;
    float diffusion = 0.7f, modulation = 0.3f, width = 1.0f, shimmer = 0, color = 0, ducking = 0, outDb = 0;
    bool freeze = false;
};

// ------------------------------------------------------------------------------------------------ engine
class Reverb
{
public:
    ~Reverb() { release(); }

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const float k = fs / 1000.f;                                   // samples per ms
        pre[0].alloc ((int) (400 * k)); pre[1].alloc ((int) (400 * k));   // 250 ms pre-delay + early reflections
        for (auto& l : fdn) l.alloc ((int) (74 * 2.8f * k + 4 * k));   // longest line x largest size + modulation
        for (int c = 0; c < 2; ++c) for (auto& a : inDiff[c]) a.l.alloc ((int) (20 * k));
        for (int c = 0; c < 2; ++c) for (auto& a : outDiff[c]) a.l.alloc ((int) (20 * k));
        const float ps = fs / 29761.f * 1.6f;                            // plate: Dattorro lengths at 29761 Hz, size up to 1.6
        for (int i = 0; i < 4; ++i) pDiff[i].l.alloc ((int) (400 * ps));
        for (int b = 0; b < 2; ++b) { pAp1[b].l.alloc ((int) (950 * ps)); pAp2[b].l.alloc ((int) (2700 * ps)); pD1[b].alloc ((int) (4500 * ps)); pD2[b].alloc ((int) (3800 * ps)); }
        const float cs = fs / 44100.f * 1.45f;                          // classic: Freeverb lengths at 44.1 kHz, size up to 1.45
        for (int c = 0; c < 2; ++c) { for (auto& l : comb[c]) l.alloc ((int) (1650 * cs)); for (auto& a : cAp[c]) a.l.alloc ((int) (600 * cs)); }
        shim.alloc ((int) (0.12f * fs));
        for (int c = 0; c < 2; ++c) duckEnv[c] = 0;
        clear();
    }
    void release()
    {
        for (auto& l : pre) l.release();
        for (auto& l : fdn) l.release();
        for (int c = 0; c < 2; ++c) { for (auto& a : inDiff[c]) a.l.release(); for (auto& a : outDiff[c]) a.l.release(); for (auto& l : comb[c]) l.release(); for (auto& a : cAp[c]) a.l.release(); }
        for (auto& a : pDiff) a.l.release();
        for (int b = 0; b < 2; ++b) { pAp1[b].l.release(); pAp2[b].l.release(); pD1[b].release(); pD2[b].release(); }
        shim.release();
    }
    void clear()
    {
        for (auto& l : pre) l.clear();
        for (auto& l : fdn) l.clear();
        for (int c = 0; c < 2; ++c) { for (auto& a : inDiff[c]) a.l.clear(); for (auto& a : outDiff[c]) a.l.clear(); for (auto& l : comb[c]) l.clear(); for (auto& a : cAp[c]) a.l.clear(); }
        for (auto& a : pDiff) a.l.clear();
        for (int b = 0; b < 2; ++b) { pAp1[b].l.clear(); pAp2[b].l.clear(); pD1[b].clear(); pD2[b].clear(); pDamp[b].z = 0; pFeed[b] = 0; }
        shim.clear(); shimPos = 0;
        for (auto& f : fdnDamp) f.z = 0;
        for (float& z : fdnZ) z = 0;
        for (int c = 0; c < 2; ++c) { for (auto& f : combDamp[c]) f.z = 0; hp1[c] = hp2[c] = hpx1[c] = hpx2[c] = 0; erLp[c].z = 0; tilt[c].z = 0; for (auto& r : res[c]) r.z1 = r.z2 = 0; }
        pBw.z = 0;
    }

    // process stereo in place; dry passes untouched when mix is 0 (after the wet fades out)
    void process (const Params& p, float* L, float* R, int n)
    {
        if (p.algo != algo)                                               // algorithm change: fade the wet out, clear, fade in
        {
            if (switchFade <= 0.f) { algo = p.algo; clear(); }
            else switchTarget = 0.f;
        }
        else switchTarget = 1.f;
        control (p, n);
        const float fadeStep = 1.f / (0.02f * fs);
        for (int i = 0; i < n; ++i)
        {
            const float dl = L[i], dr = R[i];
            // smoothed controls
            sMix += (tMix - sMix) * kCtl; sOut += (tOut - sOut) * kCtl;
            if (std::fabs (sMix - tMix) < 1e-6f) sMix = tMix;
            if (std::fabs (sOut - tOut) < 1e-6f) sOut = tOut;
            if (switchFade < switchTarget) switchFade = std::fmin (switchTarget, switchFade + fadeStep);
            else if (switchFade > switchTarget) switchFade = std::fmax (switchTarget, switchFade - fadeStep);
            if (switchFade <= 0.f && algo != p.algo) { algo = p.algo; clear(); switchTarget = 1.f; }
            // input: pre-delay, low cut (12 dB), freeze mutes new input into the tank
            float hl = dl, hr = dr;
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? hl : hr;
                const float y1 = hpA * (hp1[c] + x - hpx1[c]); hpx1[c] = x; hp1[c] = flushf (y1);
                const float y2 = hpA * (hp2[c] + y1 - hpx2[c]); hpx2[c] = y1; hp2[c] = flushf (y2);
                x = y2;
            }
            pre[0].push (hl); pre[1].push (hr);
            float xl = pre[0].read (preSamples), xr = pre[1].read (preSamples);
            const float inGain = p.freeze ? 0.f : 1.f;
            xl *= inGain; xr *= inGain;
            float wl = 0, wr = 0;
            switch (algo)
            {
                case A_PLATE:   plate (xl, xr, wl, wr); break;
                case A_CLASSIC: classic (xl, xr, wl, wr); break;
                default:        network (xl, xr, wl, wr); break;
            }
            // level-match the algorithms (measured on noise at typical settings: every mode sits near -6 dB wet)
            static const float trim[A_COUNT] { 0.95f, 1.30f, 0.84f, 3.94f, 1.76f, 2.48f, 0.51f };
            wl *= trim[algo]; wr *= trim[algo];
            // colour tilt, width, ducking
            for (int c = 0; c < 2; ++c)
            {
                float& w = c == 0 ? wl : wr;
                const float lo = tilt[c].lp (w, tiltA);
                w = lo * tiltLo + (w - lo) * tiltHi;
            }
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = mid + side; wr = mid - side;
            const float lvl = std::fmax (std::fabs (dl), std::fabs (dr));
            duckEnv[0] = lvl > duckEnv[0] ? duckEnv[0] + (lvl - duckEnv[0]) * duckAtk : duckEnv[0] * duckRel;
            const float duckG = 1.f - ducking * std::fmin (1.f, duckEnv[0] * 4.f) * 0.92f;
            // safety: a runaway tank (can't happen with the gains used, but never let it reach the speakers)
            if (! (std::fabs (wl) < 16.f && std::fabs (wr) < 16.f)) { clear(); wl = wr = 0; }
            const float wg = sWetGain (sMix) * duckG * switchFade, dg = sDryGain (sMix);
            L[i] = (dl * dg + wl * wg) * sOut;
            R[i] = (dr * dg + wr * wg) * sOut;
            const float wp = std::fmax (std::fabs (wl * wg), std::fabs (wr * wg));
            if (wp > wetPeak) wetPeak = wp;
            if (lvl > inPeak) inPeak = lvl;
        }
    }
    float takeWetPeak() { const float p = wetPeak; wetPeak = 0; return p; }
    float takeInPeak() { const float p = inPeak; inPeak = 0; return p; }
    bool isDryOnly() const { return tMix <= 0.f && sMix <= 1e-6f; }

private:
    // equal-loudness-ish mix: dry full up to 50 %, wet full from 50 %
    static float sDryGain (float m) { return m <= 0.5f ? 1.f : 2.f * (1.f - m); }
    static float sWetGain (float m) { return m >= 0.5f ? 1.f : 2.f * m; }

    void control (const Params& p, int n)
    {
        kCtl = 1.f - std::exp (-1.f / (0.01f * fs)); kSize = 1.f - std::exp (-(float) n / (0.08f * fs));
        tMix = p.mix; tOut = std::pow (10.f, p.outDb / 20.f); tSize = p.size;
        if (firstRun) { sMix = tMix; sOut = tOut; sSize = tSize; firstRun = false; }
        sSize += (tSize - sSize) * kSize;                                  // size glides (~80 ms): no zipper, a slight doppler
        preSamples = std::fmax (0.f, p.predelayMs * 0.001f * fs);
        float lc = p.lowCutHz; if (algo == A_ICE) lc = std::fmax (lc, 250.f);
        hpA = std::exp (-2.f * 3.14159265f * lc / fs);
        width = p.width; ducking = p.ducking; freeze = p.freeze; shimmer = freeze ? 0.f : p.shimmer;
        duckAtk = 1.f - std::exp (-1.f / (0.005f * fs)); duckRel = std::exp (-1.f / (0.25f * fs));
        tiltA = coefLP (900.f, fs);
        tiltLo = std::pow (10.f, -p.color * 6.f / 20.f); tiltHi = std::pow (10.f, p.color * 6.f / 20.f);
        dampA = freeze ? 0.f : coefLP (p.dampHz, fs); dampHz = p.dampHz;
        diffusion = p.diffusion; modulation = p.modulation;
        rt60 = std::fmax (0.1f, p.decay);
        if (algo == A_REFLEX) rt60 = std::fmin (rt60 * 0.2f, 1.2f);
        // per-algorithm character
        switch (algo)
        {
            case A_ROOM:   sMin = 0.22f; sMax = 0.75f; erLevel = 0.4f; tailLevel = 0.8f; modMs = 0.25f; outDiffOn = false; break;
            case A_HALL:   sMin = 0.85f; sMax = 2.0f;  erLevel = 0.3f;  tailLevel = 1.0f; modMs = 0.7f;  outDiffOn = false; break;
            case A_ICE:    sMin = 0.8f;  sMax = 2.2f;  erLevel = 0.f;   tailLevel = 1.0f; modMs = 1.1f;  outDiffOn = true;  break;
            case A_META:   sMin = 1.1f;  sMax = 2.7f;  erLevel = 0.f;   tailLevel = 1.0f; modMs = 3.0f;  outDiffOn = true;  break;
            case A_REFLEX: sMin = 0.15f; sMax = 0.45f; erLevel = 1.0f;  tailLevel = 0.35f; modMs = 0.15f; outDiffOn = false; break;
            default:       sMin = 0.85f; sMax = 2.0f;  erLevel = 0.f;   tailLevel = 1.f;  modMs = 0.5f;  outDiffOn = false; break;
        }
        // per-block tables: line lengths and decay gains (no pow() per sample)
        const float k = fs / 1000.f;
        {
            static const float base[8] { 31.3f, 37.9f, 43.1f, 47.3f, 53.9f, 61.1f, 67.3f, 73.7f };   // ms
            const float S = sMin + (sMax - sMin) * sSize;
            // decay filter per line (Jot): DC gain from RT60, Nyquist gain from a shorter HF decay set by Damping
            // (20 kHz = no extra HF loss, 1 kHz = highs die about 6x faster)
            const float hfRatio = std::fmin (1.f, std::pow (dampHz / 20000.f, 0.85f));
            for (int i = 0; i < 8; ++i)
            {
                fdnLen[i] = std::floor (base[i] * S * k);
                const float gDc = rtGain (fdnLen[i] / fs, rt60), gNy = rtGain (fdnLen[i] / fs, rt60 * hfRatio);
                fdnA[i] = (gDc - gNy) / (gDc + gNy); fdnB[i] = gDc * (1.f - fdnA[i]);
                fdnG[i] = gDc;
            }
            erScale = (0.4f + 1.1f * sSize) * k;
            modD = modMs * modulation * k;
        }
        {
            const float r = fs / 29761.f; pS = (0.55f + 1.0f * sSize) * r;
            const float loop = (672.f + 4453.f + 1800.f + 3720.f + 908.f + 4217.f + 2656.f + 3163.f) * pS / fs;
            pDecay = freeze ? 1.f : std::fmin (0.9995f, std::pow (10.f, -3.f * loop / (4.f * rt60)));
        }
        {
            static const float cl[8] { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            cS = fs / 44100.f * (0.6f + 0.85f * sSize);
            for (int c = 0; c < 2; ++c) for (int j = 0; j < 8; ++j)
            {
                cLen[c][j] = (cl[j] + (c ? 23.f : 0.f)) * cS;
                cFb[c][j] = freeze ? 1.f : std::fmin (0.985f, rtGain (cLen[c][j] / fs, rt60));
            }
        }
        // ice resonators (glassy partials, pushed up by colour)
        if (algo == A_ICE)
        {
            const float base[4] { 2600.f, 3900.f, 5800.f, 8300.f };
            for (int r = 0; r < 4; ++r)
            {
                const float f = std::fmin (base[r] * (1.f + 0.3f * p.color), 0.45f * fs), q = 9.f;
                const float w = 2.f * 3.14159265f * f / fs, al = std::sin (w) / (2.f * q), a0 = 1.f + al;
                for (int c = 0; c < 2; ++c) { res[c][r].b0 = al / a0; res[c][r].a1 = -2.f * std::cos (w) / a0; res[c][r].a2 = (1.f - al) / a0; }
            }
        }
    }

    // ---------------------------------------------------------------- FDN (Room, Hall, Ice, Meta, Reflex)
    void network (float xl, float xr, float& wl, float& wr)
    {
        static const float rate[8] { 0.31f, 0.47f, 0.59f, 0.73f, 0.83f, 0.97f, 1.13f, 1.29f };    // Hz
        static const float erT[2][10] { { 7.1f, 11.3f, 17.9f, 23.3f, 29.9f, 37.1f, 43.7f, 53.3f, 61.9f, 71.3f },
                                        { 8.3f, 13.1f, 19.7f, 25.9f, 31.7f, 39.1f, 47.3f, 55.1f, 64.7f, 73.9f } };
        const float k = fs / 1000.f;
        // early reflections straight from the pre-delayed input (size spaces them out)
        float el = 0, er = 0;
        if (erLevel > 0.f && ! freeze)
        {
            static const float erG[10] { 0.8f, -0.672f, 0.564f, -0.474f, 0.398f, -0.335f, 0.281f, -0.236f, 0.198f, -0.167f };
            for (int t = 0; t < 10; ++t)
            {
                const float g = erG[t];
                const float dL = preSamples + erT[0][t] * erScale, dR = preSamples + erT[1][t] * erScale;
                el += g * pre[(t % 3 == 2) ? 1 : 0].read (dL);
                er += g * pre[(t % 3 == 2) ? 0 : 1].read (dR);
            }
            el = erLp[0].lp (el, dampA * 0.6f); er = erLp[1].lp (er, dampA * 0.6f);
        }
        // input diffusion
        const float g = 0.45f + 0.3f * diffusion;
        static const float difMs[2][4] { { 4.7f, 3.6f, 12.7f, 9.3f }, { 5.3f, 3.1f, 11.9f, 8.7f } };
        for (int a = 0; a < 4; ++a) { inDiff[0][a].len = std::floor (difMs[0][a] * k); inDiff[1][a].len = std::floor (difMs[1][a] * k); }
        if (diffusion > 0.02f) for (int a = 0; a < 4; ++a) { xl = inDiff[0][a].run (xl, g); xr = inDiff[1][a].run (xr, g); }
        // the network
        float y[8], sum = 0.f;
        for (int i = 0; i < 8; ++i)
        {
            lfo[i] += rate[i] / fs; if (lfo[i] >= 1.f) lfo[i] -= 1.f;
            const float m = modD > 0.f ? modD * (0.5f + 0.5f * fastSin (lfo[i])) : 0.f;
            float v = m > 0.f ? fdn[i].readCubic (fdnLen[i] + m) : fdn[i].tap ((int) fdnLen[i]);
            if (! freeze) { v = fdnB[i] * v + fdnA[i] * fdnZ[i]; fdnZ[i] = flushf (v); }
            y[i] = v; sum += v;
        }
        // shimmer: octave-up pitch shift of the tail, fed back into the network
        float sh = 0.f;
        if (shimmer > 0.001f) sh = soft (pitchUp (sum * 0.125f) * shimmer * 0.55f);
        const float hh = sum * 0.25f;                                      // Householder: y - 2/N * sum
        const float inL = xl * 0.35f, inR = xr * 0.35f;
        for (int i = 0; i < 8; ++i)
        {
            const float sign = (i & 2) ? -1.f : 1.f;
            const float in = ((i & 1) ? inR : inL) * sign + sh * ((i & 1) ? -1.f : 1.f);
            fdn[i].push (flushf (y[i] - hh + in));
        }
        float tl = (y[0] + y[2] - y[4] + y[6] + 0.5f * (y[1] - y[5])) * 0.4f;
        float tr = (y[1] + y[3] - y[5] + y[7] + 0.5f * (y[2] - y[6])) * 0.4f;
        if (outDiffOn)
        {
            static const float odMs[2][2] { { 7.9f, 5.1f }, { 8.9f, 4.3f } };
            for (int a = 0; a < 2; ++a) { outDiff[0][a].len = std::floor (odMs[0][a] * k); outDiff[1][a].len = std::floor (odMs[1][a] * k); }
            for (int a = 0; a < 2; ++a) { tl = outDiff[0][a].run (tl, 0.5f); tr = outDiff[1][a].run (tr, 0.5f); }
        }
        if (algo == A_ICE)                                                 // glassy resonances ring on the tail
        {
            float rl = 0, rr = 0;
            for (int r = 0; r < 4; ++r) { rl += res[0][r].run (tl); rr += res[1][r].run (tr); }
            tl += rl * 0.9f; tr += rr * 0.9f;
        }
        wl = el * erLevel + tl * tailLevel;
        wr = er * erLevel + tr * tailLevel;
    }

    // ---------------------------------------------------------------- Plate (after Dattorro)
    void plate (float xl, float xr, float& wl, float& wr)
    {
        const float r = fs / 29761.f, S = pS;
        float x = 0.5f * (xl + xr);
        x = pBw.lp (x, coefLP (std::fmin (18000.f, 0.45f * fs), fs));
        const float dlen[4] { 142.f * r, 107.f * r, 379.f * r, 277.f * r };
        const float dg1 = 0.75f * diffusion + 0.1f, dg2 = 0.625f * diffusion + 0.1f;
        for (int a = 0; a < 4; ++a) { pDiff[a].len = std::floor (dlen[a]); x = pDiff[a].run (x, a < 2 ? dg1 : dg2); }
        const float ap1[2] { 672.f * S, 908.f * S }, ap2[2] { 1800.f * S, 2656.f * S };
        const float d1[2] { 4453.f * S, 4217.f * S }, d2[2] { 3720.f * S, 3163.f * S };
        const float decay = pDecay;
        const float excursion = 16.f * r * (0.3f + modulation);
        float outB[2];
        for (int b = 0; b < 2; ++b)
        {
            pLfo[b] += (b ? 0.83f : 1.0f) / fs; if (pLfo[b] >= 1.f) pLfo[b] -= 1.f;
            pAp1[b].len = std::floor (ap1[b]); pAp2[b].len = std::floor (ap2[b]);
            float v = x + pFeed[1 - b];
            v = pAp1[b].run (v, -0.7f, excursion * (0.5f + 0.5f * fastSin (pLfo[b])));
            pD1[b].push (v);
            float u = pD1[b].tap ((int) d1[b] - 1);
            if (! freeze) u = pDamp[b].lp (u, dampA);
            u *= decay;
            u = pAp2[b].run (u, 0.5f);
            pD2[b].push (u);
            outB[b] = pD2[b].tap ((int) d2[b] - 1) * decay;
        }
        pFeed[0] = flushf (outB[0]); pFeed[1] = flushf (outB[1]);
        // output taps (scaled with size)
        auto t = [&] (const Line& l, float d) { return l.read (d * S); };
        wl = 0.6f * (t (pD1[0], 266) + t (pD1[0], 2974) - t (pAp2[0].l, 1913) + t (pD2[0], 1996) - t (pD1[1], 1990) - t (pAp2[1].l, 187) - t (pD2[1], 1066));
        wr = 0.6f * (t (pD1[1], 353) + t (pD1[1], 3627) - t (pAp2[1].l, 1228) + t (pD2[1], 2673) - t (pD1[0], 2111) - t (pAp2[0].l, 335) - t (pD2[0], 121));
        if (shimmer > 0.001f) { const float sh = soft (pitchUp (0.5f * (wl + wr)) * shimmer * 0.5f); pFeed[0] += sh; pFeed[1] -= sh; }
    }

    // ---------------------------------------------------------------- Classic (legacy comb + allpass)
    void classic (float xl, float xr, float& wl, float& wr)
    {
        static const float al[4] { 556, 441, 341, 225 };
        const float S = cS;
        const float in = (xl + xr) * 0.025f;
        float o[2] = { 0, 0 };
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 8; ++j)
            {
                const float len = cLen[c][j], fb = cFb[c][j];
                const float y = comb[c][j].tap ((int) len - 1);
                const float f = freeze ? y : combDamp[c][j].lp (y, dampA * 0.9f);
                comb[c][j].push (flushf (in + f * fb));
                o[c] += y;
            }
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < 4; ++j) { cAp[c][j].len = std::floor ((al[j] + (c ? 23.f : 0.f)) * S); o[c] = cAp[c][j].run (o[c], 0.5f); }
        wl = o[0] * 0.9f; wr = o[1] * 0.9f;
        // the legacy algorithm has no shimmer (like the real thing)
    }

    // ---------------------------------------------------------------- shimmer: two-head octave-up pitch shifter
    float pitchUp (float x)
    {
        shim.push (x);
        const float win = 0.06f * fs;                                      // 60 ms grains
        shimPos += 1.f; if (shimPos >= win) shimPos -= win;              // delay shrinks one sample per sample: 2x speed
        float out = 0.f;
        for (int h = 0; h < 2; ++h)
        {
            float ph = shimPos / win + 0.5f * h; if (ph >= 1.f) ph -= 1.f;
            const float d = (1.f - ph) * win + 2.f;
            out += shim.read (d) * fastSin (0.5f * ph);                  // sine window: the two heads sum to constant power
        }
        return out;
    }
    static float soft (float x) { return x / (1.f + std::fabs (x)); }     // keeps the shimmer loop bounded
    static float fastSin (float ph)                                       // sin(2 pi ph), ph in [0, 1)
    {
        const float x = ph * 6.2831853f - 3.14159265f;                     // -pi .. pi, returns sin(x + pi) = -sin(x)
        const float y = 1.27323954f * x - 0.405284735f * x * std::fabs (x);
        return -(0.225f * (y * std::fabs (y) - y) + y);
    }

    struct Res { float b0 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                 inline float run (float x) { const float y = b0 * x + z1; z1 = flushf (-a1 * y + z2); z2 = flushf (-b0 * x - a2 * y); return y; } };

    float fs = 44100.f;
    int algo = A_HALL;
    bool firstRun = true, freeze = false, outDiffOn = false;
    float switchFade = 1.f, switchTarget = 1.f;
    float kCtl = 0.001f, kSize = 0.0001f, tMix = 0, sMix = 0, tOut = 1, sOut = 1, tSize = 0.5f, sSize = 0.5f;
    float preSamples = 0, hpA = 0.99f, width = 1, ducking = 0, shimmer = 0, duckAtk = 0.01f, duckRel = 0.999f, duckEnv[2] {};
    float tiltA = 0.9f, tiltLo = 1, tiltHi = 1, dampA = 0.5f, diffusion = 0.7f, modulation = 0.3f, rt60 = 2.f;
    float sMin = 1, sMax = 2, erLevel = 0, tailLevel = 1, modMs = 0.5f;
    float fdnA[8] {}, fdnB[8] {}, fdnZ[8] {}, dampHz = 9000;
    float fdnLen[8] {}, fdnG[8] {}, erScale = 1, modD = 0, pS = 1, pDecay = 0.5f, cS = 1, cLen[2][8] {}, cFb[2][8] {};
    Line pre[2];
    float hp1[2] {}, hp2[2] {}, hpx1[2] {}, hpx2[2] {};
    OnePole erLp[2], tilt[2];
    Allpass inDiff[2][4], outDiff[2][2];
    Line fdn[8]; OnePole fdnDamp[8]; float lfo[8] { 0.f, 0.13f, 0.27f, 0.41f, 0.55f, 0.69f, 0.83f, 0.97f };
    Res res[2][4];
    Allpass pDiff[4], pAp1[2], pAp2[2]; Line pD1[2], pD2[2]; OnePole pDamp[2], pBw; float pFeed[2] {}, pLfo[2] { 0.f, 0.5f };
    Line comb[2][8]; OnePole combDamp[2][8]; Allpass cAp[2][4];
    Line shim; float shimPos = 0;
    float wetPeak = 0, inPeak = 0;
};
} // namespace sp
