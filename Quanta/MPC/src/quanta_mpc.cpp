// Quanta for MPC OS: a thin VST2 wrapper around the shared engine (../../DSP).
//
// UNOFFICIAL, as with G-Glue: MPC OS loads Linux VST2 plugins registered in MPC.settings, which needs root access.
// The wrapper does three things: VST2 entry points, the parameters (names and display text), and the project state.
// Threads: the host's UI thread calls setParameter / dispatcher; the audio thread calls processReplacing only.
#include "vst2.h"
#include "../../DSP/QuantaEngine.h"
#include "../../DSP/QuantaMath.h"
#include "../../DSP/QuantaPresets.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>

#define QN_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr int kBlock = 1024;   // the audio thread works in blocks this size, so no buffer is allocated while processing
constexpr int kHistory = 32;   // undo and redo steps kept
constexpr long long kEditGapMs = 800;   // edits closer together than this are one gesture (one undo step)
constexpr long long kNoEdit = -1000000;

struct Plugin {
    AEffect fx;
    quanta::Engine engine;
    audioMasterCallback host = nullptr;
    int32_t lastFrame[quanta::P_COUNT] = {};  // meter frame last sent to the host (-1 = none)
    float slot[2][quanta::kControlCount] = {};  // settings for A and B; the active one is the engine's
    int abSlot = 0;                 // which slot is active (0 = A)
    bool bypass = false;            // requested by the host (effSetBypass) or the Bypass parameter
    float wet = 1.f;                // 1 = processed, 0 = bypassed; moves toward the request over about 10 ms
    float wetStep = 1.f / 480.f;    // set from the sample rate
    float dryL[kBlock] = {}, dryR[kBlock] = {};   // input kept for the bypass crossfade (audio thread only)
    float undoStack[kHistory][quanta::kControlCount] = {};   // sound-control states before each edit (host thread only)
    float redoStack[kHistory][quanta::kControlCount] = {};
    int undoN = 0, redoN = 0;
    long long lastEditMs = kNoEdit;
    char chunk[64];   // state, written on the host thread only
};

inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

constexpr int32_t kUniqueId = 0x516E7431;   // 'Qnt1'
constexpr char kStateMagic[4] = {'Q', 'N', 'T', '1'};

void copyStr(char* dst, const char* src, int max) {
    int k = 0;
    while (src[k] && k < max - 1) { dst[k] = src[k]; ++k; }
    dst[k] = 0;
}

// Session switches. Only the host thread calls these; the audio thread reads bypass and wet.
void setBypass(Plugin* p, bool on) { p->bypass = on; }

void setAB(Plugin* p, int b) {
    if (b == p->abSlot) return;
    for (int i = 0; i < quanta::kControlCount; ++i) p->slot[p->abSlot][i] = p->engine.getParam(i);   // keep the one we leave
    for (int i = 0; i < quanta::kControlCount; ++i) p->engine.setParam(i, p->slot[b][i]);
    p->abSlot = b;
}

void captureSlots(Plugin* p) {   // at open both slots start from the defaults, so B is never empty
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < quanta::kControlCount; ++i) p->slot[s][i] = p->engine.getParam(i);
}

// Undo and redo: the sound controls are saved before each edit. Host thread only.
long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void currentState(const Plugin* p, float* out) {
    for (int i = 0; i < quanta::kControlCount; ++i) out[i] = p->engine.getParam(i);
}

void pushState(float (*stack)[quanta::kControlCount], int& n, const float* state) {
    if (n == kHistory) { std::memmove(stack[0], stack[1], sizeof(float) * quanta::kControlCount * (kHistory - 1)); --n; }
    std::memcpy(stack[n], state, sizeof(float) * quanta::kControlCount);
    ++n;
}

