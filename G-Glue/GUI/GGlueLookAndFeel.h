#pragma once
// G-Glue look: dark charcoal hardware, machined knobs, white / grey print. All drawing is vector, so the GUI scales.
#include <juce_gui_basics/juce_gui_basics.h>

namespace gglue::gui
{
namespace colours
{
const juce::Colour plateTop    { 0xff2c2d31 };
const juce::Colour plateBottom { 0xff1b1c1f };
const juce::Colour print       { 0xffe9e9ec };
const juce::Colour printDim    { 0xff9a9ca3 };
const juce::Colour accent      { 0xffe2a03a };     // amber LED
const juce::Colour red         { 0xffe0412f };
const juce::Colour meterFace   { 0xfff1ead8 };
const juce::Colour meterInk    { 0xff1d1d1f };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override;
};

// a hardware-style button with a vector icon (no font glyphs needed)
class IconButton : public juce::TextButton
{
public:
    enum Icon { Left, Right, Star };
    Icon icon = Left;
    bool filled = false;
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

juce::Font labelFont (float height, bool bold = true);
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
juce::Image makePlateTexture (int w, int h, juce::uint32 seed);   // brushed-metal noise, generated once
} // namespace gglue::gui
