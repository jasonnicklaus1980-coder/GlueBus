#pragma once
// Sample analysis for Da Sample Lab (loader thread only; allocates, never runs on the audio thread).
//   - onset envelope: half-wave-rectified spectral flux (log magnitude, 2048-point FFT, 512-frame hop)
//   - BPM: autocorrelation of the onset envelope over 60-200 BPM with a gentle preference around 110,
//          then folded into 70-160 (the usual sampling range; halve / double with the BPM control)
//   - key: chromagram (12 pitch classes, 55 Hz - 2 kHz) correlated with the Krumhansl-Kessler major / minor profiles
//   - transients: peaks of the onset envelope above an adaptive threshold set by SENSITIVITY
//   - sections: bar-by-bar novelty (energy + chroma change) on the beat grid, for phrase-length slices
#include "core/Util.h"
#include <algorithm>
#include <array>
#include <complex>
#include <cstdlib>
#include <vector>

namespace sl
{
using cp::kPi;

// in-place radix-2 FFT (n = power of two)
inline void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / (double) len;
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Analysis
{
    int hop = 512;                       // frames per onset-envelope point
    std::vector<float> flux;             // onset strength per hop
    std::vector<float> lowFlux;          // the same below 150 Hz (kick drums: finds the downbeat)
    std::vector<std::array<float, 12>> chroma; // per hop (only every 4th hop is computed; others copy)
    double bpm = 0, bpmConfidence = 0;
    int key = -1;                        // 0..11 = C..B major, 12..23 = C..B minor
    double keyConfidence = 0;
    long firstBeat = 0;                  // frame of the first downbeat (bar-grid anchor)
    long beatAnchor = 0;                 // frame of the first beat (beat-grid anchor)
};

inline const char* keyName (int k)
{
    static const char* n[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static char b[24];
    if (k < 0) return "?";
    std::snprintf (b, sizeof b, "%s %s", n[k % 12], k < 12 ? "major" : "minor");
    return b;
}

// mono float view of interleaved 16-bit stereo
inline float monoAt (const int16_t* pcm, long i) { return (pcm[2 * i] + pcm[2 * i + 1]) * (0.5f / 32768.f); }

// ---- exact attack position: within the analysis window of hop h, the 64-frame block whose energy rises the most
// over the block before it; 1 ms earlier so the attack itself is kept
inline long refineOnset (const int16_t* pcm, long frames, long h, int hop, double rate)
{
    // energy of the high-passed signal (first difference) in 128-frame blocks every 32 frames; the attack is the
    // block that rises most above the average of the 4 blocks before it. High-passing ignores the slow rise and
    // fall of sustained bass / chords, which would otherwise look like attacks.
    const int B = 128, step = 32;
    const long a = h * hop - 4 * step, b = std::min (frames - B, h * hop + 2048);          // before the file = silence
    auto x = [&] (long i) { return i < 0 || i >= frames ? 0.f : monoAt (pcm, i); };
    std::vector<double> e;
    for (long p = a; p + B <= b; p += step)
    {
        double s = 0;
        for (long i = p; i < p + B; ++i) { const float d = x (i) - x (i - 1); s += (double) d * d; }
        e.push_back (s);
    }
    long best = h * hop + 1024; double bestRise = -1e30;
    for (size_t k = 4; k < e.size(); ++k)
    {
        const double before = (e[k - 1] + e[k - 2] + e[k - 3] + e[k - 4]) / 4;
        const double rise = e[k] - 2 * before;
        if (rise > bestRise) { bestRise = rise; best = a + (long) k * step; }
    }
    return std::max (0L, best - (long) (0.001 * rate));
}

inline void analyse (const int16_t* pcm, long frames, double rate, Analysis& A, double maxSeconds = 240.0)
{
    const int N = 2048;
    A.hop = 512;
    const long n = std::min (frames, (long) (maxSeconds * rate));
    const long hops = n > N ? (n - N) / A.hop + 1 : 0;
    A.flux.assign ((size_t) hops, 0.f);
    A.lowFlux.assign ((size_t) hops, 0.f);
    A.chroma.assign ((size_t) hops, std::array<float, 12> {});
    if (hops < 8) return;
    std::vector<float> win (N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / N));
    std::vector<float> prev (N / 2 + 1, 0.f), mag (N / 2 + 1);
    std::vector<std::complex<float>> buf (N);
    // pitch class of every FFT bin in 55 Hz .. 2 kHz (-1 = ignored)
    std::vector<int> pc (N / 2 + 1, -1);
    for (int b = 1; b <= N / 2; ++b)
    {
        const double f = b * rate / N;
        if (f < 55 || f > 2000) continue;
        const double midi = 69 + 12 * std::log2 (f / 440.0);
        pc[(size_t) b] = ((int) std::lround (midi) % 12 + 12) % 12;
    }
    for (long h = 0; h < hops; ++h)
    {
        const long s = h * A.hop;
        for (int i = 0; i < N; ++i) buf[(size_t) i] = std::complex<float> (monoAt (pcm, s + i) * win[(size_t) i], 0.f);
        fft (buf);
        float fl = 0.f, lowFl = 0.f;
        const int lowBins = (int) (150.0 * N / rate);
        std::array<float, 12> ch {};
        for (int b = 0; b <= N / 2; ++b)
        {
            const float m = std::abs (buf[(size_t) b]);
            mag[(size_t) b] = std::log1p (100.f * m);
            const float d = mag[(size_t) b] - prev[(size_t) b];
            if (d > 0) { fl += d; if (b <= lowBins) lowFl += d; }
            if (pc[(size_t) b] >= 0) ch[(size_t) pc[(size_t) b]] += m * m;
        }
        prev.swap (mag);
        A.flux[(size_t) h] = fl; A.lowFlux[(size_t) h] = lowFl;
        A.chroma[(size_t) h] = ch;
    }
    // ---- BPM: autocorrelation of the mean-removed onset envelope
    std::vector<float> e (A.flux);
    {
        // local mean removal (about 1 s) so slow loudness changes don't dominate
        const int W = (int) (rate / A.hop);
        std::vector<float> c (e.size() + 1, 0.f);
        for (size_t i = 0; i < e.size(); ++i) c[i + 1] = c[i] + e[i];
        for (size_t i = 0; i < e.size(); ++i)
        {
            const long a = std::max (0L, (long) i - W), b = std::min ((long) e.size(), (long) i + W + 1);
            const float m = (c[(size_t) b] - c[(size_t) a]) / (float) (b - a);
            e[i] = std::max (0.f, A.flux[i] - m);
        }
    }
    const double hopSec = A.hop / rate;
    double best = 0, bestBpm = 0, sumAll = 1e-9;
    std::vector<double> score;
    for (double bpm = 60; bpm <= 200.001; bpm += 0.25)
    {
        // comb: lags of 1, 2 and 4 beats (fractional lags by linear interpolation)
        double acc = 0;
        for (int mul : { 1, 2, 4 })
        {
            const double lag = 60.0 / bpm / hopSec * mul;
            const long L = (long) lag; const double fr = lag - L;
            double s = 0;
            for (size_t i = 0; i + (size_t) L + 1 < e.size(); ++i) s += e[i] * (e[i + (size_t) L] * (1 - fr) + e[i + (size_t) L + 1] * fr);
            acc += s / mul;
        }
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 110.0) / 0.9, 2));
        acc *= prior;
        sumAll += acc;
        score.push_back (acc);
        if (acc > best) { best = acc; bestBpm = bpm; }
    }
    // refine: long lags (4, 8, 16 beats) pin the period down to 0.01 BPM; then snap to a whole BPM if very close
    {
        auto comb = [&] (double bpm) {
            double acc = 0;
            for (int mul : { 4, 8, 16 })
            {
                const double lag = 60.0 / bpm / hopSec * mul; const long L = (long) lag; const double fr = lag - L;
                for (size_t i = 0; i + (size_t) L + 1 < e.size(); ++i) acc += e[i] * (e[i + (size_t) L] * (1 - fr) + e[i + (size_t) L + 1] * fr);
            }
            return acc; };
        double fine = bestBpm, fb = -1;
        for (double b = bestBpm - 1.0; b <= bestBpm + 1.0; b += 0.01) { const double c = comb (b); if (c > fb) { fb = c; fine = b; } }
        bestBpm = std::fabs (fine - std::round (fine)) < 0.15 ? std::round (fine) : fine;
    }
    while (bestBpm > 160) bestBpm /= 2;
    while (bestBpm > 0 && bestBpm < 70) bestBpm *= 2;
    A.bpm = bestBpm;
    A.bpmConfidence = score.empty() ? 0 : best / (sumAll / (double) score.size()) - 1.0;
    // ---- key: a separate high-resolution pass (8192-point FFT, ~5 Hz bins, so low notes land in the right pitch class);
    //      only bins within 0.35 semitone of a note count, weighted by magnitude; Krumhansl-Kessler profiles
    std::array<double, 12> total {};
    {
        const int K = 8192;
        std::vector<float> kw (K); for (int i = 0; i < K; ++i) kw[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / K));
        std::vector<int> kpc (K / 2 + 1, -1);
        for (int b = 1; b <= K / 2; ++b)
        {
            const double f = b * rate / K;
            if (f < 55 || f > 2000) continue;
            const double midi = 69 + 12 * std::log2 (f / 440.0), near = std::round (midi);
            if (std::fabs (midi - near) < 0.35) kpc[(size_t) b] = ((int) near % 12 + 12) % 12;
        }
        std::vector<std::complex<float>> kb (K);
        for (long st = 0; st + K <= n; st += K / 2)
        {
            for (int i = 0; i < K; ++i) kb[(size_t) i] = std::complex<float> (monoAt (pcm, st + i) * kw[(size_t) i], 0.f);
            fft (kb);
            for (int b = 1; b <= K / 2; ++b) if (kpc[(size_t) b] >= 0) total[(size_t) kpc[(size_t) b]] += std::abs (kb[(size_t) b]);
        }
    }
    static const double maj[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&] (const double* prof, int root) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += total[(size_t) ((i + root) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) { const double x = total[(size_t) ((i + root) % 12)] - mx, y = prof[i] - my; sxy += x * y; sxx += x * x; syy += y * y; }
        return sxx > 0 ? sxy / std::sqrt (sxx * syy) : 0.0; };
    double kb = -2, second = -2;
    for (int k = 0; k < 24; ++k)
    {
        const double c = corr (k < 12 ? maj : mnr, k % 12);
        if (c > kb) { second = kb; kb = c; A.key = k; } else if (c > second) second = c;
    }
    A.keyConfidence = kb - second;
    // ---- beat grid: the first strong onset is a beat; the bar starts on the beat (of 4) with the most kick energy
    long startHop = 0; while (startHop < (long) A.flux.size() && A.flux[(size_t) startHop] < 1e-3f) ++startHop;
    const double beatHops = A.bpm > 0 ? 60.0 / A.bpm / hopSec : 20.0;
    float fmax = 0;
    for (long h = startHop; h < (long) A.flux.size() && h < startHop + (long) (2 * beatHops); ++h) fmax = std::max (fmax, A.flux[(size_t) h]);
    long first = startHop;
    for (long h = startHop; h + 1 < (long) A.flux.size() && h < startHop + (long) (2 * beatHops); ++h)
    {
        const float prevF = h > 0 ? A.flux[(size_t) (h - 1)] : 0.f;
        if (A.flux[(size_t) h] >= 0.3f * fmax && A.flux[(size_t) h] >= prevF && A.flux[(size_t) h] >= A.flux[(size_t) (h + 1)]) { first = h; break; }
    }
    int phase = 0; double bestLow = -1;
    for (int ph = 0; ph < 4; ++ph)
    {
        double sum = 0;
        for (double t = first + ph * beatHops; t < (double) A.lowFlux.size(); t += 4 * beatHops)
            for (long h = (long) t - 1; h <= (long) t + 1; ++h) if (h >= 0 && h < (long) A.lowFlux.size()) sum += A.lowFlux[(size_t) h];
        if (sum > bestLow) { bestLow = sum; phase = ph; }
    }
    A.firstBeat = refineOnset (pcm, frames, first, A.hop, rate);
    A.firstBeat += (long) std::lround (phase * 60.0 / std::max (1.0, A.bpm) * rate);
    A.beatAnchor = A.firstBeat - (long) std::lround (phase * 60.0 / std::max (1.0, A.bpm) * rate);
}

