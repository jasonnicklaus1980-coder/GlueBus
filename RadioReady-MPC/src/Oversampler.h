#pragma once
// RadioReady EQ - 2x oversampling for the HQ mode: a 63-tap linear-phase halfband FIR (Kaiser window, beta 7.5),
// run polyphase so only the 16 non-zero tap pairs are computed.
// Halfband: h[31] = 0.5, h[31 +- n] = 0 for even n != 0, so
//   up:   v[2m] = 2 * sum_k c[k] (x[m-15+k] + x[m-16-k]),  v[2m+1] = x[m-15]
//   down: y[m] = 0.5 * o[m-16] + sum_k c[k] (e[m-15+k] + e[m-16-k])   (e/o: even/odd 2x samples of pair m)
// Latency: 31 samples at 2x going up + 31 going down = 31 base-rate samples.
#include <cmath>

namespace rr
{
class Oversampler2x
{
public:
    static constexpr int kTaps = 63, kHalf = 16, kLatency = 31;

    Oversampler2x()
    {
        const double beta = 7.5;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        double sum = 0;
        for (int k = 0; k < kHalf; ++k)
        {
            const int n = 2 * k + 1;                                  // odd offset from the centre tap
            const double r = n / 31.0;
            coef[k] = std::sin (M_PI * n / 2.0) / (M_PI * n) * i0 (beta * std::sqrt (std::fmax (0.0, 1.0 - r * r))) / i0 (beta);
            sum += coef[k];
        }
        for (double& c : coef) c *= 0.25 / sum;                       // DC gain exactly 1 (0.5 + 2 * 0.25)
        reset();
    }
    void reset()
    {
        for (auto& c : ch) { for (int i = 0; i < 2 * kBuf; ++i) c.x[i] = c.e[i] = c.o[i] = 0; c.pu = c.pd = 0; }
    }
    // one base-rate sample in -> two 2x-rate samples out (in time order)
    inline void up (int c, double in, double& v0, double& v1)
    {
        Chan& s = ch[c];
        s.pu = (s.pu + 1) & (kBuf - 1);
        s.x[s.pu] = in; s.x[s.pu + kBuf] = in;                         // mirrored ring: p[-d] valid for d < kBuf
        const double* p = s.x + s.pu + kBuf;
        double acc = 0;
        for (int k = 0; k < kHalf; ++k) acc += coef[k] * (p[-15 + k] + p[-16 - k]);
        v0 = 2.0 * acc;
        v1 = p[-15];
    }
    // two 2x-rate samples in -> one base-rate sample out
    inline double down (int c, double in0, double in1)
    {
        Chan& s = ch[c];
        s.pd = (s.pd + 1) & (kBuf - 1);
        s.e[s.pd] = in0; s.e[s.pd + kBuf] = in0;
        s.o[s.pd] = in1; s.o[s.pd + kBuf] = in1;
        const double* e = s.e + s.pd + kBuf;
        const double* o = s.o + s.pd + kBuf;
        double acc = 0.5 * o[-16];
        for (int k = 0; k < kHalf; ++k) acc += coef[k] * (e[-15 + k] + e[-16 - k]);
        return acc;
    }

private:
    static constexpr int kBuf = 64;
    struct Chan { double x[2 * kBuf], e[2 * kBuf], o[2 * kBuf]; int pu, pd; } ch[2];
    double coef[kHalf];
};
} // namespace rr
