#include "GGlueMPCBridge.h"
#include "../DSP/GGlueEngine.h"
#include "../Presets/GGluePresets.h"
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

using namespace gglue;

struct gglue_mpc
{
    Engine engine;
    std::atomic<float> values[kNumParams];
    std::unique_ptr<PresetLibrary> library;
    PluginState state;
    double rate = 44100.0;
    int channels = 2;

    ParamValues snapshot() const
    {
        ParamValues v;
        for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = values[i].load (std::memory_order_relaxed);
        return v;
    }
    void store (const ParamValues& v)
    {
        for (int i = 0; i < kNumParams; ++i) values[i].store (clampPlain (i, v[(size_t) i]), std::memory_order_relaxed);
    }
};

namespace
{
bool valid (int i) { return i >= 0 && i < kNumParams; }
}

extern "C" {

gglue_mpc* gglue_mpc_create (const char* folder)
{
    auto* m = new (std::nothrow) gglue_mpc();
    if (! m) return nullptr;
    m->store (defaultValues());
    m->library = std::make_unique<PresetLibrary> (folder && *folder ? std::filesystem::path (folder) : std::filesystem::path ("G-Glue Presets"));
    m->engine.setParameters (defaultValues());
    m->engine.prepare (m->rate, m->channels);
    return m;
}

void gglue_mpc_destroy (gglue_mpc* m) { delete m; }

void gglue_mpc_prepare (gglue_mpc* m, double rate, int channels)
{
    if (! m) return;
    m->rate = rate; m->channels = channels;
    m->engine.setParameters (m->snapshot());
    m->engine.prepare (rate, channels);
}

void gglue_mpc_process (gglue_mpc* m, float* const* ch, int numCh, int n)
{
    if (! m) return;
    for (int i = 0; i < kNumParams; ++i) m->engine.setParameter (i, m->values[i].load (std::memory_order_relaxed));
    m->engine.process (ch, numCh, n);
}

int gglue_mpc_latency_samples (const gglue_mpc* m) { return m ? m->engine.getLatencySamples() : 0; }

int gglue_mpc_param_count (void) { return kNumParams; }
const char* gglue_mpc_param_id (int i) { return valid (i) ? spec (i).id : ""; }
const char* gglue_mpc_param_name (int i) { return valid (i) ? spec (i).name : ""; }
const char* gglue_mpc_param_short_name (int i) { return valid (i) ? spec (i).shortName : ""; }
int gglue_mpc_param_steps (int i) { return valid (i) && spec (i).type != ParamType::Float ? spec (i).numChoices : 0; }

float gglue_mpc_get_normalized (const gglue_mpc* m, int i)
{
    return m && valid (i) ? toNormalized (i, m->values[i].load (std::memory_order_relaxed)) : 0.f;
}

void gglue_mpc_set_normalized (gglue_mpc* m, int i, float v)
{
    if (! m || ! valid (i)) return;
    m->values[i].store (fromNormalized (i, v), std::memory_order_relaxed);
    if (i != kBypass) m->state.modified = true;
}

int gglue_mpc_param_text (const gglue_mpc* m, int i, char* buf, int size)
{
    if (! m || ! valid (i) || ! buf || size <= 0) return 0;
    const std::string t = formatValue (i, m->values[i].load (std::memory_order_relaxed));
    std::strncpy (buf, t.c_str(), (size_t) size - 1);
    buf[size - 1] = 0;
    return (int) std::strlen (buf);
}

int gglue_mpc_preset_count (const gglue_mpc* m) { return m ? (int) m->library->presets().size() : 0; }

const char* gglue_mpc_preset_name (const gglue_mpc* m, int i)
{
    return m && i >= 0 && i < gglue_mpc_preset_count (m) ? m->library->presets()[(size_t) i].name.c_str() : "";
}

const char* gglue_mpc_preset_category (const gglue_mpc* m, int i)
{
    return m && i >= 0 && i < gglue_mpc_preset_count (m) ? m->library->presets()[(size_t) i].category.c_str() : "";
}

int gglue_mpc_load_preset (gglue_mpc* m, int i)
{
    if (! m || i < 0 || i >= gglue_mpc_preset_count (m)) return 0;
    const Preset& p = m->library->presets()[(size_t) i];
    ParamValues v = p.values;
    v[kBypass] = m->values[kBypass].load();                  // loading a preset never changes bypass
    m->store (v);
    m->state.presetName = p.name;
    m->state.modified = false;
    return 1;
}

int gglue_mpc_save_preset (gglue_mpc* m, const char* name, int overwrite)
{
    if (! m || ! name) return 0;
    if (! m->library->save (name, "USER", m->snapshot(), overwrite != 0)) return 0;
    m->state.presetName = sanitizePresetName (name);
    m->state.modified = false;
    return 1;
}

int gglue_mpc_get_state (const gglue_mpc* m, char* buf, int size)
{
    if (! m) return 0;
    PluginState s = m->state;
    s.current = m->snapshot();
    const std::string t = stateToText (s);
    if (buf && size > 0)
    {
        const size_t n = t.size() < (size_t) size - 1 ? t.size() : (size_t) size - 1;
        std::memcpy (buf, t.data(), n);
        buf[n] = 0;
    }
    return (int) t.size() + 1;
}

int gglue_mpc_set_state (gglue_mpc* m, const char* text, int len)
{
    if (! m || ! text || len <= 0) return 0;
    PluginState s;
    if (! stateFromText (std::string (text, (size_t) len), s)) return 0;
    m->state = s;
    m->store (s.current);
    return 1;
}

void gglue_mpc_switch_ab (gglue_mpc* m)
{
    if (! m) return;
    m->state.current = m->snapshot();
    m->state.switchSlot();
    m->store (m->state.current);
}

char gglue_mpc_active_slot (const gglue_mpc* m) { return m ? m->state.activeSlot : 'A'; }

float gglue_mpc_meter (const gglue_mpc* m, int which)
{
    if (! m) return 0.f;
    switch (which)
    {
        case GGLUE_METER_INPUT_DB: return m->engine.inputMeterDb.load();
        case GGLUE_METER_OUTPUT_DB: return m->engine.outputMeterDb.load();
        default: return m->engine.gainReductionDb.load();
    }
}
} // extern "C"
