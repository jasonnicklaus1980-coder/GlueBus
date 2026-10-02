// Da Maze Muncher: an original maze-chase game as a native MPC OS VST2 instrument.
//
// How it runs inside MPC: MPC's plugin screens are skins whose controls show plugin parameters. The game state is
// published as parameters: one per maze cell (its value picks the cell's picture from a filmstrip), plus score,
// high score, lives, level and a message line. When something on screen changes, the plugin tells MPC about that
// parameter (audioMasterAutomate) and the skin redraws it. The pads of the plugin track arrive as MIDI notes and
// steer the Muncher; the skin's D-pad and START keys work too.
//
// Pads (MIDI notes, any bank: the note is taken modulo 16 from C1 = 36):
//   pad 10 up   pad 5 left   pad 6 down   pad 7 right   pad 13 or 16 start / pause
#include "vst2.h"
#include "Game.h"
#include "Synth.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#define MM_EXPORT extern "C" __attribute__ ((visibility ("default")))

namespace
{
using namespace mm;
constexpr int kVersion = 1000;
enum { P_DIR, P_START, P_SPEED, P_BEAT, P_VOLUME, P_HISCORE, P_SCORE, P_HIGH, P_LIVES, P_LEVEL, P_MSG, P_CELL0 };
constexpr int kMaxParams = P_CELL0 + kMaxCells, kTexts = 5;
enum Kind { K_CHOICE, K_BOOL, K_FLOAT, K_STORE, K_TEXT, K_READ };
struct ParamDef { const char* key; const char* name; Kind kind; float lo, hi, def; const char* const* choices; int n; };
const char* const kDirs[] { "Up", "Down", "Left", "Right" };
const char* const kSpeeds[] { "Slow", "Normal", "Fast" };   // screen keys: SLOW NORM FAST
const float kSpeedMul[] { 0.75f, 1.f, 1.25f };
ParamDef kParams[kMaxParams];
char cellKeys[kMaxCells][12], cellNames[kMaxCells][16];
int gNumParams = P_CELL0;

int countCells()
{
    int n = 0;
    for (int r = 0; r < H; ++r) for (int c = 0; c < W; ++c) if (kMaze[r][c] != '#') ++n;
    return n < kMaxCells ? n : kMaxCells;
}
void initParams()
{
    kParams[P_DIR] = { "dir", "Direction", K_CHOICE, 0, 3, LEFT, kDirs, 4 };
    kParams[P_START] = { "start", "Start / Pause", K_BOOL, 0, 1, 0, nullptr, 0 };
    kParams[P_SPEED] = { "speed", "Speed", K_CHOICE, 0, 2, 1, kSpeeds, 3 };
    kParams[P_BEAT] = { "beat", "Beat", K_BOOL, 0, 1, 1, nullptr, 0 };
    kParams[P_VOLUME] = { "volume", "Volume", K_FLOAT, -40, 0, -6, nullptr, 0 };
    kParams[P_HISCORE] = { "hiscore", "High Score (saved)", K_STORE, 0, 1000000, 0, nullptr, 0 };
    kParams[P_SCORE] = { "score", "Score", K_TEXT, 0, 1, 0, nullptr, 0 };
    kParams[P_HIGH] = { "high", "High", K_TEXT, 0, 1, 0, nullptr, 0 };
    kParams[P_LIVES] = { "lives", "Lives", K_TEXT, 0, 1, 0, nullptr, 0 };
    kParams[P_LEVEL] = { "level", "Level", K_TEXT, 0, 1, 0, nullptr, 0 };
    kParams[P_MSG] = { "msg", "Message", K_TEXT, 0, 1, 0, nullptr, 0 };
    const int n = countCells();
    for (int k = 0; k < n; ++k)
    {
        std::snprintf (cellKeys[k], sizeof cellKeys[k], "cell%d", k);
        std::snprintf (cellNames[k], sizeof cellNames[k], "Cell %d", k + 1);
        kParams[P_CELL0 + k] = { cellKeys[k], cellNames[k], K_READ, 0, 127, 0, nullptr, 0 };
    }
    gNumParams = P_CELL0 + n;
}
float toNorm (int i, float plain)
{
    const ParamDef& d = kParams[i];
    if (d.kind == K_TEXT) return plain / 1000.f;
    if (d.kind == K_READ) return plain / 127.f;
    return d.hi > d.lo ? (plain - d.lo) / (d.hi - d.lo) : 0.f;
}
float toPlain (int i, float norm)
{
    const ParamDef& d = kParams[i];
    norm = norm < 0 ? 0 : (norm > 1 ? 1 : norm);
    if (d.kind == K_BOOL) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (d.n - 1) + 0.5f);
    return d.lo + norm * (d.hi - d.lo);
}
void copyStr (void* dst, const char* s, size_t max) { if (dst == nullptr) return; std::strncpy ((char*) dst, s, max - 1); ((char*) dst)[max - 1] = 0; }

