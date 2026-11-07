#include "BrutalLookAndFeel.h"
#include "BinaryData.h"

namespace brutal {

const Palette& palette() {
    static Palette p;
    return p;
}

juce::String upper(const juce::String& s) {
    juce::String out;
    for (auto p = s.getCharPointer(); !p.isEmpty();) {
        juce::juce_wchar c = p.getAndAdvance();
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || c == 0x153) c = (c == 0x153) ? 0x152 : c - 0x20;
        else c = juce::CharacterFunctions::toUpperCase(c);
        out += juce::String::charToString(c);
    }
    return out;
}

LookAndFeel::LookAndFeel() {
    regular = juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceMonoRegular_ttf,
                                                      BinaryData::SpaceMonoRegular_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceMonoBold_ttf,
                                                   BinaryData::SpaceMonoBold_ttfSize);
    const auto& c = palette();
    setColour(juce::ResizableWindow::backgroundColourId, c.paper);
    setColour(juce::TooltipWindow::backgroundColourId, c.ink);
    setColour(juce::TooltipWindow::textColourId, c.paper);
    setColour(juce::TooltipWindow::outlineColourId, c.ink);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont(const juce::Font& f) {
    return f.isBold() ? bold : regular;
}

juce::Font LookAndFeel::mono(float height, bool b) const {
    return juce::Font(juce::FontOptions(b ? bold : regular).withHeight(height));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int, int, int, int, float, float, float,
                                   juce::Slider::SliderStyle, juce::Slider& s) {
    const auto& c = palette();
    auto r = s.getLocalBounds();
    const bool over = s.isMouseOverOrDragging(), down = s.isMouseButtonDown();
    g.fillAll(over ? c.paperHi : c.paper);

    g.setColour(c.mid);
    g.setFont(mono(10.f, true));
    g.drawText(upper(s.getName()), r.reduced(10, 0).withTop(7).withHeight(12),
               juce::Justification::centredLeft);

    g.setColour(down ? c.signal : c.ink);
    g.setFont(mono(22.f, true));
    g.drawText(upper(s.getTextFromValue(s.getValue())).removeCharacters(" "),
               r.reduced(10, 0).withTop(20).withHeight(28), juce::Justification::centredLeft);

    // the bar: outlined, filled from the left, with a tick every 10 %
    const float prop = (float)s.valueToProportionOfLength(s.getValue());
    auto bar = juce::Rectangle<float>(10.f, (float)r.getBottom() - 18.f, (float)r.getWidth() - 20.f, 8.f);
    g.setColour(c.ink);
    g.drawRect(bar, 1.f);
    g.setColour(down ? c.signal : c.ink);
    g.fillRect(bar.withWidth(bar.getWidth() * juce::jlimit(0.f, 1.f, prop)));
    g.setColour(c.ink);
    for (int i = 1; i < 10; ++i) {
        const float tx = bar.getX() + bar.getWidth() * (float)i / 10.f;
        g.fillRect(tx, bar.getY() - 3.f, 1.f, 3.f);
    }
}

// Toggles and actions are drawn as cells too: small label (component name), big state.
void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const auto& c = palette();
    const bool on = b.getToggleState();
    const bool action = b.getProperties()["action"];
    juce::Colour bg = on ? c.ink : (over ? c.paperHi : c.paper);
    if (action && down) bg = c.signal;
    g.fillAll(bg);
}

void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down) {
    const auto& c = palette();
    const bool on = b.getToggleState();
    const bool action = b.getProperties()["action"];
    auto r = b.getLocalBounds();
    g.setColour(on ? c.paper.withAlpha(0.6f) : c.mid);
    g.setFont(mono(10.f, true));
    g.drawText(upper(b.getName()), r.reduced(10, 0).withTop(7).withHeight(12), juce::Justification::centredLeft);
    g.setColour(on ? c.paper : (action && down ? c.paper : c.ink));
    g.setFont(mono(action ? 15.f : 22.f, true));
    const juce::String text = action ? b.getButtonText() : (on ? juce::String::fromUTF8("■ OUI") : juce::String::fromUTF8("□ NON"));
    g.drawText(text, r.reduced(10, 0).withTop(20).withHeight(28), juce::Justification::centredLeft);
}

void LookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    const auto& c = palette();
    g.fillAll(c.ink);
    g.setColour(c.signal);
    g.fillRect(0, 0, 4, h);
    g.setColour(c.paper);
    g.setFont(mono(12.f));
    g.drawFittedText(text, juce::Rectangle<int>(14, 6, w - 22, h - 12), juce::Justification::centredLeft, 6);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> pos,
                                                   juce::Rectangle<int> parent) {
    const int w = 320;
    juce::AttributedString a;
    a.append(text, mono(12.f));
    juce::TextLayout tl;
    tl.createLayout(a, (float)(w - 22));
    const int h = (int)tl.getHeight() + 16;
    return juce::Rectangle<int>(pos.x + 12, pos.y + 18, w, h).constrainedWithin(parent);
}

// ------------------------------------------------------------------ Segmented
Segmented::Segmented(juce::RangedAudioParameter& p, const juce::String& l, juce::StringArray n)
    : param(p), label(l), names(std::move(n)),
      attachment(p, [this](float v) { index = juce::roundToInt(v); repaint(); }) {
    attachment.sendInitialUpdate();
}

juce::Rectangle<int> Segmented::optionBounds(int i) const {
    auto area = getLocalBounds().reduced(10, 0).withTop(22).withBottom(getHeight() - 10);
    const int n = names.size();
    const int w = area.getWidth() / n;
    auto r = area.withX(area.getX() + i * w).withWidth(i == n - 1 ? area.getRight() - (area.getX() + i * w) : w);
    return r;
}

void Segmented::paint(juce::Graphics& g) {
    const auto& c = palette();
    auto* lnf = dynamic_cast<LookAndFeel*>(&getLookAndFeel());
    g.fillAll(c.paper);
    g.setColour(c.mid);
    if (lnf) g.setFont(lnf->mono(10.f, true));
    g.drawText(upper(label), getLocalBounds().reduced(10, 0).withTop(7).withHeight(12),
               juce::Justification::centredLeft);
    const auto mouse = getMouseXYRelative();
    for (int i = 0; i < names.size(); ++i) {
        auto r = optionBounds(i);
        const bool sel = i == index;
        const bool hover = isMouseOver() && r.contains(mouse) && !sel;
        g.setColour(sel ? c.ink : (hover ? c.paperHi : c.paper));
        g.fillRect(r);
        g.setColour(c.ink);
        g.drawRect(r.toFloat().withWidth(r.getWidth() + (i < names.size() - 1 ? 1.f : 0.f)), 1.f);
        g.setColour(sel ? c.paper : c.ink);
        if (lnf) g.setFont(lnf->mono(13.f, true));
        g.drawFittedText(names[i], r.reduced(3, 0), juce::Justification::centred, 1, 0.6f);
    }
}

void Segmented::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < names.size(); ++i)
        if (optionBounds(i).contains(e.getPosition())) {
            attachment.setValueAsCompleteGesture((float)i);
            return;
        }
}

} // namespace brutal
