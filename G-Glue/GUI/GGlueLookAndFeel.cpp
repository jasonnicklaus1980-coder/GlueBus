#include "GGlueLookAndFeel.h"

namespace gglue::gui
{
juce::Font labelFont (float height, bool bold)
{
    return juce::Font (juce::Font::getDefaultSansSerifFontName(), height, bold ? juce::Font::bold : juce::Font::plain);
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    // recess, head, slot cross
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - r - 1.5f, c.y - r - 0.5f, (r + 1.5f) * 2, (r + 1.5f) * 2);
    juce::ColourGradient head (juce::Colour (0xffd4d5d9), c.x - r * 0.6f, c.y - r * 0.8f, juce::Colour (0xff5d5f66), c.x + r * 0.7f, c.y + r, true);
    g.setGradientFill (head);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (juce::Colour (0xff2a2b2f));
    g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 0.8f);
    const auto t = juce::AffineTransform::rotation (angle, c.x, c.y);
    juce::Path slot;
    slot.addRoundedRectangle (c.x - r * 0.72f, c.y - r * 0.13f, r * 1.44f, r * 0.26f, r * 0.1f);
    slot.addRoundedRectangle (c.x - r * 0.13f, c.y - r * 0.72f, r * 0.26f, r * 1.44f, r * 0.1f);
    g.setColour (juce::Colour (0xff26272b));
    g.fillPath (slot, t);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (slot, juce::PathStrokeType (0.5f), t.translated (0.4f, 0.6f));
}

