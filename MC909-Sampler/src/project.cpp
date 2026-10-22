#include "project.h"
#include "import_check.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace mc909 {

bool saveProject(const std::string& path, const Project& p)
{
    std::ofstream f(path);
    if (!f) return false;
    f << "MC909SAMPLER-PROJECT 1\n";
    f << "name=" << p.name << "\n";
    for (const auto& e : p.pads)
    {
        f << "[pad " << e.pad << "]\n";
        f << "file=" << e.file << "\n";
        f << "vel=" << e.velLo << " " << e.velHi << "\n";
        f << "window=" << e.start << " " << e.end << "\n";
        f << "tune=" << e.semitones << "\n";
        f << "volume=" << e.volume << "\n";
        f << "pan=" << e.pan << "\n";
        f << "loop=" << (e.loop ? 1 : 0) << "\n";
        f << "reverse=" << (e.reverse ? 1 : 0) << "\n";
        f << "mute=" << (e.mute ? 1 : 0) << "\n";
        f << "choke=" << e.choke << "\n";
        f << "env=" << e.attack << " " << e.decay << " " << e.sustain << " " << e.release << "\n";
    }
    return (bool)f;
}

bool loadProject(const std::string& path, Project& p, std::string& error)
{
    p = Project();
    std::ifstream f(path);
    if (!f) { error = "cannot open " + path; return false; }
    std::string line;
    if (!std::getline(f, line)) { error = "empty file"; return false; }
    if (line.rfind("MC909SAMPLER-PROJECT ", 0) != 0) { error = "not a sampler project file"; return false; }
    if (std::atoi(line.c_str() + 21) != 1) { error = "project version not supported (needs version 1)"; return false; }
    PadEntry* cur = nullptr;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("[pad ", 0) == 0)
        {
            p.pads.push_back(PadEntry());
            cur = &p.pads.back();
            cur->pad = std::atoi(line.c_str() + 5);
            if (cur->pad < 0 || cur->pad > 15) { error = "pad number out of range: " + line; return false; }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        std::istringstream in(v);
        if (!cur) { if (k == "name") p.name = v; continue; }
        if (k == "file") cur->file = v;
        else if (k == "vel") in >> cur->velLo >> cur->velHi;
        else if (k == "window") in >> cur->start >> cur->end;
        else if (k == "tune") cur->semitones = std::strtof(v.c_str(), nullptr);
        else if (k == "volume") cur->volume = std::strtof(v.c_str(), nullptr);
        else if (k == "pan") cur->pan = std::strtof(v.c_str(), nullptr);
        else if (k == "loop") cur->loop = v == "1";
        else if (k == "reverse") cur->reverse = v == "1";
        else if (k == "mute") cur->mute = v == "1";
        else if (k == "choke") cur->choke = std::atoi(v.c_str());
        else if (k == "env") in >> cur->attack >> cur->decay >> cur->sustain >> cur->release;
        // unknown keys are ignored, so later versions can add fields
    }
    return true;
}

void buildSampler(const Project& p, Sampler& s, std::vector<std::string>& warnings)
{
    for (const auto& e : p.pads)
    {
        if (e.file.empty()) continue;
        std::ifstream f(e.file, std::ios::binary);
        if (!f) { warnings.push_back("pad " + std::to_string(e.pad) + ": file not found: " + e.file); continue; }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        WavInfo info; Sample sample;
        if (!decodeAudio(bytes, info, sample.data))
        {
            warnings.push_back("pad " + std::to_string(e.pad) + ": not read in full (unsupported or damaged): " + e.file);
            continue;
        }
        sample.channels = info.channels;
        sample.rate = info.sampleRate;
        Layer l{s.addSample(std::move(sample))};
        l.velLo = e.velLo; l.velHi = e.velHi; l.start = e.start; l.end = e.end;
        s.addLayer(e.pad, l);
        Pad& pad = s.pad(e.pad);
        pad.semitones = e.semitones; pad.volume = e.volume; pad.pan = e.pan;
        pad.loop = e.loop; pad.reverse = e.reverse; pad.mute = e.mute; pad.choke = e.choke;
        pad.attack = e.attack; pad.decay = e.decay; pad.sustain = e.sustain; pad.release = e.release;
    }
}

}  // namespace mc909
