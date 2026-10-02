// Loads the BUILT G-Glue plugin binaries (VST3 bundle, optionally the VST2 .so) through JUCE's hosting code,
// the way a DAW / MPC Desktop would, and tests them: identity, parameters, processing, automation, bypass,
// programs (factory presets), state save / restore, sample rates, editor.
// Usage: GGlueHostTest <path to .vst3> [<path to VST2 .so>]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; const bool ok_ = (cond); std::printf ("  %s ", ok_ ? "ok  " : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

namespace
{
const char* const kNames[] { "Threshold", "Makeup", "Attack", "Release", "Ratio", "SC Filter", "Mix", "Input", "Output", "Analog", "Bypass" };
constexpr double kPi = 3.14159265358979323846;

juce::AudioProcessorParameter* byName (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters()) if (prm->getName (64) == name) return prm;
    return nullptr;
}

void setPlain (juce::AudioProcessorParameter* p, const juce::String& text)
{
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getValueForText (text));
    p->endChangeGesture();
}

// processes `seconds` of a 1 kHz sine at `amp`, returns output RMS over the second half; optional per-block hook
template <typename Hook>
double runSine (juce::AudioPluginInstance& p, double fs, double seconds, double amp, Hook hook, std::vector<float>* capture = nullptr, double hz = 1000)
{
    const int block = 256, total = (int) (fs * seconds);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double acc = 0; long n = 0;
    for (int pos = 0, b = 0; pos < total; pos += block, ++b)
    {
        hook (b);
        for (int i = 0; i < block; ++i)
        {
            const float x = (float) (amp * std::sin (2 * kPi * hz * (pos + i) / fs));
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            const float y = buf.getSample (0, i);
            if (capture) capture->push_back (y);
            if (pos + i > total / 2) { acc += (double) y * y; ++n; }
        }
    }
    return std::sqrt (acc / (double) juce::jmax (1L, n));
}
double runSine (juce::AudioPluginInstance& p, double fs, double seconds, double amp) { return runSine (p, fs, seconds, amp, [] (int) {}); }
double db (double x) { return 20 * std::log10 (juce::jmax (x, 1e-12)); }

