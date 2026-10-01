// Offline VST2 host test for SP1200 2.0: loads the .so like MPC does and checks the plugin interface (parameters,
// presets, latency, bypass, stepped Q-Link moves, readouts) at several host rates and block sizes.
// The DSP itself is validated stage by stage in test/stage_test.cpp.
#include "vst2.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++fails; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
typedef AEffect* (*MainFn) (audioMasterCallback);
typedef const char* (*KeyFn) (int);
static KeyFn keyOf;
static int automates = 0;
static intptr_t host (AEffect*, int32_t op, int32_t, intptr_t, void*, float) { if (op == 0) ++automates; return op == 1 ? 2400 : 0; }
static AEffect* fx;
static float SR = 44100.f;
static intptr_t D (int op, int idx = 0, intptr_t val = 0, void* p = nullptr, float opt = 0) { return fx->dispatcher (fx, op, idx, val, p, opt); }
static int idx (const char* k) { for (int i = 0; i < fx->numParams; ++i) if (std::strcmp (keyOf (i), k) == 0) return i; std::printf ("no param %s\n", k); std::exit (2); }
static void setN (const char* k, float n) { fx->setParameter (fx, idx (k), n); }
static float getN (const char* k) { return fx->getParameter (fx, idx (k)); }
static std::string text (const char* k) { char b[256] = {}; D (effGetParamDisplay, idx (k), 0, b); return b; }
static void run (std::vector<float>& il, std::vector<float>& ir, std::vector<float>& ol, std::vector<float>& orr, int block)
{
    ol.assign (il.size(), 0.f); orr.assign (il.size(), 0.f);
    for (size_t p = 0; p < il.size(); p += block)
    {
        const int m = (int) std::min<size_t> (block, il.size() - p);
        float* in[2] { &il[p], &ir[p] }; float* out[2] { &ol[p], &orr[p] };
        fx->processReplacing (fx, in, out, m);
    }
}

