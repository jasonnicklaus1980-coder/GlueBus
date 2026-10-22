// AIFF decoding and sampler project files. Fixtures are generated here and written under build/.
#include "../src/import_check.h"
#include "../src/project.h"
#include "../src/sampler.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>

using namespace mc909;
static int checks = 0, fails = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void be32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back((uint8_t)(v >> (8 * i))); }
static void be16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void add(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A big-endian mono 16-bit AIFF at 44100 Hz with four frames: 0.5, -0.5, 0.25, 0.0
static std::vector<uint8_t> makeAiff(bool truncate = false)
{
    std::vector<uint8_t> comm; be16(comm, 1); be32(comm, 4); be16(comm, 16);
    const uint8_t ext44100[10] = {0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0};    // 44100 as 80-bit extended
    comm.insert(comm.end(), ext44100, ext44100 + 10);
    std::vector<uint8_t> ssnd; be32(ssnd, 0); be32(ssnd, 0);
    const int16_t vals[4] = {16384, -16384, 8192, 0};
    for (int16_t v : vals) be16(ssnd, (uint16_t)v);
    std::vector<uint8_t> b; add(b, "FORM"); be32(b, 4 + 8 + comm.size() + 8 + ssnd.size()); add(b, "AIFF");
    add(b, "COMM"); be32(b, (uint32_t)comm.size()); b.insert(b.end(), comm.begin(), comm.end());
    add(b, "SSND"); be32(b, truncate ? (uint32_t)ssnd.size() + 100 : (uint32_t)ssnd.size()); b.insert(b.end(), ssnd.begin(), ssnd.end());
    if (truncate) b.resize(b.size() - 4);
    return b;
}

static void writeFile(const std::string& path, const std::vector<uint8_t>& b)
{
    std::ofstream f(path, std::ios::binary); f.write((const char*)b.data(), (std::streamsize)b.size());
}

int main()
{
    // AIFF: a valid file is detected and decoded in full, with big-endian samples scaled to -1..1
    {
        auto b = makeAiff();
        CHECK(detect(b) == Kind::Aiff, "AIFF is detected by its FORM/AIFF signature");
        WavInfo info; std::vector<float> d;
        CHECK(decodeAiff(b, info, d) && info.sampleRate == 44100 && info.channels == 1 && info.frames == 4,
              "AIFF rate, channels and frames are read (%u Hz, %u ch, %llu frames)", info.sampleRate, info.channels, (unsigned long long)info.frames);
        CHECK(d.size() == 4 && std::fabs(d[0] - 0.5f) < 1e-4f && std::fabs(d[1] + 0.5f) < 1e-4f && std::fabs(d[2] - 0.25f) < 1e-4f,
              "AIFF samples are big-endian and scaled (%.4f %.4f %.4f)", d[0], d[1], d[2]);
        auto r = analyze(b);
        CHECK(r.kind == Kind::Aiff && r.supported, "a valid AIFF is reported as supported");
    }
    // AIFF truncated: flagged, not decoded
    {
        auto b = makeAiff(true);
        WavInfo info; std::vector<float> d;
        CHECK(!decodeAiff(b, info, d), "a truncated AIFF is not decoded");
        CHECK(!analyze(b).supported, "a truncated AIFF is reported as not supported");
    }
    // project: save and load round trip keeps every field
    {
        std::filesystem::create_directories("build/proj");
        Project p; p.name = "kit one";
        PadEntry e; e.pad = 3; e.file = "build/proj/kick.wav"; e.velLo = 0.25f; e.velHi = 0.75f;
        e.start = 0.1f; e.end = 0.9f; e.semitones = -5.f; e.volume = 0.8f; e.pan = 0.3f;
        e.loop = true; e.reverse = false; e.mute = false; e.choke = 2;
        e.attack = 0.005f; e.decay = 0.1f; e.sustain = 0.6f; e.release = 0.2f;
        p.pads.push_back(e);
        CHECK(saveProject("build/proj/kit.mc909", p), "a project can be saved");
        Project q; std::string err;
        CHECK(loadProject("build/proj/kit.mc909", q, err), "a saved project loads (%s)", err.c_str());
        CHECK(q.name == "kit one" && q.pads.size() == 1, "project name and pad count survive the round trip");
        const PadEntry& r = q.pads[0];
        CHECK(r.pad == 3 && r.file == e.file && r.choke == 2 && r.loop && std::fabs(r.semitones + 5.f) < 1e-4f &&
              std::fabs(r.velHi - 0.75f) < 1e-4f && std::fabs(r.sustain - 0.6f) < 1e-4f, "pad fields survive the round trip");
    }
    // project: a file that is not a project, or a newer version, is refused
    {
        writeFile("build/proj/notproj.mc909", std::vector<uint8_t>{'h', 'e', 'l', 'l', 'o'});
        Project q; std::string err;
        CHECK(!loadProject("build/proj/notproj.mc909", q, err) && err.find("not a sampler project") != std::string::npos, "a file that is not a project is refused");
        writeFile("build/proj/newer.mc909", std::vector<uint8_t>{'M','C','9','0','9','S','A','M','P','L','E','R','-','P','R','O','J','E','C','T',' ','2','\n'});
        CHECK(!loadProject("build/proj/newer.mc909", q, err), "a project from a newer version is refused");
    }
    // building the sampler: a WAV-backed pad plays; a missing file is reported and left empty
    {
        // a 16-bit mono WAV with 100 frames
        std::vector<uint8_t> w = {'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x44,0xAC,0,0, 0x88,0x58,1,0, 2,0, 16,0,
                                  'd','a','t','a', 200,0,0,0};
        for (int i = 0; i < 100; ++i) { int16_t v = (int16_t)(i * 300); w.push_back((uint8_t)v); w.push_back((uint8_t)(v >> 8)); }
        writeFile("build/proj/hit.wav", w);
        Project p;
        PadEntry a; a.pad = 0; a.file = "build/proj/hit.wav"; p.pads.push_back(a);
        PadEntry b; b.pad = 1; b.file = "build/proj/missing.wav"; p.pads.push_back(b);
        Sampler s(44100.0);
        std::vector<std::string> warn;
        buildSampler(p, s, warn);
        CHECK(s.sampleCount() == 1, "the WAV-backed pad loads its sample");
        CHECK(warn.size() == 1 && warn[0].find("file not found") != std::string::npos, "a missing file is reported");
        s.noteOn(0, 1.f);
        float L[10], R[10]; s.render(L, R, 10);
        CHECK(s.activeVoices() == 1, "the loaded pad sounds");
        CHECK(std::filesystem::exists("build/proj/hit.wav") && std::filesystem::file_size("build/proj/hit.wav") == w.size(),
              "the source file is not modified by loading it");
    }
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
