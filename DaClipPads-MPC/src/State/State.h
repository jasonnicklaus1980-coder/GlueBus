#pragma once
// Project state, kits and factory presets all use one plain-text format, one "key=value" per line:
//   DaClipPads 1                      header (format version)
//   p.<param key>=<value>             a global parameter (tempo, swing, sampler, master FX, MIDI ...)
//   c<N>.<field>=<value>              clip N (0..15): file, points, pitch, stretch, FX, follow, note, slices
//   scene<N>=<mask>                   scene membership, one bit per clip
// Presets may also use all.<field> (every clip) and row<R>.<field> (clips 4R..4R+3).
// Unknown keys are ignored and missing keys keep their current value, so older states load in newer versions.
// MPC saves the state with the project through the VST2 chunk (effGetChunk / effSetChunk).
#include "../ClipEngine/Clip.h"
#include <cstdarg>
#include <cstdlib>

namespace cp
{
struct StateWriter
{
    char* buf = nullptr; size_t len = 0, cap = 0;
    ~StateWriter() { std::free (buf); }
    void add (const char* fmt, ...) __attribute__ ((format (printf, 2, 3)))
    {
        char line[1024];
        va_list ap; va_start (ap, fmt); int n = std::vsnprintf (line, sizeof line, fmt, ap); va_end (ap);
        if (n < 0) return;
        if (n >= (int) sizeof line) n = (int) sizeof line - 1;
        if (len + (size_t) n + 2 > cap)
        {
            const size_t nc = (cap ? cap * 2 : 16384) + (size_t) n;
            char* nb = (char*) std::realloc (buf, nc);
            if (nb == nullptr) return;
            buf = nb; cap = nc;
        }
        std::memcpy (buf + len, line, (size_t) n); len += (size_t) n; buf[len++] = '\n'; buf[len] = 0;
    }
};

// Calls fn(key, value) for each "key=value" line.
template <typename Fn> inline void parseState (const char* text, size_t size, Fn&& fn)
{
    size_t i = 0;
    char line[1024];
    while (i < size)
    {
        size_t n = 0;
        while (i < size && text[i] != '\n' && text[i] != 0) { if (n + 1 < sizeof line && text[i] != '\r') line[n++] = text[i]; ++i; }
        ++i;
        line[n] = 0;
        char* eq = std::strchr (line, '=');
        if (eq == nullptr || line[0] == '#') continue;
        *eq = 0;
        fn ((const char*) line, (const char*) (eq + 1));
    }
}

inline void writeClip (StateWriter& w, int i, const ClipSettings& c)
{
    w.add ("c%d.file=%s", i, c.file);
    w.add ("c%d.mode=%d", i, (int) c.mode); w.add ("c%d.quant=%d", i, (int) c.quant);
    w.add ("c%d.start=%.7f", i, (double) (float) c.start); w.add ("c%d.end=%.7f", i, (double) (float) c.end);
    w.add ("c%d.lstart=%.7f", i, (double) (float) c.lstart); w.add ("c%d.lend=%.7f", i, (double) (float) c.lend);
    w.add ("c%d.fadein=%.3f", i, (double) (float) c.fadeIn); w.add ("c%d.fadeout=%.3f", i, (double) (float) c.fadeOut);
    w.add ("c%d.xfade=%.3f", i, (double) (float) c.xfade);
    w.add ("c%d.reverse=%d", i, (int) c.reverse); w.add ("c%d.semis=%d", i, (int) c.semis); w.add ("c%d.cents=%d", i, (int) c.cents);
    w.add ("c%d.pmode=%d", i, (int) c.pmode); w.add ("c%d.slen=%d", i, (int) c.slen); w.add ("c%d.stype=%d", i, (int) c.stype);
    w.add ("c%d.vol=%.3f", i, (double) (float) c.vol); w.add ("c%d.pan=%.4f", i, (double) (float) c.pan);
    w.add ("c%d.mute=%d", i, (int) c.mute); w.add ("c%d.normalize=%d", i, (int) c.normalize);
    w.add ("c%d.filt=%d", i, (int) c.filt); w.add ("c%d.cutoff=%.2f", i, (double) (float) c.cutoff); w.add ("c%d.res=%.4f", i, (double) (float) c.res);
    w.add ("c%d.drive=%.4f", i, (double) (float) c.drive); w.add ("c%d.rsend=%.4f", i, (double) (float) c.rsend); w.add ("c%d.dsend=%.4f", i, (double) (float) c.dsend);
    w.add ("c%d.vintage=%d", i, (int) c.vintage);
    w.add ("c%d.follow=%d", i, (int) c.follow); w.add ("c%d.ftime=%d", i, (int) c.ftime); w.add ("c%d.fscene=%d", i, (int) c.fscene);
    w.add ("c%d.note=%d", i, (int) c.note);
    const int n = c.sliceCount;
    w.add ("c%d.slices=%d", i, n);
    if (n > 0)
    {
        char a[kMaxSlices * 12 + 16] = {}, b[kMaxSlices * 5 + 8] = {}, s[kMaxSlices * 4 + 8] = {};
        size_t la = 0, lb = 0, ls = 0;
        for (int k = 0; k <= n; ++k) la += (size_t) std::snprintf (a + la, sizeof a - la, "%s%.7f", k ? "," : "", (double) (float) c.slice[k]);
        for (int k = 0; k < n; ++k) lb += (size_t) std::snprintf (b + lb, sizeof b - lb, "%s%d", k ? "," : "", (int) c.slicePitch[k]);
        for (int k = 0; k < n; ++k) ls += (size_t) std::snprintf (s + ls, sizeof s - ls, "%s%d", k ? "," : "", (int) c.seq[k]);
        w.add ("c%d.slicepos=%s", i, a); w.add ("c%d.slicepitch=%s", i, b); w.add ("c%d.seq=%s", i, s);
        w.add ("c%d.slicerev=%d", i, (int) c.sliceRev); w.add ("c%d.seqon=%d", i, (int) c.seqOn);
    }
}

// Applies one clip field. Returns false for an unknown field.
inline bool readClipField (ClipSettings& c, const char* f, const char* v)
{
    const double d = std::atof (v); const int n = toInt (v);
    auto list = [&] (auto&& put) { int k = 0; const char* p = v; while (*p && k <= kMaxSlices) { put (k++, std::atof (p)); const char* q = std::strchr (p, ','); if (! q) break; p = q + 1; } };
    if (! std::strcmp (f, "file")) { std::snprintf (c.file, sizeof c.file, "%s", v); return true; }
    if (! std::strcmp (f, "mode")) { c.mode = clampi (n, 0, M_COUNT - 1); return true; }
    if (! std::strcmp (f, "quant")) { c.quant = clampi (n, 0, Q_COUNT); return true; }
    if (! std::strcmp (f, "start")) { c.start = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "end")) { c.end = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "lstart")) { c.lstart = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "lend")) { c.lend = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "fadein")) { c.fadeIn = clampf ((float) d, 0.f, 2000.f); return true; }
    if (! std::strcmp (f, "fadeout")) { c.fadeOut = clampf ((float) d, 0.f, 2000.f); return true; }
    if (! std::strcmp (f, "xfade")) { c.xfade = clampf ((float) d, 0.f, 500.f); return true; }
    if (! std::strcmp (f, "reverse")) { c.reverse = n ? 1 : 0; return true; }
    if (! std::strcmp (f, "semis")) { c.semis = clampi (n, -12, 12); return true; }
    if (! std::strcmp (f, "cents")) { c.cents = clampi (n, -100, 100); return true; }
    if (! std::strcmp (f, "pmode")) { c.pmode = clampi (n, 0, 1); return true; }
    if (! std::strcmp (f, "slen")) { c.slen = clampi (n, 0, SL_COUNT - 1); return true; }
    if (! std::strcmp (f, "stype")) { c.stype = clampi (n, 0, ST_COUNT - 1); return true; }
    if (! std::strcmp (f, "vol")) { c.vol = clampf ((float) d, -60.f, 6.f); return true; }
    if (! std::strcmp (f, "pan")) { c.pan = clampf ((float) d, -1.f, 1.f); return true; }
    if (! std::strcmp (f, "mute")) { c.mute = n ? 1 : 0; return true; }
    if (! std::strcmp (f, "normalize")) { c.normalize = n ? 1 : 0; return true; }
    if (! std::strcmp (f, "filt")) { c.filt = clampi (n, 0, 3); return true; }
    if (! std::strcmp (f, "cutoff")) { c.cutoff = clampf ((float) d, 20.f, 20000.f); return true; }
    if (! std::strcmp (f, "res")) { c.res = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "drive")) { c.drive = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "rsend")) { c.rsend = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "dsend")) { c.dsend = clampf ((float) d, 0.f, 1.f); return true; }
    if (! std::strcmp (f, "vintage")) { c.vintage = n ? 1 : 0; return true; }
    if (! std::strcmp (f, "follow")) { c.follow = clampi (n, 0, FA_COUNT - 1); return true; }
    if (! std::strcmp (f, "ftime")) { c.ftime = clampi (n, 0, FTIME_COUNT - 1); return true; }
    if (! std::strcmp (f, "fscene")) { c.fscene = clampi (n, 0, 3); return true; }
    if (! std::strcmp (f, "note")) { c.note = clampi (n, -1, 127); return true; }
    if (! std::strcmp (f, "slices")) { c.sliceCount = clampi (n, 0, kMaxSlices); return true; }
    if (! std::strcmp (f, "slicepos")) { list ([&] (int k, double x) { c.slice[k] = clampf ((float) x, 0.f, 1.f); }); return true; }
    if (! std::strcmp (f, "slicepitch")) { list ([&] (int k, double x) { if (k < kMaxSlices) c.slicePitch[k] = clampi ((int) x, -12, 12); }); return true; }
    if (! std::strcmp (f, "seq")) { list ([&] (int k, double x) { if (k < kMaxSlices) c.seq[k] = clampi ((int) x, 0, kMaxSlices - 1); }); return true; }
    if (! std::strcmp (f, "slicerev")) { c.sliceRev = n; return true; }
    if (! std::strcmp (f, "seqon")) { c.seqOn = n ? 1 : 0; return true; }
    return false;
}
} // namespace cp
