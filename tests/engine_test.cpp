// Offline checks for the Backtrack engine. Two kinds:
//  1. Behaviour: every mode runs, records, plays, clears and switches as the pedal does.
//  2. Sound, with pass marks: no aliasing on pitch-ups, no clicks on any switch or seam,
//     triggers from quiet playing, overdubs that never clip, and every factory preset
//     rendered with four kinds of playing.
#include "../Source/Engine/ParamSpec.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

using namespace backtrack;

static const double SR = 48000.0;
static const double TAU = 6.283185307179586;
static int failures = 0;

static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}
static std::string fmt (const char* f, double v) { char b[64]; std::snprintf (b, sizeof b, f, v); return b; }

using Gen = std::function<float (long)>;
using Event = std::function<void (double, Params&, Engine&)>;

struct Out { std::vector<float> l, r; };

static Out render (Engine& e, Params p, double secs, Gen g, Event ev = {}, int block = 64)
{
    Out o;
    std::vector<float> il ((size_t) block), ir ((size_t) block), ol ((size_t) block), orr ((size_t) block);
    long n = 0;
    for (long done = 0; done < (long) (secs * SR); done += block)
    {
        if (ev) ev (done / SR, p, e);
        for (int i = 0; i < block; ++i) il[(size_t) i] = ir[(size_t) i] = g (n + i);
        e.process (il.data(), ir.data(), ol.data(), orr.data(), block, p);
        o.l.insert (o.l.end(), ol.begin(), ol.end());
        o.r.insert (o.r.end(), orr.begin(), orr.end());
        n += block;
    }
    return o;
}

// A steady sine that fades in over 20 ms, so the test signal itself never clicks
static Gen sine (double hz, float amp)
{
    return [=] (long n) { const double t = n / SR; return (float) (amp * std::min (1.0, t / 0.02) * std::sin (TAU * hz * t)); };
}

// Press events at given times
static Event presses (std::vector<std::pair<double, bool>> list)
{
    return [list] (double t, Params&, Engine& e)
    {
        for (auto& ev : list)
            if (t >= ev.first && t < ev.first + 64 / SR) { if (ev.second) e.softDown(); else e.softUp(); }
    };
}

static double tone (const std::vector<float>& x, size_t a, size_t b, double f)
{
    double s1 = 0, s2 = 0;
    const double c = 2 * std::cos (TAU * f / SR);
    for (size_t i = a; i < b; ++i)
    {
        const double s = x[i] * 0.5 * (1 - std::cos (TAU * (double) (i - a) / (double) (b - a))) + c * s1 - s2;
        s2 = s1; s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double) (b - a) * (double) (b - a));
}
static double db (double v) { return 10 * std::log10 (v + 1e-30); }

// Largest second difference in a window, relative to that of the steady test tone.
// A clean fade or crossfade stays near 1; a click is many times larger.
static double clickRatio (const std::vector<float>& x, double from, double to, double hz, float amp)
{
    const double steady = amp * std::pow (TAU * hz / SR, 2.0);
    double mx = 0;
    size_t at = 0;
    for (size_t i = (size_t) (from * SR) + 2; i < (size_t) (to * SR) && i < x.size(); ++i)
    {
        const double d = std::fabs (x[i] - 2 * x[i - 1] + x[i - 2]);
        if (d > mx) { mx = d; at = i; }
    }
    if (std::getenv ("BT_DIAG") && mx / steady > 3 && at > 3)
        std::printf ("      worst at %.4f s: %.5f %.5f %.5f %.5f %.5f\n", at / SR, x[at - 3], x[at - 2], x[at - 1], x[at], x[at + 1]);
    return mx / steady;
}

static double swingDb (const std::vector<float>& x, double from, double to, double hz)
{
    // windows of a whole number of cycles, so a steady tone measures perfectly flat
    const double cycles = std::ceil (0.02 * hz);
    const size_t w = (size_t) std::lround (cycles / hz * SR);
    double lo = 1e9, hi = 0;
    for (size_t i = (size_t) (from * SR); i + w < (size_t) (to * SR); i += w)
    {
        double s = 0;
        for (size_t k = 0; k < w; ++k) s += x[i + k] * x[i + k];
        s = std::sqrt (s / w);
        lo = std::min (lo, s); hi = std::max (hi, s);
    }
    return 20 * std::log10 (hi / (lo + 1e-12));
}

