// The Remixer - Phase 1: direct audio recording on the Akai MPC X (Gen1 MPC OS, 32-bit ARM).
// Audio passes through bit-exact. While Record is on, the plugin's stereo input is copied into a lock-free ring
// buffer on the audio thread and a writer thread streams it to a 24-bit PCM WAV file on MPC storage.
// The audio thread never touches the disk, never allocates and never locks.
//
// Input selection is the MPC's job: put the plugin on a track whose input is the physical input you want
// (see docs/PHASE1.md for what still has to be checked on the device).
//
// Needs only libc/libm/libpthread. No GUI yet: the screen skin comes in a later phase.
#include "vst2.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define TR_EXPORT extern "C" __attribute__((visibility("default")))

namespace
{
constexpr int kRingFrames = 1 << 16;                      // 65536 stereo frames: about 1.5 s at 44.1 kHz
constexpr int kChunkFrames = 1024;                        // frames the writer moves per file write
constexpr const char* kRootDir = "/media/az01-internal/TheRemixer";
constexpr const char* kRecDir = "/media/az01-internal/TheRemixer/Recordings";
constexpr float kPeakDecay = 0.99995f;                    // per sample: a peak falls about 1 dB per second at 48 kHz

enum { P_RECORD, P_PEAK_L, P_PEAK_R, P_CLIPS, P_REC_TIME, P_DROPOUTS, P_STATUS, P_COUNT };
enum { S_IDLE = 0, S_RECORDING = 1, S_SAVED = 2, S_FAILED = 3 };

// Only the audio thread writes the ring's w index; only the writer thread writes r. Both are monotonic counters.
struct Ring
{
    float buf[kRingFrames * 2];
    std::atomic<unsigned> w {0}, r {0};
};

struct Plugin
{
    AEffect fx {};
    audioMasterCallback master = nullptr;
    float sr = 44100.f;

    // audio thread -> host
    std::atomic<float> peakL {0.f}, peakR {0.f};          // linear, decaying
    std::atomic<int> clips {0};
    std::atomic<int> dropouts {0};
    std::atomic<bool> armed {false};
    Ring ring;

    bool notifying = false;               // guards notifyHost() against re-entry

    // host thread -> writer thread
    std::atomic<bool> writerStop {false};
    std::atomic<long> framesWritten {0};
    pthread_t writer {};
    FILE* file = nullptr;
    uint32_t fileRate = 44100;
    std::atomic<int> state {S_IDLE};
};

float toDb (float lin) { return lin <= 1e-6f ? -120.f : 20.f * std::log10 (lin); }

// ------------------------------------------------------------------------------------------------ WAV file
void putLE (uint8_t* d, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i) d[i] = (uint8_t) (v >> (8 * i));
}

// Writes the 44-byte header at the start of the file, leaving the position just after it.
void writeHeader (FILE* f, uint32_t rate, uint32_t dataBytes)
{
    uint8_t h[44];
    std::memcpy (h, "RIFF", 4);          putLE (h + 4, 36 + dataBytes, 4);
    std::memcpy (h + 8, "WAVEfmt ", 8);
    putLE (h + 16, 16, 4);               // fmt chunk size
    putLE (h + 20, 1, 2);                // PCM
    putLE (h + 22, 2, 2);                // stereo
    putLE (h + 24, rate, 4);
    putLE (h + 28, rate * 6, 4);         // byte rate
    putLE (h + 32, 6, 2);                // block align
    putLE (h + 34, 24, 2);               // bits per sample
    std::memcpy (h + 36, "data", 4);     putLE (h + 40, dataBytes, 4);
    std::fseek (f, 0, SEEK_SET);
    std::fwrite (h, 1, 44, f);
}

inline void put24 (uint8_t* d, float x)
{
    float c = x < -1.f ? -1.f : (x > 1.f ? 1.f : x);
    int32_t v = (int32_t) std::lrintf (c * 8388607.f);
    d[0] = (uint8_t) (v & 0xff);
    d[1] = (uint8_t) ((v >> 8) & 0xff);
    d[2] = (uint8_t) ((v >> 16) & 0xff);
}

// Moves everything the ring holds to the file. Returns the number of frames moved.
unsigned drainRing (Plugin* p)
{
    unsigned r = p->ring.r.load (std::memory_order_relaxed);
    unsigned avail = p->ring.w.load (std::memory_order_acquire) - r;
    unsigned moved = 0;
    uint8_t out[kChunkFrames * 6];
    while (avail > 0)
    {
        unsigned n = avail < kChunkFrames ? avail : kChunkFrames;
        for (unsigned i = 0; i < n; ++i)
        {
            const float* f = &p->ring.buf[((r + i) % kRingFrames) * 2];
            put24 (out + i * 6, f[0]);
            put24 (out + i * 6 + 3, f[1]);
        }
        std::fwrite (out, 6, n, p->file);
        r += n; avail -= n; moved += n;
        p->ring.r.store (r, std::memory_order_release);
    }
    p->framesWritten.fetch_add ((long) moved);
    return moved;
}

