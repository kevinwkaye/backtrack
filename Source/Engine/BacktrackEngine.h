// Backtrack DSP engine, v0.2.
// Pure C++17 with no JUCE dependency, so it builds on Windows x64 and Linux ARM64 and
// runs in the offline render tests. Recreates the Montreal Assembly Count to 5:
// standard firmware Modes 1 to 3 plus the three Nightly firmware modes, in stereo.
//
// v0.2 is the premium pass: band-limited read heads, click-free bypass, mode and length
// changes, phase-matched seams, seamless loop wraps, overdubs that never clip, triggers
// judged against your playing level, singing feedback, stereo in every mode, LOW KEEP,
// two converter flavours and a soft output ceiling.
#pragma once

#include "DspCore.h"

namespace backtrack
{

//==============================================================================
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
    bool  reverse = false;     // footswitch C: every playing head runs the other way

    // Q layer, standard
    float fbkFilter = 1.0f;    // 0..1, 1 = open
    float lfoRate = 0.3f;
    float lfoDepth = 0.0f;
    float trigSens = 1.0f;     // 0..1, 1 = off
    float jump = 0.5f;         // fraction of the buffer
    float sliceFade = 0.2f;
    float stepRate = 0.4f;
    float stepVar = 0.3f;
    float stepHeads = 0.0f;    // below 0.25 = sequencer off
    float dubFade = 0.9f;      // 0..1, 0.9 = -1 dB per overdub pass
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
    int   grit = 1;            // 0 off, 1 24-BIT, 2 12-BIT
    float gritAmount = 0.3f;
    bool  boost = true;
    bool  spread = true;
    bool  trails = false;
    bool  latch = false;
    bool  sync = false;
    bool  lock = true;         // Mode 3 heads stay in step with the loop
    bool  lowKeep = false;
    float lowKeepHz = 100.0f;

    // Host clock
    double bpm = 120.0;
    bool   hostPlaying = false;
    double ppqAtBlockStart = 0.0;
};

struct Status
{
    int   state = 0;
    float loopSeconds = 0.0f;
    float recFraction = 0.0f;
    float playFraction = 0.0f;
    float speed1 = 1.0f;
    float bufferSeconds = 0.0f;
    uint32_t triggerCount = 0;
    uint32_t clipCount = 0;
    float inputLevel = 0.0f;
    float outputLevel = 0.0f;
};

enum State { Listening = 0, Held, Recording, Playing, Empty, Frozen, Armed };

//==============================================================================
// Quantise tables, as on the pedal's Q switch
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

// DIR knob (0..1) to read speed (-2..2). Noon stops the head, 2 o'clock is unity.
inline float dirToSpeed (float knob, int quant)
{
    const float s = (std::clamp (knob, 0.0f, 1.0f) - 0.5f) * 4.0f;
    if (quant <= 0) return s;
    const float a = std::fabs (s);
    if (a < 0.2f) return 0.0f;
    const float sign = s < 0.0f ? -1.0f : 1.0f;
    const float semis = std::clamp (12.0f * std::log2 (std::max (a, 0.5f)), -12.0f, 12.0f);
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = -12; i <= 12; ++i)
    {
        if (! scaleHas (quant, i)) continue;
        const float d = std::fabs ((float) i - semis);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return sign * std::pow (2.0f, (float) best / 12.0f);
}

inline float speedToDir (float speed) { return std::clamp (0.5f + speed / 4.0f, 0.0f, 1.0f); }

