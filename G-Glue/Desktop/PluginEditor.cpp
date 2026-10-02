#include "PluginEditor.h"

using namespace gglue;
namespace col = gglue::gui::colours;

namespace
{
juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// knob order on the faceplate: THRESHOLD MAKEUP / ATTACK RELEASE / RATIO SC FILTER / MIX INPUT / OUTPUT [ANALOG]
constexpr int kKnobParams[9] { kThreshold, kMakeup, kAttack, kRelease, kRatio, kScFilter, kMix, kInput, kOutput };
const char* const kCaptions[kNumParams] { "THRESHOLD", "MAKEUP", "ATTACK", "RELEASE", "RATIO", "SC FILTER", "MIX", "INPUT", "OUTPUT", "ANALOG", "BYPASS" };

// short scale labels printed around the knobs
std::vector<juce::String> scaleLabels (int param)
{
    switch (param)
    {
        case kThreshold: return { "-30", "+10" };
        case kMakeup:    return { "0", "+24" };
        case kAttack:    return { ".1", ".3", "1", "3", "10", "30" };
        case kRelease:   return { ".1", ".3", ".6", "1.2", "A" };
        case kRatio:     return { "2", "4", "10" };
        case kScFilter:  return { "OFF", "30", "60", "90", "120", "150", "200" };
        case kMix:       return { "0", "100" };
        default:         return { "-24", "+24" };
    }
}
constexpr float kRotStart = -2.35f, kRotEnd = 2.35f;
constexpr int kColX[2] { 112, 300 }, kRowY0 = 316, kRowH = 96, kKnobSize = 64;
}

GGlueEditor::GGlueEditor (GGlueProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (panel);
    auto& apvts = proc.getParameters();

    for (size_t k = 0; k < knobs.size(); ++k)
    {
        Knob& kn = knobs[k];
        kn.param = kKnobParams[k];
        const ParamSpec& s = spec (kn.param);
        kn.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        kn.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        kn.slider.setRotaryParameters (kRotStart + juce::MathConstants<float>::twoPi, kRotEnd + juce::MathConstants<float>::twoPi, true);
        kn.slider.setMouseDragSensitivity (s.type == ParamType::Choice ? 120 : 260);
        kn.slider.setVelocityBasedMode (false);
        if (s.type == ParamType::Choice) kn.slider.getProperties().set ("steps", s.numChoices);
        kn.slider.setPopupDisplayEnabled (false, false, nullptr);
        kn.slider.setTooltip (s.name);
        panel.addAndMakeVisible (kn.slider);
        kn.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, s.id, kn.slider);
        kn.slider.setDoubleClickReturnValue (true, s.def);
        kn.value.setJustificationType (juce::Justification::centred);
        kn.value.setFont (gui::labelFont (13.f));
        kn.value.setColour (juce::Label::textColourId, col::accent);
        kn.value.setInterceptsMouseClicks (false, false);
        panel.addAndMakeVisible (kn.value);
        auto update = [this, k] {
            Knob& kk = knobs[k];
            kk.value.setText (formatValue (kk.param, (float) kk.slider.getValue()), juce::dontSendNotification);
        };
        kn.slider.onValueChange = update;
        update();
    }

    for (auto* b : { &analogButton, &bypassButton })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("led", true);
        panel.addAndMakeVisible (*b);
    }
    bypassButton.getProperties().set ("ledColour", (juce::int64) col::red.getARGB());
    analogButton.onStateChange = [this] { analogButton.setButtonText (analogButton.getToggleState() ? "ON" : "OFF"); };
    analogAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, spec (kAnalog).id, analogButton);
    analogButton.setButtonText (analogButton.getToggleState() ? "ON" : "OFF");
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, spec (kBypass).id, bypassButton);

    panel.addAndMakeVisible (grMeter);
    panel.addAndMakeVisible (inMeter);
    panel.addAndMakeVisible (outMeter);

    prevButton.icon = gglue::gui::IconButton::Left;
    nextButton.icon = gglue::gui::IconButton::Right;
    favButton.icon = gglue::gui::IconButton::Star;
    copyButton.setTooltip ("Copy the active slot to the other one");
    favButton.setTooltip ("Favourite");
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    presetButton.onClick = [this] { showPresetMenu(); };
    menuButton.onClick = [this] { showSaveMenu(); };
    favButton.onClick = [this] {
        auto& lib = proc.getLibrary();
        const int i = proc.getPresetIndex();
        if (i >= 0) lib.setFavorite (i, ! lib.isFavorite (i));
        refreshPresetControls();
    };
    slotA.onClick = [this] { proc.switchSlot ('A'); };
    slotB.onClick = [this] { proc.switchSlot ('B'); };
    copyButton.onClick = [this] { proc.copyActiveToOther(); };
    for (juce::Button* b : std::initializer_list<juce::Button*> { &prevButton, &nextButton, &presetButton, &favButton, &menuButton, &slotA, &slotB, &copyButton })
        panel.addAndMakeVisible (*b);

    proc.onPresetStateChanged = [this] { refreshPresetControls(); };
    browseList = proc.getLibrary().search ("");

    layoutPanel();
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kBaseW / kBaseH);
    setResizeLimits (kBaseW * 7 / 10, kBaseH * 7 / 10, kBaseW * 2, kBaseH * 2);
    setSize (kBaseW * 9 / 10, kBaseH * 9 / 10);
    refreshPresetControls();
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