// ---- transients: up to maxN onset frames inside [a, b), strongest first then sorted; sensitivity 0..1
inline std::vector<long> transients (const Analysis& A, long a, long b, int maxN, float sensitivity, double rate, const int16_t* pcm = nullptr, long frames = 0)
{
    std::vector<long> out;
    const long h0 = a / A.hop, h1 = std::min ((long) A.flux.size(), b / A.hop);
    if (h1 - h0 < 3) return out;
    // adaptive threshold: local median-ish (mean over +-8 hops) * factor
    const float factor = 1.0f + 2.5f * (1.0f - sensitivity);
    struct C { long at; float w; };
    std::vector<C> cand;
    for (long h = std::max (h0 + 1, 1L); h + 1 < h1; ++h)
    {
        const float v = A.flux[(size_t) h];
        if (v < A.flux[(size_t) (h - 1)] || v < A.flux[(size_t) (h + 1)]) continue;
        double m = 0; int c = 0;
        for (long j = h - 8; j <= h + 8; ++j) if (j >= 0 && j < (long) A.flux.size()) { m += A.flux[(size_t) j]; ++c; }
        m /= std::max (1, c);
        if (v > m * factor && v > 1e-3f) cand.push_back ({ pcm ? refineOnset (pcm, frames, h, A.hop, rate) : h * A.hop + 1024 - A.hop, v - (float) m });
    }
    std::sort (cand.begin(), cand.end(), [] (const C& x, const C& y) { return x.w > y.w; });
    const long minGap = (long) (0.06 * rate);                          // 60 ms between slices
    for (const C& c : cand)
    {
        if ((int) out.size() >= maxN) break;
        if (c.at < a || c.at >= b) continue;
        bool ok = true;
        for (long o : out) if (std::labs (o - c.at) < minGap) { ok = false; break; }
        if (ok) out.push_back (c.at);
    }
    std::sort (out.begin(), out.end());
    return out;
}