// display text written by the audio thread, read by MPC's UI thread: double buffered
struct Text
{
    char buf[2][48] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s)
    {
        if (std::strcmp (buf[live.load()], s) == 0) return false;
        const int idle = 1 - live.load();
        std::snprintf (buf[idle], sizeof buf[idle], "%s", s);
        live.store (idle); return true;
    }
};

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    std::atomic<float> nv[kMaxParams];
    float sent[kMaxParams];
    double sr = 44100;
    Game g; Synth syn;
    float seenDir = -1.f, seenStart = -1.f;
    Text text[kTexts]; int gen[kTexts] {};
    int notes[64]; int nNotes = 0;
    int lastHi = -1;

    Plugin()
    {
        for (int i = 0; i < kMaxParams; ++i) { nv[i].store (0.f); sent[i] = -1.f; }
        for (int i = 0; i < gNumParams; ++i) if (kParams[i].kind != K_READ && kParams[i].kind != K_TEXT) nv[i].store (toNorm (i, kParams[i].def));
        g.init(); syn.prepare (sr);
        seenDir = nv[P_DIR].load(); seenStart = nv[P_START].load();
        publishScreen (false);
    }
    float get (int i) const { return toPlain (i, nv[i].load()); }
    void automate (int i)
    {
        if (master != nullptr) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
    }
    // set a value the plugin owns and tell MPC's screen about it (only when it changed)
    void show (int i, float norm, bool notify)
    {
        nv[i].store (norm);
        if (sent[i] == norm) return;
        sent[i] = norm;
        if (notify) automate (i);
    }
    void setText (int which, const char* s, bool notify)
    {
        if (! text[which].publish (s)) return;
        gen[which] = (gen[which] + 1) % 1000;
        show (P_SCORE + which, gen[which] / 1000.f, notify);
    }
    void publishScreen (bool notify)
    {
        for (int k = 0; k < g.nCells; ++k) show (P_CELL0 + k, g.frameAt (k) / 127.f, notify);
        char b[48];
        std::snprintf (b, sizeof b, "%06d", g.score); setText (0, b, notify);
        std::snprintf (b, sizeof b, "%06d", g.hiscore); setText (1, b, notify);
        std::snprintf (b, sizeof b, "%d", g.lives); setText (2, b, notify);
        std::snprintf (b, sizeof b, "%d", g.level); setText (3, b, notify);
        setText (4, g.message(), notify);
    }
    void steer (int d)
    {
        g.wantDir = d;
        const float n = toNorm (P_DIR, (float) d);
        seenDir = n; show (P_DIR, n, true);
    }
    void handleNote (int note)
    {
        const int pad = ((note - 36) % 16 + 16) % 16 + 1;                 // 1..16, any bank / octave
        switch (pad)
        {
            case 10: steer (UP); break;
            case 5: steer (LEFT); break;
            case 6: steer (DOWN); break;
            case 7: steer (RIGHT); break;
            case 13: case 16: g.pressStart(); break;
            default: break;
        }
    }
    void process (float* L, float* R, int n)
    {
        for (int k = 0; k < nNotes; ++k) handleNote (notes[k]);
        nNotes = 0;
        // on-screen D-pad / START (or a Q-Link): any change of value is a press
        const float d = nv[P_DIR].load();
        if (d != seenDir) { seenDir = d; g.wantDir = (int) get (P_DIR); }
        const float st = nv[P_START].load();
        if (st != seenStart) { seenStart = st; g.pressStart(); }
        if (lastHi < 0) { g.hiscore = (int) std::lround (get (P_HISCORE)); lastHi = g.hiscore; }
        g.speedMul = kSpeedMul[(int) get (P_SPEED)];

        g.update (n / sr);
        syn.trigger (g.takeSfx());
        const bool playing = g.state == S_PLAY || g.state == S_READY;
        double bpm = 88.0 + 6.0 * (g.level - 1); if (bpm > 132) bpm = 132;
        syn.render (L, R, n, g.scaredUntil > 0 && g.state == S_PLAY, playing && get (P_BEAT) > 0.5f, bpm,
                    std::pow (10.f, get (P_VOLUME) * 0.05f));
        publishScreen (true);
        if (g.state == S_OVER && g.hiscore != lastHi)                      // keep the high score with the project
        {
            lastHi = g.hiscore;
            show (P_HISCORE, toNorm (P_HISCORE, (float) g.hiscore), true);
        }
    }
    void display (int i, char* out, size_t max)
    {
        const ParamDef& d = kParams[i];
        const float p = get (i);
        if (i >= P_SCORE && i <= P_MSG) { std::snprintf (out, max, "%s", text[i - P_SCORE].get()); return; }
        switch (d.kind)
        {
            case K_CHOICE: std::snprintf (out, max, "%s", d.choices[(int) p]); return;
            case K_BOOL: std::snprintf (out, max, "%s", i == P_START ? "Start" : (p > 0.5f ? "On" : "Off")); return;
            case K_FLOAT: std::snprintf (out, max, "%.1f dB", (double) p); return;
            case K_STORE: std::snprintf (out, max, "%d", (int) std::lround (p)); return;
            case K_READ: std::snprintf (out, max, "%d", (int) std::lround (nv[i].load() * 127.f)); return;
            default: out[0] = 0; return;
        }
    }
};

