#pragma once
// The plugin's parameters. MPC draws the screen (TUI.json skin) from these and maps them to the Q-Links, so
// everything on screen is a parameter:
//  - global controls (tempo, quantise, scenes, sampler, master FX, MIDI ...);
//  - "selected clip" controls: they show and edit the clip chosen with CLIP (or by tapping its pad), like the
//    MPC's own pad parameters;
//  - per-clip mixer controls (volume, pan, mute) and the 16 pads;
//  - read-only readouts the plugin updates (names, info lines, states, waveform, markers, meters, texts).
// Every parameter has a stable key (used by the skin generator and in saved state), independent of its index.
#include "../ClipEngine/Clip.h"

namespace cp
{
enum Kind { K_FLOAT, K_CHOICE, K_BOOL, K_MOMENT, K_READ, K_TEXT, K_LIST };
enum Fmt { F_NUM, F_DB, F_VOLDB, F_PCT, F_HZ, F_MS, F_SEMI, F_CENT, F_PAN, F_BPM, F_NOTE, F_POS, F_BITS, F_RATIO, F_BARS, F_SWING, F_INT };
enum Map { MAP_LIN, MAP_LOG, MAP_SQR };

#define CP_CLIP_PARAMS(X) X##0, X##1, X##2, X##3, X##4, X##5, X##6, X##7, X##8, X##9, X##10, X##11, X##12, X##13, X##14, X##15
enum Param
{
    P_TEMPO, P_SYNC, P_SWING, P_QUANT, P_SCENEQ, P_STOPALL, P_MUTEALL, P_RESTART, P_RETRIGALL,
    P_PM_PLAY, P_PM_STOP, P_PM_SELECT, P_PM_MUTE,
    P_MASTER, P_SEL, P_FILE, P_LOAD, P_CLEAR, P_RESCAN, P_PREVIEW,
    // selected clip
    P_MODE, P_CQUANT, P_REVERSE, P_SEMI, P_CENT, P_PMODE, P_SLEN, P_STYPE,
    P_START, P_END, P_LSTART, P_LEND, P_FADEIN, P_FADEOUT, P_XFADE,
    P_NORMALIZE, P_FILT, P_CUT, P_RES, P_DRIVE, P_RSEND, P_DSEND, P_VINTAGE,
    P_FOLLOW, P_FTIME, P_FSCENE, P_NOTE, P_INSCENE0, P_INSCENE1, P_INSCENE2, P_INSCENE3, P_SEQ,
    P_ZOOM, P_TRIM, P_AUTOFADE, P_AUTOXF, P_LEARN,
    // chop
    P_CHOPN, P_CHOPMODE, P_CHOP, P_SLICE, P_SLICEPOS, P_SLICEREV, P_SLICEPITCH, P_SLICEBANK, P_ASSIGN, P_SLICENOTE,
    // flip
    P_FLIP, P_UNDO, P_LOCK_PITCH, P_LOCK_REV, P_LOCK_POS, P_LOCK_FILT, P_LOCK_SLICES,
    // scenes
    P_SCENE0, P_SCENE1, P_SCENE2, P_SCENE3, P_SCENESTORE, P_SCENEFOLLOW, P_SCENEBARS,
    // sampler stage
    P_SMP_ON, P_SMP_PRESET, P_BITS, P_RATE, P_AA, P_QUANTIZE, P_SAT, P_NOISE, P_CRACKLE, P_SMP_OUT,
    // master FX
    P_MSAT, P_CTHRESH, P_CRATIO, P_CATTACK, P_CRELEASE, P_CMAKEUP, P_EQLOW, P_EQMID, P_EQMIDF, P_EQHIGH,
    P_REVSIZE, P_REVDAMP, P_REVRET, P_DLYTIME, P_DLYFB, P_DLYTONE, P_DLYRET, P_LIMIT,
    // settings
    P_MIDICH, P_VELSENS, P_QUALITY, P_HOSTFOLLOW, P_PRESET, P_PRESETLOAD, P_KIT, P_KITLOAD, P_KITSAVE,
    // per clip
    CP_CLIP_PARAMS (P_CVOL), CP_CLIP_PARAMS (P_CPAN), CP_CLIP_PARAMS (P_CMUTE), CP_CLIP_PARAMS (P_PAD), CP_CLIP_PARAMS (P_SPAD),
    // read-only
    CP_CLIP_PARAMS (P_CNAME), CP_CLIP_PARAMS (P_CINFO), CP_CLIP_PARAMS (P_CINFOB), CP_CLIP_PARAMS (P_CSTAT),
    P_THUMB0, P_THUMB_END = P_THUMB0 + 64,
    P_WAVE0 = P_THUMB_END, P_WAVE_END = P_WAVE0 + 16,
    P_MARK0 = P_WAVE_END, P_MARK_END = P_MARK0 + 32,
    P_SCENELED0 = P_MARK_END, P_SCENELED1, P_SCENELED2, P_SCENELED3,
    P_BEATLED, P_TRANSPORT, P_STATUS, P_SELNAME, P_LIBINFO, P_SLICEINFO, P_METER_L, P_METER_R,
    P_COUNT
};
#undef CP_CLIP_PARAMS

constexpr int kThumbBoxes = 4, kWaveBoxes = 16, kMarks = 32, kLevels = 11;
constexpr float kMeterLo = -48.f, kMeterHi = 6.f;

struct ParamDef
{
    char key[20]; char name[28]; const char* unit; Kind kind; float lo, hi, step, def; const char* const* choices; int n;
    Fmt fmt; Map map; bool save;
};
inline ParamDef* params() { static ParamDef k[P_COUNT]; return k; }

inline void def (int i, const char* key, const char* name, const char* unit, Kind k, float lo, float hi, float step, float d,
                 Fmt f = F_NUM, Map m = MAP_LIN, bool save = true, const char* const* ch = nullptr, int n = 0)
{
    ParamDef& p = params()[i];
    std::snprintf (p.key, sizeof p.key, "%s", key); std::snprintf (p.name, sizeof p.name, "%s", name);
    p.unit = unit; p.kind = k; p.lo = lo; p.hi = hi; p.step = step; p.def = d; p.fmt = f; p.map = m; p.save = save; p.choices = ch; p.n = n;
    if (k == K_MOMENT || k == K_READ || k == K_TEXT || k == K_LIST) p.save = false;
}
inline void choice (int i, const char* key, const char* name, const char* const* ch, int n, int d, bool save = true)
{
    def (i, key, name, "", K_CHOICE, 0, (float) (n - 1), 1, (float) d, F_NUM, MAP_LIN, save, ch, n);
}

// choice lists
static const char* const kQuantNames[] { "None", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1 Bar", "2 Bars", "4 Bars" };
static const char* const kQuantGNames[] { "Global", "None", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1 Bar", "2 Bars", "4 Bars" };
static const char* const kModeNames[] { "One Shot", "Loop", "Gate", "Toggle" };
static const char* const kPModeNames[] { "Resample", "Stretch" };
static const char* const kSlenNames[] { "Off", "Auto", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars" };
static const char* const kStypeNames[] { "Drums", "Instruments", "Vocals", "Loops" };
static const char* const kFiltNames[] { "Off", "Low Pass", "High Pass", "Band Pass" };
static const char* const kFollowNames[] { "Off", "Stop", "Repeat", "Next Clip", "Prev Clip", "Random", "Rnd in Scene", "Continue", "Launch Scene" };
static const char* const kFtimeNames[] { "At End", "1 Beat", "2 Beats", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" };
static const char* const kSceneNames[] { "Scene 1", "Scene 2", "Scene 3", "Scene 4" };
static const char* const kChopNNames[] { "4 Chops", "8 Chops", "16 Chops", "32 Chops" };
static const char* const kChopModeNames[] { "Transient", "Equal" };
static const char* const kBankNames[] { "Slices 1-16", "Slices 17-32" };
static const char* const kSceneFollowNames[] { "Off", "Next", "Previous", "Random", "Repeat" };
static const char* const kSceneBarNames[] { "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" };
static const char* const kSmpPresetNames[] { "Custom", "Clean", "12-Bit", "SP Style", "MPC Style", "Vinyl", "Dusty", "Crushed" };
static const char* const kDelayNames[] { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1 Bar" };
static const char* const kMidiChNames[] { "Omni", "Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8", "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16" };
static const char* const kQualityNames[] { "Eco", "Normal", "High" };
static const char* const kHostFollowNames[] { "Off", "Restart", "Restart+Stop" };
static const char* const kZoomNames[] { "1x", "2x", "4x", "8x", "16x", "32x" };
static const char* const kSelNames[] { "Clip 1", "Clip 2", "Clip 3", "Clip 4", "Clip 5", "Clip 6", "Clip 7", "Clip 8", "Clip 9", "Clip 10", "Clip 11", "Clip 12", "Clip 13", "Clip 14", "Clip 15", "Clip 16" };
static const char* const kSliceNames[] { "Slice 1", "Slice 2", "Slice 3", "Slice 4", "Slice 5", "Slice 6", "Slice 7", "Slice 8", "Slice 9", "Slice 10", "Slice 11",
    "Slice 12", "Slice 13", "Slice 14", "Slice 15", "Slice 16", "Slice 17", "Slice 18", "Slice 19", "Slice 20", "Slice 21", "Slice 22", "Slice 23", "Slice 24",
    "Slice 25", "Slice 26", "Slice 27", "Slice 28", "Slice 29", "Slice 30", "Slice 31", "Slice 32" };
static const double kSceneBars[] { 1, 2, 4, 8, 16 };

inline bool isProxy (int i) { return (i >= P_MODE && i <= P_SEQ); }

inline void initParams (int presetCount, const char* const* presetNames)
{
    def (P_TEMPO, "tempo", "Master Tempo", "BPM", K_FLOAT, 40, 240, 0.01f, 90, F_BPM);
    def (P_SYNC, "sync", "Sync To Host", "", K_BOOL, 0, 1, 1, 1);
    def (P_SWING, "swing", "Swing", "%", K_FLOAT, 50, 75, 0.5f, 50, F_SWING);
    choice (P_QUANT, "quant", "Quantize", kQuantNames, 9, Q_BAR);
    choice (P_SCENEQ, "sceneq", "Scene Quantize", kQuantGNames, 10, 0);
    def (P_STOPALL, "stopall", "Stop All", "", K_MOMENT, 0, 1, 1, 0);
    def (P_MUTEALL, "muteall", "Global Mute", "", K_BOOL, 0, 1, 1, 0);
    def (P_RESTART, "restart", "Global Restart", "", K_MOMENT, 0, 1, 1, 0);
    def (P_RETRIGALL, "retrigall", "Retrigger All", "", K_MOMENT, 0, 1, 1, 0);
    def (P_PM_PLAY, "pmplay", "Pad Mode Play", "", K_BOOL, 0, 1, 1, 1);
    def (P_PM_STOP, "pmstop", "Pad Mode Stop", "", K_BOOL, 0, 1, 1, 0);
    def (P_PM_SELECT, "pmselect", "Pad Mode Select", "", K_BOOL, 0, 1, 1, 0);
    def (P_PM_MUTE, "pmmute", "Pad Mode Mute", "", K_BOOL, 0, 1, 1, 0);
    def (P_MASTER, "master", "Master Volume", "dB", K_FLOAT, -60, 6, 0.1f, 0, F_VOLDB);
    choice (P_SEL, "sel", "Selected Clip", kSelNames, 16, 0);
    def (P_FILE, "file", "Sample", "", K_LIST, 0, 1, 0, 0, F_NUM, MAP_LIN, false);
    def (P_LOAD, "load", "Load Sample", "", K_MOMENT, 0, 1, 1, 0);
    def (P_CLEAR, "clear", "Clear Clip", "", K_MOMENT, 0, 1, 1, 0);
    def (P_RESCAN, "rescan", "Rescan Folder", "", K_MOMENT, 0, 1, 1, 0);
    def (P_PREVIEW, "preview", "Play Selected", "", K_MOMENT, 0, 1, 1, 0);

    choice (P_MODE, "mode", "Trigger Mode", kModeNames, 4, M_LOOP, false);
    choice (P_CQUANT, "cquant", "Clip Quantize", kQuantGNames, 10, 0, false);
    def (P_REVERSE, "reverse", "Reverse", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
    def (P_SEMI, "semi", "Pitch", "st", K_FLOAT, -12, 12, 1, 0, F_SEMI, MAP_LIN, false);
    def (P_CENT, "cent", "Fine Tune", "ct", K_FLOAT, -100, 100, 1, 0, F_CENT, MAP_LIN, false);
    choice (P_PMODE, "pmode", "Pitch Mode", kPModeNames, 2, PM_RESAMPLE, false);
    choice (P_SLEN, "slen", "Stretch To", kSlenNames, 8, SL_OFF, false);
    choice (P_STYPE, "stype", "Stretch Mode", kStypeNames, 4, ST_LOOPS, false);
    def (P_START, "start", "Start", "", K_FLOAT, 0, 1, 0, 0, F_POS, MAP_LIN, false);
    def (P_END, "end", "End", "", K_FLOAT, 0, 1, 0, 1, F_POS, MAP_LIN, false);
    def (P_LSTART, "lstart", "Loop Start", "", K_FLOAT, 0, 1, 0, 0, F_POS, MAP_LIN, false);
    def (P_LEND, "lend", "Loop End", "", K_FLOAT, 0, 1, 0, 1, F_POS, MAP_LIN, false);
    def (P_FADEIN, "fadein", "Fade In", "ms", K_FLOAT, 0, 2000, 0.1f, 0, F_MS, MAP_SQR, false);
    def (P_FADEOUT, "fadeout", "Fade Out", "ms", K_FLOAT, 0, 2000, 0.1f, 0, F_MS, MAP_SQR, false);
    def (P_XFADE, "xfade", "Loop Crossfade", "ms", K_FLOAT, 0, 500, 0.1f, 5, F_MS, MAP_SQR, false);
    def (P_NORMALIZE, "normalize", "Normalize", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
    choice (P_FILT, "filt", "Filter Type", kFiltNames, 4, 0, false);
    def (P_CUT, "cut", "Cutoff", "Hz", K_FLOAT, 20, 20000, 0, 20000, F_HZ, MAP_LOG, false);
    def (P_RES, "res", "Resonance", "%", K_FLOAT, 0, 1, 0, 0, F_PCT, MAP_LIN, false);
    def (P_DRIVE, "drive", "Drive", "%", K_FLOAT, 0, 1, 0, 0, F_PCT, MAP_LIN, false);
    def (P_RSEND, "rsend", "Reverb Send", "%", K_FLOAT, 0, 1, 0, 0, F_PCT, MAP_LIN, false);
    def (P_DSEND, "dsend", "Delay Send", "%", K_FLOAT, 0, 1, 0, 0, F_PCT, MAP_LIN, false);
    def (P_VINTAGE, "vintage", "Sampler Stage", "", K_BOOL, 0, 1, 1, 1, F_NUM, MAP_LIN, false);
    choice (P_FOLLOW, "follow", "Follow Action", kFollowNames, 9, 0, false);
    choice (P_FTIME, "ftime", "Follow Time", kFtimeNames, 8, 0, false);
    choice (P_FSCENE, "fscene", "Follow Scene", kSceneNames, 4, 0, false);
    def (P_NOTE, "note", "MIDI Note", "", K_FLOAT, -1, 127, 1, 36, F_NOTE, MAP_LIN, false);
    for (int s = 0; s < 4; ++s) { char k[16], n[28]; std::snprintf (k, sizeof k, "inscene%d", s); std::snprintf (n, sizeof n, "In Scene %d", s + 1); def (P_INSCENE0 + s, k, n, "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false); }
    def (P_SEQ, "seq", "Rearranged", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
    choice (P_ZOOM, "zoom", "Zoom", kZoomNames, 6, 0);
    def (P_TRIM, "trim", "Trim", "", K_MOMENT, 0, 1, 1, 0);
    def (P_AUTOFADE, "autofade", "Fade", "", K_MOMENT, 0, 1, 1, 0);
    def (P_AUTOXF, "autoxf", "Crossfade", "", K_MOMENT, 0, 1, 1, 0);
    def (P_LEARN, "learn", "MIDI Learn", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);

    choice (P_CHOPN, "chopn", "Chops", kChopNNames, 4, 2);
    choice (P_CHOPMODE, "chopmode", "Chop Mode", kChopModeNames, 2, 0);
    def (P_CHOP, "chop", "Chop", "", K_MOMENT, 0, 1, 1, 0);
    choice (P_SLICE, "slice", "Slice", kSliceNames, 32, 0, false);
    def (P_SLICEPOS, "slicepos", "Slice Start", "", K_FLOAT, 0, 1, 0, 0, F_POS, MAP_LIN, false);
    def (P_SLICEREV, "slicerev", "Slice Reverse", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
    def (P_SLICEPITCH, "slicepitch", "Slice Pitch", "st", K_FLOAT, -12, 12, 1, 0, F_SEMI, MAP_LIN, false);
    choice (P_SLICEBANK, "slicebank", "Slice Bank", kBankNames, 2, 0);
    def (P_ASSIGN, "assign", "Pad Assign", "", K_MOMENT, 0, 1, 1, 0);
    def (P_SLICENOTE, "slicenote", "Slice Note", "", K_FLOAT, 0, 96, 1, 60, F_NOTE);

    def (P_FLIP, "flip", "Flip", "", K_MOMENT, 0, 1, 1, 0);
    def (P_UNDO, "undo", "Undo Flip", "", K_MOMENT, 0, 1, 1, 0);
    def (P_LOCK_PITCH, "lockpitch", "Lock Pitch", "", K_BOOL, 0, 1, 1, 0);
    def (P_LOCK_REV, "lockrev", "Lock Reverse", "", K_BOOL, 0, 1, 1, 0);
    def (P_LOCK_POS, "lockpos", "Lock Start/End", "", K_BOOL, 0, 1, 1, 0);
    def (P_LOCK_FILT, "lockfilt", "Lock Filter", "", K_BOOL, 0, 1, 1, 0);
    def (P_LOCK_SLICES, "lockslices", "Lock Slices", "", K_BOOL, 0, 1, 1, 0);

    for (int s = 0; s < 4; ++s) { char k[16], n[28]; std::snprintf (k, sizeof k, "scene%d", s); std::snprintf (n, sizeof n, "Scene %d", s + 1); def (P_SCENE0 + s, k, n, "", K_MOMENT, 0, 1, 1, 0); }
    def (P_SCENESTORE, "scenestore", "Scene Store", "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
    choice (P_SCENEFOLLOW, "scenefollow", "Scene Follow", kSceneFollowNames, 5, 0);
    choice (P_SCENEBARS, "scenebars", "Scene Length", kSceneBarNames, 5, 2);

    def (P_SMP_ON, "smpon", "Sampler On", "", K_BOOL, 0, 1, 1, 0);
    choice (P_SMP_PRESET, "smppreset", "Sampler Preset", kSmpPresetNames, 8, 3);
    def (P_BITS, "bits", "Bit Depth", "bit", K_FLOAT, 2, 24, 1, 12, F_BITS);
    def (P_RATE, "rate", "Sample Rate", "Hz", K_FLOAT, 2000, 48000, 10, 26040, F_HZ, MAP_LOG);
    def (P_AA, "aa", "Anti-Alias", "%", K_FLOAT, 0, 1, 0, 0, F_PCT);
    def (P_QUANTIZE, "quantize", "Quantize", "%", K_FLOAT, 0, 1, 0, 1, F_PCT);
    def (P_SAT, "sat", "Saturation", "%", K_FLOAT, 0, 1, 0, 0.25f, F_PCT);
    def (P_NOISE, "noise", "Noise", "%", K_FLOAT, 0, 1, 0, 0.06f, F_PCT);
    def (P_CRACKLE, "crackle", "Crackle", "%", K_FLOAT, 0, 1, 0, 0, F_PCT);
    def (P_SMP_OUT, "smpout", "Sampler Output", "dB", K_FLOAT, -24, 12, 0.1f, 0, F_DB);

    def (P_MSAT, "msat", "Master Saturation", "%", K_FLOAT, 0, 1, 0, 0, F_PCT);
    def (P_CTHRESH, "cthresh", "Comp Threshold", "dB", K_FLOAT, -40, 0, 0.1f, -12, F_DB);
    def (P_CRATIO, "cratio", "Comp Ratio", "", K_FLOAT, 1, 20, 0.1f, 1, F_RATIO, MAP_SQR);
    def (P_CATTACK, "cattack", "Comp Attack", "ms", K_FLOAT, 0.1f, 100, 0, 10, F_MS, MAP_LOG);
    def (P_CRELEASE, "crelease", "Comp Release", "ms", K_FLOAT, 10, 1000, 0, 150, F_MS, MAP_LOG);
    def (P_CMAKEUP, "cmakeup", "Comp Makeup", "dB", K_FLOAT, 0, 24, 0.1f, 0, F_DB);
    def (P_EQLOW, "eqlow", "EQ Low", "dB", K_FLOAT, -12, 12, 0.1f, 0, F_DB);
    def (P_EQMID, "eqmid", "EQ Mid", "dB", K_FLOAT, -12, 12, 0.1f, 0, F_DB);
    def (P_EQMIDF, "eqmidf", "EQ Mid Freq", "Hz", K_FLOAT, 200, 5000, 0, 1000, F_HZ, MAP_LOG);
    def (P_EQHIGH, "eqhigh", "EQ High", "dB", K_FLOAT, -12, 12, 0.1f, 0, F_DB);
    def (P_REVSIZE, "revsize", "Reverb Size", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    def (P_REVDAMP, "revdamp", "Reverb Damping", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    def (P_REVRET, "revret", "Reverb Return", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    choice (P_DLYTIME, "dlytime", "Delay Time", kDelayNames, 8, 3);
    def (P_DLYFB, "dlyfb", "Delay Feedback", "%", K_FLOAT, 0, 0.95f, 0, 0.35f, F_PCT);
    def (P_DLYTONE, "dlytone", "Delay Tone", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    def (P_DLYRET, "dlyret", "Delay Return", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    def (P_LIMIT, "limit", "Limiter", "", K_BOOL, 0, 1, 1, 1);

    choice (P_MIDICH, "midich", "MIDI Channel", kMidiChNames, 17, 0);
    def (P_VELSENS, "velsens", "Velocity Sens", "%", K_FLOAT, 0, 1, 0, 0.5f, F_PCT);
    choice (P_QUALITY, "quality", "Quality", kQualityNames, 3, 1);
    choice (P_HOSTFOLLOW, "hostfollow", "Host Transport", kHostFollowNames, 3, 2);
    choice (P_PRESET, "preset", "Factory Preset", presetNames, presetCount, 0, false);
    def (P_PRESETLOAD, "presetload", "Load Preset", "", K_MOMENT, 0, 1, 1, 0);
    def (P_KIT, "kit", "Kit", "", K_LIST, 0, 1, 0, 0, F_NUM, MAP_LIN, false);
    def (P_KITLOAD, "kitload", "Load Kit", "", K_MOMENT, 0, 1, 1, 0);
    def (P_KITSAVE, "kitsave", "Save Kit", "", K_MOMENT, 0, 1, 1, 0);

    for (int c = 0; c < kClips; ++c)
    {
        char k[20], n[28];
        std::snprintf (k, sizeof k, "cvol%d", c); std::snprintf (n, sizeof n, "Clip %d Volume", c + 1);
        def (P_CVOL0 + c, k, n, "dB", K_FLOAT, -60, 6, 0.1f, 0, F_VOLDB, MAP_LIN, false);
        std::snprintf (k, sizeof k, "cpan%d", c); std::snprintf (n, sizeof n, "Clip %d Pan", c + 1);
        def (P_CPAN0 + c, k, n, "", K_FLOAT, -1, 1, 0, 0, F_PAN, MAP_LIN, false);
        std::snprintf (k, sizeof k, "cmute%d", c); std::snprintf (n, sizeof n, "Clip %d Mute", c + 1);
        def (P_CMUTE0 + c, k, n, "", K_BOOL, 0, 1, 1, 0, F_NUM, MAP_LIN, false);
        std::snprintf (k, sizeof k, "pad%d", c); std::snprintf (n, sizeof n, "Pad %d", c + 1);
        def (P_PAD0 + c, k, n, "", K_MOMENT, 0, 1, 1, 0);
        std::snprintf (k, sizeof k, "spad%d", c); std::snprintf (n, sizeof n, "Slice Pad %d", c + 1);
        def (P_SPAD0 + c, k, n, "", K_MOMENT, 0, 1, 1, 0);
        std::snprintf (k, sizeof k, "cname%d", c); std::snprintf (n, sizeof n, "Clip %d Name", c + 1);
        def (P_CNAME0 + c, k, n, "", K_TEXT, 0, 1000, 0, 0);
        std::snprintf (k, sizeof k, "cinfo%d", c); std::snprintf (n, sizeof n, "Clip %d Info", c + 1);
        def (P_CINFO0 + c, k, n, "", K_TEXT, 0, 1000, 0, 0);
        std::snprintf (k, sizeof k, "cinfob%d", c); std::snprintf (n, sizeof n, "Clip %d Info 2", c + 1);
        def (P_CINFOB0 + c, k, n, "", K_TEXT, 0, 1000, 0, 0);
        std::snprintf (k, sizeof k, "cstat%d", c); std::snprintf (n, sizeof n, "Clip %d State", c + 1);
        def (P_CSTAT0 + c, k, n, "", K_READ, 0, 1, 0, 0);
    }
    for (int i = 0; i < 64; ++i) { char k[20], n[28]; std::snprintf (k, sizeof k, "thumb%d", i); std::snprintf (n, sizeof n, "Thumb %d.%d", i / 4 + 1, i % 4 + 1); def (P_THUMB0 + i, k, n, "", K_READ, 0, 1, 0, 0); }
    for (int i = 0; i < kWaveBoxes; ++i) { char k[20], n[28]; std::snprintf (k, sizeof k, "wave%d", i); std::snprintf (n, sizeof n, "Wave %d", i + 1); def (P_WAVE0 + i, k, n, "", K_READ, 0, 1, 0, 0); }
    for (int i = 0; i < kMarks; ++i) { char k[20], n[28]; std::snprintf (k, sizeof k, "mark%d", i); std::snprintf (n, sizeof n, "Marker %d", i + 1); def (P_MARK0 + i, k, n, "", K_READ, 0, 1, 0, 0); }
    for (int s = 0; s < 4; ++s) { char k[20], n[28]; std::snprintf (k, sizeof k, "sceneled%d", s); std::snprintf (n, sizeof n, "Scene %d State", s + 1); def (P_SCENELED0 + s, k, n, "", K_READ, 0, 1, 0, 0); }
    def (P_BEATLED, "beatled", "Beat", "", K_READ, 0, 1, 0, 0);
    def (P_TRANSPORT, "transport", "Transport", "", K_TEXT, 0, 1000, 0, 0);
    def (P_STATUS, "status", "Status", "", K_TEXT, 0, 1000, 0, 0);
    def (P_SELNAME, "selname", "Selected Clip Info", "", K_TEXT, 0, 1000, 0, 0);
    def (P_LIBINFO, "libinfo", "Library", "", K_TEXT, 0, 1000, 0, 0);
    def (P_SLICEINFO, "sliceinfo", "Slice Info", "", K_TEXT, 0, 1000, 0, 0);
    def (P_METER_L, "meterl", "Meter L", "dB", K_READ, kMeterLo, kMeterHi, 0, kMeterLo, F_DB);
    def (P_METER_R, "meterr", "Meter R", "dB", K_READ, kMeterLo, kMeterHi, 0, kMeterLo, F_DB);
}

// normalised <-> plain
inline float toPlain (const ParamDef& d, float norm)
{
    norm = clampf (norm, 0.f, 1.f);
    if (d.kind == K_LIST || d.kind == K_TEXT) return d.kind == K_LIST ? norm : d.lo + norm * (d.hi - d.lo);
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return norm >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return std::floor (norm * (float) (d.n - 1) + 0.5f);
    float v;
    switch (d.map)
    {
        case MAP_LOG: v = d.lo * std::pow (d.hi / d.lo, norm); break;
        case MAP_SQR: v = d.lo + norm * norm * (d.hi - d.lo); break;
        default:      v = d.lo + norm * (d.hi - d.lo); break;
    }
    if (d.step > 0.f) v = d.lo + std::floor ((v - d.lo) / d.step + 0.5f) * d.step;
    return clampf (v, d.lo, d.hi);
}
inline float toNorm (const ParamDef& d, float plain)
{
    if (d.kind == K_LIST) return clampf (plain, 0.f, 1.f);
    if (d.kind == K_BOOL || d.kind == K_MOMENT) return plain >= 0.5f ? 1.f : 0.f;
    if (d.kind == K_CHOICE) return d.n > 1 ? clampf (plain / (float) (d.n - 1), 0.f, 1.f) : 0.f;
    const float p = clampf (plain, d.lo, d.hi);
    switch (d.map)
    {
        case MAP_LOG: return clampf (std::log (p / d.lo) / std::log (d.hi / d.lo), 0.f, 1.f);
        case MAP_SQR: return clampf (std::sqrt ((p - d.lo) / (d.hi - d.lo)), 0.f, 1.f);
        default:      return clampf ((p - d.lo) / (d.hi - d.lo), 0.f, 1.f);
    }
}

// generic display (the plugin handles texts, lists and positions itself)
inline void formatValue (const ParamDef& p, float x, char* out, size_t max)
{
    if (p.kind == K_CHOICE && p.choices != nullptr) { std::snprintf (out, max, "%s", p.choices[clampi ((int) x, 0, p.n - 1)]); return; }
    if (p.kind == K_BOOL || p.kind == K_MOMENT) { std::snprintf (out, max, "%s", x >= 0.5f ? "On" : "Off"); return; }
    switch (p.fmt)
    {
        case F_DB:    std::snprintf (out, max, "%+.1f dB", (double) x); return;
        case F_VOLDB: if (x <= -59.9f) std::snprintf (out, max, "Off"); else std::snprintf (out, max, "%+.1f dB", (double) x); return;
        case F_PCT:   std::snprintf (out, max, "%.0f %%", (double) (x * 100.f)); return;
        case F_HZ:    if (x >= 1000.f) std::snprintf (out, max, "%.2f kHz", (double) (x / 1000.f)); else std::snprintf (out, max, "%.0f Hz", (double) x); return;
        case F_MS:    if (x < 10.f) std::snprintf (out, max, "%.1f ms", (double) x); else std::snprintf (out, max, "%.0f ms", (double) x); return;
        case F_SEMI:  std::snprintf (out, max, "%+d st", (int) std::lround (x)); return;
        case F_CENT:  std::snprintf (out, max, "%+d ct", (int) std::lround (x)); return;
        case F_PAN:   if (std::fabs (x) < 0.01f) std::snprintf (out, max, "C"); else std::snprintf (out, max, "%s%.0f", x < 0 ? "L" : "R", (double) (std::fabs (x) * 100.f)); return;
        case F_BPM:   std::snprintf (out, max, "%.2f", (double) x); return;
        case F_NOTE:  noteName ((int) std::lround (x), out, max); return;
        case F_BITS:  std::snprintf (out, max, "%d bit", (int) std::lround (x)); return;
        case F_RATIO: if (x <= 1.01f) std::snprintf (out, max, "Off"); else std::snprintf (out, max, "%.1f:1", (double) x); return;
        case F_SWING: std::snprintf (out, max, "%.1f %%", (double) x); return;
        case F_INT:   std::snprintf (out, max, "%d", (int) std::lround (x)); return;
        default:      std::snprintf (out, max, "%.2f", (double) x); return;
    }
}
} // namespace cp