GGlueEditor::~GGlueEditor()
{
    proc.onPresetStateChanged = nullptr;
    stopTimer();
    setLookAndFeel (nullptr);
}

void GGlueEditor::resized()
{
    panel.setBounds (0, 0, kBaseW, kBaseH);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
}

void GGlueEditor::layoutPanel()
{
    inMeter.setBounds (26, 124, 26, 172);
    grMeter.setBounds (68, 120, 324, 176);
    outMeter.setBounds (408, 124, 26, 172);
    for (size_t k = 0; k < knobs.size(); ++k)
    {
        const int row = (int) k / 2, colIdx = (int) k % 2;
        const int cx = kColX[colIdx], top = kRowY0 + row * kRowH;
        Knob& kn = knobs[k];
        kn.cell = { cx - 90, top, 180, kRowH };
        kn.slider.setBounds (cx - kKnobSize / 2, top + 24, kKnobSize, kKnobSize);
        kn.value.setBounds (cx + 60, top + 46, 58, 20);
    }
    // ANALOG sits in the last knob cell of the right column
    analogButton.setBounds (kColX[1] - 40, kRowY0 + 4 * kRowH + 38, 118, 36);
    bypassButton.setBounds (kBaseW / 2 - 70, 812, 140, 40);
    prevButton.setBounds (34, 872, 34, 30);
    presetButton.setBounds (72, 872, 280, 30);
    nextButton.setBounds (356, 872, 34, 30);
    favButton.setBounds (394, 872, 32, 30);
    menuButton.setBounds (34, 910, 110, 28);
    slotA.setBounds (238, 910, 40, 28);
    slotB.setBounds (282, 910, 40, 28);
    copyButton.setBounds (326, 910, 100, 28);
}

