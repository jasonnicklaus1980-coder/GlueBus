// G-Glue Bus Compressor for MPC OS (Gen1 standalone devices: MPC X, Live / Live II, One, Key 61, Force).
//
// UNOFFICIAL: Akai publishes no third-party plugin SDK for MPC Standalone. MPC OS loads Linux VST2 plugins that are
// registered in MPC.settings (pluginList-arm), which needs root access; this is that route (see ../README.md).
//
// A thin wrapper: the DSP, parameters, presets and state are the shared G-Glue core (../../DSP, ../../Parameters,
// ../../Presets), identical to the desktop VST3 / VST2. This file adds the VST2 entry points, the MPC-screen controls
// (preset browser, A/B, meters as read-only parameters) and the project chunk.
//
// Threads: MPC's UI thread (setParameter, dispatcher: buttons, presets, files) and the audio thread (process only:
// never allocates, locks or touches files). Meters are computed in the audio thread and sent to the screen with
// audioMasterAutomate only when their filmstrip frame changes.
#include "vst2.h"
#include "DSP/GGlueEngine.h"
#include "GUI/NeedleBallistics.h"
#include "Presets/GGluePresets.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <time.h>
#include <vector>

#define GG_EXPORT extern "C" __attribute__ ((visibility ("default")))

namespace
{
using namespace gglue;
constexpr int kVersion = 1000;

// ------------------------------------------------------------------------------------------------ parameters
// 0..10 = the shared G-Glue parameters, same order as the desktop plugin (Q-Link 1-4 = threshold, makeup, attack,
// release), then the MPC-screen controls and read-only readouts.
enum
{
    P_BROWSE = kNumParams, P_LOAD, P_PREV, P_NEXT, P_FAV, P_SAVE, P_DELETE, P_AB, P_COPY,
    P_GR, P_INMETER, P_OUTMETER, P_STATUS, P_PRESET,
    NPARAMS
};
constexpr int kFirstReadout = P_GR;
const char* const kExtraKeys[NPARAMS - kNumParams] { "browse", "load", "prev", "next", "fav", "save", "delete", "ab", "copy",
                                                     "gr", "inmeter", "outmeter", "status", "preset" };
const char* const kExtraNames[NPARAMS - kNumParams] { "Preset", "Load Preset", "Previous Preset", "Next Preset", "Favourite",
                                                      "Save Preset", "Delete Preset", "A / B", "Copy A/B", "Gain Reduction",
                                                      "Input Level", "Output Level", "Status", "Current Preset" };
bool isButton (int i) { return i >= P_LOAD && i <= P_COPY; }
const char* keyOf (int i) { return i < kNumParams ? spec (i).id : kExtraKeys[i - kNumParams]; }
const char* nameOf (int i) { return i < kNumParams ? spec (i).name : kExtraNames[i - kNumParams]; }

double nowSeconds() { timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (double) t.tv_sec + (double) t.tv_nsec * 1e-9; }

void copyStr (void* dst, const char* src, size_t max)
{
    if (! dst || max == 0) return;
    std::strncpy ((char*) dst, src, max - 1);
    ((char*) dst)[max - 1] = 0;
}

// double-buffered text: written on the UI thread, read by the dispatcher
struct Text
{
    char buf[2][128] {}; std::atomic<int> live { 0 };
    const char* get() const { return buf[live.load()]; }
    bool publish (const char* s)
    {
        if (std::strcmp (get(), s) == 0) return false;
        const int idle = 1 - live.load();
        std::snprintf (buf[idle], sizeof buf[idle], "%s", s);
        live.store (idle);
        return true;
    }
};

const char* presetFolder()
{
    static char dir[300];
    if (! dir[0]) std::snprintf (dir, sizeof dir, "%s", std::getenv ("GGLUE_PRESET_DIR") ? std::getenv ("GGLUE_PRESET_DIR") : "/sdcard/G-Glue/Presets");
    return dir;
}

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    Engine engine;
    double sr = 44100.0;

    std::atomic<float> nv[NPARAMS];        // normalised values (host side)
    float seen[NPARAMS];                    // UI thread: last value handled
    float sent[NPARAMS];                    // audio thread: last readout value sent to the screen
    double guardUntil = 0;                  // buttons ignored until then (project load)