static float pitchOf (const std::vector<float>& x, double from, double to)
{
    int c = 0;
    for (size_t i = (size_t) (from * SR) + 1; i < (size_t) (to * SR); ++i)
        if ((x[i - 1] < 0) != (x[i] < 0)) ++c;
    return (float) (c / 2.0 / (to - from));
}

static double rms (const std::vector<float>& x, double from, double to)
{
    double s = 0; size_t n = 0;
    for (size_t i = (size_t) (from * SR); i < (size_t) (to * SR) && i < x.size(); ++i) { s += x[i] * x[i]; ++n; }
    return n ? std::sqrt (s / n) : 0.0;
}

static Params clean()
{
    Params p = defaultParams();
    p.mix = 1.0f; p.grit = 0; p.boost = false; p.spread = false;
    return p;
}

int main()
{
    Engine e;

    //==========================================================================
    std::printf ("Behaviour\n");
    check (std::fabs (dirToSpeed (0.75f, 0) - 1.0f) < 1e-5, "DIR 1 at 2 o'clock is unity forwards");
    check (dirToSpeed (0.5f, 0) == 0.0f, "DIR 1 at noon stops the head");
    check (std::fabs (dirToSpeed (1.0f, 5) - 2.0f) < 1e-4, "Fully clockwise in 5ths mode is an octave up");
    check (std::fabs (dirToSpeed (0.87f, 5) - std::pow (2.0f, 7.0f / 12)) < 1e-3, "5ths mode snaps to a fifth");
    check (dirToSpeed (0.0f, 1) < -1.99f, "Fully anticlockwise is double speed reverse");

    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 4;
        auto o = render (e, p, 3.0, sine (440, 0.3f));
        const float f = pitchOf (o.l, 2.3, 2.5);
        check (rms (o.l, 1, 3) > 0.1 && std::fabs (f - 440) < 10, "Mode 1 at unity keeps pitch (" + fmt ("%.1f Hz)", f));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 5;
        int onsetAt = -1;
        auto o = render (e, p, 1.5, [] (long n) { return n >= 4800 && n < 4800 + 480 ? 0.5f * (float) std::sin (TAU * 1000 * (n - 4800) / SR) : 0.0f; });
        for (size_t i = 4800 + 2000; i < o.l.size(); ++i) if (std::fabs (o.l[i]) > 0.05f) { onsetAt = (int) i; break; }
        const double ms = (onsetAt - 4800) / SR * 1000;
        check (std::fabs (ms - 500) < 3, "LEN B step 6 echoes after 500 ms (" + fmt ("%.1f ms)", ms));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 5; p.dir1 = 1.0f; p.quant = 5;
        auto o = render (e, p, 3.0, sine (220, 0.3f));
        const float f = pitchOf (o.l, 2.3, 2.5);
        check (std::fabs (f - 440) < 10, "Mode 1 octave up doubles pitch (" + fmt ("%.1f Hz)", f));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 5; p.reverse = true;
        auto o = render (e, p, 3.0, sine (330, 0.3f));
        check (rms (o.l, 2, 3) > 0.05, "Footswitch C reverse plays the buffer backwards");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.fbk = 1.0f; p.lenB = 2; p.dir1 = 0.2f; p.lfoDepth = 1.0f; p.trigSens = 0.3f;
        auto o = render (e, p, 4.0, sine (330, 0.5f));
        double pk = 0; bool fin = true;
        for (float v : o.l) { pk = std::max (pk, (double) std::fabs (v)); fin = fin && std::isfinite (v); }
        check (fin && pk < 0.97, "Runaway feedback stays under the output ceiling (" + fmt ("peak %.3f)", pk));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 1; p.lenS = 0.4f; p.rand = 0.8f;
        auto o = render (e, p, 4.0, sine (440, 0.3f), presses ({ { 0.2, true }, { 1.7, false } }));
        check (rms (o.l, 2, 4) > 0.02 && e.getStatus().state == Playing, "Mode 2 records and plays slices");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.dir1 = 0.75f; p.dir2 = 0.25f; p.dir3 = 1.0f; p.heads = 3; p.stepHeads = 0.9f;
        auto o = render (e, p, 5.0, sine (440, 0.3f), presses ({ { 0.2, true }, { 2.2, false } }));
        check (rms (o.l, 3, 5) > 0.02 && e.getStatus().state == Playing, "Mode 3 loops with three heads");
        check (std::fabs (e.getStatus().loopSeconds - 2.0f) < 0.02f, "Mode 3 loop length matches the hold (" + fmt ("%.3f s)", e.getStatus().loopSeconds));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2;
        render (e, p, 3.0, sine (440, 0.3f), [] (double t, Params&, Engine& en)
                { if (std::fabs (t - 0.2) < 0.0007) en.softDown(); if (std::fabs (t - 1.2) < 0.0007) en.softUp();
                  if (std::fabs (t - 2.0) < 0.0007) en.softDown(); if (std::fabs (t - 2.1) < 0.0007) en.softUp(); });
        check (e.getStatus().state == Empty, "Tapping B stops and clears the Mode 3 loop");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.sync = true; p.bpm = 120;
        render (e, p, 3.0, sine (440, 0.3f), presses ({ { 0.2, true }, { 1.5, false } }));
        check (std::fabs (e.getStatus().loopSeconds - 1.5f) < 0.005f, "Sync rounds the loop to whole beats (" + fmt ("%.3f s)", e.getStatus().loopSeconds));
    }
    for (int m = 0; m < 3; ++m)
    {
        e.prepare (SR);
        Params p = clean(); p.bank = 1; p.mode = m; p.fbk = 0.6f; p.heads = 3;
        auto o = render (e, p, 4.0, sine (440, 0.3f));
        double pk = 0; for (float v : o.l) pk = std::max (pk, (double) std::fabs (v));
        check (rms (o.l, 2, 4) > 0.01 && pk < 0.97, "Nightly mode " + std::to_string (m + 1) + " makes bounded sound" + fmt (" (rms %.3f)", rms (o.l, 2, 4)));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.transcend = true;
        render (e, p, 3.0, sine (440, 0.3f), presses ({ { 0.2, true }, { 1.7, false } }));
        p.mode = 1;
        auto o = render (e, p, 2.0, sine (440, 0.3f));
        check (e.getStatus().state == Playing && rms (o.l, 1, 2) > 0.01, "Transcend carries the loop into Mode 2");
        p.mode = 0;
        o = render (e, p, 2.0, [] (long) { return 0.0f; });
        check (rms (o.l, 0.2, 1.0) > 0.01, "Transcend carries the loop into Mode 1");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2;
        render (e, p, 3.0, sine (440, 0.3f), presses ({ { 0.2, true }, { 1.7, false } }));
        p.mode = 1;
        render (e, p, 0.5, sine (440, 0.3f));
        check (e.getStatus().state == Empty, "Mode change wipes without Transcend");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2;
        render (e, p, 1.0, sine (220, 0.3f), presses ({ { 0.2, true }, { 0.4, false } }));
        check (e.getStatus().state == Empty, "A loop under 0.25 s is thrown away");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2;
        // a bounce: second press 30 ms after release is ignored
        render (e, p, 2.0, sine (220, 0.3f), presses ({ { 0.2, true }, { 1.2, false }, { 1.23, true }, { 1.25, false } }));
        check (e.getStatus().state == Playing, "A switch bounce within 80 ms is ignored");
    }

    //==========================================================================
    std::printf ("\nSound\n");
    // Aliasing on pitch-ups, in every mode that plays faster than recorded
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 6; p.dir1 = 1.0f; p.quant = 5;
        auto o = render (e, p, 2.0, sine (15000, 0.3f));
        const double a = db (0.3 * 0.3 / 4) - db (tone (o.l, 48000, 96000, 18000));
        check (a > 80, "Mode 1 octave up: 15 kHz leaves no alias at 18 kHz (" + fmt ("%.0f dB down)", a));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 6; p.dir1 = speedToDir (1.5f);
        auto o = render (e, p, 2.0, sine (17000, 0.3f));
        const double a = db (0.3 * 0.3 / 4) - db (tone (o.l, 48000, 96000, 48000 - 25500));
        check (a > 80, "Mode 1 at 1.5x: 17 kHz leaves no alias at 22.5 kHz (" + fmt ("%.0f dB down)", a));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.heads = 1; p.dir1 = 1.0f;
        auto o = render (e, p, 4.0, sine (15000, 0.3f), presses ({ { 0.1, true }, { 1.6, false } }));
        const double a = db (0.3 * 0.3 / 4) - db (tone (o.l, (size_t) (2.0 * SR), (size_t) (3.5 * SR), 18000));
        check (a > 80, "Mode 3 head at +12: no alias (" + fmt ("%.0f dB down)", a));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 1; p.dir1 = 1.0f; p.lenS = 0.8f;
        auto o = render (e, p, 4.0, sine (15000, 0.3f), presses ({ { 0.1, true }, { 1.6, false } }));
        const double a = db (0.3 * 0.3 / 4) - db (tone (o.l, (size_t) (2.0 * SR), (size_t) (3.5 * SR), 18000));
        check (a > 80, "Mode 2 slice at +12: no alias (" + fmt ("%.0f dB down)", a));
    }
    // The Mode 1 seam, where the read head laps the write head
    for (float speed : { 1.498f, 2.0f, 0.5f, -1.0f })
    {
        e.prepare (SR);
        Params p = clean(); p.lenB = 5; p.dir1 = speedToDir (speed);
        auto o = render (e, p, 3.0, sine (220, 0.3f));
        const double sw = swingDb (o.l, 1.5, 3.0, 220 * std::fabs (speed));
        const double c = clickRatio (o.l, 1.5, 3.0, 220 * std::fabs (speed), 0.3f);
        check (sw < 0.5 && c < 3, "Mode 1 seam at " + fmt ("%.2fx", speed) + fmt (": swing %.2f dB", sw) + fmt (", click %.1f", c));
    }
    // Every switch that changes what you hear: no clicks
    struct Switch { const char* name; Event ev; Params p; double hz; };
    std::vector<Switch> switches;
    { Params p = clean(); p.mix = 0.7f; p.fbk = 0.5f; switches.push_back ({ "Bypass", [] (double t, Params& q, Engine&) { q.on = t < 1.0; }, p, 220 }); }
    { Params p = clean(); p.mix = 0.7f; p.fbk = 0.5f; switches.push_back ({ "Bypass with trails", [] (double t, Params& q, Engine&) { q.on = t < 1.0; q.trails = true; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.5f; switches.push_back ({ "Mode 1 to Mode 3", [] (double t, Params& q, Engine&) { q.mode = t < 1.0 ? 0 : 2; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.5f; switches.push_back ({ "Standard to Nightly", [] (double t, Params& q, Engine&) { q.bank = t < 1.0 ? 0 : 1; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.5f; switches.push_back ({ "LEN B step", [] (double t, Params& q, Engine&) { q.lenB = t < 1.0 ? 5 : 6; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.5f; p.transcend = true; switches.push_back ({ "LEN B step with Transcend", [] (double t, Params& q, Engine&) { q.lenB = t < 1.0 ? 5 : 4; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.3f; switches.push_back ({ "Reverse", [] (double t, Params& q, Engine&) { q.reverse = t >= 1.0; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.3f; switches.push_back ({ "Low keep on", [] (double t, Params& q, Engine&) { q.lowKeep = t >= 1.0; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.3f; switches.push_back ({ "Boost on", [] (double t, Params& q, Engine&) { q.boost = t >= 1.0; }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.3f; switches.push_back ({ "Trigger (tap B)", [] (double t, Params&, Engine& en) { if (std::fabs (t - 1.0) < 0.0007) en.softDown(); if (std::fabs (t - 1.1) < 0.0007) en.softUp(); }, p, 220 }); }
    { Params p = clean(); p.fbk = 0.3f; switches.push_back ({ "Freeze (hold B)", [] (double t, Params&, Engine& en) { if (std::fabs (t - 1.0) < 0.0007) en.softDown(); if (std::fabs (t - 1.8) < 0.0007) en.softUp(); }, p, 220 }); }
    for (auto& s : switches)
    {
        e.prepare (SR);
        auto o = render (e, s.p, 2.5, sine (s.hz, 0.3f), s.ev);
        const double c = clickRatio (o.l, 0.9, 2.2, s.hz, 0.3f);
        check (c < 3, std::string (s.name) + fmt (": click %.1f", c));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.heads = 1;
        auto o = render (e, p, 3.5, sine (220, 0.3f), presses ({ { 0.2, true }, { 1.5, false }, { 3.0, true }, { 3.1, false } }));
        const double c = clickRatio (o.l, 1.45, 3.45, 220, 0.3f);
        check (c < 3, "Mode 3 loop start and stop" + fmt (": click %.1f", c));
    }
    // Loop wrap: recorded mid-cycle, at three lengths
    for (double hold : { 1.013, 0.517, 2.371 })
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.heads = 1;
        auto o = render (e, p, 0.2 + hold + 3.0, sine (233, 0.3f), presses ({ { 0.2, true }, { 0.2 + hold, false } }));
        const double from = 0.3 + hold, to = from + 2.8;
        const double c = clickRatio (o.l, from, to, 233, 0.3f), sw = swingDb (o.l, from, to, 233);
        check (c < 2 && sw < 0.5, "Mode 3 loop wrap, " + fmt ("%.3f s loop", hold) + fmt (": click %.1f", c) + fmt (", swing %.2f dB", sw));
    }
    // Triggers judged against your playing level
    for (double level : { 0.3, 0.01, 0.001 })
    {
        e.prepare (SR);
        Params p = clean(); p.trigSens = 0.5f;
        render (e, p, 3.0, [level] (long n) { const double t = n / SR, ph = std::fmod (t, 0.5);
                                              return (float) (level * std::min (1.0, ph / 0.002) * std::exp (-4 * ph) * std::sin (TAU * 196 * t)); });
        const auto n = e.getStatus().triggerCount;
        check (n == 6, fmt ("Trigger, plucks at %.0f dBFS: ", 20 * std::log10 (level)) + std::to_string (n) + " of 6");
    }
    {
        e.prepare (SR);
        Params p = clean(); p.trigSens = 0.5f;
        render (e, p, 3.0, [] (long n) { return 0.003f * (float) std::sin (TAU * 196 * n / SR) * (1.0f + 0.2f * (float) std::sin (TAU * 5 * n / SR)); });
        check (e.getStatus().triggerCount <= 1, "Trigger ignores a held note with vibrato (" + std::to_string (e.getStatus().triggerCount) + ")");
    }
    // Overdubs never clip
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.heads = 1; p.dubFade = 1.0f;
        auto o = render (e, p, 7.0, sine (220, 0.5f), presses ({ { 0.1, true }, { 1.1, false }, { 1.5, true }, { 5.5, false } }));
        float bp = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < e.getLoopLength(); ++i) bp = std::max (bp, std::fabs (e.getBuffer (c)[(size_t) (e.getLoopBase() + i)]));
        double op = 0; for (float v : o.l) op = std::max (op, (double) std::fabs (v));
        check (bp <= 0.8913f && op < 0.97, "Four overdubs of a -6 dBFS tone: loop " + fmt ("%.2f dBFS", 20 * std::log10 (bp)) + fmt (", output %.2f dBFS", 20 * std::log10 (op)));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.mode = 2; p.heads = 1; p.dubFade = 0.9f;
        auto o = render (e, p, 6.0, [] (long n) { return n < (long) (1.0 * SR) ? 0.3f * (float) std::sin (TAU * 220 * n / SR) * (float) std::min (1.0, n / 960.0) : 0.0f; },
                         presses ({ { 0.0, true }, { 1.0, false }, { 1.2, true }, { 5.2, false } }));
        const double a = rms (o.l, 1.25, 2.0), b = rms (o.l, 4.25, 5.0);
        const double drop = 20 * std::log10 (a / (b + 1e-12));
        check (std::fabs (drop - 3.0) < 0.6, "Dub fade at 90% lowers the old layer about 1 dB a pass" + fmt (" (%.1f dB over 3 passes)", drop));
    }
    // Grit leaves quiet tails clean
    {
        e.prepare (SR);
        Params p = clean(); p.grit = 1; p.gritAmount = 0.3f;
        auto o = render (e, p, 2.0, sine (440, 0.0003f));
        const double h = db (tone (o.l, 48000, 96000, 440)) - db (tone (o.l, 48000, 96000, 1320));
        check (h > 60, "24-BIT grit on a -70 dBFS tail: no harmonic grain (" + fmt ("%.0f dB under)", h));
    }
    {
        e.prepare (SR);
        Params p = clean(); p.lowKeep = true; p.mix = 1.0f; p.lowKeepHz = 100;
        auto o = render (e, p, 2.0, sine (50, 0.3f));
        const double kept = rms (o.l, 0.6, 1.4);   // before the first echo arrives
        check (kept > 0.18, "Low keep holds the dry lows under a full wet mix" + fmt (" (%.2f rms)", kept));
    }

    //==========================================================================
    std::printf ("\nPresets\n");
    struct Input { const char* name; Gen g; };
    std::vector<Input> inputs {
        { "pluck", [] (long n) { const double t = n / SR, ph = std::fmod (t, 0.5); return (float) (0.3 * std::min (1.0, ph / 0.002) * std::exp (-3 * ph) * (std::sin (TAU * 196 * t) + 0.3 * std::sin (TAU * 392 * t))); } },
        { "quiet", [] (long n) { const double t = n / SR, ph = std::fmod (t, 0.5); return (float) (0.01 * std::min (1.0, ph / 0.002) * std::exp (-3 * ph) * std::sin (TAU * 330 * t)); } },
        { "pad",   [] (long n) { const double t = n / SR; return (float) (0.12 * std::min (1.0, t / 0.3) * (std::sin (TAU * 261.6 * t) + std::sin (TAU * 329.6 * t) + std::sin (TAU * 392 * t))); } },
        { "bass",  [] (long n) { const double t = n / SR, ph = std::fmod (t, 0.5); return (float) (0.4 * std::min (1.0, ph / 0.003) * std::exp (-2 * ph) * std::sin (TAU * 55 * t)); } },
    };
    const auto& presets = factoryPresets();
    check (presets.size() == 20, "Twenty factory presets");
    for (size_t pi = 0; pi < (std::getenv ("BT_QUICK") ? 0 : presets.size()); ++pi)
    {
        const auto& pr = presets[pi];
        Params base = defaultParams();
        int expT = 0; float lo = 0, hi = 1;
        for (auto& kv : presetValues (pr))
        {
            setParam (base, kv.first, kv.second);
            if (kv.first == "expTarget") expT = (int) kv.second;
            if (kv.first == "expLo") lo = kv.second;
            if (kv.first == "expHi") hi = kv.second;
        }
        bool ok = true;
        std::string worst;
        for (auto& in : inputs)
        {
            for (float pedal : { 0.0f, 1.0f })
            {
                Params p = base;
                applyExpression (p, expT, lo, hi, pedal);
                e.prepare (SR);
                const bool loops = p.bank == 0 && p.mode > 0;
                auto o = render (e, p, 5.0, in.g, loops ? presses ({ { 0.3, true }, { 2.3, false } }) : Event {});
                double pk = 0, jump = 0; bool fin = true;
                for (size_t i = 1; i < o.l.size(); ++i)
                {
                    fin = fin && std::isfinite (o.l[i]) && std::isfinite (o.r[i]);
                    pk = std::max ({ pk, (double) std::fabs (o.l[i]), (double) std::fabs (o.r[i]) });
                    jump = std::max (jump, (double) std::fabs (o.l[i] - o.l[i - 1]));
                }
                std::vector<float> dry; for (long n = (long) (2.5 * SR); n < (long) (5 * SR); ++n) dry.push_back (in.g (n));
                double ds = 0; for (float v : dry) ds += v * v; const double dryR = std::sqrt (ds / dry.size());
                const double outR = std::sqrt (0.5 * (std::pow (rms (o.l, 2.5, 5), 2) + std::pow (rms (o.r, 2.5, 5), 2)));
                const double gain = 20 * std::log10 (outR / dryR);
                const bool good = fin && pk < 0.97 && jump < 0.5 && gain > -20 && gain < 12;
                if (! good)
                {
                    ok = false;
                    worst += std::string (" [") + in.name + fmt (" pedal %.0f", pedal) + fmt (": peak %.2f", pk) + fmt (" jump %.2f", jump) + fmt (" level %+.1f dB]", gain);
                }
            }
        }
        check (ok, std::to_string (pi + 1) + " " + pr.name + worst);
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
