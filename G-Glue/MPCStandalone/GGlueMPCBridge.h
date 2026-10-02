/* G-Glue MPC bridge: a plain C interface to the shared G-Glue engine, parameters, presets and state.
 *
 * This is the boundary a NATIVE MPC Standalone plugin wrapper would call. Akai / inMusic do not publish a
 * third-party plugin SDK for MPC Standalone (see Documentation/MPC_STANDALONE_STATUS.md), so no such wrapper
 * exists here; this bridge is built and tested for the MPC's CPU (32-bit ARM hard-float) so the DSP side is ready.
 *
 * Threading: gglue_mpc_process() runs on the audio thread and never allocates or locks.
 * gglue_mpc_set_normalized() may be called from any thread (atomics); the audio thread picks values up per block.
 * Presets, state and text functions belong on a control / UI thread.
 */
#ifndef GGLUE_MPC_BRIDGE_H
#define GGLUE_MPC_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gglue_mpc gglue_mpc;

enum { GGLUE_METER_INPUT_DB = 0, GGLUE_METER_OUTPUT_DB = 1, GGLUE_METER_GAIN_REDUCTION_DB = 2 };

/* lifetime: preset_folder = where user presets live (e.g. on the MPC internal drive); may be NULL */
gglue_mpc* gglue_mpc_create (const char* preset_folder);
void gglue_mpc_destroy (gglue_mpc*);
void gglue_mpc_prepare (gglue_mpc*, double sample_rate, int num_channels);
void gglue_mpc_process (gglue_mpc*, float* const* channels, int num_channels, int num_samples);
int gglue_mpc_latency_samples (const gglue_mpc*);

/* parameters: index 0..count-1 in the stable order Threshold, Makeup, Attack, Release, Ratio, SC Filter, Mix,
 * Input, Output, Analog, Bypass (Q-Link 1-4 = the first four) */
int gglue_mpc_param_count (void);
const char* gglue_mpc_param_id (int index);           /* stable ID, e.g. "threshold" */
const char* gglue_mpc_param_name (int index);         /* "Threshold" */
const char* gglue_mpc_param_short_name (int index);   /* <= 8 chars for small displays */
int gglue_mpc_param_steps (int index);                /* 0 = continuous, else number of positions */
float gglue_mpc_get_normalized (const gglue_mpc*, int index);
void gglue_mpc_set_normalized (gglue_mpc*, int index, float value01);
int gglue_mpc_param_text (const gglue_mpc*, int index, char* buffer, int buffer_size);   /* "-12.0 dB", "AUTO" */

/* presets: factory presets first, then user presets in preset_folder */
int gglue_mpc_preset_count (const gglue_mpc*);
const char* gglue_mpc_preset_name (const gglue_mpc*, int index);
const char* gglue_mpc_preset_category (const gglue_mpc*, int index);
int gglue_mpc_load_preset (gglue_mpc*, int index);                  /* 1 = ok */
int gglue_mpc_save_preset (gglue_mpc*, const char* name, int overwrite);   /* 1 = ok, as a USER preset */

/* complete state (text, includes A/B); get returns the length needed (call with NULL to size the buffer) */
int gglue_mpc_get_state (const gglue_mpc*, char* buffer, int buffer_size);
int gglue_mpc_set_state (gglue_mpc*, const char* text, int length);   /* 1 = ok */
void gglue_mpc_switch_ab (gglue_mpc*);
char gglue_mpc_active_slot (const gglue_mpc*);

float gglue_mpc_meter (const gglue_mpc*, int which);   /* GGLUE_METER_* */

#ifdef __cplusplus
}
#endif
#endif
