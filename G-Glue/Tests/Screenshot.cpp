// Renders the real G-Glue editor (in-process) to PNG while audio runs through the processor, so the meters move.
// Usage: GGlueScreenshot <out-dir>     (needs a display: xvfb-run -a GGlueScreenshot Resources/GUI)
#include "../Desktop/PluginEditor.h"
#include <juce_audio_utils/juce_audio_utils.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    out.createDirectory();

    GGlueProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.loadPreset (proc.getLibrary().indexOf ("Drum Punch", true));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (GGlueEditor::kBaseW, GGlueEditor::kBaseH);

    // a drum-like loop, 0.5 s per beat, processed in "real time" while the message loop runs (meters, needle)
    std::atomic<bool> running { true };
    std::thread audio ([&] {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        long t = 0;
        while (running)
        {
            for (int i = 0; i < 512; ++i, ++t)
            {
                const double s = (double) (t % 24000) / 48000.0;
                const float x = (float) (0.9 * std::exp (-s * 18) * std::sin (2 * juce::MathConstants<double>::pi * 55 * s)
                                         + 0.25 * std::sin (2 * juce::MathConstants<double>::pi * 330 * t / 48000.0));
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    });
    auto snap = [&] (const juce::String& name, float scale) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1300);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File f = out.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    };
    snap ("gglue-gui.png", 1.0f);
    snap ("gglue-gui@2x.png", 2.0f);
    running = false;
    audio.join();
    ed.reset();
    return 0;
}
