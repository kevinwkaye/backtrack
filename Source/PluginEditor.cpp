#include "PluginEditor.h"

using namespace backtrack;
using namespace backtrack::ui;

namespace
{
juce::String fmtSeconds (double s)
{
    if (s < 1.0) return juce::String (juce::roundToInt (s * 1000.0)) + " MS";
    return juce::String (s, s < 10.0 ? 2 : 1) + " S";
}

juce::String beatsName (double beats)
{
    struct N { double b; const char* n; };
    static const N names[] = { { 16, "4 BARS" }, { 8, "2 BARS" }, { 4, "1 BAR" }, { 3, "1/2 DOT" }, { 2, "1/2" },
                               { 4.0 / 3, "1/2 T" }, { 1.5, "1/4 DOT" }, { 1, "1/4" }, { 2.0 / 3, "1/4 T" }, { 0.75, "1/8 DOT" },
                               { 0.5, "1/8" }, { 1.0 / 3, "1/8 T" }, { 0.375, "1/16 DOT" }, { 0.25, "1/16" }, { 1.0 / 6, "1/16 T" },
                               { 3.0 / 16, "1/32 DOT" }, { 0.125, "1/32" }, { 1.0 / 12, "1/32 T" }, { 1.0 / 16, "1/64" },
                               { 1.0 / 32, "1/128" } };
    for (auto& n : names)
        if (std::abs (n.b - beats) < 1.0e-4) return n.n;
    return juce::String (beats, 2) + " BEATS";
}

juce::String speedText (float speed, int quant)
{
    if (speed == 0.0f) return "STOP";
    const juce::String dir = speed > 0 ? " FWD" : " REV";
    if (quant == 0) return juce::String (std::abs (speed), 2) + "X" + dir;
    const int semis = juce::roundToInt (12.0f * std::log2 (std::abs (speed)));
    if (semis == 0) return "UNISON" + dir;
    return juce::String (semis > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (semis)) + dir;
}

juce::String intervalOnly (float speed, int quant)
{
    if (speed == 0.0f) return "STOP";
    if (quant == 0) return juce::String (std::abs (speed), 2) + "X";
    const int semis = juce::roundToInt (12.0f * std::log2 (std::abs (speed)));
    if (semis == 0) return "UNISON";
    return juce::String (semis > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (semis));
}

juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
juce::String db (float v) { return v <= 0.001f ? "OFF" : juce::String (juce::Decibels::gainToDecibels (v), 1) + " DB"; }
} // namespace

