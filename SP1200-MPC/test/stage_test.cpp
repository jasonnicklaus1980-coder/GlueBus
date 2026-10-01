// Stage-level validation of the SP-1200 model (no VST layer): each stage is exercised with test signals and the
// results are compared with the documented / reported hardware figures. Also writes docs/MEASUREMENTS.md.
//   make stagetest
#include "sp/Model.h"
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sp;
static int fails = 0, checks = 0;
static std::string report;
#define CHECK(cond, ...) do { ++checks; char _b[512]; std::snprintf (_b, sizeof _b, __VA_ARGS__); \
    if (! (cond)) { ++fails; std::printf ("  FAIL: %s\n", _b); } else std::printf ("  ok:   %s\n", _b); } while (0)
static void line (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void line (const char* fmt, ...) { char b[512]; va_list ap; va_start (ap, fmt); std::vsnprintf (b, sizeof b, fmt, ap); va_end (ap); report += b; report += "\n"; }

static double FS = 44100.0;
static Model* M;
static std::vector<float> L, R;

static Settings base()
{
    Settings s;
    s.channel = 3;                  // Out 7-8 (unfiltered) unless a test says otherwise
    s.hissDb = -60; s.anaDb = -60; s.convDb = -60; s.humDb = -60; s.groundDb = -60; s.digDb = -60;
    s.analyzer = false;
    return s;
}
static void run (const Settings& s, const std::vector<float>& in, double rate = 44100.0)
{
    if (rate != FS || M == nullptr) { FS = rate; }
    M->prepare (FS);
    L.assign (in.size(), 0.f); R.assign (in.size(), 0.f);
    for (size_t p = 0; p < in.size(); p += 256)
    {
        const int m = (int) std::min<size_t> (256, in.size() - p);
        M->process (&in[p], &in[p], &L[p], &R[p], m, s);
    }
}
static std::vector<float> sine (double hz, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * FS));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2 * kPi * hz * i / FS));
    return v;
}
static double goertzel (const std::vector<float>& x, size_t a, size_t b, double hz)      // amplitude of a sinusoid
{
    const double w = 2 * kPi * hz / FS, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double win = 0.5 - 0.5 * std::cos (2 * kPi * (i - a) / (b - a - 1)); const double s0 = x[i] * win + c * s1 - s2; s2 = s1; s1 = s0; }
    return 2.0 * std::sqrt (std::fmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / ((b - a) * 0.5);
}
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (b - a)); }
static double db (double g) { return 20 * std::log10 (g + 1e-15); }
// A-weighting (IEC 61672) as a cascade of biquads designed by bilinear transform at FS
static std::vector<float> aweight (const std::vector<float>& x)
{
    // poles: 20.6 Hz (x2), 107.7 Hz, 737.9 Hz, 12194 Hz (x2); zeros: 4 at 0 Hz
    std::vector<float> y = x;
    auto hp1 = [&] (double f) { const double k = std::tan (kPi * f / FS); const double a0 = 1 + k, b0 = 1 / a0, a1 = (k - 1) / a0; double z = 0, xp = 0;
        for (auto& v : y) { const double o = b0 * (v - xp) - a1 * z; xp = v; z = o; v = (float) o; } };
    auto lp1 = [&] (double f) { const double k = std::tan (kPi * f / FS); const double a0 = 1 + k, b0 = k / a0, a1 = (k - 1) / a0; double z = 0, xp = 0;
        for (auto& v : y) { const double o = b0 * (v + xp) - a1 * z; xp = v; z = o; v = (float) o; } };
    hp1 (20.6); hp1 (20.6); hp1 (107.7); hp1 (737.9); lp1 (12194.0); lp1 (12194.0);
    // normalise to 0 dB at 1 kHz
    std::vector<float> t ((size_t) FS);
    for (size_t i = 0; i < t.size(); ++i) t[i] = (float) std::sin (2 * kPi * 1000.0 * i / FS);
    std::vector<float> save = y; y = t; hp1 (20.6); hp1 (20.6); hp1 (107.7); hp1 (737.9); lp1 (12194.0); lp1 (12194.0);
    const double g = rms (t, t.size() / 2, t.size()) / rms (y, y.size() / 2, y.size());
    for (auto& v : save) v = (float) (v * g);
    return save;
}

