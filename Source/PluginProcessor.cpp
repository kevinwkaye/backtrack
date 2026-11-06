#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;
using namespace backtrack;

// Several Backtrack copies can be open at once. The footswitches drive one of them: the
// one whose panel was last touched, or the next one along when C and D are tapped together.
namespace
{
SpinLock focusLock;
Array<BacktrackProcessor*> instances;
std::atomic<BacktrackProcessor*> focused { nullptr };

bool hasFocusPtr (const BacktrackProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

thread_local bool fromDeviceNow = false;

void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    BacktrackProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}

double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }

// controls you play rather than set: changing them doesn't mark the preset as edited
bool isPerformance (const String& id)
{
    static const StringArray p { "on", "exp", "expOn", "reverse", "sync", "latch", "trigSens" };
    return p.contains (id);
}
} // namespace

BacktrackProcessor::BacktrackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BACKTRACK", createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    for (auto& s : paramSpecs())
    {
        specRaw.push_back (apvts.getRawParameterValue (s.id));
        apvts.addParameterListener (s.id, &editWatch);
    }

    rExp = apvts.getRawParameterValue ("exp");
    rExpOn = apvts.getRawParameterValue ("expOn");
    rExpTarget = apvts.getRawParameterValue ("expTarget");
    rExpLo = apvts.getRawParameterValue ("expLo");
    rExpHi = apvts.getRawParameterValue ("expHi");
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    loadUserPresets();
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    loadPreset (0);
    startTimerHz (100);
}

BacktrackProcessor::~BacktrackProcessor()
{
    stopTimer();
    for (auto& s : paramSpecs()) apvts.removeParameterListener (s.id, &editWatch);
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void BacktrackProcessor::EditWatch::parameterChanged (const String& id, float)
{
    if (! owner.applyingPreset.load() && ! isPerformance (id)) owner.edited.store (true);
}

bool BacktrackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

void BacktrackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
    scratch.setSize (2, jmax (1, samplesPerBlock));
    if (pendingLoop.getSize() > 0) loopToRestore = true;
}

bool BacktrackProcessor::hasFootFocus() const { return hasFocusPtr (this); }

//==============================================================================
void BacktrackProcessor::readParams (int numSamples)
{
    auto& p = params;
    const auto& specs = paramSpecs();
    for (size_t i = 0; i < specs.size(); ++i)
        setParam (p, specs[i].id, specRaw[i]->load (std::memory_order_relaxed));

    // The expression pedal moves this preset's chosen control between heel and toe.
    // Smoothing takes the steps out of a 7-bit pedal.
    const float target = rExp->load();
    const int smooth = settings.expSmooth;
    if (smooth == 0) expSmoothed = target;
    else
    {
        const double tau = smooth == 1 ? 0.04 : 0.15;
        const float a = (float) (1.0 - std::exp (-numSamples / (tau * currentSampleRate)));
        expSmoothed += a * (target - expSmoothed);
    }
    if (rExpOn->load() > 0.5f)
        applyExpression (p, (int) std::lround (rExpTarget->load()), rExpLo->load(), rExpHi->load(), expSmoothed);
    ui.exp = expSmoothed;

    p.hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) { p.bpm = *bpm; ui.hostTempo = true; }
            p.hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) p.ppqAtBlockStart = *ppq;
        }
    ui.bpm = p.bpm;
}

void BacktrackProcessor::applyEngineCommands()
{
    const int c = cmds.exchange (0);
    if (c & CmdDown) engine.softDown();
    if (c & CmdCancel) engine.cancelPress();
    if (c & CmdUp) engine.softUp();
    if (c & CmdClear) engine.stopAndClear();
}

