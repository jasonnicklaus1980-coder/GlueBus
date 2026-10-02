// Offline VST2 host test for the MPC build of G-Glue: loads the .so like MPC OS does (dlopen + VSTPluginMain),
// processes audio and drives every on-screen control through setParameter, the way the MPC screen / Q-Links do.
// Usage: host_test <gglue.so> <work dir>        (GGLUE_EMULATED=1 skips the CPU budget check under QEMU)
#include "../src/vst2.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; const bool ok_ = (c); std::printf ("  %s ", ok_ ? "ok  " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)
static const double kPi = 3.14159265358979323846;

typedef AEffect* (*MainFn) (audioMasterCallback);
typedef const char* (*KeyFn) (int);
typedef int (*CountFn) ();

static std::map<int, int> automated;                // parameter index -> number of audioMasterAutomate calls
static intptr_t hostCb (AEffect*, int32_t op, int32_t idx, intptr_t, void*, float)
{
    if (op == audioMasterAutomate) automated[idx]++;
    if (op == 1) return 2400;                        // audioMasterVersion
    return 0;
}

struct Inst
{
    AEffect* e = nullptr;
    KeyFn key = nullptr;
    int n = 0;
    double sr = 48000;
    std::vector<float> L, R;
    intptr_t D (int op, int idx = 0, intptr_t val = 0, void* ptr = nullptr, float opt = 0.f) { return e->dispatcher (e, op, idx, val, ptr, opt); }
    int P (const char* k) { for (int i = 0; i < n; ++i) if (! std::strcmp (key (i), k)) return i; std::printf ("no parameter %s\n", k); std::exit (2); }
    std::string disp (const char* k) { char b[256] = {}; D (effGetParamDisplay, P (k), 0, b); return b; }
    std::string name (int i) { char b[64] = {}; D (effGetParamName, i, 0, b); return b; }
    void set (const char* k, float v) { e->setParameter (e, P (k), v); }
    float get (const char* k) { return e->getParameter (e, P (k)); }
    void press (const char* k) { const int i = P (k); e->setParameter (e, i, e->getParameter (e, i) > 0.5f ? 0.f : 1.f); }
    void rate (double r) { sr = r; D (effSetSampleRate, 0, 0, nullptr, (float) r); D (effMainsChanged, 0, 1); }
    // process `seconds` of sine (amp, hz); keeps the output in L / R
    void sine (double seconds, double amp, double hz = 1000, int block = 256)
    {
        const int total = (int) (sr * seconds);
        L.assign ((size_t) total, 0.f); R.assign ((size_t) total, 0.f);
        std::vector<float> il ((size_t) block), ir ((size_t) block);
        for (int pos = 0; pos < total; pos += block)
        {
            const int m = std::min (block, total - pos);
            for (int i = 0; i < m; ++i) il[(size_t) i] = ir[(size_t) i] = (float) (amp * std::sin (2 * kPi * hz * (pos + i) / sr));
            float* in[2] = { il.data(), ir.data() };
            float* out[2] = { L.data() + pos, R.data() + pos };
            e->processReplacing (e, in, out, m);
        }
    }
    double rmsDb (size_t a, size_t b) const
    {
        double s = 0; for (size_t i = a; i < b; ++i) s += (double) L[i] * L[i];
        return 20 * std::log10 (std::sqrt (s / (double) (b - a)) + 1e-12);
    }
};

static bool open (Inst& p, void* lib)
{
    auto main = (MainFn) dlsym (lib, "VSTPluginMain");
    p.key = (KeyFn) dlsym (lib, "GG_ParamKey");
    auto count = (CountFn) dlsym (lib, "GG_ParamCount");
    if (! main || ! p.key || ! count) return false;
    p.e = main (hostCb);
    p.n = count();
    if (! p.e) return false;
    p.D (effOpen);
    p.rate (48000);
    return true;
}