// Called before a change to the controls. A change soon after the last one joins its undo step.
void beforeEdit(Plugin* p) {
    long long t = nowMs();
    if (t - p->lastEditMs > kEditGapMs) {
        float s[quanta::kControlCount];
        currentState(p, s);
        pushState(p->undoStack, p->undoN, s);
        p->redoN = 0;   // a new edit ends any redo
    }
    p->lastEditMs = t;
}

void undo(Plugin* p) {
    if (p->undoN == 0) return;
    float cur[quanta::kControlCount];
    currentState(p, cur);
    pushState(p->redoStack, p->redoN, cur);
    --p->undoN;
    for (int i = 0; i < quanta::kControlCount; ++i) p->engine.setParam(i, p->undoStack[p->undoN][i]);
    p->lastEditMs = kNoEdit;
}

void redo(Plugin* p) {
    if (p->redoN == 0) return;
    float cur[quanta::kControlCount];
    currentState(p, cur);
    pushState(p->undoStack, p->undoN, cur);
    --p->redoN;
    for (int i = 0; i < quanta::kControlCount; ++i) p->engine.setParam(i, p->redoStack[p->redoN][i]);
    p->lastEditMs = kNoEdit;
}

int presetIndexOf(float v) {
    int i = (int)(v * (float)(quanta::kPresetCount - 1) + 0.5f);
    return i < 0 ? 0 : i >= quanta::kPresetCount ? quanta::kPresetCount - 1 : i;
}

void formatParam(int index, float v, char* out, int max) {
    if (index == quanta::P_UNDO) {
        copyStr(out, "Undo", max);
    } else if (index == quanta::P_REDO) {
        copyStr(out, "Redo", max);
    } else if (index == quanta::P_BYPASS) {
        copyStr(out, v >= 0.5f ? "Bypassed" : "Active", max);
    } else if (index == quanta::P_AB) {
        copyStr(out, v >= 0.5f ? "B" : "A", max);
    } else if (index == quanta::P_PRESET) {
        copyStr(out, quanta::presetName(presetIndexOf(v)), max);
    } else if (index == quanta::P_OUTPUT) {
        std::snprintf(out, (size_t)max, "%+.1f dB", (double)((v - 0.5f) * 24.f));
    } else if (index == quanta::P_METER_IN || index == quanta::P_METER_OUT ||
               index == quanta::P_METER_RMS_IN || index == quanta::P_METER_RMS_OUT) {
        std::snprintf(out, (size_t)max, "%.1f dB", (double)(v * 60.f - 60.f));          // meters: -60 .. 0 dB
    } else if (index == quanta::P_METER_LUFS) {
        std::snprintf(out, (size_t)max, "%.1f LUFS", (double)(v * 60.f - 60.f));
    } else if (index == quanta::P_METER_GR) {
        std::snprintf(out, (size_t)max, "%.1f dB", (double)(-v * 20.f));                  // reduction: 0 .. -20 dB
    } else if (index == quanta::P_METER_CORR) {
        std::snprintf(out, (size_t)max, "%+.2f", (double)(2.f * v - 1.f));
    } else if (index == quanta::P_MONOBASS) {
        copyStr(out, v >= 0.5f ? "On" : "Off", max);
    } else if (index == quanta::P_TONE) {
        std::snprintf(out, (size_t)max, "%+.0f", (double)((v - 0.5f) * 200.f));
    } else {
        std::snprintf(out, (size_t)max, "%d%%", (int)(v * 100.f + 0.5f));
    }
}

float getParameter(AEffect* fx, int32_t index);

