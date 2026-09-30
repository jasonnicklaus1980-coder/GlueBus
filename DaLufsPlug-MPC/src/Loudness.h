#pragma once
// Loudness meter per ITU-R BS.1770-4 / EBU R128 / EBU Tech 3341-3342 (stereo, L and R weighted 1.0):
//  - K-weighting (high shelf + RLB high-pass, coefficients exact for any sample rate), 100 ms sub-blocks
//  - Momentary = 400 ms, Short-term = 3 s, both updated every 100 ms, with their maxima
//  - Integrated = gated (absolute -70 LUFS, relative -10 LU) over the whole measurement
//  - Loudness Range (LRA, Tech 3342) = 95th - 10th percentile of short-term loudness, gated at -70 LUFS and -20 LU
// Integrated and LRA use 0.1 LU histograms: fixed memory for any length, exact to the bin.
#include <cmath>
#include <cstring>

namespace lm
{
class LoudnessMeter
{
public:
    static constexpr double kFloor = -70.0;

    void prepare (double fs)
    {
        {   // stage 1: high shelf (+4 dB above ~1.7 kHz)
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (M_PI * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1.b0 = (Vh + Vb * K / Q + K * K) / a0; s1.b1 = 2.0 * (K * K - Vh) / a0; s1.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s1.a1 = 2.0 * (K * K - 1.0) / a0;       s1.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // stage 2: RLB high-pass (~38 Hz)
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (M_PI * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2.b0 = 1.0; s2.b1 = -2.0; s2.b2 = 1.0;
            s2.a1 = 2.0 * (K * K - 1.0) / a0; s2.a2 = (1.0 - K / Q + K * K) / a0;
        }
        subLen = (int) std::lround (0.1 * fs);
        reset();
    }
    void reset()
    {
        std::memset (st, 0, sizeof st);
        acc = 0.0; accN = 0; nSub = 0; head = 0; steps = 0;
        std::memset (sub, 0, sizeof sub);
        std::memset (iCount, 0, sizeof iCount); std::memset (iSum, 0, sizeof iSum);
        std::memset (rCount, 0, sizeof rCount); std::memset (rSum, 0, sizeof rSum);
        momentary = shortTerm = integrated = maxMomentary = maxShortTerm = kFloor; range = 0.0;
    }
    // one stereo sample; returns true when a 100 ms step completed (new readings)
    inline bool add (double l, double r)
    {
        const double kl = weigh (0, l), kr = weigh (1, r);
        acc += kl * kl + kr * kr;
        if (++accN < subLen) return false;
        step();
        return true;
    }
    double seconds() const { return steps * 0.1; }
    bool shortTermValid() const { return nSub >= kSubs; }

    double momentary = kFloor, shortTerm = kFloor, integrated = kFloor, range = 0.0, maxMomentary = kFloor, maxShortTerm = kFloor;

private:
    static constexpr int kSubs = 30, kBins = 800;           // 3 s of 100 ms sub-blocks; histograms -70 .. +10 LUFS
    struct Co { double b0, b1, b2, a1, a2; } s1 {}, s2 {};
    double st[2][4] {};
    inline double weigh (int c, double x)
    {
        double* z = st[c];
        const double y = s1.b0 * x + z[0]; z[0] = s1.b1 * x - s1.a1 * y + z[1]; z[1] = s1.b2 * x - s1.a2 * y;
        const double y2 = s2.b0 * y + z[2]; z[2] = s2.b1 * y - s2.a1 * y2 + z[3]; z[3] = s2.b2 * y - s2.a2 * y2;
        for (int i = 0; i < 4; ++i) if (std::fabs (z[i]) < 1e-30) z[i] = 0;
        return y2;
    }
    static double lufs (double ms) { return ms > 0 ? -0.691 + 10.0 * std::log10 (ms) : -1e9; }
    static int binOf (double L) { int b = (int) ((L - kFloor) * 10.0); return b < 0 ? 0 : (b >= kBins ? kBins - 1 : b); }
    double mean (int count) const { double s = 0; for (int i = 1; i <= count; ++i) s += sub[(head - i + kSubs) % kSubs]; return s / count; }
    void step()
    {
        sub[head] = acc / accN; head = (head + 1) % kSubs; if (nSub < kSubs) ++nSub;
        acc = 0.0; accN = 0; ++steps;
        if (nSub >= 4)
        {
            const double z = mean (4), L = lufs (z);
            momentary = std::fmax (kFloor, L);
            maxMomentary = std::fmax (maxMomentary, momentary);
            if (L >= kFloor) { const int b = binOf (L); ++iCount[b]; iSum[b] += z; }   // 400 ms gating block, 75 % overlap
            integrated = gatedIntegrated();
        }
        if (nSub >= kSubs)
        {
            const double z = mean (kSubs), L = lufs (z);
            shortTerm = std::fmax (kFloor, L);
            maxShortTerm = std::fmax (maxShortTerm, shortTerm);
            if (L >= kFloor) { const int b = binOf (L); ++rCount[b]; rSum[b] += z; }
            range = loudnessRange();
        }
        else shortTerm = kFloor;
    }
    double gatedIntegrated() const
    {
        double s = 0; unsigned long n = 0;
        for (int b = 0; b < kBins; ++b) { s += iSum[b]; n += iCount[b]; }
        if (n == 0) return kFloor;
        const double rel = lufs (s / n) - 10.0;
        double s2 = 0; unsigned long n2 = 0;
        for (int b = 0; b < kBins; ++b) if (kFloor + (b + 1) * 0.1 > rel) { s2 += iSum[b]; n2 += iCount[b]; }
        return n2 ? std::fmax (kFloor, lufs (s2 / n2)) : kFloor;
    }
    double loudnessRange() const
    {
        double s = 0; unsigned long n = 0;
        for (int b = 0; b < kBins; ++b) { s += rSum[b]; n += rCount[b]; }
        if (n == 0) return 0.0;
        const double rel = lufs (s / n) - 20.0;
        int first = binOf (rel); if (kFloor + first * 0.1 < rel) ++first;
        unsigned long total = 0;
        for (int b = first; b < kBins; ++b) total += rCount[b];
        if (total == 0) return 0.0;
        auto percentile = [&] (double p)
        {
            const double want = p * (total - 1);
            unsigned long c = 0;
            for (int b = first; b < kBins; ++b) { c += rCount[b]; if (c > want) return kFloor + (b + 0.5) * 0.1; }
            return kFloor + (kBins - 0.5) * 0.1;
        };
        return std::fmax (0.0, percentile (0.95) - percentile (0.10));
    }
    int subLen = 4410, accN = 0, nSub = 0, head = 0;
    long steps = 0;
    double acc = 0.0, sub[kSubs] {};
    unsigned long iCount[kBins] {}, rCount[kBins] {};
    double iSum[kBins] {}, rSum[kBins] {};
};

// True peak per BS.1770-4 Annex 2: oversample to >= 176.4 kHz (4x at 44.1/48, 2x at 88.2/96, none above),
// windowed-sinc polyphase interpolator (12 taps per phase), max |sample| over all phases, in dBTP.
class TruePeak
{
public:
    void prepare (double fs)
    {
        factor = fs < 88000.0 ? 4 : (fs < 176000.0 ? 2 : 1);
        // phase p estimates the signal at t = p / factor samples after x[n - kDelay] (phase 0 = the sample itself)
        const double half = kTaps / 2.0;
        for (int p = 0; p < factor; ++p)
        {
            double s = 0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double u = k - kDelay - (double) p / factor;      // distance (in samples) from the wanted instant
                const double sinc = std::fabs (u) < 1e-12 ? 1.0 : std::sin (M_PI * u) / (M_PI * u);
                const double w = std::fabs (u) >= half ? 0.0 : 0.42 + 0.5 * std::cos (M_PI * u / half) + 0.08 * std::cos (2.0 * M_PI * u / half);
                h[p][k] = sinc * w; s += h[p][k];
            }
            for (int k = 0; k < kTaps; ++k) h[p][k] /= s;             // unity gain at DC
        }
        reset();
    }
    void reset() { std::memset (hist, 0, sizeof hist); pos = 0; peak = 0.0; }
    inline void add (double l, double r)
    {
        if (factor == 1) { const double a = std::fmax (std::fabs (l), std::fabs (r)); if (a > peak) peak = a; return; }
        pos = (pos + 1) % kTaps;
        hist[0][pos] = hist[0][pos + kTaps] = l; hist[1][pos] = hist[1][pos + kTaps] = r;
        for (int c = 0; c < 2; ++c)
        {
            const double* x = &hist[c][pos + kTaps];                          // x[0] newest, x[-k] older
            for (int p = 0; p < factor; ++p)
            {
                double y = 0; for (int k = 0; k < kTaps; ++k) y += h[p][k] * x[-k];
                const double a = std::fabs (y); if (a > peak) peak = a;
            }
        }
    }
    double dbtp() const { return peak > 0 ? 20.0 * std::log10 (peak) : -120.0; }
    double peak = 0.0;

private:
    static constexpr int kTaps = 12, kDelay = kTaps / 2 - 1;
    int factor = 4, pos = 0;
    double h[4][kTaps] {};
    double hist[2][2 * kTaps] {};
};
} // namespace lm
