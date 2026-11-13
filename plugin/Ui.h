// Conversations — look, fonts, display settings and presets shared by the editor.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui {

juce::String u8(const char* s);
juce::String upper(const juce::String& s);   // also knows French accents

// ------------------------------------------------------------------ themes
struct Palette {
    juce::Colour bg, panel, raised, line, hair, fg, muted, signal, signalText, focus, meterOff;
    float lineW = 2.f;
};
enum class Theme { Papier = 0, Encre = 1, ContrasteMax = 2 };
Palette palette(Theme t);

// ------------------------------------------------------------------ display settings
// Kept for every project (one small file in the user's settings folder).
class DisplaySettings : public juce::ChangeBroadcaster {
public:
    DisplaySettings();
    Theme theme = Theme::Papier;
    float scale = 1.f;            // 1, 1.25, 1.5 (text size), or anything set by resizing
    bool speak = true;            // announce state, scale and chord changes to screen readers
    bool flash = true;            // signal frame while it answers
    bool reduceMotion = false;
    bool shapes = true;           // shapes in addition to colours
    bool welcomeDone = false;
    int host = 0;                 // routing tab
    void save();

private:
    std::unique_ptr<juce::PropertiesFile> file;
};

// ------------------------------------------------------------------ presets
struct Preset {
    const char* name;
    const char* desc;
    float patience, interruption, contrast, random, length01, humanize;   // 0..1 (length01 -> 0.25..2)
    float groove;
};
const std::vector<Preset>& presets();
inline float lengthFrom01(float v) { return 0.25f + 1.75f * v; }
inline float lengthTo01(float l) { return (l - 0.25f) / 1.75f; }

// ------------------------------------------------------------------ look and feel
class Look : public juce::LookAndFeel_V4 {
public:
    Look();
    void setPalette(const Palette& p);
    const Palette& pal() const { return p_; }
    juce::Font body(float h, bool bold = false) const;
    juce::Font mono(float h) const;

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius(juce::Slider&) override { return 10; }
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(15.f); }
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return body(15.f); }
    void drawTooltip(juce::Graphics&, const juce::String&, int, int) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

private:
    Palette p_;
    juce::Typeface::Ptr reg, bold, monoBold;
};

// A slider for the keyboard: arrows 2 %, Shift 0.5 %, Delete = default.
class Slider : public juce::Slider {
public:
    Slider() : juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox) {
        setWantsKeyboardFocus(true);
        setSliderSnapsToMousePosition(false);
        setMouseDragSensitivity(300);
    }
    double defaultValue = 0.0;
    bool keyPressed(const juce::KeyPress& k) override;
};

// A row of radio buttons bound to a choice/int parameter.
class Choice : public juce::Component {
public:
    Choice(juce::RangedAudioParameter& p, const juce::String& title, const juce::StringArray& labels,
           const juce::StringArray& descriptions = {});
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::OwnedArray<juce::TextButton> buttons;
    int labelHeight = 0;           // > 0 draws the title above the buttons
    std::function<void(int)> onPick;

private:
    juce::RangedAudioParameter& param;
    juce::String title;
    juce::ParameterAttachment att;
};

// Simple text drawing helper
void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, const juce::Font& f,
          juce::Colour c, juce::Justification j = juce::Justification::centredLeft, int lines = 1);

// stroke icons (24x24 viewbox, square caps)
juce::Path icon(const juce::String& name);
void drawIcon(juce::Graphics& g, const juce::String& name, juce::Rectangle<float> r, juce::Colour c, float stroke = 2.2f);

} // namespace ui
