// Renders the Backtrack editor to PNG files for a visual check (developer tool, not part of CI).
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

static void pump (BacktrackProcessor& p, int blocks, float freq = 330.0f)
{
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    static double ph = 0;
    for (int k = 0; k < blocks; ++k)
    {
        for (int i = 0; i < 512; ++i) { const float x = 0.3f * (float) std::sin (ph); ph += 2 * juce::MathConstants<double>::pi * freq / 48000.0; b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : ".");
    BacktrackProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);
    auto* bed = dynamic_cast<BacktrackEditor*> (ed.get());
    auto run = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    auto shot = [&] (const juce::String& name)
    {
        run (700);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::PNGImageFormat png;
        juce::FileOutputStream os (outDir.getChildFile (name + ".png"));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (img, os);
    };

    proc.loadPreset (5);                  // FIFTHS CLIMB
    pump (proc, 50);
    shot ("main_fifths");

    proc.loadPreset (13);                 // THREE SPEEDS, a Mode 3 loop
    run (100);
    proc.footDown (1); run (50); pump (proc, 150); proc.footUp (1); run (50); pump (proc, 20);
    shot ("main_loop");

    proc.loadPreset (18);                 // AUTO STUTTER, Nightly
    pump (proc, 20);
    proc.keepNow();
    shot ("main_nightly");

    bed->showPedalPage (true);
    shot ("pedal_page");
    return 0;
}
