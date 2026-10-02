// Da Sample Lab: a Serato Sample-style slicing sampler as a native MPC OS VST2 instrument (MPC X and other Gen1
// devices). Load a WAV, get its BPM and key, chop it into up to 64 slices (transients, beats, bars, musical sections
// or equal parts), play the slices from the pads (4 banks of 16) or one slice chromatically, edit and rearrange them,
// pitch / time-stretch / sync to the MPC's tempo, and export slices as WAVs at the original quality.
//
// Threads: MPC's UI thread (parameters, chunk), the audio thread (process: never allocates, locks or touches files)
// and one worker thread here (file scan, loading + analysis, chopping, exports). Samples are swapped with an atomic
// pointer and freed by the worker a few seconds after the audio thread let go of them.
#include "vst2.h"
#include "core/Sample.h"
#include "core/Grains.h"
#include "Analysis.h"
#include "Export.h"
#include <atomic>
#include <dirent.h>
#include <mutex>
#include <new>
#include <pthread.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#define SL_EXPORT extern "C" __attribute__ ((visibility ("default")))

namespace
{
using namespace cp;
constexpr int kVersion = 1000, kMaxSlices = 64, kVoices = 16, kWaveBars = 128, kMaxFiles = 1024;
constexpr double kMaxSeconds = 600.0;          // longest sample (10 min); about 100 MB in memory at 44.1 kHz

// ------------------------------------------------------------------------------------------------ storage
// Everything lives on the MPC's internal storage, like the other GlueBus plugins:
//   /sdcard/SampleLab/Samples   your samples (anything loaded from USB is copied here first)
//   /sdcard/SampleLab/Exports   exported slices, one folder per sample
// SAMPLELAB_ROOT overrides the root (tests); SAMPLELAB_EXTRA adds a folder to scan.
const char* rootDir()
{
    static char r[300];
    if (! r[0]) std::snprintf (r, sizeof r, "%s", std::getenv ("SAMPLELAB_ROOT") ? std::getenv ("SAMPLELAB_ROOT") : "/sdcard/SampleLab");
    return r;
}
void mkdirs (const char* path)
{
    char b[600]; std::snprintf (b, sizeof b, "%s", path);
    for (char* p = b + 1; *p; ++p) if (*p == '/') { *p = 0; mkdir (b, 0755); *p = '/'; }
    mkdir (b, 0755);
}

// ------------------------------------------------------------------------------------------------ parameters
enum P
{
    P_FILE, P_LOAD, P_AUDITION, P_START, P_END, P_BPM, P_PITCH, P_FINE, P_TMODE, P_SYNC, P_SPEED, P_GAIN,
    P_CHOPMODE, P_SENS, P_COUNT, P_CHOP, P_SLICE, P_SLSTART, P_SLEND, P_SLVOL, P_SLREV, P_SLLOOP, P_SLPITCH, P_SLPLAY,
    P_SLLEFT, P_SLRIGHT, P_SPLIT, P_MERGE, P_EXPORT, P_EXPORTALL,
    P_PADMODE, P_BASENOTE, P_PLAYMODE, P_POLY, P_VELSENS, P_ATTACK, P_RELEASE,
    P_SLFILTER, P_SLATTACK, P_SLRELEASE, P_SLMUTE, P_SLCHOKE, P_TARGETKEY, P_BPMHALF, P_BPMDOUBLE, P_FIND, P_ZOOM, P_SOURCE, P_LATEST,
    P_STATUS, P_INFO, P_ANALYSIS, P_SLICEINFO, P_PADINFO,
    P_WAVE0, NPARAMS = P_WAVE0 + kWaveBars
};
enum Kind { K_FLOAT, K_CHOICE, K_TOGGLE, K_BUTTON, K_PICK, K_TEXT, K_WAVE };
struct Def { const char* key; const char* name; Kind kind; float lo, hi, def; const char* const* names; int n; };
const char* const kTModes[] { "Repitch", "Stretch" };
const char* const kSync[] { "Off", "MPC tempo" };
const char* const kChop[] { "Transients", "Beats", "Bars", "Sections", "Equal" };
const char* const kCounts[] { "4", "8", "12", "16", "24", "32", "48", "64" };
const int kCountVals[] { 4, 8, 12, 16, 24, 32, 48, 64 };
const char* const kPadModes[] { "Slices", "Chromatic" };
const char* const kPlayModes[] { "One-shot", "Gate" };
const char* const kPoly[] { "Poly", "Mono (choke)" };
const char* const kOnOff[] { "Off", "On" };
const char* const kChoke[] { "Off", "Group 1", "Group 2", "Group 3", "Group 4" };
const char* const kZoom[] { "Whole sample", "Start to end", "Selected slice" };
const char* const kSources[] { "SampleLab", "Recent recordings", "Internal drive", "USB drive" };
const char* const kTargetKeys[] { "Off", "C major", "C# major", "D major", "Eb major", "E major", "F major", "F# major", "G major", "Ab major", "A major",
                                  "Bb major", "B major", "C minor", "C# minor", "D minor", "Eb minor", "E minor", "F minor", "F# minor", "G minor",
                                  "Ab minor", "A minor", "Bb minor", "B minor" };
Def kDefs[NPARAMS];
char waveKeys[kWaveBars][12], waveNames[kWaveBars][16];
void initDefs()
{
    auto d = [] (int i, const char* k, const char* n, Kind kind, float lo, float hi, float def, const char* const* names = nullptr, int nn = 0) { kDefs[i] = { k, n, kind, lo, hi, def, names, nn }; };
    d (P_FILE, "file", "Sample", K_PICK, 0, 1, 0);
    d (P_LOAD, "load", "Load", K_BUTTON, 0, 1, 0);
    d (P_AUDITION, "audition", "Play Sample", K_BUTTON, 0, 1, 0);
    d (P_START, "start", "Start", K_FLOAT, 0, 1, 0);
    d (P_END, "end", "End", K_FLOAT, 0, 1, 1);
    d (P_BPM, "bpm", "BPM", K_FLOAT, 50, 200, 120);
    d (P_PITCH, "pitch", "Pitch", K_FLOAT, -24, 24, 0);
    d (P_FINE, "fine", "Fine", K_FLOAT, -100, 100, 0);
    d (P_TMODE, "tmode", "Time Mode", K_CHOICE, 0, 1, 0, kTModes, 2);
    d (P_SYNC, "sync", "Sync", K_CHOICE, 0, 1, 0, kSync, 2);
    d (P_SPEED, "speed", "Speed", K_FLOAT, 0.5f, 2.f, 1.f);
    d (P_GAIN, "gain", "Level", K_FLOAT, -24, 12, 0);
    d (P_CHOPMODE, "chopmode", "Chop By", K_CHOICE, 0, 4, 0, kChop, 5);
    d (P_SENS, "sens", "Sensitivity", K_FLOAT, 0, 100, 60);
    d (P_COUNT, "count", "Slices", K_CHOICE, 0, 7, 3, kCounts, 8);
    d (P_CHOP, "chop", "Chop", K_BUTTON, 0, 1, 0);
    d (P_SLICE, "slice", "Slice", K_PICK, 0, 1, 0);
    d (P_SLSTART, "slstart", "Slice Start", K_FLOAT, 0, 1, 0);
    d (P_SLEND, "slend", "Slice End", K_FLOAT, 0, 1, 1);
    d (P_SLVOL, "slvol", "Slice Level", K_FLOAT, -24, 6, 0);
    d (P_SLREV, "slrev", "Reverse", K_TOGGLE, 0, 1, 0, kOnOff, 2);
    d (P_SLLOOP, "slloop", "Loop", K_TOGGLE, 0, 1, 0, kOnOff, 2);
    d (P_SLPITCH, "slpitch", "Slice Pitch", K_FLOAT, -12, 12, 0);
    d (P_SLPLAY, "slplay", "Play Slice", K_BUTTON, 0, 1, 0);
    d (P_SLLEFT, "slleft", "Move Left", K_BUTTON, 0, 1, 0);
    d (P_SLRIGHT, "slright", "Move Right", K_BUTTON, 0, 1, 0);
    d (P_SPLIT, "split", "Split", K_BUTTON, 0, 1, 0);
    d (P_MERGE, "merge", "Merge", K_BUTTON, 0, 1, 0);
    d (P_EXPORT, "export", "Export Slice", K_BUTTON, 0, 1, 0);
    d (P_EXPORTALL, "exportall", "Export All", K_BUTTON, 0, 1, 0);
    d (P_PADMODE, "padmode", "Pad Mode", K_CHOICE, 0, 1, 0, kPadModes, 2);
    d (P_BASENOTE, "basenote", "First Pad Note", K_FLOAT, 0, 64, 36);
    d (P_PLAYMODE, "playmode", "Trigger", K_CHOICE, 0, 1, 0, kPlayModes, 2);
    d (P_POLY, "poly", "Voices", K_CHOICE, 0, 1, 0, kPoly, 2);
    d (P_VELSENS, "velsens", "Velocity", K_FLOAT, 0, 100, 80);
    d (P_ATTACK, "attack", "Attack", K_FLOAT, 0, 200, 0);
    d (P_RELEASE, "release", "Release", K_FLOAT, 5, 2000, 30);
    d (P_SLFILTER, "slfilter", "Slice Filter", K_FLOAT, -1, 1, 0);
    d (P_SLATTACK, "slattack", "Slice Attack", K_FLOAT, 0, 201, 0);         // 0 = use the global attack
    d (P_SLRELEASE, "slrelease", "Slice Release", K_FLOAT, 0, 2001, 0);     // 0 = use the global release
    d (P_SLMUTE, "slmute", "Mute", K_TOGGLE, 0, 1, 0, kOnOff, 2);
    d (P_SLCHOKE, "slchoke", "Choke Group", K_CHOICE, 0, 4, 0, kChoke, 5);
    d (P_TARGETKEY, "targetkey", "Key Shift To", K_CHOICE, 0, 24, 0, kTargetKeys, 25);
    d (P_BPMHALF, "bpmhalf", "BPM / 2", K_BUTTON, 0, 1, 0);
    d (P_BPMDOUBLE, "bpmdouble", "BPM x 2", K_BUTTON, 0, 1, 0);
    d (P_FIND, "find", "Find", K_BUTTON, 0, 1, 0);
    d (P_ZOOM, "zoom", "Zoom", K_CHOICE, 0, 2, 0, kZoom, 3);
    d (P_SOURCE, "source", "Browse", K_CHOICE, 0, 3, 0, kSources, 4);
    d (P_LATEST, "latest", "Latest Capture", K_BUTTON, 0, 1, 0);
    d (P_STATUS, "status", "Status", K_TEXT, 0, 1, 0);
    d (P_INFO, "info", "Sample Info", K_TEXT, 0, 1, 0);
    d (P_ANALYSIS, "analysis", "BPM / Key", K_TEXT, 0, 1, 0);
    d (P_SLICEINFO, "sliceinfo", "Slice Info", K_TEXT, 0, 1, 0);
    d (P_PADINFO, "padinfo", "Pad Info", K_TEXT, 0, 1, 0);
    for (int i = 0; i < kWaveBars; ++i)
    {
        std::snprintf (waveKeys[i], sizeof waveKeys[i], "wave%d", i); std::snprintf (waveNames[i], sizeof waveNames[i], "Wave %d", i + 1);
        d (P_WAVE0 + i, waveKeys[i], waveNames[i], K_WAVE, 0, 127, 0);
    }
}
float toNorm (int i, float v) { const Def& d = kDefs[i]; return d.hi > d.lo ? (v - d.lo) / (d.hi - d.lo) : 0.f; }
float toPlain (int i, float n)
{
    const Def& d = kDefs[i];
    n = clampf (n, 0.f, 1.f);
    if (d.kind == K_CHOICE || d.kind == K_TOGGLE) return std::floor (d.lo + n * (d.hi - d.lo) + 0.5f);
    return d.lo + n * (d.hi - d.lo);
}

// ------------------------------------------------------------------------------------------------ slices
// filter: -1..0 low-pass (20 kHz .. 200 Hz), 0..1 high-pass (20 Hz .. 5 kHz); att / rel in ms, -1 = the global setting;
// choke: 0 = none, 1..4 = a new hit cuts the other slices of the same group
struct Slice { long start = 0, end = 0; float vol = 0.f; bool rev = false, loop = false; float pitch = 0.f;
               float filter = 0.f, att = -1.f, rel = -1.f; bool mute = false; int choke = 0; };
struct SliceTable { Slice s[kMaxSlices]; int n = 0; };

// ------------------------------------------------------------------------------------------------ voices
struct Voice
{
    bool on = false, releasing = false;
    int note = -1, tag = 0;                   // tag: slice index, or -1 for the whole-sample audition
    Slice sl; long len = 0;
    double pos = 0, anchor = 0;               // logical position (0..len) in sample frames
    double rate = 1, timeRatio = 1, pitchRatio = 1;
    bool stretch = false;
    float gain = 1, env = 0, envTarget = 1, attackStep = 1, releaseStep = 1;
    int fmode = 0;                            // 0 off, 1 low-pass, 2 high-pass (state-variable filter, Q 0.707)
    float fa1 = 0, fa2 = 0, fa3 = 0, fk = 1.414f, ic1[2] {}, ic2[2] {};
    GrainSet grains;
    const Sample* smp = nullptr;
};

struct Plugin;
void* workerMain (void*);

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> nv[NPARAMS];
    float seen[NPARAMS];
    float sent[NPARAMS];
    double sr = 44100;
    // sample + analysis (published by the worker)
    std::atomic<Sample*> sample { nullptr };
    Sample* retired[8] {}; double retiredAt[8] {};
    sl::Analysis an; std::mutex anLock;
    char samplePath[512] {};                  // full path of the loaded file (on internal storage)
    std::atomic<int> sampleGen { 0 };
    // slices: double buffered, the audio thread copies one Slice at note-on
    SliceTable tables[2]; std::atomic<int> live { 0 };
    std::mutex editLock;
    // file list for the picker
    std::vector<std::string> files; int nFiles = 0; std::mutex filesLock;
    // texts
    Text text[5]; int gen[5] {};
    // voices + MIDI
    Voice v[kVoices];
    struct Ev { int delta, status, d1, d2; } evq[256]; int nev = 0;
    double hostTempo = 120;
    // worker
    pthread_t worker {}; bool workerOn = false; std::mutex qLock;
    std::atomic<int> reqLoad { 0 }, reqChop { 0 }, reqExport { 0 }, reqScan { 1 }, quit { 0 };
    char loadPath[512] {}; int exportWhich = -1;
    std::atomic<int> waveDirty { 1 };
    std::atomic<int> keyShift { 0 };          // semitones from KEY SHIFT TO (computed on the UI / worker thread)
    int variation = 0;                        // FIND: which alternative chop
    long latestSeen = 0;
    double guardUntil = 0;                    // buttons ignored until then (project load)
    std::atomic<int> auditionReq { 0 }, slicePlayReq { -1 };
    int auditionOn = 0;
    uint8_t waveFrame[kWaveBars] {};