//==============================================================================
class BacktrackPanel : public juce::Component, private juce::Timer
{
public:
    explicit BacktrackPanel (BacktrackProcessor& p)
        : proc (p), st (p.apvts),
          hero (st, true), k2 (st), k3 (st), kMix (st),
          modePills (st), bankPills (st), expPills (st), lpPills (st),
          intervalCard (32.0f), dirCard (32.0f), quantCard (19.0f), stateCard (22.0f), tempoCard (20.0f),
          syncChip (st, ids::sync, "SYNC", 13.0f)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &hero, &k2, &k3, &kMix, &modePills, &bankPills, &expPills, &lpPills,
                                                                  &intervalCard, &dirCard, &quantCard, &stateCard, &tempoCard, &syncChip,
                                                                  &pebble, &recButton, &tapButton, &onButton, &stepDown, &stepUp,
                                                                  &reverseButton, &qUp, &qDown })
            addAndMakeVisible (c);

        for (int i = 0; i < 6; ++i)
        {
            qKnobs.push_back (std::make_unique<Dial> (st));
            addAndMakeVisible (*qKnobs.back());
        }
        const char* chipIds[6] = { "transcend", "latch", "trails", "boost", "spread", "grit" };
        const char* chipNames[6] = { "TRANSCEND", "LATCH", "TRAILS", "BOOST", "SPREAD", "GRIT" };
        for (int i = 0; i < 6; ++i)
        {
            chips.push_back (std::make_unique<Chip> (st, chipIds[i], chipNames[i]));
            addAndMakeVisible (*chips.back());
        }

        modePills.bind (ids::mode, { "DELAY", "SLICE", "LOOP" });
        bankPills.bind (ids::bank, { "STANDARD", "NIGHTLY" });
        lpPills.bind (ids::lpLayout, { "USER 2 X-Y", "USER 1 DRUM" }, 12.0f);

        hero.extraLabels = [this] (juce::Graphics& g, juce::Point<float> c, float r) { drawHeroLabels (g, c, r); };

        recButton.onPress = [this] { if (bank() == 1) proc.toggleHold(); else proc.softDown(); };
        recButton.onRelease = [this] { if (bank() == 0) proc.softUp(); };
        tapButton.onPress = [this]
        {
            if (bank() == 1 || mode() > 0) proc.stopClear();
            else { proc.softDown(); proc.softUp(); }
        };
        onButton.onPress = [this] { toggle (ids::on); };
        stepDown.onPress = [this] { stepHero (-1); };
        stepUp.onPress = [this] { stepHero (1); };
        reverseButton.onPress = [this]
        {
            if (auto* d = st.getParameter (ids::dir1)) { d->beginChangeGesture(); d->setValueNotifyingHost (1.0f - d->getValue()); d->endChangeGesture(); }
        };
        qUp.onPress = [this] { nudgeQuant (1); };
        qDown.onPress = [this] { nudgeQuant (-1); };
        stepDown.set (juce::CharPointer_UTF8 ("\xe2\x97\x80 STEP"), {}, col::line);
        stepUp.set (juce::CharPointer_UTF8 ("STEP \xe2\x96\xb6"), {}, col::line);
        reverseButton.set ("REVERSE", {}, col::coral);
        qUp.set (juce::CharPointer_UTF8 ("\xe2\x96\xb2"), {}, col::line);
        qDown.set (juce::CharPointer_UTF8 ("\xe2\x96\xbc"), {}, col::line);
        for (auto* b : { &stepDown, &stepUp, &reverseButton, &tapButton }) b->setTextHeight (14.0f);
        qUp.setTextHeight (11.0f); qDown.setTextHeight (11.0f);

        rebind();
        startTimerHz (30);
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::face);
        for (auto r : { juce::Rectangle<float> (20, 76, 400, 440), { 440, 76, 250, 440 }, { 710, 76, 250, 440 },
                        { 980, 76, 260, 440 }, { 20, 532, 1220, 152 } })
            g.fillRoundedRectangle (r, 12.0f);

        // Header
        g.setColour (col::ink);
        g.setFont (font (26.0f, 0.04f));
        g.drawText ("BACKTRACK", 20, 20, 140, 40, juce::Justification::centredLeft);
        label (g, juce::CharPointer_UTF8 ("PITCH \xc2\xb7 TIME \xc2\xb7 REVERSE"), { 160, 20, 170, 40 });
        label (g, "MODE", { 340, 20, 40, 40 });
        label (g, "FIRMWARE", { 664, 20, 80, 40 });
        label (g, "TEMPO", { 964, 20, 54, 40 });

        // Section titles
        label (g, sec1Title, { 36, 82, 370, 20 });
        label (g, sec2Title, { 456, 82, 230, 20 });
        label (g, "Q LAYER", { 726, 82, 230, 20 });
        label (g, "SOFT SWITCH", { 996, 82, 230, 20 });
        label (g, juce::CharPointer_UTF8 ("Q \xc2\xb7 QUANTISE"), { 456, 108, 180, 18 }, 11.0f);
        label (g, expTitle, { 456, 450, 218, 18 }, 11.0f);

        if (qNote.isNotEmpty())
        {
            g.setColour (col::dim);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawFittedText (qNote, { 726, 262, 218, 120 }, juce::Justification::topLeft, 6);
        }

        // Loop bar
        auto bar = juce::Rectangle<float> (996, 176, 228, 10);
        g.setColour (col::ring);
        g.fillRoundedRectangle (bar, 5.0f);
        const int s = proc.ui.state.load();
        if (s == Recording)
        {
            g.setColour (col::red);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.0f, bar.getWidth() * proc.ui.recFraction.load())), 5.0f);
        }
        else if (s == Playing)
        {
            g.setColour (col::teal);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.0f, bar.getWidth() * proc.ui.playFraction.load())), 5.0f);
        }

        // Launchpad crib
        label (g, "LAUNCHPAD BOTTOM ROW = SOFT SWITCH", { 996, 404, 228, 20 }, 12.0f);
        label (g, "TOP 3 ROWS = DIR 1, 2, 3 INTERVALS", { 996, 426, 228, 20 }, 12.0f);
        label (g, "ROWS 4 TO 6 = LENGTH, MODE, SWITCHES", { 996, 448, 228, 20 }, 12.0f);
        label (g, "EVERY DIAL AUTOMATES IN LIVE", { 996, 470, 228, 20 }, 12.0f);

        // Bottom strip
        label (g, "PEBBLE", { 190, 556, 220, 18 });
        g.setColour (pebbleColour);
        g.setFont (font (24.0f, 0.04f));
        g.drawText (pebbleTitle, 190, 574, 230, 34, juce::Justification::centredLeft);
        label (g, pebbleSub, { 190, 608, 230, 18 }, 12.0f);

        label (g, "LEVELS", { 450, 556, 200, 18 });
        auto meter = [&] (int y, const char* name, float v, juce::Colour c)
        {
            label (g, name, { 450, y - 4, 40, 18 }, 12.0f);
            auto r = juce::Rectangle<float> (494, (float) y, 236, 10);
            g.setColour (col::ring); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (c); g.fillRoundedRectangle (r.withWidth (juce::jmax (0.0f, r.getWidth() * juce::jlimit (0.0f, 1.0f, v))), 5.0f);
        };
        meter (584, "IN", inMeter, col::teal);
        meter (606, "OUT", outMeter, outMeter > 0.95f ? col::red : col::ink);
        label (g, "LAUNCHPAD", { 770, 556, 250, 18 });
        label (g, juce::CharPointer_UTF8 ("SHIFT-DRAG FOR FINE CONTROL \xc2\xb7 DOUBLE-CLICK RESETS"), { 450, 640, 560, 18 }, 12.0f);

        // Interval bubble over Pebble
        if (bubbleTime > 0.0f && bubbleText.isNotEmpty())
        {
            const float a = juce::jlimit (0.0f, 1.0f, bubbleTime / 0.3f);
            const float rise = (1.3f - bubbleTime) * 8.0f;
            g.setFont (font (15.0f, 0.04f));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bubbleText) + 22;
            auto r = juce::Rectangle<float> (140.0f, 540.0f - rise, (float) w, 26.0f);
            g.setColour (col::ink.withAlpha (a));
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (col::bg.withAlpha (a));
            g.drawText (bubbleText, r.toNearestInt(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        modePills.setBounds (380, 24, 270, 32);
        bankPills.setBounds (744, 24, 200, 32);
        tempoCard.setBounds (1018, 20, 92, 40);
        syncChip.setBounds (1120, 24, 120, 32);

        intervalCard.setBounds (36, 112, 226, 64);
        dirCard.setBounds (270, 112, 134, 64);
        hero.setBounds (70, 184, 300, 252);
        stepDown.setBounds (36, 452, 117, 44);
        reverseButton.setBounds (161, 452, 118, 44);
        stepUp.setBounds (287, 452, 117, 44);

        quantCard.setBounds (456, 126, 172, 48);
        qUp.setBounds (636, 126, 38, 23);
        qDown.setBounds (636, 151, 38, 23);
        k2.setBounds (456, 186, 110, 124);
        k3.setBounds (570, 186, 110, 124);
        kMix.setBounds (513, 318, 110, 124);
        expPills.setBounds (456, 470, 218, 30);

        for (int i = 0; i < 6; ++i)
            qKnobs[(size_t) i]->setBounds (726 + (i % 2) * 114, 104 + (i / 2) * 124, 110, 124);

        stateCard.setBounds (996, 112, 228, 52);
        recButton.setBounds (996, 196, 228, 64);
        tapButton.setBounds (996, 268, 228, 44);
        for (int i = 0; i < 6; ++i)
            chips[(size_t) i]->setBounds (996 + (i % 3) * 78, 324 + (i / 3) * 36, 72, 30);

        pebble.setBounds (36, 542, 132, 132);
        lpPills.setBounds (770, 580, 250, 30);
        onButton.setBounds (1060, 566, 164, 72);
    }

private:
    BacktrackProcessor& proc;
    juce::AudioProcessorValueTreeState& st;

    Dial hero, k2, k3, kMix;
    std::vector<std::unique_ptr<Dial>> qKnobs;
    std::vector<std::unique_ptr<Chip>> chips;
    Pills modePills, bankPills, expPills, lpPills;
    FlipCard intervalCard, dirCard, quantCard, stateCard, tempoCard;
    Chip syncChip;
    Pebble pebble;
    BigButton recButton, tapButton, onButton, stepDown, stepUp, reverseButton, qUp, qDown;

    juce::String sec1Title, sec2Title, expTitle, qNote, pebbleTitle, pebbleSub, bubbleText;
    juce::Colour pebbleColour { col::ink };
    int lastBank = -1, lastMode = -1;
    float lastTarget = 1000.0f, bubbleTime = 0.0f, inMeter = 0.0f, outMeter = 0.0f;
    uint32_t lastTriggers = 0, lastClips = 0;

    float get (const juce::String& id) const { return st.getRawParameterValue (id)->load(); }
    int bank() const { return (int) get (ids::bank); }
    int mode() const { return (int) get (ids::mode); }
    int quant() const { return (int) get (ids::quant); }
    bool sync() const { return get (ids::sync) > 0.5f; }
    double bpm() const { return juce::jmax (1.0, proc.ui.bpm.load()); }

    void toggle (const juce::String& id)
    {
        if (auto* p = st.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
    }

    void nudgeQuant (int d)
    {
        if (auto* p = st.getParameter (ids::quant))
        {
            const int q = (quant() + d + 6) % 6;
            p->beginChangeGesture(); p->setValueNotifyingHost ((float) q / 5.0f); p->endChangeGesture();
        }
    }

    // Step the hero dial to the next distinct quantised speed (or a small nudge for non-DIR heroes)
    void stepHero (int d)
    {
        const bool isDir = bank() == 0 || (bank() == 1 && mode() == 0);
        auto* p = st.getParameter (isDir ? ids::dir1 : (mode() == 1 ? ids::n2Len1 : ids::n3Depth1));
        if (p == nullptr) return;
        float v = p->getValue();
        if (isDir)
        {
            const float now = dirToSpeed (v, quant());
            for (int i = 0; i < 400; ++i)
            {
                v = juce::jlimit (0.0f, 1.0f, v + d * 0.0025f);
                if (std::abs (dirToSpeed (v, quant()) - now) > (quant() == 0 ? 0.24f : 0.001f)) break;
            }
        }
        else v = juce::jlimit (0.0f, 1.0f, v + d * 0.05f);
        p->beginChangeGesture(); p->setValueNotifyingHost (v); p->endChangeGesture();
    }

    //==========================================================================
    Dial::TextFn dirFn() { return [this] (float v) { return speedText (dirToSpeed (v, quant()), quant()); }; }
    Dial::TextFn pctFn() { return [] (float v) { return pct (v); }; }
    Dial::TextFn lenStepFn()
    {
        return [this] (float v)
        {
            const int step = juce::roundToInt (v * 9.0f);
            return sync() ? beatsName (lenBBeats (step)) : fmtSeconds (lenBSeconds (step));
        };
    }
    Dial::TextFn sliceFn()
    {
        return [this] (float v)
        {
            if (sync())
            {
                const auto& t = sliceBeatTable();
                return beatsName (t[(size_t) juce::jlimit (0, (int) t.size() - 1, (int) std::lround (v * (t.size() - 1)))]);
            }
            return fmtSeconds (expMap (v, 0.005f, 4.0f));
        };
    }
    Dial::TextFn hzFn (float lo, float hi) { return [lo, hi] (float v) { return juce::String (expMap (v, lo, hi), 2) + " HZ"; }; }
    Dial::TextFn dbFn() { return [] (float v) { return db (v); }; }

    void rebind()
    {
        const int b = bank(), m = mode();
        lastBank = b; lastMode = m;
        qNote = {};

        for (auto& k : qKnobs) k->unbind();
        auto q = [this] (int i, const juce::String& id, const juce::String& cap, Dial::TextFn fn, bool extra = false)
        { qKnobs[(size_t) i]->bind (id, cap, std::move (fn), false, extra); };

        kMix.bind (ids::mix, "MIX", pctFn(), true);

        if (b == 0)
        {
            hero.bind (ids::dir1, "DIR 1");
            sec1Title = juce::CharPointer_UTF8 ("READ HEAD \xc2\xb7 DIR 1");
            if (m == 0)
            {
                sec2Title = juce::CharPointer_UTF8 ("BUFFER \xc2\xb7 ALWAYS LISTENING");
                k2.bind (ids::lenB, "LEN B", lenStepFn());
                k3.bind (ids::fbk, "FBK", pctFn());
                expTitle = "EXPRESSION ASSIGN";
                expPills.bind (ids::expAssign, { "DIR 1", "LEN B", "FBK" }, 12.0f);
                q (0, ids::fbkFilter, "FBK FILTER", [] (float v) { return juce::String (expMap (v, 300.0f, 18000.0f) / 1000.0f, 1) + " KHZ"; });
                q (1, ids::lfoRate, "LFO RATE", hzFn (0.05f, 4.0f));
                q (2, ids::lfoDepth, "LFO DEPTH", pctFn());
                q (3, ids::trigSens, "TRIG SENS", [] (float v) { return v >= 0.98f ? juce::String ("OFF") : juce::String (juce::roundToInt (-60.0f + 57.0f * v)) + " DB"; });
                q (4, ids::jump, "JUMP", [] (float v) { return pct (v) + " LEN"; });
                q (5, ids::gritAmt, "GRIT", pctFn(), true);
            }
            else if (m == 1)
            {
                sec2Title = juce::CharPointer_UTF8 ("SLICES \xc2\xb7 4 S SAMPLE");
                k2.bind (ids::lenS, "LEN S", sliceFn());
                k3.bind (ids::rand, "RAND", pctFn());
                expTitle = "EXPRESSION ASSIGN";
                expPills.bind (ids::expAssign, { "DIR 1", "LEN S", "RAND" }, 12.0f);
                q (0, ids::sliceFade, "SLICE FADE", [] (float v) { return juce::String (expMap (v, 0.5f, 20.0f), 1) + " MS"; }, true);
                q (1, ids::gritAmt, "GRIT", pctFn(), true);
                qNote = "The pedal hides nothing behind Q in Mode 2. These two are extras: a click-free slice fade and the converter grit.";
            }
            else
            {
                sec2Title = juce::CharPointer_UTF8 ("HEADS 2 + 3 \xc2\xb7 8 S LOOP");
                k2.bind (ids::dir2, "DIR 2", dirFn());
                k3.bind (ids::dir3, "DIR 3", dirFn());
                expTitle = "HEADS PLAYING (EXP DRIVES DIR 1)";
                expPills.bind (ids::heads, { "1 HEAD", "2 HEADS", "3 HEADS" }, 12.0f);
                q (0, ids::stepRate, "STEP RATE", [this] (float v)
                   {
                       if (sync()) { const auto& t = stepBeatTable(); return beatsName (t[(size_t) juce::jlimit (0, (int) t.size() - 1, (int) std::lround (v * (t.size() - 1)))]); }
                       return juce::String (expMap (v, 0.25f, 12.0f), 2) + " HZ";
                   });
                q (1, ids::stepVar, "STEP VAR", pctFn());
                q (2, ids::stepHeads, "STEP HEADS", [] (float v) { return v < 0.25f ? juce::String ("OFF") : v < 0.5f ? juce::String ("HEAD 3") : v < 0.75f ? juce::String ("2 + 3") : juce::String ("ALL"); });
                q (3, ids::head1, "HEAD 1", dbFn());
                q (4, ids::head2, "HEAD 2", dbFn());
                q (5, ids::head3, "HEAD 3", dbFn());
            }
        }
        else
        {
            if (m == 0)
            {
                hero.bind (ids::dir1, "DIR 1");
                sec1Title = juce::CharPointer_UTF8 ("CT5T5 \xc2\xb7 UNIT 1 DIR");
                sec2Title = juce::CharPointer_UTF8 ("CT5T5 \xc2\xb7 TWO CROSS-FED DELAYS");
                k2.bind (ids::dir2, "UNIT 2 DIR", dirFn());
                k3.bind (ids::fbk, "CROSS FBK", pctFn());
                expTitle = "EXPRESSION ASSIGN";
                expPills.bind (ids::expAssign, { "DIR 1", "DIR 2", "FBK" }, 12.0f);
                q (0, ids::n1Len1, "LENGTH 1", lenStepFn());
                q (1, ids::n1Len2, "LENGTH 2", lenStepFn());
                q (2, ids::n1Filter, "FILTER", [] (float v) { return juce::String (expMap (v, 300.0f, 18000.0f) / 1000.0f, 1) + " KHZ"; });
                q (3, ids::gritAmt, "GRIT", pctFn(), true);
            }
            else if (m == 1)
            {
                hero.bind (ids::n2Len1, "LENGTH 1");
                sec1Title = juce::CharPointer_UTF8 ("AUTO STUTTER \xc2\xb7 UNIT 1 LENGTH");
                sec2Title = "UNITS 2 + 3 LENGTH";
                auto lenFn = [this] (float v)
                {
                    if (sync()) { const auto& t = sliceBeatTable(); return beatsName (t[(size_t) juce::jlimit (0, (int) t.size() - 1, (int) std::lround (v * (t.size() - 1)))]); }
                    return fmtSeconds (expMap (v, 0.02f, 2.5f));
                };
                k2.bind (ids::n2Len2, "LENGTH 2", lenFn);
                k3.bind (ids::n2Len3, "LENGTH 3", lenFn);
                expTitle = "UNITS PLAYING";
                expPills.bind (ids::heads, { "1 UNIT", "2 UNITS", "3 UNITS" }, 12.0f);
                auto repFn = [] (float v) { return juce::String (2 + juce::roundToInt (v * 14.0f)) + " REPEATS"; };
                q (0, ids::n2Rep1, "REPEATS 1", repFn);
                q (1, ids::n2Rep2, "REPEATS 2", repFn);
                q (2, ids::n2Rep3, "REPEATS 3", repFn);
                q (3, ids::n2Vol1, "LEVEL 1", dbFn());
                q (4, ids::n2Vol2, "LEVEL 2", dbFn());
                q (5, ids::n2Vol3, "LEVEL 3", dbFn());
            }
            else
            {
                hero.bind (ids::n3Depth1, "DEPTH 1");
                sec1Title = juce::CharPointer_UTF8 ("VIBRATO DELAY \xc2\xb7 HEAD 1 DEPTH");
                sec2Title = "HEAD 1 RATE + TIME";
                k2.bind (ids::n3Rate1, "RATE 1", hzFn (0.1f, 12.0f));
                k3.bind (ids::n3Time1, "TIME 1", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
                expTitle = "HEADS PLAYING";
                expPills.bind (ids::heads, { "1 HEAD", "2 HEADS", "3 HEADS" }, 12.0f);
                q (0, ids::n3Depth2, "DEPTH 2", [this] (float v) { return juce::String ((int) quantiseSemis (v * 12.0f, quant())) + " SEMIS"; });
                q (1, ids::n3Rate2, "RATE 2", hzFn (0.1f, 12.0f));
                q (2, ids::n3Time2, "TIME 2", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
                q (3, ids::n3Fbk, "FBK HEAD", pctFn());
                q (4, ids::n3FbkTime, "FBK TIME", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
                q (5, ids::gritAmt, "GRIT", pctFn(), true);
            }
        }
        repaint();
    }

    bool heroIsDir() const { return bank() == 0 || mode() == 0; }

    void drawHeroLabels (juce::Graphics& g, juce::Point<float> c, float r)
    {
        auto at = [&] (float deg, float rad, const juce::String& t, juce::Colour colour)
        {
            const float a = juce::degreesToRadians (deg);
            auto p = juce::Point<float> (c.x + std::sin (a) * rad, c.y - std::cos (a) * rad);
            label (g, t, juce::Rectangle<int> (70, 18).withCentre (p.toInt()), 12.0f, colour, juce::Justification::centred);
        };
        if (heroIsDir())
        {
            at (-135.0f, r * 1.13f, "2X REV", col::dim);
            at (-67.5f, r * 1.2f, "1X", col::teal);
            at (0.0f, r * 1.13f, "STOP", col::dim);
            at (67.5f, r * 1.2f, "1X", col::teal);
            at (135.0f, r * 1.13f, "2X FWD", col::dim);
            g.setColour (col::teal);
            for (float deg : { -67.5f, 67.5f })
            {
                const float a = juce::degreesToRadians (deg);
                g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre ({ c.x + std::sin (a) * r * 0.73f, c.y - std::cos (a) * r * 0.73f }));
            }
        }
        else
        {
            at (-135.0f, r * 1.13f, "MIN", col::dim);
            at (135.0f, r * 1.13f, "MAX", col::dim);
        }
    }

    //==========================================================================
    void timerCallback() override
    {
        if (bank() != lastBank || mode() != lastMode)
            rebind();

        const int b = bank(), m = mode(), qn = quant();
        static const char* qNames[6] = { "NONE", "CHROMATIC", "WHOLE TONE", "DIM TRIAD", "AUG TRIAD", "5THS + 8VA" };
        quantCard.setText (qNames[juce::jlimit (0, 5, qn)]);
        tempoCard.setText (juce::String (juce::roundToInt (bpm())));

        // Hero cards
        float target = 0.0f;
        if (heroIsDir())
        {
            target = dirToSpeed (hero.getValue(), qn);
            intervalCard.setText (intervalOnly (target, qn));
            dirCard.setText (target == 0.0f ? "STOP" : target > 0 ? "FWD" : "REV", target < 0 ? col::coral : col::ink);
        }
        else if (m == 1)
        {
            const float v = get (ids::n2Len1);
            intervalCard.setText (sync() ? beatsName (sliceBeatTable()[(size_t) juce::jlimit (0, 16, (int) std::lround (v * 16))])
                                         : fmtSeconds (expMap (v, 0.02f, 2.5f)));
            dirCard.setText ("UNIT 1");
        }
        else
        {
            const float semis = quantiseSemis (get (ids::n3Depth1) * 12.0f, qn);
            intervalCard.setText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String ((int) semis));
            dirCard.setText ("HEAD 1");
        }

        reverseButton.setVisible (heroIsDir());

        // Soft switch section
        const int s = proc.ui.state.load();
        const bool latch = get (ids::latch) > 0.5f;
        if (b == 1)
        {
            stateCard.setText (s == Frozen ? "FROZEN" : "RUNNING", s == Frozen ? col::coral : col::ink);
            recButton.set (s == Frozen ? "FROZEN" : "FREEZE", "PRESS TO HOLD, AGAIN TO RELEASE", s == Frozen ? col::coral : col::line);
            tapButton.set (juce::CharPointer_UTF8 ("STOP \xc2\xb7 CLEAR"), {}, col::line);
        }
        else if (m == 0)
        {
            stateCard.setText (s == Held ? "FROZEN" : "LISTENING", s == Held ? col::coral : col::ink);
            recButton.set (s == Held ? "HELD" : "HOLD", "FREEZE THE BUFFER", s == Held ? col::red : col::line);
            tapButton.set (juce::CharPointer_UTF8 ("TAP \xc2\xb7 JUMP THE HEAD"), {}, col::line);
        }
        else
        {
            const char* names[] = { "LISTENING", "HELD", "RECORDING", "PLAYING", "EMPTY", "FROZEN", "WAITING FOR BEAT" };
            stateCard.setText (names[juce::jlimit (0, 6, s)], s == Recording || s == Armed ? col::red : col::ink);
            const bool rec = s == Recording || s == Armed;
            recButton.set (rec ? "RECORDING" : (s == Playing && m == 2 ? juce::String (juce::CharPointer_UTF8 ("REC \xc2\xb7 DUB")) : juce::String ("REC")),
                           latch ? "TAP TO START, TAP TO STOP" : (m == 1 ? "HOLD TO SAMPLE, UP TO 4 S" : "HOLD TO RECORD, HOLD AGAIN TO DUB"),
                           rec ? col::red : col::line);
            tapButton.set (juce::CharPointer_UTF8 ("TAP \xc2\xb7 STOP AND CLEAR"), {}, col::line);
        }
        const bool on = get (ids::on) > 0.5f;
        onButton.set (on ? "ON" : "BYPASSED", get (ids::trails) > 0.5f ? "TRAILS RING ON" : "LOOPS PLAY ON UNDERNEATH", on ? col::ink : col::line);
        onButton.setTextHeight (18.0f);
        recButton.setTextHeight (18.0f);

        // Pebble
        const bool transcend = get (ids::transcend) > 0.5f;
        pebble.setTranscend (transcend);
        pebble.setLevel (proc.ui.inLevel.load());
        Pebble::Mood base = Pebble::Idle;
        if (! on) base = Pebble::Asleep;
        else if (s == Recording || s == Armed) base = Pebble::Recording;
        else if (s == Held || s == Frozen) base = Pebble::Holding;
        pebble.setBase (base);

        if (heroIsDir() && lastTarget < 999.0f && std::abs (target - lastTarget) > 0.001f)
        {
            if (target != 0.0f && lastTarget != 0.0f && (target > 0) != (lastTarget > 0)) pebble.react (Pebble::Reversing);
            else if (std::abs (target) > std::abs (lastTarget)) pebble.react (Pebble::PitchUp);
            else pebble.react (Pebble::PitchDown);
            bubbleText = speedText (target, qn);
            bubbleTime = 1.3f;
        }
        lastTarget = heroIsDir() ? target : 1000.0f;

        const auto trig = proc.ui.triggers.load();
        if (trig != lastTriggers) { lastTriggers = trig; pebble.react (Pebble::PitchUp); bubbleText = "JUMP"; bubbleTime = 1.3f; }
        const auto clips = proc.ui.clips.load();
        if (clips != lastClips) { if (clips > lastClips + 2) pebble.react (Pebble::Clipping); lastClips = clips; }

        const auto mood = pebble.current();
        switch (mood)
        {
            case Pebble::Recording: pebbleTitle = "RECORDING"; pebbleSub = "MOUTH OPEN, RED RING PULSES"; pebbleColour = col::red; break;
            case Pebble::Holding:   pebbleTitle = "HOLDING ITS BREATH"; pebbleSub = "BUFFER FROZEN"; pebbleColour = col::coral; break;
            case Pebble::Reversing: pebbleTitle = "SPINNING BACK"; pebbleSub = "THE HEAD CROSSED NOON"; pebbleColour = col::coral; break;
            case Pebble::PitchUp:   pebbleTitle = "PITCH UP"; pebbleSub = "HOPS AND STRETCHES TALL"; pebbleColour = col::teal; break;
            case Pebble::PitchDown: pebbleTitle = "PITCH DOWN"; pebbleSub = "SQUASHES LOW AND DROWSY"; pebbleColour = col::blue; break;
            case Pebble::Clipping:  pebbleTitle = "TOO HOT"; pebbleSub = "FEEDBACK IS RUNNING AWAY"; pebbleColour = col::red; break;
            case Pebble::Asleep:    pebbleTitle = "ASLEEP"; pebbleSub = "SLEEPS WHILE BYPASSED"; pebbleColour = col::dim; break;
            default:
                pebbleTitle = s == Playing ? "PLAYING" : "LISTENING";
                pebbleSub = transcend ? "TRANSCEND ON, GREEN EYES" : "BOBS AND BLINKS TO THE INPUT";
                pebbleColour = col::ink;
                break;
        }

        if (bubbleTime > 0.0f) bubbleTime -= 1.0f / 30.0f;
        inMeter = juce::jmax (proc.ui.inLevel.load(), inMeter * 0.85f);
        outMeter = juce::jmax (proc.ui.outLevel.load(), outMeter * 0.85f);
        repaint (0, 76, 1260, 624);
    }
};

//==============================================================================
BacktrackEditor::BacktrackEditor (BacktrackProcessor& p) : AudioProcessorEditor (&p)
{
    panel = std::make_unique<BacktrackPanel> (p);
    addAndMakeVisible (*panel);
    panel->setBounds (0, 0, kWidth, kHeight);
    setResizable (true, true);
    setResizeLimits (kWidth / 2, kHeight / 2, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
}

BacktrackEditor::~BacktrackEditor() = default;

void BacktrackEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void BacktrackEditor::resized()
{
    const float s = (float) getWidth() / (float) kWidth;
    panel->setTransform (juce::AffineTransform::scale (s));
}
