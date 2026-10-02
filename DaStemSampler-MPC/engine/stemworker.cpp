// Da Stem Sampler stem worker: offline AI stem separation for the MPC X, started by the plugin at low priority.
// Engine: demucs.cpp (Demucs v3 / v4 in C++ with Eigen, MIT) on the MPC's 32-bit ARM, NEON, no network.
//
//   stemworker separate --models DIR --quality fast|studio|max [--six] --in SONG.wav --out DIR --status FILE
//                       [--threads N] [--bleed 0-100] [--smooth 0-100]
//   stemworker bench --models DIR [--seconds 20] [--threads N]     speed + memory of each installed model
//   stemworker models --models DIR                                 which models are installed
//
// Quality:  fast   = hdemucs_mmi (Demucs v3)           4 stems
//           studio = htdemucs (v4), or htdemucs_6s with --six  4 or 6 stems (+ guitar, piano)
//           max    = htdemucs_ft: 4 fine-tuned v4 models, one per stem (about 4x the time of studio)
// Output (24-bit WAV at the song's own sample rate): vocals drums bass other [guitar piano] instrumental.
// Bleed reduction re-estimates every stem with a sharpened soft mask over all stems (STFT), and Smooth averages the
// masks over time to avoid "musical noise" artifacts. Both 0 = the model's raw output.
// The status file is rewritten as "state progress message" (state: running / done / error).
#include "dsp.hpp"
#include "model.hpp"
#include "tensor.hpp"
#include <Eigen/Core>
#include <unsupported/Eigen/FFT>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static std::string statusPath;
static float progressBase = 0.f, progressSpan = 1.f;
static void status (const char* state, float p, const char* msg)
{
    if (statusPath.empty()) { std::printf ("[%s %3.0f%%] %s\n", state, p * 100, msg); std::fflush (stdout); return; }
    const std::string tmp = statusPath + ".tmp";
    FILE* f = std::fopen (tmp.c_str(), "w");
    if (! f) return;
    std::fprintf (f, "%s %.4f %s\n", state, p, msg);
    std::fclose (f);
    std::rename (tmp.c_str(), statusPath.c_str());
}
static void fail (const char* msg) { status ("error", 0.f, msg); std::fprintf (stderr, "stemworker: %s\n", msg); std::exit (1); }

