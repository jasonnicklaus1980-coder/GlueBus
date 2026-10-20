// Quanta VST2 wrapper tests (x86 build of the same wrapper source the MPC uses).
#include "../MPC/src/vst2.h"
#include "../DSP/QuantaEngine.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" AEffect* VSTPluginMain(audioMasterCallback host);

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, ...) do { ++g_checks; if (!(cond)) { ++g_fail; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
    AEffect* fx = VSTPluginMain(nullptr);
    CHECK(fx && fx->magic == kEffectMagic, "plugin object is created with the VST magic");
    CHECK(fx->numParams == quanta::P_COUNT && fx->numInputs == 2 && fx->numOutputs == 2, "two in, two out, %d parameters", quanta::P_COUNT);
    CHECK((fx->flags & effFlagsCanReplacing) != 0, "advertises processReplacing");
    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.f);
    fx->dispatcher(fx, effSetBlockSize, 0, 256, nullptr, 0.f);

    char name[64] = "";
    fx->dispatcher(fx, effGetParamName, quanta::P_GLUE, 0, name, 0.f);
    CHECK(!std::strcmp(name, "Glue"), "parameter 2 is named Glue (%s)", name);

    // defaults and setting through the host interface
    CHECK(std::fabs(fx->getParameter(fx, quanta::P_MIX) - 1.f) < 1e-6f, "Mix defaults to 100%%");
    fx->setParameter(fx, quanta::P_TONE, 0.75f);
    CHECK(std::fabs(fx->getParameter(fx, quanta::P_TONE) - 0.75f) < 1e-6f, "a set value reads back");

    // display text
    char disp[64] = "";
    fx->dispatcher(fx, effGetParamDisplay, quanta::P_MONOBASS, 0, disp, 0.f);
    CHECK(!std::strcmp(disp, "Off"), "Mono Bass shows Off by default (%s)", disp);

    // state round trip: save, change everything, restore
    fx->setParameter(fx, quanta::P_DENSITY, 0.4f);
    fx->setParameter(fx, quanta::P_WIDTH, 0.8f);
    void* chunk = nullptr;
    intptr_t size = fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0.f);
    CHECK(chunk && size > 0, "state can be saved (%ld bytes)", (long)size);
    std::vector<unsigned char> saved((unsigned char*)chunk, (unsigned char*)chunk + size);
    for (int i = 0; i < quanta::P_COUNT; ++i) fx->setParameter(fx, i, 0.1f);
    intptr_t ok = fx->dispatcher(fx, effSetChunk, 0, (intptr_t)saved.size(), saved.data(), 0.f);
    CHECK(ok == 1, "saved state loads");
    CHECK(std::fabs(fx->getParameter(fx, quanta::P_DENSITY) - 0.4f) < 1e-6f && std::fabs(fx->getParameter(fx, quanta::P_WIDTH) - 0.8f) < 1e-6f,
          "state restores the parameters");
    std::vector<unsigned char> junk(saved);
    junk[0] = 'X';
    CHECK(fx->dispatcher(fx, effSetChunk, 0, (intptr_t)junk.size(), junk.data(), 0.f) == 0, "a state with the wrong header is refused");

    // processing: Mix 0 gives the dry signal back exactly, through the replacing entry point
    fx->setParameter(fx, quanta::P_MIX, 0.f);
    fx->setParameter(fx, quanta::P_DENSITY, 1.f);
    fx->setParameter(fx, quanta::P_GLUE, 1.f);
    // mix and the other settings glide over about 10 ms, so the check uses the settled end of a longer buffer
    const int N = 8192, TAIL = 1024;
    std::vector<float> il(N), ir(N), ol(N), or_(N);
    for (int i = 0; i < N; ++i) { il[i] = 0.2f * std::sin(0.03f * i); ir[i] = 0.1f * std::sin(0.05f * i); }
    float* ins[2] = {il.data(), ir.data()};
    float* outs[2] = {ol.data(), or_.data()};
    fx->processReplacing(fx, ins, outs, N);
    double err = 0;
    for (int i = N - TAIL; i < N; ++i) err = std::fmax(err, std::fmax(std::fabs(ol[i] - il[i]), std::fabs(or_[i] - ir[i])));
    CHECK(err < 1e-4, "Mix 0 returns the input through the wrapper once settled (max error %g)", err);

    // in-place processing (host sends the same buffers) must also work
    fx->setParameter(fx, quanta::P_MIX, 1.f);
    std::vector<float> ql(N, 0.5f), qr(N, 0.5f);
    float* qi[2] = {ql.data(), qr.data()};
    fx->processReplacing(fx, qi, qi, N);
    bool finite = true;
    for (int i = 0; i < N; ++i) if (!std::isfinite(ql[i]) || !std::isfinite(qr[i])) finite = false;
    CHECK(finite, "in-place processing gives finite output");

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.f);
    std::printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