    Plugin()
    {
        for (int i = 0; i < NPARAMS; ++i) { nv[i].store (toNorm (i, kDefs[i].def)); seen[i] = nv[i].load(); sent[i] = -1.f; }
        guardUntil = nowSeconds() + 2.5;
        mkdirs ((std::string (rootDir()) + "/Samples").c_str());
        mkdirs ((std::string (rootDir()) + "/Exports").c_str());
        setStatus ("Pick a WAV with the SAMPLE knob, then LOAD");
        workerOn = pthread_create (&worker, nullptr, workerMain, this) == 0;
    }
    ~Plugin()
    {
        quit = 1;
        if (workerOn) pthread_join (worker, nullptr);
        for (auto& r : retired) freeSample (r);
        freeSample (sample.load());
    }
    float get (int i) const { return toPlain (i, nv[i].load()); }
    void setNorm (int i, float n) { nv[i].store (clampf (n, 0.f, 1.f)); seen[i] = nv[i].load(); }
    void setPlain (int i, float v) { setNorm (i, toNorm (i, v)); }
    // ---------------------------------------------------------------------------------------- texts
    void setText (int which, const char* s) { if (text[which].publish (s)) { gen[which] = (gen[which] + 1) % 1000; nv[P_STATUS + which].store (gen[which] / 1000.f); } }
    void setStatus (const char* s) { setText (0, s); }
    const SliceTable& table() const { return tables[live.load()]; }
    // edit the slices: copy live -> idle, change, publish
    template <typename F> void editSlices (F f)
    {
        std::lock_guard<std::mutex> g (editLock);
        const int idle = 1 - live.load();
        tables[idle] = tables[live.load()];
        f (tables[idle]);
        live.store (idle);
        waveDirty = 1;
    }
    int selected() const { const int n = table().n; return n ? clampi ((int) std::lround (nv[P_SLICE].load() * (kMaxSlices - 1)), 0, n - 1) : -1; }
    void selectSlice (int k) { setNorm (P_SLICE, (float) k / (kMaxSlices - 1)); }
    void refreshTexts()
    {
        const Sample* s = sample.load();
        char b[128];
        if (s)
        {
            std::snprintf (b, sizeof b, "%s \xc2\xb7 %.1f s \xc2\xb7 %.0f Hz", s->name, s->frames / s->rate, s->fileRate);
            setText (1, b);
            {
                std::lock_guard<std::mutex> g (anLock);
                const int t = (int) get (P_TARGETKEY);
                if (t > 0 && an.key >= 0) std::snprintf (b, sizeof b, "%.1f BPM \xc2\xb7 %s \xe2\x86\x92 %+d", (double) get (P_BPM), sl::keyName (an.key), keyShift.load());
                else std::snprintf (b, sizeof b, "%.1f BPM \xc2\xb7 %s", (double) get (P_BPM), sl::keyName (an.key));
            }
            setText (2, b);
        }
        else { setText (1, "No sample loaded"); setText (2, "\xe2\x80\x94"); }
        const SliceTable& t = table(); const int k = selected();
        if (s && k >= 0)
        {
            const Slice& c = t.s[k];
            const int pad = k % 16 + 1; const char bank = (char) ('A' + k / 16);
            std::snprintf (b, sizeof b, "Slice %d of %d \xc2\xb7 pad %c%d \xc2\xb7 %.3f s%s%s", k + 1, t.n, bank, pad, (c.end - c.start) / s->rate,
                           c.rev ? " \xc2\xb7 rev" : "", c.loop ? " \xc2\xb7 loop" : "");
        }
        else std::snprintf (b, sizeof b, "%s", s ? "No slices: tap CHOP" : "");
        setText (3, b);
        const int base = (int) get (P_BASENOTE);
        char n0[24], n1[24]; noteName (base, n0, sizeof n0); noteName (base + 63, n1, sizeof n1);
        if ((int) get (P_PADMODE) == 0) std::snprintf (b, sizeof b, "Slices 1-64 on %s..%s \xc2\xb7 pad banks A-D", n0, n1);
        else std::snprintf (b, sizeof b, "Chromatic: slice %d, C3 (60) = original pitch", k + 1);
        setText (4, b);
    }
    // KEY SHIFT TO: the smallest shift (-5..+6 semitones) that puts the detected key on the target (a minor target for
    // a major sample, or the reverse, uses its relative key so the mode doesn't change)
    void updateKeyShift()
    {
        const int t = (int) get (P_TARGETKEY) - 1;
        int src;
        { std::lock_guard<std::mutex> g (anLock); src = an.key; }
        if (t < 0 || src < 0) { keyShift = 0; return; }
        int troot = t % 12; const bool tMinor = t >= 12, sMinor = src >= 12;
        if (tMinor && ! sMinor) troot = (troot + 3) % 12;                   // A minor target for a major sample -> C major
        if (! tMinor && sMinor) troot = (troot + 9) % 12;                   // C major target for a minor sample -> A minor
        int d = ((troot - src % 12) % 12 + 12) % 12;
        if (d > 6) d -= 12;
        keyShift = d;
    }
    // ---------------------------------------------------------------------------------------- waveform readout
    // 128 bars over the view (ZOOM: whole sample / start-end / selected slice). Heights are peaks read from the audio,
    // scaled to the loudest bar in view (at least -26 dB), so a quiet slice still shows its shape.
    // frame = state * 32 + height; state 0 outside start-end, 1 inside, 2 selected slice, 3 a slice starts here
    void computeWave()
    {
        const Sample* s = sample.load();
        const SliceTable& t = table();
        const int k = selected();
        long v0 = 0, v1 = s ? s->frames : 0;
        const int zoom = (int) get (P_ZOOM);
        if (s && zoom == 1) { v0 = (long) (get (P_START) * s->frames); v1 = std::max (v0 + kWaveBars, (long) (get (P_END) * s->frames)); }
        if (s && zoom == 2 && k >= 0) { v0 = t.s[k].start; v1 = std::max (v0 + kWaveBars, t.s[k].end); }
        if (s) v1 = std::min (v1, s->frames);
        int peaks[kWaveBars] {}; int vmax = 1;
        for (int b = 0; b < kWaveBars && s && v1 > v0; ++b)
        {
            const long a = v0 + (v1 - v0) * b / kWaveBars, e = std::max (a + 1, v0 + (v1 - v0) * (b + 1) / kWaveBars);
            const long step = std::max (1L, (e - a) / 2048);                 // long bars: sample every n-th frame
            int m = 0;
            for (long i = a; i < e; i += step) { m = std::max (m, std::abs ((int) s->pcm[2 * i])); m = std::max (m, std::abs ((int) s->pcm[2 * i + 1])); }
            peaks[b] = m; vmax = std::max (vmax, m);
        }
        vmax = std::max (vmax, 1650);                                         // -26 dBFS floor
        for (int b = 0; b < kWaveBars; ++b)
        {
            int h = 0, state = 0;
            if (s && v1 > v0)
            {
                const long a = v0 + (v1 - v0) * b / kWaveBars, e = std::max (a + 1, v0 + (v1 - v0) * (b + 1) / kWaveBars);
                h = std::min (31, (int) ((long long) peaks[b] * 31 / vmax));
                const double fs = (double) a / s->frames;
                state = fs >= get (P_START) - 1e-6 && fs < get (P_END) ? 1 : 0;
                for (int i = 0; i < t.n; ++i) if (t.s[i].start >= a && t.s[i].start < e) { state = 3; break; }
                if (k >= 0 && a < t.s[k].end && e > t.s[k].start) state = 2;  // the selected slice wins
            }
            waveFrame[b] = (uint8_t) (state * 32 + h);
        }
    }
    // ---------------------------------------------------------------------------------------- MIDI + voices
    double pitchRatioFor (const Slice& c, int noteOffset) const
    {
        const double st = get (P_PITCH) + get (P_FINE) / 100.0 + c.pitch + noteOffset + keyShift.load();
        return std::pow (2.0, st / 12.0);
    }
    double timeRatio() const
    {
        if ((int) get (P_SYNC) == 1 && get (P_BPM) > 0) return hostTempo / get (P_BPM);
        return (int) get (P_TMODE) == 1 ? get (P_SPEED) : 1.0;
    }
    Voice* freeVoice()
    {
        Voice* best = &v[0];
        for (auto& x : v) { if (! x.on) return &x; if (x.env < best->env) best = &x; }
        return best;
    }
    void startVoice (int tag, const Slice& c, int note, int vel, int noteOffset)
    {
        const Sample* s = sample.load();
        if (! s || c.end <= c.start) return;
        if ((int) get (P_POLY) == 1) for (auto& x : v) if (x.on) { x.releasing = true; x.releaseStep = 1.f / (float) (0.004 * sr); }
        if (c.choke > 0) for (auto& x : v) if (x.on && x.sl.choke == c.choke) { x.releasing = true; x.releaseStep = 1.f / (float) (0.004 * sr); }
        Voice& x = *freeVoice();
        x = Voice();
        x.on = true; x.note = note; x.tag = tag; x.sl = c; x.len = c.end - c.start; x.smp = s;
        x.pitchRatio = pitchRatioFor (c, noteOffset);
        x.timeRatio = timeRatio();
        x.stretch = (int) get (P_TMODE) == 1;
        x.rate = x.stretch ? x.pitchRatio : x.pitchRatio * x.timeRatio;     // Repitch: speed and pitch move together
        const float vs = get (P_VELSENS) / 100.f;
        const float vg = (1.f - vs) + vs * std::pow (vel / 127.f, 1.6f);
        x.gain = vg * dbToGain (c.vol) * dbToGain (get (P_GAIN));
        const float att = (c.att >= 0.f ? c.att : get (P_ATTACK)) / 1000.f, rel = (c.rel >= 0.f ? c.rel : get (P_RELEASE)) / 1000.f;
        x.attackStep = att > 0.0005f ? 1.f / (float) (att * sr) : 1.f / (float) (0.0005 * sr);
        x.releaseStep = 1.f / (float) (std::max (0.005f, rel) * sr);
        x.fmode = 0; x.ic1[0] = x.ic1[1] = x.ic2[0] = x.ic2[1] = 0.f;
        if (std::fabs (c.filter) > 0.02f)
        {
            const double fc = c.filter < 0 ? 20000.0 * std::pow (0.01, -c.filter) : 20.0 * std::pow (250.0, c.filter);
            const double g = std::tan (kPi * std::min (fc, 0.45 * sr) / sr);
            x.fmode = c.filter < 0 ? 1 : 2; x.fk = 1.4142f;
            x.fa1 = (float) (1.0 / (1.0 + g * (g + x.fk))); x.fa2 = (float) (g * x.fa1); x.fa3 = (float) (g * x.fa2);
        }
        x.env = 0.f;
        x.grains.reset();
    }
    void noteOn (int note, int vel)
    {
        const SliceTable& t = table();
        if ((int) get (P_PADMODE) == 1)
        {
            const int k = selected();
            if (k >= 0 && ! t.s[k].mute) startVoice (k, t.s[k], note, vel, note - 60);
            return;
        }
        const int k = note - (int) get (P_BASENOTE);
        if (k >= 0 && k < t.n && ! t.s[k].mute) startVoice (k, t.s[k], note, vel, 0);
    }
    void noteOff (int note)
    {
        if ((int) get (P_PLAYMODE) != 1) return;                          // one-shot: slices play to their end
        for (auto& x : v) if (x.on && x.note == note) x.releasing = true;
    }
    static float at (const Sample* s, long i, int ch) { return (i < 0 || i >= s->frames) ? 0.f : s->pcm[2 * i + ch] * (1.f / 32768.f); }
    static float cubic (const Sample* s, double p, int ch)
    {
        const long i = (long) std::floor (p); const float t = (float) (p - i);
        const float y0 = at (s, i - 1, ch), y1 = at (s, i, ch), y2 = at (s, i + 1, ch), y3 = at (s, i + 2, ch);
        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3, b = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c = -0.5f * y0 + 0.5f * y2;
        return ((a * t + b) * t + c) * t + y1;
    }
    // logical position -> sample position (reverse mirrors the slice)
    static double frameOf (const Voice& x, double p) { return x.sl.rev ? (double) x.sl.end - 1.0 - p : (double) x.sl.start + p; }
    // WSOLA: the new grain starts at anchor + d, with d (within +-5 ms) chosen so the grain lines up with where the
    // grain playing now continues (cross-correlation of the mid channel, every 2nd sample). Phase stays continuous,
    // so the pitch is exact and transients don't smear.
    double wsolaStart (const Voice& x, double grainLen) const
    {
        const Grain* cur = nullptr;
        for (const auto& g : x.grains.g) if (g.on && (! cur || g.age < cur->age)) cur = &g;
        if (! cur) return x.anchor;
        const double cont = cur->start + cur->age * x.pitchRatio;           // where the running grain is reading now
        const int maxShift = (int) (0.005 * sr), win = (int) (0.006 * sr);
        double best = -1e30, bestD = 0;
        for (int d = -maxShift; d <= maxShift; d += 2)
        {
            const double cand = x.anchor + d;
            if (cand < 0 || cand + win * x.pitchRatio >= x.len) continue;
            double c = 0;
            for (int i = 0; i < win; i += 4)
            {
                const double o = i * x.pitchRatio;
                const double fa = frameOf (x, cont + o), fb = frameOf (x, cand + o);
                c += (at (x.smp, (long) fa, 0) + at (x.smp, (long) fa, 1)) * (at (x.smp, (long) fb, 0) + at (x.smp, (long) fb, 1));
            }
            c -= 1e-6 * std::fabs ((double) d);                             // prefer the smallest shift on ties
            if (c > best) { best = c; bestD = d; }
        }
        (void) grainLen;
        return x.anchor + bestD;
    }
    void renderVoice (Voice& x, float* L, float* R, int n)
    {
        const Sample* s = x.smp;
        const double grainLen = 0.06 * sr;                                  // 60 ms grains, 50 % overlap
        const auto& hw = hann();
        for (int i = 0; i < n; ++i)
        {
            // envelope
            if (x.releasing) { x.env -= x.releaseStep; if (x.env <= 0.f) { x.on = false; return; } }
            else if (x.env < 1.f) x.env = std::min (1.f, x.env + x.attackStep);
            float l = 0.f, r = 0.f;
            if (! x.stretch)
            {
                if (x.pos >= x.len) { if (x.sl.loop) x.pos -= x.len; else { x.on = false; return; } }
                const double f = frameOf (x, x.pos);
                l = cubic (s, f, 0); r = cubic (s, f, 1);
                // 2 ms fade at a one-shot slice end (no click)
                const double left = (x.len - x.pos) / x.rate;
                if (! x.sl.loop && left < 0.002 * sr) { const float g = (float) (left / (0.002 * sr)); l *= g; r *= g; }
                x.pos += x.rate;
            }
            else
            {
                if (x.anchor >= x.len) { if (x.sl.loop) x.anchor -= x.len; else { x.on = false; return; } }
                if (x.grains.due (grainLen, 2)) x.grains.spawn (wsolaStart (x, grainLen));
                float wsum = 0.f;
                for (auto& g : x.grains.g)
                {
                    if (! g.on) continue;
                    const double ph = g.age / grainLen;
                    if (ph >= 1.0) { g.on = false; continue; }
                    const float w = hw.at (ph);
                    double p = g.start + g.age * x.pitchRatio;
                    if (x.sl.loop) { while (p >= x.len) p -= x.len; }
                    if (p < x.len) { const double f = frameOf (x, p); l += w * cubic (s, f, 0); r += w * cubic (s, f, 1); }
                    wsum += w;
                    g.age += 1.0;
                }
                if (wsum > 1e-3f) { l /= wsum; r /= wsum; }
                // the weighted average keeps the level; fade in over the first grain so it doesn't start with a jump
                const double left = (x.len - x.anchor) / x.timeRatio;
                if (! x.sl.loop && left < 0.002 * sr) { const float g = (float) (left / (0.002 * sr)); l *= g; r *= g; }
                x.anchor += x.timeRatio;
                x.grains.sinceSpawn += 1.0;
            }
            if (x.fmode)
            {
                float* io[2] { &l, &r };
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float in = *io[ch], v3 = in - x.ic2[ch];
                    const float v1 = x.fa1 * x.ic1[ch] + x.fa2 * v3, v2 = x.ic2[ch] + x.fa2 * x.ic1[ch] + x.fa3 * v3;
                    x.ic1[ch] = 2.f * v1 - x.ic1[ch]; x.ic2[ch] = 2.f * v2 - x.ic2[ch];
                    *io[ch] = x.fmode == 1 ? v2 : in - x.fk * v1 - v2;
                }
            }
            const float g = x.gain * x.env;
            L[i] += l * g; R[i] += r * g;
        }
    }
    void process (float* L, float* R, int n)
    {
        std::memset (L, 0, sizeof (float) * (size_t) n); std::memset (R, 0, sizeof (float) * (size_t) n);
        if (master)
        {
            const VstTimeInfo* ti = (const VstTimeInfo*) master (&fx, audioMasterGetTime, 0, kVstTempoValid, nullptr, 0.f);
            if (ti && (ti->flags & kVstTempoValid) && ti->tempo > 20) hostTempo = ti->tempo;
        }
        // audition requests from the screen
        const int ar = auditionReq.exchange (0);
        if (ar)
        {
            bool any = false;
            for (auto& x : v) if (x.on && x.tag == -1) { x.releasing = true; any = true; }
            const Sample* s = sample.load();
            if (! any && s)
            {
                Slice whole; whole.start = (long) (get (P_START) * s->frames); whole.end = (long) (get (P_END) * s->frames);
                startVoice (-1, whole, -1, 110, 0);
            }
        }
        const int sp = slicePlayReq.exchange (-1);
        if (sp >= 0 && sp < table().n) startVoice (sp, table().s[sp], -2, 110, 0);
        // MIDI, sample accurate
        int done = 0;
        for (int e = 0; e <= nev; ++e)
        {
            const int upto = e < nev ? clampi (evq[e].delta, done, n) : n;
            if (upto > done) for (auto& x : v) if (x.on) renderVoice (x, L + done, R + done, upto - done);
            done = upto;
            if (e < nev)
            {
                const int st = evq[e].status & 0xf0;
                if (st == 0x90 && evq[e].d2 > 0) noteOn (evq[e].d1, evq[e].d2);
                else if (st == 0x80 || (st == 0x90 && evq[e].d2 == 0)) noteOff (evq[e].d1);
                else if (st == 0xb0 && (evq[e].d1 == 120 || evq[e].d1 == 123)) for (auto& x : v) x.releasing = x.on;
            }
        }
        nev = 0;
        // safety: untouched below -1 dBFS (0.891), a soft knee above that never exceeds 0 dBFS
        auto knee = [] (float x) { const float a = std::fabs (x); if (a <= 0.891f) return x; const float y = 0.891f + 0.109f * std::tanh ((a - 0.891f) / 0.109f); return x < 0 ? -y : y; };
        for (int i = 0; i < n; ++i) { L[i] = knee (L[i]); R[i] = knee (R[i]); }
        // readouts to the screen (computed by the worker; only changes are sent)
        for (int i = P_STATUS; i < NPARAMS; ++i)
            if (sent[i] != nv[i].load()) { sent[i] = nv[i].load(); if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, sent[i]); }
    }
    // ---------------------------------------------------------------------------------------- UI-thread actions
    void press (int i)
    {
        if (nowSeconds() < guardUntil) return;
        switch (i)
        {
            case P_LOAD:
            {
                std::lock_guard<std::mutex> g (filesLock);
                if (nFiles == 0) { setStatus ("No WAV files found (SampleLab/Samples or a USB drive)"); return; }
                const int k = clampi ((int) std::lround (nv[P_FILE].load() * (nFiles - 1)), 0, nFiles - 1);
                { std::lock_guard<std::mutex> q (qLock); std::snprintf (loadPath, sizeof loadPath, "%s", files[(size_t) k].c_str()); }
                reqLoad = 1; setStatus ("Loading\xe2\x80\xa6");
                return;
            }
            case P_AUDITION: auditionReq = 1; return;
            case P_CHOP: if (sample.load()) { variation = 0; reqChop = 1; setStatus ("Chopping\xe2\x80\xa6"); } return;
            case P_SLPLAY: if (selected() >= 0) slicePlayReq = selected(); return;
            case P_BPMHALF: case P_BPMDOUBLE:
            {
                const float b = get (P_BPM) * (i == P_BPMHALF ? 0.5f : 2.f);
                if (b < 50.f || b > 200.f) { setStatus ("BPM would leave the 50-200 range"); return; }
                setPlain (P_BPM, b); sent[P_BPM] = -1.f; waveDirty = 1;
                char m[64]; std::snprintf (m, sizeof m, "BPM set to %.1f", (double) b); setStatus (m);
                return;
            }
            case P_FIND: if (sample.load()) { ++variation; reqChop = 1; setStatus ("Finding another chop\xe2\x80\xa6"); } return;
            case P_LATEST:
            {
                char lp[600]; std::snprintf (lp, sizeof lp, "%s/Samples/Captures/latest.txt", rootDir());
                FILE* f = std::fopen (lp, "r");
                char path[512] = "";
                if (f) { if (std::fgets (path, sizeof path, f)) path[std::strcspn (path, "\r\n")] = 0; std::fclose (f); }
                if (! path[0]) { setStatus ("No capture yet: record with Da Sample Lab Capture"); return; }
                { std::lock_guard<std::mutex> q (qLock); std::snprintf (loadPath, sizeof loadPath, "%s", path); }
                reqLoad = 1; setStatus ("Loading the latest capture\xe2\x80\xa6");
                return;
            }
            case P_SLLEFT: case P_SLRIGHT:
            {
                const int k = selected(), to = k + (i == P_SLLEFT ? -1 : 1);
                if (k < 0 || to < 0 || to >= table().n) return;
                editSlices ([&] (SliceTable& t) { std::swap (t.s[k], t.s[to]); });
                selectSlice (to); setStatus (i == P_SLLEFT ? "Slice moved one pad down" : "Slice moved one pad up");
                return;
            }
            case P_SPLIT:
            {
                const int k = selected();
                if (k < 0 || table().n >= kMaxSlices) return;
                editSlices ([&] (SliceTable& t) {
                    const Slice c = t.s[k];
                    if (c.end - c.start < 2 * (long) (0.02 * sr)) return;
                    long mid = (c.start + c.end) / 2;
                    { std::lock_guard<std::mutex> g (anLock);                  // split at the strongest transient inside
                      auto tr = sl::transients (an, c.start + (long) (0.02 * sr), c.end - (long) (0.02 * sr), 1, 0.9f, sr, sample.load()->pcm, sample.load()->frames);
                      if (! tr.empty()) mid = tr[0]; }
                    for (int j = t.n; j > k + 1; --j) t.s[j] = t.s[j - 1];
                    t.s[k].end = mid; t.s[k + 1] = c; t.s[k + 1].start = mid; ++t.n; });
                setStatus ("Slice split in two"); return;
            }
            case P_MERGE:
            {
                const int k = selected();
                if (k < 0 || k + 1 >= table().n) return;
                editSlices ([&] (SliceTable& t) {
                    t.s[k].start = std::min (t.s[k].start, t.s[k + 1].start); t.s[k].end = std::max (t.s[k].end, t.s[k + 1].end);
                    for (int j = k + 1; j + 1 < t.n; ++j) t.s[j] = t.s[j + 1];
                    --t.n; });
                setStatus ("Merged with the next slice"); return;
            }
            case P_EXPORT: case P_EXPORTALL:
                if (table().n == 0) { setStatus ("Nothing to export: chop first"); return; }
                exportWhich = i == P_EXPORT ? selected() : -1; reqExport = 1; setStatus ("Exporting\xe2\x80\xa6");
                return;
            default: return;
        }
    }
    // a slice field changed from the screen / a Q-Link: write it into the selected slice
    void sliceParam (int i)
    {
        const int k = selected();
        const Sample* s = sample.load();
        if (k < 0 || ! s) return;
        editSlices ([&] (SliceTable& t) {
            Slice& c = t.s[k];
            switch (i)
            {
                case P_SLSTART: c.start = std::max (0L, std::min ((long) (get (P_SLSTART) * s->frames), c.end - 64)); break;
                case P_SLEND: c.end = std::max ((long) (get (P_SLEND) * s->frames), c.start + 64); if (c.end > s->frames) c.end = s->frames; break;
                case P_SLVOL: c.vol = get (P_SLVOL); break;
                case P_SLREV: c.rev = get (P_SLREV) > 0.5f; break;
                case P_SLLOOP: c.loop = get (P_SLLOOP) > 0.5f; break;
                case P_SLPITCH: c.pitch = std::round (get (P_SLPITCH)); break;
                case P_SLFILTER: c.filter = std::fabs (get (P_SLFILTER)) < 0.02f ? 0.f : get (P_SLFILTER); break;
                case P_SLATTACK: c.att = get (P_SLATTACK) < 0.5f ? -1.f : std::round (get (P_SLATTACK) - 1.f); break;
                case P_SLRELEASE: c.rel = get (P_SLRELEASE) < 0.5f ? -1.f : std::round (get (P_SLRELEASE) - 1.f); break;
                case P_SLMUTE: c.mute = get (P_SLMUTE) > 0.5f; break;
                case P_SLCHOKE: c.choke = clampi ((int) get (P_SLCHOKE), 0, 4); break;
                default: break;
            } });
    }
    // show the selected slice's values on its controls
    void showSlice()
    {
        const int k = selected(); const Sample* s = sample.load();
        if (k < 0 || ! s) return;
        const Slice& c = table().s[k];
        setNorm (P_SLSTART, (float) c.start / (float) s->frames); setNorm (P_SLEND, (float) c.end / (float) s->frames);
        setPlain (P_SLVOL, c.vol); setPlain (P_SLREV, c.rev ? 1.f : 0.f); setPlain (P_SLLOOP, c.loop ? 1.f : 0.f); setPlain (P_SLPITCH, c.pitch);
        setPlain (P_SLFILTER, c.filter); setPlain (P_SLATTACK, c.att < 0 ? 0.f : c.att + 1.f); setPlain (P_SLRELEASE, c.rel < 0 ? 0.f : c.rel + 1.f);
        setPlain (P_SLMUTE, c.mute ? 1.f : 0.f); setPlain (P_SLCHOKE, (float) c.choke);
        for (int i : { P_SLSTART, P_SLEND, P_SLVOL, P_SLREV, P_SLLOOP, P_SLPITCH, P_SLFILTER, P_SLATTACK, P_SLRELEASE, P_SLMUTE, P_SLCHOKE }) sent[i] = -1.f;
    }
    void poll()                                // called from setParameter and the idle / process path (UI thread)
    {
        for (int i = 0; i < P_STATUS; ++i)
        {
            const float val = nv[i].load();
            if (val == seen[i]) continue;
            seen[i] = val;
            const Kind k = kDefs[i].kind;
            if (k == K_BUTTON) { press (i); continue; }
            if (i == P_SLICE) { showSlice(); waveDirty = 1; continue; }
            if ((i >= P_SLSTART && i <= P_SLPITCH) || (i >= P_SLFILTER && i <= P_SLCHOKE)) { sliceParam (i); continue; }
            if (i == P_TARGETKEY) { updateKeyShift(); waveDirty = 1; continue; }
            if (i == P_SOURCE) { reqScan = 1; setNorm (P_FILE, 0.f); sent[P_FILE] = -1.f; continue; }
            waveDirty = 1;                       // start / end / bpm / pad mode ... change the readouts
        }
    }
    // ---------------------------------------------------------------------------------------- state (chunk)
    std::string chunkText;
    const std::string& saveState()
    {
        char b[512];
        chunkText = "dasamplelab 1\n";
        std::snprintf (b, sizeof b, "path %s\n", samplePath); chunkText += b;
        for (int i = 0; i < P_STATUS; ++i)
            if (kDefs[i].kind != K_BUTTON) { std::snprintf (b, sizeof b, "p %s %.6f\n", kDefs[i].key, (double) nv[i].load()); chunkText += b; }
        const SliceTable& t = table();
        for (int k = 0; k < t.n; ++k)
        {
            const Slice& c = t.s[k];
            std::snprintf (b, sizeof b, "s %ld %ld %.3f %d %d %.1f %.4f %.1f %.1f %d %d\n", c.start, c.end, (double) c.vol, c.rev ? 1 : 0, c.loop ? 1 : 0,
                           (double) c.pitch, (double) c.filter, (double) c.att, (double) c.rel, c.mute ? 1 : 0, c.choke);
            chunkText += b;
        }
        std::snprintf (b, sizeof b, "rate %.1f\n", sample.load() ? sample.load()->rate : sr); chunkText += b;
        return chunkText;
    }
    SliceTable pendingSlices; bool havePending = false; double pendingRate = 0;
    void loadState (const char* text, size_t len)
    {
        std::string s (text, len);
        SliceTable t;
        double rate = 0;
        size_t pos = 0;
        char path[512] = "";
        while (pos < s.size())
        {
            size_t e = s.find ('\n', pos); if (e == std::string::npos) e = s.size();
            std::string line = s.substr (pos, e - pos); pos = e + 1;
            if (! line.compare (0, 5, "path ")) std::snprintf (path, sizeof path, "%s", line.c_str() + 5);
            else if (! line.compare (0, 2, "p "))
            {
                const size_t sp = line.find (' ', 2);
                if (sp == std::string::npos) continue;
                const std::string key = line.substr (2, sp - 2);
                const float val = (float) std::strtod (line.c_str() + sp + 1, nullptr);
                for (int i = 0; i < P_STATUS; ++i) if (key == kDefs[i].key) setNorm (i, val);
            }
            else if (! line.compare (0, 2, "s ") && t.n < kMaxSlices)
            {
                char* p = (char*) line.c_str() + 2;
                Slice& c = t.s[t.n];
                c.start = (long) std::strtod (p, &p); c.end = (long) std::strtod (p, &p); c.vol = (float) std::strtod (p, &p);
                c.rev = std::strtod (p, &p) > 0.5; c.loop = std::strtod (p, &p) > 0.5; c.pitch = (float) std::strtod (p, &p);
                // fields added in 1.1 (older projects end here: defaults stay)
                char* q = p; const double f = std::strtod (p, &q);
                if (q != p) { c.filter = (float) f; p = q; c.att = (float) std::strtod (p, &p); c.rel = (float) std::strtod (p, &p);
                              c.mute = std::strtod (p, &p) > 0.5; c.choke = clampi ((int) std::strtod (p, &p), 0, 4); }
                if (c.end > c.start) ++t.n;
            }
            else if (! line.compare (0, 5, "rate ")) rate = std::strtod (line.c_str() + 5, nullptr);
        }
        guardUntil = nowSeconds() + 2.5;
        { std::lock_guard<std::mutex> g (qLock); pendingSlices = t; havePending = true; pendingRate = rate; std::snprintf (loadPath, sizeof loadPath, "%s", path); }
        if (path[0]) { reqLoad = 2; setStatus ("Loading the project's sample\xe2\x80\xa6"); }
        waveDirty = 1;
    }
    // ---------------------------------------------------------------------------------------- worker jobs
    // the MPC's internal drive (where MPC saves projects and recorded samples); SAMPLELAB_INTERNAL overrides (tests)
    static const char* internalDir() { static char d[300]; if (! d[0]) std::snprintf (d, sizeof d, "%s", std::getenv ("SAMPLELAB_INTERNAL") ? std::getenv ("SAMPLELAB_INTERNAL") : "/media/az01-internal"); return d; }
    struct Found { std::string path; long mtime; };
    void scanDir (const char* dir, int depth, int maxDepth, std::vector<Found>& out, size_t cap)
    {
        if (depth > maxDepth || out.size() >= cap) return;
        DIR* d = opendir (dir);
        if (! d) return;
        struct dirent* e;
        std::vector<std::string> sub;
        while ((e = readdir (d)) != nullptr && out.size() < cap)
        {
            if (e->d_name[0] == '.') continue;
            char p[600]; std::snprintf (p, sizeof p, "%s/%s", dir, e->d_name);
            struct stat st;
            if (stat (p, &st) != 0) continue;
            if (S_ISDIR (st.st_mode)) { if (std::strcmp (e->d_name, "Exports") && std::strcmp (e->d_name, "Settings")) sub.push_back (p); continue; }
            const size_t l = std::strlen (e->d_name);
            if (l > 4 && ! strcasecmp (e->d_name + l - 4, ".wav")) out.push_back ({ p, (long) st.st_mtime });
        }
        closedir (d);
        for (auto& x : sub) scanDir (x.c_str(), depth + 1, maxDepth, out, cap);
    }
    // BROWSE: SampleLab (our folder, incl. captures) / Recent recordings (the 128 newest WAVs on the internal drive,
    // e.g. what you just sampled in MPC) / Internal drive (all, by name) / USB drive
    void scan()
    {
        std::vector<Found> f;
        const int src = (int) get (P_SOURCE);
        if (src == 0) { scanDir ((std::string (rootDir()) + "/Samples").c_str(), 0, 3, f, kMaxFiles); if (const char* x = std::getenv ("SAMPLELAB_EXTRA")) scanDir (x, 0, 3, f, kMaxFiles); }
        else if (src == 1 || src == 2)
        {
            scanDir (internalDir(), 0, 6, f, 8000);
            if (std::strncmp (rootDir(), internalDir(), std::strlen (internalDir()))) scanDir ((std::string (rootDir()) + "/Samples").c_str(), 0, 3, f, 8000);
        }
        else
        {
            DIR* d = opendir ("/media");
            struct dirent* e;
            while (d && (e = readdir (d)) != nullptr)
            {
                if (e->d_name[0] == '.' || std::strstr (e->d_name, "az01-internal")) continue;
                char p[300]; std::snprintf (p, sizeof p, "/media/%s", e->d_name);
                scanDir (p, 0, 4, f, kMaxFiles);
            }
            if (d) closedir (d);
        }
        if (src == 1)
        {
            std::sort (f.begin(), f.end(), [] (const Found& x, const Found& y) { return x.mtime > y.mtime; });
            if (f.size() > 128) f.resize (128);
        }
        else std::sort (f.begin(), f.end(), [] (const Found& x, const Found& y) { return strcasecmp (std::strrchr (x.path.c_str(), '/') + 1, std::strrchr (y.path.c_str(), '/') + 1) < 0; });
        if (f.size() > (size_t) kMaxFiles) f.resize (kMaxFiles);
        std::lock_guard<std::mutex> g (filesLock);
        files.clear();
        for (auto& x : f) files.push_back (x.path);
        nFiles = (int) files.size();
    }
    const char* fileLabel (int k) const { return k >= 0 && k < nFiles ? std::strrchr (files[(size_t) k].c_str(), '/') + 1 : "(no WAV files)"; }
    // copy a file from USB onto internal storage so the project still opens when the drive is gone
    bool importToInternal (const char* src, char* dst, size_t n)
    {
        const std::string samples = std::string (rootDir()) + "/Samples/";
        // already on internal storage (our folder, or the MPC's own drive): use it where it is
        if (! std::strncmp (src, rootDir(), std::strlen (rootDir())) || ! std::strncmp (src, internalDir(), std::strlen (internalDir())) || ! std::strncmp (src, "/sdcard/", 8))
        { std::snprintf (dst, n, "%s", src); return true; }
        std::snprintf (dst, n, "%s%s", samples.c_str(), std::strrchr (src, '/') + 1);
        struct stat a, b;
        if (stat (dst, &b) == 0 && stat (src, &a) == 0 && a.st_size == b.st_size) return true;   // already imported
        FILE* in = std::fopen (src, "rb"); if (! in) return false;
        const std::string part = std::string (dst) + ".part";
        FILE* out = std::fopen (part.c_str(), "wb"); if (! out) { std::fclose (in); return false; }
        std::vector<char> buf (1 << 16); size_t r; bool ok = true;
        while ((r = std::fread (buf.data(), 1, buf.size(), in)) > 0) if (std::fwrite (buf.data(), 1, r, out) != r) { ok = false; break; }
        std::fclose (in); ok = std::fclose (out) == 0 && ok;
        if (! ok) { std::remove (part.c_str()); return false; }
        return std::rename (part.c_str(), dst) == 0;
    }
    void doLoad (int mode)
    {
        char src[512], dst[512];
        { std::lock_guard<std::mutex> g (qLock); std::snprintf (src, sizeof src, "%s", loadPath); }
        if (! importToInternal (src, dst, sizeof dst)) { setStatus ("Couldn't copy the file to internal storage (drive full?)"); return; }
        const char* err = nullptr;
        Sample* s = loadSample (dst, dst, sr, kMaxSeconds, &err);
        if (! s) { char b[160]; std::snprintf (b, sizeof b, "Can't load: %s", err ? err : "error"); setStatus (b); return; }
        sl::Analysis A;
        sl::analyse (s->pcm, s->frames, s->rate, A);
        { std::lock_guard<std::mutex> g (anLock); an = std::move (A); }
        updateKeyShift();
        // publish the sample: voices keep their own pointer until they end; free the old one later
        for (auto& x : v) x.releasing = x.on;                             // fade out what was playing
        Sample* old = sample.exchange (s);
        if (old) for (int i = 0; i < 8; ++i) if (! retired[i]) { retired[i] = old; retiredAt[i] = nowSeconds(); break; }
        std::snprintf (samplePath, sizeof samplePath, "%s", dst);
        sampleGen++;
        bool restored = false;
        {
            std::lock_guard<std::mutex> g (qLock);
            if (mode == 2 && havePending)                                 // project load: keep its slices and settings
            {
                const double k = pendingRate > 0 ? s->rate / pendingRate : 1.0;
                SliceTable t = pendingSlices;
                for (int i = 0; i < t.n; ++i) { t.s[i].start = std::min (s->frames, (long) (t.s[i].start * k)); t.s[i].end = std::min (s->frames, (long) (t.s[i].end * k)); }
                editSlices ([&] (SliceTable& x) { x = t; });
                restored = true;
            }
            havePending = false;
        }
        if (! restored)
        {
            setNorm (P_START, 0.f); setNorm (P_END, 1.f);
            if (an.bpm > 0) setPlain (P_BPM, (float) an.bpm);
            doChop();
        }
        char b[160];
        if (an.bpm > 0) std::snprintf (b, sizeof b, "Loaded %s \xc2\xb7 %.1f BPM \xc2\xb7 %s \xc2\xb7 %d slices", s->name, an.bpm, sl::keyName (an.key), table().n);
        else std::snprintf (b, sizeof b, "Loaded %s \xc2\xb7 no beat \xc2\xb7 %s \xc2\xb7 %d slices", s->name, sl::keyName (an.key), table().n);
        setStatus (b);
        showSlice();
        waveDirty = 1;
    }
    void doChop()
    {
        const Sample* s = sample.load();
        if (! s) return;
        const long a = (long) (get (P_START) * s->frames), b = std::max (a + 64, (long) (get (P_END) * s->frames));
        const int n = kCountVals[clampi ((int) get (P_COUNT), 0, 7)];
        const int mode = (int) get (P_CHOPMODE);
        const double bpm = get (P_BPM);
        std::vector<long> starts;
        {
            std::lock_guard<std::mutex> g (anLock);
            const int var = variation;
            Rng rng; rng.seed (0x9E3779B9u * (uint32_t) (var + 1));
            if (mode == 0)                                                // the sample start is slice 1 unless a hit is right there
            {
                if (var == 0)
                {
                    starts = sl::transients (an, a, b, n, get (P_SENS) / 100.f, s->rate, s->pcm, s->frames);
                    if (starts.empty() || starts[0] - a > (long) (0.03 * s->rate))
                    { starts = sl::transients (an, a, b, n - 1, get (P_SENS) / 100.f, s->rate, s->pcm, s->frames); starts.insert (starts.begin(), a); }
                }
                else
                {
                    // FIND: another pick among the 3x strongest candidates, weighted by strength (same variation = same result)
                    std::vector<long> pool = sl::transients (an, a, b, 3 * n, std::min (1.f, get (P_SENS) / 100.f + 0.2f), s->rate, s->pcm, s->frames);
                    std::vector<std::pair<double, long>> w;
                    for (size_t i = 0; i < pool.size(); ++i) w.push_back ({ (double) rng.uni(), pool[i] });   // pool = the strongest hits
                    std::sort (w.begin(), w.end(), [] (const std::pair<double, long>& x, const std::pair<double, long>& y) { return x.first > y.first; });
                    for (int i = 0; i < (int) w.size() && (int) starts.size() < n - 1; ++i) starts.push_back (w[(size_t) i].second);
                    std::sort (starts.begin(), starts.end());
                    if (starts.empty() || starts[0] - a > (long) (0.03 * s->rate)) starts.insert (starts.begin(), a);
                }
            }
            else if (mode == 1 || mode == 2)                               // beat / bar grid from the first strong beat
            {
                const double step = 60.0 / std::max (40.0, bpm) * s->rate * (mode == 2 ? 4 : 1);
                double t0 = (double) (mode == 2 ? an.firstBeat : an.beatAnchor);
                while (t0 - step >= a) t0 -= step;
                while (t0 < a) t0 += step;
                t0 += var * step;                                         // FIND: start the grid one beat / bar later
                if (t0 - a > 0.03 * s->rate) starts.push_back (a);
                for (double t = t0; t < b && (int) starts.size() < n; t += step) starts.push_back ((long) t);
            }
            else if (mode == 3) starts = sl::sections (an, a, b, n, bpm, s->rate);
            else
            {
                const long off = var ? (long) ((b - a) / n * (var % 4) / 4) : 0;      // FIND: shift the grid by a quarter slice
                if (off) starts.push_back (a);
                for (int i = 0; (int) starts.size() < n; ++i) { const long t = a + off + (b - a) * i / n; if (t >= b) break; starts.push_back (t); }
            }
        }
        if ((int) starts.size() > kMaxSlices) starts.resize (kMaxSlices);
        editSlices ([&] (SliceTable& t) {
            t.n = (int) starts.size();
            for (int i = 0; i < t.n; ++i) { t.s[i] = Slice(); t.s[i].start = starts[(size_t) i]; t.s[i].end = i + 1 < t.n ? starts[(size_t) i + 1] : b; } });
        selectSlice (0); showSlice();
        char m[96];
        if (variation) std::snprintf (m, sizeof m, "%d slices (%s) \xc2\xb7 variation %d", table().n, kChop[mode], variation);
        else std::snprintf (m, sizeof m, "%d slices (%s) on pads %s", table().n, kChop[mode], table().n > 16 ? "A1-D16: use pad banks" : "A1-A16");
        setStatus (m);
    }
    void doExport()
    {
        const Sample* s = sample.load();
        if (! s || ! samplePath[0]) return;
        const SliceTable t = table();
        const int which = exportWhich;
        char dir[600]; std::snprintf (dir, sizeof dir, "%s/Exports/%s", rootDir(), s->name);
        mkdirs (dir);
        int ok = 0, total = 0, bitsOut = 16; double rateOut = s->fileRate;
        for (int k = 0; k < t.n; ++k)
        {
            if (which >= 0 && k != which) continue;
            ++total;
            const Slice& c = t.s[k];
            const double toOrig = s->fileRate / s->rate;
            sl::Original o;
            if (! sl::readOriginal (samplePath, (long) std::floor (c.start * toOrig), (long) std::ceil ((c.end - c.start) * toOrig), o)) continue;
            if (c.rev) { std::reverse (o.l.begin(), o.l.end()); std::reverse (o.r.begin(), o.r.end()); }
            const float g = dbToGain (c.vol);
            if (g != 1.f) for (size_t i = 0; i < o.l.size(); ++i) { o.l[i] *= g; o.r[i] *= g; }
            bitsOut = o.bits <= 16 ? 16 : 24; rateOut = o.rate;
            char f[800]; std::snprintf (f, sizeof f, "%s/%s_%02d.wav", dir, s->name, k + 1);
            if (sl::writeWav (f, o.l, o.r, (int) o.rate, bitsOut)) ++ok;
        }
        char m[200];
        std::snprintf (m, sizeof m, "Exported %d slice%s (%d-bit %.0f Hz) to Exports/%s", ok, ok == 1 ? "" : "s", bitsOut, rateOut, s->name);
        if (ok != total) std::snprintf (m, sizeof m, "Exported %d of %d slices (drive full?)", ok, total);
        setStatus (m);
    }
    // MPC changed the sample rate: reload the sample at the new rate, keeping the slices
    void rateChanged (double rate)
    {
        sr = rate;
        if (! samplePath[0]) return;
        std::lock_guard<std::mutex> g (qLock);
        std::snprintf (loadPath, sizeof loadPath, "%s", samplePath);
        pendingSlices = table(); pendingRate = sample.load() ? sample.load()->rate : 0; havePending = true;
        reqLoad = 2;
    }
    void workerLoop()
    {
        while (! quit)
        {
            if (reqScan.exchange (0)) { scan(); waveDirty = 1; }
            {
                char lp[600]; std::snprintf (lp, sizeof lp, "%s/Samples/Captures/latest.txt", rootDir());
                struct stat st;
                const long m = stat (lp, &st) == 0 ? (long) st.st_mtime * 1000 + st.st_mtim.tv_nsec / 1000000 : 0;
                if (m && m != latestSeen) { if (latestSeen) { setStatus ("New capture ready: tap LATEST"); reqScan = 1; } latestSeen = m; }
            }
            if (const int m = reqLoad.exchange (0)) doLoad (m);
            if (reqChop.exchange (0)) doChop();
            if (reqExport.exchange (0)) doExport();
            if (waveDirty.exchange (0))
            {
                computeWave();
                for (int b = 0; b < kWaveBars; ++b) nv[P_WAVE0 + b].store (waveFrame[b] / 127.f);
                refreshTexts();
            }
            const double now = nowSeconds();
            for (int i = 0; i < 8; ++i) if (retired[i] && now - retiredAt[i] > 3.0) { freeSample (retired[i]); retired[i] = nullptr; }
            usleep (20000);
        }
    }
    void display (int i, char* out, size_t max)
    {
        const Def& d = kDefs[i];
        const float p = get (i);
        switch (i)
        {
            case P_FILE: { std::lock_guard<std::mutex> g (filesLock); std::snprintf (out, max, "%s", fileLabel (clampi ((int) std::lround (nv[P_FILE].load() * (nFiles - 1)), 0, std::max (0, nFiles - 1)))); return; }
            case P_SLICE: { const int k = selected(); if (k < 0) std::snprintf (out, max, "-"); else std::snprintf (out, max, "%d / %d (%c%d)", k + 1, table().n, 'A' + k / 16, k % 16 + 1); return; }
            case P_START: case P_END: case P_SLSTART: case P_SLEND:
            {
                const Sample* s = sample.load();
                std::snprintf (out, max, "%.3f s", s ? p * s->frames / s->rate : 0.0); return;
            }
            case P_BPM: std::snprintf (out, max, "%.1f", (double) p); return;
            case P_PITCH: case P_SLPITCH: std::snprintf (out, max, "%+d st", (int) std::lround (p)); return;
            case P_FINE: std::snprintf (out, max, "%+d ct", (int) std::lround (p)); return;
            case P_SPEED: std::snprintf (out, max, "%.2fx", (double) p); return;
            case P_GAIN: case P_SLVOL: std::snprintf (out, max, "%+.1f dB", (double) p); return;
            case P_SENS: case P_VELSENS: std::snprintf (out, max, "%d %%", (int) std::lround (p)); return;
            case P_BASENOTE: noteName ((int) std::lround (p), out, max); return;
            case P_SLFILTER:
            {
                if (std::fabs (p) < 0.02f) { std::snprintf (out, max, "Off"); return; }
                const double fc = p < 0 ? 20000.0 * std::pow (0.01, -p) : 20.0 * std::pow (250.0, p);
                if (fc >= 1000) std::snprintf (out, max, "%s %.1f kHz", p < 0 ? "LP" : "HP", fc / 1000); else std::snprintf (out, max, "%s %d Hz", p < 0 ? "LP" : "HP", (int) fc);
                return;
            }
            case P_SLATTACK: case P_SLRELEASE: if (p < 0.5f) std::snprintf (out, max, "Global"); else std::snprintf (out, max, "%d ms", (int) std::lround (p - 1)); return;
            case P_ATTACK: case P_RELEASE: std::snprintf (out, max, "%d ms", (int) std::lround (p)); return;
            default: break;
        }
        if (d.kind == K_CHOICE || d.kind == K_TOGGLE) { std::snprintf (out, max, "%s", d.names[clampi ((int) p, 0, d.n - 1)]); return; }
        if (d.kind == K_BUTTON) { std::snprintf (out, max, "%s", d.name); return; }
        if (d.kind == K_TEXT) { std::snprintf (out, max, "%s", text[i - P_STATUS].get()); return; }
        if (d.kind == K_WAVE) { std::snprintf (out, max, "%d", (int) std::lround (nv[i].load() * 127)); return; }
        std::snprintf (out, max, "%.2f", (double) p);
    }
};
void* workerMain (void* p) { static_cast<Plugin*> (p)->workerLoop(); return nullptr; }

