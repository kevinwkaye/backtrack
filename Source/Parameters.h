// Builds the host parameter list from the one shared list in Engine/ParamSpec.h.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ParamSpec.h"

namespace backtrack
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& s : paramSpecs())
    {
        const ParameterID id { s.id, 2 };
        switch (s.type)
        {
            case PType::Float:
                l.add (std::make_unique<AudioParameterFloat> (id, s.name, NormalisableRange<float> (s.lo, s.hi), s.def));
                break;
            case PType::Int:
                l.add (std::make_unique<AudioParameterInt> (id, s.name, (int) s.lo, (int) s.hi, (int) s.def));
                break;
            case PType::Bool:
                l.add (std::make_unique<AudioParameterBool> (id, s.name, s.def > 0.5f));
                break;
            case PType::Choice:
            {
                StringArray c;
                for (auto* n : s.choices) c.add (n);
                l.add (std::make_unique<AudioParameterChoice> (id, s.name, c, (int) s.def));
                break;
            }
        }
    }
    return l;
}
} // namespace backtrack
