// Parameter IDs and layout for Backtrack.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace backtrack::ids
{
// Front panel
inline const juce::String bank { "bank" }, mode { "mode" }, dir1 { "dir1" }, dir2 { "dir2" }, dir3 { "dir3" },
    lenB { "lenB" }, lenS { "lenS" }, fbk { "fbk" }, rand { "rand" }, mix { "mix" }, quant { "quant" },
    heads { "heads" }, expAssign { "expAssign" }, exp { "exp" }, expOn { "expOn" };
// Q layer, standard
inline const juce::String fbkFilter { "fbkFilter" }, lfoRate { "lfoRate" }, lfoDepth { "lfoDepth" },
    trigSens { "trigSens" }, jump { "jump" }, sliceFade { "sliceFade" }, stepRate { "stepRate" },
    stepVar { "stepVar" }, stepHeads { "stepHeads" }, head1 { "head1" }, head2 { "head2" }, head3 { "head3" };
// Q layer, nightly
inline const juce::String n1Len1 { "n1Len1" }, n1Len2 { "n1Len2" }, n1Filter { "n1Filter" },
    n2Len1 { "n2Len1" }, n2Len2 { "n2Len2" }, n2Len3 { "n2Len3" }, n2Rep1 { "n2Rep1" }, n2Rep2 { "n2Rep2" },
    n2Rep3 { "n2Rep3" }, n2Vol1 { "n2Vol1" }, n2Vol2 { "n2Vol2" }, n2Vol3 { "n2Vol3" }, n3Depth1 { "n3Depth1" },
    n3Depth2 { "n3Depth2" }, n3Rate1 { "n3Rate1" }, n3Rate2 { "n3Rate2" }, n3Time1 { "n3Time1" },
    n3Time2 { "n3Time2" }, n3Fbk { "n3Fbk" }, n3FbkTime { "n3FbkTime" };
// Switches
inline const juce::String on { "on" }, transcend { "transcend" }, grit { "grit" }, gritAmt { "gritAmt" },
    boost { "boost" }, spread { "spread" }, trails { "trails" }, latch { "latch" }, sync { "sync" },
    lpLayout { "lpLayout" };
} // namespace backtrack::ids

namespace backtrack
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const String& id, const String& name, float def)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def)); };
    auto i = [&] (const String& id, const String& name, int lo, int hi, int def)
    { l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto b = [&] (const String& id, const String& name, bool def)
    { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto c = [&] (const String& id, const String& name, StringArray choices, int def)
    { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def)); };

    c (ids::bank, "Firmware", { "Standard", "Nightly" }, 0);
    c (ids::mode, "Mode", { "Delay", "Slice", "Loop" }, 0);
    f (ids::dir1, "DIR 1", 0.75f);
    f (ids::dir2, "DIR 2", 0.25f);
    f (ids::dir3, "DIR 3", 0.875f);
    i (ids::lenB, "LEN B", 0, 9, 5);
    f (ids::lenS, "LEN S", 0.45f);
    f (ids::fbk, "FBK", 0.35f);
    f (ids::rand, "RAND", 0.3f);
    f (ids::mix, "Mix", 0.7f);
    c (ids::quant, "Quantise", { "None", "Chromatic", "Whole tone", "Dim triad", "Aug triad", "5ths + 8va" }, 0);
    i (ids::heads, "Heads", 1, 3, 3);
    c (ids::expAssign, "EXP assign", { "Knob 1", "Knob 2", "Knob 3" }, 0);
    f (ids::exp, "EXP", 0.5f);
    b (ids::expOn, "EXP active", false);

    f (ids::fbkFilter, "FBK filter", 1.0f);
    f (ids::lfoRate, "LFO rate", 0.3f);
    f (ids::lfoDepth, "LFO depth", 0.0f);
    f (ids::trigSens, "Trigger sensitivity", 1.0f);
    f (ids::jump, "Jump", 0.5f);
    f (ids::sliceFade, "Slice fade", 0.2f);
    f (ids::stepRate, "Step rate", 0.45f);
    f (ids::stepVar, "Step variance", 0.4f);
    f (ids::stepHeads, "Step heads", 0.0f);
    f (ids::head1, "Head 1 level", 1.0f);
    f (ids::head2, "Head 2 level", 0.8f);
    f (ids::head3, "Head 3 level", 0.7f);

    i (ids::n1Len1, "Ct5t5 length 1", 0, 9, 5);
    i (ids::n1Len2, "Ct5t5 length 2", 0, 9, 6);
    f (ids::n1Filter, "Ct5t5 filter", 0.8f);
    f (ids::n2Len1, "Stutter length 1", 0.3f);
    f (ids::n2Len2, "Stutter length 2", 0.45f);
    f (ids::n2Len3, "Stutter length 3", 0.6f);
    i (ids::n2Rep1, "Stutter repeats 1", 2, 16, 4);
    i (ids::n2Rep2, "Stutter repeats 2", 2, 16, 6);
    i (ids::n2Rep3, "Stutter repeats 3", 2, 16, 8);
    f (ids::n2Vol1, "Stutter level 1", 1.0f);
    f (ids::n2Vol2, "Stutter level 2", 0.8f);
    f (ids::n2Vol3, "Stutter level 3", 0.7f);
    f (ids::n3Depth1, "Vibrato depth 1", 0.25f);
    f (ids::n3Depth2, "Vibrato depth 2", 0.6f);
    f (ids::n3Rate1, "Vibrato rate 1", 0.3f);
    f (ids::n3Rate2, "Vibrato rate 2", 0.5f);
    f (ids::n3Time1, "Vibrato time 1", 0.2f);
    f (ids::n3Time2, "Vibrato time 2", 0.45f);
    f (ids::n3Fbk, "Feedback head level", 0.4f);
    f (ids::n3FbkTime, "Feedback head time", 0.6f);

    b (ids::on, "On", true);
    b (ids::transcend, "Transcend", false);
    b (ids::grit, "Grit", true);
    f (ids::gritAmt, "Grit amount", 0.3f);
    b (ids::boost, "Boost", true);
    b (ids::spread, "Spread", true);
    b (ids::trails, "Trails", false);
    b (ids::latch, "Latch", false);
    b (ids::sync, "Sync", false);
    c (ids::lpLayout, "Launchpad layout", { "User 2 (X-Y)", "User 1 (drum rack)" }, 0);
    return l;
}
} // namespace backtrack
