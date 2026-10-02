#pragma once
// G-Glue desktop plugin (VST3 / VST2 / standalone) around the shared engine, parameter table and presets.
#include "../DSP/GGlueEngine.h"
#include "../Presets/GGluePresets.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class GGlueProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    GGlueProcessor();
    ~GGlueProcessor() override;

    // ---- juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;                // the double-precision versions stay JUCE's
    using juce::AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "G-Glue Bus Compressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // host programs = factory presets
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread only)
    juce::AudioProcessorValueTreeState& getParameters() { return apvts; }
    gglue::PresetLibrary& getLibrary() { return *library; }
    gglue::ParamValues currentValues() const;
    void applyValues (const gglue::ParamValues& v, bool keepBypass = true);
    bool loadPreset (int libraryIndex);
    juce::String getPresetName() const { return state.presetName; }
    bool isModified() const { return state.modified.load(); }
    int getPresetIndex() const { return library->indexOf (state.presetName.toStdString()); }
    void markSaved (const juce::String& name);
    void setModified (bool m) { state.modified = m; notifyPresetState(); }
    char getActiveSlot() const { return state.activeSlot; }
    void switchSlot (char slot);
    void copyActiveToOther();
    std::function<void()> onPresetStateChanged;            // editor refresh hook (message thread)

    const gglue::Engine& getEngine() const { return engine; }
    static juce::File defaultPresetFolder();

private:
    struct StateInfo
    {
        gglue::ParamValues other = gglue::defaultValues();   // the inactive A/B slot
        char activeSlot = 'A';
        juce::String presetName = "Default";
        std::atomic<bool> modified { false };                 // set from any thread when a parameter moves
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float newValue) override;
    void pushParametersToEngine();
    void notifyPresetState();

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, gglue::kNumParams> raw {};
    gglue::Engine engine;
    std::unique_ptr<gglue::PresetLibrary> library;
    StateInfo state;
    std::atomic<bool> applying { false };
    JUCE_DECLARE_WEAK_REFERENCEABLE (GGlueProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GGlueProcessor)
};
