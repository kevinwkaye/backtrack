#pragma once

#include "PluginProcessor.h"
#include "UI/Components.h"

class BacktrackPanel;

class BacktrackEditor : public juce::AudioProcessorEditor
{
public:
    explicit BacktrackEditor (BacktrackProcessor&);
    ~BacktrackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 1260, kHeight = 700;

private:
    std::unique_ptr<BacktrackPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BacktrackEditor)
};
