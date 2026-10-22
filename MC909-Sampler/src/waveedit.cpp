#include "waveedit.h"

#include <algorithm>
#include <cmath>

namespace mc909 {

Sample trimmed(const Sample& s, uint64_t from, uint64_t to)
{
    const uint64_t n = s.frames();
    from = std::min(from, n);
    to = std::min(std::max(to, from), n);
    Sample out;
    out.channels = s.channels; out.rate = s.rate;
    out.data.assign(s.data.begin() + from * s.channels, s.data.begin() + to * s.channels);
    return out;
}

Sample normalized(const Sample& s, float peak)
{
    Sample out = s;
    float m = 0.f;
    for (float v : s.data) m = std::max(m, std::fabs(v));
    if (m <= 0.f) return out;
    const float g = peak / m;
    for (float& v : out.data) v *= g;
    return out;
}

std::vector<Peak> waveformPeaks(const Sample& s, int bins)
{
    std::vector<Peak> p;
    if (bins <= 0 || s.frames() == 0) return p;
    const uint64_t n = s.frames();
    p.resize((size_t)bins, Peak{0.f, 0.f});
    for (int b = 0; b < bins; ++b)
    {
        const uint64_t a = n * b / bins, e = std::max(a + 1, n * (b + 1) / bins);
        float lo = 0.f, hi = 0.f;
        for (uint64_t i = a; i < e && i < n; ++i)
        {
            const float v = s.data[i * s.channels];
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        p[b] = Peak{lo, hi};
    }
    return p;
}

}  // namespace mc909
