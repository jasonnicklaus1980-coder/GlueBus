// VST2 wrapper for the legacy sampler (instrument). MIDI notes play the pads (see padmap.h for the note map, which is
// an unverified convention). The project is read once, when the plugin opens, from LEGACY_SAMPLER_DIR or
// /sdcard/LegacySampler/project.mc909. The audio callback does no file access and no allocation.
// Limits: MIDI is handled at the start of each block, not at its exact frame; one parameter (Volume).
#include "vst2.h"
#include "../../src/padmap.h"
#include "../../src/project.h"
#include "../../src/sampler.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#define LS_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

constexpr int32_t kUniqueId = 0x4C534D50;   // 'LSMP'
constexpr int kBlock = 512;

struct Plugin {
    AEffect fx;
    mc909::Sampler* sampler = nullptr;
    audioMasterCallback host = nullptr;
    float volume = 0.8f;
    float bufL[kBlock] = {}, bufR[kBlock] = {};
};

const char* projectPath(char* buf, int size)
{
    const char* d = std::getenv("LEGACY_SAMPLER_DIR");
    std::snprintf(buf, (size_t)size, "%s/project.mc909", d && *d ? d : "/sdcard/LegacySampler");
    return buf;
}

// Host thread: reads the project and loads its samples. Nothing here runs on the audio thread.
void loadProject(Plugin* p)
{
    char path[256];
    mc909::Project proj; std::string err;
    if (!mc909::loadProject(projectPath(path, sizeof path), proj, err)) return;   // no project: the plugin stays silent
    std::vector<std::string> warnings;
    mc909::buildSampler(proj, *p->sampler, warnings);
}

void processEvents(Plugin* p, const VstEvents* ev)
{
    if (!ev) return;
    for (int32_t i = 0; i < ev->numEvents; ++i)
    {
        const VstEvent* e = ev->events[i];
        if (!e || e->type != kVstMidiType) continue;
        const VstMidiEvent* m = (const VstMidiEvent*)e;
        const uint8_t status = (uint8_t)m->midiData[0] & 0xF0;
        const uint8_t note = (uint8_t)m->midiData[1], vel = (uint8_t)m->midiData[2];
        const int pad = mc909::padForNote(note);
        if (pad < 0) continue;
        if (status == 0x90 && vel > 0) p->sampler->noteOn(pad, vel / 127.f);
        else if (status == 0x80 || (status == 0x90 && vel == 0)) p->sampler->noteOff(pad);
    }
}

intptr_t dispatcher(AEffect* fx, int32_t op, int32_t index, intptr_t value, void* ptr, float opt)
{
    (void)value;
    Plugin* p = (Plugin*)fx->object;
    switch (op)
    {
    case effOpen: loadProject(p); return 0;
    case effClose: delete p->sampler; p->sampler = nullptr; delete p; return 0;
    case effSetSampleRate: p->sampler->setRate((double)opt); return 0;
    case effGetParamName: if (index == 0 && ptr) std::strcpy((char*)ptr, "Volume"); return 0;
    case effGetParamDisplay:
        if (index == 0 && ptr) std::snprintf((char*)ptr, 32, "%d%%", (int)(p->volume * 100.f + 0.5f));
        return 0;
    case effGetEffectName: if (ptr) std::strcpy((char*)ptr, "Legacy Sampler"); return 1;
    case effGetProductString: if (ptr) std::strcpy((char*)ptr, "Legacy Sampler"); return 1;
    case effGetVendorString: if (ptr) std::strcpy((char*)ptr, "Local build"); return 1;
    case effGetVendorVersion: return 1000;
    case effGetPlugCategory: return kPlugCategSynth;
    case effGetVstVersion: return 2400;
    case effGetTailSize: return 0;
    case effGetProgramName: if (ptr) std::strcpy((char*)ptr, "Default"); return 0;
    case effGetProgramNameIndexed: if (index == 0 && ptr) { std::strcpy((char*)ptr, "Default"); return 1; } return 0;
    case effProcessEvents: processEvents(p, (const VstEvents*)ptr); return 1;
    case effCanBeAutomated: return index == 0 ? 1 : 0;
    case effCanDo:
        if (ptr && (std::strcmp((const char*)ptr, "receiveVstEvents") == 0 || std::strcmp((const char*)ptr, "receiveVstMidiEvent") == 0)) return 1;
        return 0;
    default: return 0;
    }
}

void setParameter(AEffect* fx, int32_t index, float v)
{
    Plugin* p = (Plugin*)fx->object;
    if (index == 0) p->volume = v < 0.f ? 0.f : v > 1.f ? 1.f : v;
}

float getParameter(AEffect* fx, int32_t index)
{
    Plugin* p = (Plugin*)fx->object;
    return index == 0 ? p->volume : 0.f;
}

void processReplacing(AEffect* fx, float** in, float** out, int32_t n)
{
    (void)in;
    Plugin* p = (Plugin*)fx->object;
    for (int32_t s = 0; s < n; s += kBlock)
    {
        const int m = n - s < kBlock ? (int)(n - s) : kBlock;
        p->sampler->render(p->bufL, p->bufR, m);
        for (int i = 0; i < m; ++i)
        {
            out[0][s + i] = p->bufL[i] * p->volume;
            out[1][s + i] = p->bufR[i] * p->volume;
        }
    }
}

}  // namespace

LS_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    Plugin* p = new (std::nothrow) Plugin();
    if (!p) return nullptr;
    p->sampler = new (std::nothrow) mc909::Sampler(44100.0);
    if (!p->sampler) { delete p; return nullptr; }
    p->host = host;
    AEffect* fx = &p->fx;
    std::memset(fx, 0, sizeof(*fx));
    fx->magic = kEffectMagic;
    fx->dispatcher = dispatcher;
    fx->process = processReplacing;
    fx->processReplacing = processReplacing;
    fx->setParameter = setParameter;
    fx->getParameter = getParameter;
    fx->numPrograms = 1;
    fx->numParams = 1;
    fx->numInputs = 0;
    fx->numOutputs = 2;
    fx->flags = effFlagsCanReplacing | effFlagsIsSynth;
    fx->uniqueID = kUniqueId;
    fx->version = 1000;
    fx->object = p;
    return fx;
}
