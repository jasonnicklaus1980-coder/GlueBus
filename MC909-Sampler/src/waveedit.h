// Non-destructive waveform editing: every operation returns a new sample. The source sample is never changed.
#pragma once
#include "sampler.h"

#include <vector>

namespace mc909 {

struct Peak { float min, max; };

// A copy of the sample from frame `from` to frame `to` (exclusive), clamped to the sample.
Sample trimmed(const Sample& s, uint64_t from, uint64_t to);

// A copy with its peak scaled to `peak` (0..1). A silent sample is returned unchanged.
Sample normalized(const Sample& s, float peak = 1.f);

// Min and max of the first channel in `bins` equal parts, for drawing a waveform.
std::vector<Peak> waveformPeaks(const Sample& s, int bins);

}  // namespace mc909
