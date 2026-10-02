#include "GGlueParameters.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gglue
{
namespace
{
const char* const kAttackNames[]   { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" };
const char* const kReleaseNames[]  { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "AUTO" };
const char* const kRatioNames[]    { "2:1", "4:1", "10:1" };
const char* const kScFilterNames[] { "OFF", "30 Hz", "60 Hz", "90 Hz", "120 Hz", "150 Hz", "200 Hz" };
const char* const kSwitchNames[]   { "OFF", "ON" };

const ParamSpec kSpecs[kNumParams] {
    { "threshold", "Threshold", "Thresh",  ParamType::Float,  -30.f, 10.f, -10.f, 0.f, "dB", nullptr, 0 },
    { "makeup",    "Makeup",    "Makeup",  ParamType::Float,    0.f, 24.f,   0.f, 0.f, "dB", nullptr, 0 },
    { "attack",    "Attack",    "Attack",  ParamType::Choice,   0.f,  5.f,   3.f, 1.f, "",   kAttackNames, 6 },
    { "release",   "Release",   "Release", ParamType::Choice,   0.f,  4.f,   4.f, 1.f, "",   kReleaseNames, 5 },
    { "ratio",     "Ratio",     "Ratio",   ParamType::Choice,   0.f,  2.f,   1.f, 1.f, "",   kRatioNames, 3 },
    { "scfilter",  "SC Filter", "SC HPF",  ParamType::Choice,   0.f,  6.f,   0.f, 1.f, "",   kScFilterNames, 7 },
    { "mix",       "Mix",       "Mix",     ParamType::Float,    0.f, 100.f, 100.f, 0.f, "%", nullptr, 0 },
    { "input",     "Input",     "Input",   ParamType::Float,  -24.f, 24.f,   0.f, 0.f, "dB", nullptr, 0 },
    { "output",    "Output",    "Output",  ParamType::Float,  -24.f, 24.f,   0.f, 0.f, "dB", nullptr, 0 },
    { "analog",    "Analog",    "Analog",  ParamType::Switch,   0.f,  1.f,   0.f, 1.f, "",   kSwitchNames, 2 },
    { "bypass",    "Bypass",    "Bypass",  ParamType::Switch,   0.f,  1.f,   0.f, 1.f, "",   kSwitchNames, 2 },
};

bool iequals (const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
        if (std::tolower ((unsigned char) *a) != std::tolower ((unsigned char) *b)) return false;
    return *a == *b;
}
} // namespace

const ParamSpec& spec (int index) { return kSpecs[index]; }

int findParam (const std::string& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kSpecs[i].id) return i;
    return -1;
}

float clampPlain (int index, float plain)
{
    const ParamSpec& s = kSpecs[index];
    if (! std::isfinite (plain)) return s.def;
    plain = plain < s.min ? s.min : (plain > s.max ? s.max : plain);
    if (s.type != ParamType::Float) plain = std::round (plain);
    return plain;
}

float toNormalized (int index, float plain)
{
    const ParamSpec& s = kSpecs[index];
    return (clampPlain (index, plain) - s.min) / (s.max - s.min);
}

float fromNormalized (int index, float normalized)
{
    const ParamSpec& s = kSpecs[index];
    normalized = normalized < 0.f ? 0.f : (normalized > 1.f ? 1.f : normalized);
    return clampPlain (index, s.min + normalized * (s.max - s.min));
}

std::string formatValue (int index, float plain)
{
    const ParamSpec& s = kSpecs[index];
    plain = clampPlain (index, plain);
    char b[32];
    if (s.type != ParamType::Float) return s.choices[(int) plain];
    if (index == kMix) std::snprintf (b, sizeof b, "%.0f %%", (double) plain);
    else if (index == kMakeup) std::snprintf (b, sizeof b, "+%.1f dB", (double) plain);
    else std::snprintf (b, sizeof b, "%+.1f dB", (double) plain);
    if (std::strcmp (b, "-0.0 dB") == 0) return "+0.0 dB";
    return b;
}

bool parseValue (int index, const std::string& text, float& plain)
{
    const ParamSpec& s = kSpecs[index];
    if (s.type != ParamType::Float)
    {
        for (int c = 0; c < s.numChoices; ++c)
            if (iequals (text.c_str(), s.choices[c])) { plain = (float) c; return true; }
        // also accept the bare number: "4" for 4:1, "90" for 90 Hz, "0.6" for 0.6 s
        char* end = nullptr; const double v = std::strtod (text.c_str(), &end);
        if (end == text.c_str()) return false;
        int best = -1; double bestErr = 1e9;
        for (int c = 0; c < s.numChoices; ++c)
        {
            const double cv = std::strtod (s.choices[c], nullptr);
            if (std::fabs (cv - v) < bestErr) { bestErr = std::fabs (cv - v); best = c; }
        }
        if (best < 0 || bestErr > 1e-3 * std::fabs (v) + 1e-6) return false;
        plain = (float) best; return true;
    }
    char* end = nullptr; const double v = std::strtod (text.c_str(), &end);
    if (end == text.c_str() || ! std::isfinite (v)) return false;
    plain = clampPlain (index, (float) v);
    return true;
}

ParamValues defaultValues()
{
    ParamValues v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = kSpecs[i].def;
    return v;
}
} // namespace gglue