inline float quantiseSemis (float semis, int quant)
{
    if (quant <= 0) return semis;
    int best = 0;
    float bestDist = 1.0e9f;
    for (int i = 0; i <= 12; ++i)
    {
        if (! scaleHas (quant, i)) continue;
        const float d = std::fabs ((float) i - semis);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return (float) best;
}

inline double lenBSeconds (int step) { return 8.0 / std::pow (2.0, 9 - std::clamp (step, 0, 9)); }
inline double lenBBeats (int step) { return 16.0 / std::pow (2.0, 9 - std::clamp (step, 0, 9)); }

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
// A Count to 5 Mode 1 buffer: a write head running at fixed speed and one read head
// running at any speed, with phase-matched crossfades wherever the pedal would jump.
class DelayUnit
{
public:
    static constexpr int kGuard = BandlimitedReader::kMaxHalf + 6;

    void attach (std::vector<float>* l, std::vector<float>* r, int capacity) { bl = l; br = r; cap = capacity; reset(); }
    void reset() { w = 0; written = 0; tap = Tap{}; tap.age = L; }
    void setFresh (double newL) { L = newL; tap = Tap{}; tap.age = newL; }
    // After a wipe the buffer starts again from silence, so its first audio fades in
    // instead of starting mid-waveform when it reaches the read head.
    void clearAudio()
    {
        written = 0;
        inGain = 0.0f;
        if (bl == nullptr) return;
        for (int k = 1; k <= 2 * kGuard; ++k)
        {
            const size_t i = (size_t) wrapIndex ((long long) w - k, cap);
            (*bl)[i] = (*br)[i] = 0.0f;
        }
    }
    float inGain = 1.0f, inStep = 1.0f / 480.0f;
    bool wasFrozen = false;

    double L = 24000.0;        // buffer length in samples
    bool frozen = false;       // hold: the write head stops
    Tap tap;
    int w = 0, written = 0, cap = 0;

    void read (double speed, float& oL, float& oR) const
    {
        readAge (tap.age, speed, oL, oR);
        if (tap.fading)
        {
            float tL, tR;
            readAge (tap.twin, speed, tL, tR);
            const float ga = tap.gainA(), gb = tap.gainB();
            oL = oL * ga + tL * gb;
            oR = oR * ga + tR * gb;
        }
    }

    void readAge (double age, double speed, float& oL, float& oR) const
    {
        if (age > (double) written + 2.0 || age < 1.0) { oL = oR = 0.0f; return; }
        BandlimitedReader::get().read (bl->data(), br->data(), 0, cap, (double) w - age, speed, oL, oR);
    }

    void write (float l, float r)
    {
        if (frozen) { wasFrozen = true; return; }
        if (wasFrozen)
        {
            // coming out of a hold: dip the splice, so old and new audio never butt together
            wasFrozen = false;
            const int n = 480;
            for (int k = 1; k <= n; ++k)
            {
                const size_t i = (size_t) wrapIndex ((long long) w - k, cap);
                const float g = 0.5f - 0.5f * std::cos (kPi * (float) (k - 1) / (float) n);
                (*bl)[i] *= g; (*br)[i] *= g;
            }
            inGain = 0.0f;
        }
        if (inGain < 1.0f)
        {
            inGain = std::min (1.0f, inGain + inStep);
            const float g = 0.5f - 0.5f * std::cos (kPi * inGain);   // raised cosine: no corner to hear
            l *= g; r *= g;
        }
        (*bl)[(size_t) w] = l;
        (*br)[(size_t) w] = r;
        if (++w >= cap) w = 0;
        if (written < cap) ++written;
    }

    // Move the read head on by one output sample. At the seam (where the pedal jumps
    // from the newest audio to the oldest) it crossfades to a twin placed where the
    // waveforms line up.
    void step (double speed, int fadeLen)
    {
        const double delta = (frozen ? 0.0 : 1.0) - speed;
        tap.advance (delta);
        if (tap.fading) return;
        const double lo = kGuard, hi = L + kGuard;
        if (delta < 0.0 && tap.age <= lo + fadeLen * -delta + 2.0)
            seam (tap.age + L, delta, fadeLen);
        else if (delta > 0.0 && tap.age >= hi)
            seam (tap.age - L, delta, fadeLen);
        if (tap.age < 2.0) tap.age = 2.0;
    }

    // A deliberate jump (trigger, length change): crossfade without phase matching
    void jumpTo (double age, int fadeLen) { tap.startFade (std::max ((double) kGuard, age), fadeLen); }

    // Pull the head inside a new length without a click
    void fitInto (double newL, int fadeLen)
    {
        const double oldL = L;
        L = newL;
        if (tap.fading || oldL <= 0.0) return;
        // keep the head at the same place in the new length, so unity stays a clean echo
        jumpTo (kGuard + (tap.age - kGuard) * (newL / oldL), fadeLen);
    }

private:
    std::vector<float>* bl = nullptr;
    std::vector<float>* br = nullptr;

    float mono (int age) const
    {
        const int i = wrapIndex ((long long) w - age, cap);
        return (*bl)[(size_t) i] + (*br)[(size_t) i];
    }

    void seam (double target, double delta, int fadeLen)
    {
        // search a few milliseconds either side for the best waveform match
        const int search = std::clamp ((int) (L * 0.2), 0, 384);
        const int span = 256, stride = 2;
        const double maxAge = std::min ((double) written, (double) cap - 4.0) - span * std::abs (delta) - 4.0;
        double best = target, bestScore = -1.0e30;
        if (search > 0 && target + search < maxAge && target - search > kGuard)
        {
            auto score = [&] (int d)
            {
                double num = 0.0, ea = 0.0, eb = 0.0;
                for (int j = 0; j < span; j += stride)
                {
                    const double off = j * delta;
                    const float a = mono ((int) (tap.age + off));
                    const float b = mono ((int) (target + d + off));
                    num += a * b; ea += a * a; eb += b * b;
                }
                return num / std::sqrt (ea * eb + 1e-12);
            };
            // coarse pass, then refine around the best match
            int bestD = 0;
            for (int d = -search; d <= search; d += 4)
            {
                const double sc = score (d);
                if (sc > bestScore) { bestScore = sc; bestD = d; }
            }
            for (int d = std::max (-search, bestD - 3); d <= std::min (search, bestD + 3); ++d)
            {
                const double sc = score (d);
                if (sc > bestScore) { bestScore = sc; bestD = d; }
            }
            best = target + bestD;
        }
        best = std::clamp (best, (double) kGuard, std::max ((double) kGuard, maxAge));
        tap.startFade (best, fadeLen);
    }
};

//==============================================================================
class Engine
{
public:
    static constexpr double kRingSeconds = 17.0;   // two 8 s lengths plus room for the seam
    static constexpr double kLoopSeconds = 8.0;
    static constexpr double kStutterSeconds = 2.5;
    static constexpr double kMinLoopSeconds = 0.25;
    static constexpr double kDebounceSeconds = 0.08;

    void prepare (double newSampleRate)
    {
        sr = newSampleRate;
        (void) BandlimitedReader::get();   // builds the shared filter table here, off the audio thread
        cap = (int) (kRingSeconds * sr) + 16;
        loopCap = (int) (kLoopSeconds * sr);
        half = loopCap / 2;
        for (auto* b : { &bufA[0], &bufA[1], &bufB[0], &bufB[1], &scratch[0], &scratch[1] })
            b->assign ((size_t) cap, 0.0f);
        unitA.attach (&bufA[0], &bufA[1], cap);
        unitB.attach (&bufB[0], &bufB[1], cap);
        stutCap = (int) (kStutterSeconds * sr) + 8;
        for (auto& u : stut)
            for (auto& c : u.buf) c.assign ((size_t) stutCap, 0.0f);
        preLen = std::max (16, (int) (0.012 * sr));
        for (auto& c : pre) c.assign ((size_t) preLen, 0.0f);
        fadeLen = std::max (32, (int) (0.012 * sr));
        dubA = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
        dubGain = 0.0f; feedS = 1.0f;
        gateStep = (float) (1.0 / (0.015 * sr));
        grit.prepare (sr);
        for (auto& f : fbHp) f.highpass (sr, 30.0);
        for (auto& f : wetHp) f.highpass (sr, 22.0);
        for (auto& x : xoDry) x.hz = 0.0;
        for (auto& x : xoWet) x.hz = 0.0;
        resetAll();
        activeMode = activeBank = -1;
        gate = 0.0f; gateTarget = 1.0f;
        onGain = 1.0f; boostGain = 1.0f; lkGain = 0.0f; mixS = 0.7f;
        envFast = envSlow = playLevel = 0.0f;
        refractory = 0; armedOnset = true;
        speedNow = { 1.0, 1.0, 1.0 };
        pendingAction = None;
        pendingDown = pendingUp = pendingClear = pendingToggleHold = pendingCancel = switchDown = false;
        sinceUp = 1 << 30;
        status = Status{};
        for (auto& f : fbLp) f.reset();
        for (auto& f : fbTape) f.reset();
    }

    void softDown() { pendingDown = true; }
    void softUp()   { pendingUp = true; }
    void stopAndClear() { pendingClear = true; }
    void toggleHold() { pendingToggleHold = true; }
    // The press was half of a two-switch combo: undo what it started
    void cancelPress() { pendingCancel = true; }

    const Status& getStatus() const { return status; }

    // Loop data for saving with the Live set
    int  getLoopLength() const { return (mode3Playing || m2Playing) ? loopLen : 0; }
    int  getLoopBase() const { return playBase; }
    const std::vector<float>& getBuffer (int ch) const { return bufA[(size_t) ch]; }
    std::vector<float>& getBufferForRestore (int ch) { return bufA[(size_t) ch]; }
    int  getCapacity() const { return loopCap; }

    void restoreLoop (int mode, int length)
    {
        length = std::clamp (length, 0, loopCap);
        if (length < 64) return;
        activeMode = mode; activeBank = 0;
        loopLen = length;
        playBase = 0;
        if (mode == 1) { m2Playing = true; m2PlayLen = length; m2Wait = 0; startSlice (Params{}); }
        else if (mode == 2) { mode3Playing = true; for (auto& h : heads) h = Tap{}; loopClock = 0.0; }
    }

    //==========================================================================
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p)
    {
        requestModeChange (p);

        beatSamples = (p.bpm > 1.0 ? 60.0 / p.bpm : 0.5) * sr;
        lfoHz = expMap (p.lfoRate, 0.05f, 4.0f);
        fbA = onePoleCoef (expMap (p.fbkFilter, 300.0f, 18000.0f), sr);
        tapeA = onePoleCoef (12000.0f, sr);
        slewA = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        const float smoothA = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        const float onTarget = p.on ? 1.0f : 0.0f;
        const float boostTarget = p.boost ? 1.4125f : 1.0f;     // +3 dB, the pedal's jump when engaged
        const float lkTarget = p.lowKeep ? 1.0f : 0.0f;
        for (int c = 0; c < 2; ++c) { xoDry[(size_t) c].set (sr, p.lowKeepHz); xoWet[(size_t) c].set (sr, p.lowKeepHz); }
        const double ppqPerSample = (p.bpm / 60.0) / sr;
        float peakIn = 0.0f, peakOut = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            peakIn = std::max (peakIn, std::max (std::fabs (xl), std::fabs (xr)));
            handleSoftSwitch (p, i, ppqPerSample);
            detectOnset (xl, xr, p);

            // Gate: fades the wet out, performs a mode change or wipe at silence, fades back in
            if (gate > gateTarget) { gate -= gateStep; if (gate <= 0.0f) { gate = 0.0f; performPending (p); } }
            else if (gate < gateTarget) gate = std::min (gateTarget, gate + gateStep);

            // With trails on and the effect bypassed, the buffers ring on but hear nothing new
            feedS += smoothA * ((p.on || ! p.trails ? 1.0f : 0.0f) - feedS);
            const float il = xl * feedS, ir = xr * feedS;

            float wl = 0.0f, wr = 0.0f;
            if (activeBank == 0)
            {
                if (activeMode == 0)      mode1 (il, ir, wl, wr, p);
                else if (activeMode == 1) mode2 (il, ir, wl, wr, p);
                else                      mode3 (il, ir, wl, wr, p);
            }
            else
            {
                if (activeMode == 0)      nightly1 (il, ir, wl, wr, p);
                else if (activeMode == 1) nightly2 (il, ir, wl, wr, p);
                else                      nightly3 (il, ir, wl, wr, p);
            }

            pushPreroll (xl, xr);   // after the modes, so the pre-roll ends just before a new recording
            wl = wetHp[0].process (wl);
            wr = wetHp[1].process (wr);
            grit.process (wl, wr, p.grit, p.gritAmount);
            const float gs = gate * gate * (3.0f - 2.0f * gate);
            wl *= gs; wr *= gs;

            mixS += smoothA * (p.mix - mixS);
            onGain += smoothA * (onTarget - onGain);
            boostGain += smoothA * (boostTarget - boostGain);
            lkGain += smoothA * (lkTarget - lkGain);

            // Wet and dry, with LOW KEEP holding the dry lows under the wet
            float dlo, dhi, dlo2, dhi2, wlo, whi, wlo2, whi2;
            xoDry[0].split (xl, dlo, dhi); xoDry[1].split (xr, dlo2, dhi2);
            xoWet[0].split (wl, wlo, whi); xoWet[1].split (wr, wlo2, whi2);
            const float plainL = xl * (1.0f - mixS) + wl * mixS;
            const float plainR = xr * (1.0f - mixS) + wr * mixS;
            const float keepL = dlo + dhi * (1.0f - mixS) + whi * mixS;
            const float keepR = dlo2 + dhi2 * (1.0f - mixS) + whi2 * mixS;
            const float procL = (plainL + lkGain * (keepL - plainL)) * boostGain;
            const float procR = (plainR + lkGain * (keepR - plainR)) * boostGain;

            // Bypass fades over 20 ms, so there's no click. Trails let the wet ring on.
            trailS += smoothA * ((p.trails ? 1.0f : 0.0f) - trailS);
            const float trailL = trailS * wl * mixS, trailR = trailS * wr * mixS;
            float ol = onGain * procL + (1.0f - onGain) * (xl + trailL);
            float orr = onGain * procR + (1.0f - onGain) * (xr + trailR);
            if (std::abs (ol) > 0.97f || std::abs (orr) > 0.97f) ++clipRun; else clipRun = 0;
            ol = softCeiling (ol, 0.85f, 0.966f);
            orr = softCeiling (orr, 0.85f, 0.966f);
            peakOut = std::max (peakOut, std::max (std::fabs (wl), std::fabs (wr)));
            outL[i] = ol;
            outR[i] = orr;
        }

        if (clipRun > 64) ++status.clipCount;
        status.inputLevel = peakIn;
        status.outputLevel = peakOut;
        status.speed1 = (float) speedNow[0];
        status.bufferSeconds = (float) (unitA.L / sr);
        fillStatus();
    }

