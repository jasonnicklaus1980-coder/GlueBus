// Factory presets: the control values for each preset (0..1, as the host sees them). One table shared by every build.
#pragma once
#include "QuantaEngine.h"

namespace quanta {

constexpr int kPresetCount = 13;   // index 0 is Custom (changes nothing); 1..12 are the factory presets

// Name of preset i (0..kPresetCount-1), at most 23 characters.
const char* presetName(int i);

// Writes the sound control values of preset i into out[0..P_PRESET-1]. Returns false for Custom or a bad index.
bool presetValues(int i, float* out);

}  // namespace quanta