void GGlueEditor::Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    // faceplate
    juce::ColourGradient plate (col::plateTop, 0, 0, col::plateBottom, 0, r.getHeight(), false);
    g.setGradientFill (plate);
    g.fillRect (r);
    if (texture.isNull()) texture = gui::makePlateTexture (getWidth(), getHeight(), 0x6611e);
    g.drawImageAt (texture, 0, 0);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRect (r.reduced (1.f), 1.f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawRect (r, 1.f);
    // engraved panel seams
    for (float y : { 104.f, 306.f, 800.f, 862.f })
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.drawHorizontalLine ((int) y, 18.f, r.getWidth() - 18.f);
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawHorizontalLine ((int) y + 1, 18.f, r.getWidth() - 18.f);
    }
    // screws
    gui::drawScrew (g, { 22.f, 22.f }, 8.f, 0.4f);
    gui::drawScrew (g, { r.getWidth() - 22.f, 22.f }, 8.f, 1.1f);
    gui::drawScrew (g, { 22.f, r.getHeight() - 22.f }, 8.f, 0.9f);
    gui::drawScrew (g, { r.getWidth() - 22.f, r.getHeight() - 22.f }, 8.f, 0.2f);
    // title
    g.setColour (col::print);
    g.setFont (gui::labelFont (40.f).withExtraKerningFactor (0.18f));
    g.drawText ("G-GLUE", juce::Rectangle<float> (0, 22, r.getWidth(), 46), juce::Justification::centred);
    g.setColour (col::printDim);
    g.setFont (gui::labelFont (13.f).withExtraKerningFactor (0.45f));
    g.drawText ("BUS COMPRESSOR", juce::Rectangle<float> (0, 70, r.getWidth(), 18), juce::Justification::centred);

    // knob captions and scale labels
    for (const auto& kn : owner.knobs)
    {
        const auto b = kn.slider.getBounds().toFloat();
        g.setColour (col::print);
        g.setFont (gui::labelFont (12.f).withExtraKerningFactor (0.08f));
        g.drawText (kCaptions[kn.param], juce::Rectangle<float> (b.getCentreX() - 80, b.getY() - 24, 160, 13), juce::Justification::centred);
        const auto labels = scaleLabels (kn.param);
        g.setColour (col::printDim);
        g.setFont (gui::labelFont (9.5f));
        const auto c = b.getCentre();
        const float rad = b.getWidth() * 0.5f + 8.f;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            const float t = labels.size() > 1 ? (float) i / (float) (labels.size() - 1) : 0.f;
            const float a = kRotStart + t * (kRotEnd - kRotStart);
            const auto pt = juce::Point<float> (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
            g.drawText (labels[i], juce::Rectangle<float> (pt.x - 16, pt.y - 6, 32, 12), juce::Justification::centred);
        }
        // value window
        auto vb = kn.value.getBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (vb, 3.f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (vb, 3.f, 1.f);
    }
    // ANALOG caption
    const auto ab = owner.analogButton.getBounds().toFloat();
    g.setColour (col::print);
    g.setFont (gui::labelFont (12.f).withExtraKerningFactor (0.08f));
    g.drawText ("ANALOG", juce::Rectangle<float> (ab.getX() - 30, ab.getY() - 38, ab.getWidth() + 60, 13), juce::Justification::centred);
    g.setColour (col::printDim);
    g.setFont (gui::labelFont (9.5f));
    g.drawText ("A / B", juce::Rectangle<float> (238, 940, 84, 12), juce::Justification::centred);
}

void GGlueEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;
    const auto& e = proc.getEngine();
    grMeter.setNeedle (needle.step (e.gainReductionDb.load(), dt));
    inMeter.setLevel (e.inputMeterDb.load());
    outMeter.setLevel (e.outputMeterDb.load());
    // the "modified" star can be set from the audio thread (automation)
    const bool mod = proc.isModified();
    if (presetButton.getButtonText().endsWith ("*") != mod) refreshPresetControls();
}

void GGlueEditor::refreshPresetControls()
{
    auto& lib = proc.getLibrary();
    const int idx = proc.getPresetIndex();
    juce::String name = proc.getPresetName();
    if (idx >= 0 && ! lib.presets()[(size_t) idx].factory) name << "  (user)";
    if (proc.isModified()) name << " *";
    presetButton.setButtonText (name);
    favButton.filled = lib.isFavorite (idx);
    favButton.repaint();
    favButton.setEnabled (idx >= 0);
    const bool a = proc.getActiveSlot() == 'A';
    slotA.setToggleState (a, juce::dontSendNotification);
    slotB.setToggleState (! a, juce::dontSendNotification);
    slotA.getProperties().set ("led", true); slotB.getProperties().set ("led", true);
    copyButton.setButtonText (utf8 (a ? "COPY A \xe2\x86\x92 B" : "COPY B \xe2\x86\x92 A"));
    slotA.repaint(); slotB.repaint();
}

