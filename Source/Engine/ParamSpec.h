// Backtrack's single parameter list, factory presets and expression mapping.
// Plain C++17 with no JUCE, so the plugin, the presets and the offline tests all share
// exactly one definition of every parameter.
#pragma once

#include "BacktrackEngine.h"
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace backtrack
{
enum class PType { Float, Int, Bool, Choice };

struct ParamSpec
{
    const char* id;
    const char* name;
    PType type;
    float lo, hi, def;
    std::vector<const char*> choices {};
};

enum ExpTarget
{
    ExpDir1 = 0, ExpDir2, ExpDir3, ExpAllHeads, ExpLenB, ExpLenS, ExpFbk, ExpRand, ExpMix,
    ExpLfoDepth, ExpSliceFade, ExpStepVar, ExpStutterLen, ExpVibDepth, ExpCount
};

inline const std::vector<const char*>& expTargetNames()
{
    static const std::vector<const char*> n { "DIR 1", "DIR 2", "DIR 3", "ALL HEADS", "LEN B", "LEN S", "FBK", "RAND",
                                              "MIX", "LFO DEPTH", "SLICE FADE", "STEP VAR", "STUTTER LEN", "VIB DEPTH" };
    return n;
}

inline const std::vector<ParamSpec>& paramSpecs()
{
    using P = PType;
    static const std::vector<ParamSpec> s {
        // Front panel
        { "bank", "Firmware", P::Choice, 0, 1, 0, { "Standard", "Nightly" } },
        { "mode", "Mode", P::Choice, 0, 2, 0, { "Delay", "Slice", "Loop" } },
        { "dir1", "DIR 1", P::Float, 0, 1, 0.75f },
        { "dir2", "DIR 2", P::Float, 0, 1, 0.25f },
        { "dir3", "DIR 3", P::Float, 0, 1, 1.0f },
        { "lenB", "LEN B", P::Int, 0, 9, 5 },
        { "lenS", "LEN S", P::Float, 0, 1, 0.45f },
        { "fbk", "FBK", P::Float, 0, 1, 0.0f },
        { "rand", "RAND", P::Float, 0, 1, 0.0f },
        { "mix", "Mix", P::Float, 0, 1, 0.5f },
        { "quant", "Quantise", P::Choice, 0, 5, 0, { "None", "Chromatic", "Whole tone", "Dim triad", "Aug triad", "5ths + 8va" } },
        { "heads", "Heads", P::Int, 1, 3, 3 },
        { "reverse", "Reverse", P::Bool, 0, 1, 0 },

        // Expression
        { "expTarget", "Expression target", P::Choice, 0, (float) (ExpCount - 1), 0, expTargetNames() },
        { "expLo", "Expression heel", P::Float, 0, 1, 0.75f },
        { "expHi", "Expression toe", P::Float, 0, 1, 1.0f },
        { "exp", "Expression", P::Float, 0, 1, 0.0f },
        { "expOn", "Expression active", P::Bool, 0, 1, 0 },

        // Q layer, standard
        { "fbkFilter", "FBK filter", P::Float, 0, 1, 1.0f },
        { "lfoRate", "LFO rate", P::Float, 0, 1, 0.3f },
        { "lfoDepth", "LFO depth", P::Float, 0, 1, 0.0f },
        { "trigSens", "Trigger sensitivity", P::Float, 0, 1, 1.0f },
        { "jump", "Jump", P::Float, 0, 1, 0.5f },
        { "sliceFade", "Slice fade", P::Float, 0, 1, 0.2f },
        { "stepRate", "Step rate", P::Float, 0, 1, 0.45f },
        { "stepVar", "Step variance", P::Float, 0, 1, 0.4f },
        { "stepHeads", "Step heads", P::Float, 0, 1, 0.0f },
        { "head1", "Head 1 level", P::Float, 0, 1, 1.0f },
        { "head2", "Head 2 level", P::Float, 0, 1, 0.8f },
        { "head3", "Head 3 level", P::Float, 0, 1, 0.7f },
        { "dubFade", "Dub fade", P::Float, 0, 1, 0.9f },
        { "lowKeepHz", "Low keep frequency", P::Float, 40, 250, 100 },

        // Q layer, nightly
        { "n1Len1", "Ct5t5 length 1", P::Int, 0, 9, 5 },
        { "n1Len2", "Ct5t5 length 2", P::Int, 0, 9, 6 },
        { "n1Filter", "Ct5t5 filter", P::Float, 0, 1, 0.8f },
        { "n2Len1", "Stutter length 1", P::Float, 0, 1, 0.3f },
        { "n2Len2", "Stutter length 2", P::Float, 0, 1, 0.45f },
        { "n2Len3", "Stutter length 3", P::Float, 0, 1, 0.6f },
        { "n2Rep1", "Stutter repeats 1", P::Int, 2, 16, 4 },
        { "n2Rep2", "Stutter repeats 2", P::Int, 2, 16, 6 },
        { "n2Rep3", "Stutter repeats 3", P::Int, 2, 16, 8 },
        { "n2Vol1", "Stutter level 1", P::Float, 0, 1, 1.0f },
        { "n2Vol2", "Stutter level 2", P::Float, 0, 1, 0.8f },
        { "n2Vol3", "Stutter level 3", P::Float, 0, 1, 0.7f },
        { "n3Depth1", "Vibrato depth 1", P::Float, 0, 1, 0.25f },
        { "n3Depth2", "Vibrato depth 2", P::Float, 0, 1, 0.6f },
        { "n3Rate1", "Vibrato rate 1", P::Float, 0, 1, 0.3f },
        { "n3Rate2", "Vibrato rate 2", P::Float, 0, 1, 0.5f },
        { "n3Time1", "Vibrato time 1", P::Float, 0, 1, 0.2f },
        { "n3Time2", "Vibrato time 2", P::Float, 0, 1, 0.45f },
        { "n3Fbk", "Feedback head level", P::Float, 0, 1, 0.4f },
        { "n3FbkTime", "Feedback head time", P::Float, 0, 1, 0.6f },

        // Switches
        { "on", "On", P::Bool, 0, 1, 1 },
        { "transcend", "Transcend", P::Bool, 0, 1, 0 },
        { "grit", "Grit", P::Choice, 0, 2, 1, { "Off", "24-BIT", "12-BIT" } },
        { "gritAmt", "Grit amount", P::Float, 0, 1, 0.3f },
        { "boost", "Boost", P::Bool, 0, 1, 0 },
        { "spread", "Spread", P::Bool, 0, 1, 1 },
        { "trails", "Trails", P::Bool, 0, 1, 0 },
        { "latch", "Latch", P::Bool, 0, 1, 0 },
        { "sync", "Sync", P::Bool, 0, 1, 0 },
        { "lock", "Lock", P::Bool, 0, 1, 1 },
        { "lowKeep", "Low keep", P::Bool, 0, 1, 0 },
    };
    return s;
}

inline const ParamSpec* findSpec (const char* id)
{
    for (auto& s : paramSpecs())
        if (std::strcmp (s.id, id) == 0) return &s;
    return nullptr;
}

// Writes one parameter (plain value: choice index, int, 0/1 for bools) into the engine's Params
inline void setParam (Params& p, const std::string& id, float v)
{
    const int iv = (int) std::lround (v);
    const bool bv = v > 0.5f;
    if      (id == "bank") p.bank = iv;
    else if (id == "mode") p.mode = iv;
    else if (id == "dir1") p.dir1 = v;
    else if (id == "dir2") p.dir2 = v;
    else if (id == "dir3") p.dir3 = v;
    else if (id == "lenB") p.lenB = iv;
    else if (id == "lenS") p.lenS = v;
    else if (id == "fbk") p.fbk = v;
    else if (id == "rand") p.rand = v;
    else if (id == "mix") p.mix = v;
    else if (id == "quant") p.quant = iv;
    else if (id == "heads") p.heads = iv;
    else if (id == "reverse") p.reverse = bv;
    else if (id == "fbkFilter") p.fbkFilter = v;
    else if (id == "lfoRate") p.lfoRate = v;
    else if (id == "lfoDepth") p.lfoDepth = v;
    else if (id == "trigSens") p.trigSens = v;
    else if (id == "jump") p.jump = v;
    else if (id == "sliceFade") p.sliceFade = v;
    else if (id == "stepRate") p.stepRate = v;
    else if (id == "stepVar") p.stepVar = v;
    else if (id == "stepHeads") p.stepHeads = v;
    else if (id == "head1") p.headVol[0] = v;
    else if (id == "head2") p.headVol[1] = v;
    else if (id == "head3") p.headVol[2] = v;
    else if (id == "dubFade") p.dubFade = v;
    else if (id == "lowKeepHz") p.lowKeepHz = v;
    else if (id == "n1Len1") p.n1Len1 = iv;
    else if (id == "n1Len2") p.n1Len2 = iv;
    else if (id == "n1Filter") p.n1Filter = v;
    else if (id == "n2Len1") p.n2Len[0] = v;
    else if (id == "n2Len2") p.n2Len[1] = v;
    else if (id == "n2Len3") p.n2Len[2] = v;
    else if (id == "n2Rep1") p.n2Rep[0] = iv;
    else if (id == "n2Rep2") p.n2Rep[1] = iv;
    else if (id == "n2Rep3") p.n2Rep[2] = iv;
    else if (id == "n2Vol1") p.n2Vol[0] = v;
    else if (id == "n2Vol2") p.n2Vol[1] = v;
    else if (id == "n2Vol3") p.n2Vol[2] = v;
    else if (id == "n3Depth1") p.n3Depth[0] = v;
    else if (id == "n3Depth2") p.n3Depth[1] = v;
    else if (id == "n3Rate1") p.n3Rate[0] = v;
    else if (id == "n3Rate2") p.n3Rate[1] = v;
    else if (id == "n3Time1") p.n3Time[0] = v;
    else if (id == "n3Time2") p.n3Time[1] = v;
    else if (id == "n3Fbk") p.n3Fbk = v;
    else if (id == "n3FbkTime") p.n3FbkTime = v;
    else if (id == "on") p.on = bv;
    else if (id == "transcend") p.transcend = bv;
    else if (id == "grit") p.grit = iv;
    else if (id == "gritAmt") p.gritAmount = v;
    else if (id == "boost") p.boost = bv;
    else if (id == "spread") p.spread = bv;
    else if (id == "trails") p.trails = bv;
    else if (id == "latch") p.latch = bv;
    else if (id == "sync") p.sync = bv;
    else if (id == "lock") p.lock = bv;
    else if (id == "lowKeep") p.lowKeep = bv;
}

inline Params defaultParams()
{
    Params p;
    for (auto& s : paramSpecs()) setParam (p, s.id, s.def);
    return p;
}

// The expression pedal moves one control between the preset's heel and toe settings.
// Heel and toe are stored on the control's own 0..1 scale (DIR knobs: 0.5 stop, 0.75 unity).
inline void applyExpression (Params& p, int target, float heel, float toe, float pedal)
{
    const float v = heel + (toe - heel) * std::clamp (pedal, 0.0f, 1.0f);
    switch (target)
    {
        case ExpDir1: p.dir1 = v; break;
        case ExpDir2: p.dir2 = v; break;
        case ExpDir3: p.dir3 = v; break;
        case ExpAllHeads:
        {
            const float d = v - heel;
            p.dir1 = std::clamp (p.dir1 + d, 0.0f, 1.0f);
            p.dir2 = std::clamp (p.dir2 + d, 0.0f, 1.0f);
            p.dir3 = std::clamp (p.dir3 + d, 0.0f, 1.0f);
            break;
        }
        case ExpLenB: p.lenB = std::clamp ((int) std::lround (v * 9.0f), 0, 9); break;
        case ExpLenS: p.lenS = v; break;
        case ExpFbk: p.fbk = v; break;
        case ExpRand: p.rand = v; break;
        case ExpMix: p.mix = v; break;
        case ExpLfoDepth: p.lfoDepth = v; break;
        case ExpSliceFade: p.sliceFade = v; break;
        case ExpStepVar: p.stepVar = v; break;
        case ExpStutterLen:
            p.n2Len = { v, std::min (1.0f, v * 1.25f), std::min (1.0f, v * 1.5f) };
            break;
        case ExpVibDepth: p.n3Depth[0] = v; break;
        default: break;
    }
}

//==============================================================================
// Factory presets. Each one starts from the defaults, sets what it needs, and says what
// the expression pedal does and one thing to try.
struct Preset
{
    std::string name;
    std::string hint;
    std::vector<std::pair<std::string, float>> values;
};

inline float dirFor (float speed) { return speedToDir (speed); }
inline float semis (float s) { return std::pow (2.0f, s / 12.0f); }

inline const std::vector<Preset>& factoryPresets()
{
    auto exp = [] (int target, float heel, float toe) -> std::vector<std::pair<std::string, float>>
    { return { { "expTarget", (float) target }, { "expLo", heel }, { "expHi", toe } }; };
    auto join = [] (std::vector<std::pair<std::string, float>> a, const std::vector<std::pair<std::string, float>>& b)
    { a.insert (a.end(), b.begin(), b.end()); return a; };
    auto dir = [] (float speed) { return dirFor (speed); };

    static const std::vector<Preset> p {
        { "INIT", "Start here. Every control sits at its plain setting.",
          join ({ { "mode", 0 }, { "dir1", dir (1) }, { "fbk", 0 }, { "mix", 0.5f }, { "lenB", 5 } }, exp (ExpDir1, dir (1), dir (2))) },
        { "TAPE ECHO", "Rock toe down to swell the echoes into runaway.",
          join ({ { "mode", 0 }, { "fbk", 0.35f }, { "mix", 0.35f }, { "lenB", 5 }, { "fbkFilter", 0.6f } }, exp (ExpFbk, 0.2f, 0.85f)) },
        { "REVERSE ECHO", "Play a phrase, then fade the backwards repeats in.",
          join ({ { "mode", 0 }, { "quant", 1 }, { "dir1", dir (-1) }, { "fbk", 0.3f }, { "mix", 0.5f }, { "lenB", 6 } }, exp (ExpMix, 0.0f, 0.6f)) },
        { "PING PONG", "Tap C to send the repeats backwards.",
          join ({ { "mode", 0 }, { "fbk", 0.45f }, { "mix", 0.4f }, { "lenB", 4 }, { "spread", 1 } }, exp (ExpFbk, 0.2f, 0.8f)) },
        { "OCTAVE ORGAN", "Hold a chord and it turns into an organ.",
          join ({ { "mode", 0 }, { "quant", 5 }, { "dir1", dir (2) }, { "fbk", 0.75f }, { "mix", 0.55f }, { "lenB", 1 },
                  { "lfoDepth", 0.06f }, { "lfoRate", 0.35f }, { "fbkFilter", 0.75f } }, exp (ExpMix, 0.0f, 0.8f)) },
        { "FIFTHS CLIMB", "Play single notes and let them climb in fifths.",
          join ({ { "mode", 0 }, { "quant", 5 }, { "dir1", dir (semis (7)) }, { "fbk", 0.65f }, { "mix", 0.45f }, { "lenB", 4 },
                  { "fbkFilter", 0.8f } }, exp (ExpFbk, 0.3f, 0.9f)) },
        { "BIRDS", "Short high notes chirp. Rock the pedal to change the song.",
          join ({ { "mode", 0 }, { "dir1", dir (1.75f) }, { "fbk", 0.4f }, { "mix", 0.5f }, { "lenB", 0 }, { "lfoDepth", 0.15f } },
                exp (ExpDir1, dir (1.25f), dir (2))) },
        { "RUNAWAY CHORUS", "Hover near heel for phasing, push for chaos.",
          join ({ { "mode", 0 }, { "fbk", 0.6f }, { "mix", 0.5f }, { "lenB", 2 }, { "lfoDepth", 0.04f } }, exp (ExpDir1, dir (1), dir (1.25f))) },
        { "SUB SHADOW", "Bass: an octave-down ghost follows each note.",
          join ({ { "mode", 0 }, { "quant", 1 }, { "dir1", dir (semis (-12)) }, { "fbk", 0.2f }, { "mix", 0.4f }, { "lenB", 4 },
                  { "lowKeep", 1 }, { "fbkFilter", 0.4f } }, exp (ExpMix, 0.0f, 0.6f)) },
        { "TAPE STOP", "Rock toe down to slow the repeats to a halt.",
          join ({ { "mode", 0 }, { "fbk", 0.3f }, { "mix", 0.5f }, { "lenB", 5 } }, exp (ExpDir1, dir (1), dir (0))) },
        { "STUTTER SLICES", "Hold B for one bar, let go and rock the pedal.",
          join ({ { "mode", 1 }, { "quant", 1 }, { "mix", 0.7f }, { "lenS", 0.3f }, { "rand", 0 } }, exp (ExpLenS, 0.1f, 0.6f)) },
        { "GLITCH SHUFFLE", "Hold B for two bars, then rock the pedal.",
          join ({ { "mode", 1 }, { "quant", 5 }, { "mix", 0.7f }, { "lenS", 0.45f }, { "rand", 0.7f } }, exp (ExpRand, 0.0f, 0.9f)) },
        { "REVERSE GRAINS", "Hold B over a chord. The grains run backwards.",
          join ({ { "mode", 1 }, { "quant", 1 }, { "dir1", dir (-1) }, { "mix", 0.6f }, { "lenS", 0.2f }, { "rand", 0.5f }, { "spread", 1 } },
                exp (ExpDir1, dir (-1), dir (-2))) },
        { "THREE SPEEDS", "Hold B for exactly one bar. The heads stay in time.",
          join ({ { "mode", 2 }, { "quant", 1 }, { "dir1", dir (semis (-12)) }, { "dir2", dir (1) }, { "dir3", dir (2) }, { "mix", 0.8f },
                  { "lock", 1 }, { "heads", 3 } }, exp (ExpMix, 0.3f, 1.0f)) },
        { "FIFTHS LOOP", "Loop a riff, then rock the pedal through the fifths.",
          join ({ { "mode", 2 }, { "quant", 5 }, { "dir1", dir (semis (7)) }, { "dir2", dir (1) }, { "dir3", dir (-0.5f) }, { "mix", 0.7f },
                  { "heads", 3 } }, exp (ExpDir1, dir (1), dir (2))) },
        { "RANDOM STEPS", "Loop two bars and let the steps wander.",
          join ({ { "mode", 2 }, { "quant", 5 }, { "dir1", dir (1) }, { "dir2", dir (semis (7)) }, { "dir3", dir (0.5f) }, { "mix", 0.7f },
                  { "stepHeads", 0.9f }, { "stepRate", 0.6f }, { "heads", 3 } }, exp (ExpStepVar, 0.0f, 0.8f)) },
        { "BACKWARDS BASS", "Bass: loop a groove, then play over it backwards.",
          join ({ { "mode", 2 }, { "quant", 1 }, { "dir1", dir (-1) }, { "heads", 1 }, { "lowKeep", 1 }, { "mix", 0.5f } },
                exp (ExpMix, 0.0f, 0.7f)) },
        { "CROSS FEED", "Single notes bloom into clusters.",
          join ({ { "bank", 1 }, { "mode", 0 }, { "quant", 5 }, { "dir1", dir (semis (7)) }, { "dir2", dir (1) }, { "fbk", 0.55f },
                  { "mix", 0.5f }, { "n1Len1", 5 }, { "n1Len2", 6 } }, exp (ExpDir2, dir (1), dir (2))) },
        { "AUTO STUTTER", "Play a run, then tap B to freeze the stutter.",
          join ({ { "bank", 1 }, { "mode", 1 }, { "mix", 0.6f }, { "n2Len1", 0.4f }, { "n2Len2", 0.5f }, { "n2Len3", 0.6f } },
                exp (ExpStutterLen, 0.1f, 0.7f)) },
        { "VIBRATO DELAY", "Long notes warble in and out of tune.",
          join ({ { "bank", 1 }, { "mode", 2 }, { "quant", 5 }, { "mix", 0.5f }, { "n3Rate1", 0.4f }, { "n3Time1", 0.3f } },
                exp (ExpVibDepth, 0.0f, 1.0f)) },
    };
    return p;
}

// Builds the full parameter set for a preset (every parameter, so nothing lingers from before)
inline std::vector<std::pair<std::string, float>> presetValues (const Preset& pr)
{
    std::vector<std::pair<std::string, float>> out;
    for (auto& s : paramSpecs())
    {
        const std::string id = s.id;
        // the performance switches and global settings stay where you left them
        if (id == "on" || id == "sync" || id == "exp" || id == "expOn" || id == "latch" || id == "trigSens") continue;
        float v = s.def;
        for (auto& kv : pr.values) if (kv.first == id) v = kv.second;
        out.emplace_back (id, v);
    }
    return out;
}

} // namespace backtrack
