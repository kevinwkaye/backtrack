#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/BacktrackEngine.h"
#include "Parameters.h"

class BacktrackProcessor : public juce::AudioProcessor
{
public:
    BacktrackProcessor();
    ~BacktrackProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Backtrack"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Soft switch and pads from the editor
    void softDown()  { uiDown = true; }
    void softUp()    { uiUp = true; }
    void stopClear() { uiClear = true; }
    void toggleHold() { uiToggleHold = true; }

    // What the editor reads each frame
    struct UiStatus
    {
        std::atomic<int> state { 0 };
        std::atomic<float> loopSeconds { 0 }, recFraction { 0 }, playFraction { 0 }, speed1 { 1 },
            bufferSeconds { 0 }, inLevel { 0 }, outLevel { 0 };
        std::atomic<uint32_t> triggers { 0 }, clips { 0 };
        std::atomic<double> bpm { 120.0 };
    } ui;

    juce::AudioProcessorValueTreeState apvts;

private:
    backtrack::Engine engine;
    backtrack::Params params;
    std::atomic<bool> uiDown { false }, uiUp { false }, uiClear { false }, uiToggleHold { false };
    juce::AudioBuffer<float> scratch;

    // Pending loop restore (set from the message thread, applied in prepare/process)
    juce::MemoryBlock pendingLoop;
    int pendingLoopMode = -1, pendingLoopLen = 0;
    std::atomic<bool> loopToRestore { false };
    double currentSampleRate = 48000.0;
    double savedLoopSampleRate = 48000.0;

    std::map<juce::String, std::atomic<float>*> raw;
    float get (const juce::String& id) const { return raw.at (id)->load(); }

    void readParams();
    void handleMidi (const juce::MidiBuffer& midi);
    void handlePad (int row, int col, bool down);
    void setParam (const juce::String& id, float normalised);
    void toggleParam (const juce::String& id);
    void applyPendingLoop();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BacktrackProcessor)
};