void BacktrackProcessor::applyPendingLoop()
{
    loopToRestore = false;
    if (pendingLoopLen <= 0 || pendingLoopMode < 1) return;
    const auto* data = static_cast<const int16_t*> (pendingLoop.getData());
    const int srcLen = pendingLoopLen;
    const double ratio = savedLoopSampleRate / currentSampleRate;
    const int dstLen = jmin (engine.getCapacity() - 8, (int) (srcLen / ratio));
    for (int c = 0; c < 2; ++c)
    {
        auto& b = engine.getBufferForRestore (c);
        for (int i = 0; i < dstLen; ++i)
        {
            const double s = i * ratio;
            const int i0 = jmin (srcLen - 1, (int) s), i1 = jmin (srcLen - 1, i0 + 1);
            const float t = (float) (s - i0);
            const float a = data[(size_t) c * (size_t) srcLen + (size_t) i0] / 32767.0f;
            const float bb = data[(size_t) c * (size_t) srcLen + (size_t) i1] / 32767.0f;
            b[(size_t) i] = a + t * (bb - a);
        }
    }
    engine.restoreLoop (pendingLoopMode, dstLen);
}

void BacktrackProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2) return;
    if (loopToRestore.load()) applyPendingLoop();

    for (const auto meta : midi) handleMidi (meta.getMessage(), false);
    midi.clear();

    readParams (n);
    applyEngineCommands();

    const int inCh = getTotalNumInputChannels();
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);
    scratch.copyFrom (0, 0, buffer, 0, 0, n);
    scratch.copyFrom (1, 0, buffer, inCh > 1 ? 1 : 0, 0, n);
    keeper.pushInput (scratch.getReadPointer (0), scratch.getReadPointer (1), n);

    engine.process (scratch.getReadPointer (0), scratch.getReadPointer (1),
                    buffer.getWritePointer (0), buffer.getWritePointer (1), n, params);
    keeper.pushOutput (buffer.getReadPointer (0), buffer.getReadPointer (1), n);

    const auto& s = engine.getStatus();
    ui.state = s.state;
    ui.loopSeconds = s.loopSeconds;
    ui.recFraction = s.recFraction;
    ui.playFraction = s.playFraction;
    ui.speed1 = s.speed1;
    ui.bufferSeconds = s.bufferSeconds;
    ui.inLevel = s.inputLevel;
    ui.outLevel = s.outputLevel;
    ui.triggers = s.triggerCount;
    ui.clips = s.clipCount;
}

//==============================================================================
// MIDI: footswitches and expression. The original pedal has no MIDI of its own.
//==============================================================================
void BacktrackProcessor::queue (int kind, int a)
{
    auto& f = fromDeviceNow ? directFifo : fifo;
    auto& buf = fromDeviceNow ? directPending : pending;
    const auto scope = f.write (1);
    if (scope.blockSize1 > 0) buf[(size_t) scope.startIndex1] = { kind, a };
}

void BacktrackProcessor::handleDirectMidi (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void BacktrackProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    if (! m.isController()) return;
    const int ch = m.getChannel(), cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc);
        return;
    }

    // Straight from the footswitch device any channel will do. Through Ableton the channel
    // must match, and when the device is read directly the copy through Ableton is ignored.
    if (! fromDevice && pedalsDirect()) return;
    int foot = -1;
    for (int k = 0; k < 4 && foot < 0; ++k)
        if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
    const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
    const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
    if (! (foot >= 0 || keep || expression)) return;

    if (! fromDevice)
    {
        hostFeedsPedals.store (true);
        const SpinLock::ScopedLockType l (focusLock);
        auto* f = focused.load();
        if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
        if (focused.load() != this) return;
    }
    if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot);
    else if (keep) { if (val >= 64) queue (KeepCC, 0); }
    else queue (Expression, val);
}

void BacktrackProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void BacktrackProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = dynamic_cast<RangedAudioParameter*> (apvts.getParameter (id)))
        setNorm (id, p->convertTo0to1 (plain));
}

void BacktrackProcessor::togglePlain (const String& id) { setPlain (id, get (id.toRawUTF8()) > 0.5f ? 0.0f : 1.0f); }

void BacktrackProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = nowSeconds();
}

void BacktrackProcessor::touchPanel() { focused.store (this); }

// What heel or toe does, in the target's own words: speeds for DIR, percentages otherwise
String BacktrackProcessor::expressionRangeText (bool toe) const
{
    const int t = (int) std::lround (get ("expTarget"));
    const float v = get (toe ? "expHi" : "expLo");
    if (t == ExpDir1 || t == ExpDir2 || t == ExpDir3 || t == ExpAllHeads)
    {
        const float sp = (v - 0.5f) * 4.0f;
        if (std::abs (sp) < 0.01f) return "STOP";
        String s (std::abs (sp), 2);
        while (s.endsWithChar ('0')) s = s.dropLastCharacters (1);
        if (s.endsWithChar ('.')) s = s.dropLastCharacters (1);
        return s + "X" + (sp < 0 ? " REV" : "");
    }
    if (t == ExpLenB) return String (jlimit (0, 9, (int) std::lround (v * 9.0f)) + 1);
    return String (roundToInt (v * 100.0f)) + "%";
}

void BacktrackProcessor::timerCallback()
{
    const double now = nowSeconds();
    if (now >= nextSettingsCheck)
    {
        // settings changed by another copy or another Flip Fella plugin
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFootFocus()) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case KeepCC: keepNow(); break;
                case Expression:
                {
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = q.a;
                        calLo = jmin (calLo, q.a);
                        calHi = jmax (calHi, q.a);
                        break;
                    }
                    if (get ("expOn") < 0.5f) setPlain ("expOn", 1.0f);
                    setPlain ("exp", settings.expressionValue (q.a));
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper) { settings.keeperChannel = ch; settings.keeperCC = cc; say ("KEEPER IS CC " + String (cc)); }
                    else if (what == LearnExpression) { settings.expChannel = ch; settings.expCC = cc; say ("EXPRESSION IS CC " + String (cc)); }
                    saveSettings();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    footswitches.tick (now);

    // a two-switch tap acts once both feet are off, so holding on for a second can do something else
    if (comboABPending && ! footswitches.isDown (0) && ! footswitches.isDown (1))
    {
        comboABPending = false;
        const int m = ((int) std::lround (get ("mode")) + 1) % 3;
        setPlain ("mode", (float) m);
        static const char* const names[3] = { "MODE 1 · DELAY", "MODE 2 · SLICE", "MODE 3 · LOOP" };
        say (names[m]);
    }
    if (comboCDPending && ! footswitches.isDown (2) && ! footswitches.isDown (3))
    {
        comboCDPending = false;
        const SpinLock::ScopedLockType l (focusLock);
        const int i = instances.indexOf (this);
        auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
        focused.store (next);
        if (next != nullptr) next->say (instances.size() > 1 ? "FOOTSWITCHES ARE HERE" : "ONLY ONE BACKTRACK OPEN");
    }

    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            saveSettings();
            say ("EXPRESSION CALIBRATED");
        }
        else say ("NO MOVEMENT, TRY AGAIN");
    }
}

//==============================================================================
// Footswitches
//   A  tap: on and off              hold: Transcend
//   B  the soft switch, acting the moment you press it
//   C  tap: reverse every head      hold: reverse while held
//   D  tap: next preset             hold: previous preset
//   A+B tap: next mode   held 1 s: Standard / Nightly
//   C+D tap: footswitches to the next Backtrack   held 1 s: Keeper
// Classic keeps only the pedal's own two: A on and off, B the soft switch.
//==============================================================================
const char* BacktrackProcessor::footJobTap (int sw) const
{
    const bool classic = footLayout() == LayoutClassic;
    switch (sw)
    {
        case 0: return "ON / OFF";
        case 1:
        {
            const bool nightly = get ("bank") > 0.5f;
            const int mode = (int) std::lround (get ("mode"));
            if (nightly) return "FREEZE";
            return mode == 0 ? "JUMP" : "STOP + CLEAR";
        }
        case 2: return classic ? "-" : "REVERSE";
        case 3: return classic ? "-" : "NEXT PRESET";
        default: return "";
    }
}

