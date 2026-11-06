#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ParamSpec.h"
#include "Parameters.h"
#include "PedalControl.h"
#include "Keeper.h"
#include <map>

// Backtrack is played from four footswitches and an expression pedal, with an on-screen
// button for every footswitch job.
class BacktrackProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    BacktrackProcessor();
    ~BacktrackProcessor() override;

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

    juce::AudioProcessorValueTreeState apvts;

    // ---- the panel (message thread) ----
    void footDown (int sw);                 // on-screen footswitches behave exactly like the real ones
    void footUp (int sw);
    void setPlain (const juce::String& id, float plain);
    void setNorm (const juce::String& id, float v01);
    void togglePlain (const juce::String& id);
    void touchPanel();                      // touching the panel takes the footswitches
    void softTap() { cmds.fetch_or (CmdDown | CmdUp); }   // a quick press of the soft switch
    void stopAndClear() { cmds.fetch_or (CmdClear); }
    juce::String expressionRangeText (bool toe) const;

    // presets: twenty factory ones, then your own
    int numPresets() const { return (int) backtrack::factoryPresets().size() + userPresets.size(); }
    juce::String presetName (int i) const;
    juce::String presetHint (int i) const;
    bool presetIsUser (int i) const { return i >= (int) backtrack::factoryPresets().size(); }
    void loadPreset (int i);
    void stepPreset (int direction);
    void saveUserPreset();
    int currentPreset() const { return preset; }
    bool presetEdited() const { return edited.load(); }
    static juce::File userPresetFolder();

    // footswitch and expression setup
    PedalSettings& pedals() { return settings; }
    void saveSettings();
    void learnFootswitch (int sw);
    void learnExpression();
    void learnKeeper();
    void calibrateExpression();
    void resetFootswitches();
    enum { LayoutBacktrack = 0, LayoutClassic = 1 };
    int footLayout() const { return settings.backtrackLayout; }
    void setFootLayout (int layout);
    void setFootDevice (const juce::String& device);
    bool pedalsDirect() const { return direct->connected(); }
    juce::String footInputStatus() const { return direct->status(); }
    bool isLearning() const { return learnTarget.load() != LearnNone; }
    int learningWhat() const { return learnTarget.load(); }
    bool isCalibrating() const { return calibrating; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };
    void handleDirectMidi (const juce::MidiMessage&);   // MIDI thread
    std::atomic<bool> hostFeedsPedals { false };
    bool hasFootFocus() const;

    // Keeper
    void keepNow();
    juce::File exportKeepers();
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }

    // ---- what the panel and Pebble show ----
    struct UiStatus
    {
        std::atomic<int> state { 0 };
        std::atomic<float> loopSeconds { 0 }, recFraction { 0 }, playFraction { 0 }, speed1 { 1 },
            bufferSeconds { 0 }, inLevel { 0 }, outLevel { 0 }, exp { 0 };
        std::atomic<uint32_t> triggers { 0 }, clips { 0 };
        std::atomic<double> bpm { 120.0 };
        std::atomic<bool> hostTempo { false };
    } ui;
    std::atomic<int> uiFootPulse { 0 }, uiKeepPulse { 0 }, uiFootDown { 0 }, uiPresetPulse { 0 };
    juce::String uiMessage;
    double uiMessageAt = 0.0;
    const char* footJobTap (int sw) const;
    const char* footJobHold (int sw) const;

private:
    void timerCallback() override;
    void readParams (int numSamples);
    void handleMidi (const juce::MidiMessage&, bool fromDevice);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void say (const juce::String&);
    void applyEngineCommands();
    void applyPendingLoop();
    void loadUserPresets();
    void markEdited();

    backtrack::Engine engine;
    backtrack::Params params;
    Keeper keeper;
    juce::AudioBuffer<float> scratch;

    std::map<juce::String, std::atomic<float>*> raw;
    float get (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }

    // engine commands from the message thread, run at the start of the next block
    enum Cmd { CmdDown = 1, CmdUp = 2, CmdClear = 4, CmdCancel = 8 };
    std::atomic<int> cmds { 0 };

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; };
    enum PendingKind { FootPress, FootRelease, Expression, Learned, KeepCC };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    juce::AbstractFifo directFifo { 512 };
    std::array<Pending, 512> directPending;
    juce::SpinLock directWriteLock;
    juce::SharedResourcePointer<DirectMidiInput> direct;
    void queue (int kind, int a);

    PedalSettings settings;
    juce::Time settingsStamp;
    double nextSettingsCheck = 0.0;
    bool wasDirect = false;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = 0.0;

    // footswitch state
    bool reverseOnDown = false, bDownFired = false;        // C's press flipped the heads; a combo puts them back
    bool comboABPending = false, comboCDPending = false;
    float expSmoothed = 0.0f;

    // presets
    juce::Array<juce::File> userPresets;
    int preset = 0;
    std::atomic<bool> edited { false }, applyingPreset { false };
    struct EditWatch : juce::AudioProcessorValueTreeState::Listener
    {
        BacktrackProcessor& owner;
        explicit EditWatch (BacktrackProcessor& o) : owner (o) {}
        void parameterChanged (const juce::String& id, float) override;
    };
    EditWatch editWatch { *this };
    std::vector<std::atomic<float>*> specRaw;
    std::atomic<float>* rExp = nullptr, * rExpOn = nullptr, * rExpTarget = nullptr, * rExpLo = nullptr, * rExpHi = nullptr;   // in paramSpecs() order, for the audio thread

    // a loop from the Live set, restored on the audio thread
    juce::MemoryBlock pendingLoop;
    int pendingLoopMode = -1, pendingLoopLen = 0;
    std::atomic<bool> loopToRestore { false };
    double currentSampleRate = 48000.0, savedLoopSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BacktrackProcessor)
};