intptr_t dispatcher(AEffect* fx, int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    Plugin* p = (Plugin*)fx->object;
    switch (op) {
    case effOpen: captureSlots(p); return 0;
    case effClose: delete p; return 0;
    case effSetSampleRate:
        p->engine.prepare((double)opt);
        p->wetStep = 1.f / (0.010f * (opt > 1000.f ? opt : 48000.f));
        return 0;
    case effSetBlockSize: return 0;
    case effSetBypass: setBypass(p, value != 0); return 1;
    case effMainsChanged: if (!value) p->engine.reset(); return 0;
    case effGetParamName:
        if (index >= 0 && index < quanta::P_COUNT && ptr) copyStr((char*)ptr, quanta::kParams[index].name, 32);
        return 0;
    case effGetParamLabel: if (ptr) ((char*)ptr)[0] = 0; return 0;
    case effGetProgram: return presetIndexOf(p->engine.getParam(quanta::P_PRESET));
    case effSetProgram: beforeEdit(p); p->engine.loadPreset((int)value); return 0;
    case effGetProgramName:
        if (ptr) copyStr((char*)ptr, quanta::presetName(presetIndexOf(p->engine.getParam(quanta::P_PRESET))), 24);
        return 0;
    case effGetProgramNameIndexed:
        if (index < 0 || index >= quanta::kPresetCount || !ptr) return 0;
        copyStr((char*)ptr, quanta::presetName(index), 24);
        return 1;
    case effSetProgramName: return 0;
    case effGetParamDisplay:
        if (index >= 0 && index < quanta::P_COUNT && ptr) formatParam(index, getParameter(fx, index), (char*)ptr, 64);
        return 0;
    case effGetChunk: {
        char* b = p->chunk;
        std::memcpy(b, kStateMagic, 4);
        for (int i = 0; i < quanta::kControlCount; ++i) { float v = p->engine.getParam(i); std::memcpy(b + 4 + 4 * i, &v, 4); }
        *(void**)ptr = b;
        return 4 + 4 * quanta::kControlCount;
    }
    case effSetChunk: {
        const char* b = (const char*)ptr;
        if (!b || value < 4 + 4 * quanta::kControlCount || std::memcmp(b, kStateMagic, 4) != 0) return 0;
        beforeEdit(p);
        for (int i = 0; i < quanta::kControlCount; ++i) { float v; std::memcpy(&v, b + 4 + 4 * i, 4); p->engine.setParam(i, v); }
        return 1;
    }
    case effGetEffectName: if (ptr) copyStr((char*)ptr, "Quanta", 32); return 1;
    case effGetVendorString: if (ptr) copyStr((char*)ptr, "Velvet", 64); return 1;
    case effGetProductString: if (ptr) copyStr((char*)ptr, "Quanta", 64); return 1;
    case effGetVendorVersion: return 1000;
    case effGetPlugCategory: return kPlugCategEffect;
    case effGetVstVersion: return 2400;
    case effGetTailSize: return 0;
    case effCanBeAutomated: return index >= 0 && index < quanta::kAutomatableCount ? 1 : 0;
    case effCanDo: return 0;
    default: return 0;
    }
}

void setParameter(AEffect* fx, int32_t index, float v) {
    Plugin* p = (Plugin*)fx->object;
    // Undo and redo act on every call: the skin sends one call per press
    if (index == quanta::P_UNDO) undo(p);
    else if (index == quanta::P_REDO) redo(p);
    else if (index == quanta::P_BYPASS) setBypass(p, v >= 0.5f);
    else if (index == quanta::P_AB) setAB(p, v >= 0.5f ? 1 : 0);
    else if (index == quanta::P_PRESET) { beforeEdit(p); p->engine.loadPreset(presetIndexOf(v)); }   // choosing a preset sets its values
    else if (index >= 0 && index < quanta::kControlCount) {
        float clamped = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
        if (p->engine.getParam(index) != clamped) { beforeEdit(p); p->engine.setParam(index, clamped); }
    }
}

