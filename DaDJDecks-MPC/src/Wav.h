#pragma once
// Da DJ Decks - track loading (WAV) and beat analysis. Runs on the loader thread, never on the audio thread.
//  - WAV: PCM 8/16/24/32-bit, 32/64-bit float, WAVE_FORMAT_EXTENSIBLE; mono or stereo (extra channels ignored);
//    any sample rate (the deck resamples). Stored as 16-bit stereo to keep memory down (~10 MB per stereo minute).
//  - BPM: onset envelope (10 ms hops, low band + high band energy rises), autocorrelation for the tempo, then a
//    fine search (0.01 BPM) that also finds where the beats fall (the beat grid used by SYNC, loops and beat lights).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dj
{
// fmod without libm's fmod (whose newest ARM symbol version needs glibc 2.38; MPC OS has 2.34)
inline double fmodd (double a, double b) { return a - b * std::trunc (a / b); }

struct Track
{
    int16_t* pcm = nullptr;          // interleaved stereo
    long frames = 0;
    double rate = 44100.0;
    double bpm = 0.0;                // 0 = unknown
    double offset = 0.0;             // first beat, in frames
    char name[96] = {};
};

inline void freeTrack (Track* t)
{
    if (t == nullptr) return;
    std::free (t->pcm);
    std::free (t);
}

// ------------------------------------------------------------------------------------------------ WAV
inline uint32_t rd32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

// returns nullptr and a reason on failure
inline Track* loadWav (const char* path, const char** error, double maxSeconds = 1200.0)
{
    *error = nullptr;
    FILE* f = std::fopen (path, "rb");
    if (f == nullptr) { *error = "can't open file"; return nullptr; }
    unsigned char hdr[12];
    if (std::fread (hdr, 1, 12, f) != 12 || std::memcmp (hdr, "RIFF", 4) != 0 || std::memcmp (hdr + 8, "WAVE", 4) != 0)
    { std::fclose (f); *error = "not a WAV file"; return nullptr; }
    int format = 0, channels = 0, bits = 0; uint32_t rate = 0; long dataPos = -1; uint32_t dataSize = 0;
    unsigned char ch[8];
    while (std::fread (ch, 1, 8, f) == 8)
    {
        const uint32_t size = rd32 (ch + 4);
        const long here = std::ftell (f);
        if (std::memcmp (ch, "fmt ", 4) == 0)
        {
            unsigned char fm[40] = {};
            const size_t n = size < sizeof fm ? size : sizeof fm;
            if (std::fread (fm, 1, n, f) != n) break;
            format = rd16 (fm); channels = rd16 (fm + 2); rate = rd32 (fm + 4); bits = rd16 (fm + 14);
            if (format == 0xFFFE && n >= 26) format = rd16 (fm + 24);            // extensible: sub-format GUID
        }
        else if (std::memcmp (ch, "data", 4) == 0) { dataPos = here; dataSize = size; break; }
        if (std::fseek (f, here + (long) size + (size & 1), SEEK_SET) != 0) break;
    }
    if (dataPos < 0 || channels < 1 || rate < 8000 || rate > 384000) { std::fclose (f); *error = "unsupported WAV"; return nullptr; }
    const bool isFloat = format == 3;
    if (! ((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    { std::fclose (f); *error = "unsupported WAV format"; return nullptr; }
    const int bps = bits / 8, frameBytes = bps * channels;
    long frames = (long) (dataSize / (uint32_t) frameBytes);
    if (dataSize == 0 || dataSize == 0xFFFFFFFFu)                              // unknown size: read to end of file
    {
        std::fseek (f, 0, SEEK_END); frames = (std::ftell (f) - dataPos) / frameBytes; std::fseek (f, dataPos, SEEK_SET);
    }
    if (frames < 16) { std::fclose (f); *error = "empty WAV"; return nullptr; }
    if (frames > (long) (maxSeconds * rate)) { std::fclose (f); *error = "track longer than 20 minutes"; return nullptr; }
    Track* t = (Track*) std::calloc (1, sizeof (Track));
    if (t == nullptr) { std::fclose (f); *error = "out of memory"; return nullptr; }
    t->pcm = (int16_t*) std::malloc ((size_t) frames * 2 * sizeof (int16_t));
    if (t->pcm == nullptr) { std::free (t); std::fclose (f); *error = "out of memory"; return nullptr; }
    t->rate = rate;
    const int chunkFrames = 4096;
    unsigned char* buf = (unsigned char*) std::malloc ((size_t) chunkFrames * frameBytes);
    if (buf == nullptr) { freeTrack (t); std::fclose (f); *error = "out of memory"; return nullptr; }
    auto sample = [&] (const unsigned char* p) -> double
    {
        switch (bits)
        {
            case 8:  return (p[0] - 128) / 128.0;
            case 16: return (int16_t) rd16 (p) / 32768.0;
            case 24: { int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24); return v / 2147483648.0; }
            case 32: { const uint32_t u = rd32 (p); if (isFloat) { float x; std::memcpy (&x, &u, 4); return x; } return (int32_t) u / 2147483648.0; }
            default: { uint64_t u = (uint64_t) rd32 (p) | ((uint64_t) rd32 (p + 4) << 32); double x; std::memcpy (&x, &u, 8); return x; }
        }
    };
    auto to16 = [] (double x) { x = x > 1.0 ? 1.0 : (x < -1.0 ? -1.0 : x); return (int16_t) std::lround (x * 32767.0); };
    long done = 0;
    while (done < frames)
    {
        const long want = frames - done < chunkFrames ? frames - done : chunkFrames;
        const long got = (long) std::fread (buf, (size_t) frameBytes, (size_t) want, f);
        for (long i = 0; i < got; ++i)
        {
            const unsigned char* p = buf + i * frameBytes;
            const double l = sample (p), r = channels > 1 ? sample (p + bps) : l;
            t->pcm[2 * (done + i)] = to16 (l); t->pcm[2 * (done + i) + 1] = to16 (r);
        }
        done += got;
        if (got < want) break;                                                     // truncated file: keep what we have
    }
    std::free (buf); std::fclose (f);
    if (done < 16) { freeTrack (t); *error = "can't read audio"; return nullptr; }
    t->frames = done;
    return t;
}

// ------------------------------------------------------------------------------------------------ beat analysis
inline void analyzeBeats (Track* t)
{
    t->bpm = 0.0; t->offset = 0.0;
    const int hop = (int) std::lround (t->rate * 0.01);                            // 10 ms
    const long maxHops = 9000;                                                     // first 90 s
    long nh = t->frames / hop; if (nh > maxHops) nh = maxHops;
    if (nh < 400) return;                                                          // < 4 s: no tempo
    float* on = (float*) std::calloc ((size_t) nh, sizeof (float));
    if (on == nullptr) return;
    // onset strength: rises in low-band (kick) and high-band (snare / hats) energy, log domain
    const double aLow = std::exp (-2.0 * M_PI * 150.0 / t->rate);
    double lp = 0, prevL = 0, prevH = 0;
    for (long h = 0; h < nh; ++h)
    {
        double eL = 0, eH = 0;
        for (int i = 0; i < hop; ++i)
        {
            const long n = h * hop + i;
            const double x = (t->pcm[2 * n] + t->pcm[2 * n + 1]) * (0.5 / 32768.0);
            lp = x + (lp - x) * aLow;
            eL += lp * lp; eH += (x - lp) * (x - lp);
        }
        const double L = std::log (eL + 1e-9), Hh = std::log (eH + 1e-9);
        on[h] = (float) (std::fmax (0.0, L - prevL) + 0.6 * std::fmax (0.0, Hh - prevH));
        prevL = L; prevH = Hh;
    }
    // remove the slow average (1 s) so steady noise doesn't count as onsets
    float* o = (float*) std::calloc ((size_t) nh, sizeof (float));
    if (o == nullptr) { std::free (on); return; }
    double acc = 0; for (long h = 0; h < nh && h < 100; ++h) acc += on[h];
    for (long h = 0; h < nh; ++h)
    {
        const long a = h - 50, b = h + 50;
        if (b < nh && b >= 100) acc += on[b];
        if (a - 1 >= 0 && b >= 100) acc -= on[a - 1];
        const long lo = a < 0 ? 0 : a, hi = b >= nh ? nh - 1 : b;
        const double mean = acc / (double) (hi - lo + 1);
        o[h] = (float) std::fmax (0.0, on[h] - mean);
    }
    std::free (on);
    // tempo + beat phase: fold the onsets onto one beat for every candidate tempo (78..156 BPM) and keep the
    // tempo whose strongest phase collects the most onset energy; then refine around it
    long nOn = 0; for (long h = 0; h < nh; ++h) if (o[h] > 0.f) ++nOn;
    long* at = (long*) std::malloc (sizeof (long) * (size_t) (nOn + 1)); float* w = (float*) std::malloc (sizeof (float) * (size_t) (nOn + 1));
    if (at == nullptr || w == nullptr) { std::free (at); std::free (w); std::free (o); return; }
    nOn = 0; for (long h = 0; h < nh; ++h) if (o[h] > 0.f) { at[nOn] = h; w[nOn] = o[h]; ++nOn; }
    const int kBins = 64;
    auto score = [&] (double bpm, double& phase)
    {
        const double period = 6000.0 / bpm;                                         // hops per beat
        double hist[kBins] = {};
        for (long i = 0; i < nOn; ++i) { const double ph = fmodd ((double) at[i], period) / period; hist[(int) (ph * kBins) % kBins] += w[i]; }
        double best = -1;
        for (int b = 0; b < kBins; ++b)
        {
            const double s = hist[b] + 0.5 * (hist[(b + 1) % kBins] + hist[(b + kBins - 1) % kBins]);
            if (s > best) { best = s; phase = (b + 0.5) / kBins; }
        }
        return best;
    };
    double bestScore = -1, bestBpm = 0, bestPhase = 0, ph = 0;
    for (double bpm = 78.0; bpm < 156.0; bpm += 0.05) { const double s = score (bpm, ph); if (s > bestScore) { bestScore = s; bestBpm = bpm; bestPhase = ph; } }
    const double coarse = bestBpm;
    for (double bpm = coarse - 0.06; bpm <= coarse + 0.06; bpm += 0.002) { const double s = score (bpm, ph); if (s > bestScore) { bestScore = s; bestBpm = bpm; bestPhase = ph; } }
    std::free (at); std::free (w);
    std::free (o);
    t->bpm = std::round (bestBpm * 100.0) / 100.0;
    const double beatFrames = t->rate * 60.0 / t->bpm;
    t->offset = fmodd (bestPhase * (6000.0 / bestBpm) * hop, beatFrames);
}
} // namespace dj
