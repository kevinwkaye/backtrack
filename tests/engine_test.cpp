// Offline checks for the Backtrack engine: every mode runs, stays finite,
// produces sound, and the read head plays at the expected pitch.
#include "../Source/Engine/BacktrackEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

using namespace backtrack;

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

struct Result { float rms = 0, peak = 0; bool finite = true; std::vector<float> l; };

static Result run (Engine& e, Params p, double seconds, double sr, float freq,
                   double recFrom = -1, double recTo = -1, double tapAt = -1, bool gated = true)
{
    Result r;
    const int block = 256;
    const int total = (int) (seconds * sr);
    std::vector<float> il (block), ir (block), ol (block), orr (block);
    double phase = 0;
    double sum = 0; int count = 0;
    for (int done = 0; done < total; done += block)
    {
        const double t = done / sr;
        if (recFrom >= 0 && t >= recFrom && t < recFrom + block / sr) e.softDown();
        if (recTo >= 0 && t >= recTo && t < recTo + block / sr) e.softUp();
        if (tapAt >= 0 && t >= tapAt && t < tapAt + block / sr) e.stopAndClear();
        for (int i = 0; i < block; ++i)
        {
            const bool gate = ! gated || std::fmod (t + i / sr, 1.0) < 0.6;
            const float x = gate ? 0.4f * (float) std::sin (phase) : 0.0f;
            phase += 2 * 3.14159265358979 * freq / sr;
            il[(size_t) i] = x; ir[(size_t) i] = x;
        }
        e.process (il.data(), ir.data(), ol.data(), orr.data(), block, p);
        for (int i = 0; i < block; ++i)
        {
            if (! std::isfinite (ol[(size_t) i]) || ! std::isfinite (orr[(size_t) i])) r.finite = false;
            r.peak = std::max (r.peak, std::fabs (ol[(size_t) i]));
            if (t > seconds * 0.5) { sum += ol[(size_t) i] * ol[(size_t) i]; ++count; }
            r.l.push_back (ol[(size_t) i]);
        }
    }
    r.rms = count ? (float) std::sqrt (sum / count) : 0.0f;
    return r;
}

// Rough pitch from zero crossings over a window
static float pitchOf (const std::vector<float>& x, double sr, size_t from, size_t to)
{
    int crossings = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i)
        if ((x[i - 1] < 0) != (x[i] < 0)) ++crossings;
    return (float) (crossings / 2.0 / ((to - from) / sr));
}

