#pragma once
// G-Glue parameter table, shared by every target (desktop plugin, MPC bridge, tests).
// IDs are stable: never rename or reorder them; add new parameters at the end of the enum.
// All values in this file are "plain" values: dB, an index for choice parameters, 0 / 1 for switches.
#include <array>
#include <string>

namespace gglue
{
enum ParamIndex
{
    kThreshold, kMakeup, kAttack, kRelease, kRatio, kScFilter, kMix, kInput, kOutput, kAnalog, kBypass,
    kNumParams
};

enum class ParamType { Float, Choice, Switch };

struct ParamSpec
{
    const char* id;          // stable automation / state ID
    const char* name;        // full name (host automation lane)
    const char* shortName;   // <= 8 chars, for small hardware displays
    ParamType type;
    float min, max, def;     // plain range; for Choice: 0 .. numChoices - 1
    float step;              // 0 = continuous
    const char* unit;
    const char* const* choices;
    int numChoices;
};

// discrete value tables
constexpr float kAttackMs[]   { 0.1f, 0.3f, 1.f, 3.f, 10.f, 30.f };
constexpr float kReleaseSec[] { 0.1f, 0.3f, 0.6f, 1.2f };             // index 4 = AUTO
constexpr int   kReleaseAuto  = 4;
constexpr float kRatios[]     { 2.f, 4.f, 10.f };
constexpr float kScFilterHz[] { 0.f, 30.f, 60.f, 90.f, 120.f, 150.f, 200.f };   // 0 = OFF

const ParamSpec& spec (int index);
int findParam (const std::string& id);                  // -1 if unknown

float clampPlain (int index, float plain);               // clamps and snaps choices / switches
float toNormalized (int index, float plain);             // 0..1
float fromNormalized (int index, float normalized);
std::string formatValue (int index, float plain);        // short, MPC-display friendly: "-12.0 dB", "AUTO", "4:1"
bool parseValue (int index, const std::string& text, float& plain);

using ParamValues = std::array<float, kNumParams>;
ParamValues defaultValues();
} // namespace gglue
