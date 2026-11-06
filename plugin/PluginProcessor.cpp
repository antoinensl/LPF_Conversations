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
        ParameterID{"length", 1}, u8("Longueur"), NormalisableRange<float>(0.25f, 2.f, 0.01f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return "x" + String(v, 2); })));
    p.push_back(pct("memory", u8("Mémoire"), 0.5f));
    p.push_back(pct("fidelity", u8("Fidélité"), 0.6f));
    p.push_back(pct("contrast", u8("Contraste"), 0.2f));
    p.push_back(pct("scale", u8("Tonalité"), 0.7f));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"low", 1}, u8("Note basse"), 0, 127, 36));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"high", 1}, u8("Note haute"), 0, 127, 96));
    p.push_back(pct("sensitivity", u8("Sensibilité"), 0.5f));
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{"gate", 1}, u8("Seuil"), NormalisableRange<float>(-80.f, -20.f, 0.5f), -55.f,
        AudioParameterFloatAttributes().withLabel("dB")));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"grid", 1}, u8("Grille"), StringArray{u8("Libre"), "1/4", "1/8", "1/16"}, 0));
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{"channel", 1}, u8("Canal MIDI"), 1, 16, 1));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"freeze", 1}, u8("Figer la mémoire"), false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"respond", 1}, u8("Répondre"), true));
    p.push_back(std::make_unique<AudioParameterChoice>(
        ParameterID{"mode", 1}, u8("Écoute"), StringArray{"Instrument", "Bright"}, 0));
    p.push_back(pct("random", u8("Hasard"), 0.2f));
    p.push_back(pct("humanize", u8("Humanisation"), 0.3f));
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
};
} // namespace

void DialogueProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    wasBypassed = false;
    engine.setParams(readParams());
    if (clearFlag.exchange(false)) engine.requestClearMemory();

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
        engine.process(mono.data(), n, t2, sink);
    }
    // audio passes through untouched

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
    midi.clear();
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
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* DialogueProcessor::createEditor() { return new DialogueEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DialogueProcessor(); }