void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    (void) in;
    static_cast<Plugin*> (e->object)->process (out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    (void) in;
    float a[512], b[512];
    for (int32_t p = 0; p < n; p += 512)
    {
        const int m = n - p < 512 ? n - p : 512;
        static_cast<Plugin*> (e->object)->process (a, b, m);
        for (int i = 0; i < m; ++i) { out[0][p + i] += a[i]; out[1][p + i] += b[i]; }
    }
}
void setParameter (AEffect* e, int32_t i, float v)
{
    if (i < 0 || i >= gNumParams) return;
    const Kind k = kParams[i].kind;
    if (k == K_READ || k == K_TEXT) return;                                   // owned by the game
    static_cast<Plugin*> (e->object)->nv[i].store (v < 0 ? 0 : (v > 1 ? 1 : v));
}
float getParameter (AEffect* e, int32_t i)
{
    if (i < 0 || i >= gNumParams) return 0.f;
    return static_cast<Plugin*> (e->object)->nv[i].load();
}
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    (void) val;
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:            p->~Plugin(); std::free (p); return 0;
        case effGetParamName:     if (idx >= 0 && idx < gNumParams) copyStr (ptr, kParams[idx].name, 28); return 0;
        case effGetParamLabel:    if (ptr != nullptr) ((char*) ptr)[0] = 0; return 0;
        case effGetParamDisplay:
            if (idx >= 0 && idx < gNumParams && ptr != nullptr) { char b[64]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); }
            return 0;
        case effCanBeAutomated:   return (idx >= 0 && idx < gNumParams && (kParams[idx].kind == K_CHOICE || kParams[idx].kind == K_FLOAT ||
                                          (kParams[idx].kind == K_BOOL && idx != P_START))) ? 1 : 0;
        case effSetSampleRate:    if (opt > 1000.f) { p->sr = opt; p->syn.prepare (opt); } return 0;
        case effMainsChanged:     return 0;
        case effProcessEvents:
        {
            const VstEvents* ev = (const VstEvents*) ptr;
            if (ev == nullptr) return 1;
            for (int i = 0; i < ev->numEvents && p->nNotes < 64; ++i)
            {
                const VstEvent* v = ev->events[i];
                if (v == nullptr || v->type != kVstMidiType) continue;
                const VstMidiEvent* m = (const VstMidiEvent*) v;
                const int st = (uint8_t) m->midiData[0] & 0xf0, d2 = (uint8_t) m->midiData[2] & 0x7f;
                if (st == 0x90 && d2 > 0) p->notes[p->nNotes++] = (uint8_t) m->midiData[1] & 0x7f;
            }
            return 1;
        }
        case effGetProgram:       return 0;
        case effGetProgramName:
        case effGetProgramNameIndexed: copyStr (ptr, "Da Maze Muncher", 24); return 1;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "Da Maze Muncher", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "RadioReady Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory:  return kPlugCategSynth;
        case effGetVstVersion:    return 2400;
        case effGetTailSize:      return 0;
        case effCanDo:
        {
            const char* s = (const char*) ptr;
            if (s == nullptr) return 0;
            if (! std::strcmp (s, "receiveVstEvents") || ! std::strcmp (s, "receiveVstMidiEvent")) return 1;
            if (! std::strcmp (s, "sendVstEvents") || ! std::strcmp (s, "sendVstMidiEvent") || ! std::strcmp (s, "offline")) return -1;
            return 0;
        }
        default: return 0;
    }
}
void initOnce()
{
    static bool done = false;
    if (! done) { initParams(); done = true; }
}
} // namespace

MM_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    initOnce();
    void* mem = std::calloc (1, sizeof (Plugin));                         // no operator new: keeps libstdc++ out
    if (mem == nullptr) return nullptr;
    Plugin* p = new (mem) Plugin();
    p->master = master;
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic;
    fx.dispatcher = dispatcher;
    fx.process = processAccumulating;
    fx.processReplacing = processReplacing;
    fx.setParameter = setParameter;
    fx.getParameter = getParameter;
    fx.numPrograms = 1;
    fx.numParams = gNumParams;
    fx.numInputs = 0;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing | effFlagsIsSynth;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('D' << 24) | ('M' << 16) | ('Z' << 8) | 'M';            // 'DMZM'
    fx.version = kVersion;
    return &fx;
}
// for tools/make_skin.py and the tests (not used by MPC)
MM_EXPORT const char* MM_ParamKey (int i) { initOnce(); return (i >= 0 && i < gNumParams) ? kParams[i].key : ""; }
MM_EXPORT int MM_ParamCount() { initOnce(); return gNumParams; }