juce::Image makePlateTexture (int w, int h, juce::uint32 seed)
{
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Random rng ((juce::int64) seed);
    juce::Graphics g (img);
    // fine horizontal brushing: many faint lines of random length and brightness
    for (int i = 0; i < w * h / 90; ++i)
    {
        const float y = rng.nextFloat() * (float) h, x = rng.nextFloat() * (float) w, len = 20.f + rng.nextFloat() * 160.f;
        const bool light = rng.nextBool();
        g.setColour ((light ? juce::Colours::white : juce::Colours::black).withAlpha (0.018f + rng.nextFloat() * 0.03f));
        g.drawHorizontalLine ((int) y, x, juce::jmin ((float) w, x + len));
    }
    return img;
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff232428));
    setColour (juce::PopupMenu::textColourId, colours::print);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3d3f45));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, colours::accent);
    setColour (juce::TextButton::textColourOffId, colours::print);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff26272b));
    setColour (juce::AlertWindow::textColourId, colours::print);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141518));
    setColour (juce::TextEditor::textColourId, colours::print);
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff4a4c52));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff141518));
    setColour (juce::ComboBox::textColourId, colours::print);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rTicks = size * 0.5f - 1.f, rKnob = size * 0.36f;
    const float angle = start + pos * (end - start);

    // scale ticks (stepped knobs: one tick per position, continuous: 11)
    const int steps = (int) s.getProperties().getWithDefault ("steps", 0);
    const int ticks = steps > 1 ? steps : 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = start + (float) i / (float) (ticks - 1) * (end - start);
        const bool major = steps > 1 || i % 5 == 0;
        const float r0 = rTicks - (major ? 6.f : 4.f);
        g.setColour (major ? colours::print.withAlpha (0.85f) : colours::printDim.withAlpha (0.7f));
        g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + rTicks * std::sin (a), c.y - rTicks * std::cos (a), major ? 1.4f : 1.0f);
    }

    // drop shadow
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - rKnob - 1.f, c.y - rKnob + 3.f, rKnob * 2 + 2.f, rKnob * 2 + 2.f);

    // knurled skirt
    juce::ColourGradient skirt (juce::Colour (0xff3a3b40), c.x, c.y - rKnob, juce::Colour (0xff111214), c.x, c.y + rKnob, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - rKnob, c.y - rKnob, rKnob * 2, rKnob * 2);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = angle + (float) i * juce::MathConstants<float>::twoPi / 48.f;
        g.drawLine (c.x + rKnob * 0.86f * std::sin (a), c.y - rKnob * 0.86f * std::cos (a), c.x + rKnob * std::sin (a), c.y - rKnob * std::cos (a), 1.0f);
    }

    // machined cap with a soft top-left highlight
    const float rCap = rKnob * 0.78f;
    juce::ColourGradient cap (juce::Colour (0xff5b5d64), c.x - rCap * 0.5f, c.y - rCap * 0.7f, juce::Colour (0xff1e1f23), c.x + rCap * 0.6f, c.y + rCap, true);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - rCap, c.y - rCap, rCap * 2, rCap * 2);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (c.x - rCap + 0.5f, c.y - rCap + 0.5f, rCap * 2 - 1.f, rCap * 2 - 1.f, 1.0f);
    // concentric machining rings
    for (float k = 0.25f; k < 0.95f; k += 0.14f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (c.x - rCap * k, c.y - rCap * k, rCap * 2 * k, rCap * 2 * k, 0.7f);
    }

    // pointer line
    juce::Path p;
    p.addRoundedRectangle (-1.6f, -rKnob * 0.98f, 3.2f, rKnob * 0.55f, 1.2f);
    g.setColour (s.isEnabled() ? colours::print : colours::printDim);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    const bool on = b.getToggleState();
    const bool led = (bool) b.getProperties().getWithDefault ("led", false);
    const auto ledColour = juce::Colour ((juce::uint32) (juce::int64) b.getProperties().getWithDefault ("ledColour", (juce::int64) colours::accent.getARGB()));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0, 1.5f), 4.f);
    juce::ColourGradient face (juce::Colour (down ? 0xff26272b : 0xff3b3d42), r.getX(), r.getY(), juce::Colour (0xff1f2023), r.getX(), r.getBottom(), false);
    g.setGradientFill (face);
    g.fillRoundedRectangle (r, 4.f);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.22f : 0.12f));
    g.drawRoundedRectangle (r, 4.f, 1.f);
    if (led)
    {
        const float d = juce::jmin (8.f, r.getHeight() * 0.32f);
        const auto lr = juce::Rectangle<float> (r.getX() + 8.f, r.getCentreY() - d / 2, d, d);
        if (on)
        {
            g.setColour (ledColour.withAlpha (0.35f));
            g.fillEllipse (lr.expanded (3.f));
        }
        g.setColour (on ? ledColour : ledColour.withBrightness (0.22f));
        g.fillEllipse (lr);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (lr, 0.8f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool led = (bool) b.getProperties().getWithDefault ("led", false);
    auto r = b.getLocalBounds();
    if (led) r.removeFromLeft (14);
    g.setColour (b.isEnabled() ? colours::print : colours::printDim);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText(), r.reduced (3, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRect (0, 0, w, h);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, over, down);
    const auto c = getLocalBounds().toFloat().getCentre().translated (0, down ? 1.f : 0.f);
    const float s = juce::jmin (getWidth(), getHeight()) * 0.22f;
    juce::Path p;
    if (icon == Left) p.addTriangle (c.x + s * 0.8f, c.y - s, c.x + s * 0.8f, c.y + s, c.x - s * 0.9f, c.y);
    else if (icon == Right) p.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s * 0.9f, c.y);
    else p.addStar (c, 5, s * 0.5f, s * 1.25f, 0.f);
    if (icon == Star && ! filled)
    {
        g.setColour (isEnabled() ? colours::print : colours::printDim);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved));
    }
    else
    {
        g.setColour (icon == Star ? colours::accent : (isEnabled() ? colours::print : colours::printDim));
        g.fillPath (p);
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return labelFont (juce::jmin (13.f, (float) h * 0.48f)); }
juce::Font LookAndFeel::getPopupMenuFont() { return labelFont (14.f, false); }
} // namespace gglue::gui
