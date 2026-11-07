// LPF Dialogue — brutalist skin: paper, ink, one signal colour, a visible grid, monospace type.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace brutal {

struct Palette {
    juce::Colour paper{0xffe8e5de};
    juce::Colour paperHi{0xffdcd8cf};   // hover
    juce::Colour ink{0xff111111};
    juce::Colour mid{0xff8a867d};
    juce::Colour signal{0xffff3b00};    // the only colour
};
const Palette& palette();
// toUpperCase that also knows French accents (é -> É ...)
juce::String upper(const juce::String& s);

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    juce::Font mono(float height, bool bold = false) const;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> pos,
                                          juce::Rectangle<int> parentArea) override;
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

private:
    juce::Typeface::Ptr regular, bold;
};

// A parameter cell: label, big value, a raw bar. Drag left/right, double-click resets.
class Cell : public juce::Slider {
public:
    Cell() : juce::Slider(juce::Slider::LinearBar, juce::Slider::NoTextBox) {
        setSliderSnapsToMousePosition(false);
        setMouseDragSensitivity(220);
    }
};

// Choice parameter as a row of boxes.
class Segmented : public juce::Component, public juce::SettableTooltipClient {
public:
    Segmented(juce::RangedAudioParameter& p, const juce::String& label, juce::StringArray shortNames);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::Rectangle<int> optionBounds(int i) const;
    juce::RangedAudioParameter& param;
    juce::String label;
    juce::StringArray names;
    int index = 0;
    juce::ParameterAttachment attachment;
};

} // namespace brutal