// ---- sections: bar boundaries with the largest change (energy + chroma) between consecutive bars
inline std::vector<long> sections (const Analysis& A, long a, long b, int maxN, double bpm, double rate, int beatsPerBar = 4)
{
    std::vector<long> out;
    if (bpm <= 0) return out;
    const double barFrames = 60.0 / bpm * rate * beatsPerBar;
    long anchor = A.firstBeat;
    while (anchor - (long) barFrames >= a) anchor -= (long) barFrames;
    std::vector<long> bars;
    for (double t = anchor; t < b; t += barFrames) if (t >= a) bars.push_back ((long) t);
    if (bars.size() < 2) { out.push_back (a); return out; }
    auto feat = [&] (long s, long e) {
        std::array<double, 13> f {};
        const long h0 = s / A.hop, h1 = std::min ((long) A.flux.size(), e / A.hop);
        for (long h = h0; h < h1; ++h) { for (int k = 0; k < 12; ++k) f[(size_t) k] += std::sqrt (A.chroma[(size_t) h][(size_t) k]); f[12] += A.flux[(size_t) h]; }
        double n = 0; for (double v : f) n += v * v; n = std::sqrt (n) + 1e-9;
        for (double& v : f) v /= n;
        return f; };
    struct C { long at; double w; };
    std::vector<C> cand { { bars[0], 1e9 } };
    for (size_t i = 1; i < bars.size(); ++i)
    {
        const long e = i + 1 < bars.size() ? bars[i + 1] : b;
        const auto f0 = feat (bars[i - 1], bars[i]), f1 = feat (bars[i], e);
        double d = 0; for (int k = 0; k < 13; ++k) d += (f0[(size_t) k] - f1[(size_t) k]) * (f0[(size_t) k] - f1[(size_t) k]);
        // phrases tend to start every 4 bars: a small bonus keeps ties on phrase lines
        cand.push_back ({ bars[i], d + (i % 4 == 0 ? 0.02 : 0.0) });
    }
    std::sort (cand.begin(), cand.end(), [] (const C& x, const C& y) { return x.w > y.w; });
    for (size_t i = 0; i < cand.size() && (int) out.size() < maxN; ++i) out.push_back (cand[i].at);
    std::sort (out.begin(), out.end());
    return out;
}
} // namespace sl
