// Writes the demo project: synthesised WAV clips in <out>/Demo and a kit that uses them in <out>/Kits.
// Copy the Clips folder to /sdcard/Clips on the MPC (the installer does this), then SETTINGS > KIT > "Demo Kit" > LOAD KIT.
//   make demo            -> test/demo/Clips/{Demo/*.wav, Kits/Demo Kit.dcpkit}
#include "Utilities/Wav.h"
#include <cmath>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace cp;
static const double kPi2 = 6.283185307179586;
struct Buf
{
    int rate; std::vector<float> l, r;
    Buf (int rate_, double seconds) : rate (rate_), l ((size_t) (seconds * rate_)), r ((size_t) (seconds * rate_)) {}
    void add (size_t i, float a, float b) { if (i < l.size()) { l[i] += a; r[i] += b; } }
    bool save (const std::string& path, float gain = 1.f) const
    {
        float pk = 1e-6f; for (size_t i = 0; i < l.size(); ++i) pk = std::fmax (pk, std::fmax (std::fabs (l[i]), std::fabs (r[i])));
        const float g = gain * 0.89f / pk;
        std::vector<int16_t> s (l.size() * 2);
        for (size_t i = 0; i < l.size(); ++i) { s[2 * i] = (int16_t) std::lround (l[i] * g * 32767.f); s[2 * i + 1] = (int16_t) std::lround (r[i] * g * 32767.f); }
        const bool ok = writeWav16 (path.c_str(), s.data(), (long) l.size(), rate);
        std::printf ("%s %s\n", ok ? "wrote" : "FAILED", path.c_str());
        return ok;
    }
};
static uint32_t seed = 12345;
static float noise() { seed = seed * 1664525u + 1013904223u; return (float) ((int32_t) seed) / 2147483648.f; }