// The meters are read from the engine (written by the audio thread, read here as plain floats)
float meterValue(const quanta::Engine& e, int index) {
    switch (index) {
    case quanta::P_METER_IN:   return clamp01((20.f * quanta::qlog10(e.inputPeak() + 1e-9f) + 60.f) / 60.f);
    case quanta::P_METER_OUT:  return clamp01((20.f * quanta::qlog10(e.outputPeak() + 1e-9f) + 60.f) / 60.f);
    case quanta::P_METER_GR:   return clamp01(-e.gainReductionDbMax() / 20.f);
    case quanta::P_METER_CORR: return clamp01(0.5f + 0.5f * e.correlation());
    case quanta::P_METER_RMS_IN:  return clamp01((e.inputRmsDb() + 60.f) / 60.f);
    case quanta::P_METER_RMS_OUT: return clamp01((e.outputRmsDb() + 60.f) / 60.f);
    case quanta::P_METER_LUFS:    return clamp01((e.outputLufs() + 60.f) / 60.f);
    default: return 0.f;
    }
}

float getParameter(AEffect* fx, int32_t index) {
    Plugin* p = (Plugin*)fx->object;
    if (index == quanta::P_BYPASS) return p->bypass ? 1.f : 0.f;
    if (index == quanta::P_AB) return (float)p->abSlot;
    if (index < 0 || index >= quanta::P_COUNT) return 0.f;
    return index < quanta::kControlCount ? p->engine.getParam(index) : meterValue(p->engine, index);
}

void processReplacing(AEffect* fx, float** in, float** out, int32_t n) {
    Plugin* p = (Plugin*)fx->object;
    for (int32_t start = 0; start < n; start += kBlock) {
        const int m = n - start < kBlock ? (int)(n - start) : kBlock;
        std::memcpy(p->dryL, in[0] + start, sizeof(float) * (size_t)m);
        std::memcpy(p->dryR, in[1] + start, sizeof(float) * (size_t)m);
        float* outL = out[0] + start;
        float* outR = out[1] + start;
        std::memcpy(outL, p->dryL, sizeof(float) * (size_t)m);
        std::memcpy(outR, p->dryR, sizeof(float) * (size_t)m);
        p->engine.process(outL, outR, m);   // the engine keeps running while bypassed, so its state is warm when bypass ends
        const float target = p->bypass ? 0.f : 1.f;
        for (int i = 0; i < m; ++i) {       // crossfade: exact input at 0, exact processed signal at 1
            if (p->wet < target) p->wet = (p->wet + p->wetStep < target) ? p->wet + p->wetStep : target;
            else if (p->wet > target) p->wet = (p->wet - p->wetStep > target) ? p->wet - p->wetStep : target;
            const float w = p->wet;
            outL[i] = p->dryL[i] * (1.f - w) + outL[i] * w;
            outR[i] = p->dryR[i] * (1.f - w) + outR[i] * w;
        }
    }
    // Meters go to the screen only when the picture would change: one update per filmstrip frame (0..127)
    if (!p->host) return;
    for (int i = quanta::P_METER_IN; i < quanta::P_COUNT; ++i) {
        int32_t frame = (int32_t)(meterValue(p->engine, i) * 127.f + 0.5f);
        if (frame != p->lastFrame[i]) {
            p->lastFrame[i] = frame;
            p->host(fx, audioMasterAutomate, i, 0, nullptr, (float)frame / 127.f);
        }
    }
}

}  // namespace

QN_EXPORT AEffect* VSTPluginMain(audioMasterCallback host) {
    (void)host;
    Plugin* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->host = host;
    for (int i = 0; i < quanta::P_COUNT; ++i) p->lastFrame[i] = -1;
    p->engine.prepare(48000.0);
    AEffect* fx = &p->fx;
    std::memset(fx, 0, sizeof(*fx));
    fx->magic = kEffectMagic;
    fx->dispatcher = dispatcher;
    fx->process = processReplacing;
    fx->processReplacing = processReplacing;
    fx->setParameter = setParameter;
    fx->getParameter = getParameter;
    fx->numPrograms = quanta::kPresetCount;
    fx->numParams = quanta::P_COUNT;
    fx->numInputs = 2;
    fx->numOutputs = 2;
    fx->flags = effFlagsCanReplacing | effFlagsProgramChunks;
    fx->uniqueID = kUniqueId;
    fx->version = 1000;
    fx->object = p;
    return fx;
}
