#include "PluginEditor.h"

using namespace backtrack;
using namespace backtrack::ui;
using juce::String;

namespace
{
const String dot (juce::CharPointer_UTF8 ("\xc2\xb7"));
const String minus (juce::CharPointer_UTF8 ("\xe2\x88\x92"));

String fmtSeconds (double s)
{
    if (s < 1.0) return String (juce::roundToInt (s * 1000.0)) + " MS";
    return String (s, s < 10.0 ? 2 : 1) + " S";
}

String beatsName (double beats)
{
    struct N { double b; const char* n; };
    static const N names[] = { { 16, "4 BARS" }, { 8, "2 BARS" }, { 4, "1 BAR" }, { 3, "1/2 DOT" }, { 2, "1/2" },
                               { 4.0 / 3, "1/2 T" }, { 1.5, "1/4 DOT" }, { 1, "1/4" }, { 2.0 / 3, "1/4 T" }, { 0.75, "1/8 DOT" },
                               { 0.5, "1/8" }, { 1.0 / 3, "1/8 T" }, { 0.375, "1/16 DOT" }, { 0.25, "1/16" }, { 1.0 / 6, "1/16 T" },
                               { 3.0 / 16, "1/32 DOT" }, { 0.125, "1/32" }, { 1.0 / 12, "1/32 T" }, { 1.0 / 16, "1/64" },
                               { 1.0 / 32, "1/128" } };
    for (auto& n : names)
        if (std::abs (n.b - beats) < 1.0e-4) return n.n;
    return String (beats, 2) + " BEATS";
}

String speedText (float speed, int quant)
{
    if (speed == 0.0f) return "STOP";
    const String dir = speed > 0 ? " FWD" : " REV";
    if (quant == 0) return String (std::abs (speed), 2) + "X" + dir;
    const int semis = juce::roundToInt (12.0f * std::log2 (std::abs (speed)));
    if (semis == 0) return "UNISON" + dir;
    return (semis > 0 ? String ("+") : minus) + String (std::abs (semis)) + dir;
}

String intervalOnly (float speed, int quant)
{
    if (speed == 0.0f) return "STOP";
    if (quant == 0) return String (std::abs (speed), 2) + "X";
    const int semis = juce::roundToInt (12.0f * std::log2 (std::abs (speed)));
    if (semis == 0) return "UNISON";
    return (semis > 0 ? String ("+") : minus) + String (std::abs (semis));
}

String pct (float v) { return String (juce::roundToInt (v * 100.0f)) + "%"; }
String dbText (float v) { return v <= 0.001f ? String ("OFF") : (v >= 0.999f ? String ("0 DB") : minus + String (std::abs (juce::Decibels::gainToDecibels (v)), 1) + " DB"); }

void paintPanel (juce::Graphics& g, juce::Rectangle<float> r) { g.setColour (col::face); g.fillRoundedRectangle (r, 12.0f); }

void wrapText (juce::Graphics& g, const String& t, juce::Rectangle<int> r, float h, juce::Colour c, int lines = 4)
{
    g.setColour (c);
    g.setFont (juce::Font (juce::FontOptions (h)));
    g.drawFittedText (t, r, juce::Justification::topLeft, lines, 1.0f);
}
} // namespace

