#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

juce::AudioProcessorValueTreeState::ParameterLayout DialogueProcessor::createLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [](const String& id, const String& name, float def) {
        return std::make_unique<AudioParameterFloat>(
            ParameterID{id, 1}, name, NormalisableRange<float>(0.f, 1.f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return String(juce::roundToInt(v * 100.f)) + " %"; }));
    };
    p.push_back(pct("patience", u8("Patience"), 0.5f));
    p.push_back(pct("interruption", u8("Interruption"), 0.3f));
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{"length", 1}, u8("Length"), NormalisableRange<float>(0.25f, 2.f, 0.01f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return "x" + String(v, 2); })));
    p.push_back(pct("memory", u8("Memory"), 0.5f));
    p.push_back(pct("fidelity", u8("Fidelity"), 0.6f));
    p.push_back(pct("contrast", u8("Contrast"), 0.2f));
    p.push_back(pct("scale", u8("Scale lock"), 0.7f));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"low", 1}, u8("Lowest note"), 0, 127, 36));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"high", 1}, u8("Highest note"), 0, 127, 96));
    p.push_back(pct("sensitivity", u8("Sensitivity"), 0.5f));
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{"gate", 1}, u8("Threshold"), NormalisableRange<float>(-80.f, -20.f, 0.5f), -55.f,
        AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"grid", 1}, u8("Grid"), StringArray{"Free", "1/4", "1/8", "1/16"}, 0));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"channel", 1}, u8("Output channel"), 1, 16, 1));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"freeze", 1}, u8("Freeze memory"), false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"respond", 1}, u8("Answer"), true));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"mode", 1}, u8("Listening mode"), StringArray{"Instrument", "Bright"}, 0));
    p.push_back(pct("random", u8("Randomness"), 0.2f));
    p.push_back(pct("humanize", u8("Humanize"), 0.3f));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"source", 1}, u8("Source"), StringArray{"Audio", "MIDI", "Audio + MIDI"}, 0));
    {
        StringArray ch{u8("All")};
        for (int i = 1; i <= 16; ++i) ch.add(String(i));
        p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{"midiin", 1}, u8("Input channel"), ch, 0));
    }
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"thru", 1}, u8("MIDI thru"), false));
    auto hz = [](const String& id, const String& name, float lo, float hi, float def) {
        NormalisableRange<float> r(lo, hi);
        r.setSkewForCentre(std::sqrt(lo * hi));      // logarithmic
        return std::make_unique<AudioParameterFloat>(
            ParameterID{id, 2}, name, r, def,
            AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
                return v >= 1000.f ? String(v / 1000.f, v >= 10000.f ? 0 : 1) + " kHz" : String(juce::roundToInt(v)) + " Hz";
            }));
    };
    p.push_back(hz("lowcut", u8("Low cut"), 30.f, 1000.f, 55.f));
    p.push_back(hz("highcut", u8("High cut"), 300.f, 12000.f, 2000.f));
    auto pct2 = [](const String& id, const String& name, float def) {
        return std::make_unique<AudioParameterFloat>(
            ParameterID{id, 2}, name, NormalisableRange<float>(0.f, 1.f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return String(juce::roundToInt(v * 100.f)) + " %"; }));
    };
    p.push_back(pct2("dynamics", u8("Dynamics"), 0.7f));
    p.push_back(pct2("sustain", u8("Held notes"), 0.5f));
    p.push_back(pct2("bend", u8("Bends"), 0.f));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"bendrange", 2}, u8("Bend range"), StringArray{u8("±2"), u8("±12"), u8("±24"), u8("±48")}, 0));
    return {p.begin(), p.end()};
}

DialogueProcessor::DialogueProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "LPFDialogue", createLayout()) {
    pPatience = apvts.getRawParameterValue("patience");
    pInterruption = apvts.getRawParameterValue("interruption");
    pLength = apvts.getRawParameterValue("length");
    pMemory = apvts.getRawParameterValue("memory");
    pFidelity = apvts.getRawParameterValue("fidelity");
    pContrast = apvts.getRawParameterValue("contrast");
    pScale = apvts.getRawParameterValue("scale");
    pLow = apvts.getRawParameterValue("low");
    pHigh = apvts.getRawParameterValue("high");
    pSens = apvts.getRawParameterValue("sensitivity");
    pGate = apvts.getRawParameterValue("gate");
    pGrid = apvts.getRawParameterValue("grid");
    pChannel = apvts.getRawParameterValue("channel");
    pFreeze = apvts.getRawParameterValue("freeze");
    pRespond = apvts.getRawParameterValue("respond");
    pMode = apvts.getRawParameterValue("mode");
    pRandom = apvts.getRawParameterValue("random");
    pHumanize = apvts.getRawParameterValue("humanize");
    pSource = apvts.getRawParameterValue("source");
    pMidiIn = apvts.getRawParameterValue("midiin");
    pThru = apvts.getRawParameterValue("thru");
    pLowCut = apvts.getRawParameterValue("lowcut");
    pHighCut = apvts.getRawParameterValue("highcut");
    pDynamics = apvts.getRawParameterValue("dynamics");
    pSustain = apvts.getRawParameterValue("sustain");
    pBend = apvts.getRawParameterValue("bend");
    pBendRange = apvts.getRawParameterValue("bendrange");
}

