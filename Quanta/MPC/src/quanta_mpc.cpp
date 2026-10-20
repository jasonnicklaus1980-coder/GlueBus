// Quanta for MPC OS: a thin VST2 wrapper around the shared engine (../../DSP).
//
// UNOFFICIAL, as with G-Glue: MPC OS loads Linux VST2 plugins registered in MPC.settings, which needs root access.
// The wrapper does three things: VST2 entry points, the parameters (names and display text), and the project state.
// Threads: the host's UI thread calls setParameter / dispatcher; the audio thread calls processReplacing only.
#include "vst2.h"
#include "../../DSP/QuantaEngine.h"
#include "../../DSP/QuantaMath.h"
#include "../../DSP/QuantaPresets.h"

#include <cstdio>
#include <cstring>
#include <new>

#define QN_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

struct Plugin {
    AEffect fx;
    quanta::Engine engine;
    audioMasterCallback host = nullptr;
    int32_t lastFrame[quanta::P_COUNT] = {};  // meter frame last sent to the host (-1 = none)
    bool bypass = false;   // set by the host (effSetBypass); the audio thread passes the input straight through
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

int presetIndexOf(float v) {
    int i = (int)(v * (float)(quanta::kPresetCount - 1) + 0.5f);
    return i < 0 ? 0 : i >= quanta::kPresetCount ? quanta::kPresetCount - 1 : i;
}

void formatParam(int index, float v, char* out, int max) {
    if (index == quanta::P_PRESET) {
        copyStr(out, quanta::presetName(presetIndexOf(v)), max);
    } else if (index == quanta::P_OUTPUT) {
        std::snprintf(out, (size_t)max, "%+.1f dB", (double)((v - 0.5f) * 24.f));
    } else if (index == quanta::P_MONOBASS) {
        copyStr(out, v >= 0.5f ? "On" : "Off", max);
    } else if (index == quanta::P_TONE) {
        std::snprintf(out, (size_t)max, "%+.0f", (double)((v - 0.5f) * 200.f));
    } else {
        std::snprintf(out, (size_t)max, "%d%%", (int)(v * 100.f + 0.5f));
    }
}

intptr_t dispatcher(AEffect* fx, int32_t op, int32_t index, intptr_t value, void* ptr, float opt) {
    Plugin* p = (Plugin*)fx->object;
    switch (op) {
    case effOpen: return 0;
    case effClose: delete p; return 0;
    case effSetSampleRate: p->engine.prepare((double)opt); return 0;
    case effSetBlockSize: return 0;
    case effSetBypass: p->bypass = value != 0; return 1;
    case effMainsChanged: if (!value) p->engine.reset(); return 0;
    case effGetParamName:
        if (index >= 0 && index < quanta::P_COUNT && ptr) copyStr((char*)ptr, quanta::kParams[index].name, 32);
        return 0;
    case effGetParamLabel: if (ptr) ((char*)ptr)[0] = 0; return 0;
    case effGetProgram: return presetIndexOf(p->engine.getParam(quanta::P_PRESET));
    case effSetProgram: p->engine.loadPreset((int)value); return 0;
    case effGetProgramName:
        if (ptr) copyStr((char*)ptr, quanta::presetName(presetIndexOf(p->engine.getParam(quanta::P_PRESET))), 24);
        return 0;
    case effGetProgramNameIndexed:
        if (index < 0 || index >= quanta::kPresetCount || !ptr) return 0;
        copyStr((char*)ptr, quanta::presetName(index), 24);
        return 1;
    case effSetProgramName: return 0;
    case effGetParamDisplay:
        if (index >= 0 && index < quanta::P_COUNT && ptr) formatParam(index, p->engine.getParam(index), (char*)ptr, 64);
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
    case effCanBeAutomated: return index >= 0 && index < quanta::kControlCount ? 1 : 0;
    case effCanDo: return 0;
    default: return 0;
    }
}

void setParameter(AEffect* fx, int32_t index, float v) {
    Plugin* p = (Plugin*)fx->object;
    if (index == quanta::P_PRESET) p->engine.loadPreset(presetIndexOf(v));   // choosing a preset sets its values
    else if (index >= 0 && index < quanta::kControlCount) p->engine.setParam(index, v);
}

// The meters are read from the engine (written by the audio thread, read here as plain floats)
float meterValue(const quanta::Engine& e, int index) {
    switch (index) {
    case quanta::P_METER_IN:   return clamp01((20.f * quanta::qlog10(e.inputPeak() + 1e-9f) + 60.f) / 60.f);
    case quanta::P_METER_OUT:  return clamp01((20.f * quanta::qlog10(e.outputPeak() + 1e-9f) + 60.f) / 60.f);
    case quanta::P_METER_GR:   return clamp01(-e.gainReductionDbMax() / 20.f);
    case quanta::P_METER_CORR: return clamp01(0.5f + 0.5f * e.correlation());
    default: return 0.f;
    }
}

float getParameter(AEffect* fx, int32_t index) {
    Plugin* p = (Plugin*)fx->object;
    if (index < 0 || index >= quanta::P_COUNT) return 0.f;
    return index < quanta::kControlCount ? p->engine.getParam(index) : meterValue(p->engine, index);
}

void processReplacing(AEffect* fx, float** in, float** out, int32_t n) {
    Plugin* p = (Plugin*)fx->object;
    if (out[0] != in[0]) std::memcpy(out[0], in[0], sizeof(float) * (size_t)n);
    if (out[1] != in[1]) std::memcpy(out[1], in[1], sizeof(float) * (size_t)n);
    if (p->bypass) return;
    p->engine.process(out[0], out[1], n);
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
