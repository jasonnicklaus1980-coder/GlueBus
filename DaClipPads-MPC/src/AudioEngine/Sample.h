#pragma once
// A loaded sample: 16-bit stereo audio at the host rate plus what the screen and the chopper need (peak envelope,
// peak level, the audible range for TRIM and the onsets for transient CHOP). Built on the loader thread; after
// that it is read-only and shared by every clip that uses it (PAD ASSIGN shares one sample between clips).
//
// Sample-rate conversion happens once, at load: a windowed-sinc resampler (32 taps, Blackman window, cutoff below
// the lower Nyquist) converts the file to the host rate, so playback never has to convert rates in real time.
#include "../Utilities/Util.h"
#include "../Utilities/Wav.h"
#include <algorithm>
#include <cstdlib>

namespace cp
{
constexpr int kEnvPoints = 2048;         // waveform overview resolution
constexpr int kMaxOnsets = 128;

struct Sample
{
    int16_t* pcm = nullptr;              // interleaved stereo at `rate`
    long frames = 0;
    double rate = 44100.0;
    double fileRate = 44100.0;
    float peak = 0.f;                    // 0..1
    long trimStart = 0, trimEnd = 0;     // first / last frame above -48 dBFS
    uint8_t env[kEnvPoints] {};          // peak per 1/2048 of the sample, 0..255 (linear)
    int onsetCount = 0;
    long onset[kMaxOnsets] {};           // frames, sorted
    float onsetStrength[kMaxOnsets] {};
    char name[64] {};                    // file name without folder and ".wav"
    char path[256] {};                   // as given to the loader (relative to the clips folder, or absolute)
    int refs = 0;                        // clips using it (audio thread only)
    double retiredAt = 0.0;              // loader frees it a few seconds after the last clip let go
};

inline void freeSample (Sample* s)
{
    if (s == nullptr) return;
    std::free (s->pcm);
    std::free (s);
}

// ------------------------------------------------------------------------------------------------ resampling
// Windowed-sinc table: kPhases sub-sample phases x kTaps taps, linear interpolation between phases.
struct SincTable
{
    static constexpr int kTaps = 32, kPhases = 256;
    float t[(kPhases + 1) * kTaps];
    double cutoff = 1.0;
    void build (double cut)
    {
        cutoff = cut;
        for (int p = 0; p <= kPhases; ++p)
            for (int k = 0; k < kTaps; ++k)
            {
                const double x = (k - kTaps / 2 + 1) - (double) p / kPhases;            // distance from the output point
                const double w = x / (kTaps / 2.0);
                const double win = std::fabs (w) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos (kPi * w) + 0.08 * std::cos (2.0 * kPi * w);
                const double a = kPi * x * cut;
                const double sinc = std::fabs (a) < 1e-9 ? 1.0 : std::sin (a) / a;
                t[p * kTaps + k] = (float) (cut * sinc * win);
            }
    }
};

// Converts `in` (at in->rate) to `rate`. Returns a new buffer or nullptr (out of memory).
inline Pcm* resample (const Pcm* in, double rate)
{
    const double ratio = rate / in->rate;                                            // output frames per input frame
    const long outFrames = (long) std::floor (in->frames * ratio);
    if (outFrames < 1) return nullptr;
    Pcm* out = (Pcm*) std::calloc (1, sizeof (Pcm));
    if (out == nullptr) return nullptr;
    out->pcm = (int16_t*) std::malloc ((size_t) outFrames * 2 * sizeof (int16_t));
    SincTable* tab = (SincTable*) std::malloc (sizeof (SincTable));
    if (out->pcm == nullptr || tab == nullptr) { std::free (tab); freePcm (out); return nullptr; }
    out->rate = rate; out->frames = outFrames;
    tab->build (ratio < 1.0 ? ratio * 0.97 : 0.97);                                  // anti-alias when going down
    const int T = SincTable::kTaps;
    for (long o = 0; o < outFrames; ++o)
    {
        const double pos = o / ratio;
        const long i0 = (long) std::floor (pos);
        const double frac = (pos - i0) * SincTable::kPhases;
        const int ph = (int) frac; const float u = (float) (frac - ph);
        const float* a = tab->t + ph * T; const float* b = a + T;
        float l = 0.f, r = 0.f;
        for (int k = 0; k < T; ++k)
        {
            const long n = i0 + k - T / 2 + 1;
            if (n < 0 || n >= in->frames) continue;
            const float w = a[k] + (b[k] - a[k]) * u;
            l += w * in->pcm[2 * n]; r += w * in->pcm[2 * n + 1];
        }
        auto s16 = [] (float x) { return (int16_t) (x > 32767.f ? 32767 : (x < -32768.f ? -32768 : (int) std::lround (x))); };
        out->pcm[2 * o] = s16 (l); out->pcm[2 * o + 1] = s16 (r);
    }
    std::free (tab);
    return out;
}

// ------------------------------------------------------------------------------------------------ analysis
inline void analyze (Sample* s)
{
    const long n = s->frames;
    // peak envelope + overall peak
    int peak = 0;
    for (int k = 0; k < kEnvPoints; ++k)
    {
        const long a = n * k / kEnvPoints, b = std::max (a + 1, n * (k + 1) / kEnvPoints);
        int m = 0;
        for (long i = a; i < b && i < n; ++i)
        {
            const int l = std::abs ((int) s->pcm[2 * i]), r = std::abs ((int) s->pcm[2 * i + 1]);
            if (l > m) m = l;
            if (r > m) m = r;
        }
        if (m > peak) peak = m;
        s->env[k] = (uint8_t) std::min (255, (m * 255 + 32766) / 32767);
    }
    s->peak = peak / 32767.f;
    // audible range (TRIM): -48 dBFS
    const int th = 128;
    s->trimStart = 0; s->trimEnd = n;
    for (long i = 0; i < n; ++i) if (std::abs ((int) s->pcm[2 * i]) > th || std::abs ((int) s->pcm[2 * i + 1]) > th) { s->trimStart = i; break; }
    for (long i = n - 1; i >= 0; --i) if (std::abs ((int) s->pcm[2 * i]) > th || std::abs ((int) s->pcm[2 * i + 1]) > th) { s->trimEnd = i + 1; break; }
    if (s->trimEnd <= s->trimStart) { s->trimStart = 0; s->trimEnd = n; }
    // onsets: rises in log energy of the high-passed signal (first difference) over 256-frame hops,
    // compared with the average of the 4 hops before; local maxima above a threshold
    const int hop = 256;
    const long nh = n / hop;
    s->onsetCount = 0;
    if (nh < 3) return;
    float* e = (float*) std::malloc (sizeof (float) * (size_t) nh);
    if (e == nullptr) return;
    float prev = 0.f;
    for (long h = 0; h < nh; ++h)
    {
        double acc = 0;
        for (int i = 0; i < hop; ++i)
        {
            const long f = h * hop + i;
            const float x = (s->pcm[2 * f] + s->pcm[2 * f + 1]) * (0.5f / 32768.f);
            const float d = x - prev; prev = x;
            acc += (double) x * x + 4.0 * d * d;
        }
        e[h] = (float) std::log (acc / hop + 1e-9);
    }
    float* on = (float*) std::malloc (sizeof (float) * (size_t) nh);
    if (on == nullptr) { std::free (e); return; }
    for (long h = 0; h < nh; ++h)
    {
        float m = 0; int c = 0;
        for (long j = h - 4; j < h; ++j) if (j >= 0) { m += e[j]; ++c; }
        m = c ? m / c : e[h];
        on[h] = e[h] > -18.f ? std::max (0.f, e[h] - m) : 0.f;                         // ignore near-silence
    }
    struct Cand { long at; float w; };
    Cand best[kMaxOnsets]; int nb = 0;
    for (long h = 1; h + 1 < nh; ++h)
    {
        if (on[h] < 1.0f || on[h] < on[h - 1] || on[h] < on[h + 1]) continue;
        const Cand c { std::max (0L, (h - 1) * (long) hop), on[h] };                    // a hop early: keep the attack
        if (nb < kMaxOnsets) best[nb++] = c;
        else
        {
            int lo = 0; for (int i = 1; i < nb; ++i) if (best[i].w < best[lo].w) lo = i;
            if (c.w > best[lo].w) best[lo] = c;
        }
    }
    std::free (on); std::free (e);
    for (int i = 1; i < nb; ++i) for (int j = i; j > 0 && best[j].at < best[j - 1].at; --j) { const Cand t = best[j]; best[j] = best[j - 1]; best[j - 1] = t; }
    for (int i = 0; i < nb; ++i) { s->onset[i] = best[i].at; s->onsetStrength[i] = best[i].w; }
    s->onsetCount = nb;
}

// Loads a WAV, converts it to `hostRate` and analyses it. maxSeconds keeps memory in check.
inline Sample* loadSample (const char* fullPath, const char* storedPath, double hostRate, double maxSeconds, const char** err)
{
    Pcm* p = readWav (fullPath, err, maxSeconds);
    if (p == nullptr) return nullptr;
    const double fileRate = p->rate;
    if (std::fabs (p->rate - hostRate) > 0.5)
    {
        Pcm* q = resample (p, hostRate);
        freePcm (p);
        if (q == nullptr) { *err = "out of memory"; return nullptr; }
        p = q;
    }
    Sample* s = (Sample*) std::calloc (1, sizeof (Sample));
    if (s == nullptr) { freePcm (p); *err = "out of memory"; return nullptr; }
    s->pcm = p->pcm; s->frames = p->frames; s->rate = p->rate; s->fileRate = fileRate;
    std::free (p);
    std::snprintf (s->path, sizeof s->path, "%s", storedPath);
    const char* base = std::strrchr (storedPath, '/');
    std::snprintf (s->name, sizeof s->name, "%s", base ? base + 1 : storedPath);
    const size_t l = std::strlen (s->name);
    if (l > 4 && (std::strcmp (s->name + l - 4, ".wav") == 0 || std::strcmp (s->name + l - 4, ".WAV") == 0)) s->name[l - 4] = 0;
    analyze (s);
    return s;
}
} // namespace cp
