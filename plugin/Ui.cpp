#include "Ui.h"
#include "BinaryData.h"

namespace ui {

juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

juce::String upper(const juce::String& s) {
    juce::String out;
    for (auto p = s.getCharPointer(); !p.isEmpty();) {
        juce::juce_wchar c = p.getAndAdvance();
        if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 0x20;
        else if (c == 0x153) c = 0x152;
        else c = juce::CharacterFunctions::toUpperCase(c);
        out += juce::String::charToString(c);
    }
    return out;
}

Palette palette(Theme t) {
    Palette p;
    switch (t) {
    case Theme::Encre:
        p = {juce::Colour(0xff111111), juce::Colour(0xff1a1a1a), juce::Colour(0xff262626), juce::Colour(0xffe8e5de),
             juce::Colour(0x38e8e5de), juce::Colour(0xffe8e5de), juce::Colour(0xffa8a49b), juce::Colour(0xffff5a26),
             juce::Colour(0xffff7a4d), juce::Colour(0xff7fb2ff), juce::Colour(0x1fe8e5de), 2.f};
        break;
    case Theme::ContrasteMax:
        p = {juce::Colour(0xff000000), juce::Colour(0xff000000), juce::Colour(0xff1f1f1f), juce::Colour(0xffffffff),
             juce::Colour(0x80ffffff), juce::Colour(0xffffffff), juce::Colour(0xffd6d6d6), juce::Colour(0xffff6a00),
             juce::Colour(0xffff8a3d), juce::Colour(0xff00e5ff), juce::Colour(0x33ffffff), 3.f};
        break;
    case Theme::Papier:
    default:
        p = {juce::Colour(0xffe8e5de), juce::Colour(0xfff2f0ea), juce::Colour(0xffdcd8cf), juce::Colour(0xff111111),
             juce::Colour(0x38111111), juce::Colour(0xff111111), juce::Colour(0xff57534c), juce::Colour(0xffff3b00),
             juce::Colour(0xffb02a00), juce::Colour(0xff0040e0), juce::Colour(0x1a111111), 2.f};
        break;
    }
    return p;
}

// ------------------------------------------------------------------ settings
DisplaySettings::DisplaySettings() {
    juce::PropertiesFile::Options o;
    o.applicationName = "LPF Dialogue";
    o.filenameSuffix = ".settings";
    o.folderName = "LifePassFilter";
    o.osxLibrarySubFolder = "Application Support";
    file = std::make_unique<juce::PropertiesFile>(o);
    theme = (Theme)juce::jlimit(0, 2, file->getIntValue("theme", 0));
    scale = (float)juce::jlimit(0.8, 2.0, file->getDoubleValue("scale", 1.0));
    speak = file->getBoolValue("speak", true);
    flash = file->getBoolValue("flash", true);
    reduceMotion = file->getBoolValue("reduceMotion", false);
    shapes = file->getBoolValue("shapes", true);
    welcomeDone = file->getBoolValue("welcomeDone", false);
    host = juce::jlimit(0, 2, file->getIntValue("host", 0));
}

void DisplaySettings::save() {
    file->setValue("theme", (int)theme);
    file->setValue("scale", (double)scale);
    file->setValue("speak", speak);
    file->setValue("flash", flash);
    file->setValue("reduceMotion", reduceMotion);
    file->setValue("shapes", shapes);
    file->setValue("welcomeDone", welcomeDone);
    file->setValue("host", host);
    file->saveIfNeeded();
    sendChangeMessage();
}

// ------------------------------------------------------------------ presets
const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = {
        {"Echo", "Answers every note, right away.", 0.f, 1.f, 0.1f, 0.f, 0.4f, 0.2f},
        {"Ping-pong", "Taking turns, phrase after phrase.", 0.4f, 0.f, 0.2f, 0.1f, 0.5f, 0.3f},
        {"Partner", "Listens, picks up, invents a little.", 0.5f, 0.3f, 0.3f, 0.35f, 0.5f, 0.5f},
        {"Contrarian", "Takes the opposite of what you play.", 0.45f, 0.4f, 0.85f, 0.3f, 0.5f, 0.4f},
        {"Quotes", "Only plays back your own phrases.", 0.6f, 0.f, 0.f, 0.f, 0.7f, 0.2f},
        {"Bird", "Short, lively answers. Goes well with Bright listening.", 0.2f, 0.6f, 0.3f, 0.4f, 0.3f, 0.6f},
    };
    return p;
}

