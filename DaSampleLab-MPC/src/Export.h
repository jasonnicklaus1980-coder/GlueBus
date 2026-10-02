#pragma once
// Slice export at full quality: the slice is cut from the ORIGINAL file (its own sample rate and resolution), not from
// the 16-bit playback copy, and written with the same bit depth (8/16-bit -> 16-bit, 24-bit -> 24-bit,
// 32-bit / float -> 24-bit). Loader thread only.
#include "core/Wav.h"
#include <vector>

namespace sl
{
struct Original { std::vector<float> l, r; double rate = 44100; int bits = 16; };

inline bool readOriginal (const char* path, long fromFrame, long frames, Original& o)
{
    using cp::rd16; using cp::rd32;
    FILE* f = std::fopen (path, "rb");
    if (! f) return false;
    unsigned char h[12];
    if (std::fread (h, 1, 12, f) != 12 || std::memcmp (h, "RIFF", 4) || std::memcmp (h + 8, "WAVE", 4)) { std::fclose (f); return false; }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0, size = 0; long data = -1; unsigned char c[8];
    while (std::fread (c, 1, 8, f) == 8)
    {
        const uint32_t sz = rd32 (c + 4); const long here = std::ftell (f);
        if (! std::memcmp (c, "fmt ", 4))
        {
            unsigned char m[40] = {}; const size_t n = sz < 40 ? sz : 40;
            if (std::fread (m, 1, n, f) != n) break;
            fmt = rd16 (m); ch = rd16 (m + 2); rate = rd32 (m + 4); bits = rd16 (m + 14);
            if (fmt == 0xFFFE && n >= 26) fmt = rd16 (m + 24);
        }
        else if (! std::memcmp (c, "data", 4)) { data = here; size = sz; break; }
        std::fseek (f, here + (long) sz + (sz & 1), SEEK_SET);
    }
    const bool fl = fmt == 3;
    if (data < 0 || ch < 1 || ! ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fl && (bits == 32 || bits == 64)))) { std::fclose (f); return false; }
    const int bps = bits / 8, fb = bps * ch;
    const long total = (long) (size / (uint32_t) fb);
    if (fromFrame < 0) fromFrame = 0;
    if (fromFrame + frames > total) frames = total - fromFrame;
    if (frames <= 0) { std::fclose (f); return false; }
    o.rate = rate; o.bits = fl ? 32 : bits;
    o.l.resize ((size_t) frames); o.r.resize ((size_t) frames);
    std::fseek (f, data + fromFrame * fb, SEEK_SET);
    std::vector<unsigned char> buf ((size_t) fb * 4096);
    auto smp = [&] (const unsigned char* p) -> float {
        switch (bits) {
            case 8: return (p[0] - 128) / 128.f;
            case 16: return (int16_t) rd16 (p) / 32768.f;
            case 24: return (float) ((int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) / 2147483648.0);
            case 32: { const uint32_t u = rd32 (p); if (fl) { float x; std::memcpy (&x, &u, 4); return x; } return (float) ((int32_t) u / 2147483648.0); }
            default: { uint64_t u = rd32 (p) | ((uint64_t) rd32 (p + 4) << 32); double x; std::memcpy (&x, &u, 8); return (float) x; } } };
    long done = 0;
    while (done < frames)
    {
        const long want = frames - done < 4096 ? frames - done : 4096;
        const long got = (long) std::fread (buf.data(), (size_t) fb, (size_t) want, f);
        for (long i = 0; i < got; ++i)
        {
            const unsigned char* p = buf.data() + i * fb;
            o.l[(size_t) (done + i)] = smp (p); o.r[(size_t) (done + i)] = ch > 1 ? smp (p + bps) : o.l[(size_t) (done + i)];
        }
        done += got;
        if (got < want) break;
    }
    std::fclose (f);
    o.l.resize ((size_t) done); o.r.resize ((size_t) done);
    return done > 0;
}

// writes 16- or 24-bit stereo PCM; returns false on any error (partial files are removed)
inline bool writeWav (const char* path, const std::vector<float>& l, const std::vector<float>& r, int rate, int bits)
{
    const int bps = bits == 24 ? 3 : 2;
    FILE* f = std::fopen (path, "wb");
    if (! f) return false;
    auto w32 = [&] (uint32_t v) { unsigned char b[4] { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { unsigned char b[2] { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const uint32_t bytes = (uint32_t) (l.size() * 2 * bps);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) rate); w32 ((uint32_t) rate * 2 * bps); w16 ((uint16_t) (2 * bps)); w16 ((uint16_t) (8 * bps));
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    std::vector<unsigned char> out ((size_t) 2 * bps * 1024);
    for (size_t i = 0; i < l.size(); i += 1024)
    {
        const size_t n = l.size() - i < 1024 ? l.size() - i : 1024;
        for (size_t k = 0; k < n; ++k)
            for (int c = 0; c < 2; ++c)
            {
                float x = (c ? r : l)[i + k]; x = x > 1.f ? 1.f : (x < -1.f ? -1.f : x);
                unsigned char* p = out.data() + (2 * k + (size_t) c) * (size_t) bps;
                // full-scale = 2^23 (2^15): a 24-bit (16-bit) source comes back bit for bit
                if (bps == 3) { int32_t v = (int32_t) std::lround ((double) x * 8388608.0); v = v > 8388607 ? 8388607 : v; p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); }
                else { int32_t v = (int32_t) std::lround ((double) x * 32768.0); v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v); p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
            }
        std::fwrite (out.data(), 1, 2 * (size_t) bps * n, f);
    }
    const bool ok = std::ferror (f) == 0;
    std::fclose (f);
    if (! ok) std::remove (path);
    return ok;
}
} // namespace sl