void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    (void) in;
    static_cast<Plugin*> (e->object)->process (out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    (void) in;
    float a[512], b[512];
    for (int32_t s = 0; s < n; s += 512)
    {
        const int m = n - s < 512 ? n - s : 512;
        float* o[2] { a, b };
        processReplacing (e, nullptr, o, m);
        for (int i = 0; i < m; ++i) { out[0][s + i] += a[i]; out[1][s + i] += b[i]; }
    }
}
void setParameter (AEffect* e, int32_t i, float v)
{
    if (i < 0 || i >= P_STATUS) return;                                 // readouts belong to the plugin
    Plugin* p = static_cast<Plugin*> (e->object);
    p->nv[i].store (clampf (v, 0.f, 1.f));
    p->poll();
}
float getParameter (AEffect* e, int32_t i) { return (i >= 0 && i < NPARAMS) ? static_cast<Plugin*> (e->object)->nv[i].load() : 0.f; }

intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose: p->~Plugin(); std::free (p); return 0;
        case effGetParamName: if (idx >= 0 && idx < NPARAMS) copyStr (ptr, kDefs[idx].name, 28); return 0;
        case effGetParamLabel: if (ptr) ((char*) ptr)[0] = 0; return 0;
        case effGetParamDisplay: if (idx >= 0 && idx < NPARAMS && ptr) { char b[128]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); } return 0;
        case effCanBeAutomated: return (idx >= 0 && idx < P_STATUS && (kDefs[idx].kind == K_FLOAT || kDefs[idx].kind == K_CHOICE)) ? 1 : 0;
        case effSetSampleRate: if (opt > 1000.f && std::fabs (opt - p->sr) > 0.5) p->rateChanged (opt); return 0;
        case effMainsChanged: if (val) p->reqScan = 1; return 0;
        case effProcessEvents:
        {
            const VstEvents* ev = (const VstEvents*) ptr;
            if (! ev) return 1;
            for (int i = 0; i < ev->numEvents && p->nev < 256; ++i)
            {
                const VstEvent* x = ev->events[i];
                if (! x || x->type != kVstMidiType) continue;
                const VstMidiEvent* m = (const VstMidiEvent*) x;
                p->evq[p->nev++] = { m->deltaFrames, (uint8_t) m->midiData[0], (uint8_t) m->midiData[1] & 0x7f, (uint8_t) m->midiData[2] & 0x7f };
            }
            // keep sample order (hosts usually send them sorted; make sure)
            std::stable_sort (p->evq, p->evq + p->nev, [] (const Plugin::Ev& a, const Plugin::Ev& b) { return a.delta < b.delta; });
            return 1;
        }
        case effGetChunk: { const std::string& s = p->saveState(); *(void**) ptr = (void*) s.c_str(); return (intptr_t) s.size(); }
        case effSetChunk: if (ptr && val > 0) p->loadState ((const char*) ptr, (size_t) val); return 1;
        case effGetProgram: return 0;
        case effGetProgramName: case effGetProgramNameIndexed: copyStr (ptr, "Da Sample Lab", 24); return 1;
        case effGetEffectName: case effGetProductString: copyStr (ptr, "Da Sample Lab", 32); return 1;
        case effGetVendorString: copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory: return kPlugCategSynth;
        case effGetVstVersion: return 2400;
        case effGetTailSize: return 0;
        case effCanDo:
        {
            const char* s = (const char*) ptr;
            if (! s) return 0;
            if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent") || ! std::strcmp (s, "receiveVstTimeInfo")) return 1;
            return -1;
        }
        default: return 0;
    }
}
} // namespace

SL_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    static bool init = false;
    if (! init) { initDefs(); init = true; }
    void* mem = std::calloc (1, sizeof (Plugin));
    if (! mem) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic; fx.dispatcher = dispatcher; fx.process = processAccumulating; fx.processReplacing = processReplacing;
    fx.setParameter = setParameter; fx.getParameter = getParameter;
    fx.numPrograms = 1; fx.numParams = NPARAMS; fx.numInputs = 0; fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing | effFlagsIsSynth | effFlagsProgramChunks;
    fx.ioRatio = 1.f; fx.object = p;
    fx.uniqueID = ('D' << 24) | ('S' << 16) | ('L' << 8) | 'B';           // 'DSLB'
    fx.version = kVersion;
    return &fx;
}
SL_EXPORT const char* SL_ParamKey (int i) { if (kDefs[0].key == nullptr) initDefs(); return (i >= 0 && i < NPARAMS) ? kDefs[i].key : ""; }
SL_EXPORT int SL_ParamCount() { return NPARAMS; }
