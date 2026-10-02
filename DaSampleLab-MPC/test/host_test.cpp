// Offline host test for Da Sample Lab: loads the plugin .so like MPC does (VST2), feeds it generated audio files,
// MIDI and parameter changes, and checks what comes out.   ./host_test build/native/dasamplelab.so WORKDIR
#include "../src/vst2.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (c) { std::printf ("  ok   "); } else { ++fails; std::printf ("  FAIL "); } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static double tempo = 120;
static VstTimeInfo ti;
static intptr_t hostCb (AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == audioMasterGetTime) { std::memset (&ti, 0, sizeof ti); ti.sampleRate = 44100; ti.tempo = tempo; ti.flags = kVstTempoValid; return (intptr_t) &ti; }
    return 0;
}
static VstMidiEvent allOff (int at) { VstMidiEvent m {}; m.type = kVstMidiType; m.byteSize = sizeof m; m.deltaFrames = at; m.midiData[0] = (char) 0xB0; m.midiData[1] = 123; return m; }
typedef AEffect* (*MainFn) (audioMasterCallback);
static MainFn mainFn;
struct Inst
{
    AEffect* e;
    const char* (*keyFn) (int) = nullptr;
    int P (const char* key)
    {
        typedef const char* (*KeyFn) (int);
        static KeyFn sk = nullptr;
        if (! sk) sk = (KeyFn) dlsym (RTLD_DEFAULT, "SL_ParamKey");
        KeyFn k = keyFn ? keyFn : sk;
        for (int i = 0; i < e->numParams; ++i) if (! std::strcmp (k (i), key)) return i;
        std::printf ("no param %s\n", key); std::exit (2);
    }
    intptr_t D (int op, int idx = 0, intptr_t val = 0, void* ptr = nullptr, float opt = 0) { return e->dispatcher (e, op, idx, val, ptr, opt); }
    std::string disp (const char* key) { char b[256] = ""; D (effGetParamDisplay, P (key), 0, b); return b; }
    void set (const char* key, float v) { e->setParameter (e, P (key), v); }
    void press (const char* key) { const int i = P (key); e->setParameter (e, i, e->getParameter (e, i) > 0.5f ? 0.f : 1.f); }
    std::vector<float> L, R; bool quietFirst = true;
    void run (int frames, std::vector<VstMidiEvent> ev = {})
    {
        const int B = 512;
        for (int s = 0; s < frames; s += B)
        {
            const int n = std::min (B, frames - s);
            std::vector<VstMidiEvent> now;
            if (s == 0 && ! ev.empty() && quietFirst) { VstMidiEvent o = allOff (0); VstEvents es {}; es.numEvents = 1; es.events[0] = (VstEvent*) &o; D (effProcessEvents, 0, 0, &es); std::vector<float> a (4096), b (4096); float* oo[2] { a.data(), b.data() }; for (int k = 0; k < 12; ++k) e->processReplacing (e, nullptr, oo, 512); }
            for (auto& m : ev) if (m.deltaFrames >= s && m.deltaFrames < s + n) { VstMidiEvent x = m; x.deltaFrames -= s; now.push_back (x); }
            for (size_t k = 0; k < now.size(); ++k)
            {
                VstEvents es {}; es.numEvents = 1; es.events[0] = (VstEvent*) &now[k];
                D (effProcessEvents, 0, 0, &es);
            }
            std::vector<float> l (n), r (n); float* o[2] { l.data(), r.data() };
            e->processReplacing (e, nullptr, o, n);
            L.insert (L.end(), l.begin(), l.end()); R.insert (R.end(), r.begin(), r.end());
        }
    }
    bool waitStatus (const char* needle, double secs)
    {
        for (int i = 0; i < secs * 20; ++i) { run (512); if (disp ("status").find (needle) != std::string::npos) return true; usleep (50000); }
        return false;
    }
};
static VstMidiEvent note (int at, int n, int vel, bool on = true)
{
    VstMidiEvent m {}; m.type = kVstMidiType; m.byteSize = sizeof m; m.deltaFrames = at;
    m.midiData[0] = (char) (on ? 0x90 : 0x80); m.midiData[1] = (char) n; m.midiData[2] = (char) vel; return m;
}