static void kick (Buf& b, size_t at, float amp = 1.f)
{
    double ph = 0;
    for (int i = 0; i < (int) (0.35 * b.rate); ++i)
    {
        const double t = (double) i / b.rate, f = 45.0 + 110.0 * std::exp (-t * 30.0);
        ph += kPi2 * f / b.rate;
        const float v = (float) (std::sin (ph) * std::exp (-t * 9.0)) * amp;
        b.add (at + i, v, v);
    }
}
static void snare (Buf& b, size_t at, float amp = 1.f)
{
    for (int i = 0; i < (int) (0.25 * b.rate); ++i)
    {
        const double t = (double) i / b.rate;
        const float body = (float) (std::sin (kPi2 * 185.0 * t) * std::exp (-t * 25.0)) * 0.6f;
        const float n = noise() * (float) std::exp (-t * 16.0) * 0.7f;
        b.add (at + i, (body + n) * amp, (body + n * 0.9f) * amp);
    }
}
static void hat (Buf& b, size_t at, float amp = 0.35f)
{
    float hp = 0, prev = 0;
    for (int i = 0; i < (int) (0.06 * b.rate); ++i)
    {
        const double t = (double) i / b.rate;
        const float n = noise(); hp = 0.9f * (hp + n - prev); prev = n;
        const float v = hp * (float) std::exp (-t * 70.0) * amp;
        b.add (at + i, v * 0.9f, v);
    }
}
static void tone (Buf& b, size_t at, double seconds, double hz, float amp, double attack, double decay, int harmonics = 1, float pan = 0.f)
{
    for (int i = 0; i < (int) (seconds * b.rate); ++i)
    {
        const double t = (double) i / b.rate;
        double env = std::fmin (1.0, t / attack) * std::exp (-t * decay);
        env *= std::fmin (1.0, (seconds - t) / 0.01);                          // no click at the end
        double v = 0;
        for (int h = 1; h <= harmonics; ++h) v += std::sin (kPi2 * hz * h * t) / h;
        const float s = (float) (v * env) * amp;
        b.add (at + i, s * (1.f - pan * 0.5f), s * (1.f + pan * 0.5f));
    }
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "test/demo/Clips";
    for (size_t i = 1; i <= out.size(); ++i) if (i == out.size() || out[i] == '/') mkdir (out.substr (0, i).c_str(), 0755);   // mkdir -p
    const std::string demo = out + "/Demo", kitDir = out + "/Kits";
    mkdir (demo.c_str(), 0755); mkdir (kitDir.c_str(), 0755);
    const int R = 44100;
    const double beat90 = 60.0 / 90.0;
    bool ok = true;
    {   // 2-bar boom-bap break at 90 BPM
        Buf b (R, 8 * beat90);
        for (int bar = 0; bar < 2; ++bar)
        {
            const double o = bar * 4 * beat90;
            kick (b, (size_t) ((o + 0) * R)); kick (b, (size_t) ((o + 1.75 * beat90) * R), 0.8f); kick (b, (size_t) ((o + 2.5 * beat90) * R), 0.9f);
            snare (b, (size_t) ((o + beat90) * R)); snare (b, (size_t) ((o + 3 * beat90) * R));
            for (int h = 0; h < 8; ++h) hat (b, (size_t) ((o + h * 0.5 * beat90 + (h & 1) * 0.06 * beat90) * R));
        }
        ok &= b.save (demo + "/Drum Break 90.wav");
    }
    {   // bass: 2 bars
        Buf b (R, 8 * beat90);
        const double notes[] { 55.0, 55.0, 65.41, 49.0, 55.0, 73.42, 65.41, 49.0 };
        for (int k = 0; k < 8; ++k) tone (b, (size_t) (k * beat90 * R), beat90 * 0.9, notes[k], 0.8f, 0.004, 1.5, 3);
        ok &= b.save (demo + "/Bass Loop 90.wav");
    }
    {   // keys: 4 bars of chords, recorded at 48 kHz (converted to the MPC's rate on load)
        Buf b (48000, 16 * beat90);
        const double chords[4][3] { { 220.0, 261.63, 329.63 }, { 196.0, 246.94, 293.66 }, { 174.61, 220.0, 261.63 }, { 196.0, 246.94, 311.13 } };
        for (int c = 0; c < 4; ++c) for (double f : chords[c]) tone (b, (size_t) (c * 4 * beat90 * 48000), 4 * beat90, f, 0.3f, 0.03, 0.4, 4, (float) (f / 400.0 - 0.5));
        ok &= b.save (demo + "/Keys Loop 90 (48k).wav");
    }
    {   // 1-bar soul chop at 96 BPM (Stretch To Auto fits it to the tempo)
        const double bt = 60.0 / 96.0;
        Buf b (R, 4 * bt);
        const double notes[] { 293.66, 349.23, 440.0, 392.0 };
        for (int k = 0; k < 4; ++k) { tone (b, (size_t) (k * bt * R), bt, notes[k], 0.35f, 0.01, 2.0, 5); tone (b, (size_t) (k * bt * R), bt, notes[k] / 2, 0.3f, 0.01, 2.0, 2); }
        kick (b, 0, 0.7f); snare (b, (size_t) (bt * R), 0.6f); kick (b, (size_t) (2 * bt * R), 0.7f); snare (b, (size_t) (3 * bt * R), 0.6f);
        ok &= b.save (demo + "/Soul Chop 96.wav");
    }
    {   Buf b (R, 1.5); tone (b, 0, 1.5, 220.0, 0.5f, 0.08, 0.6, 7); tone (b, 0, 1.5, 660.0, 0.2f, 0.1, 0.8, 1); ok &= b.save (demo + "/Vocal Ahh.wav"); }
    {   Buf b (R, 0.7); for (double f : { 261.63, 311.13, 392.0, 466.16 }) tone (b, 0, 0.7, f, 0.3f, 0.002, 5.0, 6); ok &= b.save (demo + "/Stab Chord.wav"); }
    {   Buf b (R, 0.4); kick (b, 0); ok &= b.save (demo + "/Kick.wav"); }
    {   Buf b (R, 0.3); snare (b, 0); ok &= b.save (demo + "/Snare.wav"); }
    {   Buf b (R, 0.1); hat (b, 0, 1.f); ok &= b.save (demo + "/Hat.wav"); }
    {   // riser: noise swept up over 2 bars
        Buf b (R, 8 * beat90);
        float lp = 0;
        const size_t n = b.l.size();
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / n, a = 0.02 + 0.5 * t * t;
            lp += (float) a * (noise() - lp);
            const float v = lp * (float) (0.2 + 0.8 * t) * (float) std::fmin (1.0, (1.0 - t) * 200.0);
            b.add (i, v, v * 0.95f);
        }
        ok &= b.save (demo + "/Riser FX 90.wav");
    }
    // the kit (same format as the plugin's saved state)
    const std::string kit = kitDir + "/Demo Kit.dcpkit";
    FILE* f = std::fopen (kit.c_str(), "w");
    if (f == nullptr) { std::printf ("FAILED %s\n", kit.c_str()); return 1; }
    std::fprintf (f,
        "DaClipPads 1\n"
        "p.tempo=90\np.swing=56\np.quant=6\np.smpon=1\np.smppreset=4\np.bits=12\np.rate=40000\np.aa=0.7\np.quantize=1\np.sat=0.35\np.noise=0.03\np.crackle=0\n"
        "p.revret=0.45\np.revsize=0.45\np.dlyret=0.4\np.dlytime=3\np.cratio=3\np.cthresh=-14\np.cmakeup=3\n"
        "c0.file=Demo/Drum Break 90.wav\nc0.mode=1\nc0.pmode=1\nc0.slen=1\nc0.stype=0\n"
        "c1.file=Demo/Kick.wav\nc1.mode=0\nc1.quant=5\nc1.pmode=0\n"
        "c2.file=Demo/Snare.wav\nc2.mode=0\nc2.quant=5\nc2.pmode=0\nc2.rsend=0.15\n"
        "c3.file=Demo/Hat.wav\nc3.mode=0\nc3.quant=5\nc3.pmode=0\nc3.vol=-4\nc3.pan=0.2\n"
        "c4.file=Demo/Bass Loop 90.wav\nc4.mode=1\nc4.pmode=1\nc4.slen=1\nc4.stype=1\nc4.filt=1\nc4.cutoff=2500\n"
        "c5.file=Demo/Keys Loop 90 (48k).wav\nc5.mode=1\nc5.pmode=1\nc5.slen=1\nc5.stype=1\nc5.rsend=0.2\nc5.vol=-3\n"
        "c6.file=Demo/Soul Chop 96.wav\nc6.mode=1\nc6.pmode=1\nc6.slen=1\nc6.stype=3\n"
        "c7.file=Demo/Riser FX 90.wav\nc7.mode=3\nc7.pmode=1\nc7.slen=1\nc7.rsend=0.3\nc7.dsend=0.2\nc7.vol=-6\n"
        "c8.file=Demo/Stab Chord.wav\nc8.mode=0\nc8.quant=3\nc8.rsend=0.25\nc8.dsend=0.2\n"
        "c9.file=Demo/Vocal Ahh.wav\nc9.mode=2\nc9.quant=5\nc9.pmode=1\nc9.stype=2\nc9.rsend=0.3\nc9.dsend=0.25\nc9.vol=-3\n"
        "scene0=17\nscene1=49\nscene2=65\nscene3=545\n");
    std::fclose (f);
    std::printf ("wrote %s\n", kit.c_str());
    return ok ? 0 : 1;
}