void testInstance (juce::AudioPluginFormatManager& fm, const juce::PluginDescription& desc)
{
    juce::String err;
    auto p = fm.createPluginInstance (desc, 48000, 256, err);
    CHECK (p != nullptr, "%s loads (%s)", desc.pluginFormatName.toRawUTF8(), err.isEmpty() ? desc.fileOrIdentifier.toRawUTF8() : err.toRawUTF8());
    if (! p) return;
    CHECK (p->getName().contains ("G-Glue") && ! desc.isInstrument && desc.manufacturerName == "G-Glue Audio",
           "identity: \"%s\" by %s, an effect (category %s)", p->getName().toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8());
    CHECK (p->getTotalNumInputChannels() == 2 && p->getTotalNumOutputChannels() == 2, "stereo in / out");

    const auto& params = p->getParameters();
    bool order = params.size() >= 11;
    juce::StringArray ids;
    for (int i = 0; i < 11 && order; ++i) { order = params[i]->getName (64) == kNames[i]; ids.add (params[i]->getName (64)); }
    CHECK (order, "%d parameters, first 11 in Q-Link order: Threshold, Makeup, Attack, Release, Ratio, SC Filter, Mix, Input, Output, Analog, Bypass", params.size());
    if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (params[0]))
    {
        juce::StringArray pids;
        for (int i = 0; i < 11; ++i) if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (params[i])) pids.add (h->getParameterID());
        auto p2 = fm.createPluginInstance (desc, 48000, 256, err);
        bool same = p2 != nullptr;
        for (int i = 0; i < 11 && same; ++i)
            if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (p2->getParameters()[i])) same = h->getParameterID() == pids[i];
        CHECK (same && pids.size() == 11, "host parameter IDs stable across instances (%s ...)", (pids[0] + ", " + pids[1] + ", " + pids[2]).toRawUTF8());
        (void) hp;
    }
    auto* thr = byName (*p, "Threshold");
    auto* rel = byName (*p, "Release");
    auto* mix = byName (*p, "Mix");
    auto* bypass = byName (*p, "Bypass");
    if (! thr || ! rel || ! mix || ! bypass) { CHECK (false, "parameters found by name"); return; }
    CHECK (thr->getCurrentValueAsText() == "-10.0 dB" && rel->getCurrentValueAsText() == "AUTO",
           "readable values in the host: threshold \"%s\", release \"%s\"", thr->getCurrentValueAsText().toRawUTF8(), rel->getCurrentValueAsText().toRawUTF8());
    CHECK (p->getBypassParameter() != nullptr, "host bypass is mapped to the plugin's Bypass parameter");

    // processing at every supported rate
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        p->setPlayConfigDetails (2, 2, fs, 256);
        p->prepareToPlay (fs, 256);
        const double in = 0.9 / std::sqrt (2.0), out = runSine (*p, fs, 1.0, 0.9);
        CHECK (db (out / in) < -5 && db (out / in) > -20 && std::isfinite (out), "%.1f kHz: a -0.9 dBFS sine comes out %.1f dB (defaults: -10 dB, 4:1)", fs / 1000, db (out / in));
        p->releaseResources();
    }
    p->setPlayConfigDetails (2, 2, 48000, 256);
    p->prepareToPlay (48000, 256);

    // parameter changes reach the DSP
    setPlain (thr, "+10");
    const double clean = runSine (*p, 48000, 0.6, 0.5);
    CHECK (std::fabs (db (clean / (0.5 / std::sqrt (2.0)))) < 0.1, "threshold +10 dB: no compression (%.2f dB)", db (clean / (0.5 / std::sqrt (2.0))));
    setPlain (thr, "-30");
    const double hard = runSine (*p, 48000, 0.6, 0.5);
    CHECK (db (hard / clean) < -10, "threshold -30 dB: %.1f dB of compression", db (hard / clean));

    // automation: threshold swept every block, no clicks
    std::vector<float> y;
    runSine (*p, 48000, 2.0, 0.5, [&] (int b) { thr->setValueNotifyingHost ((float) (0.5 + 0.5 * std::sin (b * 0.37))); }, &y, 100);
    double worst = 0; for (size_t i = 4800; i + 2 < y.size(); ++i) worst = juce::jmax (worst, (double) std::fabs (y[i + 2] - 2 * y[i + 1] + y[i]));
    CHECK (worst < 0.01, "threshold automated every block on a 100 Hz sine: largest step %.4f (no zipper noise)", worst);

    // bypass
    auto* hostBypass = p->getBypassParameter();
    hostBypass->beginChangeGesture(); hostBypass->setValueNotifyingHost (1.0f); hostBypass->endChangeGesture();
    runSine (*p, 48000, 0.05, 0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (bypass->getValue() > 0.5f, "host bypass switches the plugin's Bypass parameter: %s", bypass->getCurrentValueAsText().toRawUTF8());
    std::vector<float> by;
    runSine (*p, 48000, 0.5, 0.5, [] (int) {}, &by);
    double diff = 0;
    for (size_t i = 4800; i < by.size(); ++i) diff = juce::jmax (diff, std::fabs ((double) by[i] - 0.5 * std::sin (2 * kPi * 1000 * (double) i / 48000)));
    CHECK (diff < 1e-6, "Bypass ON: output = input (max difference %.1g)", diff);
    if (diff >= 1e-6) for (size_t i = 4800; i < 4805; ++i) std::printf ("    out %.7f  in %.7f\n", by[i], 0.5 * std::sin (2 * kPi * 1000 * (double) i / 48000));
    hostBypass->beginChangeGesture(); hostBypass->setValueNotifyingHost (0.0f); hostBypass->endChangeGesture();
    runSine (*p, 48000, 0.05, 0.1);

    // programs = factory presets
    CHECK (p->getNumPrograms() >= 60, "%d factory presets exposed as host programs", p->getNumPrograms());
    int idx = -1;
    for (int i = 0; i < p->getNumPrograms(); ++i) if (p->getProgramName (i) == "Parallel Smash") idx = i;
    if (idx >= 0) p->setCurrentProgram (idx);
    // VST3 sends the program change with the next process call; the plugin then loads it on its message thread
    runSine (*p, 48000, 0.05, 0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    runSine (*p, 48000, 0.05, 0.1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    CHECK (idx >= 0 && mix->getCurrentValueAsText() == "35 %" && thr->getCurrentValueAsText() == "-28.0 dB",
           "program \"Parallel Smash\" sets mix %s, threshold %s", mix->getCurrentValueAsText().toRawUTF8(), thr->getCurrentValueAsText().toRawUTF8());

    // state save / restore into a new instance
    setPlain (mix, "63");
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = fm.createPluginInstance (desc, 48000, 256, err);
    bool same = q != nullptr;
    if (q)
    {
        q->setStateInformation (state.getData(), (int) state.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        for (int i = 0; i < 11; ++i)
            if (std::fabs (q->getParameters()[i]->getValue() - p->getParameters()[i]->getValue()) >= 1e-4f)
            {
                same = false;
                std::printf ("    %s: saved %s, restored %s\n", kNames[i], p->getParameters()[i]->getCurrentValueAsText().toRawUTF8(), q->getParameters()[i]->getCurrentValueAsText().toRawUTF8());
            }
    }
    CHECK (same, "state (%d bytes) restored into a new instance: all 11 parameters equal", (int) state.getSize());

    // editor
    if (p->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditorIfNeeded());
        CHECK (ed != nullptr && ed->getWidth() > 300 && ed->getHeight() > 600, "editor opens: %d x %d", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        ed.reset();
    }
    p->releaseResources();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    for (int a = 1; a < argc; ++a)
    {
        const juce::String path (argv[a]);
        if (path.isEmpty()) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        for (auto* f : fm.getFormats())
            if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (found, path);
        std::printf ("%s\n", path.toRawUTF8());
        CHECK (found.size() == 1, "found %d plugin(s) in the binary", found.size());
        for (auto* d : found) testInstance (fm, *d);
    }
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