void GGlueEditor::stepPreset (int direction)
{
    auto& lib = proc.getLibrary();
    if (browseList.empty()) browseList = lib.search ("");
    const int next = lib.nextIndex (proc.getPresetIndex(), direction, browseList);
    if (next >= 0) proc.loadPreset (next);
}

void GGlueEditor::showPresetMenu()
{
    auto& lib = proc.getLibrary();
    const auto& list = lib.presets();
    const int current = proc.getPresetIndex();
    juce::PopupMenu menu;
    const auto favs = lib.search ("", "", true);
    if (! favs.empty())
    {
        menu.addSectionHeader ("FAVOURITES");
        for (int i : favs) menu.addItem (1 + i, list[(size_t) i].name, true, i == current);
        menu.addSeparator();
    }
    for (const auto& cat : presetCategories())
    {
        const auto items = lib.search ("", cat);
        if (items.empty()) continue;
        juce::PopupMenu sub;
        bool hasCurrent = false;
        for (int i : items) { sub.addItem (1 + i, list[(size_t) i].name, true, i == current); hasCurrent |= i == current; }
        menu.addSubMenu (cat, sub, true, nullptr, hasCurrent);
    }
    menu.addSeparator();
    menu.addItem (100000, "Search...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                        [this] (int r) {
                            if (r == 100000) { searchPresets(); return; }
                            if (r > 0) { browseList = proc.getLibrary().search (""); proc.loadPreset (r - 1); }
                        });
}

void GGlueEditor::searchPresets()
{
    auto* aw = new juce::AlertWindow ("Search presets", "Name or category (e.g. \"drum\", \"vocal\", \"glue\")", juce::MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("q", {}, "Search:");
    aw->addButton ("Search", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r) {
        if (r != 1) return;
        auto& lib = proc.getLibrary();
        const auto hits = lib.search (aw->getTextEditorContents ("q").toStdString());
        if (hits.empty()) { showMessage ("Search presets", "No preset matches."); return; }
        browseList = hits;                                      // PREV / NEXT now step through the results
        juce::PopupMenu m;
        m.addSectionHeader (juce::String ((int) hits.size()) + " found");
        for (int i : hits) m.addItem (1 + i, lib.presets()[(size_t) i].name + "   " + lib.presets()[(size_t) i].category);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this] (int x) { if (x > 0) proc.loadPreset (x - 1); });
    }), true);
}

void GGlueEditor::showSaveMenu()
{
    auto& lib = proc.getLibrary();
    const int idx = proc.getPresetIndex();
    const bool user = idx >= 0 && ! lib.presets()[(size_t) idx].factory;
    juce::PopupMenu m;
    m.addItem (1, user ? "Save" : "Save (as a new user preset)");
    m.addItem (2, "Save As...");
    m.addItem (3, "Rename...", user);
    m.addItem (4, "Delete", user);
    m.addSeparator();
    m.addItem (5, "Import...");
    m.addItem (6, "Export...");
    m.addSeparator();
    m.addItem (7, "Open preset folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [this, user] (int r) {
        switch (r)
        {
            case 1:
                if (user)
                {
                    std::string err;
                    const auto& p = proc.getLibrary().presets()[(size_t) proc.getPresetIndex()];
                    if (proc.getLibrary().save (p.name, p.category, proc.currentValues(), true, &err)) proc.markSaved (p.name);
                    else showMessage ("Save", err);
                }
                else saveAs();
                break;
            case 2: saveAs(); break;
            case 3: renameCurrent(); break;
            case 4: deleteCurrent(); break;
            case 5: importPreset(); break;
            case 6: exportPreset(); break;
            case 7:
            {
                const juce::File dir (proc.getLibrary().folder().string());
                dir.createDirectory();
                dir.startAsProcess();
                break;
            }
            default: break;
        }
    });
}

