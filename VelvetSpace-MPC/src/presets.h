#pragma once
// Velvet Space factory presets. Algo: 0 Room, 1 Hall, 2 Plate, 3 Classic, 4 Ice, 5 Meta, 6 Reflex.
// mix 0..1, pre-delay ms, size 0..1, decay s, damping Hz, low cut Hz, diffusion 0..1, modulation 0..1, width 0..1.5,
// shimmer 0..1, colour -1..1, ducking 0..1, output dB
struct Preset { const char* name; int algo; float mix, pre, size, decay, damp, lowcut, diff, mod, width, shimmer, color, duck, out; };

static const Preset kPresets[] = {
    { "Init",               1, 0.30f,  20, 0.60f, 2.5f,  9000, 100, 0.75f, 0.30f, 1.20f, 0.00f,  0.00f, 0.00f, 0 },
    // rooms
    { "Small Room",         0, 0.22f,   5, 0.25f, 0.5f,  8000, 100, 0.70f, 0.15f, 1.00f, 0.00f,  0.00f, 0.00f, 0 },
    { "Vocal Booth",        0, 0.18f,   2, 0.12f, 0.35f, 7000, 150, 0.60f, 0.10f, 0.80f, 0.00f, -0.10f, 0.00f, 0 },
    { "Drum Room",          0, 0.30f,   3, 0.45f, 0.8f,  9000,  60, 0.80f, 0.15f, 1.20f, 0.00f,  0.10f, 0.00f, 0 },
    { "Studio Room",        0, 0.25f,   8, 0.55f, 1.0f, 10000,  90, 0.70f, 0.20f, 1.10f, 0.00f,  0.00f, 0.00f, 0 },
    { "Live Room",          0, 0.28f,  12, 0.80f, 1.4f, 11000,  80, 0.75f, 0.25f, 1.30f, 0.00f,  0.15f, 0.00f, 0 },
    // halls
    { "Concert Hall",       1, 0.30f,  25, 0.60f, 2.4f,  8500,  90, 0.75f, 0.30f, 1.20f, 0.00f, -0.05f, 0.00f, 0 },
    { "Big Hall",           1, 0.32f,  35, 0.85f, 3.8f,  9000, 100, 0.80f, 0.35f, 1.35f, 0.00f,  0.00f, 0.00f, 0 },
    { "Dark Hall",          1, 0.30f,  30, 0.70f, 3.0f,  4500, 120, 0.80f, 0.30f, 1.20f, 0.00f, -0.50f, 0.00f, 0 },
    { "Bright Hall",        1, 0.28f,  25, 0.65f, 2.6f, 14000, 150, 0.75f, 0.30f, 1.30f, 0.00f,  0.40f, 0.00f, 0 },
    { "Cathedral",          1, 0.38f,  60, 1.00f, 7.5f,  7000,  90, 0.90f, 0.35f, 1.40f, 0.00f, -0.10f, 0.00f, 0 },
    // plates
    { "Vocal Plate",        2, 0.25f,  30, 0.55f, 1.8f, 11000, 180, 0.80f, 0.30f, 1.20f, 0.00f,  0.20f, 0.20f, 0 },
    { "Snare Plate",        2, 0.30f,   5, 0.45f, 1.3f,  9000, 200, 0.85f, 0.20f, 1.10f, 0.00f,  0.10f, 0.00f, 0 },
    { "Bright Plate",       2, 0.25f,  15, 0.50f, 2.0f, 15000, 250, 0.80f, 0.35f, 1.30f, 0.00f,  0.45f, 0.00f, 0 },
    { "Long Plate",         2, 0.30f,  20, 0.80f, 4.5f, 10000, 180, 0.85f, 0.40f, 1.30f, 0.00f,  0.10f, 0.00f, 0 },
    { "Vintage Plate",      2, 0.28f,  10, 0.60f, 2.2f,  6000, 150, 0.75f, 0.25f, 1.00f, 0.00f, -0.25f, 0.00f, 0 },
    // classic (legacy)
    { "Classic Room",       3, 0.22f,   5, 0.30f, 0.8f,  7000,  80, 0.50f, 0.00f, 1.00f, 0.00f,  0.00f, 0.00f, 0 },
    { "Classic Hall",       3, 0.28f,  20, 0.70f, 2.8f,  6500,  80, 0.50f, 0.00f, 1.00f, 0.00f,  0.00f, 0.00f, 0 },
    { "Classic Plate",      3, 0.25f,  10, 0.50f, 1.6f,  9000, 150, 0.50f, 0.00f, 1.00f, 0.00f,  0.20f, 0.00f, 0 },
    // ice
    { "Ice Crystal",        4, 0.30f,  20, 0.60f, 3.5f, 16000, 300, 0.80f, 0.40f, 1.30f, 0.15f,  0.40f, 0.00f, 0 },
    { "Frozen Lake",        4, 0.35f,  40, 0.85f, 6.0f, 12000, 300, 0.85f, 0.50f, 1.40f, 0.30f,  0.20f, 0.00f, 0 },
    { "Glass Keys",         4, 0.28f,  15, 0.50f, 2.5f, 17000, 350, 0.75f, 0.35f, 1.30f, 0.00f,  0.60f, 0.00f, 0 },
    // meta
    { "Meta Space",         5, 0.35f,  30, 0.70f, 4.5f,  9000, 120, 0.85f, 0.60f, 1.40f, 0.00f,  0.00f, 0.00f, 0 },
    { "Wide Meta",          5, 0.32f,  25, 0.80f, 5.0f, 10000, 150, 0.85f, 0.70f, 1.50f, 0.00f,  0.10f, 0.00f, 0 },
    { "Meta Pad Wash",      5, 0.50f,  50, 1.00f, 9.0f,  8000, 180, 0.90f, 0.80f, 1.50f, 0.10f, -0.10f, 0.00f, 0 },
    // reflex
    { "Reflex Small",       6, 0.25f,   0, 0.25f, 1.0f,  9000, 100, 0.60f, 0.10f, 1.10f, 0.00f,  0.00f, 0.00f, 0 },
    { "Reflex Wide Room",   6, 0.30f,   5, 0.60f, 1.5f, 10000,  90, 0.70f, 0.15f, 1.50f, 0.00f,  0.10f, 0.00f, 0 },
    { "Reflex Slap",        6, 0.30f,  40, 0.50f, 0.6f,  7000, 150, 0.30f, 0.05f, 1.20f, 0.00f, -0.10f, 0.00f, 0 },
    // creative: huge airy sky-verbs (High Sky: long pre-delay, bright, octave shimmer, low cut, ducked under the vocal)
    { "High Sky",           5, 0.38f,  80, 1.00f, 8.5f, 15000, 400, 0.90f, 0.60f, 1.50f, 0.35f,  0.45f, 0.35f, 0 },
    { "High Sky Vocal",     5, 0.28f,  90, 0.90f, 6.0f, 14000, 500, 0.85f, 0.50f, 1.40f, 0.25f,  0.40f, 0.55f, 0 },
    { "Shimmer Heaven",     1, 0.45f,  60, 1.00f, 10.0f, 12000, 350, 0.90f, 0.50f, 1.50f, 0.60f,  0.30f, 0.20f, 0 },
    { "Endless Pad",        5, 0.60f,  40, 1.00f, 20.0f, 7000, 200, 0.95f, 0.70f, 1.50f, 0.00f, -0.20f, 0.00f, 0 },
    { "Ducked Vocal Space", 2, 0.30f,  60, 0.70f, 2.8f, 11000, 250, 0.80f, 0.30f, 1.30f, 0.00f,  0.20f, 0.80f, 0 },
    { "Lo-Fi Dream",        3, 0.40f,  30, 0.80f, 4.0f,  3500, 200, 0.50f, 0.00f, 1.20f, 0.00f, -0.40f, 0.00f, 0 },
};
static constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));