    // meters (audio thread)
    NeedleBallistics needle;

    // presets / state (UI thread, behind libLock)
    std::recursive_mutex libLock;              // recursive: host callbacks can re-enter display()
    PresetLibrary library { presetFolder() };
    std::vector<int> browseOrder;           // favourites first, then the rest
    PluginState state;                      // other slot, active slot, preset name, modified
    bool applying = false;
    double deleteArmedUntil = 0;
    int deleteArmedIndex = -1;
    Text status, presetText;
    std::string chunk;

    Plugin()
    {
        const ParamValues d = defaultValues();
        for (int i = 0; i < NPARAMS; ++i) nv[i].store (0.f);
        for (int i = 0; i < kNumParams; ++i) nv[i].store (toNormalized (i, d[(size_t) i]));
        for (int i = 0; i < NPARAMS; ++i) { seen[i] = nv[i].load(); sent[i] = -1.f; }
        engine.setParameters (d);
        engine.prepare (sr, 2);
        guardUntil = nowSeconds() + 2.5;
        rebuildBrowse();
        const int start = library.indexOf ("Clean Glue", true);
        nv[P_BROWSE].store (browseNorm (start)); seen[P_BROWSE] = nv[P_BROWSE].load();
        state.presetName = "Default";
        status.publish ("G-Glue ready: turn PRESET and tap LOAD, or use the knobs");
        refreshPresetText();
    }