int main (int argc, char** argv)
{
    void* h = dlopen (argc > 1 ? argv[1] : "build/native/sp1200.so", RTLD_NOW);
    if (h == nullptr) { std::printf ("dlopen: %s\n", dlerror()); return 1; }
    MainFn main_ = (MainFn) dlsym (h, "VSTPluginMain"); keyOf = (KeyFn) dlsym (h, "SP_ParamKey");
    if (main_ == nullptr || keyOf == nullptr) { std::printf ("missing exports\n"); return 1; }
    fx = main_ (host);
    D (effOpen); D (effSetSampleRate, 0, 0, nullptr, SR); D (effSetBlockSize, 0, 512); D (effMainsChanged, 0, 1);

    std::printf ("[interface]\n");
    CHECK (fx->magic == kEffectMagic && fx->uniqueID == 0x53503132, "same plugin id as 1.x ('SP12'): upgrades in place");
    CHECK (fx->numInputs == 2 && fx->numOutputs == 2, "stereo insert");
    CHECK (fx->initialDelay == 40, "latency reported: %d samples", fx->initialDelay);
    CHECK (fx->numPrograms >= 50, "%d factory presets", fx->numPrograms);
    char name[64] = {}; D (effGetProductString, 0, 0, name); CHECK (std::string (name) == "SP1200", "product name %s", name);
    CHECK (D (effGetVendorVersion) == 2000, "version 2.0.0");
    CHECK (text ("srate") == "26.04 kHz HW" && text ("bits") == "12 bit HW" && text ("aaf") == "15.00 kHz", "hardware defaults: %s, %s, AA %s", text ("srate").c_str(), text ("bits").c_str(), text ("aaf").c_str());

    std::printf ("[presets]\n");
    std::vector<float> il (SR), ir (SR), ol, orr;
    for (size_t i = 0; i < il.size(); ++i) { const double t = i / (double) SR; il[i] = ir[i] = (float) (0.6 * std::sin (2 * M_PI * 220 * t) * std::exp (-std::fmod (t, 0.25) * 10)); }
    bool allOk = true; std::string bad;
    for (int p = 0; p < fx->numPrograms; ++p)
    {
        D (effSetProgram, 0, p); run (il, ir, ol, orr, 512);
        float pk = 0; bool fin = true;
        for (size_t i = 0; i < ol.size(); ++i) { if (! std::isfinite (ol[i]) || ! std::isfinite (orr[i])) fin = false; pk = std::fmax (pk, std::fabs (ol[i])); }
        char pn[64] = {}; D (effGetProgramNameIndexed, p, 0, pn);
        if (! fin || pk > 8.f || pk < 1e-4f) { allOk = false; bad += pn; bad += " "; }
    }
    CHECK (allOk, "every preset gives finite, bounded, non-silent output %s", bad.c_str());

    std::printf ("[latency, bypass, mix]\n");
    D (effSetProgram, 0, 0);
    std::vector<float> imp (8192, 0.f); imp[1000] = 0.5f; std::vector<float> imp2 = imp;
    setN ("mix", 0.f); D (effMainsChanged, 0, 1); run (imp, imp2, ol, orr, 256);
    int at = -1; for (size_t i = 0; i < ol.size(); ++i) if (ol[i] != 0.f) { at = (int) i; break; }
    CHECK (at == 1040 && ol[1040] == 0.5f, "MIX 0: dry signal, exact, delayed by the latency (sample %d)", at);
    setN ("mix", 1.f); setN ("bypass", 1.f); D (effMainsChanged, 0, 1); run (imp, imp2, ol, orr, 256);
    at = -1; for (size_t i = 0; i < ol.size(); ++i) if (ol[i] != 0.f) { at = (int) i; break; }
    CHECK (at == 1040 && ol[1040] == 0.5f, "BYPASS: dry, exact, same latency (sample %d)", at);
    setN ("bypass", 0.f);

    std::printf ("[stepped controls: slow Q-Link turns]\n");
    {
        // read, add 1/127, write back: the plugin must keep the host's exact position so small moves add up
        setN ("range", 1.f);
        setN ("pitch", 0.f);
        int distinct = 0; std::string last;
        for (int k = 0; k < 127; ++k)
        {
            const float n = getN ("pitch"); setN ("pitch", n + 1.f / 127.f);
            const std::string t = text ("pitch"); if (t != last) { ++distinct; last = t; }
        }
        CHECK (distinct == 25, "pitch steps through all 25 semitones (-12..+12) with 1/127 moves (%d)", distinct);
        setN ("range", 0.f); setN ("pitch", 1.f);
        CHECK (text ("pitch") == "+12 (HW +7)", "hardware range shows the clamp: %s", text ("pitch").c_str());
        setN ("pitch", 0.5f);
        setN ("channel", 0.f); distinct = 0; last.clear();
        for (int k = 0; k < 127; ++k) { const float n = getN ("channel"); setN ("channel", n + 1.f / 127.f); const std::string t = text ("channel"); if (t != last) { ++distinct; last = t; } }
        CHECK (distinct == 4, "Output Channel steps through 4 outputs (%d)", distinct);
    }

    std::printf ("[readouts]\n");
    {
        D (effSetProgram, 0, 0);
        CHECK (getN ("analyzer") < 0.5f, "analyzer off by default (no screen page shows it)");
        setN ("analyzer", 1.f);
        automates = 0;
        std::vector<float> s (SR), s2;
        for (size_t i = 0; i < s.size(); ++i) s[i] = (float) (0.5 * std::sin (2 * M_PI * 1000 * i / SR));
        s2 = s; run (s, s2, ol, orr, 512);
        CHECK (automates > 20, "analyzer and scope readouts are pushed to the host (%d updates)", automates);
        CHECK (text ("info").find ("SAMPLE 26.04k") != std::string::npos, "signal path line: %s", text ("info").c_str());
        CHECK (text ("aliastxt").find ("ABOVE 13.0 kHz") != std::string::npos, "image/alias meter: %s", text ("aliastxt").c_str());
        // spectrum boxes: the 1 kHz band of both spectra should be high, scope should not be flat
        float sc = 0; for (int k = 0; k < 32; ++k) { char key[16]; std::snprintf (key, sizeof key, "scope%d", k); sc = std::fmax (sc, std::fabs (getN (key) - 60.f / 127.f)); }
        CHECK (sc > 0.05f, "scope shows the waveform");
        setN ("analyzer", 0.f); automates = 0; run (s, s2, ol, orr, 512);
        CHECK (automates < 5, "analyzer off: no readout traffic (%d)", automates);
        setN ("analyzer", 1.f);
    }

    std::printf ("[host rates and block sizes]\n");
    for (float rate : { 48000.f, 96000.f, 44100.f })
    {
        SR = rate; D (effSetSampleRate, 0, 0, nullptr, rate); D (effMainsChanged, 0, 1);
        std::vector<float> s ((size_t) rate), s2; for (size_t i = 0; i < s.size(); ++i) s[i] = (float) (0.5 * std::sin (2 * M_PI * 440 * i / rate)); s2 = s;
        bool ok = true;
        for (int block : { 1, 17, 64, 512, 2048 })
        {
            run (s, s2, ol, orr, block);
            double e = 0; for (size_t i = s.size() / 2; i < s.size(); ++i) e += ol[i] * ol[i];
            if (! (e > 1.0) || ! std::isfinite (e)) ok = false;
        }
        CHECK (ok, "host %.0f Hz: output at block sizes 1, 17, 64, 512, 2048", rate);
    }

    D (effClose);
    std::printf ("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