// ------------------------------------------------------------------ look
Look::Look() {
    reg = juce::Typeface::createSystemTypefaceFor(BinaryData::AtkinsonHyperlegibleRegular_ttf,
                                                  BinaryData::AtkinsonHyperlegibleRegular_ttfSize);
    bold = juce::Typeface::createSystemTypefaceFor(BinaryData::AtkinsonHyperlegibleBold_ttf,
                                                   BinaryData::AtkinsonHyperlegibleBold_ttfSize);
    monoBold = juce::Typeface::createSystemTypefaceFor(BinaryData::SpaceMonoBold_ttf, BinaryData::SpaceMonoBold_ttfSize);
    setPalette(palette(Theme::Papier));
}

void Look::setPalette(const Palette& p) {
    p_ = p;
    setColour(juce::ResizableWindow::backgroundColourId, p.bg);
    setColour(juce::Label::textColourId, p.fg);
    setColour(juce::ToggleButton::textColourId, p.fg);
    setColour(juce::ComboBox::textColourId, p.fg);
    setColour(juce::ComboBox::backgroundColourId, p.bg);
    setColour(juce::ComboBox::outlineColourId, p.line);
    setColour(juce::ComboBox::arrowColourId, p.fg);
    setColour(juce::PopupMenu::backgroundColourId, p.panel);
    setColour(juce::PopupMenu::textColourId, p.fg);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.fg);
    setColour(juce::PopupMenu::highlightedTextColourId, p.bg);
    setColour(juce::TooltipWindow::backgroundColourId, p.fg);
    setColour(juce::TooltipWindow::textColourId, p.bg);
    setColour(juce::ScrollBar::thumbColourId, p.muted);
    setColour(juce::TextButton::textColourOffId, p.fg);
    setColour(juce::TextButton::textColourOnId, p.bg);
}

juce::Typeface::Ptr Look::getTypefaceForFont(const juce::Font& f) { return f.isBold() ? bold : reg; }
juce::Font Look::body(float h, bool b) const { return juce::Font(juce::FontOptions(b ? bold : reg).withHeight(h)); }
juce::Font Look::mono(float h) const { return juce::Font(juce::FontOptions(monoBold).withHeight(h)); }

