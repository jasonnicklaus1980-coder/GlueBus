#pragma once
// WAV reading and writing. Runs on the loader thread (and in the tests), never on the audio thread.
//  - Reads PCM 8/16/24/32-bit, 32/64-bit float and WAVE_FORMAT_EXTENSIBLE; mono or stereo (extra channels are
//    ignored). Audio is kept as 16-bit stereo to save memory (about 10 MB per stereo minute at 44.1 kHz).
//  - Writes 16-bit stereo PCM (kits, the demo project and test files).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cp
{
struct Pcm
{
    int16_t* pcm = nullptr;          // interleaved stereo
    long frames = 0;
    double rate = 44100.0;
};
inline void freePcm (Pcm* t)
{
    if (t == nullptr) return;
    std::free (t->pcm);
    std::free (t);
}

// ------------------------------------------------------------------------------------------------ WAV
inline uint32_t rd32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

// returns nullptr and a reason on failure
inline Pcm* readWav (const char* path, const char** error, double maxSeconds)
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
    if (frames > (long) (maxSeconds * rate)) { std::fclose (f); *error = "file too long"; return nullptr; }
    Pcm* t = (Pcm*) std::calloc (1, sizeof (Pcm));
    if (t == nullptr) { std::fclose (f); *error = "out of memory"; return nullptr; }
    t->pcm = (int16_t*) std::malloc ((size_t) frames * 2 * sizeof (int16_t));
    if (t->pcm == nullptr) { std::free (t); std::fclose (f); *error = "out of memory"; return nullptr; }
    t->rate = rate;
    const int chunkFrames = 4096;
    unsigned char* buf = (unsigned char*) std::malloc ((size_t) chunkFrames * frameBytes);
    if (buf == nullptr) { freePcm (t); std::fclose (f); *error = "out of memory"; return nullptr; }
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
    if (done < 16) { freePcm (t); *error = "can't read audio"; return nullptr; }
    t->frames = done;
    return t;
}

inline bool writeWav16 (const char* path, const int16_t* stereo, long frames, int rate)
{
    FILE* f = std::fopen (path, "wb");
    if (f == nullptr) return false;
    auto w32 = [&] (uint32_t v) { unsigned char b[4] { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { unsigned char b[2] { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const uint32_t bytes = (uint32_t) (frames * 4);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) rate); w32 ((uint32_t) rate * 4); w16 (4); w16 (16);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (long i = 0; i < 2 * frames; ++i) w16 ((uint16_t) stereo[i]);           // little-endian on any CPU
    const bool ok = std::ferror (f) == 0;
    std::fclose (f);
    return ok;
}
} // namespace cp