// ---------------------------------------------------------------- test audio
static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate, int bits)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    const int bps = bits / 8; const uint32_t bytes = (uint32_t) (l.size() * 2 * bps);
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 (rate); w32 (rate * 2 * bps); w16 (2 * bps); w16 (bits);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    for (size_t i = 0; i < l.size(); ++i)
        for (float x : { l[i], r[i] })
        {
            const int32_t v = (int32_t) std::lround (std::max (-1.f, std::min (1.f, x)) * (bits == 24 ? 8388607.0 : 32767.0));
            std::fwrite (&v, 1, (size_t) bps, f);
        }
    std::fclose (f);
}
// 8 bars at 96 BPM, 48 kHz: kick on 1 and 3, snare on 2 and 4, closed hats on 8ths, Am - Dm - E - Am pads
static std::vector<double> makeLoop (const std::string& path)
{
    const int sr = 48000; const double bpm = 96, beat = 60.0 / bpm; const int bars = 8;
    const long n = (long) (bars * 4 * beat * sr);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    unsigned seed = 1; auto noise = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 9) / 4194304.0f) - 1.f; };
    const double chords[4][3] { { 220.0, 261.63, 329.63 }, { 293.66, 349.23, 440.0 }, { 329.63, 415.30, 493.88 }, { 220.0, 261.63, 329.63 } };
    std::vector<double> hits;
    for (long i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const int b = (int) (t / beat); const double tb = t - b * beat;
        const int eighth = (int) (t / (beat / 2)); const double te = t - eighth * beat / 2;
        double x = 0;
        if (b % 2 == 0) x += 0.45 * std::exp (-tb * 25) * std::sin (2 * M_PI * (50 + 80 * std::exp (-tb * 40)) * tb);
        else x += 0.3 * std::exp (-tb * 20) * noise();
        x += 0.12 * std::exp (-te * 80) * noise();
        const int bar = b / 4; const double* c = chords[bar % 4];
        for (int k = 0; k < 3; ++k) x += 0.06 * std::sin (2 * M_PI * c[k] * t) + 0.02 * std::sin (2 * M_PI * c[k] * 2 * t);
        l[(size_t) i] = (float) x; r[(size_t) i] = (float) (x * 0.95);
    }
    for (int e = 0; e < bars * 8; ++e) hits.push_back (e * beat / 2);
    writeWav (path, l, r, sr, 24);
    return hits;
}
// 4 bars of G - C - D - G pads (G major), 44.1 kHz 16-bit
static void makeMajor (const std::string& path)
{
    const int sr = 44100; const double bar = 2.0; const long n = (long) (4 * bar * sr);
    const double ch[4][3] { { 196.0, 246.94, 293.66 }, { 261.63, 329.63, 392.0 }, { 293.66, 369.99, 440.0 }, { 196.0, 246.94, 293.66 } };
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (long i = 0; i < n; ++i)
    {
        const double t = (double) i / sr; const double* c = ch[(int) (t / bar) % 4]; double x = 0;
        for (int k = 0; k < 3; ++k) x += 0.12 * std::sin (2 * M_PI * c[k] * t) + 0.04 * std::sin (4 * M_PI * c[k] * t);
        x += 0.15 * std::sin (2 * M_PI * c[0] / 2 * t);
        l[(size_t) i] = r[(size_t) i] = (float) x;
    }
    writeWav (path, l, r, sr, 16);
}
static void makeSine (const std::string& path)
{
    const int sr = 44100; const long n = 2 * sr;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (long i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) (0.5 * std::sin (2 * M_PI * 440.0 * i / sr));
    writeWav (path, l, r, sr, 16);
}
// frequency by counting rising zero crossings in [a, b)
static double freq (const std::vector<float>& x, size_t a, size_t b, double sr)
{
    int c = 0; size_t first = 0, last = 0;
    for (size_t i = a + 1; i < b && i < x.size(); ++i) if (x[i - 1] <= 0 && x[i] > 0) { if (! c) first = i; last = i; ++c; }
    return c > 1 ? (c - 1) * sr / (double) (last - first) : 0;
}
static std::vector<std::string> sliceLines (Inst& p)
{
    void* data = nullptr; const intptr_t n = p.D (effGetChunk, 0, 0, &data);
    std::string s ((const char*) data, (size_t) n);
    std::vector<std::string> out; size_t pos = 0;
    while (pos < s.size()) { size_t e = s.find ('\n', pos); if (e == std::string::npos) e = s.size(); if (! s.compare (pos, 2, "s ")) out.push_back (s.substr (pos, e - pos)); pos = e + 1; }
    return out;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: host_test plugin.so workdir [capture.so]\n"); return 2; }
    const std::string work = argv[2];
    const char* capPath = argc > 3 ? argv[3] : nullptr;
    mkdir (work.c_str(), 0755);
    setenv ("SAMPLELAB_ROOT", work.c_str(), 1);
    mkdir ((work + "/Samples").c_str(), 0755);
    const std::vector<double> hits = makeLoop (work + "/Samples/loop96.wav");
    makeSine (work + "/Samples/sine440.wav");
    makeMajor (work + "/Samples/zz_gmajor.wav");
    // a fake MPC internal drive with an older project sample and a new recording (for BROWSE: Recent)
    const std::string internal = work + "-internal";
    mkdir (internal.c_str(), 0755); mkdir ((internal + "/Project A").c_str(), 0755);
    setenv ("SAMPLELAB_INTERNAL", internal.c_str(), 1);
    makeSine (internal + "/Project A/old take.wav");
    sleep (1);
    makeSine (internal + "/Project A/My Recording.wav");
    void* h = dlopen (argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (! h) { std::printf ("dlopen: %s\n", dlerror()); return 2; }
    mainFn = (MainFn) dlsym (h, "VSTPluginMain");
    const double SR = 44100;

    std::printf ("Plugin interface\n");
    Inst p { mainFn (hostCb) };
    CHECK (p.e && p.e->magic == kEffectMagic, "VSTPluginMain returns an effect");
    CHECK ((p.e->flags & effFlagsIsSynth) && p.e->numOutputs == 2 && p.e->numInputs == 0, "instrument, 0 in / 2 out");
    char name[64] = ""; p.D (effGetEffectName, 0, 0, name);
    CHECK (! std::strcmp (name, "Da Sample Lab"), "name %s", name);
    p.D (effSetSampleRate, 0, 0, nullptr, (float) SR); p.D (effMainsChanged, 0, 1);
    for (int i = 0; i < 40 && p.disp ("file").find ("loop96") == std::string::npos; ++i) { p.run (512); usleep (50000); }
    CHECK (p.disp ("file").find ("loop96.wav") != std::string::npos, "file picker shows %s", p.disp ("file").c_str());
    p.press ("load"); p.run (2048);
    CHECK (p.disp ("status").find ("Loading") == std::string::npos, "button presses are ignored during the first 2.5 s (project load guard)");
    usleep (2600000);

    std::printf ("Loading + analysis (96 BPM, A minor, 24-bit 48 kHz loop)\n");
    p.set ("file", 0.f); p.press ("load");
    CHECK (p.waitStatus ("Loaded", 60), "loads: %s", p.disp ("status").c_str());
    const double bpm = std::strtod (p.disp ("bpm").c_str(), nullptr);
    CHECK (std::fabs (bpm - 96) < 1.0, "BPM %.1f (96)", bpm);
    CHECK (p.disp ("analysis").find ("A minor") != std::string::npos, "key: %s", p.disp ("analysis").c_str());

    std::printf ("Chopping\n");
    auto sl = sliceLines (p);
    CHECK (sl.size() == 16, "transients: %zu slices (16)", sl.size());
    int onGrid = 0;
    for (auto& s : sl) { const double t = std::strtod (s.c_str() + 2, nullptr) / SR; for (double hgt : hits) if (std::fabs (t - hgt) < 0.025) { ++onGrid; break; } }
    if (getenv ("VERBOSE")) for (auto& s : sl) std::printf ("    slice at %.4f s\n", std::strtod (s.c_str() + 2, nullptr) / SR);
    CHECK (onGrid == (int) sl.size(), "%d of %zu transient slices start within 25 ms of a hit", onGrid, sl.size());
    p.set ("chopmode", 2 / 4.f); p.set ("count", 3 / 7.f); p.press ("chop"); p.waitStatus ("slices (Bars)", 10);
    sl = sliceLines (p);
    if (getenv ("VERBOSE")) for (auto& s : sl) std::printf ("    bar slice at %.4f s\n", std::strtod (s.c_str() + 2, nullptr) / SR);
    CHECK (sl.size() == 8, "bars: %zu slices (8 bars)", sl.size());
    bool barsOk = true;
    for (size_t k = 0; k < sl.size(); ++k) if (std::fabs (std::strtod (sl[k].c_str() + 2, nullptr) / SR - k * 4 * 0.625) > 0.03) barsOk = false;
    CHECK (barsOk, "bar slices start on the bar lines (2.5 s apart)");
    p.set ("chopmode", 4 / 4.f); p.set ("count", 7 / 7.f); p.press ("chop"); p.waitStatus ("slices (Equal)", 10);
    CHECK (sliceLines (p).size() == 64, "equal: 64 slices (4 pad banks)");
    p.set ("chopmode", 3 / 4.f); p.set ("count", 1 / 7.f); p.press ("chop"); p.waitStatus ("slices (Sections)", 10);
    sl = sliceLines (p);
    CHECK (sl.size() >= 2 && sl.size() <= 8, "sections: %zu slices, on bar lines", sl.size());
    p.set ("chopmode", 0.f); p.set ("count", 3 / 7.f); p.press ("chop"); p.waitStatus ("slices (Transients)", 10);

    std::printf ("Pads\n");
    p.run (8192);
    p.L.clear(); p.R.clear();
    p.run (22050, { note (100, 36, 127) });
    size_t firstSound = 0; while (firstSound < p.L.size() && std::fabs (p.L[firstSound]) < 1e-4f) ++firstSound;
    CHECK (firstSound >= 100 && firstSound <= 104, "pad A1 (note 36) sounds at sample %zu (MIDI at 100)", firstSound);
    double e1 = 0; for (float x : p.L) e1 += x * x;
    p.L.clear(); p.R.clear(); p.run (22050, { note (0, 36, 40) });
    double e2 = 0; for (float x : p.L) e2 += x * x;
    CHECK (e2 < e1 * 0.6, "velocity: soft hit is quieter (%.0f%% of the energy)", 100 * e2 / e1);
    p.L.clear(); p.R.clear(); p.run (22050, { note (0, 36 + 40, 127) });
    double e3 = 0; for (float x : p.L) e3 += x * x;
    CHECK (e3 == 0, "a pad with no slice (note 76, slice 41 of 16) stays silent");

    std::printf ("Slice editing\n");
    p.set ("slice", 3 / 63.f); p.run (2048);
    const std::string before = sliceLines (p)[3];
    p.press ("slright"); p.run (2048);
    CHECK (sliceLines (p)[4] == before, "move right: slice 4 is now on pad 5");
    p.press ("slleft"); p.run (2048);
    CHECK (sliceLines (p)[3] == before, "move left: back on pad 4");
    const size_t n0 = sliceLines (p).size();
    p.press ("split"); p.run (2048);
    CHECK (sliceLines (p).size() == n0 + 1, "split: %zu -> %zu slices", n0, sliceLines (p).size());
    p.press ("merge"); p.run (2048);
    CHECK (sliceLines (p).size() == n0 && sliceLines (p)[3] == before, "merge: back to the original slice");
    p.set ("slice", 0.f); p.run (1024);
    p.L.clear(); p.R.clear(); p.run (8000, { note (0, 36, 127) });
    std::vector<float> fwd = p.L;
    p.set ("slrev", 1.f); p.run (1024);
    CHECK (sliceLines (p)[0].find (" 1 0 ") != std::string::npos, "reverse stored on slice 1: %s", sliceLines (p)[0].c_str());
    p.L.clear(); p.R.clear(); p.run (8000, { note (0, 36, 127) });
    double headF = 0, headR = 0; for (int i = 0; i < 2000; ++i) { headF += fwd[(size_t) i] * fwd[(size_t) i]; headR += p.L[(size_t) i] * p.L[(size_t) i]; }
    CHECK (headR < headF * 0.5, "reverse: the kick's attack is no longer at the start");
    p.set ("slrev", 0.f);
    p.set ("slvol", (-12 + 24) / 30.f); p.run (1024);
    p.L.clear(); p.R.clear(); p.run (8000, { note (0, 36, 127) });
    double eq = 0, el = 0; for (int i = 0; i < 8000; ++i) { eq += p.L[(size_t) i] * p.L[(size_t) i]; el += fwd[(size_t) i] * fwd[(size_t) i]; }
    CHECK (std::fabs (10 * std::log10 (eq / el) + 12) < 1.0, "slice level -12 dB: measured %.1f dB", 10 * std::log10 (eq / el));
    p.set ("slvol", 24 / 30.f);

    std::printf ("Export (original 24-bit / 48 kHz)\n");
    p.set ("slice", 2 / 63.f); p.run (1024);
    p.press ("export");
    CHECK (p.waitStatus ("Exported", 20), "%s", p.disp ("status").c_str());
    const std::string ex = work + "/Exports/loop96/loop96_03.wav";
    FILE* f = std::fopen (ex.c_str(), "rb");
    CHECK (f != nullptr, "file %s", ex.c_str());
    if (f)
    {
        unsigned char hd[44]; size_t got = std::fread (hd, 1, 44, f); (void) got;
        const int rate = hd[24] | (hd[25] << 8) | (hd[26] << 16), bits = hd[34];
        const uint32_t bytes = hd[40] | (hd[41] << 8) | (hd[42] << 16) | ((uint32_t) hd[43] << 24);
        CHECK (rate == 48000 && bits == 24, "%d Hz, %d-bit", rate, bits);
        const auto s3 = sliceLines (p)[2];
        char* q = (char*) s3.c_str() + 2; const long a = (long) std::strtod (q, &q), b = (long) std::strtod (q, &q);
        const long want = (long) std::ceil ((b - a) * 48000.0 / SR);
        CHECK (std::labs ((long) (bytes / 6) - want) <= 1, "%u frames (slice = %ld)", bytes / 6, want);
        // bit-exact against the source file region
        FILE* src = std::fopen ((work + "/Samples/loop96.wav").c_str(), "rb");
        const long from = (long) std::floor (a * 48000.0 / SR);
        std::fseek (src, 44 + from * 6, SEEK_SET);
        std::vector<unsigned char> x (bytes), y (bytes);
        got = std::fread (x.data(), 1, bytes, src); got = std::fread (y.data(), 1, bytes, f);
        CHECK (x == y, "exported audio is bit-identical to the original file");
        std::fclose (src); std::fclose (f);
    }
    p.press ("exportall"); p.waitStatus ("Exported 16", 30);
    CHECK (p.disp ("status").find ("Exported 16 slices (24-bit 48000 Hz)") != std::string::npos, "export all: %s", p.disp ("status").c_str());

    std::printf ("Waveform view\n");
    {
        std::vector<float> whole, zoomed;
        for (int b = 0; b < 128; ++b) whole.push_back (p.e->getParameter (p.e, p.P (("wave" + std::to_string (b)).c_str())));
        p.set ("slice", 5 / 63.f); p.set ("zoom", 1.f); p.run (512); usleep (300000); p.run (512);
        for (int b = 0; b < 128; ++b) zoomed.push_back (p.e->getParameter (p.e, p.P (("wave" + std::to_string (b)).c_str())));
        int sel = 0; for (float v : zoomed) if ((int) std::lround (v * 127) / 32 >= 2) ++sel;
        CHECK (whole != zoomed && sel >= 120, "zoom to the selected slice: %d of 128 bars show the slice", sel);
        p.set ("zoom", 0.f); p.set ("slice", 0.f); p.run (512);
    }
    std::printf ("BPM half / double, key shift\n");
    p.press ("bpmdouble"); p.run (512);
    CHECK (std::fabs (std::strtod (p.disp ("bpm").c_str(), nullptr) - 192) < 0.1, "BPM x 2: %s", p.disp ("bpm").c_str());
    p.press ("bpmhalf"); p.run (512);
    CHECK (std::fabs (std::strtod (p.disp ("bpm").c_str(), nullptr) - 96) < 0.1, "BPM / 2: %s", p.disp ("bpm").c_str());
    p.set ("targetkey", 13 / 24.f); p.run (512); usleep (300000); p.run (512);
    CHECK (p.disp ("analysis").find ("+3") != std::string::npos, "A minor shifted to C minor: %s", p.disp ("analysis").c_str());
    p.set ("targetkey", 1 / 24.f); p.run (512); usleep (300000); p.run (512);
    CHECK (p.disp ("analysis").find ("+0") != std::string::npos, "A minor to C major (its relative): %s", p.disp ("analysis").c_str());
    p.set ("targetkey", 0.f); p.run (512);
    std::printf ("FIND variations\n");
    {
        const auto orig = sliceLines (p);
        p.press ("find"); p.waitStatus ("variation 1", 10);
        const auto v1 = sliceLines (p);
        p.press ("find"); p.waitStatus ("variation 2", 10);
        const auto v2 = sliceLines (p);
        CHECK (v1 != orig && v2 != v1 && v1.size() == 16 && v2.size() == 16, "FIND gives other 16-slice chops (%zu, %zu)", v1.size(), v2.size());
        p.press ("chop"); p.waitStatus ("slices (Transients) on pads", 10);
        CHECK (sliceLines (p) == orig, "CHOP returns to the original chop");
    }

    std::printf ("Project save / restore\n");
    void* data = nullptr; const intptr_t clen = p.D (effGetChunk, 0, 0, &data);
    const std::string chunk ((const char*) data, (size_t) clen);
    Inst q { mainFn (hostCb) };
    q.D (effSetSampleRate, 0, 0, nullptr, (float) SR); q.D (effMainsChanged, 0, 1);
    q.D (effSetChunk, 0, (intptr_t) chunk.size(), (void*) chunk.data());
    CHECK (q.waitStatus ("Loaded", 30), "restored instance loads the sample: %s", q.disp ("status").c_str());
    void* d2 = nullptr; const intptr_t l2 = q.D (effGetChunk, 0, 0, &d2);
    CHECK (std::string ((const char*) d2, (size_t) l2) == chunk, "state after restore is identical (%zu bytes)", chunk.size());

    std::printf ("Key of a major-key sample\n");
    p.set ("file", 1.f); p.press ("load"); p.waitStatus ("Loaded zz_gmajor", 30);
    CHECK (p.disp ("analysis").find ("G major") != std::string::npos, "G - C - D - G: %s", p.disp ("analysis").c_str());

    std::printf ("Chromatic + time\n");
    p.set ("file", 0.5f); p.press ("load"); p.waitStatus ("Loaded sine440", 30);
    p.set ("chopmode", 1.f); p.set ("count", 0.f); p.press ("chop"); p.waitStatus ("slices (Equal)", 10);
    p.set ("padmode", 1.f); p.set ("slice", 0.f); p.run (2048);
    p.L.clear(); p.R.clear(); p.run (8192, { note (0, 60, 127) });
    CHECK (std::fabs (freq (p.L, 1000, 8000, SR) - 440) < 2, "chromatic C3: %.1f Hz (440)", freq (p.L, 1000, 8000, SR));
    p.L.clear(); p.R.clear(); p.run (8192, { note (0, 72, 127) });
    CHECK (std::fabs (freq (p.L, 1000, 8000, SR) - 880) < 4, "chromatic C4: %.1f Hz (880)", freq (p.L, 1000, 8000, SR));
    p.L.clear(); p.R.clear(); p.run (8192, { note (0, 67, 127) });
    CHECK (std::fabs (freq (p.L, 1000, 8000, SR) - 659.26) < 4, "chromatic G3: %.1f Hz (659.3)", freq (p.L, 1000, 8000, SR));
    // slice 1 is 0.5 s long: Repitch +12 halves the time, Stretch at speed 0.5 doubles it at the same pitch
    auto soundLen = [&] { size_t last = 0; for (size_t i = 0; i < p.L.size(); ++i) if (std::fabs (p.L[i]) > 1e-3f) last = i; return last / SR; };
    p.L.clear(); p.R.clear(); p.run ((int) (2 * SR), { note (0, 60, 127) });
    const double t1 = soundLen();
    p.set ("tmode", 1.f); p.set ("speed", (0.5f - 0.5f) / 1.5f); p.run (512);
    p.L.clear(); p.R.clear(); p.run ((int) (2 * SR), { note (0, 60, 127) });
    const double t2 = soundLen(); const double f2 = freq (p.L, 2000, 30000, SR);
    CHECK (std::fabs (t1 - 0.5) < 0.02 && std::fabs (t2 - 1.0) < 0.06, "stretch: %.3f s -> %.3f s at speed 0.5", t1, t2);
    CHECK (std::fabs (f2 - 440) < 3, "stretch keeps the pitch: %.1f Hz", f2);
    p.set ("speed", 1 / 3.f); p.set ("sync", 1.f); tempo = 120; p.run (512);       // sine BPM is unknown: set 60 -> ratio 2
    p.set ("bpm", (60 - 50) / 150.f); p.run (512);
    p.L.clear(); p.R.clear(); p.run ((int) (2 * SR), { note (0, 60, 127) });
    CHECK (std::fabs (soundLen() - 0.25) < 0.03, "sync to MPC tempo 120 with sample BPM 60: %.3f s (0.25)", soundLen());

    std::printf ("CPU (10 s of audio)\n");
    {
        p.set ("file", 0.f); p.press ("load"); p.waitStatus ("Loaded", 30);
        p.set ("padmode", 0.f); p.set ("sync", 0.f); p.set ("playmode", 0.f); p.set ("chopmode", 0.f); p.set ("count", 3 / 7.f);
        p.press ("chop"); p.waitStatus ("slices (Transients)", 10);
        for (int mode = 0; mode < 2; ++mode)
        {
            p.set ("tmode", (float) mode); p.set ("speed", (0.8f - 0.5f) / 1.5f);
            for (auto& x : p.L) (void) x;
            std::vector<VstMidiEvent> ev; for (int k = 0; k < 16; ++k) { ev.push_back (note (k * 5, 36 + k, 100)); }
            std::vector<float> a (512), b (512); float* o[2] { a.data(), b.data() };
            for (auto& m : ev) { VstEvents es {}; es.numEvents = 1; es.events[0] = (VstEvent*) &m; m.deltaFrames = 0; p.D (effProcessEvents, 0, 0, &es); }
            p.set ("slloop", 1.f);
            const double t0 = (double) clock() / CLOCKS_PER_SEC;
            for (int i = 0; i < (int) (10 * SR / 512); ++i)
            {
                if (i % 40 == 0) for (int k = 0; k < 16; ++k) { VstMidiEvent m = note (0, 36 + k, 100); VstEvents es {}; es.numEvents = 1; es.events[0] = (VstEvent*) &m; p.D (effProcessEvents, 0, 0, &es); }
                p.e->processReplacing (p.e, nullptr, o, 512);
            }
            const double t = (double) clock() / CLOCKS_PER_SEC - t0;
            std::printf ("  16 voices, %s: %.2f s CPU for 10 s audio = %.1f %% of one core (this machine)\n", mode ? "Stretch" : "Repitch", t, t * 10);
        }
    }
    std::printf ("Mute, choke, filter, slice envelope (sine)\n");
    {
        p.set ("tmode", 0.f); p.set ("sync", 0.f); p.set ("padmode", 0.f); p.set ("playmode", 0.f); p.set ("poly", 0.f);
        p.set ("slloop", 0.f); p.set ("speed", 1 / 3.f); p.set ("pitch", 0.5f); p.set ("fine", 0.5f); p.run (512);
        p.set ("file", 0.5f); p.press ("load"); p.waitStatus ("Loaded sine440", 30);
        p.set ("chopmode", 1.f); p.set ("count", 0.f); p.press ("chop"); p.waitStatus ("slices (Equal)", 10);
        auto peak = [&] (size_t a, size_t b) { float m = 0; for (size_t i = a; i < b && i < p.L.size(); ++i) m = std::max (m, std::fabs (p.L[i])); return m; };
        auto energy = [&] (size_t a, size_t b) { double e = 0; for (size_t i = a; i < b && i < p.L.size(); ++i) e += p.L[i] * p.L[i]; return e; };
        p.set ("slice", 0.f); p.run (512);
        p.L.clear(); p.R.clear(); p.run (8192, { note (0, 36, 127) });
        const double e0 = energy (0, 8192), att0 = energy (0, 441);
        p.set ("slmute", 1.f); p.run (512);
        p.L.clear(); p.R.clear(); p.run (8192, { note (0, 36, 127) });
        CHECK (energy (0, 8192) == 0, "muted slice is silent");
        p.set ("slmute", 0.f);
        p.set ("slattack", 101 / 201.f); p.run (512);
        p.L.clear(); p.R.clear(); p.run (8192, { note (0, 36, 127) });
        CHECK (energy (0, 441) < att0 * 0.05 && p.disp ("slattack") == "100 ms", "slice attack %s: first 10 ms at %.1f %%", p.disp ("slattack").c_str(), 100 * energy (0, 441) / att0);
        p.set ("slattack", 0.f);
        p.set ("slfilter", 0.f); p.run (512);       // -1 = low-pass 200 Hz
        p.L.clear(); p.R.clear(); p.run (8192, { note (0, 36, 127) });
        CHECK (energy (2000, 8192) < e0 * 0.25 * (6192.0 / 8192), "low-pass %s cuts the 440 Hz sine by %.1f dB", p.disp ("slfilter").c_str(), 10 * std::log10 (energy (2000, 8192) / (e0 * 6192 / 8192)));
        p.set ("slfilter", 0.5f); p.run (512);
        p.L.clear(); p.R.clear(); p.run (8192, { note (0, 36, 127) });
        CHECK (std::fabs (10 * std::log10 (energy (2000, 8192) / (e0 * 6192 / 8192))) < 0.5, "filter Off leaves it alone (%s)", p.disp ("slfilter").c_str());
        // choke: slices 1 and 2 (in phase) - without a group they add up, in the same group the second cuts the first
        p.L.clear(); p.R.clear(); p.run (22050, { note (0, 36, 127), note (4410, 37, 127) });
        const float both = peak (6000, 12000);
        p.set ("slchoke", 1 / 4.f); p.set ("slice", 1 / 63.f); p.run (512); p.set ("slchoke", 1 / 4.f); p.run (512);
        p.L.clear(); p.R.clear(); p.run (22050, { note (0, 36, 127), note (4410, 37, 127) });
        const float choked = peak (6000, 12000);
        CHECK (both > 0.8f && choked < 0.6f, "choke group: %.2f -> %.2f", both, choked);
    }
    std::printf ("Recent recordings (MPC's own samples) + capture plugin\n");
    {
        p.set ("source", 1 / 3.f); p.run (512);
        for (int i = 0; i < 40 && p.disp ("file").find ("My Recording") == std::string::npos; ++i) { p.run (512); usleep (50000); }
        CHECK (p.disp ("file").find ("My Recording.wav") != std::string::npos, "Recent: the newest MPC recording comes first (%s)", p.disp ("file").c_str());
        p.set ("file", 0.f); p.press ("load");
        CHECK (p.waitStatus ("Loaded My Recording", 30), "loads it: %s", p.disp ("status").c_str());
        struct stat st;
        CHECK (stat ((work + "/Samples/My Recording.wav").c_str(), &st) != 0, "an internal-drive file is used in place (no copy)");
        p.set ("source", 0.f); p.run (512);
    }
    if (capPath)
    {
        void* hc = dlopen (capPath, RTLD_NOW | RTLD_LOCAL);
        CHECK (hc != nullptr, "capture plugin loads");
        MainFn cmain = (MainFn) dlsym (hc, "VSTPluginMain");
        Inst c { cmain (hostCb) }; c.keyFn = (const char* (*) (int)) dlsym (hc, "CA_ParamKey");
        CHECK (c.e->numInputs == 2 && c.e->numOutputs == 2 && ! (c.e->flags & effFlagsIsSynth), "insert effect, 2 in / 2 out");
        c.D (effSetSampleRate, 0, 0, nullptr, (float) SR); c.D (effMainsChanged, 0, 1);
        usleep (2600000);
        // feed a sine; output must equal input
        std::vector<float> inL, inR; bool exact = true;
        auto feed = [&] (int frames, float amp, double f0) {
            for (int s = 0; s < frames; s += 512)
            {
                std::vector<float> l (512), r (512), ol (512), orr (512);
                for (int i = 0; i < 512; ++i) { const size_t k = inL.size() + (size_t) i; l[(size_t) i] = (float) (amp * std::sin (2 * M_PI * f0 * (double) k / SR)); r[(size_t) i] = -l[(size_t) i]; }
                float* in[2] { l.data(), r.data() }; float* out[2] { ol.data(), orr.data() };
                c.e->processReplacing (c.e, in, out, 512);
                if (ol != l || orr != r) exact = false;
                inL.insert (inL.end(), l.begin(), l.end()); inR.insert (inR.end(), r.begin(), r.end());
            } };
        feed (4096, 0.25f, 440);
        c.press ("record");
        const size_t from = inL.size();
        feed (44032, 0.25f, 440);
        c.press ("record");
        CHECK (exact, "audio passes through unchanged");
        bool saved = false;
        for (int i = 0; i < 100 && ! saved; ++i) { feed (512, 0.f, 0); usleep (20000); saved = c.disp ("status").find ("Saved") != std::string::npos; }
        CHECK (saved, "%s", c.disp ("status").c_str());
        char latest[512] = ""; FILE* lf = std::fopen ((work + "/Samples/Captures/latest.txt").c_str(), "r");
        if (lf) { if (std::fgets (latest, sizeof latest, lf)) latest[std::strcspn (latest, "\n")] = 0; std::fclose (lf); }
        FILE* f = std::fopen (latest, "rb");
        CHECK (f != nullptr, "latest.txt -> %s", latest);
        if (f)
        {
            unsigned char hd[44]; size_t got = std::fread (hd, 1, 44, f); (void) got;
            const uint32_t bytes = hd[40] | (hd[41] << 8) | (hd[42] << 16) | ((uint32_t) hd[43] << 24);
            CHECK (hd[34] == 24 && (hd[24] | (hd[25] << 8) | (hd[26] << 16)) == 44100, "24-bit, 44100 Hz");
            CHECK (bytes / 6 == 44032, "%u frames recorded (44032 fed)", bytes / 6);
            std::vector<unsigned char> d (bytes); got = std::fread (d.data(), 1, bytes, f); std::fclose (f);
            double maxErr = 0;
            for (size_t i = 0; i < bytes / 6; ++i)
            {
                const int32_t v = (int32_t) ((uint32_t) d[6 * i] << 8 | (uint32_t) d[6 * i + 1] << 16 | (uint32_t) d[6 * i + 2] << 24) >> 8;
                maxErr = std::max (maxErr, std::fabs (v / 8388608.0 - inL[from + i]));
            }
            CHECK (maxErr < 1.0 / 8388608.0, "recording matches the input (max error %.2g)", maxErr);
        }
        // armed: starts at the signal, with a little pre-roll
        c.set ("arm", 1.f); c.set ("threshold", (-30 + 60) / 54.f);
        c.press ("record");
        feed (13230, 0.f, 0);                                       // 0.3 s of silence
        feed (22016, 0.25f, 440);                                   // then the sound
        c.press ("record");
        saved = false;
        for (int i = 0; i < 100 && ! saved; ++i) { feed (512, 0.f, 0); usleep (20000); saved = c.disp ("status").find ("Saved") != std::string::npos; }
        lf = std::fopen ((work + "/Samples/Captures/latest.txt").c_str(), "r");
        if (lf) { if (std::fgets (latest, sizeof latest, lf)) latest[std::strcspn (latest, "\n")] = 0; std::fclose (lf); }
        f = std::fopen (latest, "rb");
        if (f)
        {
            unsigned char hd[44]; size_t got = std::fread (hd, 1, 44, f); (void) got; std::fclose (f);
            const uint32_t frames = (hd[40] | (hd[41] << 8) | (hd[42] << 16) | ((uint32_t) hd[43] << 24)) / 6;
            CHECK (frames >= 22016 - 512 && frames <= 22016 + 2400, "armed take: %u frames (sound 22016 + pre-roll <= 50 ms)", frames);
        }
        c.D (effClose);
        // the sampler's LATEST key loads it
        p.press ("latest");
        CHECK (p.waitStatus ("Loaded Capture", 30), "Da Sample Lab LATEST: %s", p.disp ("status").c_str());
    }

    std::printf ("Stability\n");
    bool finite = true; for (float x : p.L) if (! std::isfinite (x) || std::fabs (x) > 1.f) finite = false;
    CHECK (finite, "output finite and within +-1");
    p.D (effClose); q.D (effClose);
    CHECK (true, "both instances closed");
    std::printf ("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
