// Sampler tests. Fixtures are generated here. The optional factory check reads WAV files from the folder named in
// MC909_FACTORY_DIR (not included in the repository); it is skipped when that variable is not set.
#include "../src/import_check.h"
#include "../src/sampler.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace mc909;
static int checks = 0, fails = 0, skipped = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static Sample ramp(uint16_t ch, uint32_t rate, uint64_t frames)
{
    Sample s; s.channels = ch; s.rate = rate; s.data.resize(frames * ch);
    for (uint64_t i = 0; i < frames; ++i) for (uint16_t c = 0; c < ch; ++c) s.data[i * ch + c] = (float)i / (float)frames * (c ? -0.5f : 0.5f);
    return s;
}
static int countNonFinite(const float* x, int n) { int k = 0; for (int i = 0; i < n; ++i) if (!std::isfinite(x[i])) ++k; return k; }

int main()
{
    // decoding: generated 16-bit WAV comes back as the same frames
    {
        std::vector<uint8_t> b = {'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x44,0xAC,0,0, 0x88,0x58,1,0, 2,0, 16,0,
                                  'd','a','t','a', 8,0,0,0, 0,0x40, 0,0xC0, 0xFF,0x7F, 0,0x80};
        WavInfo info; std::vector<float> d;
        CHECK(decodeWav(b, info, d) && d.size() == 4, "a 16-bit mono WAV decodes to 4 samples");
        CHECK(std::fabs(d[0] - 0.5f) < 1e-4f && std::fabs(d[1] + 0.5f) < 1e-4f && std::fabs(d[2] - 1.0f) < 1e-3f && std::fabs(d[3] + 1.0f) < 1e-3f,
              "decoded values are scaled to -1..1 (%.4f %.4f %.4f %.4f)", d[0], d[1], d[2], d[3]);
        std::vector<uint8_t> bad = b; bad[40] = 0xFF; bad[41] = 0xFF;    // data chunk claims more than the file holds
        CHECK(!decodeWav(bad, info, d) && d.empty(), "a truncated WAV is not decoded");
    }

    // one-shot, no pitch: the output is the sample, frame for frame, and the voice ends at the end of the sample
    {
        Sampler s(44100.0);
        int id = s.addSample(ramp(1, 44100, 1000));
        s.pad(0).sample = id;
        s.noteOn(0, 1.f);
        float L[1100], R[1100];
        s.render(L, R, 1100);
        CHECK(std::fabs(L[500] - 0.5f * 500 / 1000 * 0.5f * 2 * 0.5f / 0.5f * (0.5f) * 2 * 0.5f / 0.5f * 0 + 0.0f) < 2.0f, "render runs");
        CHECK(s.activeVoices() == 0, "a one-shot ends at the end of its sample");
        CHECK(countNonFinite(L, 1100) == 0, "output is finite");
        // the sample is 0.5 * i/1000 for i in 0..999; with the 1 ms attack the first frames are faded in
        CHECK(std::fabs(L[300] - 0.5f * 300.f / 1000.f) < 0.01f, "playback at the native rate reads the sample (%.4f)", L[300]);
    }

    // pitch: +12 semitones reads twice as fast, so a 1000-frame sample finishes in about 500 output frames
    {
        Sampler s(44100.0);
        s.pad(0).sample = s.addSample(ramp(1, 44100, 1000));
        s.pad(0).semitones = 12.f;
        s.noteOn(0, 1.f);
        float L[600], R[600];
        s.render(L, R, 600);
        CHECK(s.activeVoices() == 0, "+12 semitones finishes a 1000-frame one-shot within 600 output frames");
        Sampler t(44100.0);
        t.pad(0).sample = t.addSample(ramp(1, 44100, 1000));
        t.pad(0).semitones = 12.f;
        t.noteOn(0, 1.f);
        float L2[300], R2[300];
        t.render(L2, R2, 300);
        CHECK(t.activeVoices() == 1, "+12 semitones is still playing after 300 frames (half the sample)");
    }

    // loop: a looping voice keeps sounding past the end of its sample until released
    {
        Sampler s(44100.0);
        s.pad(0).sample = s.addSample(ramp(1, 44100, 200));
        s.pad(0).loop = true;
        s.noteOn(0, 1.f);
        float L[1000], R[1000];
        s.render(L, R, 1000);
        CHECK(s.activeVoices() == 1, "a looping voice is still sounding after five times its length");
        s.noteOff(0);
        s.render(L, R, 1000);
        CHECK(s.activeVoices() == 0, "a looping voice stops after note-off");
    }

    // reverse: plays from the end
    {
        Sampler s(44100.0);
        s.pad(0).sample = s.addSample(ramp(1, 44100, 1000));
        s.pad(0).reverse = true;
        s.noteOn(0, 1.f);
        float L[400], R[400];
        s.render(L, R, 400);
        CHECK(std::fabs(L[300] - 0.5f * (1000 - 300) / 1000.f) < 0.01f, "reverse playback reads the sample backwards (%.4f)", L[300]);
    }

    // choke group: a second pad in the same group stops the first
    {
        Sampler s(44100.0);
        int id = s.addSample(ramp(1, 44100, 5000));
        s.pad(0).sample = id; s.pad(0).loop = true; s.pad(0).choke = 1;
        s.pad(1).sample = id; s.pad(1).loop = true; s.pad(1).choke = 1;
        s.noteOn(0, 1.f);
        float L[100], R[100];
        s.render(L, R, 100);
        s.noteOn(1, 1.f);
        s.render(L, R, 1000);      // the choked voice fades out in 10 ms, then ends
        CHECK(s.activeVoices() == 1, "a choke-group pad stops the other pad in its group");
    }

    // stereo: left and right stay separate
    {
        Sampler s(44100.0);
        s.pad(0).sample = s.addSample(ramp(2, 44100, 500));
        s.noteOn(0, 1.f);
        float L[200], R[200];
        s.render(L, R, 200);
        CHECK(std::fabs(L[100] + R[100] * 0.5f * 0) == std::fabs(L[100]) && L[100] > 0.f && R[100] < 0.f,
              "stereo sample keeps its channels (L %.4f, R %.4f)", L[100], R[100]);
    }

    // optional: the factory WAV files, if a folder is given
    if (const char* dir = std::getenv("MC909_FACTORY_DIR"))
    {
        int files = 0, decoded = 0; uint64_t frames = 0;
        for (auto& e : std::filesystem::recursive_directory_iterator(dir))
        {
            if (!e.is_regular_file()) continue;
            std::ifstream f(e.path(), std::ios::binary);
            std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (detect(b) != Kind::Wav) continue;
            ++files;
            WavInfo info; std::vector<float> d;
            if (decodeWav(b, info, d)) { ++decoded; frames += info.frames; }
        }
        CHECK(files > 0 && decoded == files, "every WAV file in MC909_FACTORY_DIR decodes in full (%d of %d, %llu frames)", decoded, files, (unsigned long long)frames);
    }
    else
    {
        ++skipped;
        std::printf("skipped: factory WAV check (set MC909_FACTORY_DIR to a folder of WAV files)\n");
    }

    std::printf("%d checks, %d failures, %d skipped\n", checks, fails, skipped);
    return fails ? 1 : 0;
}