//==============================================================================
class MainPage : public juce::Component, private juce::Timer
{
public:
    MainPage (BacktrackProcessor& p, BacktrackEditor& ed)
        : proc (p), editor (ed), st (p.apvts),
          hero (st, true), k2 (st), k3 (st), kMix (st),
          modePills (st), bankPills (st), headPills (st),
          presetCard (18.0f), tempoCard (18.0f), intervalCard (32.0f), dirCard (32.0f), quantCard (19.0f), stateCard (22.0f), expCard (16.0f),
          gritChip (st, "grit", { "GRIT", "24-BIT", "12-BIT" })
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &hero, &k2, &k3, &kMix, &modePills, &bankPills, &headPills, &presetCard, &tempoCard, &intervalCard, &dirCard,
                 &quantCard, &stateCard, &expCard, &gritChip, &pebble, &prevButton, &nextButton, &saveButton, &syncButton,
                 &stepDown, &stepUp, &reverseButton, &qUp, &qDown, &recButton, &tapButton, &keeperButton, &expSlider, &pedalLink })
            addAndMakeVisible (c);

        for (int i = 0; i < 6; ++i) { qKnobs.push_back (std::make_unique<Dial> (st)); addAndMakeVisible (*qKnobs.back()); }
        for (int i = 0; i < 4; ++i) { expPills.push_back (std::make_unique<PillButton>()); addChildComponent (*expPills.back()); }
        const char* chipIds[7] = { "transcend", "latch", "trails", "boost", "spread", "lowKeep", "lock" };
        const char* chipNames[7] = { "TRANSCEND", "LATCH", "TRAILS", "BOOST", "SPREAD", "LOW KEEP", "LOCK" };
        for (int i = 0; i < 7; ++i) { chips.push_back (std::make_unique<Chip> (st, chipIds[i], chipNames[i])); addAndMakeVisible (*chips.back()); }
        for (int i = 0; i < 4; ++i)
        {
            feet.push_back (std::make_unique<FootButton>());
            addAndMakeVisible (*feet.back());
            feet.back()->onPress = [this, i] { proc.footDown (i); };
            feet.back()->onRelease = [this, i] { proc.footUp (i); };
        }

        modePills.bind ("mode", { "DELAY", "SLICE", "LOOP" }, 12.0f);
        bankPills.bind ("bank", { "STANDARD", "NIGHTLY" }, 12.0f);
        headPills.bind ("heads", { "1 HEAD", "2 HEADS", "3 HEADS" }, 11.0f);
        hero.extraLabels = [this] (juce::Graphics& g, juce::Point<float> c, float r) { drawHeroLabels (g, c, r); };

        prevButton.set (juce::CharPointer_UTF8 ("\xe2\x97\x80"), {}, col::line);
        nextButton.set (juce::CharPointer_UTF8 ("\xe2\x96\xb6"), {}, col::line);
        for (auto* b : { &prevButton, &nextButton }) b->setTextHeight (12.0f);
        prevButton.onPress = [this] { proc.touchPanel(); proc.stepPreset (-1); };
        nextButton.onPress = [this] { proc.touchPanel(); proc.stepPreset (1); };
        saveButton.onClick = [this] { proc.saveUserPreset(); savedFlash = 1.5f; };
        syncButton.onClick = [this] { proc.togglePlain ("sync"); };

        stepDown.set (juce::CharPointer_UTF8 ("\xe2\x97\x80 STEP"), {}, col::line);
        stepUp.set (juce::CharPointer_UTF8 ("STEP \xe2\x96\xb6"), {}, col::line);
        reverseButton.set ("REVERSE", {}, col::coral);
        for (auto* b : { &stepDown, &stepUp, &reverseButton, &tapButton }) b->setTextHeight (14.0f);
        stepDown.onPress = [this] { stepHero (-1); };
        stepUp.onPress = [this] { stepHero (1); };
        reverseButton.onPress = [this] { proc.togglePlain ("reverse"); };
        qUp.set (juce::CharPointer_UTF8 ("\xe2\x96\xb2"), {}, col::line);
        qDown.set (juce::CharPointer_UTF8 ("\xe2\x96\xbc"), {}, col::line);
        qUp.setTextHeight (11.0f); qDown.setTextHeight (11.0f);
        qUp.onPress = [this] { nudgeQuant (1); };
        qDown.onPress = [this] { nudgeQuant (-1); };

        // the soft switch on screen is footswitch B, so it holds and taps exactly like it
        recButton.onPress = [this] { proc.footDown (1); };
        recButton.onRelease = [this] { proc.footUp (1); };
        tapButton.onPress = [this]
        {
            proc.touchPanel();
            if (bank() == 0 && mode() > 0) proc.stopAndClear(); else proc.softTap();
        };
        keeperButton.set ("KEEPER", "HOLD C+D, 1 S", col::green);
        keeperButton.setTextHeight (17.0f);
        keeperButton.onPress = [this] { proc.touchPanel(); proc.keepNow(); };

        expSlider.onChange = [this] (float v)
        {
            proc.touchPanel();
            if (get ("expOn") < 0.5f) proc.setPlain ("expOn", 1.0f);
            proc.setPlain ("exp", v);
        };
        pedalLink.text = String (juce::CharPointer_UTF8 ("PEDAL PAGE \xe2\x80\xba"));
        pedalLink.onClick = [this] { editor.showPedalPage (true); };

        rebind();
        startTimerHz (30);
    }

    void mouseDown (const juce::MouseEvent&) override { proc.touchPanel(); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        for (auto r : { juce::Rectangle<float> (20, 76, 400, 440), { 440, 76, 250, 440 }, { 710, 76, 250, 440 },
                        { 980, 76, 260, 440 }, { 20, 532, 1220, 152 } })
            paintPanel (g, r);

        g.setColour (col::ink);
        g.setFont (font (26.0f, 0.04f));
        g.drawText ("BACKTRACK", 20, 20, 150, 40, juce::Justification::centredLeft);

        label (g, sec1Title, { 36, 82, 370, 20 });
        label (g, sec2Title, { 456, 82, 230, 20 });
        label (g, "Q LAYER " + dot + " HIDDEN ON THE PEDAL", { 726, 82, 230, 20 });
        label (g, "SOFT SWITCH", { 996, 82, 230, 20 });
        label (g, "Q " + dot + " QUANTISE", { 456, 108, 180, 18 }, 11.0f);
        label (g, expTitle, { 456, 450, 230, 18 }, 11.0f);

        if (qNote.isNotEmpty()) wrapText (g, qNote, { 726, 262, 218, 120 }, 14.0f, col::dim, 6);

        // loop bar
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

        label (g, "TRY THIS", { 996, 430, 228, 18 }, 11.0f, col::teal);
        wrapText (g, proc.presetHint (proc.currentPreset()), { 996, 450, 228, 60 }, 14.0f, col::ink, 3);

        // bottom strip
        label (g, "PEBBLE", { 180, 548, 150, 18 });
        g.setColour (pebbleColour);
        g.setFont (font (22.0f, 0.04f));
        g.drawFittedText (pebbleTitle, { 180, 566, 156, 30 }, juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (col::dim);
        g.setFont (font (11.0f, 0.05f));
        g.drawFittedText (pebbleSub, { 180, 598, 156, 48 }, juce::Justification::topLeft, 3, 1.0f);

        label (g, "FOOTSWITCHES", { 340, 540, 200, 18 });
        label (g, "EXPRESSION", { 942, 540, 160, 18 });
        label (g, "HEEL " + proc.expressionRangeText (false) + " " + dot + " TOE " + proc.expressionRangeText (true), { 942, 610, 170, 16 }, 10.0f);
        if (get ("expOn") < 0.5f) label (g, "NOT MOVED YET", { 942, 652, 170, 16 }, 10.0f, col::faint);

        // interval bubble over Pebble
        if (bubbleTime > 0.0f && bubbleText.isNotEmpty())
        {
            const float a = juce::jlimit (0.0f, 1.0f, bubbleTime / 0.3f);
            const float rise = (1.3f - bubbleTime) * 8.0f;
            g.setFont (font (15.0f, 0.04f));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), bubbleText) + 22;
            auto r = juce::Rectangle<float> (140.0f, 536.0f - rise, (float) w, 26.0f);
            g.setColour (col::ink.withAlpha (a));
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (col::bg.withAlpha (a));
            g.drawText (bubbleText, r.toNearestInt(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        prevButton.setBounds (168, 24, 36, 32);
        presetCard.setBounds (210, 20, 250, 40);
        nextButton.setBounds (466, 24, 36, 32);
        saveButton.setBounds (510, 24, 62, 32);
        modePills.setBounds (584, 24, 220, 32);
        bankPills.setBounds (818, 24, 176, 32);
        tempoCard.setBounds (1008, 20, 96, 40);
        syncButton.setBounds (1114, 24, 126, 32);

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
        headPills.setBounds (456, 470, 218, 30);
        layoutExpPills();

        for (int i = 0; i < 6; ++i)
            qKnobs[(size_t) i]->setBounds (726 + (i % 2) * 114, 104 + (i / 2) * 120, 110, 124);

        stateCard.setBounds (996, 112, 228, 52);
        recButton.setBounds (996, 196, 228, 64);
        tapButton.setBounds (996, 268, 228, 44);
        std::vector<juce::Component*> chipOrder { chips[0].get(), chips[1].get(), chips[2].get(), chips[3].get(), chips[4].get(), &gritChip, chips[5].get(), chips[6].get() };
        for (int i = 0; i < 8; ++i)
            chipOrder[(size_t) i]->setBounds (996 + (i % 3) * 78, 324 + (i / 3) * 36, 72, 30);

        pebble.setBounds (36, 542, 132, 132);
        pedalLink.setBounds (790, 540, 134, 18);
        for (int i = 0; i < 4; ++i) feet[(size_t) i]->setBounds (340 + i * 148, 566, 140, 104);
        expCard.setBounds (942, 566, 160, 40);
        expSlider.setBounds (938, 628, 168, 22);
        keeperButton.setBounds (1114, 566, 126, 104);
    }

private:
    BacktrackProcessor& proc;
    BacktrackEditor& editor;
    juce::AudioProcessorValueTreeState& st;

    Dial hero, k2, k3, kMix;
    std::vector<std::unique_ptr<Dial>> qKnobs;
    std::vector<std::unique_ptr<Chip>> chips;
    std::vector<std::unique_ptr<PillButton>> expPills;
    std::vector<std::unique_ptr<FootButton>> feet;
    Pills modePills, bankPills, headPills;
    FlipCard presetCard, tempoCard, intervalCard, dirCard, quantCard, stateCard, expCard;
    CycleChip gritChip;
    Pebble pebble;
    BigButton prevButton, nextButton, stepDown, stepUp, reverseButton, qUp, qDown, recButton, tapButton, keeperButton;
    PillButton saveButton, syncButton;
    ThinSlider expSlider;
    LinkButton pedalLink;

    String sec1Title, sec2Title, expTitle, qNote, pebbleTitle, pebbleSub, bubbleText;
    juce::Colour pebbleColour { col::ink };
    int lastBank = -1, lastMode = -1, lastPresetPulse = -1, lastFootPulse = 0, lastKeepPulse = 0;
    float lastTarget = 1000.0f, bubbleTime = 0.0f, savedFlash = 0.0f;
    bool lastReverse = false;
    uint32_t lastTriggers = 0, lastClips = 0;
    struct ExpChoice { String name; int target; };
    std::vector<ExpChoice> expChoices;

    float get (const char* id) const { return st.getRawParameterValue (id)->load(); }
    int bank() const { return (int) std::lround (get ("bank")); }
    int mode() const { return (int) std::lround (get ("mode")); }
    int quant() const { return (int) std::lround (get ("quant")); }
    bool sync() const { return get ("sync") > 0.5f; }
    double bpm() const { return juce::jmax (1.0, proc.ui.bpm.load()); }

    void nudgeQuant (int d) { proc.touchPanel(); proc.setPlain ("quant", (float) ((quant() + d + 6) % 6)); }

    // Step the hero dial to the next distinct quantised speed (or a small nudge for the Nightly heroes)
    void stepHero (int d)
    {
        proc.touchPanel();
        const bool isDir = heroIsDir();
        auto* p = st.getParameter (isDir ? "dir1" : (mode() == 1 ? "n2Len1" : "n3Depth1"));
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
    Dial::TextFn hzFn (float lo, float hi) { return [lo, hi] (float v) { return String (expMap (v, lo, hi), 2) + " HZ"; }; }
    Dial::TextFn dbFn() { return [] (float v) { return dbText (v); }; }
    Dial::TextFn lowKeepFn() { return [this] (float v) { return get ("lowKeep") > 0.5f ? String (juce::roundToInt (40.0f + 210.0f * v)) + " HZ" : String ("OFF"); }; }
    Dial::TextFn dubFn()
    {
        return [] (float v)
        {
            if (v >= 0.999f) return String ("KEEP ALL");
            if (v <= 0.001f) return String ("REPLACE");
            return minus + String (10.0f * (1.0f - v), 1) + " DB";
        };
    }

    void rebind()
    {
        const int b = bank(), m = mode();
        lastBank = b; lastMode = m;
        qNote = {};
        for (auto& k : qKnobs) k->unbind();
        auto q = [this] (int i, const char* id, const String& cap, Dial::TextFn fn, bool extra = false)
        { qKnobs[(size_t) i]->bind (id, cap, std::move (fn), false, extra); };

        kMix.bind ("mix", "MIX", pctFn(), true);
        expChoices.clear();
        headPills.setVisible (false);

        if (b == 0)
        {
            hero.bind ("dir1", "DIR 1");
            sec1Title = "READ HEAD " + dot + " DIR 1";
            if (m == 0)
            {
                sec2Title = "BUFFER " + dot + " ALWAYS LISTENING";
                k2.bind ("lenB", "LEN B", lenStepFn());
                k3.bind ("fbk", "FBK", pctFn());
                expChoices = { { "DIR 1", ExpDir1 }, { "FBK", ExpFbk }, { "MIX", ExpMix } };
                q (0, "fbkFilter", "FBK FILTER", [] (float v) { return String (expMap (v, 300.0f, 18000.0f) / 1000.0f, 1) + " KHZ"; });
                q (1, "lfoRate", "LFO RATE", hzFn (0.05f, 4.0f));
                q (2, "lfoDepth", "LFO DEPTH", pctFn());
                q (3, "trigSens", "TRIG SENS", [] (float v) { return v >= 0.98f ? String ("OFF") : minus + String (juce::roundToInt (2.0f + 14.0f * v)) + " DB"; });
                q (4, "jump", "JUMP", [] (float v) { return pct (v) + " LEN"; });
                q (5, "lowKeepHz", "LOW KEEP", lowKeepFn(), true);
            }
            else if (m == 1)
            {
                sec2Title = "SLICES " + dot + " 4 S SAMPLE";
                k2.bind ("lenS", "LEN S", sliceFn());
                k3.bind ("rand", "RAND", pctFn());
                expChoices = { { "DIR 1", ExpDir1 }, { "LEN S", ExpLenS }, { "RAND", ExpRand }, { "MIX", ExpMix } };
                q (0, "sliceFade", "SLICE FADE", [] (float v) { return String (expMap (v, 0.5f, 20.0f), 1) + " MS"; }, true);
                q (1, "lowKeepHz", "LOW KEEP", lowKeepFn(), true);
                qNote = "The pedal hides nothing behind Q in Mode 2. These two are plugin extras: a click-free slice fade and LOW KEEP for bass.";
            }
            else
            {
                sec2Title = "HEADS 2 + 3 " + dot + " 8 S LOOP";
                k2.bind ("dir2", "DIR 2", dirFn());
                k3.bind ("dir3", "DIR 3", dirFn());
                headPills.setVisible (true);
                q (0, "stepRate", "STEP RATE", [this] (float v)
                   {
                       if (sync()) { const auto& t = stepBeatTable(); return beatsName (t[(size_t) juce::jlimit (0, (int) t.size() - 1, (int) std::lround (v * (t.size() - 1)))]); }
                       return String (expMap (v, 0.25f, 12.0f), 2) + " HZ";
                   });
                q (1, "stepVar", "STEP VAR", pctFn());
                q (2, "dubFade", "DUB FADE", dubFn(), true);
                q (3, "head1", "HEAD 1", dbFn());
                q (4, "head2", "HEAD 2", dbFn());
                q (5, "head3", "HEAD 3", dbFn());
            }
        }
        else
        {
            if (m == 0)
            {
                hero.bind ("dir1", "DIR 1");
                sec1Title = "CT5T5 " + dot + " UNIT 1 DIR";
                sec2Title = "CT5T5 " + dot + " TWO CROSS-FED DELAYS";
                k2.bind ("n1Len1", "LENGTH 1", lenStepFn());
                k3.bind ("fbk", "CROSS FBK", pctFn());
                expChoices = { { "DIR 1", ExpDir1 }, { "DIR 2", ExpDir2 }, { "FBK", ExpFbk }, { "MIX", ExpMix } };
                q (0, "n1Len2", "LENGTH 2", lenStepFn());
                q (1, "dir2", "UNIT 2 DIR", dirFn());
                q (2, "n1Filter", "FILTER", [] (float v) { return String (expMap (v, 300.0f, 18000.0f) / 1000.0f, 1) + " KHZ"; });
                q (3, "lowKeepHz", "LOW KEEP", lowKeepFn(), true);
            }
            else if (m == 1)
            {
                hero.bind ("n2Len1", "LENGTH 1");
                sec1Title = "AUTO STUTTER " + dot + " UNIT 1 LENGTH";
                sec2Title = "UNITS 2 + 3 LENGTH";
                auto lenFn = [this] (float v)
                {
                    if (sync()) { const auto& t = sliceBeatTable(); return beatsName (t[(size_t) juce::jlimit (0, (int) t.size() - 1, (int) std::lround (v * (t.size() - 1)))]); }
                    return fmtSeconds (expMap (v, 0.02f, 2.5f));
                };
                k2.bind ("n2Len2", "LENGTH 2", lenFn);
                k3.bind ("n2Len3", "LENGTH 3", lenFn);
                expChoices = { { "LEN S", ExpStutterLen }, { "MIX", ExpMix } };
                auto repFn = [] (float v) { return String (2 + juce::roundToInt (v * 14.0f)) + " REPEATS"; };
                q (0, "n2Rep1", "REPEATS 1", repFn);
                q (1, "n2Rep2", "REPEATS 2", repFn);
                q (2, "n2Rep3", "REPEATS 3", repFn);
                q (3, "n2Vol1", "LEVEL 1", dbFn());
                q (4, "n2Vol2", "LEVEL 2", dbFn());
                q (5, "n2Vol3", "LEVEL 3", dbFn());
            }
            else
            {
                hero.bind ("n3Depth1", "DEPTH 1");
                sec1Title = "VIBRATO DELAY " + dot + " HEAD 1 DEPTH";
                sec2Title = "HEAD 1 RATE + TIME";
                k2.bind ("n3Rate1", "RATE 1", hzFn (0.1f, 12.0f));
                k3.bind ("n3Time1", "TIME 1", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
                expChoices = { { "DEPTH", ExpVibDepth }, { "MIX", ExpMix } };
                q (0, "n3Depth2", "DEPTH 2", [this] (float v) { return String ((int) quantiseSemis (v * 12.0f, quant())) + " SEMIS"; });
                q (1, "n3Rate2", "RATE 2", hzFn (0.1f, 12.0f));
                q (2, "n3Time2", "TIME 2", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
                q (3, "n3Fbk", "FBK HEAD", pctFn());
                q (4, "n3FbkTime", "FBK TIME", [] (float v) { return fmtSeconds (expMap (v, 0.5f, 7.0f)); });
            }
        }

        layoutExpPills();
        repaint();
    }

    // expression target pills, as many as this mode offers
    void layoutExpPills()
    {
        const int n = (int) expChoices.size();
        for (int i = 0; i < 4; ++i)
        {
            auto& pill = *expPills[(size_t) i];
            pill.setVisible (i < n && ! headPills.isVisible());
            if (i >= n) continue;
            const float w = (218.0f - 6.0f * (float) (n - 1)) / (float) n;
            pill.setBounds ((int) (456 + i * (w + 6)), 470, (int) w, 30);
            const int target = expChoices[(size_t) i].target;
            pill.onClick = [this, target] { pickExpTarget (target); };
        }
    }

    // Picking a new target gives it a sensible heel and toe; the preset's own range stays until you do
    void pickExpTarget (int target)
    {
        proc.touchPanel();
        if ((int) std::lround (get ("expTarget")) == target) return;
        float lo = 0.0f, hi = 1.0f;
        if (target == ExpDir1 || target == ExpDir2) { lo = 0.75f; hi = 1.0f; }   // unity up to an octave
        proc.setPlain ("expTarget", (float) target);
        proc.setPlain ("expLo", lo);
        proc.setPlain ("expHi", hi);
    }

    bool heroIsDir() const { return bank() == 0 || mode() == 0; }

    void drawHeroLabels (juce::Graphics& g, juce::Point<float> c, float r)
    {
        auto at = [&] (float deg, float rad, const String& t, juce::Colour colour)
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

    void bubble (const String& t) { bubbleText = t; bubbleTime = 1.3f; }

    //==========================================================================
    void timerCallback() override
    {
        if (bank() != lastBank || mode() != lastMode) rebind();
        const int b = bank(), m = mode(), qn = quant();
        const int s = proc.ui.state.load();

        // header
        const int pi = proc.currentPreset();
        presetCard.setText (String (pi + 1).paddedLeft ('0', 2) + " " + proc.presetName (pi) + (proc.presetEdited() ? " *" : ""));
        saveButton.set (savedFlash > 0.0f ? "SAVED" : "SAVE", savedFlash > 0.0f, 12.0f);
        if (savedFlash > 0.0f) savedFlash -= 1.0f / 30.0f;
        syncButton.set (sync() ? "SYNC ON" : "FREE", sync(), 13.0f);
        tempoCard.setText (String (juce::roundToInt (bpm())) + " BPM");

        static const char* qNames[6] = { "NONE", "CHROMATIC", "WHOLE TONE", "DIM TRIAD", "AUG TRIAD", "5THS + 8VA" };
        quantCard.setText (qNames[juce::jlimit (0, 5, qn)]);

        // section 1: the hero read head, with reverse applied as you hear it
        const bool rev = get ("reverse") > 0.5f;
        float target = 0.0f;
        if (heroIsDir())
        {
            target = dirToSpeed (hero.getValue(), qn) * (rev ? -1.0f : 1.0f);
            intervalCard.setText (intervalOnly (target, qn));
            dirCard.setText (target == 0.0f ? "STOP" : target > 0 ? "FWD" : "REV", target < 0 ? col::coral : col::ink);
        }
        else if (m == 1)
        {
            const float v = get ("n2Len1");
            intervalCard.setText (sync() ? beatsName (sliceBeatTable()[(size_t) juce::jlimit (0, 16, (int) std::lround (v * 16))])
                                         : fmtSeconds (expMap (v, 0.02f, 2.5f)));
            dirCard.setText (rev ? "REV" : "UNIT 1", rev ? col::coral : col::ink);
        }
        else
        {
            const float semis = quantiseSemis (get ("n3Depth1") * 12.0f, qn);
            intervalCard.setText (String (juce::CharPointer_UTF8 ("\xc2\xb1")) + String ((int) semis));
            dirCard.setText (rev ? "REV" : "HEAD 1", rev ? col::coral : col::ink);
        }
        reverseButton.set (rev ? "FORWARDS" : "REVERSE", {}, rev ? col::coral : col::line);

        // LEN B in free time also reads as a tempo
        if (b == 0 && m == 0 && ! sync())
        {
            const double secs = lenBSeconds ((int) std::lround (get ("lenB")));
            const double bpmEq = 60.0 / secs;
            const float sp = std::abs (dirToSpeed (get ("dir1"), qn));
            String sub = "= " + String (juce::roundToInt (bpmEq)) + " BPM";
            if (sp > 0.01f && std::abs (sp - 1.0f) > 0.01f) sub << " " << dot << " " << String (juce::roundToInt (bpmEq * sp)) << " AFTER DIR 1";
            sec2Title = "BUFFER " + dot + " " + sub;
        }
        else if (b == 0 && m == 0) sec2Title = "BUFFER " + dot + " ALWAYS LISTENING";

        // expression
        const int et = (int) std::lround (get ("expTarget"));
        for (size_t i = 0; i < expChoices.size() && i < expPills.size(); ++i)
            expPills[i]->set (expChoices[i].name, expChoices[i].target == et, 11.0f);
        expTitle = headPills.isVisible() ? "HEADS PLAYING " + dot + " EXP ON " + String (expTargetNames()[(size_t) juce::jlimit (0, ExpCount - 1, et)])
                                         : "EXPRESSION TARGET, THIS PRESET";
        expCard.setText (expTargetNames()[(size_t) juce::jlimit (0, ExpCount - 1, et)]);
        if (! expSlider.isMouseButtonDown()) expSlider.setValue (proc.ui.exp.load());

        // soft switch section
        const bool latch = get ("latch") > 0.5f;
        if (b == 1)
        {
            stateCard.setText (s == Frozen ? "FROZEN" : "RUNNING", s == Frozen ? col::coral : col::ink);
            recButton.set (s == Frozen ? "FROZEN" : "FREEZE", "PRESS TO HOLD, AGAIN TO RELEASE", s == Frozen ? col::coral : col::line);
            tapButton.set ("TAP " + dot + " FREEZE", {}, col::line);
        }
        else if (m == 0)
        {
            stateCard.setText (s == Held ? "FROZEN" : "LISTENING", s == Held ? col::coral : col::ink);
            recButton.set (s == Held ? "HELD" : "HOLD", "FREEZE THE BUFFER", s == Held ? col::red : col::line);
            tapButton.set ("TAP " + dot + " JUMP THE HEAD", {}, col::line);
        }
        else
        {
            const char* names[] = { "LISTENING", "HELD", "RECORDING", "PLAYING", "EMPTY", "FROZEN", "WAITING FOR BEAT" };
            const bool rec = s == Recording || s == Armed;
            stateCard.setText (names[juce::jlimit (0, 6, s)], rec ? col::red : col::ink);
            recButton.set (rec ? "RECORDING" : (s == Playing && m == 2 ? "REC " + dot + " DUB" : String ("REC")),
                           latch ? "TAP TO START, TAP TO STOP" : (m == 1 ? "HOLD TO SAMPLE, UP TO 4 S" : "HOLD TO RECORD, HOLD AGAIN TO DUB"),
                           rec ? col::red : col::line);
            tapButton.set ("TAP " + dot + " STOP AND CLEAR", {}, col::line);
        }
        recButton.setTextHeight (18.0f);

        // footswitches
        const int down = proc.uiFootDown.load();
        const bool on = get ("on") > 0.5f;
        const bool transcend = get ("transcend") > 0.5f;
        const juce::Colour fc[4] = { col::ink, col::red, col::coral, col::teal };
        const bool lit[4] = { on, s == Recording || s == Armed || s == Held || s == Frozen, rev, false };
        for (int i = 0; i < 4; ++i)
        {
            String tapJob = proc.footJobTap (i), holdJob = proc.footJobHold (i);
            if (i == 0) tapJob = on ? "ON" : "BYPASSED";
            if (i == 0 && transcend && holdJob == "TRANSCEND") holdJob = "TRANSCEND ON";
            if (i == 2 && rev && tapJob == "REVERSE") tapJob = "FORWARDS";
            feet[(size_t) i]->set (String::charToString ((juce::juce_wchar) ('A' + i)), tapJob, holdJob, lit[i], fc[i]);
            feet[(size_t) i]->setDown ((down >> i) & 1);
        }

        // Pebble
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
            bubble (speedText (target, qn));
        }
        else if (! heroIsDir() && rev != lastReverse) pebble.react (Pebble::Reversing);
        lastTarget = heroIsDir() ? target : 1000.0f;
        lastReverse = rev;

        const int pp = proc.uiPresetPulse.load();
        if (pp != lastPresetPulse)
        {
            if (lastPresetPulse >= 0) { pebble.react (Pebble::PitchUp); bubble (String (pi + 1).paddedLeft ('0', 2)); }
            lastPresetPulse = pp;
            lastTarget = 1000.0f;   // a preset's new speed isn't a pitch jump of its own
        }
        const int fp = proc.uiFootPulse.load();
        if (fp != lastFootPulse) { lastFootPulse = fp; if (pebble.current() == Pebble::Idle) pebble.react (Pebble::FootTap); }
        const int kp = proc.uiKeepPulse.load();
        if (kp != lastKeepPulse) { lastKeepPulse = kp; pebble.react (Pebble::Kept); bubble ("KEPT"); }

        const auto trig = proc.ui.triggers.load();
        if (trig != lastTriggers) { lastTriggers = trig; if (pebble.current() == Pebble::Idle) { pebble.react (Pebble::PitchUp); bubble ("JUMP"); } }
        const auto clips = proc.ui.clips.load();
        if (clips != lastClips) { if (clips > lastClips + 2) pebble.react (Pebble::Clipping); lastClips = clips; }

        const auto mood = pebble.current();
        const bool recNotHold = b == 0 && m > 0;
        switch (mood)
        {
            case Pebble::Recording: pebbleTitle = "RECORDING"; pebbleSub = "MOUTH OPEN, RED RING PULSES"; pebbleColour = col::red; break;
            case Pebble::Holding:   pebbleTitle = "HOLDING ITS BREATH"; pebbleSub = "BUFFER FROZEN, RED RING PULSES"; pebbleColour = col::coral; break;
            case Pebble::Reversing: pebbleTitle = "SPINNING BACK"; pebbleSub = "SPINS A FULL TURN BACKWARDS"; pebbleColour = col::coral; break;
            case Pebble::PitchUp:   pebbleTitle = pp != lastPresetPulse ? "PITCH UP" : "PITCH UP"; pebbleSub = "HOPS AND STRETCHES TALL"; pebbleColour = col::teal; break;
            case Pebble::PitchDown: pebbleTitle = "PITCH DOWN"; pebbleSub = "SQUASHES LOW AND DROWSY"; pebbleColour = col::blue; break;
            case Pebble::Clipping:  pebbleTitle = "TOO HOT"; pebbleSub = "X EYES, SHAKES TILL IT COOLS"; pebbleColour = col::red; break;
            case Pebble::Asleep:    pebbleTitle = "ASLEEP"; pebbleSub = "SLEEPS WHILE BYPASSED"; pebbleColour = col::dim; break;
            case Pebble::FootTap:   pebbleTitle = "FOOT TAP"; pebbleSub = "TAPS ALONG WITH YOUR FOOT"; pebbleColour = col::ink; break;
            case Pebble::Kept:      pebbleTitle = "KEPT"; pebbleSub = "A HAPPY NOD, SAVED TO KEEPERS"; pebbleColour = col::green; break;
            default:
                pebbleTitle = s == Playing && recNotHold ? "PLAYING" : "LISTENING";
                pebbleSub = transcend ? "TRANSCEND ON " + dot + " GREEN EYES" : String ("BOBS AND BLINKS TO THE INPUT");
                pebbleColour = col::ink;
                break;
        }
        // a recent message from the footswitches takes over the subtitle for a moment
        if (proc.uiMessage.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() * 0.001 - proc.uiMessageAt < 2.5)
            pebbleSub = proc.uiMessage;

        if (bubbleTime > 0.0f) bubbleTime -= 1.0f / 30.0f;
        repaint();
    }
};

//==============================================================================
class PedalPage : public juce::Component, private juce::Timer
{
public:
    PedalPage (BacktrackProcessor& p, BacktrackEditor& ed) : proc (p), editor (ed), expTargetCard (18.0f)
    {
        back.text = String (juce::CharPointer_UTF8 ("\xe2\x80\xb9 BACK TO THE PLUGIN"));
        back.onClick = [this] { editor.showPedalPage (false); };
        for (auto* c : std::initializer_list<juce::Component*> { &back, &expTargetCard, &expSlider, &calButton, &learnExp, &defaults,
                                                                  &exportButton, &pebble })
            addAndMakeVisible (c);
        for (int i = 0; i < 4; ++i)
        {
            tapCards.push_back (std::make_unique<FlipCard> (15.0f));
            holdCards.push_back (std::make_unique<FlipCard> (15.0f));
            learn.push_back (std::make_unique<BigButton>());
            addAndMakeVisible (*tapCards.back());
            addAndMakeVisible (*holdCards.back());
            addAndMakeVisible (*learn.back());
            learn.back()->setTextHeight (12.0f);
            learn.back()->onPress = [this, i] { proc.learnFootswitch (i); };
        }
        auto pills = [this] (std::vector<std::unique_ptr<PillButton>>& v, int n)
        { for (int i = 0; i < n; ++i) { v.push_back (std::make_unique<PillButton>()); addAndMakeVisible (*v.back()); } };
        pills (layoutPills, 2); pills (curvePills, 3); pills (smoothPills, 3); pills (inputPills, 2); pills (keeperPills, 2);
        layoutPills[0]->onClick = [this] { proc.setFootLayout (BacktrackProcessor::LayoutBacktrack); };
        layoutPills[1]->onClick = [this] { proc.setFootLayout (BacktrackProcessor::LayoutClassic); };
        for (int i = 0; i < 3; ++i)
        {
            curvePills[(size_t) i]->onClick = [this, i] { proc.pedals().curve = i; proc.saveSettings(); };
            smoothPills[(size_t) i]->onClick = [this, i] { proc.pedals().expSmooth = i; proc.saveSettings(); };
        }
        inputPills[0]->onClick = [this] { proc.setFootDevice ({}); };
        inputPills[1]->onClick = [this] { proc.setFootDevice ("none"); };
        keeperPills[0]->onClick = [this] { proc.setKeeperLive (true); };
        keeperPills[1]->onClick = [this] { proc.setKeeperLive (false); };

        expSlider.onChange = [this] (float v)
        {
            if (proc.apvts.getRawParameterValue ("expOn")->load() < 0.5f) proc.setPlain ("expOn", 1.0f);
            proc.setPlain ("exp", v);
        };
        calButton.setTextHeight (13.0f);
        calButton.onPress = [this] { proc.calibrateExpression(); };
        learnExp.setTextHeight (12.0f);
        learnExp.onPress = [this] { proc.learnExpression(); };
        defaults.set ("DEFAULTS", {}, col::line);
        defaults.setTextHeight (12.0f);
        defaults.onPress = [this] { proc.resetFootswitches(); };
        exportButton.set ("EXPORT KEEPERS", {}, col::green);
        exportButton.setTextHeight (13.0f);
        exportButton.onPress = [this] { proc.exportKeepers(); };
        startTimerHz (20);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::ink);
        g.setFont (font (26.0f, 0.04f));
        g.drawText ("BACKTRACK", 20, 20, 150, 40, juce::Justification::centredLeft);
        label (g, "PEDAL PAGE", { 176, 20, 200, 40 });

        paintPanel (g, { 20, 76, 788, 480 });
        paintPanel (g, { 828, 76, 412, 480 });
        paintPanel (g, { 20, 572, 1220, 108 });

        auto& s = proc.pedals();
        label (g, "FOOTSWITCHES", { 36, 92, 130, 30 });
        label (g, deviceText, { 166, 92, 330, 30 }, 12.0f, proc.pedalsDirect() ? col::teal : col::dim);
        label (g, "LAYOUT", { 504, 92, 64, 30 }, 12.0f);

        const int learning = proc.learningWhat();
        for (int i = 0; i < 4; ++i)
        {
            auto r = juce::Rectangle<int> (36 + i * 192, 136, 180, 290);
            g.setColour (col::bg);
            g.fillRoundedRectangle (r.toFloat(), 12.0f);
            g.setColour (learning == i ? col::teal : col::line.withAlpha (0.0f));
            g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 12.0f, 2.0f);
            g.setColour (col::ink);
            g.setFont (font (30.0f, 0.02f));
            g.drawText (String::charToString ((juce::juce_wchar) ('A' + i)), r.getX() + 12, r.getY() + 10, 30, 32, juce::Justification::centredLeft);
            label (g, "FOOTSWITCH " + String::charToString ((juce::juce_wchar) ('A' + i)), { r.getX() + 44, r.getY() + 18, 130, 18 }, 11.0f);
            label (g, "TAP", { r.getX() + 12, r.getY() + 48, 100, 14 }, 10.0f);
            label (g, "HOLD", { r.getX() + 12, r.getY() + 120, 100, 14 }, 10.0f);
            const String listen = proc.pedalsDirect() ? "LISTENS TO CC " + String (s.fsCC[(size_t) i]) + ", ANY CHANNEL"
                                                      : "LISTENS TO CC " + String (s.fsCC[(size_t) i]) + " ON CHANNEL " + String (s.fsChannel[(size_t) i]);
            g.setColour (col::dim);
            g.setFont (font (11.0f, 0.05f));
            g.drawFittedText (listen, { r.getX() + 12, r.getY() + 192, 156, 30 }, juce::Justification::topLeft, 2, 1.0f);
        }
        // combos
        const bool classic = proc.footLayout() == BacktrackProcessor::LayoutClassic;
        struct Combo { const char* name; const char* tap; const char* hold; };
        const Combo combos[2] = { { "A+B", "NEXT MODE", "STANDARD / NIGHTLY" }, { "C+D", "FOOTSWITCHES TO THE NEXT BACKTRACK", "KEEPER" } };
        for (int i = 0; i < 2; ++i)
        {
            auto r = juce::Rectangle<int> (36 + i * 384, 438, 372, 52);
            g.setColour (col::bg);
            g.fillRoundedRectangle (r.toFloat(), 12.0f);
            g.setColour (col::ink);
            g.setFont (font (20.0f, 0.04f));
            g.drawText (combos[i].name, r.getX() + 14, r.getY(), 56, r.getHeight(), juce::Justification::centredLeft);
            label (g, "TAP " + dot + " " + String (combos[i].tap), { r.getX() + 76, r.getY() + 8, 290, 18 }, 12.0f, col::ink);
            label (g, "HOLD 1 S " + dot + " " + String (combos[i].hold), { r.getX() + 76, r.getY() + 26, 290, 18 }, 12.0f, col::ink);
        }
        (void) classic;
        wrapText (g, "Any momentary footswitch works. Defaults: CC 20 to 23 on channel 16, or any channel when Backtrack reads the "
                     "footswitch's USB cable directly. Settings are shared by every Flip Fella plugin on this computer.",
                  { 36, 502, 640, 44 }, 13.0f, col::dim, 3);

        // expression
        label (g, "EXPRESSION", { 844, 88, 200, 22 });
        label (g, "HEEL " + proc.expressionRangeText (false) + " " + dot + " TOE " + proc.expressionRangeText (true), { 1026, 128, 200, 16 }, 11.0f, col::ink);
        label (g, "SAVED WITH EACH PRESET", { 1026, 144, 200, 16 }, 11.0f);
        label (g, "PEDAL POSITION " + String (juce::roundToInt (proc.ui.exp.load() * 127.0f)) + " " + dot + " HEEL " + String (s.heel) + " " + dot + " TOE " + String (s.toe),
               { 844, 178, 380, 16 }, 11.0f);
        auto bar = juce::Rectangle<float> (844, 228, 380, 14);
        g.setColour (col::ring); g.fillRoundedRectangle (bar, 7.0f);
        g.setColour (col::teal); g.fillRoundedRectangle (bar.withWidth (juce::jmax (14.0f, bar.getWidth() * proc.ui.exp.load())), 7.0f);
        wrapText (g, calNote, { 844, 308, 380, 40 }, 13.0f, col::dim, 2);
        label (g, "CURVE", { 844, 352, 200, 18 }, 11.0f);
        label (g, "SMOOTHING", { 844, 418, 200, 18 }, 11.0f);

        // bottom strip
        label (g, "FOOTSWITCH INPUT", { 150, 588, 240, 18 });
        label (g, proc.footInputStatus().toUpperCase(), { 150, 646, 270, 16 }, 10.0f, col::faint);
        label (g, "KEEPERS ARE TAGGED", { 432, 588, 240, 18 });
        label (g, proc.lastKeeper() > 0 ? "LAST KEPT: NUMBER " + String (proc.lastKeeper()) : String ("NOTHING KEPT YET"), { 432, 646, 260, 16 }, 10.0f, col::faint);
        wrapText (g, "In Ableton, switch Track, Sync and Remote off for the footswitch input. Backtrack reads it directly, "
                     "so every copy just works.", { 940, 592, 284, 70 }, 13.0f, col::dim, 4);
    }

    void resized() override
    {
        back.setBounds (1000, 20, 240, 40);
        for (int i = 0; i < 2; ++i) layoutPills[(size_t) i]->setBounds (572 + i * 113, 92, 107, 30);
        for (int i = 0; i < 4; ++i)
        {
            const int x = 36 + i * 192;
            tapCards[(size_t) i]->setBounds (x + 12, 202, 156, 46);
            holdCards[(size_t) i]->setBounds (x + 12, 274, 156, 46);
            learn[(size_t) i]->setBounds (x + 12, 378, 156, 34);
        }
        defaults.setBounds (688, 505, 104, 34);

        expTargetCard.setBounds (844, 116, 170, 48);
        expSlider.setBounds (840, 196, 388, 24);
        calButton.setBounds (844, 255, 380, 44);
        for (int i = 0; i < 3; ++i)
        {
            curvePills[(size_t) i]->setBounds (844 + i * 129, 374, 123, 30);
            smoothPills[(size_t) i]->setBounds (844 + i * 129, 440, 123, 30);
        }
        learnExp.setBounds (844, 494, 380, 34);

        pebble.setBounds (36, 584, 92, 92);
        for (int i = 0; i < 2; ++i)
        {
            inputPills[(size_t) i]->setBounds (150 + i * 128, 610, 122, 28);
            keeperPills[(size_t) i]->setBounds (432 + i * 96, 610, 90, 28);
        }
        exportButton.setBounds (714, 604, 170, 44);
    }

private:
    BacktrackProcessor& proc;
    BacktrackEditor& editor;
    LinkButton back;
    FlipCard expTargetCard;
    ThinSlider expSlider;
    BigButton calButton, learnExp, defaults, exportButton;
    Pebble pebble;
    std::vector<std::unique_ptr<FlipCard>> tapCards, holdCards;
    std::vector<std::unique_ptr<BigButton>> learn;
    std::vector<std::unique_ptr<PillButton>> layoutPills, curvePills, smoothPills, inputPills, keeperPills;
    String deviceText, calNote;
    int lastKept = 0;

    void timerCallback() override
    {
        auto& s = proc.pedals();
        deviceText = proc.pedalsDirect() ? "READING YOUR FOOTSWITCH DIRECTLY " + dot + " READY"
                                         : (s.footDevice == "none" ? String ("THROUGH ABLETON ONLY") : String ("NO FOOTSWITCH FOUND, PLUG ONE IN"));
        const bool classic = proc.footLayout() == BacktrackProcessor::LayoutClassic;
        layoutPills[0]->set ("BACKTRACK", ! classic);
        layoutPills[1]->set ("CLASSIC", classic);
        const int learning = proc.learningWhat();
        for (int i = 0; i < 4; ++i)
        {
            tapCards[(size_t) i]->setText (proc.footJobTap (i));
            holdCards[(size_t) i]->setText (proc.footJobHold (i));
            learn[(size_t) i]->set (learning == i ? "PRESS IT NOW" : "LEARN", {}, learning == i ? col::teal : col::line);
        }
        const int t = (int) std::lround (proc.apvts.getRawParameterValue ("expTarget")->load());
        expTargetCard.setText (expTargetNames()[(size_t) juce::jlimit (0, ExpCount - 1, t)]);
        if (! expSlider.isMouseButtonDown()) expSlider.setValue (proc.ui.exp.load());
        calButton.set (proc.isCalibrating() ? "HEEL DOWN, THEN ROCK TO TOE" : "CALIBRATE HEEL AND TOE", {}, proc.isCalibrating() ? col::teal : col::line);
        calNote = proc.isCalibrating() ? "Listening for six seconds. Rock the pedal all the way, both ways."
                                       : "Fixes a pedal that doesn't reach 0 or 127, or one that runs backwards.";
        static const char* curves[3] = { "LINEAR", "SLOW START", "FAST START" };
        static const char* smooths[3] = { "OFF", "LOW", "HIGH" };
        for (int i = 0; i < 3; ++i)
        {
            curvePills[(size_t) i]->set (curves[i], s.curve == i, 11.0f);
            smoothPills[(size_t) i]->set (smooths[i], s.expSmooth == i, 11.0f);
        }
        learnExp.set (learning == BacktrackProcessor::LearnExpression ? "MOVE THE PEDAL NOW" : "LEARN EXPRESSION " + dot + " CC " + String (s.expCC), {},
                      learning == BacktrackProcessor::LearnExpression ? col::teal : col::line);
        inputPills[0]->set ("READ DIRECTLY", s.footDevice != "none", 11.0f);
        inputPills[1]->set ("ABLETON ONLY", s.footDevice == "none", 11.0f);
        keeperPills[0]->set ("LIVE", s.keeperLive, 11.0f);
        keeperPills[1]->set ("STUDIO", ! s.keeperLive, 11.0f);
        if (proc.lastKeeper() != lastKept) { lastKept = proc.lastKeeper(); pebble.react (Pebble::Kept); }
        repaint();
    }
};

//==============================================================================
BacktrackEditor::BacktrackEditor (BacktrackProcessor& p) : AudioProcessorEditor (&p)
{
    main = std::make_unique<MainPage> (p, *this);
    pedal = std::make_unique<PedalPage> (p, *this);
    addAndMakeVisible (*main);
    addChildComponent (*pedal);
    main->setBounds (0, 0, kWidth, kHeight);
    pedal->setBounds (0, 0, kWidth, kHeight);
    setResizable (true, true);
    setResizeLimits (kWidth / 2, kHeight / 2, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
}

BacktrackEditor::~BacktrackEditor() = default;

void BacktrackEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void BacktrackEditor::showPedalPage (bool show)
{
    pedal->setVisible (show);
    main->setVisible (! show);
}

void BacktrackEditor::resized()
{
    const float s = (float) getWidth() / (float) kWidth;
    main->setTransform (juce::AffineTransform::scale (s));
    pedal->setTransform (juce::AffineTransform::scale (s));
}
