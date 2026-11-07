#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "BrutalLookAndFeel.h"
#include "PluginProcessor.h"

class DialogueEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit DialogueEditor(DialogueProcessor&);
    ~DialogueEditor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    struct ParamCell {
        brutal::Cell cell;
        std::unique_ptr<SliderAtt> att;
    };
    struct Toggle {
        juce::TextButton button;
        std::unique_ptr<ButtonAtt> att;
    };
    void addCell(ParamCell& c, const juce::String& id, const juce::String& label, const juce::String& tip);
    void addToggle(Toggle& t, const juce::String& id, const juce::String& label, const juce::String& tip);
    void timerCallback() override;

    DialogueProcessor& proc;
    brutal::LookAndFeel lnf;

    ParamCell patience, interruption, length, humanize;
    ParamCell memory, fidelity, contrast, scale, random;
    ParamCell sens, gate, low, high;
    ParamCell midiIn, channel;
    std::unique_ptr<brutal::Segmented> mode, source, grid;
    Toggle freeze, respond, thru;
    juce::TextButton clear;
    juce::TooltipWindow tooltips{this, 500};

    // layout (computed in resized, used by paint)
    juce::Rectangle<int> headerR, meterR, keyR, chordR;
    struct RowHead { juce::Rectangle<int> r; juce::String num, name; };
    std::vector<RowHead> rowHeads;
    std::vector<juce::Rectangle<int>> grid_;

    bool hearing = false, answering = false;
    int memSize = 0;
    float level = -120.f, pitch = 0.f;
    int keyRoot = -1, scaleType = 0, chordRoot = -1, chordType = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogueEditor)
};