const char* BacktrackProcessor::footJobHold (int sw) const
{
    const bool classic = footLayout() == LayoutClassic;
    switch (sw)
    {
        case 0: return classic ? "-" : "TRANSCEND";
        case 1:
        {
            if (get ("bank") > 0.5f) return "FREEZE";
            const int mode = (int) std::lround (get ("mode"));
            return mode == 0 ? "FREEZE" : (mode == 1 ? "SAMPLE" : "RECORD / DUB");
        }
        case 2: return classic ? "-" : "REVERSE HELD";
        case 3: return classic ? "-" : "PREV PRESET";
        default: return "";
    }
}

void BacktrackProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    const bool classic = footLayout() == LayoutClassic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);

    switch (e)
    {
        case E::ComboAB:
            // B's press already started something: undo it, it was half of a pair
            if (bDownFired) { cmds.fetch_or (CmdCancel); bDownFired = false; }
            comboABPending = true;
            return;
        case E::ComboHoldAB:
            comboABPending = false;
            setPlain ("bank", get ("bank") > 0.5f ? 0.0f : 1.0f);
            say (get ("bank") > 0.5f ? "NIGHTLY MODES" : "STANDARD MODES");
            return;
        case E::ComboCD:
            if (reverseOnDown) { togglePlain ("reverse"); reverseOnDown = false; }
            comboCDPending = true;
            return;
        case E::ComboHoldCD:
            comboCDPending = false;
            focused.store (this);
            keepNow();
            return;
        default: break;
    }

    switch (sw)
    {
        case 0:
            if (e == E::Tap)
            {
                togglePlain ("on");
                say (get ("on") > 0.5f ? "ON" : (get ("trails") > 0.5f ? "OFF, TRAILS RING ON" : "OFF"));
            }
            if (e == E::HoldStart && ! classic)
            {
                togglePlain ("transcend");
                say (get ("transcend") > 0.5f ? "TRANSCEND ON: THE BUFFER CARRIES ACROSS" : "TRANSCEND OFF");
            }
            break;
        case 1:
            if (e == E::Down) { cmds.fetch_or (CmdDown); bDownFired = true; }
            if (e == E::Tap || e == E::HoldEnd) { cmds.fetch_or (CmdUp); bDownFired = false; }
            break;
        case 2:
            if (classic) break;
            if (e == E::Down) { togglePlain ("reverse"); reverseOnDown = true; }
            if (e == E::Tap) reverseOnDown = false;
            if (e == E::HoldEnd) { togglePlain ("reverse"); reverseOnDown = false; }
            break;
        case 3:
            if (classic) break;
            if (e == E::Tap) stepPreset (1);
            if (e == E::HoldStart) stepPreset (-1);
            break;
        default: break;
    }
}

void BacktrackProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, nowSeconds());
}

void BacktrackProcessor::footUp (int sw) { footswitches.release (sw, nowSeconds()); }

void BacktrackProcessor::setFootLayout (int layout)
{
    settings.backtrackLayout = jlimit (0, 1, layout);
    saveSettings();
    say (settings.backtrackLayout == LayoutClassic ? "CLASSIC: A AND B ONLY" : "BACKTRACK FOOTSWITCHES");
}

void BacktrackProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    saveSettings();
    direct->setWanted (device);
}

void BacktrackProcessor::saveSettings()
{
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

void BacktrackProcessor::learnFootswitch (int sw) { learnTarget.store (sw); say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw))); }
void BacktrackProcessor::learnExpression() { learnTarget.store (LearnExpression); say ("MOVE THE EXPRESSION PEDAL"); }
void BacktrackProcessor::learnKeeper() { learnTarget.store (LearnKeeper); say ("PRESS YOUR KEEPER SWITCH"); }

void BacktrackProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = nowSeconds() + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void BacktrackProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.expChannel = 16; settings.expCC = 11;
    settings.heel = 0; settings.toe = 127; settings.curve = 0; settings.expSmooth = 1;
    saveSettings();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

//==============================================================================
// Presets
//==============================================================================
File BacktrackProcessor::userPresetFolder()
{
    return File::getSpecialLocation (File::userDocumentsDirectory)
        .getChildFile ("Flip Fella Presets").getChildFile ("Backtrack");
}

void BacktrackProcessor::loadUserPresets()
{
    userPresets.clear();
    auto files = userPresetFolder().findChildFiles (File::findFiles, false, "*.xml");
    files.sort();
    userPresets.addArray (files);
}

String BacktrackProcessor::presetName (int i) const
{
    const auto& f = factoryPresets();
    if (i >= 0 && i < (int) f.size()) return f[(size_t) i].name;
    const int u = i - (int) f.size();
    return isPositiveAndBelow (u, userPresets.size()) ? userPresets[u].getFileNameWithoutExtension().toUpperCase() : String ("INIT");
}

String BacktrackProcessor::presetHint (int i) const
{
    const auto& f = factoryPresets();
    if (i >= 0 && i < (int) f.size()) return f[(size_t) i].hint;
    return "Your own preset.";
}

void BacktrackProcessor::loadPreset (int i)
{
    const int count = numPresets();
    if (count == 0) return;
    i = ((i % count) + count) % count;
    applyingPreset = true;
    const auto& f = factoryPresets();
    if (i < (int) f.size())
    {
        for (auto& kv : presetValues (f[(size_t) i])) setPlain (kv.first, kv.second);
    }
    else if (auto xml = parseXML (userPresets[i - (int) f.size()]))
    {
        for (auto& s : paramSpecs())
            if (! isPerformance (s.id) && xml->hasAttribute (s.id))
                setPlain (s.id, (float) xml->getDoubleAttribute (s.id));
    }
    applyingPreset = false;
    preset = i;
    edited = false;
    uiPresetPulse.fetch_add (1);
    say (String (i + 1).paddedLeft ('0', 2) + " " + presetName (i));
}

void BacktrackProcessor::stepPreset (int direction) { loadPreset (preset + direction); }

void BacktrackProcessor::saveUserPreset()
{
    auto folder = userPresetFolder();
    folder.createDirectory();
    int n = 1;
    while (folder.getChildFile ("USER " + String (n).paddedLeft ('0', 2) + ".xml").exists()) ++n;
    const auto file = folder.getChildFile ("USER " + String (n).paddedLeft ('0', 2) + ".xml");
    XmlElement xml ("BacktrackPreset");
    for (auto& s : paramSpecs())
        if (! isPerformance (s.id)) xml.setAttribute (s.id, get (s.id));
    if (xml.writeTo (file))
    {
        loadUserPresets();
        preset = (int) factoryPresets().size() + userPresets.indexOf (file);
        edited = false;
        uiPresetPulse.fetch_add (1);
        say ("SAVED AS " + file.getFileNameWithoutExtension());
    }
    else say ("COULDN'T SAVE THE PRESET");
}