void Look::drawLinearSlider(juce::Graphics& g, int, int, int, int, float, float, float,
                            juce::Slider::SliderStyle, juce::Slider& s) {
    auto r = s.getLocalBounds().toFloat();
    const float th = 10.f;
    auto track = juce::Rectangle<float>(r.getX() + 9.f, r.getCentreY() - th / 2.f, r.getWidth() - 18.f, th);
    const float prop = (float)s.valueToProportionOfLength(s.getValue());
    g.setColour(p_.bg);
    g.fillRect(track);
    g.setColour(s.isEnabled() ? p_.fg : p_.muted);
    g.fillRect(track.withWidth(track.getWidth() * juce::jlimit(0.f, 1.f, prop)));
    g.setColour(p_.line);
    g.drawRect(track, p_.lineW);
    const float x = track.getX() + track.getWidth() * juce::jlimit(0.f, 1.f, prop);
    auto thumb = juce::Rectangle<float>(18.f, 18.f).withCentre({x, track.getCentreY()});
    g.setColour(s.isMouseButtonDown() ? p_.signal : p_.fg);
    g.fillRect(thumb);
    g.setColour(p_.bg);
    g.drawRect(thumb.reduced(2.f), 2.f);
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const bool accent = b.getProperties()["accent"];
    const bool on = b.getToggleState();
    auto r = b.getLocalBounds().toFloat();
    juce::Colour bg = on ? p_.fg : (over ? p_.raised : p_.bg);
    if (accent) bg = down ? p_.signal.darker(0.2f) : p_.signal;
    if (b.getProperties()["flat"]) bg = over ? p_.raised : juce::Colours::transparentBlack;
    g.setColour(bg);
    g.fillRect(r);
    if (!b.getProperties()["flat"]) {
        g.setColour(p_.line);
        g.drawRect(r, p_.lineW);
    }
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    const bool accent = b.getProperties()["accent"];
    const bool on = b.getToggleState();
    const juce::Colour c = accent ? juce::Colour(0xff111111) : (on ? p_.bg : p_.fg);
    auto r = b.getLocalBounds().reduced(8, 2);
    const juce::String iconName = b.getProperties()["icon"].toString();
    const juce::String desc = b.getProperties()["desc"].toString();
    const float fs = (float)b.getProperties().getWithDefault("fontSize", 15.0);
    if (iconName.isNotEmpty()) {
        const bool right = b.getProperties()["iconRight"];
        auto ir = right ? r.removeFromRight(20) : r.removeFromLeft(20);
        drawIcon(g, iconName, ir.toFloat().withSizeKeepingCentre(18.f, 18.f), c);
        if (right) r.removeFromRight(6); else r.removeFromLeft(6);
    }
    const auto just = b.getProperties()["left"] ? juce::Justification::centredLeft : juce::Justification::centred;
    if (desc.isNotEmpty()) {
        auto top = r.removeFromTop(r.getHeight() / 2);
        text(g, b.getButtonText(), top, body(fs, true), c, juce::Justification::bottomLeft);
        text(g, desc, r, body(13.f), on ? p_.bg : (accent ? c : p_.muted), juce::Justification::topLeft);
    } else {
        g.setColour(c);
        g.setFont(body(fs, true));
        g.drawFittedText(b.getButtonText(), r, just, 2, 0.8f);
    }
}

void Look::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) {
    auto r = b.getLocalBounds();
    auto box = r.removeFromLeft(24).withSizeKeepingCentre(22, 22).toFloat();
    if (b.getProperties()["topAlign"]) box.setY(2.f);
    g.setColour(over ? p_.raised : p_.bg);
    g.fillRect(box);
    g.setColour(p_.line);
    g.drawRect(box, p_.lineW);
    if (b.getToggleState()) {
        g.setColour(p_.fg);
        g.fillRect(box.reduced(4.f));
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 5.5f, box.getCentreY());
        tick.lineTo(box.getX() + 9.5f, box.getBottom() - 6.f);
        tick.lineTo(box.getRight() - 5.f, box.getY() + 6.f);
        g.setColour(p_.bg);
        g.strokePath(tick, juce::PathStrokeType(2.4f));
    }
    r.removeFromLeft(10);
    const juce::String desc = b.getProperties()["desc"].toString();
    if (desc.isEmpty()) {
        text(g, b.getButtonText(), r, body(15.f, true), p_.fg);
    } else {
        text(g, b.getButtonText(), r.removeFromTop(22), body(15.f, true), p_.fg);
        text(g, desc, r, body(13.f), p_.muted, juce::Justification::topLeft, 3);
    }
}

void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) {
    auto r = juce::Rectangle<float>(0.f, 0.f, (float)w, (float)h);
    g.setColour(cb.isMouseOver(true) ? p_.raised : p_.bg);
    g.fillRect(r);
    g.setColour(p_.line);
    g.drawRect(r, p_.lineW);
    juce::Path a;
    const float cx = (float)w - 16.f, cy = (float)h / 2.f;
    a.startNewSubPath(cx - 5.f, cy - 2.5f);
    a.lineTo(cx, cy + 2.5f);
    a.lineTo(cx + 5.f, cy - 2.5f);
    g.setColour(p_.fg);
    g.strokePath(a, juce::PathStrokeType(2.2f));
}

