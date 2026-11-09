#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Ui.h"

class Content;

class DialogueEditor : public juce::AudioProcessorEditor,
                       private juce::Timer,
                       private juce::FocusChangeListener,
                       private juce::ChangeListener {
public:
    static constexpr int kW = 1120, kH = 760;
    explicit DialogueEditor(DialogueProcessor&);
    ~DialogueEditor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void applyScale(float s);

private:
    void timerCallback() override;
    void globalFocusChanged(juce::Component*) override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    DialogueProcessor& proc;
    juce::SharedResourcePointer<ui::DisplaySettings> settings;
    ui::Look look;
    std::unique_ptr<Content> content;
    int tick = 0;
    bool constructing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DialogueEditor)
};
