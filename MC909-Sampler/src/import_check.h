// File detection and structure checks for the import engine. Only documented formats are decoded here:
// RIFF/WAVE audio and Standard MIDI Files. Anything else is reported as unsupported, never guessed at.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mc909 {

enum class Kind { Wav, Aiff, Midi, Unknown };

struct WavInfo {
    bool ok = false;
    uint16_t channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    uint64_t frames = 0;
    std::string encoding;        // "PCM" or "float"
};

struct MidiInfo {
    bool ok = false;
    uint16_t format = 0, tracks = 0, division = 0;
    uint64_t noteOns = 0;
    int minNote = 127, maxNote = 0;
    double firstTempoBpm = 0;    // 0 when no tempo event is present
    std::vector<std::string> trackProblems;
};

struct Report {
    Kind kind = Kind::Unknown;
    std::string summary;
    std::vector<std::string> warnings;   // problems that do not stop the file being read
    bool supported = false;              // true only for a file decoded in full
    WavInfo wav;
    MidiInfo midi;
};

Kind detect(const std::vector<uint8_t>& bytes);
Report analyze(const std::vector<uint8_t>& bytes);

// Decodes a WAV file to interleaved float samples in -1..1. Returns false (and leaves out empty) unless the file is
// read in full. Only the documented PCM and float encodings are converted.
bool decodeWav(const std::vector<uint8_t>& bytes, WavInfo& info, std::vector<float>& out);

// Decodes an uncompressed AIFF file (IFF FORM/AIFF, big-endian PCM 8/16/24/32-bit) the same way. AIFC files with
// compression are not decoded.
bool decodeAiff(const std::vector<uint8_t>& bytes, WavInfo& info, std::vector<float>& out);

// Decodes whichever audio format the file is (WAV or AIFF).
bool decodeAudio(const std::vector<uint8_t>& bytes, WavInfo& info, std::vector<float>& out);
std::string hexHead(const std::vector<uint8_t>& bytes, size_t n = 16);

}  // namespace mc909
