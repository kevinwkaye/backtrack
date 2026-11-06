#pragma once

#include "PluginProcessor.h"
#include "UI/Components.h"

class MainPage;
class PedalPage;

class BacktrackEditor : public juce::AudioProcessorEditor
{
public:
    explicit BacktrackEditor (BacktrackProcessor&);
    ~BacktrackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void showPedalPage (bool show);

    static constexpr int kWidth = 1260, kHeight = 700;

private:
    std::unique_ptr<MainPage> main;
    std::unique_ptr<PedalPage> pedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BacktrackEditor)
};