private:
    double sr = 48000.0;
    int cap = 0, loopCap = 0, half = 0, stutCap = 0, fadeLen = 576;
    std::array<std::vector<float>, 2> bufA, bufB, scratch;
    DelayUnit unitA, unitB;
    Rng rng;

    // Block-rate values
    double beatSamples = 24000.0;
    float lfoHz = 0.3f, fbA = 1.0f, tapeA = 1.0f, slewA = 0.001f;

    // Read speeds, slewed like the pedal's knobs
    std::array<double, 3> speedNow { 1.0, 1.0, 1.0 };
    double lfoPhase = 0.0;
    bool held = false, frozen = false;

    // Feedback path
    std::array<Biquad, 4> fbHp;
    std::array<OnePole, 4> fbLp, fbTape;

    // Onset detection against your playing level
    float envFast = 0.0f, envSlow = 0.0f, playLevel = 0.0f;
    int refractory = 0;
    bool armedOnset = true;

    // Mode 1 length
    double lastLenSamples = -1.0;
    int lastN1a = -1, lastN1b = -1;

    // Mode 2 and 3 recording
    bool recording = false, armed = false, recWrapped = false;
    int recBase = 0, recPos = 0, recCap = 0;
    bool m2Playing = false, mode3Playing = false;
    int playBase = 0, loopLen = 0, m2PlayLen = 0, m2Wait = 0;
    double sliceStart = 0.0, slicePhase = 0.0, sliceLen = 1.0;
    int sliceCount = 0;
    std::array<Tap, 3> heads;
    float dubGain = 0.0f, dubA = 0.004f, feedS = 1.0f, trailS = 0.0f;
    std::array<float, 3> stepOffset { 0.0f, 0.0f, 0.0f };
    double stepCounter = 0.0, loopClock = 0.0;

    // Pre-roll: the last few ms of input, so a loop's end can flow into its start
    std::array<std::vector<float>, 2> pre;
    int preLen = 576, prePos = 0;

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
    std::array<OnePole, 2> vibSmooth;
    std::array<Tap, 2> vibHead;
    std::array<double, 2> vibSpeed { 1.0, 1.0 };

    // Soft switch timing
    bool pendingDown = false, pendingUp = false, pendingClear = false, pendingToggleHold = false, pendingCancel = false;
    bool switchDown = false;
    int downSamples = 0, sinceUp = 1 << 30;

    // Mode switching through the gate
    int activeMode = -1, activeBank = -1;
    enum Pending { None, Switch, WipeLength, ClearLoop, StartLoop };
    int pendBase = 0, pendLen = 0;
    Pending pendingAction = None;
    int wantMode = 0, wantBank = 0;
    double wantLen = 0.0;
    float gate = 0.0f, gateTarget = 1.0f, gateStep = 0.001f;

    // Output
    std::array<Biquad, 2> wetHp;
    std::array<Crossover, 2> xoDry, xoWet;
    Grit grit;
    float mixS = 0.7f, onGain = 1.0f, boostGain = 1.0f, lkGain = 0.0f;
    int clipRun = 0;
    Status status;

    //==========================================================================
    void resetAll()
    {
        unitA.reset(); unitB.reset();
        unitA.clearAudio(); unitB.clearAudio();
        for (auto& u : stut) { u.pos = 0; u.rep = 0; u.capturing = true; }
        recording = armed = recWrapped = false;
        m2Playing = mode3Playing = false;
        loopLen = m2PlayLen = 0;
        held = frozen = false;
        loopClock = 0.0;
        for (auto& h : heads) h = Tap{};
        for (auto& v : vibHead) v = Tap{};
        lastLenSamples = -1.0;
        lastN1a = lastN1b = -1;
    }

    void pushPreroll (float l, float r)
    {
        pre[0][(size_t) prePos] = l;
        pre[1][(size_t) prePos] = r;
        if (++prePos >= preLen) prePos = 0;
    }

    //==========================================================================
    // Mode changes wipe the buffer, as on the pedal, unless Transcend is on.
    // Either way the wet fades to silence first, so nothing clicks.
    void requestModeChange (const Params& p)
    {
        if (activeMode < 0) { activeMode = p.mode; activeBank = p.bank; resetAll(); gate = 0.0f; gateTarget = 1.0f; return; }
        if ((p.mode != activeMode || p.bank != activeBank) && pendingAction != Switch)
        {
            pendingAction = Switch;
            wantMode = p.mode; wantBank = p.bank;
            gateTarget = 0.0f;
        }
        else if (pendingAction == Switch && p.mode == activeMode && p.bank == activeBank && gate > 0.0f)
        {
            pendingAction = None; // changed back before the fade finished
            gateTarget = 1.0f;
        }
    }

    void performPending (const Params& p)
    {
        if (pendingAction == Switch)
        {
            const int fromMode = activeMode, fromBank = activeBank;
            activeMode = wantMode; activeBank = wantBank;
            if (! p.transcend) resetAll();
            else carryAcross (fromMode, fromBank);
        }
        else if (pendingAction == WipeLength)
        {
            unitA.clearAudio();
            unitA.setFresh (wantLen);
        }
        else if (pendingAction == ClearLoop)
        {
            m2Playing = mode3Playing = false;
            loopLen = 0;
        }
        else if (pendingAction == StartLoop)
            startLoop (pendBase, pendLen, p);
        pendingAction = None;
        gateTarget = 1.0f;
    }

    // Transcend: carry the audio from one mode to the next
    void carryAcross (int fromMode, int fromBank)
    {
        recording = armed = false;
        const bool hadLoop = (m2Playing || mode3Playing) && loopLen > 0;
        const bool fromRing = (fromBank == 0 && fromMode == 0) || (fromBank == 1 && fromMode != 1);
        if (activeBank == 0 && (activeMode == 1 || activeMode == 2))
        {
            int len = 0;
            if (hadLoop) len = loopLen;
            else if (fromRing && unitA.written > 64)
            {
                // the last LEN B of the ring becomes the loop
                len = std::min ({ (int) unitA.L, unitA.written, loopCap });
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < len; ++k)
                        scratch[(size_t) c][(size_t) k] = bufA[(size_t) c][(size_t) wrapIndex ((long long) unitA.w - len + k, cap)];
                for (int c = 0; c < 2; ++c) std::copy (scratch[(size_t) c].begin(), scratch[(size_t) c].begin() + len, bufA[(size_t) c].begin());
                playBase = 0;
            }
            if (len >= 64)
            {
                loopLen = len;
                m2Playing = activeMode == 1;
                mode3Playing = activeMode == 2;
                m2PlayLen = len;
                for (auto& h : heads) h = Tap{};
                loopClock = 0.0;
                if (m2Playing) startSlice (Params{});
            }
        }
        else if ((activeBank == 0 && activeMode == 0) || (activeBank == 1 && activeMode == 0))
        {
            if (hadLoop)
            {
                // the loop becomes the ring, and Mode 1's longest length follows it
                for (int c = 0; c < 2; ++c)
                    std::copy (bufA[(size_t) c].begin() + playBase, bufA[(size_t) c].begin() + playBase + loopLen, scratch[(size_t) c].begin());
                for (int c = 0; c < 2; ++c) std::copy (scratch[(size_t) c].begin(), scratch[(size_t) c].begin() + loopLen, bufA[(size_t) c].begin());
                unitA.w = loopLen % cap;
                unitA.written = loopLen;
                unitA.tap = Tap{};
                unitA.tap.age = std::min ((double) loopLen, unitA.L) * 0.5 + DelayUnit::kGuard;
                m2Playing = mode3Playing = false;
            }
            lastLenSamples = -1.0;
        }
    }

    //==========================================================================
    // Soft switch: press acts straight away, a quick release counts as a tap
    void handleSoftSwitch (const Params& p, int sampleInBlock, double ppqPerSample)
    {
        ++sinceUp;
        if (pendingClear) { pendingClear = false; doTap (p, true); }
        if (pendingToggleHold)
        {
            pendingToggleHold = false;
            if (activeBank == 1) frozen = ! frozen;
            else if (activeMode == 0) held = ! held;
        }

        if (pendingDown)
        {
            pendingDown = false;
            if (! switchDown && sinceUp > (int) (kDebounceSeconds * sr))
            {
                switchDown = true;
                downSamples = 0;
                if (p.latch && activeBank == 0 && activeMode > 0)
                {
                    if (recording || armed) finishRecording (p); else beginRecording (p);
                }
                else pressAction (p);
            }
        }
        if (switchDown) ++downSamples;

        if (pendingCancel)
        {
            pendingCancel = false;
            pendingUp = false;
            if (switchDown)
            {
                switchDown = false;
                sinceUp = 0;
                if (activeBank == 0 && activeMode == 0) held = false;
                else if (activeBank == 0) { recording = false; armed = false; }
            }
        }

        if (pendingUp)
        {
            pendingUp = false;
            if (switchDown)
            {
                switchDown = false;
                sinceUp = 0;
                const bool tap = downSamples < (int) (0.45 * sr);
                if (! (p.latch && activeBank == 0 && activeMode > 0)) releaseAction (p, tap);
            }
        }

        // Synced recording starts on the next beat
        if (armed && p.hostPlaying)
        {
            const double ppq = p.ppqAtBlockStart + sampleInBlock * ppqPerSample;
            if (std::floor (ppq) != std::floor (ppq - ppqPerSample)) { armed = false; recording = true; }
        }
    }

    void pressAction (const Params& p)
    {
        if (activeBank == 0 && activeMode == 0) { held = true; return; }
        if (activeBank == 0) { beginRecording (p); return; }
    }

    void releaseAction (const Params& p, bool tap)
    {
        if (activeBank == 0 && activeMode == 0)
        {
            held = false;
            if (tap) trigger (p);
            return;
        }
        if (activeBank == 0)
        {
            if (tap)
            {
                // a quick press on an empty pedal records nothing; on a loop it stops and clears
                recording = armed = false;
                doTap (p, false);
            }
            else finishRecording (p);
            return;
        }
        frozen = ! frozen;   // Nightly: tap or hold, the soft switch freezes and releases
        (void) tap;
    }

    void doTap (const Params& p, bool fromStopPad)
    {
        if (activeBank == 0 && activeMode == 0)
        {
            if (fromStopPad) { held = false; requestWipe (unitA.L); }
            return;
        }
        if (activeBank == 1)
        {
            if (fromStopPad) { frozen = false; requestWipe (unitA.L); }
            return;
        }
        recording = armed = false;
        (void) p;
        if (! (m2Playing || mode3Playing)) return;
        if (pendingAction == Switch) return;
        pendingAction = ClearLoop;   // fade the loop out, then clear it
        gateTarget = 0.0f;
    }

    void requestWipe (double newLen)
    {
        if (pendingAction == Switch) return;
        pendingAction = WipeLength;
        wantLen = newLen;
        gateTarget = 0.0f;
    }

    void beginRecording (const Params& p)
    {
        const bool overdub = (activeMode == 2 && mode3Playing);
        if (activeMode == 1)
        {
            if (p.transcend) { recBase = 0; recCap = loopCap; }
            else
            {
                const int other = m2Playing ? (playBase == 0 ? 1 : 0) : 0;
                recBase = other == 0 ? 0 : half;
                recCap = half;
            }
        }
        else { recBase = 0; recCap = loopCap; }

        if (! overdub && ! (activeMode == 1 && p.transcend && m2Playing))
        {
            // stop the ring overlapping a new recording
            if (activeMode == 2) { mode3Playing = false; loopLen = 0; }
            for (int c = 0; c < 2; ++c)
                std::fill (bufA[(size_t) c].begin() + recBase, bufA[(size_t) c].begin() + recBase + recCap, 0.0f);
            // the pre-roll is what flowed into the first sample
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < preLen; ++k)
                    scratch[(size_t) c][(size_t) k] = pre[(size_t) c][(size_t) ((prePos + k) % preLen)];
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
        if (activeMode == 2 && mode3Playing) return; // overdub finished, the loop carries on

        int len = recWrapped ? recCap : recPos;
        if (len < (int) (kMinLoopSeconds * sr)) return; // too short to be a loop: throw it away
        if (p.sync && beatSamples > 1.0)
        {
            const double beats = std::max (1.0, std::round (len / beatSamples));
            len = std::min (recCap, (int) (beats * beatSamples));
        }

        // Free loops: nudge the end point (up to 10 ms) to where the waveform best matches
        // the audio that led into the start, so the wrap has nothing to hide
        if (! p.sync && ! recWrapped)
        {
            const int span = std::min (256, preLen);
            const int maxTrim = std::min ((int) (0.01 * sr), len / 8);
            int bestTrim = 0;
            double bestScore = -1.0e30;
            for (int d = 0; d <= maxTrim; ++d)
            {
                double num = 0.0, ea = 0.0, eb = 0.0;
                const int e0 = recBase + len - d - span;
                for (int j = 0; j < span; j += 2)
                {
                    const float a = bufA[0][(size_t) (e0 + j)] + bufA[1][(size_t) (e0 + j)];
                    const float b = scratch[0][(size_t) (preLen - span + j)] + scratch[1][(size_t) (preLen - span + j)];
                    num += a * b; ea += a * a; eb += b * b;
                }
                const double score = num / std::sqrt (ea * eb + 1e-12);
                if (score > bestScore + 1e-4) { bestScore = score; bestTrim = d; }
            }
            len -= bestTrim;
        }

        // Seamless wrap: the loop's end fades into the audio that originally led into its start
        const int x = std::min (preLen, len / 4);
        for (int c = 0; c < 2; ++c)
        {
            auto& b = bufA[(size_t) c];
            for (int k = 0; k < x; ++k)
            {
                const float t = (float) (k + 1) / (float) (x + 1);
                const float gIn = 0.5f - 0.5f * std::cos (kPi * t), gOut = 1.0f - gIn;   // equal gain: the two sides are matched
                const size_t at = (size_t) (recBase + len - x + k);
                b[at] = b[at] * gOut + scratch[(size_t) c][(size_t) (preLen - x + k)] * gIn;
            }
        }

        if ((m2Playing || mode3Playing) && pendingAction == None)
        {
            // swap loops behind a quick fade
            pendingAction = StartLoop; pendBase = recBase; pendLen = len;
            gateTarget = 0.0f;
        }
        else { startLoop (recBase, len, p); gate = 0.0f; }
    }

    void startLoop (int base, int len, const Params& p)
    {
        playBase = base;
        loopLen = len;
        if (activeMode == 1)
        {
            m2PlayLen = len;
            m2Playing = true;
            startSlice (p);
        }
        else
        {
            mode3Playing = true;
            for (auto& h : heads) h = Tap{};
            loopClock = 0.0;
        }
    }

    void trigger (const Params& p)
    {
        if (activeBank != 0 || activeMode != 0) return;
        double target = unitA.tap.age + std::max (0.02f, p.jump) * unitA.L;
        if (target > unitA.L + DelayUnit::kGuard) target -= unitA.L;
        unitA.jumpTo (target, fadeLen / 2);
        ++status.triggerCount;
    }

    // Onsets are judged against how loud you've been playing, never a fixed level
    void detectOnset (float l, float r, const Params& p)
    {
        const float x = std::max (std::fabs (l), std::fabs (r));
        const float aF = x > envFast ? 0.5f : 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        envFast += aF * (x - envFast);
        envSlow += (1.0f - std::exp (-1.0f / (0.12f * (float) sr))) * (envFast - envSlow);
        const float aP = envFast > playLevel ? 1.0f - std::exp (-1.0f / (0.05f * (float) sr)) : 1.0f - std::exp (-1.0f / (4.0f * (float) sr));
        playLevel += aP * (envFast - playLevel);
        if (refractory > 0) { --refractory; return; }
        if (p.trigSens >= 0.98f || activeBank != 0 || activeMode != 0) return;
        const float ratio = std::pow (10.0f, (2.0f + 14.0f * p.trigSens) / 20.0f);
        const float floor = std::max (2.0e-5f, playLevel * 0.0708f);   // 23 dB under your playing, never under -94 dBFS
        if (armedOnset && envFast > envSlow * ratio && envFast > floor)
        {
            trigger (p);
            armedOnset = false;
            refractory = (int) (0.08 * sr);
        }
        else if (envFast < envSlow * 1.05f) armedOnset = true;
    }

    //==========================================================================
    double targetLength (int step, const Params& p, bool limitToLoop) const
    {
        const double secs = p.sync ? lenBBeats (step) * (beatSamples / sr) : lenBSeconds (step);
        double len = std::min (kLoopSeconds * sr, secs * sr);
        if (limitToLoop && p.transcend && loopLen >= 64) len = std::min (len, (double) loopLen);
        return std::max (len, 32.0);
    }

    float speedFor (int head, float knob, const Params& p) const
    {
        float s = dirToSpeed (knob, p.quant);
        if (p.reverse) s = -s;
        (void) head;
        return s;
    }

    float shapeFeedback (float x, int ch)
    {
        x = fbHp[(size_t) ch].process (x);
        x = fbLp[(size_t) ch].process (x, fbA);
        x = fbTape[(size_t) ch].process (x, tapeA);
        return tapeSat (x);
    }

    //==========================================================================
    // Mode 1: always-listening pitch delay
    void mode1 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        const double want = targetLength (p.lenB, p, true);
        if (std::abs (want - lastLenSamples) > 0.5)
        {
            if (lastLenSamples < 0.0) unitA.setFresh (want);
            else if (p.transcend) unitA.fitInto (want, fadeLen);
            else requestWipe (want);   // the pedal wipes on a length change; we fade first
            lastLenSamples = want;
        }

        lfoPhase += lfoHz / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        const float knob = p.dir1 + 0.25f * p.lfoDepth * std::sin (2.0f * kPi * (float) lfoPhase);
        speedNow[0] += slewA * (speedFor (0, knob, p) - speedNow[0]);

        unitA.frozen = held;
        unitA.read (speedNow[0], yl, yr);

        const float g = p.fbk * 1.05f;
        const float fl = shapeFeedback (yl, 0), fr = shapeFeedback (yr, 1);
        if (p.spread)
        {
            // ping-pong: the first repeat lands left, the next right, and so on
            unitA.write (0.5f * (xl + xr) + g * fr, g * fl);
        }
        else unitA.write (xl + g * fl, xr + g * fr);
        unitA.step (speedNow[0], fadeLen);
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
        ++sliceCount;
    }

    void writeRecording (float xl, float xr)
    {
        if (! recording) return;
        const size_t idx = (size_t) (recBase + recPos);
        if (recWrapped) { bufA[0][idx] = softCeiling (bufA[0][idx] + xl, 0.6f, 0.891f); bufA[1][idx] = softCeiling (bufA[1][idx] + xr, 0.6f, 0.891f); }
        else            { bufA[0][idx] = xl; bufA[1][idx] = xr; }
        if (++recPos >= recCap) { recPos = 0; recWrapped = true; }
    }

    void mode2 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        writeRecording (xl, xr);
        if (p.transcend && recording && m2Playing)
            m2PlayLen = std::max (m2PlayLen, recWrapped ? recCap : recPos);

        speedNow[0] += slewA * (speedFor (0, p.dir1, p) - speedNow[0]);
        if (! m2Playing || m2PlayLen < 64) { yl = yr = 0.0f; return; }

        const double sp = speedNow[0];
        const double src = sp >= 0.0 ? sliceStart + slicePhase : sliceStart + sliceLen - slicePhase;
        const double fade = std::max (8.0, expMap (p.sliceFade, 0.0005f, 0.02f) * sr);
        const float g = (float) std::clamp (std::min (slicePhase / fade, (sliceLen - slicePhase) / fade), 0.0, 1.0);
        BandlimitedReader::get().read (bufA[0].data(), bufA[1].data(), playBase, m2PlayLen, src, sp, yl, yr);
        yl *= g; yr *= g;

        if (p.spread)
        {
            // alternate slices lean left and right
            const float pan = (sliceCount & 1) ? 0.6f : -0.6f;
            const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
            yl *= gl; yr *= gr;
        }

        slicePhase += std::fabs (sp);
        if (slicePhase >= sliceLen) startSlice (p);
    }

    //==========================================================================
    // Mode 3: three heads on one loop
    void mode3 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        if (! mode3Playing) writeRecording (xl, xr);

        // step sequencer
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
        std::array<double, 3> target {};
        for (int h = 0; h < 3; ++h)
        {
            target[(size_t) h] = speedFor (h, knobs[h] + stepOffset[(size_t) h], p);
            speedNow[(size_t) h] += slewA * (target[(size_t) h] - speedNow[(size_t) h]);
        }

        yl = yr = 0.0f;
        if (! mode3Playing || loopLen < 64) return;

        const int active = std::clamp (p.heads, 1, 3);
        const float pans[3] = { 0.0f, -0.8f, 0.8f };
        const auto& reader = BandlimitedReader::get();
        for (int h = 0; h < 3; ++h)
        {
            auto& hd = heads[(size_t) h];
            const double sp = speedNow[(size_t) h];
            if (h < active)
            {
                float sl, s2;
                reader.read (bufA[0].data(), bufA[1].data(), playBase, loopLen, hd.age, sp, sl, s2);
                if (hd.fading)
                {
                    float tl, t2;
                    reader.read (bufA[0].data(), bufA[1].data(), playBase, loopLen, hd.twin, sp, tl, t2);
                    const float ga = std::cos (0.5f * kPi * hd.xf), gb = std::sin (0.5f * kPi * hd.xf);
                    sl = sl * ga + tl * gb; s2 = s2 * ga + t2 * gb;
                }
                const float v = p.headVol[(size_t) h];
                sl *= v; s2 *= v;
                const float pan = p.spread ? pans[h] : 0.0f;
                if (pan == 0.0f) { yl += sl; yr += s2; }
                else
                {
                    const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.41421f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.41421f;
                    const float m = 0.5f * (sl + s2);
                    yl += m * gl; yr += m * gr;
                }
            }
            hd.advance (sp);
            hd.age = wrap (hd.age, loopLen);
            if (hd.fading) hd.twin = wrap (hd.twin, loopLen);

            // LOCK: once a head settles on its speed, keep it in step with the loop
            if (p.lock && ! hd.fading && std::abs (sp - target[(size_t) h]) < 1.0e-4)
            {
                const double ideal = wrap (loopClock * sp, loopLen);
                double d = std::abs (ideal - hd.age);
                d = std::min (d, loopLen - d);
                if (d > 24.0) hd.startFade (ideal, fadeLen + fadeLen / 2);
            }
        }
        const float norm = 1.0f / std::sqrt ((float) active);
        yl *= norm; yr *= norm;

        // Overdub, written after the heads have read, so you hear the loop and not yourself.
        // It fades in and out over 5 ms, each pass keeps the old layer a little quieter, and it never clips.
        dubGain += ((recording ? 1.0f : 0.0f) - dubGain) * dubA;
        if (dubGain > 1.0e-4f)
        {
            const size_t idx = (size_t) (playBase + wrapIndex ((long long) loopClock, loopLen));
            const float k = 1.0f - dubGain * (1.0f - fadeKeep (p));
            bufA[0][idx] = softCeiling (bufA[0][idx] * k + xl * dubGain, 0.6f, 0.891f);
            bufA[1][idx] = softCeiling (bufA[1][idx] * k + xr * dubGain, 0.6f, 0.891f);
        }
        loopClock += 1.0;
        if (loopClock >= (double) loopLen * 1024.0) loopClock -= (double) loopLen * 1024.0;
    }

    static float fadeKeep (const Params& p)
    {
        // DUB FADE: 1.0 keeps every layer, 0.9 lowers the old layer 1 dB per pass, 0 replaces it
        return std::pow (10.0f, -10.0f * (1.0f - std::clamp (p.dubFade, 0.0f, 1.0f)) / 20.0f) * (p.dubFade <= 0.001f ? 0.0f : 1.0f);
    }

    //==========================================================================
    // Nightly 1: Ct5t5, two Mode 1 units feeding each other
    void nightly1 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        if (p.n1Len1 != lastN1a) { const double L = targetLength (p.n1Len1, p, false); if (lastN1a < 0) unitA.setFresh (L); else unitA.fitInto (L, fadeLen); lastN1a = p.n1Len1; }
        if (p.n1Len2 != lastN1b) { const double L = targetLength (p.n1Len2, p, false); if (lastN1b < 0) unitB.setFresh (L); else unitB.fitInto (L, fadeLen); lastN1b = p.n1Len2; }

        speedNow[0] += slewA * (speedFor (0, p.dir1, p) - speedNow[0]);
        speedNow[1] += slewA * (speedFor (1, p.dir2, p) - speedNow[1]);

        unitA.frozen = unitB.frozen = frozen;
        float al, ar, bl, br;
        unitA.read (speedNow[0], al, ar);
        unitB.read (speedNow[1], bl, br);

        const float a = onePoleCoef (expMap (p.n1Filter, 300.0f, 18000.0f), sr);
        const float g = p.fbk * 1.05f;
        unitA.write (xl + g * tapeSat (fbLp[0].process (fbHp[0].process (bl), a)), xr + g * tapeSat (fbLp[1].process (fbHp[1].process (br), a)));
        unitB.write (tapeSat (fbLp[2].process (fbHp[2].process (al), a)), tapeSat (fbLp[3].process (fbHp[3].process (ar), a)));
        unitA.step (speedNow[0], fadeLen);
        unitB.step (speedNow[1], fadeLen);

        if (p.spread) { yl = 0.85f * al + 0.4f * bl; yr = 0.4f * ar + 0.85f * br; }
        else          { yl = 0.7f * (al + bl); yr = 0.7f * (ar + br); }
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
                const int at = p.reverse ? u.len - 1 - u.pos : u.pos;
                const double fade = std::min (0.003 * sr, u.len / 4.0);
                const float g = (float) std::clamp (std::min (u.pos / fade, (u.len - u.pos) / fade), 0.0, 1.0);
                sl = u.buf[0][(size_t) at] * g;
                s2 = u.buf[1][(size_t) at] * g;
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
                yl += sl * std::sqrt (0.5f * (1.0f - pan)) * 1.41421f * v;
                yr += s2 * std::sqrt (0.5f * (1.0f + pan)) * 1.41421f * v;
            }
        }
        const float norm = 1.0f / std::sqrt ((float) active);
        yl *= norm; yr *= norm;
    }

    //==========================================================================
    // Nightly 3: long delay with two square-wave vibrato heads and a feedback head
    void nightly3 (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        unitA.L = std::min ((double) cap * 0.45, 7.5 * sr);
        const double tf = expMap (p.n3FbkTime, 0.5f, 7.0f) * sr;
        float fl, fr;
        unitA.readAge (tf, 1.0, fl, fr);

        yl = yr = 0.0f;
        const int active = std::clamp (p.heads, 1, 3);
        const auto& reader = BandlimitedReader::get();
        for (int h = 0; h < 2; ++h)
        {
            auto& hd = vibHead[(size_t) h];
            const double t = expMap (p.n3Time[(size_t) h], 0.5f, 7.0f) * sr;
            if (hd.age < DelayUnit::kGuard) hd.age = t;
            const double hz = expMap (p.n3Rate[(size_t) h], 0.1f, 12.0f);
            vibPhase[(size_t) h] += hz / sr;
            if (vibPhase[(size_t) h] >= 1.0) vibPhase[(size_t) h] -= 1.0;
            const float semis = quantiseSemis (p.n3Depth[(size_t) h] * 12.0f, p.quant);
            const float sq = vibPhase[(size_t) h] < 0.5 ? 1.0f : -1.0f;
            const float sm = vibSmooth[(size_t) h].process (sq, 1.0f - std::exp (-1.0f / (0.003f * (float) sr)));
            double speed = std::pow (2.0, sm * semis / 12.0);
            if (p.reverse) speed = -speed;
            vibSpeed[(size_t) h] = speed;

            if (h < active)
            {
                float sl, s2;
                const double pos = (double) unitA.w - hd.age;
                reader.read (bufA[0].data(), bufA[1].data(), 0, cap, pos, speed, sl, s2);
                if (hd.fading)
                {
                    float tl, t2;
                    reader.read (bufA[0].data(), bufA[1].data(), 0, cap, (double) unitA.w - hd.twin, speed, tl, t2);
                    sl = sl * hd.gainA() + tl * hd.gainB();
                    s2 = s2 * hd.gainA() + t2 * hd.gainB();
                }
                if (p.spread) { if (h == 0) { yl += sl; yr += 0.3f * s2; } else { yl += 0.3f * sl; yr += s2; } }
                else { yl += sl; yr += s2; }
            }

            // forwards the head hovers round its delay time; reversed it plays chunks backwards
            hd.advance ((frozen ? 0.0 : 1.0) - speed);
            if (! p.reverse) hd.age += (t - hd.age) * 0.00005;
            if (! hd.fading)
            {
                if (hd.age > t * 1.5) hd.startFade (hd.age - t, fadeLen);
                else if (hd.age < std::max ((double) DelayUnit::kGuard, t * 0.5)) hd.startFade (hd.age + t * 0.5, fadeLen);
            }
        }
        if (active >= 3) { yl += fl; yr += fr; }
        yl *= 0.7f; yr *= 0.7f;

        const float fb = frozen ? 1.0f : std::min (p.n3Fbk, 0.98f);
        const float in = frozen ? 0.0f : 1.0f;
        unitA.frozen = false;
        unitA.write (in * xl + fb * tapeSat (fl), in * xr + fb * tapeSat (fr));
        speedNow[0] = 1.0;
    }

    //==========================================================================
    void fillStatus()
    {
        if (activeBank == 1)       status.state = frozen ? Frozen : Listening;
        else if (activeMode == 0)  status.state = held ? Held : Listening;
        else if (armed)            status.state = Armed;
        else if (recording)        status.state = Recording;
        else if (m2Playing || mode3Playing) status.state = Playing;
        else                       status.state = Empty;

        status.loopSeconds = (float) (loopLen / sr);
        status.recFraction = recCap > 0 ? (float) (recWrapped ? 1.0 : (double) recPos / recCap) : 0.0f;
        if (mode3Playing && loopLen > 0) status.playFraction = (float) (heads[0].age / loopLen);
        else if (m2Playing && m2PlayLen > 0) status.playFraction = (float) (wrap (sliceStart + slicePhase, m2PlayLen) / m2PlayLen);
        else status.playFraction = 0.0f;
    }
};

} // namespace backtrack