bool DialogueProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void DialogueProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate);
    engine.setSeed((uint32_t)juce::Time::getMillisecondCounter() | 1u);
    mono.assign((size_t)juce::jmax(samplesPerBlock, 32), 0.f);
    inNotes.clear();
    inNotes.reserve(2048);
    thruBuf.ensureSize(16384);
}

lpfd::Params DialogueProcessor::readParams() const {
    lpfd::Params p;
    p.patience = pPatience->load();
    p.interruption = pInterruption->load();
    p.length = pLength->load();
    p.memory = pMemory->load();
    p.fidelity = pFidelity->load();
    p.contrast = pContrast->load();
    p.scaleLock = pScale->load();
    p.rangeLow = juce::roundToInt(pLow->load());
    p.rangeHigh = juce::roundToInt(pHigh->load());
    p.sensitivity = pSens->load();
    p.gateDb = pGate->load();
    p.grid = juce::roundToInt(pGrid->load());
    p.channel = juce::roundToInt(pChannel->load());
    p.freeze = pFreeze->load() > 0.5f;
    p.respond = pRespond->load() > 0.5f;
    p.bright = pMode->load() > 0.5f;
    p.randomness = pRandom->load();
    p.humanize = pHumanize->load();
    p.source = juce::roundToInt(pSource->load());
    p.midiInChannel = juce::roundToInt(pMidiIn->load());
    p.lowCutHz = pLowCut->load();
    p.highCutHz = pHighCut->load();
    p.dynamics = pDynamics->load();
    p.sustain = pSustain->load();
    p.bend = pBend->load();
    static const int ranges[] = {2, 12, 24, 48};
    p.bendRange = ranges[juce::jlimit(0, 3, juce::roundToInt(pBendRange->load()))];
    return p;
}

namespace {
struct JuceSink : lpfd::MidiSink {
    juce::MidiBuffer& buf;
    int offset = 0;
    explicit JuceSink(juce::MidiBuffer& b) : buf(b) {}
    void noteOn(int o, int ch, int p, int v) override {
        buf.addEvent(juce::MidiMessage::noteOn(ch, p, (juce::uint8)v), offset + o);
    }
    void noteOff(int o, int ch, int p) override {
        buf.addEvent(juce::MidiMessage::noteOff(ch, p), offset + o);
    }
    void pitchBend(int o, int ch, int v) override {
        buf.addEvent(juce::MidiMessage::pitchWheel(ch, v), offset + o);
    }
};
} // namespace

void DialogueProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // take what came in before the buffer is reused for our answers
    inNotes.clear();
    for (const auto m : midi) {
        const auto msg = m.getMessage();
        if ((msg.isNoteOn() || msg.isNoteOff()) && inNotes.size() < inNotes.capacity())
            inNotes.push_back({m.samplePosition, msg.isNoteOn(), msg.getChannel(), msg.getNoteNumber(),
                               (int)msg.getVelocity()});
        else if (msg.isPitchWheel() && inNotes.size() < inNotes.capacity())
            inNotes.push_back({m.samplePosition, false, msg.getChannel(), -1, msg.getPitchWheelValue()});
    }
    const bool thru = pThru->load() > 0.5f;
    thruBuf.clear();
    if (thru) thruBuf.addEvents(midi, 0, -1, 0);
    midi.clear();
    wasBypassed = false;
    engine.setParams(readParams());
    if (clearFlag.exchange(false)) engine.requestClearMemory();
    if (testNoteFlag.exchange(false)) engine.requestTestNote();
    if (calibRequest.exchange(false)) {
        calibSamples = 0;
        calibTotal = (int64_t)(3.0 * getSampleRate());
        calibMax = -120.f;
        calibState.store(1);
    }

    lpfd::Transport tr;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            tr.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm()) tr.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) { tr.ppqAtBlockStart = *ppq; tr.valid = pos->getBpm().hasValue(); }
        }
    }

    const int numCh = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    const int total = buffer.getNumSamples();
    const int chunk = (int)mono.size();
    JuceSink sink(midi);
    for (int start = 0; start < total; start += chunk) {
        const int n = juce::jmin(chunk, total - start);
        if (numCh <= 0) std::fill(mono.begin(), mono.begin() + n, 0.f);
        else {
            const float g = 1.f / (float)numCh;
            const float* c0 = buffer.getReadPointer(0, start);
            for (int i = 0; i < n; ++i) mono[(size_t)i] = c0[i] * g;
            for (int c = 1; c < numCh; ++c) {
                const float* cp = buffer.getReadPointer(c, start);
                for (int i = 0; i < n; ++i) mono[(size_t)i] += cp[i] * g;
            }
        }
        lpfd::Transport t2 = tr;
        if (tr.valid && start > 0) t2.ppqAtBlockStart += start * tr.bpm / (60.0 * getSampleRate());
        sink.offset = start;
        for (const auto& e : inNotes)
            if (e.pos >= start && e.pos < start + n) {
                if (e.pitch < 0) engine.addMidiPitchBend(e.pos - start, e.ch, e.vel);   // pitch wheel
                else engine.addMidiInput(e.pos - start, e.on, e.ch, e.pitch, e.vel);
            }
        engine.process(mono.data(), n, t2, sink);
    }
    if (thru) midi.addEvents(thruBuf, 0, -1, 0);
    // audio passes through untouched

    // silence measurement: loudest short-term level over 3 s (the first 0.2 s ignored)
    if (calibState.load() == 1) {
        const int64_t skip = (int64_t)(0.2 * getSampleRate());
        if (calibSamples >= skip) calibMax = juce::jmax(calibMax, engine.analyzer().levelDb());
        calibSamples += total;
        calibProgress.store((float)juce::jlimit(0.0, 1.0, (double)calibSamples / (double)juce::jmax<int64_t>(1, calibTotal)));
        if (calibSamples >= calibTotal) {
            const float noise = juce::jmax(-90.f, calibMax);
            calibNoiseDb.store(noise);
            calibResultDb.store(juce::jlimit(-80.f, -20.f, std::ceil(noise + 6.f)));
            calibState.store(2);
        }
    }
    {
        const float lv = engine.analyzer().levelDb();
        peakAge += total;
        if (lv > peakHold || peakAge > (int64_t)(2.0 * getSampleRate())) { peakHold = lv; peakAge = 0; }
        uiInputPeakDb.store(peakHold);
    }
    uiPhase.store((int)engine.phase());
    uiWaitProgress.store(engine.waitProgress());
    uiWaitRemaining.store(engine.waitRemainingSeconds());
    uiSilenceWait.store((float)engine.silenceWaitSeconds());
    uiHeardInPhrase.store(engine.heardInPhrase());
    uiEventsNeeded.store(juce::jmin(99, engine.eventsNeeded()));
    uiNotesSent.store(engine.notesSent());
    uiNow.store(engine.now());
    uiSampleRate.store(getSampleRate());
    uiMemory.store(engine.memorySize());
    uiHearing.store(engine.hearing());
    uiAnswering.store(engine.answering());
    uiLevelDb.store(engine.analyzer().levelDb());
    {
        const auto& h = engine.harmony();
        uiKeyRoot.store(h.keyRoot); uiScale.store(h.scale);
        uiChordRoot.store(h.chordRoot); uiChord.store(h.chord);
    }
    uiPitch.store(engine.analyzer().lastConfidence() > 0.8f ? engine.analyzer().lastPitch() : 0.f);
}

void DialogueProcessor::processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer& midi) {
    // bypassed: let the incoming MIDI through, silence our answers once
    if (pThru->load() <= 0.5f) midi.clear();
    if (!wasBypassed) {
        JuceSink sink(midi);
        engine.allNotesOff(0, sink);
        wasBypassed = true;
    }
}

void DialogueProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, destData);
}

void DialogueProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType())) {
            // sessions from before v0.6 have no detection band: give Bright its own
            bool hasBand = false, bright = false;
            for (auto* c : xml->getChildIterator()) {
                if (c->getStringAttribute("id") == "lowcut") hasBand = true;
                if (c->getStringAttribute("id") == "mode") bright = c->getDoubleAttribute("value") > 0.5;
            }
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            if (!hasBand && bright) {
                if (auto* lp = apvts.getParameter("lowcut")) lp->setValueNotifyingHost(lp->convertTo0to1(100.f));
                if (auto* hp = apvts.getParameter("highcut")) hp->setValueNotifyingHost(hp->convertTo0to1(10000.f));
            }
        }
}

juce::AudioProcessorEditor* DialogueProcessor::createEditor() { return new DialogueEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DialogueProcessor(); }