//==============================================================================
// Keeper
//==============================================================================
void BacktrackProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Backtrack");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "Montreal Assembly Count to 5");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    e->setProperty ("preset", String (preset + 1).paddedLeft ('0', 2) + " " + presetName (preset) + (edited.load() ? " (edited)" : ""));
    e->setProperty ("tempo", ui.bpm.load());
    e->setProperty ("tempoFromHost", ui.hostTempo.load());
    e->setProperty ("footswitchLayout", footLayout() == LayoutClassic ? "Classic" : "Backtrack");

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& tn = expTargetNames();
    exp->setProperty ("target", tn[(size_t) jlimit (0, (int) tn.size() - 1, (int) std::lround (get ("expTarget")))]);
    exp->setProperty ("active", get ("expOn") > 0.5f);
    exp->setProperty ("position", ui.exp.load());
    exp->setProperty ("movedSecondsAgo", nowSeconds() - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    static const char* const states[] = { "Listening", "Held", "Recording", "Playing", "Empty", "Frozen", "Armed" };
    e->setProperty ("state", states[jlimit (0, 6, ui.state.load())]);
    e->setProperty ("loopSeconds", ui.loopSeconds.load());

    DynamicObject::Ptr prm = new DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            prm->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (prm.get()));

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File BacktrackProcessor::exportKeepers()
{
    const auto f = keeper.exportZip();
    if (f.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); f.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return f;
}

void BacktrackProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    saveSettings();
}

//==============================================================================
// State: every parameter, the preset you're on, and a Mode 2 or Mode 3 loop
//==============================================================================
void BacktrackProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    xml->setAttribute ("preset", preset);
    xml->setAttribute ("presetEdited", edited.load());

    const int len = engine.getLoopLength();
    const int mode = (int) std::lround (get ("mode"));
    if (len > 0 && get ("bank") < 0.5f && mode >= 1)
    {
        MemoryBlock pcm ((size_t) len * 2 * sizeof (int16_t));
        auto* d = static_cast<int16_t*> (pcm.getData());
        const int base = engine.getLoopBase();
        for (int c = 0; c < 2; ++c)
        {
            const auto& b = engine.getBuffer (c);
            for (int i = 0; i < len; ++i)
                d[(size_t) c * (size_t) len + (size_t) i] = (int16_t) jlimit (-32767, 32767, (int) std::lround (b[(size_t) (base + i)] * 32767.0f));
        }
        MemoryOutputStream zipped;
        {
            GZIPCompressorOutputStream gz (zipped, 6);
            gz.write (pcm.getData(), pcm.getSize());
        }
        auto* loop = xml->createNewChildElement ("LOOP");
        loop->setAttribute ("mode", mode);
        loop->setAttribute ("length", len);
        loop->setAttribute ("sampleRate", currentSampleRate);
        loop->setAttribute ("data", zipped.getMemoryBlock().toBase64Encoding());
    }
    copyXmlToBinary (*xml, destData);
}

void BacktrackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr) return;

    if (auto* loop = xml->getChildByName ("LOOP"))
    {
        MemoryBlock zipped;
        if (zipped.fromBase64Encoding (loop->getStringAttribute ("data")))
        {
            MemoryInputStream in (zipped, false);
            GZIPDecompressorInputStream gz (in);
            MemoryBlock pcm;
            gz.readIntoMemoryBlock (pcm);
            const int len = loop->getIntAttribute ("length");
            if ((size_t) len * 2 * sizeof (int16_t) == pcm.getSize())
            {
                pendingLoop = pcm;
                pendingLoopLen = len;
                pendingLoopMode = loop->getIntAttribute ("mode");
                savedLoopSampleRate = loop->getDoubleAttribute ("sampleRate", 48000.0);
                loopToRestore = true;
            }
        }
        xml->removeChildElement (loop, true);
    }

    const int savedPreset = xml->getIntAttribute ("preset", 0);
    const bool savedEdited = xml->getBoolAttribute ("presetEdited", false);
    if (xml->hasTagName (apvts.state.getType()))
    {
        applyingPreset = true;
        apvts.replaceState (ValueTree::fromXml (*xml));
        applyingPreset = false;
    }
    preset = jlimit (0, jmax (0, numPresets() - 1), savedPreset);
    edited = savedEdited;
    uiPresetPulse.fetch_add (1);
}

AudioProcessorEditor* BacktrackProcessor::createEditor() { return new BacktrackEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BacktrackProcessor(); }
