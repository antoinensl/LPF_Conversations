#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class DialogueEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DialogueEditor(DialogueProcessor&);
    ~DialogueEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    void addKnob(Knob& k, const juce::String& id, const juce::String& text, const juce::String& tip);
    void timerCallback() override;

    DialogueProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    Knob patience, interruption, length, humanize, memory, fidelity, contrast, scale, random, sens, gate, low, high, channel;
    juce::ComboBox grid, mode, source, midiIn;
    juce::Label gridLabel, modeLabel, sourceLabel, midiInLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAtt, modeAtt, sourceAtt, midiInAtt;
    juce::ToggleButton freeze, respond, thru;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAtt, respondAtt, thruAtt;
    juce::TextButton clear;
    juce::TooltipWindow tooltips{this, 400};

    bool hearing = false, answering = false;
    int memSize = 0;
    float level = -120.f, pitch = 0.f;
    int keyRoot = -1, scaleType = 0, chordRoot = -1, chordType = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogueEditor)
};
