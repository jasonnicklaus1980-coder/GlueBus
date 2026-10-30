// Host test for The Remixer without an MPC: loads the built plugin, runs it like a host would and checks
//   1. audio passes through bit-exact,
//   2. recording writes a valid 24-bit stereo WAV with the right length,
//   3. the recorded samples match what went in (to 24-bit precision),
//   4. nothing is dropped and recording never stops the audio.
// Usage: host_test <path to theremixer.so>
#include "vst2.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
int failures = 0;
void check (bool ok, const char* what)
{
    std::printf ("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}
intptr_t masterStub (AEffect*, int32_t, int32_t, intptr_t, void*, float) { return 0; }

uint32_t rd32 (const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: %s theremixer.so\n", argv[0]); return 2; }
    void* lib = dlopen (argv[1], RTLD_NOW);
    if (lib == nullptr) { std::fprintf (stderr, "dlopen failed: %s\n", dlerror()); return 2; }
    auto entry = (AEffect* (*) (audioMasterCallback)) dlsym (lib, "VSTPluginMain");
    if (entry == nullptr) { std::fprintf (stderr, "no VSTPluginMain\n"); return 2; }

    const char* recDir = "build/test_rec";
    mkdir ("build", 0755);
    mkdir (recDir, 0755);
    setenv ("REMIXER_REC_DIR", recDir, 1);

    AEffect* fx = entry (masterStub);
    check (fx != nullptr && fx->magic == kEffectMagic, "plugin loads and has the VST magic");
    if (fx == nullptr) return 1;
    check (fx->numInputs == 2 && fx->numOutputs == 2, "stereo in, stereo out");
    check ((fx->flags & effFlagsCanReplacing) != 0, "supports processReplacing");

    const double rate = 44100.0;
    fx->dispatcher (fx, effSetSampleRate, 0, 0, nullptr, (float) rate);
    fx->dispatcher (fx, effMainsChanged, 0, 1, nullptr, 0.f);

    const int block = 512;
    const int blocks = 200;                          // about 2.3 s of audio in total
    const int recStart = 40, recEnd = 160;           // recording covers blocks 40 .. 159
    float inL[512], inR[512], outL[512], outR[512];
    float* ins[2] = { inL, inR };
    float* outs[2] = { outL, outR };

    // stereo test tone: different frequency per channel, kept under full scale
    std::vector<float> sentL, sentR;
    long passedBad = 0;
    long clipsSeen = 0;
    for (int b = 0; b < blocks; ++b)
    {
        if (b == recStart) fx->setParameter (fx, 0, 1.f);
        if (b == recEnd) fx->setParameter (fx, 0, 0.f);
        for (int i = 0; i < block; ++i)
        {
            const double t = (double) (b * block + i) / rate;
            inL[i] = (float) (0.5 * std::sin (2 * M_PI * 220.0 * t));
            inR[i] = (float) (0.3 * std::sin (2 * M_PI * 330.0 * t + 0.4));
        }
        fx->processReplacing (fx, ins, outs, block);
        for (int i = 0; i < block; ++i)
        {
            if (std::memcmp (&outL[i], &inL[i], sizeof (float)) != 0 || std::memcmp (&outR[i], &inR[i], sizeof (float)) != 0)
                ++passedBad;
            if (b >= recStart && b < recEnd) { sentL.push_back (inL[i]); sentR.push_back (inR[i]); }
        }
    }
    clipsSeen = (long) fx->getParameter (fx, 3);
    check (passedBad == 0, "audio passes through bit-exact while recording");
    check (clipsSeen == 0, "no clip warnings on a tone under full scale");
    check ((long) fx->getParameter (fx, 5) == 0, "no dropouts");
    check ((long) fx->getParameter (fx, 6) == 2, "status is Saved after stop");

    // the newest rec_*.wav in the test folder is the one we just wrote
    char newest[512] = "";
    {
        char cmd[256];
        std::snprintf (cmd, sizeof cmd, "ls -1 %s/rec_*.wav 2>/dev/null | sort | tail -n 1", recDir);
        FILE* ls = popen (cmd, "r");
        if (ls != nullptr)
        {
            if (std::fgets (newest, sizeof newest, ls) != nullptr) newest[std::strcspn (newest, "\n")] = 0;
            pclose (ls);
        }
    }
    FILE* f = newest[0] ? std::fopen (newest, "rb") : nullptr;
    check (f != nullptr, "a WAV file was written");
    if (f == nullptr) { dlclose (lib); return 1; }

    uint8_t h[44];
    const size_t got = std::fread (h, 1, 44, f);
    check (got == 44 && std::memcmp (h, "RIFF", 4) == 0 && std::memcmp (h + 8, "WAVE", 4) == 0, "RIFF/WAVE header");
    check (rd16 (h + 20) == 1 && rd16 (h + 22) == 2 && rd16 (h + 34) == 24, "PCM, stereo, 24 bit");
    check (rd32 (h + 24) == 44100, "sample rate 44100 in the header");
    const uint32_t dataBytes = rd32 (h + 40);
    const long frames = (long) (dataBytes / 6);
    check (dataBytes == sentL.size() * 6, "data length matches the recorded frames");
    check (rd32 (h + 4) == 36 + dataBytes, "RIFF size is consistent");

    long bad = 0;
    double maxErr = 0.0;
    for (long i = 0; i < frames && i < (long) sentL.size(); ++i)
    {
        uint8_t s[6];
        if (std::fread (s, 1, 6, f) != 6) { ++bad; break; }
        for (int c = 0; c < 2; ++c)
        {
            const uint8_t* q = s + c * 3;
            int32_t v = (int32_t) (q[0] | (q[1] << 8) | (q[2] << 16));
            if (v & 0x800000) v -= 0x1000000;
            const float got24 = (float) v / 8388607.f;
            const float want = c == 0 ? sentL[(size_t) i] : sentR[(size_t) i];
            const double err = std::fabs ((double) got24 - (double) want);
            if (err > maxErr) maxErr = err;
            if (err > 1.0 / 8388607.0 * 1.5) ++bad;
        }
    }
    std::fclose (f);
    std::printf ("     recorded %ld frames (%.3f s), max sample error %.2e\n", frames, frames / rate, maxErr);
    check (bad == 0, "recorded samples match the input to 24-bit precision");

    fx->dispatcher (fx, effClose, 0, 0, nullptr, 0.f);
    dlclose (lib);
    std::printf (failures ? "HOST TEST FAILED (%d)\n" : "host test passed\n", failures);
    return failures ? 1 : 0;
}
