// Import-check tests. Every fixture is generated here, so no third-party files are needed.
#include "../src/import_check.h"

#include <cstdio>
#include <cstring>

using namespace mc909;
static int checks = 0, fails = 0;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++fails; std::printf("FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void put32le(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
static void put16le(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static void put32be(std::vector<uint8_t>& b, uint32_t v) { for (int i = 3; i >= 0; --i) b.push_back((uint8_t)(v >> (8 * i))); }
static void put16be(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)(v >> 8)); b.push_back((uint8_t)v); }
static void tagAt(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// A PCM WAV: channels, rate, bits, number of frames. Samples are a 1 kHz ramp.
static std::vector<uint8_t> makeWav(uint16_t ch, uint32_t rate, uint16_t bits, uint32_t frames, bool truncate = false)
{
    std::vector<uint8_t> d; uint16_t bytes = bits / 8; uint32_t dataBytes = frames * ch * bytes;
    for (uint32_t i = 0; i < frames * ch; ++i) for (int k = 0; k < bytes; ++k) d.push_back((uint8_t)(i >> (8 * k)));
    std::vector<uint8_t> b; tagAt(b, "RIFF"); put32le(b, 4 + 24 + 8 + (uint32_t)d.size()); tagAt(b, "WAVE");
    tagAt(b, "fmt "); put32le(b, 16); put16le(b, 1); put16le(b, ch); put32le(b, rate);
    put32le(b, rate * ch * bytes); put16le(b, ch * bytes); put16le(b, bits);
    tagAt(b, "data"); put32le(b, truncate ? dataBytes + 100 : dataBytes); b.insert(b.end(), d.begin(), d.end());
    return b;
}

// A one-track MIDI file: a tempo event (120 BPM), note-ons of the given notes, and an end-of-track event.
static std::vector<uint8_t> makeMidi(const std::vector<int>& notes, bool endTrack = true)
{
    std::vector<uint8_t> t;
    t.insert(t.end(), {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});    // tempo 500000 us = 120 BPM
    for (int n : notes) t.insert(t.end(), {0x00, 0x90, (uint8_t)n, 0x64});
    if (endTrack) t.insert(t.end(), {0x00, 0xFF, 0x2F, 0x00});
    std::vector<uint8_t> b; tagAt(b, "MThd"); put32be(b, 6); put16be(b, 0); put16be(b, 1); put16be(b, 96);
    tagAt(b, "MTrk"); put32be(b, (uint32_t)t.size()); b.insert(b.end(), t.begin(), t.end());
    return b;
}

int main()
{
    // WAV: a valid stereo 16-bit file is read in full
    {
        auto r = analyze(makeWav(2, 44100, 16, 1000));
        CHECK(r.kind == Kind::Wav && r.supported, "valid stereo 16-bit WAV is supported");
        CHECK(r.wav.channels == 2 && r.wav.sampleRate == 44100 && r.wav.bitsPerSample == 16 && r.wav.frames == 1000,
              "WAV channels, rate, depth and frame count are read (%u ch, %u Hz, %u bit, %llu frames)", r.wav.channels, r.wav.sampleRate, r.wav.bitsPerSample, (unsigned long long)r.wav.frames);
    }
    // 24-bit mono at 48 kHz
    {
        auto r = analyze(makeWav(1, 48000, 24, 480));
        CHECK(r.supported && r.wav.frames == 480 && r.wav.bitsPerSample == 24, "mono 24-bit 48 kHz WAV is supported");
    }
    // a WAV whose data chunk runs past the end of the file is flagged, and the readable part is kept
    {
        auto r = analyze(makeWav(1, 44100, 16, 500, true));
        CHECK(!r.supported && r.wav.ok && r.wav.frames == 500, "truncated WAV is flagged and the readable frames are reported");
        CHECK(!r.warnings.empty() && r.warnings[0].find("truncated") != std::string::npos, "truncation warning names the problem");
    }
    // a WAV with an unsupported sample size is not decoded
    {
        auto r = analyze(makeWav(1, 44100, 12, 100));
        CHECK(!r.supported && !r.wav.ok, "12-bit WAV is not decoded (unsupported sample size)");
    }
    // MIDI: a valid file is read, with its notes and tempo
    {
        auto r = analyze(makeMidi({60, 64, 67}));
        CHECK(r.kind == Kind::Midi && r.supported, "valid single-track MIDI is supported");
        CHECK(r.midi.noteOns == 3 && r.midi.minNote == 60 && r.midi.maxNote == 67, "MIDI note count and range are read");
        CHECK(r.midi.firstTempoBpm > 119.9 && r.midi.firstTempoBpm < 120.1, "MIDI tempo is read (%.2f BPM)", r.midi.firstTempoBpm);
    }
    // a MIDI track without an end-of-track event is flagged
    {
        auto r = analyze(makeMidi({60}, false));
        CHECK(!r.supported && !r.warnings.empty(), "MIDI track with no end-of-track is flagged");
    }
    // files that are neither WAV nor MIDI are unsupported and nothing is decoded
    {
        std::vector<uint8_t> junk = {'M', 'C', '9', '0', 0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE};
        auto r = analyze(junk);
        CHECK(r.kind == Kind::Unknown && !r.supported, "unknown file type is unsupported");
        CHECK(r.summary.find("4D 43 39 30") != std::string::npos, "unknown file reports its first bytes");
    }
    // an empty file is unsupported
    {
        auto r = analyze({});
        CHECK(r.kind == Kind::Unknown && !r.supported, "empty file is unsupported");
    }
    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