void Look::positionComboBoxText(juce::ComboBox& cb, juce::Label& l) {
    l.setBounds(4, 1, cb.getWidth() - 30, cb.getHeight() - 2);
    l.setFont(getComboBoxFont(cb));
}

void Look::drawTooltip(juce::Graphics& g, const juce::String& t, int w, int h) {
    g.fillAll(p_.fg);
    g.setColour(p_.signal);
    g.fillRect(0, 0, 4, h);
    g.setColour(p_.bg);
    g.setFont(body(14.f));
    g.drawFittedText(t, juce::Rectangle<int>(14, 6, w - 22, h - 12), juce::Justification::centredLeft, 8);
}

juce::Rectangle<int> Look::getTooltipBounds(const juce::String& t, juce::Point<int> pos, juce::Rectangle<int> parent) {
    const int w = 320;
    juce::AttributedString a;
    a.append(t, body(14.f));
    juce::TextLayout tl;
    tl.createLayout(a, (float)(w - 22));
    return juce::Rectangle<int>(pos.x + 12, pos.y + 18, w, (int)tl.getHeight() + 16).constrainedWithin(parent);
}

// ------------------------------------------------------------------ slider
bool Slider::keyPressed(const juce::KeyPress& k) {
    const auto range = getRange();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) {
        setValue(defaultValue, juce::sendNotificationSync);
        return true;
    }
    const int code = k.getKeyCode();
    int dir = 0;
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey) dir = 1;
    else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey) dir = -1;
    else if (code == juce::KeyPress::homeKey) { setValue(range.getStart(), juce::sendNotificationSync); return true; }
    else if (code == juce::KeyPress::endKey) { setValue(range.getEnd(), juce::sendNotificationSync); return true; }
    if (dir == 0) return juce::Slider::keyPressed(k);
    const double step = range.getLength() * (k.getModifiers().isShiftDown() ? 0.005 : 0.02);
    double v = getValue() + dir * juce::jmax(step, getInterval());
    setValue(juce::jlimit(range.getStart(), range.getEnd(), v), juce::sendNotificationSync);
    return true;
}

// ------------------------------------------------------------------ choice
Choice::Choice(juce::RangedAudioParameter& p, const juce::String& t, const juce::StringArray& labels,
               const juce::StringArray& descs)
    : param(p), title(t),
      att(p, [this](float v) {
          const int idx = juce::roundToInt(v - param.getNormalisableRange().start);
          for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(i == idx, juce::dontSendNotification);
      }) {
    setTitle(t);
    setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
    static int group = 4100;
    ++group;
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = buttons.add(new juce::TextButton(labels[i]));
        b->setRadioGroupId(group);
        b->setClickingTogglesState(false);
        b->setTitle(labels[i]);
        if (i < descs.size()) { b->getProperties().set("desc", descs[i]); b->getProperties().set("left", true); }
        b->setDescription(t);
        b->onClick = [this, i] {
            att.setValueAsCompleteGesture(param.getNormalisableRange().start + (float)i);
            if (onPick) onPick(i);
        };
        addAndMakeVisible(b);
    }
    att.sendInitialUpdate();
}

void Choice::paint(juce::Graphics& g) {
    if (labelHeight > 0) {
        auto* look = dynamic_cast<Look*>(&getLookAndFeel());
        if (look) text(g, title, getLocalBounds().removeFromTop(labelHeight), look->body(15.f, true), look->pal().fg);
    }
}

void Choice::resized() {
    auto r = getLocalBounds();
    r.removeFromTop(labelHeight);
    const int n = buttons.size();
    if (n == 0) return;
    const int w = (r.getWidth() + (n - 1) * 2) / n;
    for (int i = 0; i < n; ++i) {
        const int x = r.getX() + i * (w - 2);
        buttons[i]->setBounds(x, r.getY(), i == n - 1 ? r.getRight() - x : w, r.getHeight());
    }
}

void text(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, const juce::Font& f, juce::Colour c,
          juce::Justification j, int lines) {
    g.setColour(c);
    g.setFont(f);
    if (lines <= 1) g.drawFittedText(s, r, j, 1, 0.85f);
    else g.drawFittedText(s, r, j, lines, 0.95f);
}

