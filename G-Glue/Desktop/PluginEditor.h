#pragma once
#include "PluginProcessor.h"
#include "../GUI/GGlueLookAndFeel.h"
#include "../GUI/Meters.h"

// The G-Glue faceplate. Laid out at a fixed base size (kBaseW x kBaseH) and scaled as a whole when resized.
class GGlueEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 460, kBaseH = 980;
    explicit GGlueEditor (GGlueProcessor&);
    ~GGlueEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

private:
    class Panel : public juce::Component
    {
    public:
        explicit Panel (GGlueEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        GGlueEditor& owner;
        juce::Image texture;
    };

    struct Knob
    {
        int param = 0;
        juce::Slider slider;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<int> cell;
    };

    void timerCallback() override;
    void layoutPanel();
    void refreshPresetControls();
    void showPresetMenu();
    void showSaveMenu();
    void stepPreset (int direction);
    void saveAs();
    void renameCurrent();
    void deleteCurrent();
    void importPreset();
    void exportPreset();
    void searchPresets();
    void showMessage (const juce::String& title, const juce::String& text);

    GGlueProcessor& proc;
    gglue::gui::LookAndFeel lnf;
    Panel panel { *this };
    std::array<Knob, 9> knobs;                         // every parameter except analog / bypass (buttons)
    juce::TextButton analogButton { "ANALOG" }, bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analogAttachment, bypassAttachment;
    gglue::gui::GainReductionMeter grMeter;
    gglue::gui::LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    gglue::gui::IconButton prevButton, nextButton, favButton;
    juce::TextButton presetButton, menuButton { "PRESET" }, slotA { "A" }, slotB { "B" }, copyButton;
    std::unique_ptr<juce::FileChooser> chooser;
    gglue::NeedleBallistics needle;
    double lastTick = 0.0;
    std::vector<int> browseList;                        // what PREV / NEXT step through (last menu or search)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GGlueEditor)
};
