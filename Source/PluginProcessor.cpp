#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "LaunchpadMap.h"

using namespace backtrack;

BacktrackProcessor::BacktrackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BACKTRACK", createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
}

bool BacktrackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BacktrackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    scratch.setSize (2, juce::jmax (1, samplesPerBlock));
    if (pendingLoop.getSize() > 0)
        loopToRestore = true;
}

//==============================================================================
void BacktrackProcessor::readParams()
{
    auto& p = params;
    p.bank = (int) get (ids::bank);
    p.mode = (int) get (ids::mode);
    p.dir1 = get (ids::dir1);
    p.dir2 = get (ids::dir2);
    p.dir3 = get (ids::dir3);
    p.lenB = (int) get (ids::lenB);
    p.lenS = get (ids::lenS);
    p.fbk = get (ids::fbk);
    p.rand = get (ids::rand);
    p.mix = get (ids::mix);
    p.quant = (int) get (ids::quant);
    p.heads = (int) get (ids::heads);

    p.fbkFilter = get (ids::fbkFilter);
    p.lfoRate = get (ids::lfoRate);
    p.lfoDepth = get (ids::lfoDepth);
    p.trigSens = get (ids::trigSens);
    p.jump = get (ids::jump);
    p.sliceFade = get (ids::sliceFade);
    p.stepRate = get (ids::stepRate);
    p.stepVar = get (ids::stepVar);
    p.stepHeads = get (ids::stepHeads);
    p.headVol = { get (ids::head1), get (ids::head2), get (ids::head3) };

    p.n1Len1 = (int) get (ids::n1Len1);
    p.n1Len2 = (int) get (ids::n1Len2);
    p.n1Filter = get (ids::n1Filter);
    p.n2Len = { get (ids::n2Len1), get (ids::n2Len2), get (ids::n2Len3) };
    p.n2Rep = { (int) get (ids::n2Rep1), (int) get (ids::n2Rep2), (int) get (ids::n2Rep3) };
    p.n2Vol = { get (ids::n2Vol1), get (ids::n2Vol2), get (ids::n2Vol3) };
    p.n3Depth = { get (ids::n3Depth1), get (ids::n3Depth2) };
    p.n3Rate = { get (ids::n3Rate1), get (ids::n3Rate2) };
    p.n3Time = { get (ids::n3Time1), get (ids::n3Time2) };
    p.n3Fbk = get (ids::n3Fbk);
    p.n3FbkTime = get (ids::n3FbkTime);

    p.on = get (ids::on) > 0.5f;
    p.transcend = get (ids::transcend) > 0.5f;
    p.grit = get (ids::grit) > 0.5f;
    p.gritAmount = get (ids::gritAmt);
    p.boost = get (ids::boost) > 0.5f;
    p.spread = get (ids::spread) > 0.5f;
    p.trails = get (ids::trails) > 0.5f;
    p.latch = get (ids::latch) > 0.5f;
    p.sync = get (ids::sync) > 0.5f;

    // Expression pedal (or any CC mapped to EXP) stands in for one knob, like the pedal's E switch.
    // In Mode 3 the E switch picks the head count, so EXP always drives DIR 1.
    if (get (ids::expOn) > 0.5f)
    {
        const float e = get (ids::exp);
        const int assign = (int) get (ids::expAssign);
        if (p.bank == 0 && p.mode == 2) p.dir1 = e;
        else if (assign == 0) p.dir1 = e;
        else if (assign == 1)
        {
            if (p.mode == 0) p.lenB = juce::jlimit (0, 9, (int) std::round (e * 9.0f));
            else             p.lenS = e;
        }
        else
        {
            if (p.mode == 0) p.fbk = e;
            else             p.rand = e;
        }
    }

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) p.bpm = *bpm;
            p.hostPlaying = pos->getIsPlaying();
            if (auto ppq = pos->getPpqPosition()) p.ppqAtBlockStart = *ppq;
        }
    }
    ui.bpm = p.bpm;
}

//==============================================================================
void BacktrackProcessor::setParam (const juce::String& id, float normalised)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        p->endChangeGesture();
    }
}

void BacktrackProcessor::toggleParam (const juce::String& id)
{
    if (auto* p = apvts.getParameter (id))
        setParam (id, p->getValue() > 0.5f ? 0.0f : 1.0f);
}

void BacktrackProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    const bool drumLayout = (int) get (ids::lpLayout) == 1;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! (m.isNoteOn() || m.isNoteOff()))
            continue;
        const auto pad = launchpad::padFromNote (m.getNoteNumber(), drumLayout);
        if (pad.row < 0)
            continue;
        handlePad (pad.row, pad.col, m.isNoteOn() && m.getVelocity() > 0);
    }
}

