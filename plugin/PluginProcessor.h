#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "../core/engine.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

class DialogueProcessor : public juce::AudioProcessor {
public:
    DialogueProcessor();
    ~DialogueProcessor() override = default;

    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI <-> audio
    void requestClear() { clearFlag.store(true); }
    std::atomic<int> uiMemory{0};
    std::atomic<bool> uiHearing{false}, uiAnswering{false};
    std::atomic<float> uiLevelDb{-120.f}, uiPitch{0.f};
    std::atomic<int> uiKeyRoot{-1}, uiScale{0}, uiChordRoot{-1}, uiChord{0};
    std::atomic<int> uiPhase{0}, uiHeardInPhrase{0}, uiEventsNeeded{1};
    std::atomic<float> uiWaitProgress{0.f}, uiWaitRemaining{0.f}, uiSilenceWait{1.f};
    std::atomic<uint64_t> uiNotesSent{0};
    std::atomic<int64_t> uiNow{0};
    std::atomic<double> uiSampleRate{44100.0};
    std::atomic<float> uiInputPeakDb{-120.f};   // loudest level in the last ~2 s

    // the conversation, for the display (read on the message thread)
    const lpfd::Timeline& timeline() const { return engine.timeline(); }

    // "Measure silence": listens 3 s, then proposes a gate just above the room noise
    void requestCalibration() { calibRequest.store(true); }
    std::atomic<int> calibState{0};             // 0 idle, 1 measuring, 2 done (result in calibResultDb)
    std::atomic<float> calibProgress{0.f}, calibNoiseDb{-120.f}, calibResultDb{-55.f};

    void requestTestNote() { testNoteFlag.store(true); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    lpfd::Params readParams() const;

    lpfd::Engine engine;
    std::vector<float> mono;
    std::atomic<bool> clearFlag{false}, calibRequest{false}, testNoteFlag{false};
    int64_t calibSamples = 0, calibTotal = 0;
    float calibMax = -120.f;
    float peakHold = -120.f;
    int64_t peakAge = 0;
    bool wasBypassed = false;

    std::atomic<float>* pPatience = nullptr; std::atomic<float>* pInterruption = nullptr;
    std::atomic<float>* pLength = nullptr;   std::atomic<float>* pMemory = nullptr;
    std::atomic<float>* pFidelity = nullptr; std::atomic<float>* pContrast = nullptr;
    std::atomic<float>* pScale = nullptr;    std::atomic<float>* pLow = nullptr;
    std::atomic<float>* pHigh = nullptr;     std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pGate = nullptr;     std::atomic<float>* pGrid = nullptr;
    std::atomic<float>* pChannel = nullptr;  std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pRespond = nullptr;  std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pRandom = nullptr;   std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pSource = nullptr;   std::atomic<float>* pMidiIn = nullptr;
    std::atomic<float>* pThru = nullptr;
    std::atomic<float>* pLowCut = nullptr;   std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pDynamics = nullptr; std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pBend = nullptr;     std::atomic<float>* pBendRange = nullptr;
    struct InNote { int pos; bool on; int ch, pitch, vel; };
    std::vector<InNote> inNotes;   // preallocated
    juce::MidiBuffer thruBuf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogueProcessor)
};