// ------------------------------------------------------------------ icons
juce::Path icon(const juce::String& n) {
    juce::Path p;
    auto line = [&](float x0, float y0, float x1, float y1) { p.startNewSubPath(x0, y0); p.lineTo(x1, y1); };
    if (n == "listen") { line(3, 12, 6, 12); line(8, 7, 8, 17); line(12, 4, 12, 20); line(16, 8, 16, 16); line(20, 11, 20, 13); }
    else if (n == "wait") { p.addEllipse(3, 3, 18, 18); line(12, 7, 12, 12); p.lineTo(15, 15); }
    else if (n == "answer") { p.startNewSubPath(4, 5); p.lineTo(20, 5); p.lineTo(20, 16); p.lineTo(9, 16); p.lineTo(4, 20); p.closeSubPath(); }
    else if (n == "silence") { p.startNewSubPath(5, 9); p.lineTo(9, 9); p.lineTo(14, 5); p.lineTo(14, 19); p.lineTo(9, 15); p.lineTo(5, 15); p.closeSubPath(); line(17, 9, 21, 15); line(21, 9, 17, 15); }
    else if (n == "help") { p.addEllipse(3, 3, 18, 18); p.startNewSubPath(9.5f, 9.5f); p.cubicTo(9.5f, 6.f, 14.5f, 6.f, 14.5f, 9.5f); p.cubicTo(14.5f, 11.5f, 12.f, 11.5f, 12.f, 14.f); line(12, 17, 12, 17.5f); }
    else if (n == "display") { line(4, 7, 14, 7); line(18, 7, 20, 7); line(4, 17, 8, 17); line(12, 17, 20, 17); line(16, 5, 16, 9); line(10, 15, 10, 19); }
    else if (n == "right") { line(5, 12, 19, 12); p.startNewSubPath(13, 6); p.lineTo(19, 12); p.lineTo(13, 18); }
    else if (n == "left") { line(19, 12, 5, 12); p.startNewSubPath(11, 6); p.lineTo(5, 12); p.lineTo(11, 18); }
    else if (n == "close") { line(6, 6, 18, 18); line(18, 6, 6, 18); }
    else if (n == "check") { p.startNewSubPath(5, 12); p.lineTo(10, 17); p.lineTo(19, 7); }
    else if (n == "warn") { line(12, 6, 12, 14); line(12, 18, 12, 18.5f); }
    else if (n == "play") { p.startNewSubPath(7, 4); p.lineTo(20, 12); p.lineTo(7, 20); p.closeSubPath(); }
    else if (n == "guitar") { line(14, 4, 20, 10); line(17, 7, 8, 16); p.addEllipse(4, 13, 7, 7); }
    else if (n == "mic") { p.addRoundedRectangle(9, 3, 6, 12, 3); p.startNewSubPath(5, 11); p.cubicTo(5, 20, 19, 20, 19, 11); line(12, 18, 12, 21); }
    else if (n == "keys") { p.addRectangle(3, 6, 18, 12); line(8, 6, 8, 13); line(12, 6, 12, 13); line(16, 6, 16, 13); }
    else if (n == "both") { p.addRectangle(4, 8, 7, 8); p.addRectangle(13, 8, 7, 8); }
    else if (n == "wave") { line(3, 12, 6, 12); line(8, 8, 8, 16); line(12, 5, 12, 19); line(16, 9, 16, 15); line(20, 12, 21, 12); }
    return p;
}

void drawIcon(juce::Graphics& g, const juce::String& name, juce::Rectangle<float> r, juce::Colour c, float stroke) {
    auto p = icon(name);
    p.applyTransform(juce::AffineTransform::scale(r.getWidth() / 24.f, r.getHeight() / 24.f).translated(r.getX(), r.getY()));
    g.setColour(c);
    if (name == "play") g.fillPath(p);
    else g.strokePath(p, juce::PathStrokeType(stroke * r.getWidth() / 24.f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
}

} // namespace ui
