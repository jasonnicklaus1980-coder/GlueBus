#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace gglue;

namespace
{
juce::String textFor (int index, float plain) { return formatValue (index, plain); }
}

juce::AudioProcessorValueTreeState::ParameterLayout GGlueProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamSpec& s = spec (i);
        const juce::ParameterID pid { s.id, 1 };                  // version hint 1: IDs are stable from 1.0 on
        if (s.type == ParamType::Float)
        {
            auto attr = juce::AudioParameterFloatAttributes()
                            .withStringFromValueFunction ([i] (float v, int) { return textFor (i, v); })
                            .withValueFromStringFunction ([i] (const juce::String& t) { float v = spec (i).def; parseValue (i, t.toStdString(), v); return v; });
            layout.add (std::make_unique<juce::AudioParameterFloat> (pid, s.name, juce::NormalisableRange<float> (s.min, s.max), s.def, attr));
        }
        else if (s.type == ParamType::Choice)
        {
            juce::StringArray names;
            for (int c = 0; c < s.numChoices; ++c) names.add (s.choices[c]);
            layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, names, (int) s.def));
        }
        else
        {
            auto attr = juce::AudioParameterBoolAttributes()
                            .withStringFromValueFunction ([] (bool v, int) { return juce::String (v ? "ON" : "OFF"); })
                            .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().equalsIgnoreCase ("on") || t.getIntValue() != 0; });
            layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f, attr));
        }
    }
    return layout;
}

juce::File GGlueProcessor::defaultPresetFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("GGLUE_PRESET_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("G-Glue").getChildFile ("Presets");
}

GGlueProcessor::GGlueProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GGlue", createLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        raw[(size_t) i] = apvts.getRawParameterValue (spec (i).id);
        apvts.addParameterListener (spec (i).id, this);
    }
    library = std::make_unique<PresetLibrary> (defaultPresetFolder().getFullPathName().toStdString());
    engine.setParameters (defaultValues());
    engine.prepare (44100.0, 2);
}

GGlueProcessor::~GGlueProcessor()
{
    for (int i = 0; i < kNumParams; ++i) apvts.removeParameterListener (spec (i).id, this);
}

bool GGlueProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void GGlueProcessor::prepareToPlay (double sampleRate, int)
{
    pushParametersToEngine();
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (engine.getLatencySamples());
}

void GGlueProcessor::pushParametersToEngine()
{
    for (int i = 0; i < kNumParams; ++i) engine.setParameter (i, raw[(size_t) i]->load (std::memory_order_relaxed));
}

void GGlueProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    for (int c = ins; c < outs; ++c) buffer.clear (c, 0, buffer.getNumSamples());
    pushParametersToEngine();
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), buffer.getNumSamples());
}

void GGlueProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // host bypass: audio passes untouched (our own bypass parameter is reported as the host bypass, see below)
    const int ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    for (int c = ins; c < outs; ++c) buffer.clear (c, 0, buffer.getNumSamples());
}

juce::AudioProcessorParameter* GGlueProcessor::getBypassParameter() const
{
    return apvts.getParameter (spec (kBypass).id);
}

juce::AudioProcessorEditor* GGlueProcessor::createEditor() { return new GGlueEditor (*this); }

// ---------------------------------------------------------------------------------------------------------------
ParamValues GGlueProcessor::currentValues() const
{
    ParamValues v;
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = raw[(size_t) i]->load();
    return v;
}

void GGlueProcessor::applyValues (const ParamValues& v, bool keepBypass)
{
    applying = true;
    for (int i = 0; i < kNumParams; ++i)
    {
        if (keepBypass && i == kBypass) continue;
        if (auto* p = apvts.getParameter (spec (i).id))
        {
            const float norm = p->convertTo0to1 (clampPlain (i, v[(size_t) i]));
            if (p->getValue() != norm)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (norm);
                p->endChangeGesture();
            }
        }
    }
    applying = false;
}

bool GGlueProcessor::loadPreset (int index)
{
    const auto& list = library->presets();
    if (index < 0 || index >= (int) list.size()) return false;
    applyValues (list[(size_t) index].values);
    state.presetName = list[(size_t) index].name;
    state.modified = false;
    notifyPresetState();
    return true;
}

void GGlueProcessor::markSaved (const juce::String& name)
{
    state.presetName = name;
    state.modified = false;
    notifyPresetState();
}

void GGlueProcessor::switchSlot (char slot)
{
    if (slot == state.activeSlot) return;
    const ParamValues now = currentValues();
    applyValues (state.other);
    state.other = now;
    state.activeSlot = slot;
    notifyPresetState();
}

void GGlueProcessor::copyActiveToOther()
{
    state.other = currentValues();
    notifyPresetState();
}

void GGlueProcessor::parameterChanged (const juce::String& id, float)
{
    // may run on the audio thread (automation): only flag, the editor / state code does the comparison
    if (applying || id == spec (kBypass).id) return;
    state.modified = true;          // a plain bool, read on the message thread; a stale read only delays the "*"
}

void GGlueProcessor::notifyPresetState()
{
    if (onPresetStateChanged) onPresetStateChanged();
}

// ---------------------------------------------------------------------------------------------------------------
int GGlueProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

int GGlueProcessor::getCurrentProgram()
{
    const int i = library->indexOf (state.presetName.toStdString(), true);
    return i >= 0 ? i : 0;
}

void GGlueProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= (int) factoryPresets().size()) return;
    // VST3 delivers program changes on the audio thread: never touch the preset state there, load it on the
    // message thread instead (the parameters reach the DSP a few milliseconds later, smoothed).
    auto load = [safe = juce::WeakReference<GGlueProcessor> (this), index] {
        if (safe) safe->loadPreset (safe->library->indexOf (factoryPresets()[(size_t) index].name, true));
    };
    if (juce::MessageManager::existsAndIsCurrentThread()) load();
    else juce::MessageManager::callAsync (load);
}

const juce::String GGlueProcessor::getProgramName (int index)
{
    if (index < 0 || index >= (int) factoryPresets().size()) return {};
    return factoryPresets()[(size_t) index].name;
}

void GGlueProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    PluginState s;
    s.current = currentValues();
    s.other = state.other;
    s.activeSlot = state.activeSlot;
    s.presetName = state.presetName.toStdString();
    s.modified = state.modified.load();
    const std::string text = stateToText (s);
    dest.replaceAll (text.data(), text.size());
}

void GGlueProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > (1 << 20)) return;
    PluginState s;
    if (! stateFromText (std::string (static_cast<const char*> (data), (size_t) size), s)) return;
    applyValues (s.current, false);
    state.other = s.other;
    state.activeSlot = s.activeSlot;
    state.presetName = s.presetName;
    state.modified = s.modified;
    juce::MessageManager::callAsync ([safe = juce::WeakReference<GGlueProcessor> (this)] { if (safe) safe->notifyPresetState(); });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GGlueProcessor(); }