static int countFiles (const std::string& dir, const char* ext)
{
    int n = 0;
    if (DIR* d = opendir (dir.c_str()))
    {
        while (dirent* f = readdir (d)) { const size_t l = std::strlen (f->d_name); if (l > 6 && ! std::strcmp (f->d_name + l - 6, ext)) ++n; }
        closedir (d);
    }
    return n;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: host_test gglue.so workdir\n"); return 2; }
    const std::string work = argv[2], presets = work + "/Presets";
    mkdir (work.c_str(), 0755);
    setenv ("GGLUE_PRESET_DIR", presets.c_str(), 1);
    void* lib = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (! lib) { std::printf ("dlopen failed: %s\n", dlerror()); return 1; }

    std::printf ("Loading (like MPC OS: dlopen + VSTPluginMain)\n");
    Inst p;
    CHECK (open (p, lib), "plugin loads");
    char b[64] = {};
    p.D (effGetEffectName, 0, 0, b); const std::string nm = b;
    p.D (effGetVendorString, 0, 0, b); const std::string vendor = b;
    CHECK (nm == "G-Glue Bus Compressor" && vendor == "G-Glue Audio" && p.D (effGetPlugCategory) == kPlugCategEffect,
           "\"%s\" by %s, an effect", nm.c_str(), vendor.c_str());
    CHECK (p.e->numInputs == 2 && p.e->numOutputs == 2 && p.e->uniqueID == (('G' << 24) | ('g' << 16) | ('B' << 8) | 'c'),
           "stereo in / out, unique ID 'GgBc' (same as the desktop build)");
    const char* order[] { "Threshold", "Makeup", "Attack", "Release", "Ratio", "SC Filter", "Mix", "Input", "Output", "Analog", "Bypass" };
    bool ok = true; for (int i = 0; i < 11; ++i) ok = ok && p.name (i) == order[i];
    CHECK (ok && p.e->numParams == p.n, "%d parameters; 1-11 in Q-Link order: Threshold, Makeup, Attack, Release, Ratio, SC Filter ...", p.n);
    ok = true; for (int i = 0; i < 11; ++i) ok = ok && p.D (effCanBeAutomated, i) == 1;
    CHECK (ok && p.D (effCanBeAutomated, p.P ("gr")) == 0, "the 11 compressor parameters are automatable, readouts are not");
    CHECK (p.disp ("threshold") == "-10.0 dB" && p.disp ("release") == "AUTO" && p.disp ("ratio") == "4:1" && p.disp ("scfilter") == "OFF",
           "screen texts: %s, %s, %s, %s", p.disp ("threshold").c_str(), p.disp ("release").c_str(), p.disp ("ratio").c_str(), p.disp ("scfilter").c_str());

    std::printf ("Audio\n");
    for (double r : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        p.rate (r);
        p.sine (1.0, 0.9);
        const double out = p.rmsDb ((size_t) (r * 0.5), (size_t) r) - 20 * std::log10 (0.9 / std::sqrt (2.0));
        CHECK (out < -5 && out > -20, "%.1f kHz: -0.9 dBFS sine comes out %.1f dB (defaults -10 dB, 4:1)", r / 1000, out);
    }
    p.rate (48000);
    p.set ("threshold", 1.f);
    p.sine (0.5, 0.5);
    const double clean = p.rmsDb (12000, 24000) - 20 * std::log10 (0.5 / std::sqrt (2.0));
    CHECK (std::fabs (clean) < 0.1, "threshold +10 dB: no compression (%.2f dB)", clean);
    p.set ("threshold", 0.f);
    automated.clear();
    p.sine (1.0, 0.9);
    const float gr = p.get ("gr"), inm = p.get ("inmeter"), outm = p.get ("outmeter");
    CHECK (gr > 0.4f && automated[p.P ("gr")] > 3 && automated[p.P ("gr")] < 200,
           "GR needle: %s (position %.2f), %d screen updates in 1 s (only when the frame changes)", p.disp ("gr").c_str(), gr, automated[p.P ("gr")]);
    CHECK (inm > 0.9f && outm < inm, "input meter %s, output meter %s", p.disp ("inmeter").c_str(), p.disp ("outmeter").c_str());
    // needle falls back with the release (AUTO: slow after sustained compression)
    p.sine (6.0, 0.0);
    CHECK (p.get ("gr") < 0.05f, "6 s of silence after 21 dB of GR (AUTO release): needle back to %s", p.disp ("gr").c_str());
    p.set ("threshold", (float) (20.0 / 40.0));          // -10 dB

    std::printf ("Bypass and automation\n");
    p.set ("bypass", 1.f);
    p.sine (0.3, 0.5);
    double diff = 0;
    for (size_t i = 2400; i < p.L.size(); ++i) diff = std::max (diff, std::fabs ((double) p.L[i] - (double) (float) (0.5 * std::sin (2 * kPi * 1000 * (double) i / 48000))));
    CHECK (diff == 0.0, "Bypass ON: output = input, bit for bit");
    p.set ("bypass", 0.f);
    {
        const int block = 64, total = 48000 * 2;
        std::vector<float> il ((size_t) block), ir ((size_t) block), out ((size_t) total), outR ((size_t) total);
        for (int pos = 0, k = 0; pos < total; pos += block, ++k)
        {
            p.set ("threshold", (float) (0.5 + 0.5 * std::sin (k * 0.37)));
            p.set ("makeup", (float) (0.25 + 0.25 * std::sin (k * 0.21)));
            for (int i = 0; i < block; ++i) il[(size_t) i] = ir[(size_t) i] = (float) (0.5 * std::sin (2 * kPi * 100 * (pos + i) / 48000.0));
            float* in[2] = { il.data(), ir.data() }; float* o[2] = { out.data() + pos, outR.data() + pos };
            p.e->processReplacing (p.e, in, o, block);
        }
        double worst = 0; for (size_t i = 4800; i + 2 < out.size(); ++i) worst = std::max (worst, (double) std::fabs (out[i + 2] - 2 * out[i + 1] + out[i]));
        CHECK (worst < 0.01, "threshold + makeup automated every 64 samples: largest step %.4f (no zipper noise)", worst);
    }

    std::printf ("Screen controls: presets, favourites, save, delete, A/B\n");
    p.set ("threshold", 0.5f);
    {
        Inst g;                                                  // a fresh instance: its 2.5 s guard has just started
        open (g, lib);
        g.press ("load");
        CHECK (g.disp ("status").find ("Loaded") == std::string::npos, "buttons are ignored during the first 2.5 s (project load guard)");
        g.D (effClose);
    }
    usleep (2600000);
    // browse to Drum Punch (browse knob = preset list position)
    int pos = -1, total = 0;
    for (int k = 0; k < 200 && pos < 0; ++k)
    {
        p.set ("browse", k / 199.f);
        if (p.disp ("browse") == "Drum Punch") pos = k;
    }
    for (int k = 0; k < 2000; ++k) { p.set ("browse", k / 1999.f); if (p.disp ("browse") != "") total = k; }
    if (pos >= 0) p.set ("browse", pos / 199.f);
    p.press ("load");
    CHECK (pos >= 0 && p.disp ("threshold") == "-18.0 dB" && p.disp ("status").find ("Loaded Drum Punch") != std::string::npos
           && p.disp ("preset").find ("Drum Punch") == 0, "PRESET knob + LOAD: %s / %s", p.disp ("status").c_str(), p.disp ("preset").c_str());
    (void) total;
    p.press ("next");
    const std::string after = p.disp ("preset");
    p.press ("prev");
    CHECK (after.find ("Drum Punch") == std::string::npos && p.disp ("preset").find ("Drum Punch") == 0, "NEXT -> %s, PREV -> back to Drum Punch", after.c_str());
    p.set ("mix", 0.5f);
    CHECK (p.disp ("preset").find ("Drum Punch *") == 0, "editing a knob marks the preset as modified: %s", p.disp ("preset").c_str());
    p.press ("fav");
    CHECK (p.disp ("browse").find ("\xe2\x98\x85 Drum Punch") == 0 && p.get ("browse") == 0.f, "FAV: %s moves to the top of the list", p.disp ("browse").c_str());
    p.press ("save");
    CHECK (p.disp ("status").find ("Saved User 01") == 0 && countFiles (presets, ".gglue") == 1 && p.disp ("preset").find ("User 01") == 0,
           "SAVE: %s", p.disp ("status").c_str());
    p.press ("save");
    CHECK (p.disp ("status").find ("Saved User 02") == 0 && countFiles (presets, ".gglue") == 2, "SAVE again: User 02");
    // DELETE needs two taps on the selected user preset
    p.press ("delete");
    CHECK (p.disp ("status").find ("Tap DELETE again") == 0 && countFiles (presets, ".gglue") == 2, "DELETE, first tap: %s", p.disp ("status").c_str());
    p.press ("delete");
    CHECK (p.disp ("status").find ("Deleted User 02") == 0 && countFiles (presets, ".gglue") == 1, "DELETE, second tap: %s", p.disp ("status").c_str());
    // factory presets can't be deleted
    p.set ("browse", 0.f); p.press ("delete");
    CHECK (p.disp ("status").find ("Only user presets") == 0, "factory presets can't be deleted");
    // A/B
    p.set ("threshold", 0.25f);                                 // A: -20 dB
    p.press ("copy");                                           // B = A
    p.press ("ab");
    p.set ("threshold", 0.75f);                                 // B: 0 dB
    const std::string bText = p.disp ("preset");
    p.press ("ab");
    CHECK (p.disp ("threshold") == "-20.0 dB" && bText.find ("[B]") != std::string::npos && p.disp ("preset").find ("[A]") != std::string::npos,
           "A/B: A -20.0 dB, B %s; COPY copies the active slot", "0.0 dB");
    p.press ("ab");
    CHECK (p.disp ("threshold") == "+0.0 dB", "A/B again: B is %s", p.disp ("threshold").c_str());
    p.press ("ab");

    std::printf ("Programs and project state\n");
    char pn[64] = {};
    p.D (effGetProgramNameIndexed, 46, 0, pn);
    CHECK (p.e->numPrograms >= 60 && std::string (pn).size() > 2, "%d factory presets as VST programs (program 46: %s)", p.e->numPrograms, pn);
    p.D (effSetProgram, 0, 46);
    CHECK (p.disp ("preset").find (pn) == 0, "effSetProgram loads it: %s", p.disp ("preset").c_str());
    p.set ("mix", 0.63f);
    void* data = nullptr;
    const intptr_t size = p.D (effGetChunk, 0, 0, &data);
    const std::string chunk ((const char*) data, (size_t) size);
    Inst q;
    CHECK (open (q, lib), "second instance");
    q.D (effSetChunk, 0, (intptr_t) chunk.size(), (void*) chunk.data());
    bool same = true; for (int i = 0; i < 11; ++i) same = same && std::fabs (q.e->getParameter (q.e, i) - p.e->getParameter (p.e, i)) < 1e-6f;
    CHECK (same && chunk.rfind ("G-Glue State 1", 0) == 0 && q.disp ("preset").find (pn) == 0,
           "project chunk (%d bytes, same format as the desktop plugin) restores all 11 parameters and \"%s\"", (int) size, q.disp ("preset").c_str());
    q.press ("ab");
    q.sine (0.05, 0.1);
    CHECK (q.disp ("status").find ("Now hearing") == std::string::npos, "buttons ignored for 2.5 s after a project load");
    q.D (effClose);

    std::printf ("Robustness\n");
    {
        std::vector<float> il (256), ir (256), ol (256), orr (256);
        bool finite = true;
        for (int k = 0; k < 200; ++k)
        {
            for (int i = 0; i < 256; ++i) { il[(size_t) i] = (k % 7 == 3 && i == 10) ? NAN : (float) (8.0 * std::sin (i * 0.3)); ir[(size_t) i] = (k % 11 == 5 && i == 3) ? INFINITY : 1e-38f; }
            float* in[2] = { il.data(), ir.data() }; float* o[2] = { ol.data(), orr.data() };
            p.e->processReplacing (p.e, in, o, 256);
            for (int i = 0; i < 256; ++i) finite = finite && std::isfinite (ol[(size_t) i]) && std::isfinite (orr[(size_t) i]) && std::fabs (ol[(size_t) i]) <= 1.f;
        }
        CHECK (finite, "NaN / inf / +18 dBFS / denormal input: output finite and within 0 dBFS");
    }
    {
        p.rate (48000);
        p.set ("analog", 1.f);
        std::vector<float> il (512), ir (512), ol (512), orr (512);
        for (int i = 0; i < 512; ++i) il[(size_t) i] = ir[(size_t) i] = (float) (0.7 * std::sin (i * 0.05));
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 48000 * 10 / 512; ++k) { float* in[2] = { il.data(), ir.data() }; float* o[2] = { ol.data(), orr.data() }; p.e->processReplacing (p.e, in, o, 512); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  CPU: %.3f s for 10 s of stereo 48 kHz audio (2x oversampling, analog on) = %.2f %% of one core of this machine\n", sec, sec * 10);
        if (! std::getenv ("GGLUE_EMULATED")) CHECK (sec < 1.0, "under 10 %% of one core");
    }
    p.D (effClose);
    dlclose (lib);
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
