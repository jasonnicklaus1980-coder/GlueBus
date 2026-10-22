#include "import_check.h"
#include "mathlite.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace mc909 {
namespace {

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
bool tag(const std::vector<uint8_t>& b, size_t at, const char* t) { return at + 4 <= b.size() && std::memcmp(&b[at], t, 4) == 0; }

WavInfo parseWav(const std::vector<uint8_t>& b, std::vector<std::string>& warn)
{
    WavInfo w;
    if (b.size() < 12 || !tag(b, 0, "RIFF") || !tag(b, 8, "WAVE")) { warn.push_back("not a RIFF/WAVE file"); return w; }
    bool haveFmt = false, haveData = false;
    uint32_t dataSize = 0; size_t dataAt = 0;
    size_t p = 12;
    while (p + 8 <= b.size())
    {
        uint32_t sz = le32(&b[p + 4]);
        size_t body = p + 8;
        if (tag(b, p, "fmt "))
        {
            if (body + 16 > b.size()) { warn.push_back("fmt chunk truncated"); return w; }
            uint16_t code = le16(&b[body]);
            w.channels = le16(&b[body + 2]);
            w.sampleRate = le32(&b[body + 4]);
            uint16_t blockAlign = le16(&b[body + 12]);
            w.bitsPerSample = le16(&b[body + 14]);
            if (code == 0xFFFE && sz >= 40 && body + 26 <= b.size()) code = le16(&b[body + 24]);   // WAVE_FORMAT_EXTENSIBLE
            if (code == 1) w.encoding = "PCM";
            else if (code == 3) w.encoding = "float";
            else { warn.push_back("unsupported WAV encoding code " + std::to_string(code)); return w; }
            if (w.channels == 0 || w.sampleRate == 0) { warn.push_back("fmt chunk has zero channels or rate"); return w; }
            bool bitsOk = w.encoding == "PCM" ? (w.bitsPerSample == 8 || w.bitsPerSample == 16 || w.bitsPerSample == 24 || w.bitsPerSample == 32)
                                              : (w.bitsPerSample == 32 || w.bitsPerSample == 64);
            if (!bitsOk) { warn.push_back("unsupported sample size " + std::to_string(w.bitsPerSample) + " bits"); return w; }
            if (blockAlign != w.channels * (w.bitsPerSample / 8)) warn.push_back("block align does not match channels and sample size");
            haveFmt = true;
        }
        else if (tag(b, p, "data"))
        {
            haveData = true; dataAt = body; dataSize = sz;
            if (body + sz > b.size())
            {
                warn.push_back("data chunk is truncated: declares " + std::to_string(sz) + " bytes, file has " + std::to_string(b.size() - body));
                dataSize = (uint32_t)(b.size() - body);
            }
        }
        size_t next = body + sz + (sz & 1);
        if (sz > b.size() || next <= p) break;
        p = next;
    }
    if (!haveFmt) { warn.push_back("no fmt chunk"); return w; }
    if (!haveData) { warn.push_back("no data chunk"); return w; }
    uint32_t frameBytes = w.channels * (w.bitsPerSample / 8);
    if (dataSize % frameBytes != 0) warn.push_back("data length is not a whole number of frames");
    w.frames = dataSize / frameBytes;
    (void)dataAt;
    w.ok = true;
    return w;
}

// VLQ read; returns false on overrun.
bool vlq(const std::vector<uint8_t>& b, size_t& p, size_t end, uint32_t& out)
{
    out = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p >= end) return false;
        uint8_t c = b[p++];
        out = out << 7 | (c & 0x7F);
        if (!(c & 0x80)) return true;
    }
    return false;
}