int main()
{
    const double sr = 48000.0;

    // Quantiser
    check (std::fabs (dirToSpeed (0.75f, 0) - 1.0f) < 1e-5, "DIR 1 at 2 o'clock is unity forwards");
    check (dirToSpeed (0.5f, 0) == 0.0f, "DIR 1 at noon stops the head");
    check (std::fabs (dirToSpeed (1.0f, 5) - 2.0f) < 1e-4, "Fully clockwise in 5ths mode is an octave up");
    check (std::fabs (dirToSpeed (0.87f, 5) - std::pow (2.0f, 7.0f / 12)) < 1e-3, "5ths mode snaps to a fifth");
    check (dirToSpeed (0.0f, 1) < -1.99f, "Fully anticlockwise is double speed reverse");

    Engine e;

    // Mode 1, wet only, unity, no feedback: should echo at input pitch
    {
        e.prepare (sr);
        Params p; p.mode = 0; p.mix = 1.0f; p.fbk = 0.0f; p.lenB = 4; p.grit = false; p.boost = false;
        auto r = run (e, p, 3.0, sr, 440.0f);
        check (r.finite && r.rms > 0.05f, "Mode 1 delay makes sound");
        const float f = pitchOf (r.l, sr, (size_t) (2.3 * sr), (size_t) (2.5 * sr));
        check (std::fabs (f - 440.0f) < 15.0f, "Mode 1 at unity keeps pitch (" + std::to_string (f) + " Hz)");
    }
    // Mode 1, octave up
    {
        e.prepare (sr);
        Params p; p.mode = 0; p.mix = 1.0f; p.fbk = 0.0f; p.lenB = 5; p.dir1 = 1.0f; p.quant = 5; p.grit = false;
        auto r = run (e, p, 3.0, sr, 220.0f, -1, -1, -1, false);
        const float f = pitchOf (r.l, sr, (size_t) (2.3 * sr), (size_t) (2.5 * sr));
        check (r.finite && std::fabs (f - 440.0f) < 30.0f, "Mode 1 octave up doubles pitch (" + std::to_string (f) + " Hz)");
    }
    // Mode 1, heavy feedback, reverse, LFO, trigger: must stay finite and bounded
    {
        e.prepare (sr);
        Params p; p.mode = 0; p.mix = 1.0f; p.fbk = 1.0f; p.lenB = 2; p.dir1 = 0.2f; p.lfoDepth = 1.0f; p.trigSens = 0.3f;
        auto r = run (e, p, 4.0, sr, 330.0f);
        check (r.finite && r.peak < 2.5f, "Mode 1 runaway feedback stays bounded (peak " + std::to_string (r.peak) + ")");
    }
    // Mode 2: record 1.5 s then slices play
    {
        e.prepare (sr);
        Params p; p.mode = 1; p.mix = 1.0f; p.lenS = 0.4f; p.rand = 0.8f;
        auto r = run (e, p, 4.0, sr, 440.0f, 0.2, 1.7);
        check (r.finite && r.rms > 0.02f && e.getStatus().state == Playing, "Mode 2 records and plays slices");
    }
    // Mode 3: record, three heads
    {
        e.prepare (sr);
        Params p; p.mode = 2; p.mix = 1.0f; p.dir1 = 0.75f; p.dir2 = 0.25f; p.dir3 = 1.0f; p.heads = 3; p.stepHeads = 0.9f;
        auto r = run (e, p, 5.0, sr, 440.0f, 0.2, 2.2);
        check (r.finite && r.rms > 0.02f && e.getStatus().state == Playing, "Mode 3 loops with three heads");
        check (std::fabs (e.getStatus().loopSeconds - 2.0f) < 0.05f, "Mode 3 loop length matches the hold (" + std::to_string (e.getStatus().loopSeconds) + " s)");
    }
    // Mode 3 tap clears
    {
        e.prepare (sr);
        Params p; p.mode = 2; p.mix = 1.0f;
        run (e, p, 3.0, sr, 440.0f, 0.2, 1.2, 2.5);
        check (e.getStatus().state == Empty, "Stop pad clears the Mode 3 loop");
    }
    // Synced loop length snaps to whole beats at 120 bpm
    {
        e.prepare (sr);
        Params p; p.mode = 2; p.mix = 1.0f; p.sync = true; p.bpm = 120.0;
        run (e, p, 3.0, sr, 440.0f, 0.2, 1.5);
        check (std::fabs (e.getStatus().loopSeconds - 1.5f) < 0.02f, "Sync rounds the loop to whole beats (" + std::to_string (e.getStatus().loopSeconds) + " s)");
    }
    // Nightly modes
    for (int m = 0; m < 3; ++m)
    {
        e.prepare (sr);
        Params p; p.bank = 1; p.mode = m; p.mix = 1.0f; p.fbk = 0.6f; p.heads = 3;
        auto r = run (e, p, 4.0, sr, 440.0f);
        check (r.finite && r.rms > 0.01f && r.peak < 3.0f, "Nightly mode " + std::to_string (m + 1) + " makes bounded sound (rms " + std::to_string (r.rms) + ")");
    }
    // Transcendence: loop recorded in Mode 3 survives a switch to Mode 2
    {
        e.prepare (sr);
        Params p; p.mode = 2; p.mix = 1.0f; p.transcend = true;
        run (e, p, 3.0, sr, 440.0f, 0.2, 1.7);
        p.mode = 1;
        auto r = run (e, p, 2.0, sr, 440.0f);
        check (e.getStatus().state == Playing && r.rms > 0.01f, "Transcend carries the loop into Mode 2");
    }
    // Without transcendence a mode switch wipes
    {
        e.prepare (sr);
        Params p; p.mode = 2; p.mix = 1.0f;
        run (e, p, 3.0, sr, 440.0f, 0.2, 1.7);
        p.mode = 1;
        run (e, p, 0.5, sr, 440.0f);
        check (e.getStatus().state == Empty, "Mode switch wipes without transcend");
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
