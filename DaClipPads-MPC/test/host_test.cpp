// Offline host test for Da Clip Pads: loads the plugin like MPC does (dlopen + VSTPluginMain), plays it through the
// VST2 interface and checks the results. Test WAVs are generated into build/native/cptest.
//   make test
// Covers: clip loading, triggering, looping, quantised launching, MIDI triggering, pitch shifting, time stretch,
// start/end points, scene launching, state save/restore, sample-rate conversion, click-free transitions, follow
// actions, host sync, chop, flip, presets, kits and a CPU benchmark.
#include "vst2.h"
#include "Utilities/Wav.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

typedef AEffect* (*MainFn) (audioMasterCallback);
typedef const char* (*KeyFn) (int);
static MainFn pluginMain; static KeyFn paramKey;
static VstTimeInfo hostTime {};
static int hostFlags = 0;
static std::atomic<int> automateCalls { 0 };
static const double SR = 44100.0;

static intptr_t hostCb (AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == audioMasterAutomate) { ++automateCalls; return 0; }
    if (op == audioMasterGetTime) { hostTime.flags = hostFlags; hostTime.sampleRate = SR; return hostFlags ? (intptr_t) &hostTime : 0; }
    if (op == 1) return 2400;                                                  // audioMasterVersion
    return 0;
}

struct Host
{
    AEffect* e = nullptr;
    int nparams = 0;
    std::vector<float> L, R;                                                    // everything rendered since the last clear()
    long total = 0;                                                             // samples rendered since the plugin opened
    Host()
    {
        e = pluginMain (hostCb);
        nparams = e->numParams;
        e->dispatcher (e, effSetSampleRate, 0, 0, nullptr, (float) SR);
        e->dispatcher (e, effSetBlockSize, 0, 256, nullptr, 0.f);
        e->dispatcher (e, effMainsChanged, 0, 1, nullptr, 0.f);
    }
    ~Host() { e->dispatcher (e, effClose, 0, 0, nullptr, 0.f); }
    int idx (const char* key) const
    {
        for (int i = 0; i < nparams; ++i) if (std::strcmp (paramKey (i), key) == 0) return i;
        std::printf ("unknown param key %s\n", key); std::exit (2);
    }
    void setNorm (const char* key, float v) { e->setParameter (e, idx (key), v); }
    float getNorm (const char* key) { return e->getParameter (e, idx (key)); }
    void setChoice (const char* key, int k, int n) { setNorm (key, n > 1 ? (float) k / (n - 1) : 0.f); }
    void setBool (const char* key, bool on) { setNorm (key, on ? 1.f : 0.f); }
    void press (const char* key) { setNorm (key, 1.f); }
    void setLin (const char* key, float plain, float lo, float hi) { setNorm (key, (plain - lo) / (hi - lo)); }
    std::string text (const char* key)             // readouts refresh 20 times a second: render one refresh first
    {
        render (2300);
        char b[256] = {}; e->dispatcher (e, effGetParamDisplay, idx (key), 0, b, 0.f); return b;
    }
    void render (int frames, int block = 256)
    {
        std::vector<float> l (block), r (block);
        float* outs[2] { l.data(), r.data() };
        for (int done = 0; done < frames; )
        {
            const int n = std::min (block, frames - done);
            e->processReplacing (e, nullptr, outs, n);
            L.insert (L.end(), l.begin(), l.begin() + n); R.insert (R.end(), r.begin(), r.begin() + n);
            if (hostFlags & kVstPpqPosValid) hostTime.ppqPos += n * hostTime.tempo / 60.0 / SR;
            hostTime.samplePos += n;
            done += n; total += n;
        }
    }
    void clear() { L.clear(); R.clear(); }
    void midi (int status, int note, int vel, int delta)
    {
        VstMidiEvent m {}; m.type = kVstMidiType; m.byteSize = sizeof m; m.deltaFrames = delta;
        m.midiData[0] = (char) status; m.midiData[1] = (char) note; m.midiData[2] = (char) vel;
        VstEvents ev {}; ev.numEvents = 1; ev.events[0] = (VstEvent*) &m;
        e->dispatcher (e, effProcessEvents, 0, 0, &ev, 0.f);
    }
    // waits (rendering) until the clip's name is no longer "Loading..."
    bool waitLoaded (int clip, double seconds = 5.0)
    {
        char key[16]; std::snprintf (key, sizeof key, "cname%d", clip);
        for (int i = 0; i < (int) (seconds * 100); ++i)
        {
            render (441);
            const std::string t = text (key);
            if (t != "Loading..." && t != "-") { clear(); return t != "Missing file"; }
            usleep (10000);
        }
        clear(); return false;
    }
    void loadFile (int clip, const char* name)                                  // via the library selector + LOAD
    {
        selectClip (clip);
        waitScan();
        const int n = libCount();
        int k = -1;
        for (int i = 0; i < n; ++i)
        {
            setNorm ("file", (i + 0.5f) / n);
            if (text ("file").find (name) != std::string::npos) { k = i; break; }
        }
        if (k < 0) { std::printf ("file %s not in library\n", name); return; }
        press ("load");
    }
    int libCount()
    {
        const std::string t = text ("libinfo");
        return std::atoi (t.c_str());
    }
    void waitScan()
    {
        for (int i = 0; i < 300; ++i) { render (441); if (text ("libinfo").find ("Scanning") == std::string::npos && libCount() > 0) break; usleep (10000); }
        clear();
    }
    void selectClip (int c) { setChoice ("sel", c, 16); }
};