MidiInfo parseMidi(const std::vector<uint8_t>& b, std::vector<std::string>& warn)
{
    MidiInfo m;
    if (b.size() < 14 || !tag(b, 0, "MThd") || be32(&b[4]) != 6) { warn.push_back("MThd header missing or wrong length"); return m; }
    m.format = be16(&b[8]); m.tracks = be16(&b[10]); m.division = be16(&b[12]);
    if (m.format > 2) warn.push_back("unknown MIDI file format " + std::to_string(m.format));
    if (m.division & 0x8000) { warn.push_back("SMPTE time division is not supported for tempo"); }
    size_t p = 14; int trackNo = 0;
    while (p + 8 <= b.size() && trackNo < m.tracks)
    {
        if (!tag(b, p, "MTrk")) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": missing MTrk at byte " + std::to_string(p)); break; }
        uint32_t len = be32(&b[p + 4]);
        size_t start = p + 8, end = start + len;
        if (end > b.size()) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": declared length runs past the end of the file"); end = b.size(); }
        size_t q = start; uint8_t status = 0; bool sawEnd = false;
        uint64_t tick = 0;
        const size_t firstNote = m.notes.size();
        while (q < end)
        {
            uint32_t delta; if (!vlq(b, q, end, delta)) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": event delta cut off"); break; }
            if (q >= end) break;
            tick += delta;
            uint8_t c = b[q];
            if (c == 0xFF)                                    // meta event
            {
                if (q + 2 > end) break;
                uint8_t type = b[q + 1]; q += 2;
                uint32_t n; if (!vlq(b, q, end, n) || q + n > end) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": meta event cut off"); break; }
                if (type == 0x51 && n == 3 && m.firstTempoBpm == 0)
                {
                    uint32_t us = (uint32_t)b[q] << 16 | (uint32_t)b[q + 1] << 8 | b[q + 2];
                    if (us) m.firstTempoBpm = 60000000.0 / us;
                }
                if (type == 0x2F) { sawEnd = true; q += n; break; }
                q += n; continue;
            }
            if (c == 0xF0 || c == 0xF7)                       // sysex
            {
                q++; uint32_t n; if (!vlq(b, q, end, n) || q + n > end) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": sysex cut off"); break; }
                q += n; continue;
            }
            if (c & 0x80) { status = c; q++; }               // new status; otherwise running status
            else if (status == 0) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": data byte without status"); break; }
            uint8_t kind = status & 0xF0;
            int dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if (q + dataBytes > end) { m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": channel event cut off"); break; }
            if (kind == 0x90 && b[q + 1] > 0)
            {
                m.noteOns++;
                int note = b[q];
                if (note < m.minNote) m.minNote = note;
                if (note > m.maxNote) m.maxNote = note;
                MidiNote n; n.tick = tick; n.channel = status & 0x0F; n.note = (uint8_t)note; n.velocity = b[q + 1];
                n.duration = UINT64_MAX;                       // open until its note-off
                m.notes.push_back(n);
            }
            else if (kind == 0x80 || (kind == 0x90 && b[q + 1] == 0))
            {
                for (size_t k = m.notes.size(); k-- > firstNote;)
                {
                    MidiNote& n = m.notes[k];
                    if (n.duration == UINT64_MAX && n.note == b[q] && n.channel == (status & 0x0F))
                    {
                        n.duration = tick - n.tick;
                        break;
                    }
                }
            }
            q += dataBytes;
        }
        if (!sawEnd) m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": no end-of-track event");
        for (size_t k = firstNote; k < m.notes.size(); ++k)
            if (m.notes[k].duration == UINT64_MAX)
            {
                m.notes[k].duration = 0;
                m.trackProblems.push_back("track " + std::to_string(trackNo + 1) + ": a note has no note-off");
            }
        p = end; trackNo++;
    }
    if (trackNo != m.tracks) m.trackProblems.push_back("header declares " + std::to_string(m.tracks) + " tracks, found " + std::to_string(trackNo));
    m.ok = m.trackProblems.empty();
    return m;
}

}  // namespace