    // ------------------------------------------------------------------------------------------ values
    ParamValues currentValues() const
    {
        ParamValues v;
        for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = fromNormalized (i, nv[i].load());
        return v;
    }
    void setPlainFromUi (int i, float plain)               // UI thread: change a value and tell the host
    {
        const float n = toNormalized (i, plain);
        nv[i].store (n); seen[i] = n;
        if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, n);
    }
    void applyValues (const ParamValues& v, bool keepBypass)
    {
        applying = true;
        for (int i = 0; i < kNumParams; ++i)
            if (! (keepBypass && i == kBypass)) setPlainFromUi (i, clampPlain (i, v[(size_t) i]));
        applying = false;
    }

    // ------------------------------------------------------------------------------------------ preset browser
    void rebuildBrowse()
    {
        browseOrder.clear();
        const auto& list = library.presets();
        for (int i = 0; i < (int) list.size(); ++i) if (library.isFavorite (i)) browseOrder.push_back (i);
        for (int i = 0; i < (int) list.size(); ++i) if (! library.isFavorite (i)) browseOrder.push_back (i);
    }
    float browseNorm (int libIndex) const
    {
        const auto it = std::find (browseOrder.begin(), browseOrder.end(), libIndex);
        const int pos = it == browseOrder.end() ? 0 : (int) (it - browseOrder.begin());
        return browseOrder.size() > 1 ? (float) pos / (float) (browseOrder.size() - 1) : 0.f;
    }
    int browsePos() const
    {
        if (browseOrder.empty()) return 0;
        const int n = (int) browseOrder.size();
        const int p = (int) std::lround (nv[P_BROWSE].load() * (float) (n - 1));
        return p < 0 ? 0 : (p >= n ? n - 1 : p);
    }
    int browseIndex() const { return browseOrder.empty() ? -1 : browseOrder[(size_t) browsePos()]; }
    void selectBrowse (int libIndex)
    {
        const float n = browseNorm (libIndex);
        nv[P_BROWSE].store (n); seen[P_BROWSE] = n;
        if (master) master (&fx, audioMasterAutomate, P_BROWSE, 0, nullptr, n);
    }
    std::string browseText()
    {
        std::lock_guard<std::recursive_mutex> g (libLock);
        const int i = browseIndex();
        if (i < 0) return "-";
        const Preset& p = library.presets()[(size_t) i];
        char b[96];
        std::snprintf (b, sizeof b, "%s%s", library.isFavorite (i) ? "\xe2\x98\x85 " : "", p.name.c_str());
        return b;
    }
    void refreshPresetText()
    {
        char b[128];
        std::snprintf (b, sizeof b, "%s%s   [%c]", state.presetName.c_str(), state.modified ? " *" : "", state.activeSlot);
        if (presetText.publish (b)) notifyReadout (P_PRESET);
    }
    void setStatus (const char* s) { if (status.publish (s)) notifyReadout (P_STATUS); }
    void notifyReadout (int i)
    {
        // text readouts: bump the value so MPC re-reads the display string
        nv[i].store (nv[i].load() > 0.5f ? 0.f : 1.f);
        if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, nv[i].load());
    }
    void loadPreset (int libIndex)
    {
        const auto& list = library.presets();
        if (libIndex < 0 || libIndex >= (int) list.size()) return;
        applyValues (list[(size_t) libIndex].values, true);
        state.presetName = list[(size_t) libIndex].name;
        state.modified = false;
        selectBrowse (libIndex);
        char b[128];
        std::snprintf (b, sizeof b, "Loaded %s (%s)", list[(size_t) libIndex].name.c_str(), list[(size_t) libIndex].category.c_str());
        setStatus (b);
        refreshPresetText();
    }

    // ------------------------------------------------------------------------------------------ UI thread
    void poll()
    {
        bool presetChanged = false;
        for (int i = 0; i < kFirstReadout; ++i)
        {
            const float v = nv[i].load();
            if (v == seen[i]) continue;
            seen[i] = v;
            if (isButton (i)) { press (i); continue; }
            if (i < kNumParams && i != kBypass && ! applying && ! state.modified) { state.modified = true; presetChanged = true; }
            if (i == P_BROWSE) { char b[128]; std::snprintf (b, sizeof b, "Selected %s: tap LOAD", browseText().c_str()); setStatus (b); }
        }
        if (presetChanged) refreshPresetText();
    }

    void press (int i)
    {
        if (nowSeconds() < guardUntil) return;
        std::lock_guard<std::recursive_mutex> g (libLock);
        char b[160];
        switch (i)
        {
            case P_LOAD: loadPreset (browseIndex()); break;
            case P_PREV: case P_NEXT:
            {
                if (browseOrder.empty()) break;
                const int n = (int) browseOrder.size();
                const int pos = ((browsePos() + (i == P_NEXT ? 1 : -1)) % n + n) % n;
                loadPreset (browseOrder[(size_t) pos]);
                break;
            }
            case P_FAV:
            {
                const int idx = browseIndex();
                if (idx < 0) break;
                const bool fav = ! library.isFavorite (idx);
                library.setFavorite (idx, fav);
                const std::string name = library.presets()[(size_t) idx].name;
                rebuildBrowse();
                selectBrowse (idx);
                std::snprintf (b, sizeof b, "%s %s favourites", name.c_str(), fav ? "added to" : "removed from");
                setStatus (b);
                break;
            }
            case P_SAVE:
            {
                // no keyboard on the MPC screen: user presets are numbered (rename them on a computer if you like)
                std::string name, err;
                for (int k = 1; k < 1000; ++k)
                {
                    char nm[32]; std::snprintf (nm, sizeof nm, "User %02d", k);
                    if (library.indexOf (nm, false) < 0) { name = nm; break; }
                }
                if (library.save (name, "USER", currentValues(), false, &err))
                {
                    rebuildBrowse();
                    state.presetName = name; state.modified = false;
                    selectBrowse (library.indexOf (name, false));
                    std::snprintf (b, sizeof b, "Saved %s in %s", name.c_str(), presetFolder());
                    refreshPresetText();
                }
                else std::snprintf (b, sizeof b, "Save failed: %s", err.c_str());
                setStatus (b);
                break;
            }
            case P_DELETE:
            {
                const int idx = browseIndex();
                if (idx < 0 || library.presets()[(size_t) idx].factory) { setStatus ("Only user presets can be deleted"); break; }
                const std::string name = library.presets()[(size_t) idx].name;
                if (deleteArmedIndex != idx || nowSeconds() > deleteArmedUntil)
                {
                    deleteArmedIndex = idx; deleteArmedUntil = nowSeconds() + 4.0;
                    std::snprintf (b, sizeof b, "Tap DELETE again to delete %s", name.c_str());
                    setStatus (b);
                    break;
                }
                deleteArmedIndex = -1;
                std::string err;
                if (library.remove (idx, &err))
                {
                    rebuildBrowse();
                    selectBrowse (browseOrder.empty() ? -1 : browseOrder[0]);
                    std::snprintf (b, sizeof b, "Deleted %s", name.c_str());
                }
                else std::snprintf (b, sizeof b, "Delete failed: %s", err.c_str());
                setStatus (b);
                break;
            }
            case P_AB:
            {
                const ParamValues now = currentValues();
                applyValues (state.other, true);
                state.other = now;
                state.activeSlot = state.activeSlot == 'A' ? 'B' : 'A';
                std::snprintf (b, sizeof b, "Now hearing %c", state.activeSlot);
                setStatus (b);
                refreshPresetText();
                break;
            }
            case P_COPY:
                state.other = currentValues();
                std::snprintf (b, sizeof b, "Copied %c to %c", state.activeSlot, state.activeSlot == 'A' ? 'B' : 'A');
                setStatus (b);
                break;
            default: break;
        }
    }

    // ------------------------------------------------------------------------------------------ audio thread
    void process (float** in, float** out, int n)
    {
        for (int i = 0; i < kNumParams; ++i) engine.setParameter (i, fromNormalized (i, nv[i].load (std::memory_order_relaxed)));
        if (in[0] != out[0]) std::memcpy (out[0], in[0], sizeof (float) * (size_t) n);
        if (in[1] != out[1]) std::memcpy (out[1], in[1], sizeof (float) * (size_t) n);
        engine.process (out, 2, n);
        // meters: needle physics here (no GUI timer on MPC); values are filmstrip positions 0..1
        const double gr = needle.step (engine.gainReductionDb.load (std::memory_order_relaxed), (double) n / sr);
        sendReadout (P_GR, (float) (1.0 - GrScale::position (gr)));
        sendReadout (P_INMETER, std::min (1.f, std::max (0.f, (engine.inputMeterDb.load() + 48.f) / 48.f)));
        sendReadout (P_OUTMETER, std::min (1.f, std::max (0.f, (engine.outputMeterDb.load() + 48.f) / 48.f)));
    }
    void sendReadout (int i, float v)
    {
        v = std::round (v * 127.f) / 127.f;                 // one filmstrip frame = one update
        nv[i].store (v, std::memory_order_relaxed);
        if (v != sent[i]) { sent[i] = v; if (master) master (&fx, audioMasterAutomate, i, 0, nullptr, v); }
    }

    // ------------------------------------------------------------------------------------------ chunk
    const std::string& saveState()
    {
        PluginState s = state;
        s.current = currentValues();
        chunk = stateToText (s);
        return chunk;
    }
    void loadState (const char* data, size_t size)
    {
        PluginState s;
        if (size > (1u << 20) || ! stateFromText (std::string (data, size), s)) return;
        state = s;
        for (int i = 0; i < kNumParams; ++i) { nv[i].store (toNormalized (i, s.current[(size_t) i])); seen[i] = nv[i].load(); }
        guardUntil = nowSeconds() + 2.5;
        std::lock_guard<std::recursive_mutex> g (libLock);
        const int idx = library.indexOf (s.presetName);
        if (idx >= 0) { const float bn = browseNorm (idx); nv[P_BROWSE].store (bn); seen[P_BROWSE] = bn; }
        refreshPresetText();
    }

    void display (int i, char* out, size_t max)
    {
        if (i < kNumParams) { std::snprintf (out, max, "%s", formatValue (i, fromNormalized (i, nv[i].load())).c_str()); return; }
        switch (i)
        {
            case P_BROWSE: std::snprintf (out, max, "%s", browseText().c_str()); return;
            case P_STATUS: std::snprintf (out, max, "%s", status.get()); return;
            case P_PRESET: std::snprintf (out, max, "%s", presetText.get()); return;
            case P_GR: std::snprintf (out, max, "%.1f dB", needle.position); return;
            case P_INMETER: case P_OUTMETER: std::snprintf (out, max, "%.0f dB", nv[i].load() * 48.0 - 48.0); return;
            default: std::snprintf (out, max, "%s", nameOf (i)); return;
        }
    }
};

