// G-Glue shared-core tests: DSP engine, parameters, presets, preset library, state. No plugin framework.
// Build: see CMakeLists.txt (target gglue_dsp_tests) or Tests/Makefile for the ARM / QEMU run.
#include "../DSP/GGlueEngine.h"
#include "../Presets/GGluePresets.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace gglue;
namespace fs = std::filesystem;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; const bool ok_ = (cond); std::printf ("  %s ", ok_ ? "ok  " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

static const double kPi = 3.14159265358979323846;
static double db (double g) { return 20.0 * std::log10 (std::max (g, 1e-12)); }

struct Buf
{
    std::vector<float> l, r;
    explicit Buf (size_t n = 0) : l (n), r (n) {}
    size_t size() const { return l.size(); }
};

static Buf sine (double fs, double hz, double seconds, double amp, double phaseR = 0.0)
{
    Buf b ((size_t) (fs * seconds));
    for (size_t i = 0; i < b.size(); ++i)
    {
        b.l[i] = (float) (amp * std::sin (2 * kPi * hz * i / fs));
        b.r[i] = (float) (amp * std::sin (2 * kPi * hz * i / fs + phaseR));
    }
    return b;
}

// process in blocks; optional per-block hook (automation) and per-block GR capture
template <typename Hook>
static std::vector<float> run (Engine& e, Buf& b, int block, Hook hook, bool stereo = true)
{
    std::vector<float> gr;
    for (size_t pos = 0; pos < b.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, b.size() - pos);
        hook ((int) (pos / (size_t) block));
        float* ch[2] = { b.l.data() + pos, b.r.data() + pos };
        e.process (ch, stereo ? 2 : 1, n);
        gr.push_back (e.gainReductionDb.load());
    }
    return gr;
}
static std::vector<float> run (Engine& e, Buf& b, int block = 64, bool stereo = true) { return run (e, b, block, [] (int) {}, stereo); }

static double rms (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (b - a));
}
static double mean (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += x[i];
    return s / (double) (b - a);
}
// amplitude of one frequency (Goertzel, Hann window)
static double toneAmp (const std::vector<float>& x, size_t a, size_t b, double fs, double hz)
{
    const size_t n = b - a;
    const double w = 2 * kPi * hz / fs, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) (n - 1));
        wsum += win;
        const double s = x[a + i] * win + c * s1 - s2; s2 = s1; s1 = s;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}
// largest absolute second difference: spikes here are audible clicks
static double maxSecondDiff (const std::vector<float>& x, size_t a, size_t b)
{
    double m = 0;
    for (size_t i = a + 2; i < b; ++i) m = std::max (m, (double) std::fabs (x[i] - 2 * x[i - 1] + x[i - 2]));
    return m;
}

static Engine& fresh (Engine& e, double fs, ParamValues v = defaultValues(), EngineConfig c = {})
{
    e.configure (c);
    e.setParameters (v);
    e.prepare (fs, 2);
    return e;
}

// steady-state gain reduction for a sine at the given level (dBFS peak)
static double staticGr (double fs, const ParamValues& v, double levelDb, Detector det = Detector::Blend)
{
    Engine e; EngineConfig c; c.detector = det;
    fresh (e, fs, v, c);
    Buf b = sine (fs, 1000, 1.5, std::pow (10.0, levelDb / 20.0));
    const auto gr = run (e, b, 64);
    double s = 0; int n = 0;
    for (size_t i = gr.size() / 2; i < gr.size(); ++i) { s += gr[i]; ++n; }
    return s / n;
}

static double timeTo (const std::vector<float>& gr, double fs, int block, size_t fromBlock, double level, bool falling)
{
    for (size_t i = fromBlock; i < gr.size(); ++i)
        if (falling ? gr[i] <= level : gr[i] >= level) return (double) (i - fromBlock) * block / fs;
    return 1e9;
}

