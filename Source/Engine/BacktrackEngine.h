// Backtrack DSP engine.
// Pure C++ with no JUCE dependency, so it can be tested offline.
// Recreates the Montreal Assembly Count to 5: standard firmware Modes 1 to 3
// plus the three Nightly firmware modes, in stereo.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace backtrack
{

constexpr float kPi = 3.14159265358979f;

//==============================================================================
// Settings handed to the engine once per block.
struct Params
{
    // Front panel
    int   bank = 0;            // 0 standard, 1 nightly
    int   mode = 0;            // 0 delay, 1 slice, 2 loop
    float dir1 = 0.75f;        // 0..1, 0.5 = stop, 0.75 = unity forwards
    float dir2 = 0.25f;
    float dir3 = 0.75f;
    int   lenB = 5;            // 0..9 doubling steps
    float lenS = 0.5f;         // 0..1
    float fbk  = 0.3f;         // 0..1
    float rand = 0.0f;         // 0..1
    float mix  = 0.7f;         // 0 dry .. 1 wet
    int   quant = 0;           // 0 none .. 5 fifths and octaves
    int   heads = 3;           // 1..3

    // Q layer, standard
    float fbkFilter = 1.0f;    // 0..1, 1 = open
    float lfoRate = 0.3f;      // 0..1
    float lfoDepth = 0.0f;     // 0..1
    float trigSens = 1.0f;     // 0..1, 1 = off
    float jump = 0.5f;         // 0..1 of buffer length
    float sliceFade = 0.2f;    // 0..1
    float stepRate = 0.4f;     // 0..1
    float stepVar = 0.3f;      // 0..1
    float stepHeads = 0.0f;    // 0..1, below 0.25 = sequencer off
    std::array<float, 3> headVol { 1.0f, 1.0f, 1.0f };

    // Q layer, nightly
    int   n1Len1 = 5, n1Len2 = 6;
    float n1Filter = 0.8f;
    std::array<float, 3> n2Len { 0.3f, 0.45f, 0.6f };
    std::array<int,   3> n2Rep { 4, 4, 4 };
    std::array<float, 3> n2Vol { 1.0f, 1.0f, 1.0f };
    std::array<float, 2> n3Depth { 0.2f, 0.4f };
    std::array<float, 2> n3Rate  { 0.3f, 0.5f };
    std::array<float, 2> n3Time  { 0.2f, 0.45f };
    float n3Fbk = 0.4f;
    float n3FbkTime = 0.6f;

    // Switches
    bool  on = true;
    bool  transcend = false;
    bool  grit = true;
    float gritAmount = 0.3f;
    bool  boost = true;
    bool  spread = true;
    bool  trails = false;
    bool  latch = false;
    bool  sync = false;

    // Host clock
    double bpm = 120.0;
    bool   hostPlaying = false;
    double ppqAtBlockStart = 0.0;
};

// What the editor shows. Copied out after each block.
struct Status
{
    int   state = 0;           // see State
    float loopSeconds = 0.0f;
    float recFraction = 0.0f;  // 0..1 of the record limit
    float playFraction = 0.0f; // head 1 position in the loop
    float speed1 = 1.0f;       // effective DIR 1 speed after quantise and LFO
    float bufferSeconds = 0.0f;
    uint32_t triggerCount = 0;
    uint32_t clipCount = 0;
    float inputLevel = 0.0f;
    float outputLevel = 0.0f;
};

enum State
{
    Listening = 0, Held, Recording, Playing, Empty, Frozen, Armed
};

//==============================================================================
// Quantise tables
inline bool scaleHas (int quant, int semis)
{
    const int a = std::abs (semis);
    switch (quant)
    {
        case 1: return true;
        case 2: return (a % 2) == 0;
        case 3: return (a % 3) == 0;
        case 4: return (a % 4) == 0;
        case 5: return a == 0 || a == 7 || a == 12;
        default: return true;
    }
}

// DIR knob (0..1) to read speed (-2..2). Noon stops the head.
inline float dirToSpeed (float knob, int quant)
{
    const float s = (std::clamp (knob, 0.0f, 1.0f) - 0.5f) * 4.0f;
    if (quant <= 0)
        return s;

    const float a = std::fabs (s);
    if (a < 0.2f)
        return 0.0f;

    const float sign = s < 0.0f ? -1.0f : 1.0f;
    const float semis = std::clamp (12.0f * std::log2 (std::max (a, 0.5f)), -12.0f, 12.0f);
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = -12; i <= 12; ++i)
    {
        if (! scaleHas (quant, i))
            continue;
        const float d = std::fabs ((float) i - semis);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return sign * std::pow (2.0f, (float) best / 12.0f);
}

// Snap a semitone depth (0..12) to the quantise scale.
inline float quantiseSemis (float semis, int quant)
{
    if (quant <= 0)
        return semis;
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = 0; i <= 12; ++i)
    {
        if (! scaleHas (quant, i))
            continue;
        const float d = std::fabs ((float) i - semis);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return (float) best;
}

// LEN B step (0..9) to seconds, free running: 8 s down to 15.6 ms.
inline double lenBSeconds (int step) { return 8.0 / std::pow (2.0, 9 - std::clamp (step, 0, 9)); }

// LEN B step to beats when synced: 4 bars down to 1/128 note.
inline double lenBBeats (int step) { return 16.0 / std::pow (2.0, 9 - std::clamp (step, 0, 9)); }

// LEN S synced note values, in beats.
inline const std::vector<double>& sliceBeatTable()
{
    static const std::vector<double> t { 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 3.0 / 16, 1.0 / 4, 1.0 / 3, 3.0 / 8,
                                         1.0 / 2, 2.0 / 3, 3.0 / 4, 1.0, 4.0 / 3, 3.0 / 2, 2.0, 3.0, 4.0 };
    return t;
}

inline const std::vector<double>& stepBeatTable()
{
    static const std::vector<double> t { 4.0, 2.0, 1.0, 0.5, 0.25, 1.0 / 6 };
    return t;
}

inline float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, std::clamp (v, 0.0f, 1.0f)); }