void processReplacing (AEffect* e, float** in, float** out, int32_t n) { static_cast<Plugin*> (e->object)->process (in, out, n); }
void processAccumulating (AEffect* e, float** in, float** out, int32_t n) { static_cast<Plugin*> (e->object)->process (in, out, n); }

void setParameter (AEffect* e, int32_t i, float v)
{
    if (i < 0 || i >= kFirstReadout) return;                     // readouts belong to the plugin
    Plugin* p = static_cast<Plugin*> (e->object);
    p->nv[i].store (v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
    p->poll();
}
float getParameter (AEffect* e, int32_t i) { return (i >= 0 && i < NPARAMS) ? static_cast<Plugin*> (e->object)->nv[i].load() : 0.f; }

intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    const auto& fp = factoryPresets();
    switch (op)
    {
        case effClose: delete p; return 0;
        case effGetParamName: if (idx >= 0 && idx < NPARAMS) copyStr (ptr, nameOf (idx), 28); return 0;
        case effGetParamLabel: if (ptr) ((char*) ptr)[0] = 0; return 0;
        case effGetParamDisplay: if (idx >= 0 && idx < NPARAMS && ptr) { char b[160]; p->display (idx, b, sizeof b); copyStr (ptr, b, 64); } return 0;
        case effCanBeAutomated: return (idx >= 0 && idx < kNumParams) ? 1 : 0;
        case effSetSampleRate:
            if (opt > 1000.f && std::fabs (opt - p->sr) > 0.5) { p->sr = opt; p->engine.prepare (p->sr, 2); }
            return 0;
        case effMainsChanged: if (val) p->engine.prepare (p->sr, 2); return 0;
        case effGetChunk: { const std::string& s = p->saveState(); *(void**) ptr = (void*) s.c_str(); return (intptr_t) s.size(); }
        case effSetChunk: if (ptr && val > 0) p->loadState ((const char*) ptr, (size_t) val); return 1;
        case effSetProgram:
            if (val >= 0 && val < (intptr_t) fp.size() && nowSeconds() >= p->guardUntil)
            {
                std::lock_guard<std::recursive_mutex> g (p->libLock);
                p->loadPreset (p->library.indexOf (fp[(size_t) val].name, true));
            }
            return 0;
        case effGetProgram:
        {
            std::lock_guard<std::recursive_mutex> g (p->libLock);
            const int i = p->library.indexOf (p->state.presetName, true);
            return i < 0 ? 0 : i;
        }
        case effGetProgramName: copyStr (ptr, p->state.presetName.c_str(), 24); return 1;
        case effGetProgramNameIndexed:
            if (idx >= 0 && idx < (int) fp.size()) { copyStr (ptr, fp[(size_t) idx].name.c_str(), 24); return 1; }
            return 0;
        case effGetEffectName: case effGetProductString: copyStr (ptr, "G-Glue Bus Compressor", 32); return 1;
        case effGetVendorString: copyStr (ptr, "G-Glue Audio", 32); return 1;
        case effGetVendorVersion: return kVersion;
        case effGetPlugCategory: return kPlugCategEffect;
        case effGetVstVersion: return 2400;
        case effGetTailSize: return 0;
        case effCanDo: return -1;
        default: return 0;
    }
}
} // namespace

GG_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    Plugin* p = new (std::nothrow) Plugin();
    if (! p) return nullptr;
    p->master = master;
    AEffect& fx = p->fx;
    fx.magic = kEffectMagic; fx.dispatcher = dispatcher; fx.process = processAccumulating; fx.processReplacing = processReplacing;
    fx.setParameter = setParameter; fx.getParameter = getParameter;
    fx.numPrograms = (int32_t) factoryPresets().size(); fx.numParams = NPARAMS; fx.numInputs = 2; fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing | effFlagsProgramChunks;
    fx.ioRatio = 1.f; fx.object = p;
    fx.uniqueID = ('G' << 24) | ('g' << 16) | ('B' << 8) | 'c';           // 'GgBc', same as the desktop build
    fx.version = kVersion;
    return &fx;
}
GG_EXPORT const char* GG_ParamKey (int i) { return (i >= 0 && i < NPARAMS) ? keyOf (i) : ""; }
GG_EXPORT int GG_ParamCount() { return NPARAMS; }
