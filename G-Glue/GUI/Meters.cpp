#include "Meters.h"

namespace gglue::gui
{
namespace
{
constexpr float kArcStart = -0.86f, kArcEnd = 0.86f;   // radians from vertical, left and right end of the scale
}

void GainReductionMeter::renderFace (float scale)
{
    const int w = getWidth(), h = getHeight();
    faceScale = scale;
    face = juce::Image (juce::Image::ARGB, juce::jmax (1, (int) (w * scale)), juce::jmax (1, (int) (h * scale)), true);
    juce::Graphics g (face);
    g.addTransform (juce::AffineTransform::scale (scale));
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);

    // bezel
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, 2.f), 8.f);
    juce::ColourGradient bezel (juce::Colour (0xff4a4c52), 0, 0, juce::Colour (0xff15161a), 0, (float) h, false);
    g.setGradientFill (bezel);
    g.fillRoundedRectangle (r, 8.f);
    auto inner = r.reduced (7.f);
    // dial face: warm off-white with a soft vignette
    juce::ColourGradient dial (colours::meterFace, inner.getCentreX(), inner.getY() + inner.getHeight() * 0.35f,
                               colours::meterFace.darker (0.28f), inner.getX(), inner.getBottom(), true);
    g.setGradientFill (dial);
    g.fillRoundedRectangle (inner, 4.f);

    const auto pivot = juce::Point<float> (inner.getCentreX(), inner.getBottom() + inner.getHeight() * 0.18f);
    const float rScale = inner.getHeight() * 0.92f;
    auto at = [&] (double pos, float radius) {
        const float a = kArcStart + (float) pos * (kArcEnd - kArcStart);
        return juce::Point<float> (pivot.x + radius * std::sin (a), pivot.y - radius * std::cos (a));
    };
    // scale arc, red zone beyond 10 dB
    juce::Path arc, red;
    arc.addCentredArc (pivot.x, pivot.y, rScale, rScale, 0.f, kArcStart, kArcEnd, true);
    const float a10 = kArcStart + (float) GrScale::position (10.0) * (kArcEnd - kArcStart);
    red.addCentredArc (pivot.x, pivot.y, rScale - 2.f, rScale - 2.f, 0.f, kArcStart, a10, true);
    g.setColour (colours::meterInk);
    g.strokePath (arc, juce::PathStrokeType (1.4f));
    g.setColour (colours::red.withAlpha (0.85f));
    g.strokePath (red, juce::PathStrokeType (3.5f));
    // minor ticks every 1 dB to 10, then at 12.5, 17.5
    g.setColour (colours::meterInk);
    for (double d : { 4.0, 6.0, 8.0, 9.0, 12.5, 17.5 })
        g.drawLine (juce::Line<float> (at (GrScale::position (d), rScale), at (GrScale::position (d), rScale - 6.f)), 1.f);
    // labelled marks
    g.setFont (labelFont (juce::jmax (9.f, inner.getHeight() * 0.1f)));
    for (int i = 0; i < GrScale::kMarks; ++i)
    {
        const double pos = GrScale::kPos[i];
        g.drawLine (juce::Line<float> (at (pos, rScale + 1.f), at (pos, rScale - 10.f)), 1.6f);
        const auto t = at (pos, rScale + inner.getHeight() * 0.11f);
        const juce::String txt = i == 0 ? "0" : juce::String ((int) GrScale::kDb[i]);
        g.drawText (txt, juce::Rectangle<float> (t.x - 14, t.y - 7, 28, 14), juce::Justification::centred);
    }
    g.setFont (labelFont (juce::jmax (8.f, inner.getHeight() * 0.085f)));
    g.drawText ("GAIN REDUCTION  dB", inner.withTrimmedTop (inner.getHeight() * 0.58f).withHeight (inner.getHeight() * 0.14f), juce::Justification::centred);
    g.setColour (colours::meterInk.withAlpha (0.55f));
    g.setFont (labelFont (juce::jmax (7.f, inner.getHeight() * 0.07f), false));
    g.drawText ("G-GLUE", inner.withTrimmedTop (inner.getHeight() * 0.72f).withHeight (inner.getHeight() * 0.12f), juce::Justification::centred);
    // pivot cover
    g.setColour (juce::Colour (0xff1c1c1e));
    juce::Path cover;
    cover.addRectangle (inner.getX(), inner.getBottom() - inner.getHeight() * 0.13f, inner.getWidth(), inner.getHeight() * 0.13f);
    g.saveState();
    g.reduceClipRegion (inner.toNearestInt());
    g.fillPath (cover);
    g.restoreState();
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const float scale = juce::jlimit (1.f, 4.f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (face.isNull() || std::abs (scale - faceScale) > 0.01f) renderFace (scale);
    g.drawImage (face, getLocalBounds().toFloat());
    auto inner = getLocalBounds().toFloat().reduced (7.f);
    const auto pivot = juce::Point<float> (inner.getCentreX(), inner.getBottom() + inner.getHeight() * 0.18f);
    const float len = inner.getHeight() * 0.98f;
    const float a = kArcStart + (float) GrScale::position (needleDb) * (kArcEnd - kArcStart);
    g.saveState();
    g.reduceClipRegion (inner.withTrimmedBottom (inner.getHeight() * 0.13f).toNearestInt());
    const auto tip = juce::Point<float> (pivot.x + len * std::sin (a), pivot.y - len * std::cos (a));
    g.setColour (juce::Colours::black.withAlpha (0.18f));                   // needle shadow
    g.drawLine (juce::Line<float> (pivot.translated (2.5f, 1.5f), tip.translated (2.5f, 1.5f)), 2.0f);
    g.setColour (juce::Colour (0xff141414));
    g.drawLine (juce::Line<float> (pivot, tip), 1.6f);
    g.setColour (colours::red);
    g.drawLine (juce::Line<float> (pivot + (tip - pivot) * 0.86f, tip), 1.8f);
    g.restoreState();
    // glass reflection
    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.16f), inner.getX(), inner.getY(), juce::Colours::transparentWhite, inner.getX(), inner.getCentreY(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (inner.withHeight (inner.getHeight() * 0.45f), 4.f);
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto cap = r.removeFromBottom (14.f);
    g.setColour (colours::printDim);
    g.setFont (labelFont (10.f));
    g.drawText (label, cap, juce::Justification::centred);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r, 3.f, 1.f);
    auto bar = r.reduced (3.f);
    constexpr int segments = 24;                       // -48 .. 0 dBFS, 2 dB per segment
    const float segH = bar.getHeight() / segments;
    for (int i = 0; i < segments; ++i)
    {
        const float segDb = -48.f + 2.f * (float) (i + 1);
        const bool lit = levelDb >= segDb - 2.f + 0.01f;
        const juce::Colour c = segDb > -2.f ? colours::red : (segDb > -10.f ? colours::accent : juce::Colour (0xff3ccf7a));
        const auto seg = juce::Rectangle<float> (bar.getX(), bar.getBottom() - segH * (float) (i + 1) + 1.f, bar.getWidth(), segH - 1.5f);
        g.setColour (lit ? c : c.withBrightness (0.16f));
        g.fillRect (seg);
    }
}
} // namespace gglue::gui