// ------------------------------------------------------------------------------------------------ WAV in / out
struct Audio { std::vector<float> l, r; int rate = 44100; int bits = 16; };
static uint32_t rd32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
static uint16_t rd16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
static bool readWav (const char* path, Audio& a, double maxSeconds)
{
    FILE* f = std::fopen (path, "rb");
    if (! f) return false;
    unsigned char h[12];
    if (std::fread (h, 1, 12, f) != 12 || std::memcmp (h, "RIFF", 4) || std::memcmp (h + 8, "WAVE", 4)) { std::fclose (f); return false; }
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0, size = 0; long data = -1; unsigned char c[8];
    while (std::fread (c, 1, 8, f) == 8)
    {
        const uint32_t sz = rd32 (c + 4); const long here = std::ftell (f);
        if (! std::memcmp (c, "fmt ", 4))
        {
            unsigned char m[40] = {}; const size_t n = sz < 40 ? sz : 40;
            if (std::fread (m, 1, n, f) != n) break;
            fmt = rd16 (m); ch = rd16 (m + 2); rate = rd32 (m + 4); bits = rd16 (m + 14);
            if (fmt == 0xFFFE && n >= 26) fmt = rd16 (m + 24);
        }
        else if (! std::memcmp (c, "data", 4)) { data = here; size = sz; break; }
        std::fseek (f, here + (long) sz + (sz & 1), SEEK_SET);
    }
    const bool fl = fmt == 3;
    if (data < 0 || ch < 1 || ! ((fmt == 1 && (bits == 16 || bits == 24 || bits == 32 || bits == 8)) || (fl && (bits == 32 || bits == 64)))) { std::fclose (f); return false; }
    const int bps = bits / 8, fb = bps * ch;
    long frames = (long) (size / (uint32_t) fb);
    if (frames > (long) (maxSeconds * rate)) frames = (long) (maxSeconds * rate);
    a.rate = (int) rate; a.bits = bits; a.l.resize ((size_t) frames); a.r.resize ((size_t) frames);
    std::vector<unsigned char> buf ((size_t) fb * 4096);
    long done = 0;
    auto smp = [&] (const unsigned char* p) -> float {
        switch (bits) {
            case 8: return (p[0] - 128) / 128.f;
            case 16: return (int16_t) rd16 (p) / 32768.f;
            case 24: return (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24) / 2147483648.f;
            case 32: { const uint32_t u = rd32 (p); if (fl) { float x; std::memcpy (&x, &u, 4); return x; } return (int32_t) u / 2147483648.f; }
            default: { uint64_t u = rd32 (p) | ((uint64_t) rd32 (p + 4) << 32); double x; std::memcpy (&x, &u, 8); return (float) x; } } };
    while (done < frames)
    {
        const long want = std::min (4096L, frames - done);
        const long got = (long) std::fread (buf.data(), (size_t) fb, (size_t) want, f);
        for (long i = 0; i < got; ++i) { const unsigned char* p = buf.data() + i * fb; a.l[(size_t) (done + i)] = smp (p); a.r[(size_t) (done + i)] = ch > 1 ? smp (p + bps) : a.l[(size_t) (done + i)]; }
        done += got;
        if (got < want) break;
    }
    std::fclose (f);
    a.l.resize ((size_t) done); a.r.resize ((size_t) done);
    return done > 0;
}
static bool writeWav24 (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int rate)
{
    const std::string tmp = path + ".part";
    FILE* f = std::fopen (tmp.c_str(), "wb");
    if (! f) return false;
    auto w32 = [&] (uint32_t v) { unsigned char b[4] { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) }; std::fwrite (b, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { unsigned char b[2] { (unsigned char) v, (unsigned char) (v >> 8) }; std::fwrite (b, 1, 2, f); };
    const uint32_t bytes = (uint32_t) (l.size() * 6);
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) rate); w32 ((uint32_t) rate * 6); w16 (6); w16 (24);
    std::fwrite ("data", 1, 4, f); w32 (bytes);
    std::vector<unsigned char> out (6 * 4096);
    for (size_t i = 0; i < l.size(); i += 4096)
    {
        const size_t n = std::min ((size_t) 4096, l.size() - i);
        for (size_t k = 0; k < n; ++k)
            for (int c = 0; c < 2; ++c)
            {
                float x = (c ? r : l)[i + k]; x = x > 1.f ? 1.f : (x < -1.f ? -1.f : x);
                const int32_t v = (int32_t) std::lround (x * 8388607.f);
                unsigned char* p = out.data() + 6 * k + 3 * c; p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16);
            }
        std::fwrite (out.data(), 1, 6 * n, f);
    }
    const bool ok = ! std::ferror (f);
    std::fclose (f);
    return ok && std::rename (tmp.c_str(), path.c_str()) == 0;
}
// windowed-sinc resampler (Blackman, 32 taps), any ratio
static std::vector<float> resample (const std::vector<float>& in, double from, double to)
{
    if (std::fabs (from - to) < 0.5) return in;
    const double ratio = to / from, cut = std::min (1.0, ratio) * 0.97;
    const long n = (long) std::floor (in.size() * ratio);
    std::vector<float> out ((size_t) std::max (0L, n));
    const int T = 32;
    for (long o = 0; o < n; ++o)
    {
        const double pos = o / ratio; const long i0 = (long) std::floor (pos); const double fr = pos - i0;
        double acc = 0;
        for (int k = -T / 2 + 1; k <= T / 2; ++k)
        {
            const long idx = i0 + k; if (idx < 0 || idx >= (long) in.size()) continue;
            const double x = k - fr, w = x / (T / 2.0);
            const double win = std::fabs (w) >= 1 ? 0 : 0.42 + 0.5 * std::cos (M_PI * w) + 0.08 * std::cos (2 * M_PI * w);
            const double a = M_PI * x * cut, sinc = std::fabs (a) < 1e-9 ? 1.0 : std::sin (a) / a;
            acc += in[(size_t) idx] * cut * sinc * win;
        }
        out[(size_t) o] = (float) acc;
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ models
static std::string findModel (const std::string& dir, const char* key)
{
    DIR* d = opendir (dir.c_str());
    if (! d) return "";
    std::string hit; struct dirent* e;
    while ((e = readdir (d)) != nullptr) if (std::strstr (e->d_name, key) && std::strstr (e->d_name, ".bin")) { hit = dir + "/" + e->d_name; break; }
    closedir (d);
    return hit;
}
static long peakRssMb() { struct rusage u; getrusage (RUSAGE_SELF, &u); return u.ru_maxrss / 1024; }

// runs one v4 model on the 44.1 kHz stereo song; returns targets(source, channel, sample)
static Eigen::Tensor3dXf runV4 (const std::string& file, const Eigen::MatrixXf& audio, const char* label)
{
    std::unique_ptr<demucscpp::demucs_model> m (new demucscpp::demucs_model());
    status ("running", progressBase, (std::string ("loading ") + label).c_str());
    if (! demucscpp::load_demucs_model (file, m.get())) fail ((std::string ("can't load model ") + file).c_str());
    return demucscpp::demucs_inference (*m, audio, [label] (float p, const std::string&) {
        char b[96]; std::snprintf (b, sizeof b, "separating (%s)", label);
        status ("running", progressBase + progressSpan * std::min (1.f, p), b); });
}
static Eigen::Tensor3dXf runV3 (const std::string& file, const Eigen::MatrixXf& audio)
{
    std::unique_ptr<demucscpp_v3::demucs_v3_model> m (new demucscpp_v3::demucs_v3_model());
    status ("running", progressBase, "loading Demucs v3");
    if (! demucscpp_v3::load_demucs_v3_model (file, m.get())) fail ("can't load the Demucs v3 model");
    return demucscpp_v3::demucs_v3_inference (*m, audio, [] (float p, const std::string&) {
        status ("running", progressBase + progressSpan * std::min (1.f, p), "separating (Demucs v3)"); });
}

// ------------------------------------------------------------------------------------------------ bleed reduction
// Per STFT bin: mask_s = |S_s|^p / sum_k |S_k|^p (p = 1 + 3 * bleed), optionally smoothed over time, applied to the
// stems' own sum (the mixture estimate). p = 1 is a plain ratio mask; larger p pushes energy towards the dominant
// stem, which removes bleed but can add artifacts; Smooth counters that.
static void reduceBleed (std::vector<std::vector<float>>& st /* [stem*2+ch] */, int nStems, float bleed, float smooth)
{
    if (bleed <= 0.f) return;
    const int N = 4096, hop = 1024; const float p = 1.f + 3.f * bleed, a = std::min (0.9f, smooth * 0.9f);
    const size_t len = st[0].size();
    std::vector<float> win (N); for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * std::cos (2 * (float) M_PI * i / N);
    Eigen::FFT<float> fft;
    for (int ch = 0; ch < 2; ++ch)
    {
        std::vector<std::vector<float>> out ((size_t) nStems, std::vector<float> (len, 0.f));
        std::vector<float> norm (len, 0.f);
        std::vector<std::vector<float>> prevMask ((size_t) nStems, std::vector<float> (N / 2 + 1, -1.f));
        std::vector<std::vector<std::complex<float>>> spec ((size_t) nStems);
        std::vector<float> frame (N);
        for (size_t start = 0; start + 1 < len; start += hop)
        {
            for (int s = 0; s < nStems; ++s)
            {
                for (int i = 0; i < N; ++i) { const size_t k = start + (size_t) i; frame[(size_t) i] = k < len ? st[(size_t) (2 * s + ch)][k] * win[(size_t) i] : 0.f; }
                fft.fwd (spec[(size_t) s], frame);
            }
            for (int b = 0; b <= N / 2; ++b)
            {
                std::complex<float> mix (0, 0); float sum = 1e-12f; float w[8];
                for (int s = 0; s < nStems; ++s) { mix += spec[(size_t) s][(size_t) b]; w[s] = std::pow (std::abs (spec[(size_t) s][(size_t) b]) + 1e-9f, p); sum += w[s]; }
                for (int s = 0; s < nStems; ++s)
                {
                    float m = w[s] / sum;
                    float& pm = prevMask[(size_t) s][(size_t) b];
                    if (pm >= 0.f) m = a * pm + (1.f - a) * m;
                    pm = m;
                    spec[(size_t) s][(size_t) b] = mix * m;
                }
            }
            for (int s = 0; s < nStems; ++s)
            {
                for (int b = 1; b < N / 2; ++b) spec[(size_t) s][(size_t) (N - b)] = std::conj (spec[(size_t) s][(size_t) b]);
                fft.inv (frame, spec[(size_t) s]);
                for (int i = 0; i < N; ++i) { const size_t k = start + (size_t) i; if (k < len) out[(size_t) s][k] += frame[(size_t) i] * win[(size_t) i]; }
            }
            for (int i = 0; i < N; ++i) { const size_t k = start + (size_t) i; if (k < len) norm[k] += win[(size_t) i] * win[(size_t) i]; }
            status ("running", progressBase + progressSpan * (ch * 0.5f + 0.5f * (float) start / (float) len), "reducing bleed");
        }
        for (int s = 0; s < nStems; ++s) for (size_t k = 0; k < len; ++k) st[(size_t) (2 * s + ch)][k] = norm[k] > 1e-6f ? out[(size_t) s][k] / norm[k] : 0.f;
    }
}

// ------------------------------------------------------------------------------------------------ commands
static const char* arg (int argc, char** argv, const char* key, const char* def)
{
    for (int i = 1; i + 1 < argc; ++i) if (! std::strcmp (argv[i], key)) return argv[i + 1];
    return def;
}
static bool flag (int argc, char** argv, const char* key) { for (int i = 1; i < argc; ++i) if (! std::strcmp (argv[i], key)) return true; return false; }

static int cmdModels (const std::string& dir)
{
    const char* keys[][2] = { { "hdemucs_mmi", "Fast (Demucs v3)" }, { "htdemucs-4s", "Studio 4 stems (Demucs v4)" }, { "htdemucs-6s", "Studio 6 stems (Demucs v4)" },
                              { "htdemucs_ft_vocals", "Maximum: vocals" }, { "htdemucs_ft_drums", "Maximum: drums" }, { "htdemucs_ft_bass", "Maximum: bass" },
                              { "htdemucs_ft_other", "Maximum: other" } };
    for (auto& k : keys) { const std::string f = findModel (dir, k[0]); std::printf ("%-28s %s\n", k[1], f.empty() ? "not installed" : f.c_str()); }
    return 0;
}
static int cmdBench (const std::string& dir, double secs)
{
    // a synthetic test song (drums + bass + chords + a "voice"), so the benchmark needs no audio files
    const int sr = 44100; const long n = (long) (secs * sr);
    Eigen::MatrixXf audio (2, n);
    for (long i = 0; i < n; ++i)
    {
        const double t = (double) i / sr, beat = std::fmod (t, 0.5);
        const double kick = std::exp (-beat * 30) * std::sin (2 * M_PI * 55 * beat * (1 + 2 * std::exp (-beat * 40)));
        const double bass = 0.3 * std::sin (2 * M_PI * 55 * t), chord = 0.1 * (std::sin (2 * M_PI * 220 * t) + std::sin (2 * M_PI * 277 * t) + std::sin (2 * M_PI * 330 * t));
        const double voice = 0.2 * std::sin (2 * M_PI * (440 + 20 * std::sin (2 * M_PI * 5 * t)) * t) * (0.5 + 0.5 * std::sin (2 * M_PI * 0.25 * t));
        audio (0, i) = (float) (0.5 * kick + bass + chord + voice); audio (1, i) = (float) (0.5 * kick + bass + chord * 0.8 + voice);
    }
    std::printf ("benchmark: %.0f s of audio, %s threads\n", secs, std::getenv ("OMP_NUM_THREADS") ? std::getenv ("OMP_NUM_THREADS") : "default");
    const char* keys[][2] = { { "hdemucs_mmi", "v3" }, { "htdemucs-4s", "v4" }, { "htdemucs-6s", "v4" }, { "htdemucs_ft_vocals", "v4" } };
    for (auto& k : keys)
    {
        const std::string f = findModel (dir, k[0]);
        if (f.empty()) { std::printf ("%-20s not installed\n", k[0]); continue; }
        const auto t0 = std::chrono::steady_clock::now();
        statusPath.clear();
        if (! std::strcmp (k[1], "v3")) runV3 (f, audio); else runV4 (f, audio, k[0]);
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("RESULT %-20s %7.1f s for %.0f s audio = %5.1fx real time, peak memory %ld MB%s\n", k[0], el, secs, el / secs, peakRssMb(),
                     ! std::strcmp (k[0], "htdemucs_ft_vocals") ? " (Maximum runs 4 of these)" : "");
        std::fflush (stdout);
    }
    return 0;
}
static int cmdSeparate (int argc, char** argv)
{
    const std::string dir = arg (argc, argv, "--models", ""), in = arg (argc, argv, "--in", ""), outDir = arg (argc, argv, "--out", "");
    const std::string q = arg (argc, argv, "--quality", "studio");
    statusPath = arg (argc, argv, "--status", "");
    const bool six = flag (argc, argv, "--six");
    const float bleed = (float) std::atof (arg (argc, argv, "--bleed", "0")) / 100.f, smooth = (float) std::atof (arg (argc, argv, "--smooth", "30")) / 100.f;
    if (dir.empty() || in.empty() || outDir.empty()) fail ("usage: stemworker separate --models DIR --in WAV --out DIR --status FILE");
    setpriority (PRIO_PROCESS, 0, 19);                                       // never compete with MPC's audio
    status ("running", 0.f, "reading the song");
    Audio a;
    if (! readWav (in.c_str(), a, 900.0)) fail ("can't read the WAV file");
    const int origRate = a.rate;
    status ("running", 0.01f, "converting to 44.1 kHz for the model");
    const std::vector<float> l44 = resample (a.l, origRate, 44100), r44 = resample (a.r, origRate, 44100);
    a.l.clear(); a.l.shrink_to_fit(); a.r.clear(); a.r.shrink_to_fit();
    Eigen::MatrixXf audio (2, (Eigen::Index) l44.size());
    for (size_t i = 0; i < l44.size(); ++i) { audio (0, (Eigen::Index) i) = l44[i]; audio (1, (Eigen::Index) i) = r44[i]; }
    // stems at 44.1 kHz: index order drums bass other vocals [guitar piano]
    std::vector<std::vector<float>> st;
    int nStems = 4;
    auto take = [&] (const Eigen::Tensor3dXf& t, int src, int slot) {
        for (int ch = 0; ch < 2; ++ch) { auto& v = st[(size_t) (2 * slot + ch)]; v.resize (l44.size()); for (size_t i = 0; i < l44.size(); ++i) v[i] = t (src, ch, (Eigen::Index) i); } };
    const float bleedShare = bleed > 0 ? 0.1f : 0.f;
    if (q == "max")
    {
        const char* names[] = { "htdemucs_ft_drums", "htdemucs_ft_bass", "htdemucs_ft_other", "htdemucs_ft_vocals" };
        for (auto n : names) if (findModel (dir, n).empty()) fail ("Maximum quality needs the four htdemucs_ft models (see the model manager)");
        st.assign (8, {});
        for (int s = 0; s < 4; ++s)                                          // one model at a time: memory stays at one model
        {
            progressBase = 0.02f + s * (0.96f - bleedShare) / 4; progressSpan = (0.96f - bleedShare) / 4;
            Eigen::Tensor3dXf t = runV4 (findModel (dir, names[s]), audio, names[s] + 10);
            take (t, s, s);
        }
    }
    else if (q == "fast")
    {
        const std::string f = findModel (dir, "hdemucs_mmi");
        if (f.empty()) fail ("Fast quality needs the hdemucs_mmi model (see the model manager)");
        progressBase = 0.02f; progressSpan = 0.96f - bleedShare;
        Eigen::Tensor3dXf t = runV3 (f, audio);
        st.assign (8, {}); for (int s = 0; s < 4; ++s) take (t, s, s);
    }
    else
    {
        const std::string f = findModel (dir, six ? "htdemucs-6s" : "htdemucs-4s");
        if (f.empty()) fail (six ? "6-stem separation needs the htdemucs-6s model" : "Studio quality needs the htdemucs-4s model");
        progressBase = 0.02f; progressSpan = 0.96f - bleedShare;
        Eigen::Tensor3dXf t = runV4 (f, audio, six ? "Demucs v4, 6 stems" : "Demucs v4");
        nStems = six ? 6 : 4;
        st.assign ((size_t) (2 * nStems), {}); for (int s = 0; s < nStems; ++s) take (t, s, s);
    }
    progressBase = 0.98f - bleedShare; progressSpan = bleedShare;
    reduceBleed (st, nStems, bleed, smooth);
    // back to the song's own rate, 24-bit; instrumental = every stem except vocals
    mkdir (outDir.c_str(), 0755);
    static const char* names[] = { "drums", "bass", "other", "vocals", "guitar", "piano" };
    std::vector<float> il (l44.size(), 0.f), ir (l44.size(), 0.f);
    for (int s = 0; s < nStems; ++s) if (s != 3) for (size_t i = 0; i < l44.size(); ++i) { il[i] += st[(size_t) (2 * s)][i]; ir[i] += st[(size_t) (2 * s + 1)][i]; }
    for (int s = 0; s <= nStems; ++s)
    {
        const bool inst = s == nStems;
        char b[96]; std::snprintf (b, sizeof b, "writing %s", inst ? "instrumental" : names[s]);
        status ("running", 0.985f + 0.015f * s / (nStems + 1), b);
        const std::vector<float>& L = inst ? il : st[(size_t) (2 * s)]; const std::vector<float>& R = inst ? ir : st[(size_t) (2 * s + 1)];
        if (! writeWav24 (outDir + "/" + (inst ? "instrumental" : names[s]) + ".wav", resample (L, 44100, origRate), resample (R, 44100, origRate), origRate))
            fail ("can't write the stems (is the drive full?)");
    }
    char done[128]; std::snprintf (done, sizeof done, "%d stems ready (peak memory %ld MB)", nStems, peakRssMb());
    status ("done", 1.f, done);
    return 0;
}

int main (int argc, char** argv)
{
    const char* cmd = argc > 1 ? argv[1] : "";
    if (const char* t = arg (argc, argv, "--threads", nullptr)) setenv ("OMP_NUM_THREADS", t, 1);
    if (! std::strcmp (cmd, "separate")) return cmdSeparate (argc, argv);
    if (! std::strcmp (cmd, "bench")) return cmdBench (arg (argc, argv, "--models", "."), std::atof (arg (argc, argv, "--seconds", "20")));
    if (! std::strcmp (cmd, "models")) return cmdModels (arg (argc, argv, "--models", "."));
    std::fprintf (stderr, "usage: stemworker separate|bench|models ... (see the top of stemworker.cpp)\n");
    return 1;
}