int main()
{
    M = new Model();
    const size_t T0 = 8192;                                    // settle time before measuring

    std::printf ("[1] sampling clock and DAC staircase\n");
    line ("## 1. Clock and resolution\n");
    {
        Settings s = base(); s.recon = RC_RAW; s.analogOn = false;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            FS = rate;
            auto in = sine (440.0, 0.8, 1.0);
            run (s, in, rate);
            int ch = 0; for (size_t i = T0 + 1; i < L.size(); ++i) if (L[i] != L[i - 1]) ++ch;
            const double perSec = ch * FS / (L.size() - T0);
            CHECK (std::fabs (perSec - 26040.0) < 150.0, "DAC steps per second at host %.0f Hz: %.0f (26040)", rate, perSec);
            line ("- DAC steps per second at host %.0f Hz: **%.0f** (hardware clock 26 040 Hz)", rate, perSec);
        }
        FS = 44100.0;
        Settings g = s; g.pitch = -8; g.mode = PM_GRIT;
        auto in = sine (440.0, 0.8, 1.0); run (g, in);
        int ch = 0; for (size_t i = T0 + 1; i < L.size(); ++i) if (L[i] != L[i - 1]) ++ch;
        const double distinct = ch * FS / (L.size() - T0);
        CHECK (distinct < 26040 * 0.66, "45>33 GRIT at -8 st: new values per second %.0f (sampling clock 26040 x 0.63 = 16400)", distinct);
        line ("- 45>33 GRIT at -8 st: **%.0f** new values per second (sampling clock 16 400 Hz, DAC still 26 040)", distinct);
        // 12-bit: a -80 dBFS sine gives at most a few output levels, -40 dBFS gives ~ 2 x 10^(-40/20) x 2048 + 1 levels
        for (double lev : { -80.0, -40.0 })
        {
            auto in2 = sine (440.0, std::pow (10.0, lev / 20.0), 0.5); run (s, in2);
            std::vector<float> lv; for (size_t i = T0; i < L.size(); ++i) { bool f = false; for (float x : lv) if (x == L[i]) { f = true; break; } if (! f) lv.push_back (L[i]); }
            const double want = 2 * std::pow (10.0, lev / 20.0) * 2048 + 1;
            CHECK (lev < -60 ? lv.size() <= 3 : std::fabs ((double) lv.size() - want) < 5, "%.0f dBFS sine: %zu output levels (12-bit: ~%.0f)", lev, lv.size(), want);
            line ("- %.0f dBFS sine: **%zu** distinct output levels (12-bit ideal: %.0f)", lev, lv.size(), want);
        }
    }

    std::printf ("[2] zero-order hold and reconstruction\n");
    line ("\n## 2. Zero-order hold (Out 7-8, unfiltered)\n");
    line ("| tone | measured | ideal sinc(f/26.04k) |\n|---|---|---|");
    {
        Settings s = base(); s.analogOn = false;
        for (double f : { 1000.0, 5000.0, 10000.0, 12500.0 })
        {
            auto in = sine (f, 0.5, 0.5); run (s, in);
            const double g = goertzel (L, T0, L.size(), f) / 0.5;
            const double x = kPi * f / 26040.0, ideal = std::sin (x) / x;
            CHECK (std::fabs (db (g) - db (ideal)) < 0.6, "ZOH response at %.0f Hz: %.2f dB (sinc: %.2f dB)", f, db (g), db (ideal));
            line ("| %.1f kHz | %.2f dB | %.2f dB |", f / 1000, db (g), db (ideal));
        }
        // image of a 3 kHz tone at 26.04 - 3 = 23.04 kHz folds to 44.1 - 23.04 = 21.06 kHz with raw steps; band-limited
        // steps keep images below 22.05 kHz (e.g. the first image's lower side at 23.04 kHz is above it, so it must vanish)
        auto in = sine (3000.0, 0.5, 0.5);
        run (s, in); const double bl = goertzel (L, T0, L.size(), 44100.0 - 23040.0);
        Settings r = s; r.recon = RC_RAW; run (r, in); const double raw = goertzel (L, T0, L.size(), 44100.0 - 23040.0);
        CHECK (db (raw) - db (bl) > 20.0, "image above host Nyquist folded to 21.06 kHz: raw steps %.1f dB, band-limited %.1f dB", db (raw / 0.5), db (bl / 0.5));
        line ("\nA 3 kHz tone's first image (23.04 kHz) is above the 44.1 kHz host's Nyquist. With raw host-grid steps it folds "
              "to 21.06 kHz at **%.1f dB**; with band-limited steps (hardware model) it stays out: **%.1f dB**.", db (raw / 0.5), db (bl / 0.5));
        // an image below host Nyquist is KEPT: 8 kHz tone -> image at 18.04 kHz
        auto in8 = sine (8000.0, 0.5, 0.5); run (s, in8);
        const double img = goertzel (L, T0, L.size(), 18040.0) / 0.5;
        const double x = kPi * 18040.0 / 26040.0, want = std::sin (x) / x;
        CHECK (std::fabs (db (img) - db (want)) < 1.5, "8 kHz tone: image at 18.04 kHz kept at %.1f dB (ZOH predicts %.1f dB)", db (img), db (want));
        line ("An 8 kHz tone's image at 18.04 kHz is kept: **%.1f dB** (the ZOH predicts %.1f dB).", db (img), db (want));
    }

    std::printf ("[3] anti-alias filter and sampling aliases\n");
    line ("\n## 3. Sampling: what folds back\n");
    {
        Settings s = base(); s.channel = 3;
        // 18 kHz into a 26.04 kHz converter aliases to 8.04 kHz
        auto in = sine (18000.0, 0.5, 0.5);
        Settings off = s; off.aaHz = 22000; run (off, in); const double a0 = goertzel (L, T0, L.size(), 8040.0);
        run (s, in); const double a1 = goertzel (L, T0, L.size(), 8040.0);
        CHECK (db (a0) - db (a1) > 6.0 && db (a1 / 0.5) > -40.0, "18 kHz input -> 8.04 kHz alias: AA off %.1f dB, AA on (15 kHz, 4-pole) %.1f dB (leaky, not removed)", db (a0 / 0.5), db (a1 / 0.5));
        line ("- 18 kHz input aliases to 8.04 kHz: **%.1f dB** without the anti-alias filter, **%.1f dB** with it (15 kHz, 4th order: leaky by design).", db (a0 / 0.5), db (a1 / 0.5));
        // sample rate moves the alias: 12 kHz at 20 kHz sampling -> 8 kHz alias; at 26.04 kHz no alias (below 13.02)
        auto in12 = sine (12000.0, 0.5, 0.5);
        Settings sr = off; sr.srate = 20000; run (sr, in12); const double a20 = goertzel (L, T0, L.size(), 8000.0);
        sr.srate = 26040; run (sr, in12); const double a26 = goertzel (L, T0, L.size(), 8000.0);
        CHECK (db (a20) - db (a26) > 30.0, "12 kHz tone: alias at 8 kHz %.1f dB at 20 kHz sampling, %.1f dB at 26.04 kHz", db (a20 / 0.5), db (a26 / 0.5));
        line ("- 12 kHz tone: alias at 8 kHz **%.1f dB** with SAMPLE RATE 20 kHz, **%.1f dB** at 26.04 kHz.", db (a20 / 0.5), db (a26 / 0.5));
    }

    std::printf ("[4] pitch-dependent aliasing (drop-sample reads)\n");
    line ("\n## 4. Pitch: drop-sample aliasing changes with every setting\n");
    line ("A 1 kHz tone through PITCH mode (live drop-sample read heads). *Spur* = everything except the pitched tone "
          "(+-200 Hz) and DC, relative to the tone. At large intervals part of it is the 79 ms splice of the live-stream "
          "read heads (sidebands near the tone); REPLAY, the hardware-faithful mode, has no splice.\n\n| semitones | tone at | spur level | top spur |\n|---|---|---|---|");
    {
        Settings s = base(); s.mode = PM_PITCH; s.hwRange = false; s.analogOn = false;
        std::vector<double> spurs;
        for (int st : { -12, -7, -5, 0, 5, 7, 12 })
        {
            s.pitch = (float) st;
            auto in = sine (1000.0, 0.5, 1.0); run (s, in);
            const double f = 1000.0 * std::pow (2.0, st / 12.0);
            // spectrum by Goertzel sweep (50 Hz steps)
            double tone = goertzel (L, 16384, L.size(), f), spurP = 0, top = 0, topF = 0;
            for (double g = 100; g < 20000; g += 50)
            {
                if (std::fabs (g - f) < 200) continue;
                const double a = goertzel (L, 16384, L.size(), g);
                spurP += a * a; if (a > top) { top = a; topF = g; }
            }
            const double sp = db (std::sqrt (spurP) / tone);
            spurs.push_back (sp);
            line ("| %+d | %.1f Hz | %.1f dB | %.0f Hz |", st, f, sp, topF);
            std::printf ("    %+3d st: spur %.1f dB, top spur at %.0f Hz\n", st, sp, topF);
        }
        double mn = 1e9, mx = -1e9; for (double x : spurs) { mn = std::fmin (mn, x); mx = std::fmax (mx, x); }
        CHECK (mx - mn > 6.0, "spur level varies across pitch settings by %.1f dB (not a static effect)", mx - mn);
        CHECK (spurs[3] < spurs[0] && spurs[3] < spurs[6], "0 st is cleaner than +-12 st (%.1f vs %.1f / %.1f dB)", spurs[3], spurs[0], spurs[6]);
        // ALIAS BYPASS (interpolated reads) reduces the read aliasing at +7
        s.pitch = 7; auto in = sine (1000.0, 0.5, 1.0);
        run (s, in); const double fa = 1000.0 * std::pow (2.0, 7 / 12.0);
        double spA = 0; for (double g = 100; g < 20000; g += 50) if (std::fabs (g - fa) > 200) { const double a = goertzel (L, 16384, L.size(), g); spA += a * a; }
        Settings b = s; b.alias = AL_BYPASS; run (b, in);
        double spB = 0; for (double g = 100; g < 20000; g += 50) if (std::fabs (g - fa) > 200) { const double a = goertzel (L, 16384, L.size(), g); spB += a * a; }
        CHECK (db (std::sqrt (spA)) - db (std::sqrt (spB)) > 3.0, "+7 st: drop-sample spurs %.1f dB above interpolated (ALIAS BYPASS)", db (std::sqrt (spA)) - db (std::sqrt (spB)));
        line ("\nAt +7 st the drop-sample reads produce **%.1f dB** more spurious energy than interpolated reads (ALIAS BYPASS).", db (std::sqrt (spA)) - db (std::sqrt (spB)));
    }

    std::printf ("[5] REPLAY: hits play from memory at the tuned rate\n");
    {
        Settings s = base(); s.mode = PM_REPLAY; s.hwRange = false;
        // a decaying 'drum' every 0.5 s
        std::vector<float> in ((size_t) FS * 2);
        for (size_t i = 0; i < in.size(); ++i) { const double t = std::fmod ((double) i / FS, 0.5); in[i] = (float) (0.8 * std::exp (-t * 12) * std::sin (2 * kPi * 180 * t)); }
        auto energyLen = [&] () { size_t last = 0; for (size_t i = (size_t) FS; i < (size_t) (FS * 1.5); ++i) if (std::fabs (L[i]) > 0.01f) last = i; return (double) (last - FS) / FS; };
        s.pitch = 7; run (s, in); const double up = energyLen();
        s.pitch = -8; run (s, in); const double dn = energyLen();
        s.pitch = 0; run (s, in); const double z = energyLen();
        CHECK (up < z && z < dn + 0.001, "a hit lasts %.3f s at +7, %.3f s at 0, %.3f s at -8 (shorter up, longer down)", up, z, dn);
        line ("\n## 5. REPLAY\nA decaying 180 Hz hit lasts **%.3f s** at +7 st, **%.3f s** at 0 and **%.3f s** at -8 st "
              "(tuned up the sample plays faster and ends sooner; down it is slower and longer).", up, z, dn);
    }

    std::printf ("[6] level DAC decay steps\n");
    {
        Settings s = base(); s.analogOn = false; s.decay = 0.3f; s.recon = RC_RAW;
        std::vector<float> in ((size_t) FS);
        for (size_t i = 0; i < in.size(); ++i) in[i] = i < 20 ? 0.f : 0.5f;          // a step: one hit, then DC
        run (s, in);
        // the output falls in 8-bit level steps: count distinct values late in the decay
        std::vector<float> lv; for (size_t i = (size_t) (0.25 * FS); i < (size_t) (0.3 * FS); ++i) { bool f = false; for (float x : lv) if (std::fabs (x - L[i]) < 1e-7f) { f = true; break; } if (! f) lv.push_back (L[i]); }
        CHECK (lv.size() >= 2 && lv.size() < 12, "decay tail 250-300 ms falls in %zu discrete 8-bit level steps", lv.size());
        line ("\n## 6. Level DAC\nA held DC level decaying (300 ms) falls in **%zu** discrete steps between 250 and 300 ms: the 8-bit level DAC.", lv.size());
    }

    std::printf ("[7] output channel filters\n");
    line ("\n## 7. Output channels (relative to Out 7-8)\n\n| output | 2 kHz | 7.5 kHz | 10 kHz | 12 kHz |\n|---|---|---|---|---|");
    {
        Settings s = base(); s.analogOn = true; s.ampsOn = false;
        double ref[4];
        const double fr[4] { 2000, 7500, 10000, 12000 };
        for (int k = 0; k < 4; ++k) { s.channel = 3; auto in = sine (fr[k], 0.3, 0.5); run (s, in); ref[k] = goertzel (L, T0, L.size(), fr[k]); }
        for (int ch : { 1, 2 })
        {
            double r[4];
            for (int k = 0; k < 4; ++k) { s.channel = ch; auto in = sine (fr[k], 0.3, 0.5); run (s, in); r[k] = db (goertzel (L, T0, L.size(), fr[k]) / ref[k]); }
            line ("| Out %s | %.1f dB | %.1f dB | %.1f dB | %.1f dB |", ch == 1 ? "3-4" : "5-6", r[0], r[1], r[2], r[3]);
            const double atCut = ch == 1 ? r[1] : r[2];
            CHECK (std::fabs (r[0]) < 1.0 && atCut < -1.5 && atCut > -4.5, "Out %s: flat at 2 kHz (%.1f dB), %.1f dB at its corner", ch == 1 ? "3-4" : "5-6", r[0], atCut);
        }
        // Out 1-2: bright on the hit, dark after the envelope closes
        std::vector<float> in ((size_t) FS);
        for (size_t i = 0; i < in.size(); ++i) { const double t = (double) i / FS; in[i] = (float) ((i % 22050) < 4000 ? 0.4 * std::sin (2 * kPi * 4000 * t) : 0.0); }
        s.channel = 0; run (s, in);
        const double early = rms (L, 22050 + 30, 22050 + 30 + 44), late = rms (L, 22050 + 2500, 22050 + 3500);
        CHECK (db (early) - db (late) > 15.0, "Out 1-2 (SSM2044): a 4 kHz burst is %.1f dB louder just after the hit than once the filter has closed", db (early) - db (late));
        line ("| Out 1-2 | SSM2044 envelope: a 4 kHz burst is %.1f dB louder in its first ms than after the filter closes | | | |", db (early) - db (late));
    }

    std::printf ("[8] analog stages: level-dependent behaviour\n");
    line ("\n## 8. Distortion (1 kHz, THD = 2nd-5th harmonics)\n\n| condition | THD |\n|---|---|");
    {
        auto thd = [&] (const Settings& s, double amp, double f = 1000.0)
        {
            auto in = sine (f, amp, 0.5); run (s, in);
            const double h1 = goertzel (L, T0, L.size(), f); double hs = 0;
            for (int k = 2; k <= 5; ++k) { const double a = goertzel (L, T0, L.size(), f * k); hs += a * a; }
            return 100.0 * std::sqrt (hs) / h1;
        };
        Settings s = base(); s.channel = 3;
        const double t10 = thd (s, 0.316);
        CHECK (t10 < 0.1, "Out 7-8 at -10 dBFS: THD %.3f %% (published 0.05 %%)", t10);
        line ("| Out 7-8, -10 dBFS (published spec: 0.05 %%) | %.3f %% |", t10);
        Settings d = s; d.driveDb = 18; const double td = thd (d, 0.5);
        CHECK (td > 1.0, "DRIVE +18 dB into the output stage: THD %.2f %%", td);
        line ("| DRIVE +18 dB, -6 dBFS | %.2f %% |", td);
        Settings in = s; in.inputDb = 12; const double ti = thd (in, 0.7);
        CHECK (ti > 3.0, "INPUT +12 dB (converter clips): THD %.1f %%", ti);
        line ("| INPUT +12 dB (converter clipping) | %.1f %% |", ti);
        // SSM: level dependent
        Settings ss = base(); ss.channel = 0; ss.floorHz = 2000; ss.sweep = 0.25f;
        ss.ssmDb = 0; const double s0 = thd (ss, 0.5, 200.0);
        ss.ssmDb = 15; const double s15 = thd (ss, 0.5, 200.0);
        CHECK (s15 > 3 * s0, "SSM2044: THD %.3f %% at SSM 0 dB, %.2f %% at SSM +15 dB (200 Hz)", s0, s15);
        line ("| Out 1-2, SSM CHARACTER 0 dB, 200 Hz | %.3f %% |\n| Out 1-2, SSM CHARACTER +15 dB, 200 Hz | %.2f %% |", s0, s15);
        // frequency dependence of SSM saturation: same level, higher tone sits above the cutoff -> less saturation
        ss.ssmDb = 15; const double s15hi = thd (ss, 0.5, 1500.0);
        line ("| Out 1-2, SSM +15 dB, 1.5 kHz (near the cutoff) | %.2f %% |", s15hi);
        // ANALOG 0 = linear analog stages
        Settings z = d; z.analog = 0; const double tz = thd (z, 0.5);
        CHECK (tz < 0.1, "ANALOG 0 %%: output stage linear even with DRIVE (THD %.3f %%)", tz);
        line ("| DRIVE +18 dB with ANALOG 0 %% | %.3f %% |", tz);
    }

    std::printf ("[9] bypass tests: each half of the machine keeps its own character\n");
    line ("\n## 9. Bypass checks\n");
    {
        Settings s = base(); s.analogOn = false; s.recon = RC_RAW;
        auto in = sine (18000.0, 0.5, 0.5); run (s, in);
        const double al = goertzel (L, T0, L.size(), 8040.0);
        CHECK (db (al / 0.5) > -20.0, "analog stages off: digital aliasing remains (18 kHz -> 8.04 kHz at %.1f dB)", db (al / 0.5));
        Settings d = base(); d.digitalOn = false; d.driveDb = 18;
        run (d, in); const double al2 = goertzel (L, T0, L.size(), 8040.0);
        CHECK (db (al2 / 0.5) < -80.0, "digital stages off: no sampling alias (%.1f dB)", db (al2 / 0.5));
        auto in1 = sine (1000.0, 0.5, 0.5); run (d, in1);
        double hs = 0; for (int k = 2; k <= 5; ++k) { const double a = goertzel (L, T0, L.size(), 1000.0 * k); hs += a * a; }
        const double thd = 100 * std::sqrt (hs) / goertzel (L, T0, L.size(), 1000.0);
        CHECK (thd > 1.0, "digital stages off: analog colouration remains (DRIVE +18: THD %.2f %%)", thd);
        line ("- Analog stages off: the 18 kHz -> 8.04 kHz sampling alias stays at **%.1f dB**.\n- Digital stages off: no sampling alias "
              "(**%.1f dB**), but the analog stages still colour the signal (THD **%.2f %%** with DRIVE +18 dB).", db (al / 0.5), db (al2 / 0.5), thd);
    }

    std::printf ("[10] noise floor\n");
    line ("\n## 10. Noise (idle, nothing playing)\n\n| setting | unweighted | A-weighted |\n|---|---|---|");
    {
        std::vector<float> silence ((size_t) FS * 2, 0.f);
        Settings s; s.channel = 3; s.analyzer = false;
        run (s, silence);
        const double u = db (rms (L, T0, L.size()));
        auto aw = aweight (L); const double a = db (rms (aw, T0 * 2, aw.size()));
        CHECK (a < -86 && a > -96, "default noise: %.1f dB unweighted, %.1f dBA (published S/N 90 dBA)", u, a);
        line ("| defaults (Out 7-8) | %.1f dB | %.1f dBA (published S/N: 90 dBA) |", u, a);
        Settings q = base(); q.hissDb = -60; run (q, silence);
        CHECK (rms (L, T0, L.size()) < 1e-6, "all noise sources off: silence (%.1f dB)", db (rms (L, T0, L.size())));
        Settings h = s; h.humDb = 12; run (h, silence);
        const double hum = goertzel (L, T0, L.size(), 60.0);
        CHECK (db (hum) > -80, "HUM +12: 60 Hz component at %.1f dBFS", db (hum));
        line ("| HUM +12 dB | 60 Hz at %.1f dBFS | |", db (hum));
        // noise rides the stages: a loud input lifts the converter's quantisation noise with it, silence doesn't
        auto in = sine (1000.0, 0.003, 1.0); run (s, in);                                     // -50 dBFS: few codes
        const double lowNoise = rms (L, T0, L.size());
        line ("| -50 dBFS 1 kHz tone (quantisation now audible) | total %.1f dB | |", db (lowNoise));
    }

    std::printf ("[11] component variation\n");
    {
        auto in = sine (5000.0, 0.5, 0.3);
        Settings s = base(); s.channel = 2;
        run (s, in); double d0 = 0; for (size_t i = T0; i < L.size(); ++i) d0 = std::fmax (d0, std::fabs (L[i] - R[i]));
        s.variation = 1.f; run (s, in); double d1 = 0; for (size_t i = T0; i < L.size(); ++i) d1 = std::fmax (d1, std::fabs (L[i] - R[i]));
        CHECK (d0 == 0.0 && d1 > 1e-4 && d1 < 0.1, "variation 0 %%: L = R exactly; 100 %%: small L/R difference (%.4f)", d1);
        line ("\n## 11. Component variation\nAt 0 %% left and right are identical. At 100 %% the two voices differ slightly (peak difference %.4f on a -6 dBFS 5 kHz tone through Out 5-6).", d1);
    }

    std::printf ("[12] transient (kick) punch\n");
    {
        std::vector<float> kick ((size_t) FS);
        double ph = 0;
        for (size_t i = 0; i < kick.size(); ++i) { const double t = (double) i / FS; ph += 2 * kPi * (50 + 120 * std::exp (-t * 30)) / FS; kick[i] = (float) (0.9 * std::sin (ph) * std::exp (-t * 8)); }
        Settings s; s.channel = 3; s.analyzer = false;
        run (s, kick);
        auto crest = [] (const std::vector<float>& v, size_t a, size_t b) { double pk = 0; for (size_t i = a; i < b; ++i) pk = std::fmax (pk, std::fabs (v[i])); return db (pk / rms (v, a, b)); };
        const double ci = crest (kick, 0, 8000), co = crest (L, kLatency, 8000 + kLatency);
        CHECK (std::fabs (ci - co) < 1.5, "kick crest factor in %.1f dB, out %.1f dB (punch kept, no compression)", ci, co);
        line ("\n## 12. Transients\nA synthetic kick's crest factor: **%.1f dB** in, **%.1f dB** out (defaults, Out 7-8): the path does not compress.", ci, co);
    }

    std::printf ("[13] latency and CPU\n");
    {
        std::vector<float> imp ((size_t) FS / 2, 0.f); imp[1000] = 0.5f;
        Settings s = base(); s.mix = 0; run (s, imp);
        int at = -1; for (size_t i = 0; i < L.size(); ++i) if (L[i] != 0.f) { at = (int) i; break; }
        CHECK (at == 1000 + kLatency, "dry path delayed by the reported latency (%d samples)", at - 1000);
        line ("\n## 13. Latency and CPU\nLatency: **%d samples** (reported to the host; the dry path is delayed to match).\n\n| quality | CPU (x86, one core) |\n|---|---|", kLatency);
        auto in = sine (440.0, 0.5, 10.0);
        for (int q : { Q_ECO, Q_NORMAL, Q_ACCURATE, Q_REFERENCE })
        {
            Settings c; c.quality = q; c.channel = 0; c.analyzer = true; c.mode = PM_PITCH; c.pitch = -5;
            M->prepare (FS);
            std::vector<float> o1 (in.size()), o2 (in.size());
            const auto t0 = std::clock();
            for (size_t p = 0; p < in.size(); p += 256) M->process (&in[p], &in[p], &o1[p], &o2[p], (int) std::min<size_t> (256, in.size() - p), c);
            const double sec = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
            static const char* nm[] { "ECO", "NORMAL", "ACCURATE", "REFERENCE" };
            std::printf ("    %-9s %.2f %%\n", nm[q], sec / 10.0 * 100);
            line ("| %s | %.2f %% |", nm[q], sec / 10.0 * 100);
        }
    }

    FILE* f = std::fopen ("docs/MEASUREMENTS.md", "w");
    if (f)
    {
        std::fprintf (f, "# SP1200 2.0: measurements of the model\n\nGenerated by `make stagetest` (test/stage_test.cpp) at a 44.1 kHz host rate, "
                         "default settings unless stated, noise sources off except in section 10. Hardware reference figures: owner's manual and "
                         "spec sheet (26.04 kHz, 12-bit, S/N 90 dBA, THD 0.05 %%), owners' filter measurements (Out 3-4 ~7.5 kHz, Out 5-6 ~10 kHz). "
                         "No measurements of a physical unit were available to this build; the comparisons are with those published figures.\n\n%s\n", report.c_str());
        std::fclose (f);
    }
    std::printf ("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
