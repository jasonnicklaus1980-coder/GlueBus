// Host-side test of the legacy sampler's VST2 wrapper: loads the plugin the way the host does, sends MIDI through
// effProcessEvents, and checks the audio and the interface. Fixtures are generated under build/.
#include "../MPC/src/vst2.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" AEffect* VSTPluginMain(audioMasterCallback host);

static int checks = 0, fails = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }

// A 16-bit mono WAV: a 200 Hz tone, half a second.
static std::vector<uint8_t> toneWav()
{
    const uint32_t frames = 22050;
    std::vector<uint8_t> d;
    for (uint32_t i = 0; i < frames; ++i) put16(d, (uint16_t)(int16_t)(12000 * std::sin(2 * 3.14159265358979 * 200 * i / 44100.0)));
    std::vector<uint8_t> b = {'R','I','F','F'}; put32(b, 36 + (uint32_t)d.size()); b.insert(b.end(), {'W','A','V','E','f','m','t',' '});
    put32(b, 16); put16(b, 1); put16(b, 1); put32(b, 44100); put32(b, 88200); put16(b, 2); put16(b, 16);
    b.insert(b.end(), {'d','a','t','a'}); put32(b, (uint32_t)d.size()); b.insert(b.end(), d.begin(), d.end());
    return b;
}

static double rms(const float* x, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double)x[i] * x[i]; return std::sqrt(s / n); }

static VstEvents oneMidi(uint8_t status, uint8_t note, uint8_t vel, VstMidiEvent& e)
{
    std::memset(&e, 0, sizeof e);
    e.type = kVstMidiType; e.byteSize = sizeof(VstMidiEvent);
    e.midiData[0] = (char)status; e.midiData[1] = (char)note; e.midiData[2] = (char)vel;
    VstEvents ev; std::memset(&ev, 0, sizeof ev);
    ev.numEvents = 1; ev.events[0] = (VstEvent*)&e;
    return ev;
}

int main()
{
    // a project with one pad: MIDI note 40 is pad 5 under the default map
    const std::string dir = "build/lsampler";
    std::filesystem::create_directories(dir);
    { std::ofstream f(dir + "/tone.wav", std::ios::binary); auto w = toneWav(); f.write((const char*)w.data(), (std::streamsize)w.size()); }
    { std::ofstream f(dir + "/project.mc909");
      f << "MC909SAMPLER-PROJECT 1\nname=test\n[pad 4]\nfile=" << dir << "/tone.wav\n"; }
    setenv("LEGACY_SAMPLER_DIR", dir.c_str(), 1);

    AEffect* fx = VSTPluginMain(nullptr);
    CHECK(fx && fx->magic == kEffectMagic, "the plugin object is created with the VST magic");
    CHECK(fx->numInputs == 0 && fx->numOutputs == 2, "an instrument: no inputs, two outputs");
    CHECK((fx->flags & effFlagsIsSynth) && (fx->flags & effFlagsCanReplacing), "advertises an instrument that can process in place");
    CHECK(fx->dispatcher(fx, effGetPlugCategory, 0, 0, nullptr, 0.f) == kPlugCategSynth, "category is synth");
    char can[64] = "receiveVstEvents";
    CHECK(fx->dispatcher(fx, effCanDo, 0, 0, can, 0.f) == 1, "accepts MIDI events");

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0.f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.f);

    // silent before any note
    std::vector<float> L(512), R(512), OL(512), OR(512);
    float* outs[2] = {L.data(), R.data()}; float* ins[1] = {nullptr};
    fx->processReplacing(fx, ins, outs, 512);
    CHECK(rms(L.data(), 512) < 1e-9, "silent before any note");

    // a note-on for pad 5 starts the sample
    VstMidiEvent me; VstEvents ev = oneMidi(0x90, 40, 100, me);
    fx->dispatcher(fx, effProcessEvents, 0, 0, &ev, 0.f);
    fx->processReplacing(fx, ins, outs, 512);
    CHECK(rms(L.data(), 512) > 0.01, "a note-on plays its pad (rms %.4f)", rms(L.data(), 512));

    // a note outside the pad range does nothing (checked on a fresh instance, so no earlier note can be heard)
    AEffect* fresh = VSTPluginMain(nullptr);
    fresh->dispatcher(fresh, effOpen, 0, 0, nullptr, 0.f);
    fresh->dispatcher(fresh, effSetSampleRate, 0, 0, nullptr, 44100.f);
    VstMidiEvent far; VstEvents evFar = oneMidi(0x90, 20, 100, far);
    fresh->dispatcher(fresh, effProcessEvents, 0, 0, &evFar, 0.f);
    fresh->processReplacing(fresh, ins, outs, 512);
    CHECK(rms(L.data(), 512) < 1e-9, "a note with no pad is ignored");
    fresh->dispatcher(fresh, effClose, 0, 0, nullptr, 0.f);

    // volume: zero silences the output
    fx->setParameter(fx, 0, 0.f);
    std::fill(L.begin(), L.end(), 0.f);
    fx->processReplacing(fx, ins, outs, 512);
    CHECK(rms(L.data(), 512) < 1e-9, "Volume at 0 is silent");
    fx->setParameter(fx, 0, 0.8f);
    CHECK(std::fabs(fx->getParameter(fx, 0) - 0.8f) < 1e-6f, "Volume reads back");

    // a big block is rendered in pieces and stays finite
    std::vector<float> BL(4096), BR(4096); float* bo[2] = {BL.data(), BR.data()};
    fx->processReplacing(fx, ins, bo, 4096);
    bool finite = true; for (float v : BL) if (!std::isfinite(v)) finite = false;
    CHECK(finite, "a 4096-frame block is rendered in pieces and stays finite");

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.f);
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