//==============================================================================
// Small helpers
struct Rng
{
    uint32_t s = 0x9e3779b9u;
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / (float) 0x1000000; }
};

struct OnePole
{
    float z = 0.0f;
    float process (float x, float a) { z += a * (x - z); return z; }
};

struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
    float process (float x, float r = 0.9995f) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

inline float softClip (float x) { return std::tanh (x); }

// Hermite read from a circular region [base, base + len).
inline float readHermite (const float* buf, int base, int len, double pos)
{
    if (len < 4)
        return 0.0f;
    double p = std::fmod (pos, (double) len);
    if (p < 0.0) p += len;
    const int i1 = (int) p;
    const float t = (float) (p - i1);
    const int i0 = (i1 - 1 + len) % len, i2 = (i1 + 1) % len, i3 = (i1 + 2) % len;
    const float y0 = buf[base + i0], y1 = buf[base + i1], y2 = buf[base + i2], y3 = buf[base + i3];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

inline double wrap (double p, double len)
{
    if (len <= 0.0) return 0.0;
    p = std::fmod (p, len);
    return p < 0.0 ? p + len : p;
}

//==============================================================================
class Engine
{
public:
    static constexpr double kMaxSeconds = 8.0;
    static constexpr double kStutterSeconds = 2.5;

    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        cap = (int) (kMaxSeconds * sr) + 8;
        half = cap / 2;
        for (auto* b : { &buf[0], &buf[1], &bufB[0], &bufB[1] })
            b->assign ((size_t) cap, 0.0f);
        stutCap = (int) (kStutterSeconds * sr) + 8;
        for (auto& u : stut)
            for (auto& c : u.buf)
                c.assign ((size_t) stutCap, 0.0f);
        resetAll();
    }

    // Soft switch, called from the message or MIDI side via the processor.
    void softDown() { pendingDown = true; }
    void softUp()   { pendingUp = true; }

    // Clear everything (STOP pad or tap in modes 2 and 3).
    void stopAndClear() { pendingClear = true; }

    // Latching hold: freezes Mode 1 or the Nightly modes until pressed again.
    void toggleHold() { pendingToggleHold = true; }

    const Status& getStatus() const { return status; }

    // Loop data for saving with the Live set.
    int  getLoopLength() const { return (mode3Playing || m2Playing) ? loopLen : 0; }
    int  getLoopBase() const { return playBase; }
    const std::vector<float>& getBuffer (int ch) const { return buf[ch]; }
    std::vector<float>& getBufferForRestore (int ch) { return buf[ch]; }
    int  getCapacity() const { return cap; }

    void restoreLoop (int mode, int length)
    {
        length = std::clamp (length, 0, cap);
        if (length < 64) return;
        lastMode = mode; lastBank = 0;
        loopLen = length;
        playBase = 0;
        if (mode == 1) { m2Playing = true; m2PlayLen = length; startSlice (Params{}); }
        else if (mode == 2) { mode3Playing = true; for (auto& h : headPos) h = 0.0; dubPos = 0.0; }
    }

    //==========================================================================
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p)
    {
        handleModeChange (p);

        // Block-rate values
        beatSamples = (p.bpm > 1.0 ? 60.0 / p.bpm : 0.5) * sr;
        const float lfoHz = expMap (p.lfoRate, 0.05f, 4.0f);
        const float fbkCut = expMap (p.fbkFilter, 300.0f, 18000.0f);
        const float fbkA = 1.0f - std::exp (-2.0f * kPi * fbkCut / (float) sr);
        const float gritCut = 20000.0f - 9000.0f * p.gritAmount;
        const float gritA = 1.0f - std::exp (-2.0f * kPi * std::min (gritCut, 0.45f * (float) sr) / (float) sr);
        const float gritSteps = std::pow (2.0f, 16.0f - 4.0f * p.gritAmount);
        const float slewA = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        const float boostGain = p.boost ? 1.4125f : 1.0f; // +3 dB, the pedal's jump when engaged
        const float envA = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
        const float trigThresh = std::pow (10.0f, (-60.0f + 57.0f * p.trigSens) / 20.0f);
        const bool  trigOn = p.trigSens < 0.98f;

        float peakIn = 0.0f, peakOut = 0.0f;
        const double ppqPerSample = (p.bpm / 60.0) / sr;

        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            peakIn = std::max (peakIn, std::max (std::fabs (xl), std::fabs (xr)));

            handleSoftSwitch (p, i, ppqPerSample);

            // Input envelope for the Mode 1 trigger
            env += envA * (std::max (std::fabs (xl), std::fabs (xr)) - env);
            if (trigOn && p.bank == 0 && p.mode == 0)
            {
                if (env > trigThresh && ! aboveThresh) { aboveThresh = true; trigger (p); }
                else if (env < trigThresh * 0.5f) aboveThresh = false;
            }

            // With trails on and the pedal bypassed, the buffers keep sounding but hear nothing new
            const float il = (p.on ? xl : (p.trails ? 0.0f : xl));
            const float ir = (p.on ? xr : (p.trails ? 0.0f : xr));

            float wl = 0.0f, wr = 0.0f;
            if (p.bank == 0)
            {
                if (p.mode == 0)      mode1 (il, ir, wl, wr, p, lfoHz, fbkA, slewA);
                else if (p.mode == 1) mode2 (il, ir, wl, wr, p, slewA);
                else                  mode3 (il, ir, wl, wr, p, slewA);
            }
            else
            {
                if (p.mode == 0)      nightly1 (il, ir, wl, wr, p, slewA);
                else if (p.mode == 1) nightly2 (il, ir, wl, wr, p);
                else                  nightly3 (il, ir, wl, wr, p);
            }

            // Wet colour: DC block, converter grit
            wl = dcL.process (wl);
            wr = dcR.process (wr);
            if (p.grit)
            {
                wl = gritLpL.process (wl, gritA);
                wr = gritLpR.process (wr, gritA);
                wl = std::round (wl * gritSteps) / gritSteps;
                wr = std::round (wr * gritSteps) / gritSteps;
            }
            wl = softClip (wl * 0.9f) / 0.9f;
            wr = softClip (wr * 0.9f) / 0.9f;
            peakOut = std::max (peakOut, std::max (std::fabs (wl), std::fabs (wr)));

            float ol, orr;
            if (p.on)
            {
                const float m = mixSmooth.process (p.mix, 0.002f);
                ol = (xl * (1.0f - m) + wl * m) * boostGain;
                orr = (xr * (1.0f - m) + wr * m) * boostGain;
            }
            else
            {
                const float m = mixSmooth.process (p.mix, 0.002f);
                ol = xl + (p.trails ? wl * m : 0.0f);
                orr = xr + (p.trails ? wr * m : 0.0f);
            }
            outL[i] = ol;
            outR[i] = orr;
        }

        if (peakOut > 0.97f)
            ++status.clipCount;
        status.inputLevel = peakIn;
        status.outputLevel = peakOut;
        status.speed1 = (float) speedNow1;
        status.bufferSeconds = (float) (curLen / sr);
        fillStatus (p);
    }