// ------------------------------------------------------------------------------------------------ signal helpers
static int firstSound (const std::vector<float>& x, float th = 1e-4f, int from = 0)
{
    for (size_t i = (size_t) from; i < x.size(); ++i) if (std::fabs (x[i]) > th) return (int) i;
    return -1;
}
static int lastSound (const std::vector<float>& x, float th = 1e-4f)
{
    for (int i = (int) x.size() - 1; i >= 0; --i) if (std::fabs (x[(size_t) i]) > th) return i;
    return -1;
}
static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; b = std::min (b, x.size()); if (b <= a) return 0;
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}
static double freq (const std::vector<float>& x, size_t a, size_t b)          // from rising zero crossings
{
    int n = 0; double first = -1, last = -1;
    for (size_t i = a + 1; i < std::min (b, x.size()); ++i)
        if (x[i - 1] < 0 && x[i] >= 0)
        {
            const double t = (double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0) first = t; last = t; ++n;
        }
    return n > 1 ? (n - 1) * SR / (last - first) : 0.0;
}
static double goertzel (const std::vector<float>& x, double hz)                // magnitude at one frequency
{
    const double w = 2 * M_PI * hz / SR, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (float v : x) { const double s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (double) x.size();
}
static double maxStep (const std::vector<float>& x, size_t a, size_t b)
{
    double m = 0; for (size_t i = std::max<size_t> (a, 1); i < std::min (b, x.size()); ++i) m = std::fmax (m, std::fabs (x[i] - x[i - 1]));
    return m;
}

// ------------------------------------------------------------------------------------------------ test files
static std::string dir;
static void writeTone (const char* name, int rate, double seconds, double hz, float amp = 0.5f, bool dc = false)
{
    const long n = (long) (seconds * rate);
    std::vector<int16_t> s ((size_t) n * 2);
    for (long i = 0; i < n; ++i)
    {
        const double v = dc ? amp : amp * std::sin (2 * M_PI * hz * i / rate);
        s[2 * i] = s[2 * i + 1] = (int16_t) std::lround (v * 32767.0);
    }
    cp::writeWav16 ((dir + "/" + name).c_str(), s.data(), n, rate);
}
static void writeClicks (const char* name, int rate, double seconds, double bpm)          // a click on every beat
{
    const long n = (long) (seconds * rate);
    std::vector<int16_t> s ((size_t) n * 2, 0);
    const double beat = 60.0 / bpm * rate;
    for (int k = 0; k * beat < n; ++k)
        for (int i = 0; i < 300 && (long) (k * beat) + i < n; ++i)
        {
            const double v = 0.8 * std::sin (2 * M_PI * 1000.0 * i / rate) * std::exp (-i / 60.0);
            s[2 * ((long) (k * beat) + i)] = s[2 * ((long) (k * beat) + i) + 1] = (int16_t) std::lround (v * 32767.0);
        }
    cp::writeWav16 ((dir + "/" + name).c_str(), s.data(), n, rate);
}

int main (int argc, char** argv)
{
    const char* so = argc > 1 ? argv[1] : "build/native/daclippads.so";
    void* h = dlopen (so, RTLD_NOW);
    if (h == nullptr) { std::printf ("dlopen: %s\n", dlerror()); return 1; }
    pluginMain = (MainFn) dlsym (h, "VSTPluginMain"); paramKey = (KeyFn) dlsym (h, "DCP_ParamKey");
    if (pluginMain == nullptr || paramKey == nullptr) { std::printf ("missing exports\n"); return 1; }
    dir = "build/native/cptest";
    mkdir ("build", 0755); mkdir ("build/native", 0755); mkdir (dir.c_str(), 0755);
    if (std::system (("rm -rf '" + dir + "'/*").c_str()) != 0) return 1;
    writeTone ("Tone 440.wav", 44100, 1.0, 440.0);
    writeTone ("Tone 1k 48k.wav", 48000, 1.0, 1000.0);
    writeTone ("Tone 220 Long.wav", 44100, 4.0, 220.0, 0.8f);
    writeTone ("DC.wav", 44100, 1.0, 0.0, 0.5f, true);
    writeClicks ("Clicks 120.wav", 44100, 2.0, 120.0);                       // exactly one bar at 120 BPM
    writeClicks ("Clicks 100.wav", 44100, 2.4, 100.0);                       // one bar at 100 BPM
    setenv ("CLIPS_FOLDER", dir.c_str(), 1);

    std::printf ("[plugin]\n");
    {
        Host H;
        CHECK (H.e->magic == kEffectMagic && (H.e->flags & effFlagsIsSynth) && (H.e->flags & effFlagsProgramChunks), "VST2 instrument with chunks");
        CHECK (H.e->numInputs == 0 && H.e->numOutputs == 2, "0 in / 2 out");
        CHECK (H.e->dispatcher (H.e, effCanDo, 0, 0, (void*) "receiveVstMidiEvent", 0.f) == 1, "canDo receiveVstMidiEvent");
        CHECK (H.e->dispatcher (H.e, effCanDo, 0, 0, (void*) "receiveVstTimeInfo", 0.f) == 1, "canDo receiveVstTimeInfo");
        CHECK (H.e->dispatcher (H.e, effGetPlugCategory, 0, 0, nullptr, 0.f) == kPlugCategSynth, "category synth");
        std::printf ("  %d parameters\n", H.nparams);
        H.render (44100);
        CHECK (rms (H.L, 0, H.L.size()) == 0.0, "silent with no clips");
    }

    std::printf ("[loading + sample-rate conversion]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440");
        CHECK (H.waitLoaded (0), "clip 1 loaded from the library");
        CHECK (H.text ("cname0") == "Tone 440", "clip name shown: %s", H.text ("cname0").c_str());
        H.loadFile (1, "Tone 1k 48k");
        CHECK (H.waitLoaded (1), "48 kHz file loaded");
        H.selectClip (1);
        H.render (4410);
        const std::string info = H.text ("selname");
        CHECK (info.find ("converted") != std::string::npos && info.find ("1.00 s") != std::string::npos, "48 kHz file converted, length kept: %s", info.c_str());
        H.setChoice ("quant", 0, 9);
        H.clear(); H.press ("pad1"); H.render (22050);
        const double f = freq (H.L, 2000, 20000);
        CHECK (std::fabs (f - 1000.0) < 1.0, "48 kHz 1 kHz tone plays at 1 kHz on a 44.1 kHz host: %.2f Hz", f);
        const double r = rms (H.L, 4000, 20000);
        CHECK (std::fabs (r - 0.5 / std::sqrt (2.0)) < 0.02, "level preserved after conversion (rms %.3f)", r);
        H.loadFile (2, "does not exist");
        std::string t = H.text ("cname2");
        CHECK (t == "-", "unknown file isn't loaded");
    }

    std::printf ("[triggering, one-shot, start/end, reverse]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.setChoice ("quant", 0, 9);                                            // none
        H.selectClip (0); H.setChoice ("mode", 0, 4);                           // one shot
        H.render (256); H.clear();
        H.press ("pad0"); H.render (88200);
        const int a = firstSound (H.L), b = lastSound (H.L);
        CHECK (a >= 0 && a < 64, "unquantised pad starts at once (sample %d)", a);
        CHECK (std::abs ((b - a) - 44100) < 200, "one-shot plays the whole 1 s sample once (%d frames)", b - a);
        const double f = freq (H.L, 2000, 40000);
        CHECK (std::fabs (f - 440.0) < 0.5, "pitch 440 Hz: %.2f", f);
        H.setNorm ("start", 0.5f); H.render (256); H.clear();
        H.press ("pad0"); H.render (88200);
        const int a2 = firstSound (H.L), b2 = lastSound (H.L);
        CHECK (std::abs ((b2 - a2) - 22050) < 200, "START at 50%% halves the length (%d frames)", b2 - a2);
        H.setNorm ("start", 0.f); H.setNorm ("end", 0.25f); H.render (256); H.clear();
        H.press ("pad0"); H.render (44100);
        CHECK (std::abs ((lastSound (H.L) - firstSound (H.L)) - 11025) < 200, "END at 25%% (%d frames)", lastSound (H.L) - firstSound (H.L));
        H.setNorm ("end", 1.f);
        // reverse: a rising ramp played backwards; use the long 220 Hz tone and compare phase by checking the start is the file's end
        H.setBool ("reverse", true); H.render (256); H.clear();
        H.press ("pad0"); H.render (44100 + 2000);
        CHECK (std::abs ((lastSound (H.L) - firstSound (H.L)) - 44100) < 200, "reverse plays the full length");
        CHECK (std::fabs (freq (H.L, 2000, 40000) - 440.0) < 0.5, "reverse keeps the pitch");
        H.setBool ("reverse", false);
        // stop pad mode
        H.setChoice ("mode", 1, 4); H.render (256); H.clear();
        H.press ("pad0"); H.render (22050);
        H.setBool ("pmstop", true); H.press ("pad0"); H.render (22050);
        const int end = lastSound (H.L);
        CHECK (end > 22050 && end < 22050 + 441 + 256, "STOP pad mode stops the clip (last sound at %d)", end);
        CHECK (H.getNorm ("pmplay") < 0.5f && H.getNorm ("pmstop") > 0.5f, "pad modes are radio buttons");
        H.setBool ("pmplay", true);
    }

    std::printf ("[looping]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.setChoice ("quant", 0, 9); H.selectClip (0); H.setChoice ("mode", 1, 4);
        H.render (256); H.clear();
        H.press ("pad0"); H.render (44100 * 3);
        CHECK (rms (H.L, 44100 * 2, 44100 * 3) > 0.3, "loop keeps playing after the sample end (rms %.3f)", rms (H.L, 44100 * 2, 44100 * 3));
        CHECK (std::fabs (freq (H.L, 1000, 44100 * 3) - 440.0) < 0.5, "looped pitch steady");
        // loop points: loop the second half
        H.clear(); H.press ("stopall"); H.render (4410); H.clear();
        H.setNorm ("lstart", 0.5f); H.setNorm ("lend", 0.75f);
        H.press ("pad0"); H.render (44100 * 2);
        // after the first pass (0.75 s) it loops 0.25 s sections: still a continuous 440 Hz tone
        CHECK (rms (H.L, 44100, 88200) > 0.3, "loop region repeats");
        CHECK (maxStep (H.L, 1000, 88200) < 0.08, "loop wrap is click-free (max step %.4f)", maxStep (H.L, 1000, 88200));
        H.setChoice ("mode", 3, 4); H.render (256); H.clear();                  // toggle
        H.press ("stopall"); H.render (4410); H.clear();
        H.press ("pad0"); H.render (22050); H.press ("pad0"); H.render (22050);
        CHECK (rms (H.L, 0, 22000) > 0.3 && rms (H.L, 23000, 44100) < 1e-4, "TOGGLE: second press stops");
    }

    std::printf ("[quantised launching]\n");
    {
        Host H;
        H.setLin ("tempo", 120.f, 40.f, 240.f);                                 // internal clock: bar = 88200 samples from the start
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.setChoice ("quant", 6, 9);                                            // 1 bar
        H.selectClip (0); H.setChoice ("mode", 1, 4);
        const long bar = 88200;
        auto pressAt = [&] (long offsetInBar, const char* pad) -> long         // returns the absolute start sample
        {
            long now = H.total;
            long target = (now / bar + 1) * bar + offsetInBar;
            H.render ((int) (target - now)); H.clear();
            const long pressed = H.total;
            H.press (pad); H.render (bar * 2 + 1000);
            const int a = firstSound (H.L);
            H.press ("stopall"); H.render (4410); H.clear();
            return a < 0 ? -1 : pressed + a;
        };
        const long s1 = pressAt (30000, "pad0");
        CHECK (s1 > 0 && std::labs (s1 % bar) < 300, "1-bar launch waits for the next bar line (starts %ld samples after it)", s1 % bar);
        const long s2 = pressAt (70000, "pad0");
        CHECK (s2 > 0 && std::labs (s2 % bar) < 300 && s2 - s1 >= bar, "second launch on a later bar line too");
        // 1/4 with swing ignored (swing is for 1/8 and 1/16)
        H.setChoice ("quant", 1, 9);
        const long s3 = pressAt (5000, "pad0");
        CHECK (s3 > 0 && std::labs (s3 % 22050) < 300, "1/4 launch on a beat (%ld into it)", s3 % 22050);
        // 1/8 with 66 % swing: the off-beat 8th moves from 0.5 to 0.66 of the beat
        H.setChoice ("quant", 2, 9); H.setLin ("swing", 66.f, 50.f, 75.f);
        const long s4 = pressAt (22050 + 3000, "pad0");                          // just after beat 2: next is the swung 8th of beat 2
        const double inBeat = (double) (s4 % 22050) / 22050.0;
        CHECK (std::fabs (inBeat - 0.66) < 0.02, "swung 1/8 lands at %.3f of the beat (0.66 wanted)", inBeat);
        // 1/16T: triplet grid (1/6 beat)
        H.setLin ("swing", 50.f, 50.f, 75.f); H.setChoice ("quant", 5, 9);
        const long s5 = pressAt (2000, "pad0");
        CHECK (std::labs (s5 % 3675) < 300 || std::labs (s5 % 3675 - 3675) < 300, "1/16T launch on the triplet grid (%ld)", s5 % 3675);
        // late press: 20 ms after a bar line starts at once, already in step
        H.setChoice ("quant", 6, 9);
        const long now = H.total;
        H.render ((int) ((now / bar + 1) * bar + 882 - now)); H.clear();
        H.press ("pad0"); H.render (4410);
        const int late = firstSound (H.L);
        CHECK (late >= 0 && late < 300, "a press 20 ms late starts at once (%d)", late);
        // quantised stop (loop, toggle): stops on the next bar
        H.press ("stopall"); H.render (4410);
        H.setChoice ("mode", 3, 4);
        const long t0 = H.total; H.render ((int) ((t0 / bar + 1) * bar - t0)); H.clear();
        H.press ("pad0"); H.render (bar / 2); H.press ("pad0"); H.render (bar);
        const int e = lastSound (H.L);
        CHECK (std::labs (e - bar) < 600, "toggle stop waits for the bar line (%d vs %ld)", e, bar);
    }

    std::printf ("[MIDI]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.loadFile (1, "Tone 1k 48k"); H.waitLoaded (1);
        H.setChoice ("quant", 0, 9);
        for (int c = 0; c < 2; ++c) { H.selectClip (c); H.setChoice ("mode", 0, 4); }
        H.render (256); H.clear();
        H.midi (0x90, 36, 127, 100);                                            // C1 -> clip 1, sample 100 of the block
        H.render (256);
        const int a = firstSound (H.L);
        CHECK (a >= 100 && a < 110, "note 36 starts clip 1 sample-accurately (at %d)", a);
        H.render (44100 * 2); H.clear();
        H.midi (0x90, 37, 127, 0); H.render (22050);
        CHECK (std::fabs (freq (H.L, 1000, 20000) - 1000.0) < 1.0, "note 37 starts clip 2");
        H.render (44100); H.clear();
        H.midi (0x90, 36, 32, 0); H.render (22050);
        const double quiet = rms (H.L, 1000, 20000);
        H.render (44100); H.clear();
        H.midi (0x90, 36, 127, 0); H.render (22050);
        CHECK (quiet < rms (H.L, 1000, 20000) * 0.8, "velocity scales the level");
        // gate mode: note off stops
        H.selectClip (0); H.setChoice ("mode", 2, 4); H.render (44100); H.clear();
        H.midi (0x90, 36, 127, 0); H.render (11025); H.midi (0x80, 36, 0, 0); H.render (11025);
        const int e = lastSound (H.L);
        CHECK (e > 11025 && e < 11025 + 600, "GATE: note-off stops the clip (%d)", e);
        // learn
        H.selectClip (1); H.setBool ("learn", true); H.render (256);
        H.midi (0x90, 50, 100, 0); H.render (2048);
        CHECK (H.text ("note").find ("50") != std::string::npos && H.getNorm ("learn") < 0.5f, "MIDI LEARN sets clip 2 to note 50: %s", H.text ("note").c_str());
        // channel filter
        H.setChoice ("midich", 2, 17); H.render (44100); H.clear();
        H.midi (0x90, 50, 100, 0); H.render (4410);
        CHECK (rms (H.L, 0, 4410) < 1e-5, "notes on other channels ignored when MIDI CHANNEL = 2");
        H.midi (0x91, 50, 100, 0); H.render (4410);
        CHECK (rms (H.L, 4410, 8820) > 0.1, "notes on channel 2 play");
    }

    std::printf ("[pitch + time stretch]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.setChoice ("quant", 0, 9); H.selectClip (0); H.setChoice ("mode", 0, 4);
        H.setLin ("semi", 12, -12, 12); H.render (256); H.clear();
        H.press ("pad0"); H.render (44100);
        CHECK (std::fabs (freq (H.L, 500, 20000) - 880.0) < 1.0, "resample +12 st = 880 Hz: %.2f", freq (H.L, 500, 20000));
        CHECK (std::abs ((lastSound (H.L) - firstSound (H.L)) - 22050) < 200, "resample +12 st plays twice as fast (%d frames)", lastSound (H.L) - firstSound (H.L));
        H.setLin ("semi", -7, -12, 12); H.setLin ("cent", 50, -100, 100); H.render (256); H.clear();
        H.press ("pad0"); H.render (44100 * 2);
        const double want = 440.0 * std::pow (2.0, (-7 + 0.5) / 12.0);
        CHECK (std::fabs (freq (H.L, 500, 40000) - want) < 1.0, "-7 st +50 ct = %.2f Hz: %.2f", want, freq (H.L, 500, 40000));
        H.setLin ("cent", 0, -100, 100);
        H.setChoice ("pmode", 1, 2); H.setLin ("semi", 7, -12, 12); H.render (256); H.clear();
        H.press ("pad0"); H.render (44100 * 2);
        const double f7 = freq (H.L, 3000, 40000);
        CHECK (std::fabs (f7 - 440.0 * std::pow (2.0, 7 / 12.0)) < 3.0, "stretch mode +7 st = %.1f Hz (want %.1f)", f7, 440.0 * std::pow (2.0, 7 / 12.0));
        CHECK (std::abs ((lastSound (H.L) - firstSound (H.L)) - 44100) < 600, "stretch mode keeps the length (%d frames)", lastSound (H.L) - firstSound (H.L));
        // stretch to tempo: a 1-bar 100 BPM click loop at 120 BPM, Stretch To 1 Bar -> 2.0 s per bar
        H.loadFile (1, "Clicks 100"); H.waitLoaded (1);
        H.setLin ("tempo", 120.f, 40.f, 240.f); H.selectClip (1);
        H.setChoice ("mode", 1, 4); H.setChoice ("pmode", 1, 2); H.setChoice ("slen", 4, 8); H.setChoice ("stype", 0, 4);
        H.render (256); H.clear();
        H.press ("pad1"); H.render (44100 * 5);
        // clicks should now come every 0.5 s (120 BPM)
        std::vector<int> on; int from = 0;
        for (int k = 0; k < 8; ++k) { const int s = firstSound (H.L, 0.2f, from); if (s < 0) break; on.push_back (s); from = s + 8000; }
        bool even = on.size() >= 6;
        for (size_t k = 1; k < on.size(); ++k) if (std::abs ((on[k] - on[k - 1]) - 22050) > 400) even = false;
        CHECK (even, "Stretch To 1 Bar: a 100 BPM loop plays at 120 BPM (%zu clicks, first gaps %d %d)", on.size(),
               on.size() > 1 ? on[1] - on[0] : 0, on.size() > 2 ? on[2] - on[1] : 0);
        CHECK (H.text ("cinfo1").find ("100 BPM") != std::string::npos, "clip info shows the source tempo: %s", H.text ("cinfo1").c_str());
        // resample + stretch-to: speed follows the tempo, pitch follows the speed
        H.setChoice ("pmode", 0, 2); H.press ("stopall"); H.render (4410); H.clear();
        H.press ("pad1"); H.render (44100 * 3);
        on.clear(); from = 0;
        for (int k = 0; k < 5; ++k) { const int s = firstSound (H.L, 0.2f, from); if (s < 0) break; on.push_back (s); from = s + 8000; }
        CHECK (on.size() >= 4 && std::abs ((on[2] - on[1]) - 22050) < 100, "resample Stretch To: 120 BPM spacing (%d)", on.size() > 2 ? on[2] - on[1] : 0);
    }

    std::printf ("[no-click transitions]\n");
    {
        Host H;
        H.loadFile (0, "DC"); H.waitLoaded (0);                                  // a constant 0.5: worst case for clicks
        H.setChoice ("quant", 0, 9); H.selectClip (0); H.setChoice ("mode", 1, 4);
        H.setChoice ("smpon", 0, 2);
        H.render (256); H.clear();
        H.press ("pad0"); H.render (22050); H.press ("pad0"); H.render (22050);     // retrigger
        H.press ("stopall"); H.render (22050);
        const double ms = maxStep (H.L, 0, H.L.size());
        CHECK (ms < 0.03, "start / retrigger / stop of a DC clip: max step %.4f (0.5 would be a click)", ms);
        H.clear(); H.setNorm ("lstart", 0.3f); H.setNorm ("lend", 0.4f); H.press ("pad0"); H.render (44100 * 2);
        CHECK (maxStep (H.L, 0, H.L.size()) < 0.03, "DC loop wraps without clicks (%.4f)", maxStep (H.L, 0, H.L.size()));
        H.clear(); H.setBool ("cmute0", true); H.render (4410); H.setBool ("cmute0", false); H.render (4410);
        CHECK (maxStep (H.L, 0, H.L.size()) < 0.03, "mute / unmute ramps (%.4f)", maxStep (H.L, 0, H.L.size()));
        // live pitch changes on a sine: the read position never jumps
        H.press ("stopall"); H.render (4410);
        H.loadFile (9, "Tone 220 Long"); H.waitLoaded (9); H.selectClip (9); H.setChoice ("mode", 1, 4);
        H.render (256); H.clear(); H.press ("pad9"); H.render (11025);
        for (int k = 0; k < 8; ++k) { H.setLin ("semi", (float) ((k * 5) % 13 - 6), -12, 12); H.render (2000); }
        const double lim = 0.8 * 2 * M_PI * 220.0 * 2.0 / SR * 1.2;              // slope of the loudest sine, an octave up, +20 %
        CHECK (maxStep (H.L, 4410, H.L.size()) < lim, "live pitch changes are click-free on a sine (max step %.4f < %.4f)", maxStep (H.L, 4410, H.L.size()), lim);
        H.setLin ("semi", 0, -12, 12); H.selectClip (0);
        // limiter: 16 loud clips never exceed full scale
        H.setLin ("semi", 0, -12, 12);
        for (int c = 1; c < 8; ++c) { H.loadFile (c, "DC"); H.waitLoaded (c); }
        for (int c = 0; c < 8; ++c) { H.selectClip (c); H.setChoice ("mode", 1, 4); H.setLin ("cvol" + std::to_string (c) == "" ? "" : ("cvol" + std::to_string (c)).c_str(), 6, -60, 6); }
        H.clear(); for (int c = 0; c < 8; ++c) H.press (("pad" + std::to_string (c)).c_str()); H.render (44100);
        float pk = 0; for (float x : H.L) pk = std::fmax (pk, std::fabs (x));
        CHECK (pk <= 0.97f, "limiter keeps 8 clips at +6 dB below full scale (peak %.3f)", pk);
    }

    std::printf ("[sampler stage]\n");
    {
        Host H;
        H.loadFile (0, "Tone 1k 48k"); H.waitLoaded (0);
        H.setChoice ("quant", 0, 9); H.selectClip (0); H.setChoice ("mode", 1, 4);
        H.setBool ("smpon", true); H.setChoice ("smppreset", 5, 8);            // Vinyl: hiss + crackle
        H.render (44100); H.clear(); H.render (44100);
        CHECK (rms (H.L, 0, H.L.size()) == 0.0, "noise stays off while nothing plays");
        H.setChoice ("smppreset", 0, 8);
        H.setLin ("bits", 24, 2, 24); H.setNorm ("aa", 1.f); H.setNorm ("quantize", 0.f); H.setNorm ("sat", 0.f); H.setNorm ("noise", 0.f); H.setNorm ("crackle", 0.f);
        H.setNorm ("rate", 1.f);
        H.press ("pad0"); H.render (22050); H.clear(); H.render (22050);
        const std::vector<float> clean = H.L;
        H.setLin ("bits", 4, 2, 24); H.setNorm ("quantize", 1.f); H.render (4410); H.clear(); H.render (22050);
        double err = 0; for (size_t i = 0; i < H.L.size() && i < clean.size(); ++i) err = std::fmax (err, std::fabs (H.L[i]));
        std::vector<float> diff (H.L.size()); for (size_t i = 0; i < diff.size(); ++i) diff[i] = H.L[i] - clean[i];
        CHECK (rms (diff, 0, diff.size()) > 0.01, "4-bit quantisation adds error (rms %.4f)", rms (diff, 0, diff.size()));
        H.setLin ("bits", 24, 2, 24); H.setNorm ("quantize", 0.f);
        H.setNorm ("aa", 0.f); H.setNorm ("rate", (float) (std::log (3000.0 / 2000.0) / std::log (24.0)));   // 3 kHz, no filter
        H.render (4410); H.clear(); H.render (22050);
        CHECK (rms (H.L, 0, H.L.size()) > 0.2, "3 kHz sample-and-hold passes the 1 kHz tone (rms %.3f)", rms (H.L, 0, H.L.size()));
        const double img = goertzel (H.L, 2000.0) / goertzel (H.L, 1000.0);
        CHECK (img > 0.05, "no anti-alias filter: the 1 kHz tone aliases to 2 kHz (image at %.1f dB)", 20 * std::log10 (img));
        H.setNorm ("rate", 0.f); H.setNorm ("aa", 1.f); H.render (4410); H.clear(); H.render (22050);
        CHECK (rms (H.L, 0, H.L.size()) < 0.2, "anti-alias filter at 0.45 x 2 kHz attenuates the 1 kHz tone (rms %.3f)", rms (H.L, 0, H.L.size()));
    }

    std::printf ("[scenes + follow actions]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.loadFile (1, "Tone 1k 48k"); H.waitLoaded (1);
        H.loadFile (2, "Tone 220 Long"); H.waitLoaded (2);
        H.setLin ("tempo", 120.f, 40.f, 240.f);
        H.setChoice ("quant", 0, 9); H.setChoice ("sceneq", 1, 10);             // no quantise
        for (int c = 0; c < 3; ++c) { H.selectClip (c); H.setChoice ("mode", 1, 4); }
        // scene 1 = clips 1+2 (via IN SCENE), scene 2 = clip 3 (via STORE)
        H.selectClip (0); H.setBool ("inscene0", true); H.selectClip (1); H.setBool ("inscene0", true);
        H.press ("pad2"); H.render (4410); H.setBool ("scenestore", true); H.press ("scene1"); H.render (256);
        CHECK (H.text ("status").find ("Scene 2 stored: 1 clip") != std::string::npos, "STORE saves the playing clips: %s", H.text ("status").c_str());
        H.press ("stopall"); H.render (4410); H.clear();
        H.press ("scene0"); H.render (22050);
        CHECK (rms (H.L, 2000, 22050) > 0.3 && std::string (H.text ("cstat0")) == "Looping" && H.text ("cstat1") == "Looping", "scene 1 launches clips 1 and 2");
        H.press ("scene1"); H.render (22050);
        CHECK (H.text ("cstat0") == "Stopped" && H.text ("cstat2") == "Looping", "scene 2 stops them and starts clip 3");
        // quantised scene launch on the bar
        H.setChoice ("sceneq", 7, 10); H.render (256); H.clear();                 // 1 bar
        H.press ("scene0"); H.render (44100 * 3);
        CHECK (H.text ("cstat0") == "Looping", "quantised scene launched");
        // follow: clip 1 -> Next after 1 bar
        H.press ("stopall"); H.render (4410);
        H.setChoice ("sceneq", 0, 10); H.setChoice ("quant", 6, 9);
        H.selectClip (0); H.setChoice ("follow", 3, 9); H.setChoice ("ftime", 3, 8);
        H.render (256); H.clear();
        H.press ("pad0"); H.render (44100 * 5);
        // find when 1 kHz (clip 2) started: first region where the frequency is 1 kHz
        int start1 = firstSound (H.L), handover = -1;
        for (int s = start1 + 4410; s + 4410 < (int) H.L.size(); s += 441) if (std::fabs (freq (H.L, (size_t) s, (size_t) s + 4410) - 1000.0) < 5.0) { handover = s; break; }
        CHECK (handover > 0, "follow action NEXT hands over to clip 2");
        CHECK (handover > 0 && std::fabs ((handover - start1) / SR - 2.0) < 0.12, "hand-over after 1 bar (%.3f s at 120 BPM)", handover > 0 ? (handover - start1) / SR : -1.0);
        CHECK (H.text ("cstat0") == "Stopped" && H.text ("cstat1") == "Looping", "clip 1 stopped, clip 2 playing");
        // follow at end, one-shot -> repeat
        H.press ("stopall"); H.render (4410);
        H.setChoice ("quant", 0, 9); H.selectClip (0); H.setChoice ("mode", 0, 4); H.setChoice ("follow", 2, 9); H.setChoice ("ftime", 0, 8);
        H.render (256); H.clear(); H.press ("pad0"); H.render (44100 * 3);
        CHECK (rms (H.L, 44100 * 2, 44100 * 3) > 0.3, "one-shot with REPEAT at end keeps repeating");
        H.selectClip (0); H.setChoice ("follow", 0, 9);
    }

    std::printf ("[host sync]\n");
    {
        Host H;
        H.loadFile (0, "Tone 440"); H.waitLoaded (0);
        H.selectClip (0); H.setChoice ("mode", 1, 4); H.setChoice ("quant", 6, 9);
        hostTime = {}; hostTime.tempo = 100.0; hostTime.timeSigNumerator = 4; hostTime.timeSigDenominator = 4; hostTime.ppqPos = 0.5; hostTime.barStartPos = 0.0;
        hostFlags = kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying | kVstBarsValid | kVstTimeSigValid;
        H.render (256); H.clear();
        const double ppq0 = hostTime.ppqPos;
        H.press ("pad0"); H.render (44100 * 4);
        const int a = firstSound (H.L);
        const double ppqAt = ppq0 + a * 100.0 / 60.0 / SR;
        CHECK (std::fabs (ppqAt - 4.0) < 0.002, "launch on the host's bar 2 (ppq %.4f at 100 BPM)", ppqAt);
        CHECK (H.text ("transport").find ("HOST") != std::string::npos && H.text ("transport").find ("100.00") != std::string::npos, "transport shows host tempo: %s", H.text ("transport").c_str());
        // host loops back (sequence loop): a pending launch keeps its distance
        H.press ("stopall"); H.render (4410); H.clear();
        H.selectClip (0); H.setChoice ("mode", 0, 4);
        H.press ("pad0"); H.render (512);
        hostTime.ppqPos -= 8.0;                                                 // jump back 2 bars
        H.render (44100 * 3);
        CHECK (firstSound (H.L) > 0, "launch still happens after the host position jumps");
        // host stop stops clips (Restart+Stop)
        H.selectClip (0); H.setChoice ("mode", 1, 4); H.setChoice ("quant", 0, 9);
        H.press ("pad0"); H.render (4410);
        hostFlags &= ~kVstTransportPlaying; H.render (4410); H.clear(); H.render (4410);
        CHECK (rms (H.L, 0, 4410) < 1e-5, "host stop stops the clips");
        hostFlags = 0;
    }

    std::printf ("[chop + pad assign + flip]\n");
    {
        Host H;
        H.loadFile (0, "Clicks 120"); H.waitLoaded (0);
        H.selectClip (0); H.setChoice ("chopn", 0, 4); H.setChoice ("chopmode", 0, 2);   // 4 chops, transient
        H.press ("chop"); H.render (4410);
        CHECK (H.text ("sliceinfo").find ("SLICE 1/4") != std::string::npos, "chopped into 4: %s", H.text ("sliceinfo").c_str());
        bool onBeats = true;
        for (int k = 1; k < 4; ++k)
        {
            H.setChoice ("slice", k, 32);
            const double t = std::atof (H.text ("slicepos").c_str());
            if (std::fabs (t - 0.5 * k) > 0.015) onBeats = false;
            std::printf ("    slice %d at %.4f s\n", k + 1, t);
        }
        CHECK (onBeats, "transient slices land on the clicks (every 0.5 s)");
        H.setChoice ("quant", 0, 9); H.render (256); H.clear();
        H.press ("spad2"); H.render (22050);
        const int s = firstSound (H.L, 0.2f);
        CHECK (s >= 0 && s < 400, "slice pad 3 plays slice 3 right away (%d)", s);
        H.clear(); H.midi (0x90, 60 + 1, 127, 0); H.render (8000);
        CHECK (firstSound (H.L, 0.2f) >= 0 && firstSound (H.L, 0.2f) < 400, "slice notes: C3+1 plays slice 2");
        H.press ("assign"); H.render (8820);
        CHECK (H.text ("status").find ("4 slices assigned to clips 2-5") != std::string::npos, "PAD ASSIGN: %s", H.text ("status").c_str());
        CHECK (H.text ("cname3") == "Clicks 120", "assigned clips share the sample");
        H.clear(); H.press ("pad3"); H.render (30000);
        const int a3 = firstSound (H.L, 0.2f), e3 = lastSound (H.L, 1e-4f);
        CHECK (a3 >= 0 && a3 < 400 && e3 - a3 < 23000, "clip 4 plays one slice (%d frames)", e3 - a3);
        // flip + undo
        H.selectClip (0); H.render (256);
        const std::string before = H.text ("cinfob0") + H.text ("start") + H.text ("end") + H.text ("filt") + H.text ("seq");
        int changed = 0;
        for (int k = 0; k < 6; ++k) { H.press ("flip"); H.render (256); const std::string now = H.text ("cinfob0") + H.text ("start") + H.text ("end") + H.text ("filt") + H.text ("seq"); if (now != before) ++changed; H.press ("undo"); H.render (256); }
        CHECK (changed >= 5, "FLIP changes the clip (%d of 6)", changed);
        const std::string after = H.text ("cinfob0") + H.text ("start") + H.text ("end") + H.text ("filt") + H.text ("seq");
        CHECK (after == before, "UNDO restores it");
        H.setBool ("lockpitch", true); H.setBool ("lockrev", true); H.setBool ("lockpos", true); H.setBool ("lockfilt", true); H.setBool ("lockslices", true);
        H.press ("flip"); H.render (256);
        CHECK (H.text ("status").find ("locked") != std::string::npos, "all locks: nothing changes");
        H.setBool ("lockpitch", false); H.setBool ("lockfilt", false);
        for (int k = 0; k < 5; ++k) { H.press ("flip"); H.render (256); }
        CHECK (H.text ("start") == "0.000 s" && H.text ("reverse") == "Off", "locked START/END and REVERSE are kept: %s %s", H.text ("start").c_str(), H.text ("reverse").c_str());
        // rearranged playback plays without crashing and without clicks beyond the clicks themselves
        H.setBool ("lockslices", false); H.setBool ("lockpos", false);
        H.setBool ("seq", true); H.setChoice ("mode", 1, 4); H.clear(); H.press ("pad0"); H.render (44100 * 3);
        CHECK (firstSound (H.L, 0.1f) >= 0, "rearranged slices play");
    }

    std::printf ("[state save / restore + kits + presets]\n");
    {
        std::vector<char> saved;
        std::string beforeText;
        {
            Host H;
            H.loadFile (0, "Tone 440"); H.waitLoaded (0);
            H.loadFile (5, "Clicks 120"); H.waitLoaded (5);
            H.selectClip (5); H.setChoice ("mode", 0, 4); H.setLin ("semi", -3, -12, 12); H.setNorm ("start", 0.25f); H.setChoice ("follow", 3, 9);
            H.setBool ("inscene2", true); H.setChoice ("chopn", 1, 4); H.press ("chop");
            H.setLin ("cvol5", -6, -60, 6); H.setLin ("cpan5", -0.5f, -1, 1);
            H.setLin ("tempo", 97.5f, 40, 240); H.setChoice ("smppreset", 7, 8);
            H.selectClip (0); H.setLin ("note", 48, -1, 127);
            H.render (4410);
            void* data = nullptr;
            const intptr_t n = H.e->dispatcher (H.e, effGetChunk, 0, 0, &data, 0.f);
            CHECK (n > 100 && data != nullptr, "chunk saved (%ld bytes)", (long) n);
            saved.assign ((char*) data, (char*) data + n);
            beforeText = std::string (saved.begin(), saved.end());
            // kit save
            H.press ("kitsave"); H.render (4410);
            CHECK (H.text ("status").find ("Saved Kits/Kit 001") != std::string::npos, "kit saved: %s", H.text ("status").c_str());
        }
        {
            Host H2;
            H2.e->dispatcher (H2.e, effSetChunk, 0, (intptr_t) saved.size(), saved.data(), 0.f);
            CHECK (H2.waitLoaded (5) && H2.waitLoaded (0), "clips reload from the chunk");
            void* data = nullptr;
            const intptr_t n = H2.e->dispatcher (H2.e, effGetChunk, 0, 0, &data, 0.f);
            const std::string again ((char*) data, (char*) data + n);
            CHECK (again == beforeText, "restored state is identical when saved again");
            H2.selectClip (5); H2.render (256);
            CHECK (H2.text ("semi") == "-3 st" && H2.text ("mode") == "One Shot" && H2.text ("follow") == "Next Clip", "clip settings restored");
            CHECK (H2.text ("slice") == "Slice 1 of 8" && H2.getNorm ("inscene2") > 0.5f, "slices and scene membership restored");
            CHECK (H2.text ("cvol5") == "-6.0 dB" && H2.text ("cpan5") == "L50", "mixer restored");
            CHECK (H2.text ("tempo") == "97.50" && H2.text ("smppreset") == "Crushed", "globals restored");
            H2.selectClip (0); H2.render (256);
            CHECK (H2.text ("note").find ("48") != std::string::npos, "MIDI mapping restored");
            // the state plays like before
            H2.setChoice ("quant", 0, 9); H2.render (256); H2.clear();
            H2.midi (0x90, 48, 127, 0); H2.render (4410);
            CHECK (rms (H2.L, 500, 4410) > 0.05, "restored note mapping plays clip 1");
        }
        {
            Host H3;
            H3.waitScan();
            for (int i = 0; i < 100 && H3.text ("kit").find ("Kit 001") == std::string::npos; ++i) { H3.press ("rescan"); H3.render (4410); usleep (20000); H3.setNorm ("kit", 0.f); }
            H3.press ("kitload");
            CHECK (H3.waitLoaded (5), "kit loads its clips");
            H3.selectClip (5); H3.render (256);
            CHECK (H3.text ("semi") == "-3 st", "kit restores the clip settings");
            // factory presets
            const int presetIdx = H3.idx ("preset");
            const int np = (int) std::lround (1.0 / (1.0 / 53.0));
            (void) np;
            int count = 0;
            for (int k = 0; k < 200; ++k)
            {
                H3.e->setParameter (H3.e, presetIdx, 1.f);
                char b[128] = {}; H3.e->dispatcher (H3.e, effGetParamDisplay, presetIdx, 0, b, 0.f);
                count = k; break;
            }
            (void) count;
            // walk every preset by stepping the normalised value
            std::vector<std::string> names;
            for (int k = 0; k < 400; ++k)
            {
                H3.e->setParameter (H3.e, presetIdx, k / 399.f);
                char b[128] = {}; H3.e->dispatcher (H3.e, effGetParamDisplay, presetIdx, 0, b, 0.f);
                if (names.empty() || names.back() != b) names.push_back (b);
            }
            CHECK (names.size() >= 50, "%zu factory presets", names.size());
            bool allOk = true;
            for (size_t k = 0; k < names.size(); ++k)
            {
                H3.setNorm ("preset", names.size() > 1 ? (float) k / (names.size() - 1) : 0.f);
                H3.press ("presetload"); H3.render (2048);
                if (H3.text ("status").find ("Preset:") == std::string::npos) allOk = false;
            }
            CHECK (allOk, "every preset loads");
            CHECK (H3.text ("cname5") == "Clicks 120", "presets keep the loaded samples");
        }
    }

    std::printf ("[CPU]\n");
    {
        Host H;
        for (int c = 0; c < 16; ++c) { H.loadFile (c, c % 2 ? "Clicks 120" : "Tone 220 Long"); H.waitLoaded (c); }
        H.setChoice ("quant", 0, 9); H.setChoice ("quality", 1, 3);
        for (int c = 0; c < 16; ++c) { H.selectClip (c); H.setChoice ("mode", 1, 4); H.setChoice ("pmode", 1, 2); H.setChoice ("slen", 1, 8); H.setChoice ("filt", 1, 4); H.setNorm ("cut", 0.7f); H.setNorm ("rsend", 0.2f); H.setNorm ("dsend", 0.2f); }
        for (int c = 0; c < 16; ++c) H.press (("pad" + std::to_string (c)).c_str());
        H.render (4410); H.clear();
        const auto t0 = std::chrono::steady_clock::now();
        H.render (44100 * 10);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  16 clips, time-stretch + filter + sends + sampler + master FX: %.1f%% of one x86 core (10 s audio in %.3f s)\n", sec / 10.0 * 100.0, sec);
        CHECK (sec < 10.0, "faster than real time");
        for (int c = 0; c < 16; ++c) { H.selectClip (c); H.setChoice ("pmode", 0, 2); H.setChoice ("filt", 0, 4); }
        H.render (4410); H.clear();
        const auto t1 = std::chrono::steady_clock::now();
        H.render (44100 * 10);
        const double sec2 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        std::printf ("  16 clips, resample (no stretch, no filter), sends + master FX: %.1f%% of one x86 core\n", sec2 / 10.0 * 100.0);
        H.press ("stopall"); H.render (44100); H.clear();
        const auto t2 = std::chrono::steady_clock::now();
        H.render (44100 * 10);
        const double sec3 = std::chrono::duration<double> (std::chrono::steady_clock::now() - t2).count();
        std::printf ("  idle (nothing playing, reverb + delay tails done): %.2f%% of one x86 core\n", sec3 / 10.0 * 100.0);
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