int main (int argc, char** argv)
{
    const fs::path work = argc > 1 ? fs::path (argv[1]) : fs::temp_directory_path() / "gglue-tests";
    std::error_code ec; fs::remove_all (work, ec); fs::create_directories (work, ec);
    const double fs48 = 48000.0;

    std::printf ("Initialization\n");
    {
        Engine e;
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
            for (int os : { 1, 2, 4 })
            {
                EngineConfig c; c.oversampling = os; fresh (e, fs, defaultValues(), c);
                Buf b = sine (fs, 997, 0.2, 0.25);
                run (e, b);
                bool finite = true; for (float x : b.l) finite = finite && std::isfinite (x);
                if (! finite || e.getLatencySamples() != 0) { CHECK (false, "init %g Hz os %d", fs, os); goto initDone; }
            }
        CHECK (true, "prepares and runs at 44.1 / 48 / 88.2 / 96 kHz with 1x / 2x / 4x oversampling, latency 0");
    initDone:;
        const auto d = defaultValues();
        CHECK (d[kThreshold] == -10.f && d[kRatio] == 1.f && d[kRelease] == (float) kReleaseAuto && d[kMix] == 100.f,
               "defaults: threshold -10 dB, 4:1, AUTO release, mix 100 %%");
    }

    std::printf ("Silence\n");
    {
        Engine e; ParamValues v = defaultValues(); v[kAnalog] = 1; v[kMakeup] = 24; fresh (e, fs48, v);
        Buf b (48000);
        run (e, b);
        double m = 0; for (size_t i = 0; i < b.size(); ++i) m = std::max (m, (double) std::max (std::fabs (b.l[i]), std::fabs (b.r[i])));
        CHECK (m == 0.0 && e.gainReductionDb.load() == 0.f, "silence in -> exact silence out (analog on, +24 dB makeup), GR 0");
    }

    std::printf ("Threshold and ratio (static curve, 1 kHz sine)\n");
    {
        ParamValues v = defaultValues(); v[kThreshold] = -20; v[kAttack] = 2; v[kRelease] = 2;
        v[kRatio] = 1; const double below = staticGr (fs48, v, -30);
        CHECK (below < 0.05, "10 dB below threshold: %.3f dB GR", below);
        for (int ri = 0; ri < 3; ++ri)
        {
            v[kRatio] = (float) ri;
            const double gr = staticGr (fs48, v, 0.0, Detector::Rms);
            const double want = 20.0 * (1.0 - 1.0 / kRatios[ri]);
            CHECK (std::fabs (gr - want) < 0.6, "ratio %s, 20 dB over: GR %.2f dB (expected %.2f)", formatValue (kRatio, (float) ri).c_str(), gr, want);
        }
        v[kRatio] = 1;
        const double atT = staticGr (fs48, v, -20.0, Detector::Rms);
        CHECK (atT > 0.2 && atT < 1.5, "soft knee: at the threshold %.2f dB GR (hard knee would be 0)", atT);
        v[kThreshold] = 0; const double hi = staticGr (fs48, v, -6, Detector::Rms);
        v[kThreshold] = -20; const double lo = staticGr (fs48, v, -6, Detector::Rms);
        CHECK (lo > hi + 8, "lower threshold compresses more: %.2f dB vs %.2f dB", lo, hi);
    }

    std::printf ("Mono and stereo\n");
    {
        Engine e; ParamValues v = defaultValues(); v[kThreshold] = -20; fresh (e, fs48, v);
        e.prepare (fs48, 1);
        Buf b = sine (fs48, 1000, 1.0, 1.0);
        run (e, b, 64, false);
        const double out = rms (b.l, 24000, 48000) * std::sqrt (2.0);
        CHECK (db (out) < -4.0 && std::isfinite (out), "mono: 0 dBFS sine compressed to %.1f dBFS", db (out));
        // identical L/R in -> identical L/R out (image preserved)
        fresh (e, fs48, v);
        b = sine (fs48, 220, 1.0, 0.8);
        run (e, b);
        double d = 0; for (size_t i = 0; i < b.size(); ++i) d = std::max (d, (double) std::fabs (b.l[i] - b.r[i]));
        CHECK (d == 0.0, "stereo: identical channels stay identical (max difference %.1g)", d);
        // linked: a loud left channel ducks the quiet right channel by the same amount
        fresh (e, fs48, v);
        Buf s = sine (fs48, 1000, 1.0, 1.0);
        Buf ref = s;
        for (size_t i = 0; i < s.size(); ++i) { s.r[i] *= 0.05f; ref.r[i] *= 0.05f; }
        run (e, s);
        const double gL = rms (s.l, 24000, 48000) / rms (ref.l, 24000, 48000), gR = rms (s.r, 24000, 48000) / rms (ref.r, 24000, 48000);
        CHECK (std::fabs (db (gL) - db (gR)) < 0.05, "stereo link: left %.2f dB, right %.2f dB (same gain, image kept)", db (gL), db (gR));
    }

    std::printf ("Attack and release (peak detector, 10:1, step input)\n");
    {
        const int block = 16;
        for (int ai : { 1, 4, 5 })
        {
            ParamValues v = defaultValues(); v[kThreshold] = -30; v[kRatio] = 2; v[kAttack] = (float) ai; v[kRelease] = 3;
            Engine e; EngineConfig c; c.detector = Detector::Peak; fresh (e, fs48, v, c);
            Buf b (48000);
            for (size_t i = 0; i < b.size(); ++i) b.l[i] = b.r[i] = (float) ((i >= 4800 ? 1.0 : 0.001) * std::sin (2 * kPi * 3000 * i / fs48));
            const auto gr = run (e, b, block);
            const double fin = gr.back();
            const double t = timeTo (gr, fs48, block, 4800 / block, 0.632 * fin, false);
            const double want = kAttackMs[ai] * 0.001;
            CHECK (t <= want * 1.6 + 0.0006 && t >= want * 0.5 - 0.0004, "attack %s: 63 %% of %.1f dB GR after %.2f ms", formatValue (kAttack, (float) ai).c_str(), fin, t * 1000);
        }
        for (int ri : { 0, 2, 3 })
        {
            ParamValues v = defaultValues(); v[kThreshold] = -30; v[kRatio] = 2; v[kAttack] = 0; v[kRelease] = (float) ri;
            Engine e; EngineConfig c; c.detector = Detector::Peak; fresh (e, fs48, v, c);
            Buf b (48000 * 4);
            for (size_t i = 0; i < b.size(); ++i) b.l[i] = b.r[i] = (float) ((i < 48000 ? 1.0 : 0.0) * std::sin (2 * kPi * 3000 * i / fs48));
            const auto gr = run (e, b, block);
            const double start = gr[48000 / block - 1];
            const double t = timeTo (gr, fs48, block, 48000 / block, 0.368 * start, true);
            const double want = kReleaseSec[ri];
            CHECK (std::fabs (t - want) < want * 0.15 + 0.002, "release %s: back to 37 %% after %.3f s", formatValue (kRelease, (float) ri).c_str(), t);
        }
    }

    std::printf ("Auto release (programme dependent)\n");
    {
        const int block = 32;
        auto recover = [&] (double burstSec) {
            ParamValues v = defaultValues(); v[kThreshold] = -30; v[kRatio] = 2; v[kAttack] = 1; v[kRelease] = (float) kReleaseAuto;
            Engine e; EngineConfig c; c.detector = Detector::Peak; fresh (e, fs48, v, c);
            const size_t on = (size_t) (burstSec * fs48);
            Buf b (on + 48000 * 5);
            for (size_t i = 0; i < b.size(); ++i) b.l[i] = b.r[i] = (float) ((i < on ? 1.0 : 0.0) * std::sin (2 * kPi * 2000 * i / fs48));
            const auto gr = run (e, b, block);
            return timeTo (gr, fs48, block, on / block, 1.0, true);
        };
        const double shortT = recover (0.02), longT = recover (2.0);
        CHECK (shortT < 0.35 && longT > 1.5 * shortT && longT > 1.0, "after a 20 ms hit GR is under 1 dB in %.2f s, after 2 s of compression in %.2f s", shortT, longT);
    }

    std::printf ("Sidechain high-pass\n");
    {
        double grOff = 0, gr200 = 0;
        for (int sc : { 0, 6 })
        {
            ParamValues v = defaultValues(); v[kThreshold] = -20; v[kScFilter] = (float) sc;
            Engine e; fresh (e, fs48, v);
            Buf b = sine (fs48, 40, 1.5, 0.7);
            const auto gr = run (e, b);
            (sc ? gr200 : grOff) = gr.back();
        }
        CHECK (grOff > 8 && gr200 < grOff - 6, "40 Hz bass: %.1f dB GR with SC OFF, %.1f dB with SC 200 Hz", grOff, gr200);
        ParamValues v = defaultValues(); v[kScFilter] = 3; Engine e; fresh (e, fs48, v);
        Buf b = sine (fs48, 1000, 0.5, 0.1); Buf ref = b;
        run (e, b);
        CHECK (std::fabs (rms (b.l, 12000, 24000) - rms (ref.l, 12000, 24000)) < 1e-3, "the sidechain filter never filters the audio itself");
    }

    std::printf ("Parallel mix\n");
    {
        ParamValues v = defaultValues(); v[kThreshold] = -30; v[kRatio] = 2; v[kMakeup] = 0;
        double lv[3];
        int k = 0;
        for (float mix : { 0.f, 50.f, 100.f })
        {
            v[kMix] = mix; Engine e; fresh (e, fs48, v);
            Buf b = sine (fs48, 1000, 1.0, 0.5);
            run (e, b);
            lv[k++] = db (rms (b.l, 24000, 48000) * std::sqrt (2.0) / 0.5);
        }
        CHECK (std::fabs (lv[0]) < 0.05 && lv[2] < -10 && lv[1] > lv[2] + 3 && lv[1] < lv[0] - 2,
               "mix 0 / 50 / 100 %%: %.2f / %.2f / %.2f dB", lv[0], lv[1], lv[2]);
        v[kMix] = 0; EngineConfig c; c.oversampling = 1; Engine e; fresh (e, fs48, v, c);
        Buf b = sine (fs48, 1000, 0.5, 0.5); Buf ref = b;
        run (e, b);
        double d = 0; for (size_t i = 0; i < b.size(); ++i) d = std::max (d, (double) std::fabs (b.l[i] - ref.l[i]));
        CHECK (d == 0.0, "mix 0 %% without oversampling is bit-exact dry");
    }

    std::printf ("Analog mode\n");
    {
        double h2[2], h3[2], dc[2], fund[2], peakIn = 0.5;
        for (int a = 0; a < 2; ++a)
        {
            ParamValues v = defaultValues(); v[kThreshold] = 10; v[kAnalog] = (float) a;   // no compression: colour only
            Engine e; fresh (e, fs48, v);
            Buf b = sine (fs48, 1000, 1.0, peakIn);
            run (e, b);
            fund[a] = toneAmp (b.l, 9600, 48000, fs48, 1000);
            h2[a] = toneAmp (b.l, 9600, 48000, fs48, 2000) / fund[a];
            h3[a] = toneAmp (b.l, 9600, 48000, fs48, 3000) / fund[a];
            dc[a] = mean (b.l, 9600, 48000);
        }
        CHECK (db (h2[0]) < -90 && db (h3[0]) < -90, "analog OFF: 2nd %.0f dB, 3rd %.0f dB (clean)", db (h2[0]), db (h3[0]));
        CHECK (db (h2[1]) > -50 && db (h2[1]) < -25 && db (h3[1]) < db (h2[1]), "analog ON at -6 dBFS: 2nd harmonic %.1f dB, 3rd %.1f dB (subtle, even-order)", db (h2[1]), db (h3[1]));
        CHECK (std::fabs (db (fund[1] / fund[0])) < 0.3 && std::fabs (dc[1]) < 1e-4, "analog ON keeps the level (%.2f dB) and adds no DC (%.1g)", db (fund[1] / fund[0]), dc[1]);
        // transients: a fast attack transient keeps its peak with analog on
        ParamValues v = defaultValues(); v[kThreshold] = 10; v[kAnalog] = 1; Engine e; fresh (e, fs48, v);
        // a kick-like hit: 1 ms rise, fast decay (an instant 0 -> full-scale step would make any band-limited path ring)
        Buf b (9600);
        for (size_t i = 0; i < 2400; ++i)
        {
            const double rise = i < 48 ? 0.5 - 0.5 * std::cos (kPi * (double) i / 48.0) : 1.0;
            b.l[i] = b.r[i] = (float) (0.7 * rise * std::exp (-(double) i / 600.0) * std::sin (2 * kPi * 150 * i / fs48 + 1.0));
        }
        Buf ref = b; run (e, b);
        double pi = 0, po = 0; for (size_t i = 0; i < 2400; ++i) { pi = std::max (pi, (double) std::fabs (ref.l[i])); po = std::max (po, (double) std::fabs (b.l[i])); }
        CHECK (std::fabs (db (po / pi)) < 1.0, "analog ON keeps transient peaks: %.2f dB", db (po / pi));
    }

    std::printf ("Bypass\n");
    {
        ParamValues v = defaultValues(); v[kThreshold] = -30; v[kMakeup] = 12; v[kBypass] = 1; v[kAnalog] = 1;
        Engine e; fresh (e, fs48, v);
        Buf b = sine (fs48, 440, 0.5, 0.9); Buf ref = b;
        run (e, b);
        double d = 0; for (size_t i = 0; i < b.size(); ++i) d = std::max (d, (double) std::fabs (b.l[i] - ref.l[i]));
        CHECK (d == 0.0, "bypass ON is bit-exact");
        // switching bypass mid-stream: no click
        v[kBypass] = 0; fresh (e, fs48, v);
        Buf s = sine (fs48, 100, 2.0, 0.5);
        run (e, s, 128, [&] (int blk) { if (blk == 200) e.setParameter (kBypass, 1); if (blk == 500) e.setParameter (kBypass, 0); });
        const double base = maxSecondDiff (s.l, 1000, 25000), around = maxSecondDiff (s.l, 25000, 72000);
        CHECK (around < base * 2.0 + 1e-3, "bypass on/off while playing: largest step %.4f (steady %.4f), no click", around, base);
        // after the 15 ms crossfade the output is the input, bit for bit
        v[kBypass] = 0; fresh (e, fs48, v);
        Buf w = sine (fs48, 440, 0.5, 0.5); Buf wref = w;
        run (e, w, 64, [&] (int blk) { if (blk == 10) e.setParameter (kBypass, 1); });
        const size_t exactFrom = 640 + 720 + 64;                                     // switch + 15 ms + one block
        double dd = 0; for (size_t i = exactFrom; i < w.size(); ++i) dd = std::max (dd, (double) std::fabs (w.l[i] - wref.l[i]));
        CHECK (dd == 0.0, "15 ms after switching bypass on, the output is bit-exact");
    }

    std::printf ("Automation (smoothing, no zipper noise)\n");
    {
        ParamValues v = defaultValues(); v[kThreshold] = -20;
        Engine e; fresh (e, fs48, v);
        Buf ref = sine (fs48, 80, 3.0, 0.5);
        Buf s = ref;
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> u (0.f, 1.f);
        run (e, s, 64, [&] (int) {
            e.setParameter (kThreshold, -30 + 40 * u (rng)); e.setParameter (kMakeup, 12 * u (rng));
            e.setParameter (kOutput, -12 + 12 * u (rng)); e.setParameter (kMix, 100 * u (rng)); e.setParameter (kInput, -6 + 12 * u (rng));
        });
        bool finite = true; for (float x : s.l) finite = finite && std::isfinite (x);
        // a hard gain jump of 1 dB on this signal would give a second difference of about 0.06
        const double worst = maxSecondDiff (s.l, 4800, s.size());
        CHECK (finite && worst < 0.01, "every continuous parameter randomised every 64 samples: largest step %.4f (a 1 dB jump = 0.06)", worst);
        // discrete parameters switched while playing (attack kept at 10 ms here: 0.1 ms attack on 80 Hz distorts by design)
        v[kAttack] = 4; fresh (e, fs48, v);
        Buf d = sine (fs48, 80, 3.0, 0.7);
        run (e, d, 64, [&] (int blk) { if (blk % 100 == 50) { e.setParameter (kRatio, (float) (blk / 100 % 3)); e.setParameter (kRelease, (float) (blk / 100 % 5));
                                                                 e.setParameter (kScFilter, (float) (blk / 100 % 7)); e.setParameter (kAnalog, (float) (blk / 100 % 2)); } });
        const double w2 = maxSecondDiff (d.l, 4800, d.size());
        CHECK (w2 < 0.01, "ratio / release / SC filter / analog switched while playing: largest step %.4f", w2);
        // attack switched between 3, 10 and 30 ms
        v[kAttack] = 3; fresh (e, fs48, v);
        Buf a = sine (fs48, 80, 3.0, 0.7);
        run (e, a, 64, [&] (int blk) { if (blk % 100 == 50) e.setParameter (kAttack, (float) (3 + blk / 100 % 3)); });
        const double w3 = maxSecondDiff (a.l, 4800, a.size());
        CHECK (w3 < 0.01, "attack 3 / 10 / 30 ms switched while playing: largest step %.4f", w3);
    }

    std::printf ("Sample rates\n");
    {
        ParamValues v = defaultValues(); v[kThreshold] = -20; v[kRelease] = 2;
        double lo = 1e9, hi = -1e9;
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const double g = staticGr (fs, v, -6);
            lo = std::min (lo, g); hi = std::max (hi, g);
        }
        CHECK (hi - lo < 0.3, "same gain reduction at 44.1 / 48 / 88.2 / 96 kHz: %.2f .. %.2f dB", lo, hi);
        // switching the rate on a running engine
        Engine e; fresh (e, 44100, v);
        Buf b = sine (44100, 1000, 0.3, 0.9); run (e, b);
        e.prepare (96000, 2);
        Buf c = sine (96000, 1000, 0.6, 0.9); run (e, c);
        bool finite = true; for (float x : c.l) finite = finite && std::isfinite (x);
        CHECK (finite && e.getSampleRate() == 96000, "re-prepare 44.1 -> 96 kHz while running");
    }

    std::printf ("Oversampling filters\n");
    {
        double coef[kMaxHalfbandCoefs]; designHalfband (coef, 12, 0.04);
        Upsampler2x up; up.setup (coef, 12);
        Downsampler2x dn; dn.setup (coef, 12);
        // image rejection: 5 kHz at 48 kHz -> 96 kHz; the image sits at 43 kHz
        std::vector<float> o; o.reserve (96000);
        for (int i = 0; i < 48000; ++i) { float a, b; up.process ((float) std::sin (2 * kPi * 5000 * i / 48000.0), a, b); o.push_back (a); o.push_back (b); }
        const double img = toneAmp (o, 20000, 96000, 96000, 43000) / toneAmp (o, 20000, 96000, 96000, 5000);
        CHECK (db (img) < -90, "2x up-sampler image rejection: %.0f dB", db (img));
        // pass band: up + down round trip at 1 / 10 / 18 / 20 kHz (44.1 kHz)
        double worst = 0;
        for (double hz : { 1000.0, 10000.0, 18000.0, 20000.0 })
        {
            Upsampler2x u; u.setup (coef, 12); Downsampler2x d; d.setup (coef, 12);
            std::vector<float> y;
            for (int i = 0; i < 44100; ++i) { float a, b; u.process ((float) std::sin (2 * kPi * hz * i / 44100.0), a, b); y.push_back (d.process (a, b)); }
            worst = std::max (worst, std::fabs (db (toneAmp (y, 4410, 44100, 44100, hz))));
        }
        CHECK (worst < 0.1, "2x round trip flat to 20 kHz at 44.1 kHz: worst %.3f dB", worst);
        // aliasing of the analog stage: 15 kHz at 44.1 kHz driven hard; 3rd harmonic 45 kHz aliases to 900 Hz
        double alias[2];
        for (int os : { 1, 2 })
        {
            ParamValues v = defaultValues(); v[kThreshold] = 10; v[kAnalog] = 1; v[kInput] = 6;
            EngineConfig c; c.oversampling = os; Engine e; fresh (e, 44100, v, c);
            Buf b = sine (44100, 15000, 1.0, 0.45); run (e, b);
            alias[os - 1] = toneAmp (b.l, 4410, 44100, 44100, 900) / toneAmp (b.l, 4410, 44100, 44100, 15000);
        }
        CHECK (alias[1] < alias[0] * 0.1 || db (alias[1]) < -100, "aliasing at 900 Hz from a 15 kHz tone: %.0f dB without, %.0f dB with 2x oversampling", db (alias[0]), db (alias[1]));
    }

    std::printf ("Extreme settings, NaN, infinity, DC, denormals\n");
    {
        std::mt19937 rng (3);
        bool ok = true; double maxOut = 0;
        for (int t = 0; t < 40 && ok; ++t)
        {
            ParamValues v;
            for (int i = 0; i < kNumParams; ++i) { const auto& s = spec (i); v[(size_t) i] = clampPlain (i, (rng() & 1) ? s.max : s.min); }
            v[kBypass] = 0;                                                   // bypass passes the spikes on untouched, by design
            EngineConfig c; c.oversampling = 1 << (t % 3); Engine e; fresh (e, fs48, v, c);
            Buf b = sine (fs48, 30 + t * 500, 0.2, 1.0);
            for (size_t i = 0; i < b.size(); i += 97) b.l[i] = (rng() & 1) ? 8.f : -8.f;           // > 0 dBFS spikes
            run (e, b);
            for (size_t i = 0; i < b.size(); ++i) { ok = ok && std::isfinite (b.l[i]) && std::isfinite (b.r[i]); maxOut = std::max (maxOut, (double) std::fabs (b.l[i])); }
        }
        CHECK (ok && maxOut <= 1.0, "40 random min/max combinations with +18 dBFS spikes: finite, peak %.4f (<= 1.0, output protection)", maxOut);

        Engine e; ParamValues v = defaultValues(); v[kAnalog] = 1; fresh (e, fs48, v);
        Buf b = sine (fs48, 1000, 0.5, 0.5);
        b.l[100] = NAN; b.r[200] = INFINITY; b.l[300] = -INFINITY;
        run (e, b);
        bool finite = true; for (size_t i = 0; i < b.size(); ++i) finite = finite && std::isfinite (b.l[i]) && std::isfinite (b.r[i]);
        Buf after = sine (fs48, 1000, 0.5, 0.5); run (e, after);
        CHECK (finite && rms (after.l, 4800, 24000) > 0.1, "NaN / +inf / -inf in the input: output stays finite and keeps working");

        fresh (e, fs48, v);
        Buf d = sine (fs48, 50, 2.0, 0.6);
        run (e, d);
        CHECK (std::fabs (mean (d.l, 9600, d.size())) < 2e-4, "zero-mean input (analog on, compressing): output DC %.1g", mean (d.l, 9600, d.size()));
        Buf dc (48000); for (size_t i = 0; i < dc.size(); ++i) dc.l[i] = dc.r[i] = (float) (0.2 + 0.1 * std::sin (2 * kPi * 500 * i / fs48));
        fresh (e, fs48, defaultValues()); run (e, dc);
        CHECK (std::isfinite (dc.l.back()) && std::fabs (dc.l.back()) < 1.0, "DC offset in the input: stable");

        // denormals: a decaying tail down to 1e-30 then silence
        fresh (e, fs48, v);
        Buf t (48000 * 2);
        for (size_t i = 0; i < 48000; ++i) t.l[i] = t.r[i] = (float) (0.5 * std::exp (-(double) i / 700.0) * std::sin (2 * kPi * 300 * i / fs48));
        const auto t0 = std::chrono::steady_clock::now();
        run (e, t);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        int sub = 0; for (size_t i = 0; i < t.size(); ++i) sub += std::fpclassify (t.l[i]) == FP_SUBNORMAL;
        CHECK (sub == 0, "decay into silence: no denormal outputs, %.1f ms CPU for 2 s", sec * 1000);
    }

    std::printf ("Clicks and pops (analog on/off, preset changes while playing)\n");
    {
        Engine e; ParamValues v = defaultValues(); v[kThreshold] = -18; fresh (e, fs48, v);
        Buf s = sine (fs48, 100, 4.0, 0.5);
        const auto& fp = factoryPresets();
        run (e, s, 256, [&] (int blk) { if (blk > 20 && blk % 60 == 0) e.setParameters (fp[(size_t) (blk / 60) % fp.size()].values); });
        const double steady = maxSecondDiff (s.l, 0, 5000), worst = maxSecondDiff (s.l, 5000, s.size());
        // a 100 Hz sine at 0.5 has a second difference of 0.0002; a click would be 100x larger
        CHECK (worst < 0.006, "12 preset changes while playing: largest step %.4f (steady %.4f)", worst, steady);
    }

    std::printf ("Parameters\n");
    {
        std::set<std::string> ids;
        for (int i = 0; i < kNumParams; ++i) ids.insert (spec (i).id);
        CHECK ((int) ids.size() == kNumParams && kNumParams == 11, "11 parameters with unique stable IDs");
        const char* expect[] { "threshold", "makeup", "attack", "release", "ratio", "scfilter", "mix", "input", "output", "analog", "bypass" };
        bool order = true; for (int i = 0; i < kNumParams; ++i) order = order && std::string (spec (i).id) == expect[i];
        CHECK (order, "ID order fixed (Q-Link 1-4 = threshold, makeup, attack, release)");
        bool rt = true; size_t longest = 0;
        for (int i = 0; i < kNumParams; ++i)
            for (int k = 0; k <= 20; ++k)
            {
                const float p = fromNormalized (i, k / 20.f);
                float back; const std::string t = formatValue (i, p);
                longest = std::max (longest, t.size());
                rt = rt && parseValue (i, t, back) && std::fabs (back - p) < 0.051f && std::fabs (fromNormalized (i, toNormalized (i, p)) - p) < 1e-4f;
            }
        CHECK (rt && longest <= 8, "display text round-trips for every parameter; longest text %zu chars (MPC display)", longest);
        CHECK (formatValue (kRelease, 4) == "AUTO" && formatValue (kRatio, 2) == "10:1" && formatValue (kScFilter, 0) == "OFF"
               && formatValue (kThreshold, -12.f) == "-12.0 dB" && formatValue (kMix, 75) == "75 %", "readable values: AUTO, 10:1, OFF, -12.0 dB, 75 %%");
    }

    std::printf ("Factory presets\n");
    {
        const auto& fp = factoryPresets();
        std::set<std::string> names; std::map<std::string, int> perCat; bool inRange = true;
        for (const auto& p : fp)
        {
            names.insert (p.name); perCat[p.category]++;
            for (int i = 0; i < kNumParams; ++i) inRange = inRange && clampPlain (i, p.values[(size_t) i]) == p.values[(size_t) i];
            inRange = inRange && p.values[kBypass] == 0.f;
        }
        int minCat = 1000; for (const auto& c : presetCategories()) if (c != "USER") minCat = std::min (minCat, perCat[c]);
        CHECK (fp.size() >= 60 && names.size() == fp.size(), "%zu factory presets, all names unique", fp.size());
        CHECK (perCat.size() == 12 && minCat >= 5, "12 categories, at least %d presets each", minCat);
        CHECK (inRange, "every preset value inside its parameter range, bypass off");
        // every preset produces sane audio on a drum-like signal
        bool sane = true; double worstOut = 0;
        for (const auto& p : fp)
        {
            Engine e; fresh (e, fs48, p.values);
            Buf b (24000);
            for (size_t i = 0; i < b.size(); ++i) { const double t = (double) (i % 12000) / fs48; b.l[i] = b.r[i] = (float) (0.8 * std::exp (-t * 30) * std::sin (2 * kPi * 60 * t) + 0.05 * std::sin (2 * kPi * 3000 * t)); }
            run (e, b);
            for (float x : b.l) { sane = sane && std::isfinite (x); worstOut = std::max (worstOut, (double) std::fabs (x)); }
        }
        CHECK (sane && worstOut <= 1.0, "all presets run on a drum loop: finite, peak %.3f", worstOut);
        Engine e; fresh (e, fs48, fp[3].values);
        bool same = true; for (int i = 0; i < kNumParams; ++i) same = same && e.getParameter (i) == fp[3].values[(size_t) i];
        CHECK (same, "loading a preset sets every engine parameter (%s)", fp[3].name.c_str());
    }

    std::printf ("Preset library: save, save as, rename, delete, import, export, favourites, search, next / previous\n");
    {
        const fs::path dir = work / "User Presets";
        PresetLibrary lib (dir);
        const size_t nf = factoryPresets().size();
        ParamValues v = defaultValues(); v[kThreshold] = -17.5f; v[kRatio] = 2; v[kAnalog] = 1; v[kBypass] = 1;
        std::string err;
        CHECK (lib.save ("My Bus", "USER", v, false, &err) && lib.presets().size() == nf + 1, "save: \"My Bus\" written to %s", dir.string().c_str());
        const int idx = lib.indexOf ("My Bus", false);
        CHECK (idx >= 0 && lib.presets()[(size_t) idx].values[kThreshold] == -17.5f && lib.presets()[(size_t) idx].values[kBypass] == 0.f,
               "saved values read back (bypass is never stored in a preset)");
        CHECK (! lib.save ("My Bus", "USER", v, false, &err), "save as an existing name is refused without overwrite (%s)", err.c_str());
        v[kThreshold] = -3; CHECK (lib.save ("My Bus", "USER", v, true, &err) && lib.presets()[(size_t) lib.indexOf ("My Bus", false)].values[kThreshold] == -3.f, "save (overwrite) updates it");
        CHECK (lib.save ("Bad/Name:*?", "VOCALS", v, false, &err) && lib.indexOf ("BadName", false) >= 0, "unsafe characters are removed from names");
        lib.setFavorite (lib.indexOf ("My Bus", false), true);
        lib.setFavorite (lib.indexOf ("Drum Punch", true), true);
        CHECK (lib.rename (lib.indexOf ("My Bus", false), "Master Bus", &err) && lib.indexOf ("Master Bus", false) >= 0 && lib.indexOf ("My Bus", false) < 0
               && lib.isFavorite (lib.indexOf ("Master Bus", false)), "rename keeps the favourite");
        CHECK (! lib.rename (lib.indexOf ("Clean Glue", true), "X", &err) && ! lib.remove (0, &err), "factory presets can't be renamed or deleted");
        const fs::path ex = work / "export" / "Master Bus.gglue";
        CHECK (lib.exportFile (lib.indexOf ("Master Bus", false), ex, &err) && fs::exists (ex), "export to %s", ex.string().c_str());
        std::string imported;
        CHECK (lib.importFile (ex, &imported, &err) && imported == "Master Bus 2" && lib.presets()[(size_t) lib.indexOf (imported, false)].values[kThreshold] == -3.f,
               "import never overwrites: \"%s\"", imported.c_str());
        { std::ofstream bad (work / "bad.gglue"); bad << "hello"; }
        CHECK (! lib.importFile (work / "bad.gglue", nullptr, &err), "importing a non-preset file is refused (%s)", err.c_str());
        PresetLibrary again (dir);
        CHECK (again.isFavorite (again.indexOf ("Master Bus", false)) && again.isFavorite (again.indexOf ("Drum Punch", true)) && again.presets().size() == nf + 3,
               "favourites and user presets persist (new library instance)");
        CHECK (again.search ("glue").size() >= 10 && again.search ("", "DRUM BUS").size() == 6 && again.search ("", "", true).size() == 2
               && again.search ("", "USER").size() == 3 && again.search ("MASTER BUS").size() == 2, "search by name, category, favourites, user");
        const auto all = again.search ("");
        CHECK (again.nextIndex (all.back(), 1, all) == all.front() && again.nextIndex (all.front(), -1, all) == all.back()
               && again.nextIndex (all[3], 1, all) == all[4], "next / previous wrap around");
        CHECK (again.remove (again.indexOf ("BadName", false), &err) && again.indexOf ("BadName", false) < 0 && ! fs::exists (dir / "BadName.gglue"), "delete removes the file");
    }

    std::printf ("State save / restore (A/B)\n");
    {
        PluginState s;
        s.current = factoryPresets()[5].values; s.presetName = factoryPresets()[5].name;
        s.copyActiveToOther(); s.switchSlot(); s.current[kMix] = 42; s.current[kBypass] = 1; s.modified = true;
        const std::string text = stateToText (s);
        PluginState r;
        CHECK (stateFromText (text, r) && r.activeSlot == 'B' && r.current == s.current && r.other == s.other && r.presetName == s.presetName && r.modified,
               "full state round trip: both A/B slots, active slot, preset name, modified flag (%zu bytes)", text.size());
        r.switchSlot();
        CHECK (r.current[kMix] == factoryPresets()[5].values[kMix] && r.other[kMix] == 42, "A/B switch swaps the slots");
        PluginState junk;
        CHECK (! stateFromText ("garbage", junk) && ! stateFromText ("G-Glue State 1\nfoo=1\n", junk), "corrupt state is rejected");
        PluginState partial;
        CHECK (stateFromText ("G-Glue State 1\nslot=A\nA.threshold=-12\nA.future_param=3\nA.mix=500\n", partial)
               && partial.current[kThreshold] == -12.f && partial.current[kMix] == 100.f && partial.current[kRatio] == defaultValues()[kRatio],
               "unknown keys ignored, out-of-range values clamped, missing values default");
    }

    std::printf ("CPU (stereo, 48 kHz, 10 s of audio, this machine)\n");
    for (int os : { 1, 2, 4 })
    {
        ParamValues v = defaultValues(); v[kThreshold] = -20; v[kAnalog] = 1;
        EngineConfig c; c.oversampling = os; Engine e; fresh (e, fs48, v, c);
        Buf b = sine (fs48, 100, 10.0, 0.7);
        const auto t0 = std::chrono::steady_clock::now();
        run (e, b, 512);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %dx oversampling: %.3f s for 10 s = %.2f %% of one core\n", os, sec, sec * 10);
        // under an emulator (QEMU, set by MPCStandalone/Makefile) the timing says nothing about real hardware
        if (os == 2 && ! std::getenv ("GGLUE_EMULATED")) CHECK (sec < 1.0, "2x oversampling (default) under 10 %% of one core");
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
