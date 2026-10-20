#include "QuantaPresets.h"

namespace quanta {

namespace {
// Particle, Tone, Density, Glue, Width, Low End, Air, Mix, Output, Mono Bass
struct Preset {
    const char* name;
    float v[P_PRESET];          // the ten sound controls, Particle .. Mono Bass
};

// Values are 0..1. 0.5 is neutral for Tone, Width and Output. Particle and the other amounts are kept modest so each
// preset is a starting point rather than a finished master.
const Preset kPresets[kPresetCount] = {
    {"Custom",                {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}},
    {"Hip-Hop Mix Bus",       {0.35f, 0.55f, 0.25f, 0.35f, 0.50f, 0.45f, 0.35f, 1.00f, 0.50f, 0.f}},
    {"R&B Mix Bus",           {0.30f, 0.50f, 0.20f, 0.30f, 0.55f, 0.35f, 0.40f, 1.00f, 0.50f, 0.f}},
    {"Vintage Soul",          {0.30f, 0.40f, 0.45f, 0.40f, 0.45f, 0.30f, 0.10f, 0.90f, 0.50f, 0.f}},
    {"Drum Bus",              {0.25f, 0.55f, 0.30f, 0.50f, 0.50f, 0.40f, 0.45f, 1.00f, 0.50f, 0.f}},
    {"Kick and Bass",         {0.20f, 0.45f, 0.20f, 0.20f, 0.45f, 0.60f, 0.10f, 1.00f, 0.50f, 1.f}},
    {"Vocal Bus",             {0.30f, 0.55f, 0.25f, 0.40f, 0.50f, 0.25f, 0.55f, 1.00f, 0.50f, 0.f}},
    {"Instrument Bus",        {0.25f, 0.50f, 0.30f, 0.30f, 0.60f, 0.30f, 0.30f, 1.00f, 0.50f, 0.f}},
    {"Sampled Music",         {0.30f, 0.40f, 0.40f, 0.30f, 0.45f, 0.30f, 0.25f, 0.85f, 0.50f, 0.f}},
    {"Warm and Dense",        {0.45f, 0.35f, 0.60f, 0.50f, 0.50f, 0.50f, 0.05f, 1.00f, 0.50f, 0.f}},
    {"Clean and Open",        {0.20f, 0.60f, 0.05f, 0.10f, 0.65f, 0.20f, 0.60f, 1.00f, 0.50f, 0.f}},
    {"Full Stereo Mix",       {0.35f, 0.50f, 0.25f, 0.35f, 0.60f, 0.35f, 0.35f, 1.00f, 0.50f, 1.f}},
    {"Gentle Master",         {0.15f, 0.50f, 0.10f, 0.15f, 0.50f, 0.15f, 0.20f, 0.80f, 0.50f, 1.f}},
};
}  // namespace

const char* presetName(int i) { return (i < 0 || i >= kPresetCount) ? "" : kPresets[i].name; }

bool presetValues(int i, float* out) {
    if (i < 1 || i >= kPresetCount) return false;
    for (int k = 0; k < P_PRESET; ++k) out[k] = kPresets[i].v[k];
    return true;
}

}  // namespace quanta