void BacktrackProcessor::handlePad (int row, int col, bool down)
{
    using namespace launchpad;
    const int bank = (int) get (ids::bank);
    const int mode = (int) get (ids::mode);

    // Bottom row is momentary, so it cares about release too
    if (row == 7)
    {
        if (col <= 3)
        {
            if (down) engine.softDown(); else engine.softUp();
            return;
        }
        if (! down) return;
        switch (col)
        {
            case 4: engine.stopAndClear(); break;
            case 5: setParam (ids::dir1, 1.0f - get (ids::dir1)); break;
            case 6: engine.toggleHold(); break;
            case 7: toggleParam (ids::on); break;
            default: break;
        }
        return;
    }

    if (! down)
        return;

    switch (row)
    {
        case 0: setParam (ids::dir1, dirPadKnob (col)); break;
        case 1: setParam (ids::dir2, dirPadKnob (col)); break;
        case 2: setParam (ids::dir3, dirPadKnob (col)); break;
        case 3:
            if (bank == 0 && mode == 0)      setParam (ids::lenB, (float) (col + 2) / 9.0f);
            else if (bank == 0 && mode == 1) setParam (ids::lenS, 0.1f + 0.9f * (float) col / 7.0f);
            else if (bank == 1 && mode == 0) setParam (ids::n1Len1, (float) (col + 2) / 9.0f);
            else if (bank == 1 && mode == 1) setParam (ids::n2Len1, 0.1f + 0.9f * (float) col / 7.0f);
            break;
        case 4:
            switch (col)
            {
                case 0: case 1: case 2: setParam (ids::mode, (float) col / 2.0f); break;
                case 3: toggleParam (ids::bank); break;
                case 4: setParam (ids::quant, (float) juce::jmax (0, (int) get (ids::quant) - 1) / 5.0f); break;
                case 5: setParam (ids::quant, (float) juce::jmin (5, (int) get (ids::quant) + 1) / 5.0f); break;
                case 6: toggleParam (ids::transcend); break;
                case 7: toggleParam (ids::sync); break;
                default: break;
            }
            break;
        case 5:
            switch (col)
            {
                case 0: case 1: case 2: setParam (ids::heads, (float) col / 2.0f); break;
                case 3: toggleParam (ids::grit); break;
                case 4: toggleParam (ids::boost); break;
                case 5: toggleParam (ids::spread); break;
                case 6: toggleParam (ids::latch); break;
                case 7: toggleParam (ids::trails); break;
                default: break;
            }
            break;
        default: break;
    }
}

//==============================================================================
void BacktrackProcessor::applyPendingLoop()
{
    loopToRestore = false;
    if (pendingLoopLen <= 0 || pendingLoopMode < 1)
        return;

    const auto* data = static_cast<const int16_t*> (pendingLoop.getData());
    const int srcLen = pendingLoopLen;
    const double ratio = savedLoopSampleRate / currentSampleRate;
    const int dstLen = juce::jmin (engine.getCapacity() - 8, (int) (srcLen / ratio));
    for (int c = 0; c < 2; ++c)
    {
        auto& b = engine.getBufferForRestore (c);
        for (int i = 0; i < dstLen; ++i)
        {
            const double s = i * ratio;
            const int i0 = juce::jmin (srcLen - 1, (int) s);
            const int i1 = juce::jmin (srcLen - 1, i0 + 1);
            const float t = (float) (s - i0);
            const float a = data[(size_t) c * (size_t) srcLen + (size_t) i0] / 32767.0f;
            const float bb = data[(size_t) c * (size_t) srcLen + (size_t) i1] / 32767.0f;
            b[(size_t) i] = a + t * (bb - a);
        }
    }
    engine.restoreLoop (pendingLoopMode, dstLen);
}

void BacktrackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;

    if (loopToRestore.load())
        applyPendingLoop();

    handleMidi (midi);
    midi.clear();

    if (uiDown.exchange (false)) engine.softDown();
    if (uiUp.exchange (false)) engine.softUp();
    if (uiClear.exchange (false)) engine.stopAndClear();
    if (uiToggleHold.exchange (false)) engine.toggleHold();

    readParams();

    const int inCh = getTotalNumInputChannels();
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);
    scratch.copyFrom (0, 0, buffer, 0, 0, n);
    scratch.copyFrom (1, 0, buffer, inCh > 1 ? 1 : 0, 0, n);

    engine.process (scratch.getReadPointer (0), scratch.getReadPointer (1),
                    buffer.getWritePointer (0), buffer.getWritePointer (1), n, params);

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
void BacktrackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    // Save the recorded Mode 2 / Mode 3 loop with the Live set
    const int len = engine.getLoopLength();
    const int mode = (int) get (ids::mode);
    if (len > 0 && (int) get (ids::bank) == 0 && mode >= 1)
    {
        juce::MemoryBlock pcm ((size_t) len * 2 * sizeof (int16_t));
        auto* d = static_cast<int16_t*> (pcm.getData());
        const int base = engine.getLoopBase();
        for (int c = 0; c < 2; ++c)
        {
            const auto& b = engine.getBuffer (c);
            for (int i = 0; i < len; ++i)
                d[(size_t) c * (size_t) len + (size_t) i] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (b[(size_t) (base + i)] * 32767.0f));
        }
        juce::MemoryOutputStream zipped;
        {
            juce::GZIPCompressorOutputStream gz (zipped, 6);
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
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    if (auto* loop = xml->getChildByName ("LOOP"))
    {
        juce::MemoryBlock zipped;
        if (zipped.fromBase64Encoding (loop->getStringAttribute ("data")))
        {
            juce::MemoryInputStream in (zipped, false);
            juce::GZIPDecompressorInputStream gz (in);
            juce::MemoryBlock pcm;
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

    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BacktrackProcessor::createEditor() { return new BacktrackEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BacktrackProcessor(); }
