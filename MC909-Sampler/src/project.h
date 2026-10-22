// Sampler project files: a plain-text format of our own. Each pad refers to its source file by path; the source
// is read, never written. Processed copies are not made.
#pragma once
#include "sampler.h"

#include <string>
#include <vector>

namespace mc909 {

struct PadEntry {
    int pad = 0;                       // 0..15
    std::string file;                  // source file (WAV or AIFF)
    float velLo = 0.f, velHi = 1.f;    // velocity range for this layer
    float start = 0.f, end = 1.f;
    float semitones = 0.f, volume = 1.f, pan = 0.5f;
    bool loop = false, reverse = false, mute = false;
    int choke = 0;
    float attack = 0.001f, decay = 0.f, sustain = 1.f, release = 0.01f;
};

struct Project {
    std::string name;
    std::vector<PadEntry> pads;
};

// Writes the project. Returns false if the file cannot be written.
bool saveProject(const std::string& path, const Project& p);

// Reads a project. Returns false with an error message for a missing file, a bad header, or a newer version.
bool loadProject(const std::string& path, Project& p, std::string& error);

// Loads every pad's source file into the sampler. Missing or undecodable files are reported in warnings and their
// pads are left empty; nothing is changed on disk.
void buildSampler(const Project& p, Sampler& s, std::vector<std::string>& warnings);

}  // namespace mc909
