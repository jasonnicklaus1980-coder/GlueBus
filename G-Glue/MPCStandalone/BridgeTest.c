/* Pure C test of the MPC bridge (proves the C ABI is usable from a non-C++ host wrapper). */
#include "GGlueMPCBridge.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; int ok_ = (c); printf ("  %s ", ok_ ? "ok  " : "FAIL"); printf (__VA_ARGS__); printf ("\n"); if (! ok_) ++failures; } while (0)

int main (int argc, char** argv)
{
    const char* folder = argc > 1 ? argv[1] : "/tmp/gglue-bridge-presets";
    gglue_mpc* m = gglue_mpc_create (folder);
    printf ("MPC bridge (C ABI)\n");
    CHECK (m != NULL && gglue_mpc_param_count() == 11, "create; %d parameters", gglue_mpc_param_count());
    CHECK (strcmp (gglue_mpc_param_id (0), "threshold") == 0 && strcmp (gglue_mpc_param_id (3), "release") == 0 && gglue_mpc_param_steps (3) == 5,
           "Q-Link order: %s, %s, %s, %s", gglue_mpc_param_short_name (0), gglue_mpc_param_short_name (1), gglue_mpc_param_short_name (2), gglue_mpc_param_short_name (3));
    char text[32];
    gglue_mpc_param_text (m, 3, text, sizeof text);
    CHECK (strcmp (text, "AUTO") == 0, "release text \"%s\"", text);

    gglue_mpc_prepare (m, 48000.0, 2);
    static float l[48000], r[48000];
    float* ch[2] = { l, r };
    for (int i = 0; i < 48000; ++i) l[i] = r[i] = 0.9f * (float) sin (2 * 3.14159265358979 * 1000 * i / 48000.0);
    for (int p = 0; p < 48000; p += 128) { float* c[2] = { l + p, r + p }; gglue_mpc_process (m, c, 2, 128); }
    double acc = 0; for (int i = 24000; i < 48000; ++i) acc += (double) l[i] * l[i];
    const double outDb = 20 * log10 (sqrt (acc / 24000) * sqrt (2.0));
    CHECK (outDb < -3 && gglue_mpc_meter (m, GGLUE_METER_GAIN_REDUCTION_DB) > 3, "compresses: -0.9 dBFS in, %.1f dBFS out, GR meter %.1f dB", outDb,
           gglue_mpc_meter (m, GGLUE_METER_GAIN_REDUCTION_DB));
    (void) ch;

    gglue_mpc_set_normalized (m, 0, 1.0f);
    gglue_mpc_param_text (m, 0, text, sizeof text);
    CHECK (strcmp (text, "+10.0 dB") == 0 && fabsf (gglue_mpc_get_normalized (m, 0) - 1.0f) < 1e-6f, "set threshold to max: \"%s\"", text);

    const int n = gglue_mpc_preset_count (m);
    int idx = -1;
    for (int i = 0; i < n; ++i) if (strcmp (gglue_mpc_preset_name (m, i), "Vocal Bus") == 0) idx = i;
    CHECK (n >= 60 && idx >= 0 && gglue_mpc_load_preset (m, idx), "%d presets; load \"Vocal Bus\" (%s)", n, idx >= 0 ? gglue_mpc_preset_category (m, idx) : "?");
    gglue_mpc_param_text (m, 0, text, sizeof text);
    CHECK (strcmp (text, "-16.0 dB") == 0, "preset threshold \"%s\"", text);

    const int need = gglue_mpc_get_state (m, NULL, 0);
    char state[4096];
    gglue_mpc_get_state (m, state, sizeof state);
    gglue_mpc* m2 = gglue_mpc_create (folder);
    CHECK (need > 100 && gglue_mpc_set_state (m2, state, (int) strlen (state)), "state %d bytes restored in a second instance", need);
    int same = 1;
    for (int i = 0; i < 11; ++i) same = same && fabsf (gglue_mpc_get_normalized (m, i) - gglue_mpc_get_normalized (m2, i)) < 1e-6f;
    CHECK (same, "all parameters equal after restore");

    gglue_mpc_switch_ab (m2);
    gglue_mpc_set_normalized (m2, 6, 0.5f);
    gglue_mpc_switch_ab (m2);
    gglue_mpc_param_text (m2, 6, text, sizeof text);
    CHECK (gglue_mpc_active_slot (m2) == 'A' && strcmp (text, "100 %") == 0, "A/B: B edited to 50 %%, A still %s", text);

    CHECK (gglue_mpc_save_preset (m, "MPC Bus", 1) && gglue_mpc_preset_count (m) == n + 1, "save a user preset to %s", folder);
    gglue_mpc_destroy (m);
    gglue_mpc_destroy (m2);
    printf ("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