void* writerMain (void* arg)
{
    Plugin* p = static_cast<Plugin*> (arg);
    while (true)
    {
        if (drainRing (p) == 0)
        {
            if (p->writerStop.load()) break;
            usleep (2000);
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ recording
// Runs on the host's control thread (setParameter), never on the audio thread.
void startRecording (Plugin* p)
{
    if (p->state.load() == S_RECORDING) return;
    const char* dir = std::getenv ("REMIXER_REC_DIR");     // test hook: send recordings somewhere else
    if (dir == nullptr)
    {
        mkdir (kRootDir, 0755);
        dir = kRecDir;
    }
    mkdir (dir, 0755);
    char path[512];
    std::snprintf (path, sizeof path, "%s/rec_%ld.wav", dir, (long) std::time (nullptr));
    p->file = std::fopen (path, "wb");
    if (p->file == nullptr) { p->state.store (S_FAILED); return; }

    p->fileRate = (uint32_t) std::lrintf (p->sr);
    writeHeader (p->file, p->fileRate, 0);
    p->ring.r.store (0); p->ring.w.store (0);
    p->framesWritten.store (0);
    p->dropouts.store (0);
    p->clips.store (0);
    p->writerStop.store (false);
    pthread_create (&p->writer, nullptr, writerMain, p);
    p->armed.store (true);
    p->state.store (S_RECORDING);
}

void stopRecording (Plugin* p)
{
    if (p->state.load() != S_RECORDING) return;
    p->armed.store (false);
    usleep (50000);                       // let any audio block already in flight finish writing to the ring
    p->writerStop.store (true);
    pthread_join (p->writer, nullptr);
    drainRing (p);                        // whatever the writer thread left behind
    writeHeader (p->file, p->fileRate, (uint32_t) (p->framesWritten.load() * 6));
    std::fclose (p->file);
    p->file = nullptr;
    p->state.store (S_SAVED);
}

// ------------------------------------------------------------------------------------------------ audio
void processBlock (Plugin* p, const float* il, const float* ir, float* ol, float* orr, int32_t n)
{
    const bool rec = p->armed.load (std::memory_order_acquire);
    const float decay = std::pow (kPeakDecay, (float) n);
    float pl = p->peakL.load (std::memory_order_relaxed) * decay;
    float pr = p->peakR.load (std::memory_order_relaxed) * decay;
    int clipped = 0;
    unsigned w = p->ring.w.load (std::memory_order_relaxed);
    const unsigned r = p->ring.r.load (std::memory_order_acquire);
    int dropped = 0;

    for (int32_t i = 0; i < n; ++i)
    {
        const float l = il[i], rv = ir[i];
        ol[i] = l;
        orr[i] = rv;
        const float al = std::fabs (l), ar = std::fabs (rv);
        if (al > pl) pl = al;
        if (ar > pr) pr = ar;
        if (al >= 1.f || ar >= 1.f) ++clipped;
        if (rec)
        {
            if (w - r >= (unsigned) kRingFrames) { ++dropped; continue; }
            float* f = &p->ring.buf[(w % kRingFrames) * 2];
            f[0] = l; f[1] = rv;
            ++w;
        }
    }
    if (rec) p->ring.w.store (w, std::memory_order_release);
    p->peakL.store (pl, std::memory_order_relaxed);
    p->peakR.store (pr, std::memory_order_relaxed);
    if (clipped) p->clips.fetch_add (clipped, std::memory_order_relaxed);
    if (dropped) p->dropouts.fetch_add (dropped, std::memory_order_relaxed);
}

void processReplacing (AEffect* e, float** in, float** out, int32_t n)
{
    processBlock (static_cast<Plugin*> (e->object), in[0], in[1], out[0], out[1], n);
}
void processAccumulating (AEffect* e, float** in, float** out, int32_t n)
{
    float tl[256], tr[256];
    Plugin* p = static_cast<Plugin*> (e->object);
    for (int32_t pos = 0; pos < n; pos += 256)
    {
        const int32_t m = (n - pos) < 256 ? (n - pos) : 256;
        processBlock (p, in[0] + pos, in[1] + pos, tl, tr, m);
        for (int32_t i = 0; i < m; ++i) { out[0][pos + i] += tl[i]; out[1][pos + i] += tr[i]; }
    }
}

// ------------------------------------------------------------------------------------------------ parameters
// Record is a press, not a switch: MPC sends 1 on each press and 0 on release, and the skin's buttons are Instant.
// Each press toggles recording. The plugin then sets Record back to 0 and tells the host, so the next press is a
// press again (the same pattern as Da Lufs Plug's Prev/Next/Reset). The state is shown by the Status readout.
void notifyHost (Plugin* p)
{
    if (p->master == nullptr || p->notifying) return;
    p->notifying = true;
    p->master (&p->fx, 0 /* audioMasterAutomate */, P_RECORD, 0, nullptr, 0.f);
    p->master (&p->fx, 42 /* audioMasterUpdateDisplay */, 0, 0, nullptr, 0.f);
    p->notifying = false;
}

void setParameter (AEffect* e, int32_t idx, float v)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    if (idx != P_RECORD || v < 0.5f || p->notifying) return;   // releases and our own resets do nothing
    if (p->state.load() == S_RECORDING) stopRecording (p);
    else startRecording (p);
    notifyHost (p);
}

float getParameter (AEffect* e, int32_t idx)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (idx)
    {
        case P_RECORD:    return 0.f;                       // momentary: always at rest
        case P_PEAK_L:    return toDb (p->peakL.load());
        case P_PEAK_R:    return toDb (p->peakR.load());
        case P_CLIPS:     return (float) p->clips.load();
        case P_REC_TIME:  return p->sr > 0.f ? (float) p->framesWritten.load() / p->sr : 0.f;
        case P_DROPOUTS:  return (float) p->dropouts.load();
        case P_STATUS:    return (float) p->state.load();
        default:          return 0.f;
    }
}

const char* kNames[P_COUNT] = { "Record", "Peak L", "Peak R", "Clips", "Rec Time", "Dropouts", "Status" };
const char* kLabels[P_COUNT] = { "", "dBFS", "dBFS", "", "", "", "" };

void copyStr (void* dst, const char* src, size_t max = 24)
{
    if (dst == nullptr) return;
    std::strncpy ((char*) dst, src, max - 1);
    ((char*) dst)[max - 1] = 0;
}

// ------------------------------------------------------------------------------------------------ dispatcher
intptr_t dispatcher (AEffect* e, int32_t op, int32_t idx, intptr_t val, void* ptr, float opt)
{
    Plugin* p = static_cast<Plugin*> (e->object);
    switch (op)
    {
        case effClose:
            stopRecording (p);
            p->~Plugin();
            std::free (p);
            return 0;
        case effSetSampleRate:    if (opt > 1000.f) p->sr = opt; return 0;
        case effGetProgram:       return 0;
        case effGetProgramName:   copyStr (ptr, "Default", 32); return 0;
        case effGetProgramNameIndexed:
            if (idx != 0) return 0;
            copyStr (ptr, "Default", 32); return 1;
        case effGetParamName:     if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kNames[idx]); return 0;
        case effGetParamLabel:    if (idx >= 0 && idx < P_COUNT) copyStr (ptr, kLabels[idx]); return 0;
        case effGetParamDisplay:
        {
            if (idx < 0 || idx >= P_COUNT || ptr == nullptr) return 0;
            char b[40];
            const float v = getParameter (e, idx);
            switch (idx)
            {
                case P_RECORD:   std::snprintf (b, sizeof b, "%s", "Press"); break;
                case P_PEAK_L:
                case P_PEAK_R:
                    if (v <= -119.f) std::snprintf (b, sizeof b, "-inf");
                    else std::snprintf (b, sizeof b, "%.1f", (double) v);
                    break;
                case P_CLIPS:    std::snprintf (b, sizeof b, "%d", (int) v); break;
                case P_REC_TIME: std::snprintf (b, sizeof b, "%02d:%02d", (int) v / 60, (int) v % 60); break;
                case P_DROPOUTS: std::snprintf (b, sizeof b, "%d", (int) v); break;
                case P_STATUS:   std::snprintf (b, sizeof b, "%s", v < 0.5f ? "Idle" : (v < 1.5f ? "Recording" : (v < 2.5f ? "Saved" : "Failed")));
                                 break;
                default:         b[0] = 0; break;
            }
            copyStr (ptr, b, 32);
            return 0;
        }
        case effCanBeAutomated:   return idx == P_RECORD ? 1 : 0;
        case effGetEffectName:
        case effGetProductString: copyStr (ptr, "The Remixer", 32); return 1;
        case effGetVendorString:  copyStr (ptr, "GlueBus", 32); return 1;
        case effGetVendorVersion: return 10;
        case effGetPlugCategory:  return kPlugCategEffect;
        case effGetVstVersion:    return 2400;
        case effSetProcessPrecision: return val == 0 ? 1 : 0;
        default: return 0;
    }
}
} // namespace

TR_EXPORT AEffect* VSTPluginMain (audioMasterCallback master)
{
    void* mem = std::calloc (1, sizeof (Plugin));        // no operator new: keeps libstdc++ out of the link
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
    fx.numParams = P_COUNT;
    fx.numInputs = 2;
    fx.numOutputs = 2;
    fx.flags = effFlagsCanReplacing;
    fx.ioRatio = 1.f;
    fx.object = p;
    fx.uniqueID = ('T' << 24) | ('R' << 16) | ('M' << 8) | 'X';   // 'TRMX' = 0x54524d58
    fx.version = 10;
    return &fx;
}
