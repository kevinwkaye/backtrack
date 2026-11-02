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

static void set (BacktrackProcessor& p, const juce::String& id, float v)
{
    p.apvts.getParameter (id)->setValueNotifyingHost (v);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : ".");
    BacktrackProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setVisible (true);

    auto shot = [&] (const juce::String& name)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::PNGImageFormat png;
        juce::FileOutputStream os (outDir.getChildFile (name + ".png"));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (img, os);
    };

    set (proc, "quant", 1.0f);            // 5ths + 8va
    set (proc, "dir1", 0.86f);
    pump (proc, 50);
    shot ("mode1");

    set (proc, "mode", 1.0f);             // loop
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
    proc.softDown(); pump (proc, 150); proc.softUp(); pump (proc, 20);
    set (proc, "dir1", 0.2f);
    pump (proc, 10);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    shot ("mode3");

    set (proc, "bank", 1.0f);
    set (proc, "mode", 0.5f);             // nightly stutter
    pump (proc, 20);
    shot ("nightly2");
    return 0;
}