void GGlueEditor::saveAs()
{
    auto* aw = new juce::AlertWindow ("Save preset", "Saved in " + juce::String (proc.getLibrary().folder().string()), juce::MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", proc.getPresetName().upToFirstOccurrenceOf (" *", false, false), "Name:");
    juce::StringArray cats;
    for (const auto& c : presetCategories()) cats.add (c);
    aw->addComboBox ("cat", cats, "Category:");
    aw->getComboBoxComponent ("cat")->setText ("USER");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r) {
        if (r != 1) return;
        const std::string name = sanitizePresetName (aw->getTextEditorContents ("name").toStdString());
        const std::string cat = aw->getComboBoxComponent ("cat")->getText().toStdString();
        std::string err;
        if (proc.getLibrary().save (name, cat, proc.currentValues(), false, &err)) proc.markSaved (name);
        else showMessage ("Save preset", juce::String (err) + ".\nChoose another name, or load it and use Save to overwrite.");
    }), true);
}

void GGlueEditor::renameCurrent()
{
    auto* aw = new juce::AlertWindow ("Rename preset", {}, juce::MessageBoxIconType::NoIcon);
    aw->setLookAndFeel (&lnf);
    aw->addTextEditor ("name", proc.getPresetName(), "New name:");
    aw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r) {
        if (r != 1) return;
        std::string err;
        const std::string name = sanitizePresetName (aw->getTextEditorContents ("name").toStdString());
        const bool wasModified = proc.isModified();
        if (proc.getLibrary().rename (proc.getPresetIndex(), name, &err))
        {
            proc.markSaved (name);
            proc.setModified (wasModified);           // renaming doesn't save unsaved knob changes
        }
        else showMessage ("Rename preset", err);
    }), true);
}

void GGlueEditor::deleteCurrent()
{
    const juce::String name = proc.getPresetName();
    auto* aw = new juce::AlertWindow ("Delete preset", "Delete the user preset \"" + name + "\"? This can't be undone.", juce::MessageBoxIconType::WarningIcon);
    aw->setLookAndFeel (&lnf);
    aw->addButton ("Delete", 1);
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r) {
        if (r != 1) return;
        std::string err;
        if (proc.getLibrary().remove (proc.getPresetIndex(), &err)) { proc.markSaved ("Untitled"); browseList = proc.getLibrary().search (""); }
        else showMessage ("Delete preset", err);
    }), true);
}

void GGlueEditor::importPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a G-Glue preset", juce::File::getSpecialLocation (juce::File::userHomeDirectory), "*.gglue");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        std::string name, err;
        if (proc.getLibrary().importFile (f.getFullPathName().toStdString(), &name, &err))
        {
            browseList = proc.getLibrary().search ("");
            proc.loadPreset (proc.getLibrary().indexOf (name, false));
        }
        else showMessage ("Import preset", err);
    });
}

void GGlueEditor::exportPreset()
{
    const juce::String name = proc.getPresetName().upToFirstOccurrenceOf (" *", false, false);
    chooser = std::make_unique<juce::FileChooser> ("Export the current settings", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (name + ".gglue"), "*.gglue");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, name] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! f.hasFileExtension ("gglue")) f = f.withFileExtension ("gglue");
        const int idx = proc.getPresetIndex();
        const std::string cat = idx >= 0 ? proc.getLibrary().presets()[(size_t) idx].category : "USER";
        std::string err;
        if (! PresetLibrary::exportValues (name.toStdString(), cat, proc.currentValues(), f.getFullPathName().toStdString(), &err))
            showMessage ("Export preset", err);
    });
}

void GGlueEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, text, "OK", this);
}