Kind detect(const std::vector<uint8_t>& b)
{
    if (b.size() >= 12 && tag(b, 0, "RIFF") && tag(b, 8, "WAVE")) return Kind::Wav;
    if (b.size() >= 12 && tag(b, 0, "FORM") && tag(b, 8, "AIFF")) return Kind::Aiff;
    if (b.size() >= 4 && tag(b, 0, "MThd")) return Kind::Midi;
    return Kind::Unknown;
}

std::string hexHead(const std::vector<uint8_t>& b, size_t n)
{
    std::string s;
    char t[4];
    for (size_t i = 0; i < n && i < b.size(); ++i) { std::snprintf(t, sizeof t, "%02X ", b[i]); s += t; }
    return s;
}

// IEEE 80-bit extended, as used for the AIFF sample rate.
static double readExtended80(const uint8_t* p)
{
    const int expo = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 2; i < 10; ++i) mant = mant << 8 | p[i];
    if (expo == 0 && mant == 0) return 0.0;
    double v = ldexpd((double)mant, expo - 16383 - 63);
    return (p[0] & 0x80) ? -v : v;
}

bool decodeAiff(const std::vector<uint8_t>& b, WavInfo& info, std::vector<float>& out)
{
    out.clear();
    info = WavInfo();
    if (b.size() < 12 || !tag(b, 0, "FORM") || !tag(b, 8, "AIFF")) return false;
    bool haveComm = false, haveSound = false;
    size_t soundAt = 0;
    size_t p = 12;
    while (p + 8 <= b.size())
    {
        uint32_t sz = be32(&b[p + 4]);
        size_t body = p + 8;
        if (tag(b, p, "COMM"))
        {
            if (body + 18 > b.size()) return false;
            info.channels = be16(&b[body]);
            info.frames = be32(&b[body + 2]);
            info.bitsPerSample = be16(&b[body + 6]);
            info.sampleRate = (uint32_t)readExtended80(&b[body + 8]);
            info.encoding = "PCM";
            haveComm = true;
        }
        else if (tag(b, p, "SSND"))
        {
            if (body + 8 > b.size()) return false;
            uint32_t offset = be32(&b[body]);
            soundAt = body + 8 + offset;
            haveSound = true;
        }
        size_t next = body + sz + (sz & 1);
        if (next <= p) break;
        p = next;
    }
    if (!haveComm || !haveSound || info.channels == 0 || info.sampleRate == 0) return false;
    if (info.bitsPerSample != 8 && info.bitsPerSample != 16 && info.bitsPerSample != 24 && info.bitsPerSample != 32) return false;
    const int bytes = info.bitsPerSample / 8;
    if (soundAt + (uint64_t)info.frames * info.channels * bytes > b.size()) return false;   // truncated: not read in full
    const uint64_t n = (uint64_t)info.frames * info.channels;
    out.resize((size_t)n);
    const uint8_t* d = &b[soundAt];
    for (uint64_t i = 0; i < n; ++i)
    {
        const uint8_t* s = d + i * bytes;
        float v;
        if (bytes == 1) v = (int8_t)s[0] / 128.f;
        else if (bytes == 2) v = (int16_t)(s[0] << 8 | s[1]) / 32768.f;
        else if (bytes == 3) { int32_t x = (int32_t)((uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8) >> 8; v = x / 8388608.f; }
        else v = (int32_t)be32(s) / 2147483648.f;
        out[(size_t)i] = v;
    }
    info.ok = true;
    return true;
}

bool decodeAudio(const std::vector<uint8_t>& b, WavInfo& info, std::vector<float>& out)
{
    switch (detect(b))
    {
    case Kind::Wav: return decodeWav(b, info, out);
    case Kind::Aiff: return decodeAiff(b, info, out);
    default: out.clear(); info = WavInfo(); return false;
    }
}

bool decodeWav(const std::vector<uint8_t>& b, WavInfo& info, std::vector<float>& out)
{
    out.clear();
    std::vector<std::string> warn;
    info = parseWav(b, warn);
    if (!info.ok || !warn.empty()) return false;
    // find the data chunk again (parseWav does not keep its position)
    size_t p = 12, dataAt = 0; uint32_t dataSize = 0;
    while (p + 8 <= b.size())
    {
        uint32_t sz = le32(&b[p + 4]);
        if (tag(b, p, "data")) { dataAt = p + 8; dataSize = sz; break; }
        size_t next = p + 8 + sz + (sz & 1);
        if (next <= p) break;
        p = next;
    }
    if (dataAt == 0 || dataAt + dataSize > b.size()) return false;
    const int bytes = info.bitsPerSample / 8;
    const uint64_t n = (uint64_t)info.frames * info.channels;
    out.resize((size_t)n);
    const uint8_t* d = &b[dataAt];
    for (uint64_t i = 0; i < n; ++i)
    {
        const uint8_t* s = d + i * bytes;
        float v = 0.f;
        if (info.encoding == "float")
        {
            if (bytes == 4) { uint32_t u = le32(s); float f; std::memcpy(&f, &u, 4); v = f; }
            else { uint64_t u = 0; for (int k = 7; k >= 0; --k) u = u << 8 | s[k]; double f; std::memcpy(&f, &u, 8); v = (float)f; }
        }
        else if (bytes == 1) v = ((int)s[0] - 128) / 128.f;
        else if (bytes == 2) v = (int16_t)le16(s) / 32768.f;
        else if (bytes == 3) { int32_t x = (int32_t)(s[0] << 8 | s[1] << 16 | (uint32_t)s[2] << 24) >> 8; v = x / 8388608.f; }
        else { v = (int32_t)le32(s) / 2147483648.f; }
        out[(size_t)i] = v;
    }
    return true;
}

Report analyze(const std::vector<uint8_t>& b)
{
    Report r;
    r.kind = detect(b);
    if (r.kind == Kind::Aiff)
    {
        std::vector<float> samples;
        r.supported = decodeAiff(b, r.wav, samples);
        if (!r.supported)
        {
            r.warnings.push_back("AIFF not read in full: compressed (AIFC), truncated, or an unsupported sample size");
            r.summary = "AIFF (not decoded)";
        }
        else
            r.summary = std::to_string(r.wav.channels) + " ch, " + std::to_string(r.wav.sampleRate) + " Hz, " +
                        std::to_string(r.wav.bitsPerSample) + "-bit AIFF, " + std::to_string(r.wav.frames) + " frames";
    }
    else if (r.kind == Kind::Wav)
    {
        r.wav = parseWav(b, r.warnings);
        r.supported = r.wav.ok && r.warnings.empty();
        r.summary = r.wav.ok ? std::to_string(r.wav.channels) + " ch, " + std::to_string(r.wav.sampleRate) + " Hz, " +
                               std::to_string(r.wav.bitsPerSample) + "-bit " + r.wav.encoding + ", " + std::to_string(r.wav.frames) + " frames"
                             : "WAV (not decoded)";
    }
    else if (r.kind == Kind::Midi)
    {
        r.midi = parseMidi(b, r.warnings);
        for (auto& p : r.midi.trackProblems) r.warnings.push_back(p);
        r.supported = r.midi.ok && r.warnings.empty();
        r.summary = "MIDI format " + std::to_string(r.midi.format) + ", " + std::to_string(r.midi.tracks) + " tracks, " +
                    std::to_string(r.midi.noteOns) + " note-ons" +
                    (r.midi.firstTempoBpm > 0 ? ", tempo " + std::to_string((int)(r.midi.firstTempoBpm + 0.5)) + " BPM" : std::string(", no tempo event"));
    }
    else
    {
        r.summary = "unknown file type; first bytes " + hexHead(b);
        r.warnings.push_back("not a supported format: the file was not read and nothing was guessed from it");
    }
    return r;
}

}  // namespace mc909
