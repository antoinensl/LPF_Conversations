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
    bool acceptsMidi() const override { return false; }
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    lpfd::Params readParams() const;

    lpfd::Engine engine;
    std::vector<float> mono;
    std::atomic<bool> clearFlag{false};
    bool wasBypassed = false;

    std::atomic<float>* pPatience = nullptr; std::atomic<float>* pInterruption = nullptr;
    std::atomic<float>* pLength = nullptr;   std::atomic<float>* pMemory = nullptr;
    std::atomic<float>* pFidelity = nullptr; std::atomic<float>* pContrast = nullptr;
    std::atomic<float>* pScale = nullptr;    std::atomic<float>* pLow = nullptr;
    std::atomic<float>* pHigh = nullptr;     std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pGate = nullptr;     std::atomic<float>* pGrid = nullptr;
    std::atomic<float>* pChannel = nullptr;  std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pRespond = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogueProcessor)
};