private:
    //==========================================================================
    double sr = 48000.0;
    int cap = 0, half = 0, stutCap = 0;
    std::array<std::vector<float>, 2> buf, bufB;
    Rng rng;

    // Mode 1 and Nightly 1
    int    curLen = 1, wPos = 0, curLenB = 1, wPosB = 0;
    double rPos = 0.0, rPosB = 0.0;
    double speedNow1 = 1.0, speedNow2 = 1.0, speedNow3 = 1.0;
    float  seamGain = 1.0f, seamGainB = 1.0f;
    OnePole fbLpL, fbLpR, fbLpBL, fbLpBR;
    double lfoPhase = 0.0;
    bool   held = false, frozen = false;
    float  env = 0.0f;
    bool   aboveThresh = false;
    int    lastLenB = -1, lastN1a = -1, lastN1b = -1;

    // Mode 2 and 3 recording
    bool   recording = false, armed = false, recWrapped = false;
    int    recBase = 0, recPos = 0, recCap = 0, recBuf = 0;
    bool   m2Playing = false, mode3Playing = false;
    int    playBase = 0, loopLen = 0, m2PlayLen = 0;
    double sliceStart = 0.0, slicePhase = 0.0, sliceLen = 1.0;
    std::array<double, 3> headPos { 0.0, 0.0, 0.0 };
    std::array<float, 3> stepOffset { 0.0f, 0.0f, 0.0f };
    double stepCounter = 0.0;
    double dubPos = 0.0;
    double beatSamples = 24000.0;

    // Nightly 2
    struct Stutter
    {
        std::array<std::vector<float>, 2> buf;
        int pos = 0, len = 1, rep = 0;
        bool capturing = true;
    };
    std::array<Stutter, 3> stut;

    // Nightly 3
    std::array<double, 2> vibPhase { 0.0, 0.0 };
    std::array<double, 2> vibOffset { 0.0, 0.0 };
    std::array<OnePole, 2> vibSmooth;

    // Soft switch timing
    bool   pendingDown = false, pendingUp = false, pendingClear = false, pendingToggleHold = false;
    bool   switchDown = false;
    int    downSamples = 0;

    // Mode tracking for wipe / transcend
    int    lastMode = -1, lastBank = -1;

    // Output
    DcBlock dcL, dcR;
    OnePole gritLpL, gritLpR, mixSmooth;
    Status status;

    //==========================================================================
    void resetAll()
    {
        for (auto* b : { &buf[0], &buf[1], &bufB[0], &bufB[1] })
            std::fill (b->begin(), b->end(), 0.0f);
        for (auto& u : stut) { for (auto& c : u.buf) std::fill (c.begin(), c.end(), 0.0f); u.pos = 0; u.rep = 0; u.capturing = true; }
        wPos = wPosB = 0;
        rPos = rPosB = 3.0;
        recording = armed = recWrapped = false;
        m2Playing = mode3Playing = false;
        loopLen = m2PlayLen = 0;
        held = frozen = false;
        dubPos = 0.0;
        headPos = { 0.0, 0.0, 0.0 };
        vibOffset = { 0.0, 0.0 };
        lastLenB = lastN1a = lastN1b = -1;
        curLen = curLenB = 16;
    }

    void clearAudio()
    {
        for (auto* b : { &buf[0], &buf[1], &bufB[0], &bufB[1] })
            std::fill (b->begin(), b->end(), 0.0f);
    }

    void handleModeChange (const Params& p)
    {
        if (p.mode == lastMode && p.bank == lastBank)
            return;

        const bool firstTime = lastMode < 0;
        const bool cameFromMode1 = (lastBank == 0 && lastMode == 0);
        const int  mode1Len = curLen;
        const bool hadLoop = (m2Playing || mode3Playing) && loopLen > 0;

        if (! p.transcend || firstTime)
        {
            const int keepMode = p.mode, keepBank = p.bank;
            resetAll();
            lastMode = keepMode; lastBank = keepBank;
            lastLenB = -1;
            return;
        }

        // Transcendence: the buffer survives the switch.
        lastMode = p.mode; lastBank = p.bank;
        recording = armed = false;
        if (p.bank == 0 && (p.mode == 1 || p.mode == 2))
        {
            int len = hadLoop ? loopLen : (cameFromMode1 ? mode1Len : 0);
            if (len >= 64)
            {
                loopLen = len;
                playBase = 0;
                m2Playing = (p.mode == 1);
                mode3Playing = (p.mode == 2);
                m2PlayLen = len;
                headPos = { 0.0, 0.0, 0.0 };
                if (m2Playing) startSlice (p);
            }
        }
        else if (p.bank == 0 && p.mode == 0)
        {
            lastLenB = -1; // pick up length on the next sample without wiping
        }
    }

    //==========================================================================
    // Soft switch: press starts the action straight away, a quick release counts as a tap.
    void handleSoftSwitch (const Params& p, int sampleInBlock, double ppqPerSample)
    {
        if (pendingClear) { pendingClear = false; doTap (p, true); }
        if (pendingToggleHold)
        {
            pendingToggleHold = false;
            if (p.bank == 1) frozen = ! frozen;
            else if (p.mode == 0) held = ! held;
        }

        if (pendingDown)
        {
            pendingDown = false;
            if (! switchDown)
            {
                switchDown = true;
                downSamples = 0;
                if (p.latch && p.bank == 0 && p.mode > 0)
                {
                    if (recording || armed) finishRecording (p); else beginRecording (p, sampleInBlock, ppqPerSample);
                }
                else
                {
                    pressAction (p, sampleInBlock, ppqPerSample);
                }
            }
        }
        if (switchDown) ++downSamples;

        if (pendingUp)
        {
            pendingUp = false;
            if (switchDown)
            {
                switchDown = false;
                const bool tap = downSamples < (int) (0.25 * sr);
                if (p.latch && p.bank == 0 && p.mode > 0)
                {
                    // In latch mode a tap toggles recording, so nothing more to do here
                }
                else
                {
                    releaseAction (p, tap);
                }
            }
        }

        // Synced record start: wait for the next beat
        if (armed && p.hostPlaying)
        {
            const double ppq = p.ppqAtBlockStart + sampleInBlock * ppqPerSample;
            if (std::floor (ppq) != std::floor (ppq - ppqPerSample))
            {
                armed = false;
                recording = true;
            }
        }
    }

    void pressAction (const Params& p, int s, double pps)
    {
        if (p.bank == 0 && p.mode == 0) { held = true; return; }
        if (p.bank == 0) { beginRecording (p, s, pps); return; }
    }

    void releaseAction (const Params& p, bool tap)
    {
        if (p.bank == 0 && p.mode == 0)
        {
            held = false;
            if (tap) trigger (p);
            return;
        }
        if (p.bank == 0)
        {
            if (tap) doTap (p, false);
            else     finishRecording (p);
            return;
        }
        // Nightly modes: a tap toggles freeze
        if (tap) frozen = ! frozen;
    }

    void doTap (const Params& p, bool fromStopPad)
    {
        if (p.bank == 0 && p.mode == 0)
        {
            if (fromStopPad) { held = false; clearAudio(); }
            return;
        }
        if (p.bank == 1)
        {
            if (fromStopPad) { frozen = false; clearAudio(); }
            return;
        }
        recording = armed = false;
        m2Playing = mode3Playing = false;
        loopLen = 0;
        if (! p.transcend) clearAudio();
    }

    void beginRecording (const Params& p, int, double)
    {
        const bool overdub = (p.mode == 2 && mode3Playing);
        if (p.mode == 1)
        {
            if (p.transcend) { recBase = 0; recCap = cap - 8; }
            else
            {
                recBuf = m2Playing ? 1 - (playBase == 0 ? 0 : 1) : 0;
                recBase = recBuf == 0 ? 0 : half;
                recCap = half - 8;
            }
        }
        else
        {
            recBase = 0;
            recCap = cap - 8;
        }

        if (! overdub && ! (p.mode == 1 && p.transcend && m2Playing))
        {
            for (int c = 0; c < 2; ++c)
                std::fill (buf[c].begin() + recBase, buf[c].begin() + recBase + recCap, 0.0f);
        }
        recPos = 0;
        recWrapped = false;
        if (p.sync && p.hostPlaying && ! overdub) armed = true; else recording = true;
    }

    void finishRecording (const Params& p)
    {
        if (armed) { armed = false; return; }
        if (! recording) return;
        recording = false;

        if (p.mode == 2 && mode3Playing)
            return; // overdub finished, loop carries on

        int len = recWrapped ? recCap : recPos;
        if (len < 64) return;
        if (p.sync && beatSamples > 1.0)
        {
            const double beats = std::max (1.0, std::round (len / beatSamples));
            len = std::min (recCap, (int) (beats * beatSamples));
        }

        playBase = recBase;
        loopLen = len;
        if (p.mode == 1)
        {
            m2PlayLen = len;
            m2Playing = true;
            startSlice (p);
        }
        else
        {
            mode3Playing = true;
            for (int h = 0; h < 3; ++h) headPos[(size_t) h] = 0.0;
            dubPos = 0.0;
        }
    }

    void trigger (const Params& p)
    {
        if (p.bank != 0 || p.mode != 0) return;
        rPos = wrap ((double) wPos - std::max (0.02f, p.jump) * curLen, curLen);
        ++status.triggerCount;
    }

    void writeRecording (float xl, float xr)
    {
        if (! recording) return;
        const int idx = recBase + recPos;
        if (recWrapped) { buf[0][(size_t) idx] += xl; buf[1][(size_t) idx] += xr; }
        else            { buf[0][(size_t) idx]  = xl; buf[1][(size_t) idx]  = xr; }
        if (++recPos >= recCap) { recPos = 0; recWrapped = true; }
    }

    //==========================================================================
    // Mode 1: always-listening pitch delay
    int lengthFor (int step, const Params& p, bool limitToLoop) const
    {
        double secs = p.sync ? lenBBeats (step) * (beatSamples / sr) : lenBSeconds (step);
        int len = (int) std::min ((double) (cap - 8), secs * sr);
        if (limitToLoop && p.transcend && loopLen >= 64)
            len = std::min (len, loopLen);
        return std::max (len, 16);
    }

    void mode1 (float xl, float xr, float& yl, float& yr, const Params& p, float lfoHz, float fbkA, float slewA)
    {
        if (p.lenB != lastLenB)
        {
            const int newLen = lengthFor (p.lenB, p, true);
            if (! p.transcend && lastLenB >= 0) clearAudio();
            curLen = newLen;
            lastLenB = p.lenB;
            wPos %= curLen;
            rPos = wrap ((double) wPos + 3.0, curLen);
        }

        // LFO on DIR 1, slow and narrow like the pedal
        lfoPhase += lfoHz / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        const float knob = p.dir1 + 0.25f * p.lfoDepth * std::sin (2.0f * kPi * (float) lfoPhase);
        const double target = dirToSpeed (knob, p.quant);
        speedNow1 += slewA * (target - speedNow1);

        // Seam: fade the read head as it crosses the write head
        const double age = wrap ((double) wPos - rPos, curLen);
        const double fade = std::min (64.0, curLen / 4.0);
        float g = (float) std::clamp (std::min (age, curLen - age) / fade, 0.0, 1.0);
        if (std::fabs (speedNow1 - 1.0) < 1.0e-4) g = 1.0f;
        seamGain += 0.02f * (g - seamGain);

        yl = readHermite (buf[0].data(), 0, curLen, rPos) * seamGain;
        yr = readHermite (buf[1].data(), 0, curLen, rPos) * seamGain;

        if (! held)
        {
            const float fl = fbLpL.process (yl, fbkA), fr = fbLpR.process (yr, fbkA);
            const float fb = p.fbk * 1.02f;
            buf[0][(size_t) wPos] = xl + softClip (fb * fl);
            buf[1][(size_t) wPos] = xr + softClip (fb * fr);
        }
        wPos = (wPos + 1) % curLen;
        rPos = wrap (rPos + speedNow1, curLen);
    }

    //==========================================================================
    // Mode 2: slice sampler
    double sliceLengthSamples (const Params& p) const
    {
        if (p.sync)
        {
            const auto& t = sliceBeatTable();
            const int idx = std::clamp ((int) std::lround (p.lenS * (t.size() - 1)), 0, (int) t.size() - 1);
            return t[(size_t) idx] * beatSamples;
        }
        return expMap (p.lenS, 0.005f, 4.0f) * sr;
    }

    void startSlice (const Params& p)
    {
        const double len = std::max (64, m2PlayLen);
        sliceLen = std::clamp (sliceLengthSamples (p), 64.0, len);
        sliceStart = p.rand * rng.next01() * len;
        slicePhase = 0.0;
    }

    void mode2 (float xl, float xr, float& yl, float& yr, const Params& p, float slewA)
    {
        writeRecording (xl, xr);
        if (p.transcend && recording && m2Playing)
            m2PlayLen = std::max (m2PlayLen, recWrapped ? recCap : recPos);

        const double target = dirToSpeed (p.dir1, p.quant);
        speedNow1 += slewA * (target - speedNow1);

        if (! m2Playing || m2PlayLen < 64) { yl = yr = 0.0f; return; }

        const double sp = speedNow1;
        const double src = sp >= 0.0 ? sliceStart + slicePhase : sliceStart + sliceLen - slicePhase;
        const double fade = std::max (8.0, expMap (p.sliceFade, 0.0005f, 0.02f) * sr);
        const float g = (float) std::clamp (std::min (slicePhase / fade, (sliceLen - slicePhase) / fade), 0.0, 1.0);

        yl = readHermite (buf[0].data(), playBase, m2PlayLen, src) * g;
        yr = readHermite (buf[1].data(), playBase, m2PlayLen, src) * g;

        slicePhase += std::fabs (sp);
        if (slicePhase >= sliceLen)
            startSlice (p);
    }

    //==========================================================================
    // Mode 3: three heads on one loop
    void mode3 (float xl, float xr, float& yl, float& yr, const Params& p, float slewA)
    {
        if (recording && mode3Playing)
        {
            // Overdub at loop time
            const int idx = (int) wrap (dubPos, loopLen);
            buf[0][(size_t) idx] += xl;
            buf[1][(size_t) idx] += xr;
        }
        else
        {
            writeRecording (xl, xr);
        }

        // Step sequencer
        const bool seqOn = p.stepHeads >= 0.25f;
        if (seqOn && mode3Playing)
        {
            double period;
            if (p.sync)
            {
                const auto& t = stepBeatTable();
                const int idx = std::clamp ((int) std::lround (p.stepRate * (t.size() - 1)), 0, (int) t.size() - 1);
                period = t[(size_t) idx] * beatSamples;
            }
            else period = sr / expMap (p.stepRate, 0.25f, 12.0f);

            stepCounter += 1.0;
            if (stepCounter >= period)
            {
                stepCounter = 0.0;
                const int first = p.stepHeads < 0.5f ? 2 : (p.stepHeads < 0.75f ? 1 : 0);
                for (int h = 0; h < 3; ++h)
                    stepOffset[(size_t) h] = h >= first ? (rng.next01() * 2.0f - 1.0f) * 0.5f * p.stepVar : 0.0f;
            }
        }
        else stepOffset = { 0.0f, 0.0f, 0.0f };

        const float knobs[3] = { p.dir1, p.dir2, p.dir3 };
        double* speeds[3] = { &speedNow1, &speedNow2, &speedNow3 };
        for (int h = 0; h < 3; ++h)
        {
            const double t = dirToSpeed (knobs[h] + stepOffset[(size_t) h], p.quant);
            *speeds[h] += slewA * (t - *speeds[h]);
        }

        yl = yr = 0.0f;
        if (! mode3Playing || loopLen < 64) return;

        const int active = std::clamp (p.heads, 1, 3);
        const double fade = std::max (8.0, 0.001 * sr);
        const float pans[3] = { 0.0f, -0.8f, 0.8f };
        for (int h = 0; h < active; ++h)
        {
            const double pos = headPos[(size_t) h];
            const float g = (float) std::clamp (std::min (pos / fade, (loopLen - pos) / fade), 0.0, 1.0) * p.headVol[(size_t) h];
            const float sl = readHermite (buf[0].data(), playBase, loopLen, pos) * g;
            const float sr2 = readHermite (buf[1].data(), playBase, loopLen, pos) * g;
            const float pan = p.spread ? pans[h] : 0.0f;
            const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.4142f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
            if (pan == 0.0f) { yl += sl; yr += sr2; }
            else { yl += (sl + sr2) * 0.5f * gl; yr += (sl + sr2) * 0.5f * gr; }
            headPos[(size_t) h] = wrap (pos + *speeds[h], loopLen);
        }
        const float norm = 1.0f / std::sqrt ((float) active);
        yl *= norm; yr *= norm;
        dubPos = wrap (dubPos + 1.0, loopLen);
    }

    //==========================================================================
    // Nightly 1: Ct5t5, two Mode 1 engines feeding each other
    void nightly1 (float xl, float xr, float& yl, float& yr, const Params& p, float slewA)
    {
        if (p.n1Len1 != lastN1a) { curLen = lengthFor (p.n1Len1, p, false); lastN1a = p.n1Len1; wPos %= curLen; rPos = wrap (wPos + 3.0, curLen); }
        if (p.n1Len2 != lastN1b) { curLenB = lengthFor (p.n1Len2, p, false); lastN1b = p.n1Len2; wPosB %= curLenB; rPosB = wrap (wPosB + 3.0, curLenB); }

        speedNow1 += slewA * (dirToSpeed (p.dir1, p.quant) - speedNow1);
        speedNow2 += slewA * (dirToSpeed (p.dir2, p.quant) - speedNow2);

        auto seam = [] (double w, double r, double len, double speed, float& state)
        {
            const double age = wrap (w - r, len);
            const double fade = std::min (64.0, len / 4.0);
            float g = (float) std::clamp (std::min (age, len - age) / fade, 0.0, 1.0);
            if (std::fabs (speed - 1.0) < 1.0e-4) g = 1.0f;
            state += 0.02f * (g - state);
            return state;
        };
        const float ga = seam (wPos, rPos, curLen, speedNow1, seamGain);
        const float gb = seam (wPosB, rPosB, curLenB, speedNow2, seamGainB);

        const float al = readHermite (buf[0].data(), 0, curLen, rPos) * ga;
        const float ar = readHermite (buf[1].data(), 0, curLen, rPos) * ga;
        const float bl = readHermite (bufB[0].data(), 0, curLenB, rPosB) * gb;
        const float br = readHermite (bufB[1].data(), 0, curLenB, rPosB) * gb;

        if (! frozen)
        {
            const float a = 1.0f - std::exp (-2.0f * kPi * expMap (p.n1Filter, 300.0f, 18000.0f) / (float) sr);
            buf[0][(size_t) wPos] = xl + softClip (p.fbk * 1.02f * fbLpL.process (bl, a));
            buf[1][(size_t) wPos] = xr + softClip (p.fbk * 1.02f * fbLpR.process (br, a));
            bufB[0][(size_t) wPosB] = softClip (fbLpBL.process (al, a));
            bufB[1][(size_t) wPosB] = softClip (fbLpBR.process (ar, a));
        }
        wPos = (wPos + 1) % curLen;
        wPosB = (wPosB + 1) % curLenB;
        rPos = wrap (rPos + speedNow1, curLen);
        rPosB = wrap (rPosB + speedNow2, curLenB);

        if (p.spread)
        {
            yl = 0.85f * al + 0.45f * bl;
            yr = 0.45f * ar + 0.85f * br;
        }
        else
        {
            yl = 0.7f * (al + bl);
            yr = 0.7f * (ar + br);
        }
    }

    //==========================================================================
    // Nightly 2: auto stutter
    void nightly2 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        yl = yr = 0.0f;
        const int active = std::clamp (p.heads, 1, 3);
        const float pans[3] = { 0.0f, -0.7f, 0.7f };
        for (int k = 0; k < 3; ++k)
        {
            auto& u = stut[(size_t) k];
            double secs;
            if (p.sync)
            {
                const auto& t = sliceBeatTable();
                const int idx = std::clamp ((int) std::lround (p.n2Len[(size_t) k] * (t.size() - 1)), 0, (int) t.size() - 1);
                secs = t[(size_t) idx] * beatSamples / sr;
            }
            else secs = expMap (p.n2Len[(size_t) k], 0.02f, (float) kStutterSeconds);
            const int target = std::clamp ((int) (secs * sr), 32, stutCap - 8);
            if (u.capturing && u.pos == 0) u.len = target;

            float sl, s2;
            if (u.capturing && ! frozen)
            {
                u.buf[0][(size_t) u.pos] = xl;
                u.buf[1][(size_t) u.pos] = xr;
                sl = xl; s2 = xr;
            }
            else
            {
                const double fade = std::min (0.002 * sr, u.len / 4.0);
                const float g = (float) std::clamp (std::min (u.pos / fade, (u.len - u.pos) / fade), 0.0, 1.0);
                sl = u.buf[0][(size_t) u.pos] * g;
                s2 = u.buf[1][(size_t) u.pos] * g;
            }

            if (++u.pos >= u.len)
            {
                u.pos = 0;
                if (u.capturing) { u.capturing = false; u.rep = 1; }
                else if (++u.rep >= std::clamp (p.n2Rep[(size_t) k], 2, 16) && ! frozen) { u.capturing = true; u.rep = 0; }
            }

            if (k < active)
            {
                const float v = p.n2Vol[(size_t) k];
                const float pan = p.spread ? pans[k] : 0.0f;
                yl += (sl * (1.0f - std::max (0.0f, pan))) * v;
                yr += (s2 * (1.0f + std::min (0.0f, pan))) * v;
            }
        }
        const float norm = 1.0f / std::sqrt ((float) active);
        yl *= norm; yr *= norm;
    }

    //==========================================================================
    // Nightly 3: long delay with two square-wave vibrato heads and a feedback head
    void nightly3 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        const int len = cap - 8;
        if (curLen != len) { curLen = len; wPos = 0; }

        const double tf = expMap (p.n3FbkTime, 0.5f, 7.0f) * sr;
        const double rf = wPos - tf;
        const float fl = readHermite (buf[0].data(), 0, len, rf);
        const float fr = readHermite (buf[1].data(), 0, len, rf);

        yl = yr = 0.0f;
        const int active = std::clamp (p.heads, 1, 3);
        for (int h = 0; h < 2; ++h)
        {
            const double t = expMap (p.n3Time[(size_t) h], 0.5f, 7.0f) * sr;
            const double hz = expMap (p.n3Rate[(size_t) h], 0.1f, 12.0f);
            vibPhase[(size_t) h] += hz / sr;
            if (vibPhase[(size_t) h] >= 1.0) vibPhase[(size_t) h] -= 1.0;
            const float semis = quantiseSemis (p.n3Depth[(size_t) h] * 12.0f, p.quant);
            const float sq = vibPhase[(size_t) h] < 0.5 ? 1.0f : -1.0f;
            const float smooth = vibSmooth[(size_t) h].process (sq, 1.0f - std::exp (-1.0f / (0.003f * (float) sr)));
            const double speed = std::pow (2.0, smooth * semis / 12.0);
            vibOffset[(size_t) h] += (speed - 1.0);
            vibOffset[(size_t) h] *= 0.99995; // pull back the drift from uneven up and down speeds
            vibOffset[(size_t) h] = std::clamp (vibOffset[(size_t) h], -t + 64.0, (double) len - t - 64.0);
            if (h < active)
            {
                const double pos = wPos - t + vibOffset[(size_t) h];
                const float sl = readHermite (buf[0].data(), 0, len, pos);
                const float s2 = readHermite (buf[1].data(), 0, len, pos);
                if (p.spread) { if (h == 0) { yl += sl; yr += 0.3f * s2; } else { yl += 0.3f * sl; yr += s2; } }
                else { yl += sl; yr += s2; }
            }
        }
        if (active >= 3) { yl += fl; yr += fr; }
        yl *= 0.7f; yr *= 0.7f;

        const float fb = frozen ? 1.0f : std::min (p.n3Fbk, 0.98f);
        const float in = frozen ? 0.0f : 1.0f;
        buf[0][(size_t) wPos] = in * xl + fb * fl;
        buf[1][(size_t) wPos] = in * xr + fb * fr;
        wPos = (wPos + 1) % len;
        speedNow1 = 1.0;
    }

    //==========================================================================
    void fillStatus (const Params& p)
    {
        if (p.bank == 1)
            status.state = frozen ? Frozen : Listening;
        else if (p.mode == 0)
            status.state = held ? Held : Listening;
        else if (armed)
            status.state = Armed;
        else if (recording)
            status.state = Recording;
        else if (m2Playing || mode3Playing)
            status.state = Playing;
        else
            status.state = Empty;

        status.loopSeconds = (float) (loopLen / sr);
        status.recFraction = recCap > 0 ? (float) (recWrapped ? 1.0 : (double) recPos / recCap) : 0.0f;
        if (mode3Playing && loopLen > 0) status.playFraction = (float) (headPos[0] / loopLen);
        else if (m2Playing && m2PlayLen > 0) status.playFraction = (float) (wrap (sliceStart + slicePhase, m2PlayLen) / m2PlayLen);
        else status.playFraction = 0.0f;
    }
};

} // namespace backtrack
