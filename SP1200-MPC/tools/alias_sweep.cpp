// Aliasing check for the SP-1200 model: tone tests and the 20 Hz - 20 kHz exponential sweep at a 96 kHz host, as in
// Yeh, Nolting and Smith (ICMC 2007). Build: g++ -std=c++17 -O2 -Isrc -o alias_sweep tools/alias_sweep.cpp
// Run: SWEEP_OUT=sweep.f32 ./alias_sweep   (prints the tone table; writes the sweep as float32, mono)
#include "sp/Model.h"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace sp;
static const double HOST = 96000.0, PI2 = 2 * 3.14159265358979323846;
static Settings base()
{
    Settings s;
    s.channel = 3;                   // Out 7-8: the unfiltered channel, as in the stage tests
    s.hissDb = -60; s.anaDb = -60; s.convDb = -60; s.humDb = -60; s.groundDb = -60; s.digDb = -60;
    s.analyzer = false;
    return s;
}
static void run (Model& m, const Settings& s, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R)
{
    m.prepare (HOST);
    L.assign (in.size(), 0.f); R.assign (in.size(), 0.f);
    for (size_t p = 0; p < in.size(); p += 256)
    {
        const int n = (int) std::min<size_t> (256, in.size() - p);
        m.process (&in[p], &in[p], &L[p], &R[p], n, s);
    }
}
static double goertzel (const std::vector<float>& x, size_t a, size_t b, double hz)
{
    const double w = PI2 * hz / HOST, c = 2 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (size_t i = a; i < b; ++i) { const double win = 0.5 - 0.5 * std::cos (PI2 * (i - a) / (b - a - 1)); const double s0 = x[i] * win + c * s1 - s2; s2 = s1; s1 = s0; }
    return 2.0 * std::sqrt (std::fmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / ((b - a) * 0.5);
}
int main()
{
    Model* m = new Model();
    Settings s = base();
    const double clk = 26040.0;
    std::printf("tone test, host 96 kHz, SP-1200 clock 26.04 kHz, amplitude 0.5\n");
    std::printf("%8s %10s %10s %10s   (dB re the tone level)\n", "tone Hz", "at tone", "alias", "image");
    for (double f : {1000.0, 4000.0, 8000.0, 12000.0, 14000.0, 16000.0, 18000.0, 20000.0})
    {
        const size_t N = (size_t) (0.5 * HOST);
        std::vector<float> in (N), L, R;
        for (size_t i = 0; i < N; ++i) in[i] = (float) (0.5 * std::sin (PI2 * f * i / HOST));
        run (*m, s, in, L, R);
        const size_t a = N / 2, b = N;
        const double ref = goertzel (in, a, b, f);
        const double at = goertzel (L, a, b, f);
        const double alias = std::fabs (clk - f);     // mirror about the 13.02 kHz Nyquist of the SP-1200 clock
        const double image = clk + f;                 // first image above the clock
        const double ga = goertzel (L, a, b, alias), gi = goertzel (L, a, b, image);
        std::printf("%8.0f %10.1f %10.1f %10.1f\n", f, 20*std::log10(at/ref+1e-12), 20*std::log10(ga/ref+1e-12), 20*std::log10(gi/ref+1e-12));
    }
    // the paper's sweep: 20 Hz to 20 kHz, exponential, 1.5 s, written as float32 for analysis
    const double sec = 1.5; const size_t N = (size_t) (sec * HOST);
    std::vector<float> in (N), L, R;
    const double k = std::log (20000.0 / 20.0) / sec;
    double ph = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const double t = (double) i / HOST, f = 20.0 * std::exp (k * t);
        ph += PI2 * f / HOST; in[i] = (float) (0.5 * std::sin (ph));
    }
    run (*m, s, in, L, R);
    FILE* fo = std::fopen (getenv ("SWEEP_OUT") ? getenv ("SWEEP_OUT") : "sweep_out.f32", "wb");
    std::fwrite (L.data(), sizeof (float), L.size(), fo); std::fclose (fo);
    delete m;
    return 0;
}
