#pragma once
// Meters for the G-Glue faceplate: an analog gain-reduction meter (needle) and vertical LED level meters.
// Values are pushed in from the editor's timer (message thread); nothing here touches the audio thread.
#include "GGlueLookAndFeel.h"
#include "NeedleBallistics.h"

namespace gglue::gui
{
class GainReductionMeter : public juce::Component
{
public:
    void setNeedle (double grDb) { if (grDb != needleDb) { needleDb = grDb; repaint(); } }
    void paint (juce::Graphics&) override;
    void resized() override { face = {}; }
private:
    juce::Image face;                 // cached dial (everything but the needle), at the current pixel scale
    float faceScale = 0.f;
    double needleDb = 0.0;
    void renderFace (float scale);
};

class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String caption) : label (std::move (caption)) {}
    void setLevel (float db) { if (std::abs (db - levelDb) > 0.05f) { levelDb = db; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    juce::String label;
    float levelDb = -120.f;
};
} // namespace gglue::gui
