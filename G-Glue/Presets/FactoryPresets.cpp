// G-Glue factory presets: original names and settings, written for this plugin.
// Columns: name, category, threshold dB, makeup dB, attack index, release index, ratio index, SC filter index,
//          mix %, input dB, output dB, analog
// attack  0=0.1 ms 1=0.3 ms 2=1 ms 3=3 ms 4=10 ms 5=30 ms     release 0=0.1 s 1=0.3 s 2=0.6 s 3=1.2 s 4=AUTO
// ratio   0=2:1 1=4:1 2=10:1                                 SC      0=OFF 1=30 2=60 3=90 4=120 5=150 6=200 Hz
#include "GGluePresets.h"

namespace gglue
{
namespace
{
struct Row { const char* name; const char* cat; float thr, mk; int atk, rel, ratio, sc; float mix, in, out; int analog; };

const Row kRows[] {
    // MIX BUS
    { "Clean Glue",        "MIX BUS",    -14.f,  2.0f, 5, 4, 0, 3, 100.f,  0.f,  0.f, 0 },
    { "Wide Mix",          "MIX BUS",    -12.f,  1.5f, 5, 4, 0, 4, 100.f,  0.f,  0.f, 0 },
    { "Open Mix",          "MIX BUS",    -10.f,  1.0f, 5, 2, 0, 5, 100.f,  0.f,  0.f, 0 },
    { "Tight Mix",         "MIX BUS",    -16.f,  3.0f, 4, 1, 1, 3, 100.f,  0.f,  0.f, 0 },
    { "Console Glue",      "MIX BUS",    -15.f,  2.5f, 4, 4, 1, 2, 100.f,  0.f,  0.f, 1 },
    { "Mix Bus Lift",      "MIX BUS",    -13.f,  2.0f, 3, 4, 0, 4,  80.f,  0.f,  0.f, 0 },
    // DRUM BUS
    { "Drum Punch",        "DRUM BUS",   -18.f,  4.0f, 4, 0, 1, 0, 100.f,  0.f,  0.f, 0 },
    { "Tight Drums",       "DRUM BUS",   -20.f,  5.0f, 3, 1, 1, 2, 100.f,  0.f,  0.f, 0 },
    { "Kick Control",      "DRUM BUS",   -16.f,  3.0f, 2, 0, 2, 0, 100.f,  0.f,  0.f, 0 },
    { "Snare Snap",        "DRUM BUS",   -18.f,  4.0f, 5, 0, 1, 5, 100.f,  0.f,  0.f, 0 },
    { "Room Breath",       "DRUM BUS",   -22.f,  6.0f, 1, 3, 2, 3,  60.f,  0.f,  0.f, 1 },
    { "Drum Glue",         "DRUM BUS",   -15.f,  2.5f, 4, 4, 0, 3, 100.f,  0.f,  0.f, 0 },
    // HIP-HOP
    { "Hip-Hop Glue",      "HIP-HOP",    -14.f,  2.5f, 5, 4, 1, 3, 100.f,  0.f,  0.f, 1 },
    { "Beat Glue",         "HIP-HOP",    -16.f,  3.0f, 4, 4, 1, 2, 100.f,  0.f,  0.f, 0 },
    { "Boom Bap Bus",      "HIP-HOP",    -18.f,  4.0f, 4, 1, 1, 4, 100.f,  0.f,  0.f, 1 },
    { "808 Hold",          "HIP-HOP",    -12.f,  1.5f, 5, 3, 0, 0, 100.f,  0.f,  0.f, 0 },
    { "Low-End Control",   "HIP-HOP",    -10.f,  1.0f, 3, 2, 1, 0, 100.f,  0.f,  0.f, 0 },
    { "Dusty Squeeze",     "HIP-HOP",    -24.f,  8.0f, 1, 1, 2, 0,  70.f,  0.f,  0.f, 1 },
    // MASTERING
    { "Gentle Master",     "MASTERING",   -8.f,  1.0f, 5, 4, 0, 4, 100.f,  0.f,  0.f, 0 },
    { "Transparent Glue",  "MASTERING",  -10.f,  1.0f, 5, 4, 0, 6, 100.f,  0.f,  0.f, 0 },
    { "Master Polish",     "MASTERING",  -12.f,  1.5f, 5, 2, 0, 3, 100.f,  0.f,  0.f, 1 },
    { "Loud Master",       "MASTERING",  -14.f,  3.0f, 4, 1, 1, 3, 100.f,  0.f,  0.f, 0 },
    { "Peak Guard",        "MASTERING",   -4.f,  0.0f, 2, 0, 2, 2, 100.f,  0.f,  0.f, 0 },
    // VOCALS
    { "Vocal Bus",         "VOCALS",     -16.f,  3.0f, 3, 4, 1, 4, 100.f,  0.f,  0.f, 0 },
    { "Smooth Vocal",      "VOCALS",     -18.f,  4.0f, 4, 2, 0, 5, 100.f,  0.f,  0.f, 1 },
    { "Vocal Upfront",     "VOCALS",     -20.f,  5.0f, 2, 1, 1, 6, 100.f,  0.f,  0.f, 0 },
    { "Backing Vox Blend", "VOCALS",     -18.f,  3.0f, 4, 3, 0, 6, 100.f,  0.f,  0.f, 0 },
    { "Rap Vocal Control", "VOCALS",     -20.f,  5.0f, 1, 0, 2, 5, 100.f,  0.f,  0.f, 0 },
    // ROCK
    { "Rock Bus",          "ROCK",       -14.f,  2.5f, 4, 4, 1, 3, 100.f,  0.f,  0.f, 1 },
    { "Guitar Wall",       "ROCK",       -18.f,  3.5f, 4, 2, 1, 4, 100.f,  0.f,  0.f, 0 },
    { "Live Room Smash",   "ROCK",       -24.f,  8.0f, 0, 1, 2, 0,  50.f,  0.f,  0.f, 1 },
    { "Rock Drums",        "ROCK",       -18.f,  4.0f, 3, 1, 1, 2, 100.f,  0.f,  0.f, 0 },
    { "Arena Glue",        "ROCK",       -12.f,  2.0f, 5, 3, 0, 3, 100.f,  0.f,  0.f, 1 },
    // POP
    { "Pop Sheen",         "POP",        -12.f,  2.0f, 5, 4, 0, 5, 100.f,  0.f,  0.f, 0 },
    { "Radio Push",        "POP",        -16.f,  3.5f, 4, 1, 1, 4, 100.f,  0.f,  0.f, 0 },
    { "Pop Vocal Mix",     "POP",        -14.f,  2.5f, 3, 4, 0, 6, 100.f,  0.f,  0.f, 0 },
    { "Glossy Bus",        "POP",        -11.f,  1.5f, 5, 2, 0, 4, 100.f,  0.f,  0.f, 1 },
    { "Chorus Lift",       "POP",        -15.f,  3.0f, 4, 4, 1, 3, 100.f,  0.f,  0.f, 0 },
    // ELECTRONIC
    { "Electronic Glue",   "ELECTRONIC", -14.f,  2.0f, 5, 4, 1, 0, 100.f,  0.f,  0.f, 0 },
    { "Pump Groove",       "ELECTRONIC", -20.f,  5.0f, 0, 1, 2, 0, 100.f,  0.f,  0.f, 0 },
    { "House Bounce",      "ELECTRONIC", -16.f,  3.0f, 4, 1, 1, 1, 100.f,  0.f,  0.f, 0 },
    { "Techno Drive",      "ELECTRONIC", -18.f,  4.0f, 3, 2, 1, 2, 100.f,  0.f,  0.f, 1 },
    { "Synth Bus",         "ELECTRONIC", -12.f,  2.0f, 4, 4, 0, 4, 100.f,  0.f,  0.f, 0 },
    // PARALLEL
    { "Parallel Smash",    "PARALLEL",   -28.f, 12.0f, 0, 0, 2, 0,  35.f,  0.f,  0.f, 1 },
    { "Crush Blend",       "PARALLEL",   -26.f, 10.0f, 2, 1, 2, 2,  40.f,  0.f,  0.f, 0 },
    { "Parallel Thicken",  "PARALLEL",   -24.f,  8.0f, 4, 4, 1, 3,  30.f,  0.f,  0.f, 1 },
    { "Drum Crush",        "PARALLEL",   -30.f, 14.0f, 0, 0, 2, 0,  45.f,  0.f,  0.f, 1 },
    { "Vocal Parallel",    "PARALLEL",   -24.f,  9.0f, 3, 2, 2, 5,  30.f,  0.f,  0.f, 0 },
    { "Bass Parallel",     "PARALLEL",   -26.f, 10.0f, 4, 3, 2, 0,  35.f,  0.f,  0.f, 0 },
    // GENTLE
    { "Slow Glue",         "GENTLE",     -10.f,  1.0f, 5, 3, 0, 3, 100.f,  0.f,  0.f, 0 },
    { "Feather Touch",     "GENTLE",      -6.f,  0.5f, 5, 4, 0, 4, 100.f,  0.f,  0.f, 0 },
    { "Soft Bus",          "GENTLE",     -12.f,  1.5f, 5, 4, 0, 3, 100.f,  0.f,  0.f, 0 },
    { "Breathe",           "GENTLE",     -10.f,  1.0f, 4, 2, 0, 5, 100.f,  0.f,  0.f, 0 },
    { "Easy Level",        "GENTLE",     -14.f,  2.0f, 5, 4, 0, 4,  80.f,  0.f,  0.f, 0 },
    // AGGRESSIVE
    { "Heavy Bus",         "AGGRESSIVE", -22.f,  7.0f, 2, 1, 2, 0, 100.f,  0.f,  0.f, 1 },
    { "Fast Punch",        "AGGRESSIVE", -20.f,  5.0f, 0, 0, 1, 0, 100.f,  0.f,  0.f, 0 },
    { "Slam",              "AGGRESSIVE", -26.f, 10.0f, 0, 0, 2, 0, 100.f,  0.f,  0.f, 1 },
    { "Brick Glue",        "AGGRESSIVE", -20.f,  6.0f, 1, 1, 2, 2, 100.f,  0.f,  0.f, 0 },
    { "Pressure",          "AGGRESSIVE", -24.f,  8.0f, 2, 4, 2, 0, 100.f,  0.f,  0.f, 1 },
    // CREATIVE
    { "Pump Effect",       "CREATIVE",   -24.f,  6.0f, 0, 2, 2, 0, 100.f,  0.f,  0.f, 0 },
    { "Breathing Room",    "CREATIVE",   -28.f, 10.0f, 5, 3, 2, 0, 100.f,  0.f,  0.f, 0 },
    { "Vintage Bus",       "CREATIVE",   -14.f,  3.0f, 4, 4, 1, 3, 100.f,  0.f,  0.f, 1 },
    { "Hot Drive",         "CREATIVE",    -8.f,  0.0f, 5, 4, 0, 0, 100.f,  6.f, -6.f, 1 },
    { "Half Squash",       "CREATIVE",   -22.f,  8.0f, 3, 1, 2, 1,  50.f,  0.f,  0.f, 0 },
    { "Transient Lift",    "CREATIVE",   -18.f,  4.0f, 5, 0, 1, 0, 100.f,  0.f,  0.f, 0 },
};
} // namespace

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = [] {
        std::vector<Preset> v;
        for (const Row& r : kRows)
        {
            Preset p;
            p.name = r.name; p.category = r.cat; p.factory = true;
            p.values = defaultValues();
            p.values[kThreshold] = r.thr; p.values[kMakeup] = r.mk; p.values[kAttack] = (float) r.atk;
            p.values[kRelease] = (float) r.rel; p.values[kRatio] = (float) r.ratio; p.values[kScFilter] = (float) r.sc;
            p.values[kMix] = r.mix; p.values[kInput] = r.in; p.values[kOutput] = r.out; p.values[kAnalog] = (float) r.analog;
            p.values[kBypass] = 0.f;
            v.push_back (p);
        }
        return v;
    }();
    return list;
}

const std::vector<std::string>& presetCategories()
{
    static const std::vector<std::string> c { "MIX BUS", "DRUM BUS", "HIP-HOP", "MASTERING", "VOCALS", "ROCK", "POP",
                                              "ELECTRONIC", "PARALLEL", "GENTLE", "AGGRESSIVE", "CREATIVE", "USER" };
    return c;
}
} // namespace gglue
