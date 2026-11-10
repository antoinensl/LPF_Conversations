// LPF Dialogue — editor: a conversation you can read, settings in plain words.
#include "PluginEditor.h"

using ui::u8;
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

namespace {

constexpr int W = DialogueEditor::kW, H = DialogueEditor::kH;
constexpr int kHeaderH = 64, kFooterH = 32;

juce::String fmtS(float s) { return juce::String(s, 1) + " s"; }

juce::String noteName(int m) {
    static const char* n[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return u8(n[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1);
}

juce::String keyText(int root, int scale) {
    if (root < 0) return "---";
    return u8(lpfd::Harmony::pcName(root)) + " " + u8(lpfd::Harmony::scaleName(scale));
}

juce::String chordText(int root, int chord) {
    if (root < 0) return "---";
    return juce::String(lpfd::Harmony::pcLetter(root)) + lpfd::Harmony::chordSuffix(chord);
}

void setParam(DialogueProcessor& p, const char* id, float value) {
    if (auto* prm = p.apvts.getParameter(id)) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(prm->convertTo0to1(value));
        prm->endChangeGesture();
    }
}
float getParam(DialogueProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }

ui::Look& lookOf(juce::Component& c) { return static_cast<ui::Look&>(c.getLookAndFeel()); }

// A framed panel.
void panel(juce::Graphics& g, const ui::Palette& p, juce::Rectangle<int> r) {
    g.setColour(p.panel);
    g.fillRect(r);
    g.setColour(p.line);
    g.drawRect(r.toFloat(), p.lineW);
}

void styleButton(juce::TextButton& b, const juce::String& icon = {}, bool accent = false, float fontSize = 15.f) {
    if (icon.isNotEmpty()) b.getProperties().set("icon", icon);
    if (accent) b.getProperties().set("accent", true);
    b.getProperties().set("fontSize", fontSize);
    b.setWantsKeyboardFocus(true);
}

// ------------------------------------------------------------------ a question-slider
struct QSlider : juce::Component {
    juce::String question, low, high;
    std::function<juce::String(double)> sentence;
    ui::Slider s;
    std::unique_ptr<SliderAtt> att;

    QSlider(DialogueProcessor& p, const char* id, const juce::String& q, const juce::String& lo, const juce::String& hi,
            std::function<juce::String(double)> sent)
        : question(q), low(lo), high(hi), sentence(std::move(sent)) {
        addAndMakeVisible(s);
        att = std::make_unique<SliderAtt>(p.apvts, id, s);
        if (auto* prm = p.apvts.getParameter(id)) s.defaultValue = prm->convertFrom0to1(prm->getDefaultValue());
        s.setTitle(q);
        s.setDescription(lo + u8(" ↔ ") + hi);
        s.textFromValueFunction = [this](double v) { return sentence ? sentence(v) : juce::String(v); };
        s.onValueChange = [this] { repaint(); };
        setTitle(q);
    }
    void resized() override {
        auto r = getLocalBounds();
        r.removeFromTop(20);
        auto row = r.removeFromTop(28);
        row.removeFromLeft(86);
        row.removeFromRight(86);
        s.setBounds(row);
    }
    void paint(juce::Graphics& g) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        auto r = getLocalBounds();
        ui::text(g, question, r.removeFromTop(20), L.body(15.f, true), p.fg);
        auto row = r.removeFromTop(28);
        ui::text(g, low, row.removeFromLeft(82), L.body(13.f), p.muted);
        ui::text(g, high, row.removeFromRight(82), L.body(13.f), p.muted, juce::Justification::centredRight);
        ui::text(g, sentence ? sentence(s.getValue()) : juce::String(), r.removeFromTop(20), L.body(13.5f), p.fg);
        g.setColour(p.hair);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
};

// ------------------------------------------------------------------ detection band
// Two handles on a logarithmic frequency axis: the lowest and highest pitch it listens for.
// Each handle is a real (invisible) slider: Tab reaches it, arrows move it, a screen reader
// reads it. The mouse is handled here so that both can be dragged on one track.
static juce::String hzText(double v) {
    return v >= 1000.0 ? juce::String(v / 1000.0, v >= 10000.0 ? 0 : 1) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
}

struct FreqRange : juce::Component, private juce::Timer {
    static constexpr double fMin = 30.0, fMax = 12000.0;
    DialogueProcessor& proc;
    ui::Slider lo, hi;
    std::unique_ptr<SliderAtt> loAtt, hiAtt;
    ui::Slider* dragging = nullptr;
    float marker = 0.f;

    explicit FreqRange(DialogueProcessor& p) : proc(p) {
        setTitle(u8("Pitches it listens for"));
        setFocusContainerType(FocusContainerType::focusContainer);
        auto setup = [this](ui::Slider& s, const char* id, const juce::String& title, const juce::String& desc) {
            addAndMakeVisible(s);
            s.setAlpha(0.f);
            s.setInterceptsMouseClicks(false, false);
            s.setTitle(title);
            s.setDescription(desc);
            if (auto* prm = proc.apvts.getParameter(id)) s.defaultValue = prm->convertFrom0to1(prm->getDefaultValue());
            s.textFromValueFunction = [](double v) { return hzText(v); };
            s.onValueChange = [this] { resized(); repaint(); };
        };
        setup(lo, "lowcut", u8("Low cut"), u8("Lowest pitch it listens for. Below it, sounds are filtered out: rumble, wind, handling noise."));
        setup(hi, "highcut", u8("High cut"), u8("Highest pitch it listens for."));
        loAtt = std::make_unique<SliderAtt>(proc.apvts, "lowcut", lo);
        hiAtt = std::make_unique<SliderAtt>(proc.apvts, "highcut", hi);
        startTimerHz(15);
    }

    juce::Rectangle<int> track() const { return getLocalBounds().withTrimmedTop(26).withHeight(30).reduced(10, 0); }
    float xOf(double hz) const {
        const auto t = track();
        return (float)t.getX() + (float)t.getWidth() * (float)(std::log(juce::jlimit(fMin, fMax, hz) / fMin) / std::log(fMax / fMin));
    }
    double hzOf(float x) const {
        const auto t = track();
        const double u = juce::jlimit(0.0, 1.0, (double)(x - (float)t.getX()) / juce::jmax(1, t.getWidth()));
        return fMin * std::pow(fMax / fMin, u);
    }
    void resized() override {
        const auto t = track();
        lo.setBounds(juce::roundToInt(xOf(lo.getValue())) - 9, t.getY(), 18, t.getHeight());
        hi.setBounds(juce::roundToInt(xOf(hi.getValue())) - 9, t.getY(), 18, t.getHeight());
    }
    void timerCallback() override {
        if (!isShowing()) return;
        const float m = proc.uiHearing.load() ? proc.uiPitch.load() : 0.f;
        if (std::abs(m - marker) > 0.05f) { marker = m; repaint(); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const float x = (float)e.x;
        dragging = std::abs(x - xOf(lo.getValue())) <= std::abs(x - xOf(hi.getValue())) ? &lo : &hi;
        if (auto* prm = proc.apvts.getParameter(dragging == &lo ? "lowcut" : "highcut")) prm->beginChangeGesture();
        mouseDrag(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!dragging) return;
        double v = hzOf((float)e.x);
        if (dragging == &lo) v = juce::jmin(v, hi.getValue() / 1.5);
        else v = juce::jmax(v, lo.getValue() * 1.5);
        dragging->setValue(v, juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (dragging)
            if (auto* prm = proc.apvts.getParameter(dragging == &lo ? "lowcut" : "highcut")) prm->endChangeGesture();
        dragging = nullptr;
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override {
        auto& s = std::abs((float)e.x - xOf(lo.getValue())) <= std::abs((float)e.x - xOf(hi.getValue())) ? lo : hi;
        s.setValue(s.defaultValue, juce::sendNotificationSync);
    }

    void paint(juce::Graphics& g) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        auto r = getLocalBounds();
        auto head = r.removeFromTop(20);
        ui::text(g, u8("Pitches it listens for"), head, L.body(15.f, true), p.fg);
        ui::text(g, hzText(lo.getValue()) + u8(" – ") + hzText(hi.getValue()), head, L.mono(13.f), p.fg, juce::Justification::centredRight);
        const auto t = track();
        g.setColour(p.meterOff);
        g.fillRect(t.reduced(0, 10));
        const float x0 = xOf(lo.getValue()), x1 = xOf(hi.getValue());
        g.setColour(p.fg);
        g.fillRect(juce::Rectangle<float>(x0, (float)t.getY() + 10.f, x1 - x0, (float)t.getHeight() - 20.f));
        // what it hears right now
        if (marker > 0.f) {
            const double hz = 440.0 * std::pow(2.0, (marker - 69.0) / 12.0);
            const float xm = xOf(hz);
            g.setColour(p.signal);
            g.fillRect(xm - 1.5f, (float)t.getY() + 2.f, 3.f, (float)t.getHeight() - 4.f);
        }
        for (auto* s : {&lo, &hi}) {
            const auto b = s->getBounds().toFloat().reduced(2.f, 0.f);
            g.setColour(p.bg);
            g.fillRect(b);
            g.setColour(p.line);
            g.drawRect(b, p.lineW);
            g.fillRect(b.getCentreX() - 1.f, b.getY() + 7.f, 2.f, b.getHeight() - 14.f);
        }
        // axis
        auto ticks = getLocalBounds().withTrimmedTop(t.getBottom() + 2).withHeight(16);
        for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
            const float x = xOf(f);
            g.setColour(p.hair);
            g.fillRect(x, (float)t.getBottom() - 2.f, 1.f, 4.f);
            ui::text(g, f >= 1000.0 ? juce::String((int)(f / 1000)) + "k" : juce::String((int)f),
                     juce::Rectangle<int>(juce::roundToInt(x) - 20, ticks.getY(), 40, 16), L.mono(10.5f), p.muted, juce::Justification::centred);
        }
        auto sent = getLocalBounds().withTrimmedTop(ticks.getBottom() + 4).withHeight(20);
        ui::text(g, u8("Below ") + hzText(lo.getValue()) + u8(" it filters out rumble and wind. Notes above ")
                    + hzText(hi.getValue()) + u8(" are not heard as pitches."),
                 sent, L.body(13.f), p.fg);
        g.setColour(p.hair);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
};

// ------------------------------------------------------------------ header bits
struct StepChip : juce::Button {
    juce::String num, label, value;
    StepChip(const juce::String& n, const juce::String& l) : juce::Button(l), num(n), label(l) { setWantsKeyboardFocus(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        auto r = getLocalBounds();
        g.setColour(over ? p.raised : p.bg);
        g.fillRect(r);
        g.setColour(p.line);
        g.drawRect(r.toFloat(), p.lineW);
        auto badge = r.removeFromLeft(r.getHeight()).reduced(4);
        g.setColour(p.fg);
        g.fillRect(badge);
        ui::text(g, num, badge, L.mono(15.f), p.bg, juce::Justification::centred);
        r.removeFromLeft(8);
        ui::text(g, label, r.removeFromTop(r.getHeight() / 2).withTrimmedTop(4), L.body(12.f), p.muted, juce::Justification::bottomLeft);
        ui::text(g, value, r.withTrimmedBottom(4), L.body(14.f, true), p.fg, juce::Justification::topLeft);
    }
};

struct SwitchButton : juce::ToggleButton {
    SwitchButton() { setWantsKeyboardFocus(true); }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        const bool on = getToggleState();
        auto r = getLocalBounds();
        g.setColour(on ? p.fg : (over ? p.raised : p.bg));
        g.fillRect(r);
        g.setColour(p.line);
        g.drawRect(r.toFloat(), p.lineW);
        const juce::Colour ink = on ? p.bg : p.fg;
        auto knob = r.removeFromLeft(58).reduced(8, 11);
        g.setColour(ink);
        g.drawRect(knob.toFloat(), 2.f);
        auto k = knob.reduced(4);
        k = on ? k.removeFromRight(k.getWidth() / 2) : k.removeFromLeft(k.getWidth() / 2);
        g.setColour(on ? p.signal : ink);
        g.fillRect(k);
        ui::text(g, u8("Answer"), r.removeFromTop(r.getHeight() / 2).withTrimmedTop(4), L.body(12.f), ink, juce::Justification::bottomLeft);
        ui::text(g, on ? "ON" : "OFF", r.withTrimmedBottom(4), L.body(15.f, true), ink, juce::Justification::topLeft);
    }
};

// A card that can be picked (welcome screen).
struct Card : juce::Button {
    juce::String title, desc, iconName;
    Card(const juce::String& t, const juce::String& d, const juce::String& i = {}) : juce::Button(t), title(t), desc(d), iconName(i) {
        setWantsKeyboardFocus(true);
        setDescription(d);
    }
    void paintButton(juce::Graphics& g, bool over, bool) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        const bool on = getToggleState();
        auto r = getLocalBounds();
        g.setColour(on ? p.raised : (over ? p.raised : p.bg));
        g.fillRect(r);
        g.setColour(p.line);
        g.drawRect(r.toFloat(), on ? p.lineW * 2.f : p.lineW);
        r = r.reduced(on ? 12 : 10, 8);
        if (iconName.isNotEmpty()) {
            ui::drawIcon(g, iconName, r.removeFromTop(28).removeFromLeft(28).toFloat(), p.fg);
            r.removeFromTop(4);
        }
        ui::text(g, title, r.removeFromTop(20), L.body(15.5f, true), p.fg);
        ui::text(g, desc, r, L.body(13.f), p.muted, juce::Justification::topLeft, 3);
        if (on) {
            auto mark = getLocalBounds().removeFromRight(30).removeFromTop(30).reduced(7);
            g.setColour(p.fg);
            g.fillRect(mark);
            ui::drawIcon(g, "check", mark.toFloat().reduced(1.f), p.bg, 3.f);
        }
    }
};

} // namespace

// =====================================================================================
class Content : public juce::Component {
public:
    Content(DialogueProcessor& p, ui::DisplaySettings& s, DialogueEditor& e, ui::Look& look)
        : proc(p), settings(s), editor(e) {
        setLookAndFeel(&look);
        setTitle("LPF Dialogue");
        setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        buildHeader();
        buildStatus();
        buildHearing();
        rightPainter.c = this;
        rightPainter.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(rightPainter);
        buildCharacter();
        buildAdvanced();
        buildRouting();
        buildFooter();
        buildWelcome();
        buildHelp();
        buildDisplay();
        showView(View::Character);
        welcome.setVisible(!settings.welcomeDone);
        addMouseListener(this, true);
        built = true;
        setSize(W, H);
    }
    bool built = false;

    ~Content() override { removeMouseListener(this); setLookAndFeel(nullptr); }

    enum class View { Character, Advanced, Routing };

    // --------------------------------------------------------------- refresh from the processor
    void refresh(bool slowTick) {
        phase = proc.uiPhase.load();
        waitProg = proc.uiWaitProgress.load();
        waitRemain = proc.uiWaitRemaining.load();
        silenceWait = proc.uiSilenceWait.load();
        heard = proc.uiHeardInPhrase.load();
        needed = proc.uiEventsNeeded.load();
        memSize = proc.uiMemory.load();
        level = proc.uiLevelDb.load();
        peak = proc.uiInputPeakDb.load();
        pitch = proc.uiPitch.load();
        hearing = proc.uiHearing.load();
        keyRoot = proc.uiKeyRoot.load(); scaleType = proc.uiScale.load();
        chordRoot = proc.uiChordRoot.load(); chordType = proc.uiChord.load();
        nowS = proc.uiNow.load();
        sr = proc.uiSampleRate.load();
        answeringNow = phase == (int)lpfd::Engine::Phase::Answering;

        // answer notes in the last minute (from the conversation history)
        if (slowTick && routing.isVisible()) {
            const int n = proc.timeline().read(tl.data(), (int)tl.size());
            sentLastMinute = 0;
            const double now = (double)nowS / sr;
            for (int i = 0; i < n; ++i)
                if (tl[(size_t)i].kind == lpfd::TimelineEvent::AnswerOn && now - (double)tl[(size_t)i].time / sr < 60.0) ++sentLastMinute;
            tlCount = n;
        }

        // calibration
        const int cs = proc.calibState.load();
        if (cs == 2 && calibShown != 2) {
            setParam(proc, "gate", proc.calibResultDb.load());
            calibShown = 2;
        } else if (cs != 2) calibShown = cs;

        if (slowTick || !settings.reduceMotion) timelineDirty = true;
        updatePresetHighlight();
        updateChips();
        repaintLive();
        announce();
    }

    // --------------------------------------------------------------- layout
    void resized() override {
        if (!built) return;
        // header
        auto h = juce::Rectangle<int>(0, 0, W, kHeaderH).reduced(12, 10);
        h.removeFromLeft(236);
        for (auto* c : chips) { c->setBounds(h.removeFromLeft(172)); h.removeFromLeft(8); }
        displayBtn.setBounds(h.removeFromRight(110));
        h.removeFromRight(8);
        helpBtn.setBounds(h.removeFromRight(80));
        h.removeFromRight(8);
        respondBtn.setBounds(h.removeFromRight(124));

        body = juce::Rectangle<int>(0, kHeaderH, W, H - kHeaderH - kFooterH).reduced(12);
        auto b = body;
        rightR = b.removeFromRight(400);
        b.removeFromRight(12);
        statusR = b.removeFromTop(100);
        b.removeFromTop(10);
        hearingR = b.removeFromBottom(116);
        b.removeFromBottom(10);
        timelineR = b;

        // status children
        auto mem = statusR.removeFromRight(196).reduced(14, 0);
        mem.removeFromTop(54);
        auto btns = mem.removeFromTop(34);
        freezeBtn.setBounds(btns.removeFromLeft(80));
        btns.removeFromLeft(6);
        clearBtn.setBounds(btns.removeFromLeft(90));
        statusR = statusR.withRight(statusR.getRight() + 196);

        // hearing children
        {
            auto r = hearingR.reduced(14, 10);
            r.removeFromRight(354);
            r.removeFromTop(48);
            auto row = r.removeFromTop(36);
            row.removeFromLeft(44);
            measureBtn.setBounds(row.removeFromRight(150).reduced(0, 1));
            row.removeFromRight(8);
            gateSlider.setBounds(row);
        }

        // right panel views
        rightPainter.setBounds(rightR);
        timelineAcc.setBounds(timelineR);
        character.setBounds(rightR);
        advanced.setBounds(rightR);
        routing.setBounds(rightR);
        layoutCharacter();
        layoutAdvanced();
        layoutRouting();

        // footer
        diagBtn.setBounds(juce::Rectangle<int>(W - 330, H - kFooterH, 318, kFooterH).reduced(0, 3));

        // overlays
        welcome.setBounds(0, kHeaderH, W, H - kHeaderH);
        layoutWelcome();
        help.setBounds(getLocalBounds());
        display.setBounds(getLocalBounds());
        layoutDialog(help, helpItems);
        layoutDisplay();
    }

    // --------------------------------------------------------------- painting
    void paint(juce::Graphics& g) override {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        g.fillAll(p.bg);
        // header
        auto hr = juce::Rectangle<int>(0, 0, W, kHeaderH);
        g.setColour(p.panel);
        g.fillRect(hr);
        g.setColour(p.line);
        g.fillRect(0, kHeaderH - (int)p.lineW, W, (int)p.lineW);
        ui::text(g, "LPF/DIALOGUE", {16, 10, 230, 28}, L.mono(22.f), p.fg);
        ui::text(g, u8("A partner that listens and answers"), {16, 36, 230, 18}, L.body(12.5f), p.muted);

        paintStatus(g, L, p);
        paintTimeline(g, L, p);
        paintHearing(g, L, p);
        panel(g, p, rightR);
        paintFooter(g, L, p);
    }

    void paintOverChildren(juce::Graphics& g) override {
        const auto& p = lookOf(*this).pal();
        if (settings.flash && answeringNow && !welcome.isVisible()) {
            g.setColour(p.signal);
            g.drawRect(getLocalBounds().toFloat(), 6.f);
        }
        // visible keyboard focus
        if (keyboardMode) {
            if (auto* f = juce::Component::getCurrentlyFocusedComponent())
                if (isParentOf(f) && f->isShowing()) {
                    auto r = getLocalArea(f, f->getLocalBounds()).toFloat().expanded(3.f);
                    g.setColour(p.focus);
                    g.drawRect(r, 3.f);
                }
        }
    }

    void setKeyboardMode(bool k) { if (k != keyboardMode) { keyboardMode = k; repaint(); } }
    void mouseDown(const juce::MouseEvent&) override { setKeyboardMode(false); }

    // --------------------------------------------------------------- keyboard shortcuts
    bool shortcut(const juce::KeyPress& k) {
        if (k == juce::KeyPress::escapeKey) {
            if (help.isVisible()) { closeDialog(help); return true; }
            if (display.isVisible()) { closeDialog(display); return true; }
            if (currentView != View::Character) { showView(View::Character); return true; }
            return false;
        }
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        const bool onButton = dynamic_cast<juce::Button*>(focused) != nullptr;
        const juce::juce_wchar c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
        if (k == juce::KeyPress::spaceKey && !onButton) { respondBtn.triggerClick(); return true; }
        if (c == 'f') { freezeBtn.triggerClick(); return true; }
        if (c == 's') { measureBtn.triggerClick(); return true; }
        if (c == '?') { openDialog(help); return true; }
        if (c >= '1' && c <= '6') { applyPreset((int)(c - '1')); return true; }
        return false;
    }

    void applyTheme() {
        lookOf(*this).setPalette(ui::palette(settings.theme));
        sendLookAndFeelChange();
        repaint();
    }

private:
    // ================================================================ header
    juce::OwnedArray<StepChip> chips;
    SwitchButton respondBtn;
    std::unique_ptr<ButtonAtt> respondAtt;
    juce::TextButton helpBtn{"Help"}, displayBtn{"Display"};

    void buildHeader() {
        const char* labels[] = {"Listens to", "Answers on", "Character"};
        for (int i = 0; i < 3; ++i) {
            auto* c = chips.add(new StepChip(juce::String(i + 1), u8(labels[i])));
            c->onClick = [this, i] {
                if (i == 1) showView(View::Routing);
                else openWelcome();
            };
            addAndMakeVisible(c);
        }
        respondBtn.setTitle(u8("Answer"));
        respondBtn.setDescription(u8("Off: it listens and learns without answering. Shortcut: Space."));
        addAndMakeVisible(respondBtn);
        respondAtt = std::make_unique<ButtonAtt>(proc.apvts, "respond", respondBtn);
        styleButton(helpBtn, "help");
        styleButton(displayBtn, "display");
        helpBtn.onClick = [this] { openDialog(help); };
        displayBtn.onClick = [this] { openDialog(display); };
        helpBtn.setDescription(u8("How it works, keyboard shortcuts. Shortcut: ?"));
        displayBtn.setDescription(u8("Theme, text size, accessibility."));
        addAndMakeVisible(helpBtn);
        addAndMakeVisible(displayBtn);
    }

    void updateChips() {
        const int src = juce::roundToInt(getParam(proc, "source"));
        const bool bright = getParam(proc, "mode") > 0.5f;
        juce::String s = src == 1 ? "MIDI keyboard" : (src == 2 ? "Audio + MIDI" : (bright ? u8("Audio · Bright") : u8("Audio · instrument")));
        juce::String r = u8("channel ") + juce::String(juce::roundToInt(getParam(proc, "channel")));
        juce::String c = presetIndex >= 0 ? u8(ui::presets()[(size_t)presetIndex].name) : juce::String("Custom");
        const juce::String vals[] = {s, r, c};
        for (int i = 0; i < 3; ++i)
            if (chips[i]->value != vals[i]) {
                chips[i]->value = vals[i];
                chips[i]->setTitle(chips[i]->label + " : " + vals[i]);
                chips[i]->repaint();
            }
    }

    // ================================================================ status
    juce::TextButton freezeBtn{"Freeze"}, clearBtn{u8("Clear…")};
    std::unique_ptr<ButtonAtt> freezeAtt;
    bool clearArmed = false;
    juce::Rectangle<int> statusR, timelineR, hearingR, rightR, body;

    void buildStatus() {
        freezeBtn.setClickingTogglesState(true);
        styleButton(freezeBtn, {}, false, 14.f);
        freezeBtn.setTitle(u8("Freeze memory"));
        freezeBtn.setDescription(u8("It stops learning, but keeps listening and answering. Shortcut: F."));
        freezeAtt = std::make_unique<ButtonAtt>(proc.apvts, "freeze", freezeBtn);
        addAndMakeVisible(freezeBtn);
        styleButton(clearBtn, {}, false, 14.f);
        clearBtn.setTitle(u8("Clear memory"));
        clearBtn.setDescription(u8("Forgets everything it heard. Asks for confirmation."));
        clearBtn.onClick = [this] {
            if (!clearArmed) {
                clearArmed = true;
                clearBtn.setButtonText(u8("Confirm"));
                clearBtn.getProperties().set("accent", true);
                clearBtn.repaint();
                juce::Component::SafePointer<Content> safe(this);
                juce::Timer::callAfterDelay(3000, [safe] {
                    if (safe != nullptr) safe->disarmClear();
                });
            } else {
                proc.requestClear();
                disarmClear();
                juce::AccessibilityHandler::postAnnouncement(u8("Memory cleared."), juce::AccessibilityHandler::AnnouncementPriority::medium);
            }
        };
        addAndMakeVisible(clearBtn);
    }

    void disarmClear() {
        clearArmed = false;
        clearBtn.setButtonText(u8("Clear…"));
        clearBtn.getProperties().remove("accent");
        clearBtn.repaint();
    }

    struct StatusText { juce::String word, iconName, text, sub; bool countdown = false; int style = 0; };

    StatusText statusText() const {
        StatusText s;
        const bool respond = getParam(const_cast<DialogueProcessor&>(proc), "respond") > 0.5f;
        const float interrupt = getParam(const_cast<DialogueProcessor&>(proc), "interruption");
        const juce::String key = keyRoot >= 0 ? u8("in ") + keyText(keyRoot, scaleType) : juce::String();
        switch ((lpfd::Engine::Phase)phase) {
        case lpfd::Engine::Phase::Listening:
            s = {u8("LISTENING"), "listen", u8("It's listening to you."), {}, false, 1};
            if (!respond) s.sub = u8("Answering is off: it learns without answering.");
            else if (interrupt <= 0.01f || needed >= 99) s.sub = juce::String(heard) + u8(" notes heard. Waiting for the end of your phrase.");
            else s.sub = juce::String(heard) + u8(" notes heard. May answer from note ") + juce::String(needed) + ".";
            break;
        case lpfd::Engine::Phase::Waiting:
            s = {u8("WAITING"), "wait", respond ? u8("Phrase over. Answering in ") + fmtS(waitRemain) + "."
                                               : u8("Phrase over. Answering is off."), {}, respond, 2};
            break;
        case lpfd::Engine::Phase::Answering:
            s = {u8("ANSWERING"), "answer", u8("It's answering you") + (key.isNotEmpty() ? ", " + key : juce::String()) + ".",
                 interrupt < 0.5f ? u8("It will stop if you start again.") : u8("It keeps going even if you start again."), false, 3};
            break;
        case lpfd::Engine::Phase::Silence:
        default:
            if (memSize == 0 && peak < getParam(const_cast<DialogueProcessor&>(proc), "gate"))
                s = {u8("SILENCE"), "silence", u8("It hears nothing."), u8("Play something, or lower the silence threshold if your signal is quiet."), false, 0};
            else
                s = {u8("YOUR TURN"), "listen", u8("Your turn."), respond ? u8("It will answer once you've played.") : u8("Answering is off."), false, 0};
        }
        return s;
    }

    void paintStatus(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        panel(g, p, statusR);
        const auto st = statusText();
        auto r = statusR;
        auto badge = r.removeFromLeft(108);
        juce::Colour bg = p.panel, ink = p.fg;
        if (st.style == 1) { bg = p.fg; ink = p.bg; }
        else if (st.style == 2) { bg = p.raised; }
        else if (st.style == 3) { bg = p.signal; ink = juce::Colour(0xff111111); }
        g.setColour(bg);
        g.fillRect(badge.reduced((int)p.lineW));
        g.setColour(p.line);
        g.fillRect(badge.getRight() - (int)p.lineW, badge.getY(), (int)p.lineW, badge.getHeight());
        ui::drawIcon(g, st.iconName, badge.withTrimmedBottom(26).toFloat().withSizeKeepingCentre(32.f, 32.f).translated(0, 8), ink);
        ui::text(g, st.word, badge.removeFromBottom(34), L.mono(14.f), ink, juce::Justification::centred);

        auto mem = r.removeFromRight(196);
        g.setColour(p.line);
        g.fillRect(mem.getX(), mem.getY(), (int)p.lineW, mem.getHeight());
        mem = mem.reduced(14, 8);
        ui::text(g, u8("Memory"), mem.removeFromTop(18), L.body(13.f), p.muted);
        const bool frozen = getParam(proc, "freeze") > 0.5f;
        ui::text(g, juce::String(memSize) + " notes" + (frozen ? u8(" · frozen") : juce::String()), mem.removeFromTop(26), L.mono(17.f), p.fg);

        r = r.reduced(18, 12);
        ui::text(g, st.text, r.removeFromTop(30), L.body(19.f, true), p.fg);
        if (st.countdown) {
            auto bar = r.removeFromTop(30).withSizeKeepingCentre(r.getWidth(), 12);
            auto label = bar.removeFromRight(150);
            g.setColour(p.bg);
            g.fillRect(bar);
            g.setColour(p.fg);
            g.fillRect(bar.withWidth(juce::roundToInt(bar.getWidth() * waitProg)));
            g.setColour(p.line);
            g.drawRect(bar.toFloat(), p.lineW);
            ui::text(g, u8("silence ") + fmtS(silenceWait - waitRemain) + " / " + fmtS(silenceWait), label.expanded(0, 6),
                     L.mono(11.5f), p.muted, juce::Justification::centredRight);
        } else {
            ui::text(g, st.sub, r.removeFromTop(40), L.body(14.f), p.muted, juce::Justification::topLeft, 2);
        }
    }

    // ================================================================ timeline
    std::vector<lpfd::TimelineEvent> tl = std::vector<lpfd::TimelineEvent>(1600);
    int tlCount = 0;
    bool timelineDirty = true;
    juce::String timelineSummary;

    void paintTimeline(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        panel(g, p, timelineR);
        auto r = timelineR;
        auto head = r.removeFromTop(40).reduced(14, 0);
        ui::text(g, u8("The conversation"), head.removeFromLeft(140), L.body(16.f, true), p.fg);
        ui::text(g, u8("· last 12 seconds"), head.removeFromLeft(200), L.body(14.f), p.muted);
        {
            auto lg = head.removeFromRight(150);
            auto a = lg.removeFromLeft(75), b = lg;
            g.setColour(p.fg);
            g.fillRect(a.removeFromLeft(14).withSizeKeepingCentre(12, 12));
            ui::text(g, "You", a.withTrimmedLeft(6), L.body(14.f), p.fg);
            auto d = b.removeFromLeft(14).withSizeKeepingCentre(12, 12).toFloat();
            if (settings.shapes) {
                juce::Path dia;
                dia.addRectangle(d);
                dia.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi / 4.f, d.getCentreX(), d.getCentreY()));
                g.setColour(p.signal);
                g.fillPath(dia);
                g.setColour(p.line);
                g.strokePath(dia, juce::PathStrokeType(1.5f));
            } else {
                g.setColour(p.signal);
                g.fillRect(d);
            }
            ui::text(g, "It", b.withTrimmedLeft(6), L.body(14.f), p.fg);
        }
        g.setColour(p.line);
        g.fillRect(timelineR.getX(), timelineR.getY() + 40, timelineR.getWidth(), (int)p.lineW);

        auto axis = r.removeFromBottom(26);
        auto lanes = r;
        auto labels = lanes.removeFromLeft(64);
        g.fillRect(labels.getRight() - (int)p.lineW, lanes.getY(), (int)p.lineW, lanes.getHeight() + 26);
        auto you = lanes.removeFromTop(lanes.getHeight() / 2), him = lanes;
        ui::text(g, "YOU", labels.removeFromTop(you.getHeight()), L.mono(13.f), p.fg, juce::Justification::centred);
        ui::text(g, "IT", labels, L.mono(13.f), p.fg, juce::Justification::centred);
        g.setColour(p.hair);
        g.fillRect(you.getX(), you.getBottom(), you.getWidth(), 1);
        g.setColour(p.line);
        g.fillRect(timelineR.getX(), axis.getY(), timelineR.getWidth(), (int)p.lineW);

        if (timelineDirty) { tlCount = proc.timeline().read(tl.data(), (int)tl.size()); timelineDirty = false; }

        const double span = 12.4, nowPos = 0.965;
        const double now = (double)nowS / sr;
        const float x0 = (float)you.getX(), wpx = (float)you.getWidth();
        auto xOf = [&](double t) { return x0 + (float)((t - (now - span * nowPos)) / span) * wpx; };
        const double tMin = now - span * nowPos;

        // collect notes
        struct N { double on, off; int pitch; };
        std::vector<N> yn, hn;
        struct T { double t; int reason; float info; };
        std::vector<T> trig;
        double openYou[128], openHim[128];
        for (int i = 0; i < 128; ++i) openYou[i] = openHim[i] = -1;
        for (int i = 0; i < tlCount; ++i) {
            const auto& e = tl[(size_t)i];
            const double t = (double)e.time / sr;
            switch (e.kind) {
            case lpfd::TimelineEvent::PlayerOn: openYou[e.pitch] = t; break;
            case lpfd::TimelineEvent::PlayerOff:
                if (openYou[e.pitch] >= 0) { yn.push_back({openYou[e.pitch], t, e.pitch}); openYou[e.pitch] = -1; }
                break;
            case lpfd::TimelineEvent::AnswerOn: openHim[e.pitch] = t; break;
            case lpfd::TimelineEvent::AnswerOff:
                if (openHim[e.pitch] >= 0) { hn.push_back({openHim[e.pitch], t, e.pitch}); openHim[e.pitch] = -1; }
                break;
            case lpfd::TimelineEvent::Trigger: trig.push_back({t, e.reason, e.info}); break;
            default: break;
            }
        }
        for (int i = 0; i < 128; ++i) {
            if (openYou[i] >= 0) yn.push_back({openYou[i], now, i});
            if (openHim[i] >= 0) hn.push_back({openHim[i], now, i});
        }

        g.saveState();
        g.reduceClipRegion(you.getUnion(him));
        // group notes into phrases: for you, by gaps; for him, by the answer they belong to
        auto groupOf = [&](const N& n) {
            int gi = -1;
            for (int k = 0; k < (int)trig.size(); ++k) if (trig[(size_t)k].t <= n.on + 0.02) gi = k;
            return gi;
        };
        int lastLabelEnd = -100000;
        auto drawLane = [&](std::vector<N>& v, juce::Rectangle<int> lane, bool isHim) {
            if (v.empty()) return;
            std::sort(v.begin(), v.end(), [](const N& a, const N& b) { return a.on < b.on; });
            int lo = 127, hi = 0;
            for (auto& n : v) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
            if (hi - lo < 12) { const int mid = (lo + hi) / 2; lo = mid - 6; hi = mid + 6; }
            size_t i = 0;
            int phraseCount = 0;
            while (i < v.size()) {
                size_t j = i;
                double end = v[i].off;
                const int gi = isHim ? groupOf(v[i]) : -1;
                while (j + 1 < v.size()) {
                    const bool sameGroup = isHim && gi >= 0 ? groupOf(v[j + 1]) == gi : v[j + 1].on - end < 0.35;
                    if (!sameGroup) break;
                    ++j;
                    end = std::max(end, v[j].off);
                }
                if (end >= tMin) {
                    const float a = xOf(v[i].on) - 4.f, b = xOf(end) + 4.f;
                    auto box = juce::Rectangle<float>(a, lane.getY() + lane.getHeight() * 0.16f, juce::jmax(8.f, b - a), lane.getHeight() * 0.58f);
                    g.setColour(isHim ? p.signal : p.fg);
                    g.fillRect(box);
                    g.setColour(p.line);
                    g.drawRect(box, p.lineW);
                    const juce::Colour noteC = isHim ? juce::Colour(0xff111111) : p.bg;
                    for (size_t k = i; k <= j; ++k) {
                        const float nx = xOf(v[k].on), nw = std::max(3.f, xOf(v[k].off) - nx - 1.f);
                        const float ny = box.getBottom() - 6.f - (box.getHeight() - 14.f) * (float)(v[k].pitch - lo) / (float)(hi - lo);
                        g.setColour(noteC);
                        g.fillRect(nx, ny - 3.f, nw, 5.f);
                    }
                    if (isHim && settings.shapes) {
                        juce::Path dia;
                        auto d = juce::Rectangle<float>(9.f, 9.f).withCentre({box.getX() + 2.f, box.getY()});
                        dia.addRectangle(d);
                        dia.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi / 4.f, d.getCentreX(), d.getCentreY()));
                        g.setColour(p.signal);
                        g.fillPath(dia);
                        g.setColour(p.line);
                        g.strokePath(dia, juce::PathStrokeType(1.5f));
                    }
                    // why it answered, under its answer (skipped if it would overlap the previous one)
                    if (isHim && gi >= 0) {
                        const auto& t = trig[(size_t)gi];
                        juce::String why = t.reason == lpfd::TimelineEvent::Silence ? u8("after ") + fmtS(t.info) + u8(" of silence")
                                         : t.reason == lpfd::TimelineEvent::Reflex ? u8("reflex")
                                         : t.reason == lpfd::TimelineEvent::Held ? u8("over your held note")
                                         : u8("cut in after ") + juce::String((int)t.info) + " notes";
                        const auto f = L.body(12.5f);
                        const int tw = (int)juce::GlyphArrangement::getStringWidth(f, why) + 4;
                        const int tx = juce::jmin((int)a, lane.getRight() - tw - 4);
                        if (tx > lastLabelEnd + 10) {
                            ui::text(g, why, juce::Rectangle<int>(tx, lane.getBottom() - 22, tw, 18), f, p.muted);
                            lastLabelEnd = tx + tw;
                        }
                    }
                    ++phraseCount;
                }
                i = j + 1;
            }
            (isHim ? phrasesHim : phrasesYou) = phraseCount;
        };
        phrasesHim = phrasesYou = 0;
        drawLane(yn, you, false);
        drawLane(hn, him, true);
        g.restoreState();

        // axis + now
        for (int s = -12; s <= 0; s += 2) {
            const float x = xOf(now + s);
            ui::text(g, s == 0 ? "0 s" : juce::String(s) + " s", juce::Rectangle<int>((int)x - (s == 0 ? 40 : 20), axis.getY() + 4, 40, 18),
                     L.mono(11.f), p.muted, s == 0 ? juce::Justification::centredRight : juce::Justification::centred);
        }
        const float nx = xOf(now);
        g.setColour(p.signal);
        g.fillRect(nx - 1.f, (float)you.getY(), 2.f, (float)(him.getBottom() - you.getY()));
        auto tag = juce::Rectangle<int>((int)nx - 104, you.getY() + 6, 104, 18);
        g.fillRect(tag);
        ui::text(g, "NOW", tag, L.mono(11.f), juce::Colour(0xff111111), juce::Justification::centred);

        // summary for screen readers
        juce::String sum = phrasesYou == 0 && phrasesHim == 0 ? u8("Nothing in the last 12 seconds.")
                         : u8("In the last 12 seconds: you played ") + juce::String(phrasesYou) + u8(" phrase(s), it answered ")
                               + juce::String(phrasesHim) + u8(" time(s).");
        if (sum != timelineSummary) { timelineSummary = sum; timelineAcc.setTitle(sum); }
    }
    int phrasesYou = 0, phrasesHim = 0;
    juce::Component timelineAcc;   // an invisible element that carries the summary

    // ================================================================ hearing
    ui::Slider gateSlider;
    std::unique_ptr<SliderAtt> gateAtt;
    juce::TextButton measureBtn{"Measure silence"};
    int calibShown = 0;

    void buildHearing() {
        addAndMakeVisible(gateSlider);
        gateAtt = std::make_unique<SliderAtt>(proc.apvts, "gate", gateSlider);
        gateSlider.defaultValue = -55.0;
        gateSlider.setTitle("Silence threshold");
        gateSlider.setDescription(u8("Below this level it's silence. The orange mark on the meter."));
        gateSlider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " dB"; };
        gateSlider.onValueChange = [this] { repaint(hearingR); };
        styleButton(measureBtn, "wave", false, 14.f);
        measureBtn.setDescription(u8("Don't play for 3 seconds: it measures the room noise and sets the threshold just above it. Shortcut: S."));
        measureBtn.onClick = [this] { proc.requestCalibration(); };
        addAndMakeVisible(measureBtn);
        addAndMakeVisible(timelineAcc);
        timelineAcc.setInterceptsMouseClicks(false, false);
        timelineAcc.setAccessible(true);
    }

    void paintHearing(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        panel(g, p, hearingR);
        auto r = hearingR;
        auto read = r.removeFromRight(354);
        g.setColour(p.line);
        g.fillRect(read.getX(), read.getY(), (int)p.lineW, read.getHeight());
        auto cell = [&](juce::Rectangle<int> c, const juce::String& label, const juce::String& v, bool sep) {
            if (sep) { g.setColour(p.line); g.fillRect(c.getRight() - (int)p.lineW, c.getY(), (int)p.lineW, c.getHeight()); }
            c = c.reduced(12, 0);
            ui::text(g, label, c.withTrimmedTop(26).withHeight(18), L.body(13.f), p.muted);
            ui::text(g, v, c.withTrimmedTop(46).withHeight(32), L.mono(v.length() > 9 ? 15.f : 20.f), p.fg);
        };
        const bool src = getParam(proc, "source") >= 0.5f;
        cell(read.removeFromLeft(92), "Note", hearing && pitch > 0.f ? noteName(juce::roundToInt(pitch)) : (src ? juce::String("MIDI") : "---"), true);
        cell(read.removeFromLeft(170), "Scale", ui::upper(keyText(keyRoot, scaleType)), true);
        cell(read, "Chord", chordText(chordRoot, chordType), false);

        r = r.reduced(14, 10);
        auto title = r.removeFromTop(20);
        ui::text(g, u8("What it hears"), title.removeFromLeft(130), L.body(16.f, true), p.fg);
        const float gate = getParam(proc, "gate");
        ui::text(g, "threshold " + juce::String(juce::roundToInt(gate)) + " dB", title, L.body(13.f), p.muted, juce::Justification::centredRight);
        auto meter = r.removeFromTop(26).reduced(0, 4);
        const int n = 40;
        const float segW = (float)meter.getWidth() / n;
        for (int i = 0; i < n; ++i) {
            const float db = -80.f + (i + 0.5f) * 60.f / n;
            const bool lit = db <= level;
            g.setColour(lit ? (db < gate ? p.muted : p.fg) : p.meterOff);
            g.fillRect(meter.getX() + i * segW + 1.f, (float)meter.getY(), segW - 2.f, (float)meter.getHeight());
        }
        const float gx = meter.getX() + meter.getWidth() * juce::jlimit(0.f, 1.f, (gate + 80.f) / 60.f);
        g.setColour(p.signal);
        g.fillRect(gx - 2.f, (float)meter.getY() - 4.f, 4.f, (float)meter.getHeight() + 8.f);
        auto row = r.removeFromTop(36);
        ui::text(g, "Threshold", row.removeFromLeft(44), L.body(13.f), p.muted);
        const int cs = proc.calibState.load();
        juce::String note;
        if (cs == 1) note = u8("Measuring silence… don't play (") + juce::String(juce::roundToInt(proc.calibProgress.load() * 100)) + " %)";
        else if (cs == 2) note = u8("Room noise ") + juce::String(juce::roundToInt(proc.calibNoiseDb.load())) + u8(" dB: threshold set to ")
                                 + juce::String(juce::roundToInt(proc.calibResultDb.load())) + " dB.";
        if (note.isNotEmpty()) ui::text(g, note, r.removeFromTop(18), L.body(12.5f), cs == 1 ? p.signalText : p.muted);
    }

    // ================================================================ right: character
    juce::Component character, advanced, routing;
    juce::OwnedArray<juce::TextButton> presetBtns;
    juce::OwnedArray<QSlider> charSliders;
    juce::TextButton advancedBtn{u8("Advanced: ear, expression, MIDI")};
    int presetIndex = -1;
    View currentView = View::Character;

    void buildCharacter() {
        addAndMakeVisible(character);
        character.setTitle(u8("Its character"));
        character.setFocusContainerType(FocusContainerType::focusContainer);
        for (int i = 0; i < (int)ui::presets().size(); ++i) {
            auto* b = presetBtns.add(new juce::TextButton(u8(ui::presets()[(size_t)i].name)));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5100);
            b->setDescription(u8(ui::presets()[(size_t)i].desc) + u8(" Shortcut: ") + juce::String(i + 1) + ".");
            b->onClick = [this, i] { applyPreset(i); };
            character.addAndMakeVisible(b);
        }
        auto key = [this] { return keyRoot >= 0 ? keyText(keyRoot, scaleType) : u8("the detected scale"); };
        auto add = [this](QSlider* q) { charSliders.add(q); character.addAndMakeVisible(q); };
        add(new QSlider(proc, "patience", u8("When does it answer?"), u8("Every note"), u8("At the end"), [](double v) {
            if (v < 0.04) return u8("It answers every note, without waiting.");
            return u8("It listens to about ") + juce::String(1 + juce::roundToInt(v * v * 15)) + u8(" notes, or ")
                   + fmtS((float)(0.08 + 1.92 * std::pow(v, 1.5))) + u8(" of silence.");
        }));
        add(new QSlider(proc, "interruption", u8("Does it cut in?"), "Never", "Often", [](double v) {
            return v < 0.03 ? u8("Never over you: it waits for a pause, and stops if you start again.")
                 : v < 0.5 ? u8("It sometimes cuts in (") + juce::String(juce::roundToInt(v * 200)) + u8(" % of the time), but stops if you start again.")
                           : u8("It answers as soon as its patience allows, even over you.");
        }));
        add(new QSlider(proc, "contrast", u8("How does it answer?"), "Imitates", "Contradicts", [](double v) {
            return v < 0.25 ? u8("It follows your register, rhythm and contour.")
                 : v < 0.6 ? u8("Sometimes it imitates, sometimes it opposes.")
                           : u8("It goes the other way: other register, other rhythm.");
        }));
        add(new QSlider(proc, "random", u8("Where do its notes come from?"), "From you", u8("Invented"), [key](double v) {
            return v < 0.15 ? u8("Only sequences it heard from you.")
                 : v < 0.55 ? u8("It adds a few notes from ") + key() + "."
                            : u8("It invents a lot, staying in ") + key() + ".";
        }));
        add(new QSlider(proc, "length", u8("Length of its answers"), "Short", "Long", [](double v) {
            return u8("About ") + juce::String(juce::jmax(1, juce::roundToInt(6 * v))) + u8(" notes for every 6 heard.");
        }));
        add(new QSlider(proc, "humanize", "Feel", "Machine", "Human", [](double v) {
            return v < 0.1 ? u8("Instant reaction, strict timing, even dynamics.")
                           : u8("Reacts with a slightly varying delay, breathes, varies its dynamics.");
        }));
        styleButton(advancedBtn, "right", false, 14.f);
        advancedBtn.getProperties().set("iconRight", true);
        advancedBtn.getProperties().set("left", true);
        advancedBtn.onClick = [this] { showView(View::Advanced); };
        character.addAndMakeVisible(advancedBtn);
    }

    void layoutCharacter() {
        auto r = character.getLocalBounds().reduced(14, 0);
        r.removeFromTop(42);
        auto grid = r.removeFromTop(94);
        const int bw = (grid.getWidth() - 12) / 3;
        for (int i = 0; i < presetBtns.size(); ++i)
            presetBtns[i]->setBounds(grid.getX() + (i % 3) * (bw + 6), grid.getY() + (i / 3) * 50, bw, 44);
        r.removeFromTop(8);
        advancedBtn.setBounds(character.getLocalBounds().removeFromBottom(48).reduced(10, 4));
        r.removeFromBottom(50);
        const int h = r.getHeight() / juce::jmax(1, charSliders.size());
        for (auto* q : charSliders) q->setBounds(r.removeFromTop(h).withTrimmedTop(4));
    }

    void updatePresetHighlight() {
        const float v[] = {getParam(proc, "patience"), getParam(proc, "interruption"), getParam(proc, "contrast"),
                           getParam(proc, "random"), ui::lengthTo01(getParam(proc, "length")), getParam(proc, "humanize")};
        int found = -1;
        for (int i = 0; i < (int)ui::presets().size(); ++i) {
            const auto& pr = ui::presets()[(size_t)i];
            const float w[] = {pr.patience, pr.interruption, pr.contrast, pr.random, pr.length01, pr.humanize};
            bool same = true;
            for (int k = 0; k < 6; ++k) same = same && std::abs(v[k] - w[k]) < 0.015f;
            if (same) { found = i; break; }
        }
        if (found != presetIndex) {
            presetIndex = found;
            for (int i = 0; i < presetBtns.size(); ++i) presetBtns[i]->setToggleState(i == found, juce::dontSendNotification);
            for (int i = 0; i < welcomePresets.size(); ++i) welcomePresets[i]->setToggleState(i == found, juce::dontSendNotification);
            character.repaint();
        }
    }

public:
    void applyPreset(int i) {
        if (i < 0 || i >= (int)ui::presets().size()) return;
        const auto& pr = ui::presets()[(size_t)i];
        setParam(proc, "patience", pr.patience);
        setParam(proc, "interruption", pr.interruption);
        setParam(proc, "contrast", pr.contrast);
        setParam(proc, "random", pr.random);
        setParam(proc, "length", ui::lengthFrom01(pr.length01));
        setParam(proc, "humanize", pr.humanize);
        if (settings.speak)
            juce::AccessibilityHandler::postAnnouncement(u8("Character: ") + u8(pr.name), juce::AccessibilityHandler::AnnouncementPriority::medium);
    }

private:
    // ================================================================ right: advanced
    juce::Viewport advView;
    juce::Component advInner;
    juce::TextButton advBack{"Back"}, routeBack{"Back"};
    std::unique_ptr<ui::Choice> earChoice, gridChoice, sourceChoice, bendChoice;
    std::unique_ptr<FreqRange> band;
    juce::OwnedArray<QSlider> advSliders;
    juce::ComboBox lowBox, highBox, inBox, outBox;
    std::unique_ptr<ComboAtt> inAtt;
    juce::ToggleButton thruBtn{u8("Also send your MIDI playing out")};
    std::unique_ptr<ButtonAtt> thruAtt;
    struct Strip : juce::Component {
        DialogueProcessor* proc = nullptr;
        void paint(juce::Graphics& g) override {
            auto& L = lookOf(*this);
            const auto& p = L.pal();
            const int lo = juce::roundToInt(getParam(*proc, "low")), hi = juce::roundToInt(getParam(*proc, "high"));
            auto r = getLocalBounds();
            const float w = (float)r.getWidth() / 9.f;
            for (int o = 0; o < 9; ++o) {
                const int m0 = (o + 1) * 12, m1 = m0 + 12;
                const bool in = m1 > lo && m0 < hi;
                auto c = juce::Rectangle<float>(r.getX() + o * w, (float)r.getY(), w, (float)r.getHeight());
                g.setColour(in ? p.fg : p.bg);
                g.fillRect(c);
                ui::text(g, "C" + juce::String(o), c.toNearestInt(), L.mono(11.f), in ? p.bg : p.muted, juce::Justification::centred);
                if (o) { g.setColour(p.line); g.fillRect(c.getX(), c.getY(), 1.f, c.getHeight()); }
            }
            g.setColour(p.line);
            g.drawRect(r.toFloat(), p.lineW);
        }
    } strip;
    struct Heading : juce::Component {
        juce::String t;
        void paint(juce::Graphics& g) override {
            auto& L = lookOf(*this);
            g.setColour(L.pal().line);
            g.fillRect(0, 0, getWidth(), (int)L.pal().lineW);
            ui::text(g, t, getLocalBounds().withTrimmedTop(6), L.mono(13.f), L.pal().fg);
        }
    };
    juce::OwnedArray<Heading> headings;
    juce::OwnedArray<juce::Label> advLabels;

    juce::Label* advLabel(const juce::String& t, bool bold, float size = 15.f) {
        auto* l = advLabels.add(new juce::Label({}, t));
        l->setFont(lookOf(*this).body(size, bold));
        l->setJustificationType(juce::Justification::topLeft);
        l->setMinimumHorizontalScale(0.9f);
        advInner.addAndMakeVisible(l);
        return l;
    }

    void buildAdvanced() {
        addChildComponent(advanced);
        advanced.setTitle(u8("Advanced settings"));
        advanced.setFocusContainerType(FocusContainerType::focusContainer);
        styleButton(advBack, "left", false, 14.f);
        advBack.setTitle(u8("Back to character"));
        advBack.onClick = [this] { showView(View::Character); };
        advanced.addAndMakeVisible(advBack);
        advanced.addAndMakeVisible(advView);
        advView.setViewedComponent(&advInner, false);
        advView.setScrollBarsShown(true, false);

        auto heading = [this](const juce::String& t) { auto* h = headings.add(new Heading()); h->t = t; advInner.addAndMakeVisible(h); };
        heading(u8("EAR"));
        earChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("mode"), u8("Listening type"),
                                                 juce::StringArray{"Instrument", "Bright"},
                                                 juce::StringArray{u8("Guitar, keys, bass · 55 Hz – 2 kHz"), u8("Voice, birds · 100 Hz – 10 kHz")});
        // picking a listening type also sets its usual band (it can be moved afterwards)
        earChoice->onPick = [this](int i) {
            setParam(proc, "lowcut", i == 1 ? 100.f : 55.f);
            setParam(proc, "highcut", i == 1 ? 10000.f : 2000.f);
        };
        advInner.addAndMakeVisible(*earChoice);
        band = std::make_unique<FreqRange>(proc);
        advInner.addAndMakeVisible(*band);
        auto addQ = [this](QSlider* q) { advSliders.add(q); advInner.addAndMakeVisible(q); };
        addQ(new QSlider(proc, "sensitivity", u8("Attack sensitivity"), "Little", "A lot", [](double v) {
            return v > 0.7 ? u8("Hears soft notes and re-plucks, but also finger noise.")
                 : v < 0.3 ? u8("Only clear attacks.") : u8("Balanced.");
        }));
        heading(u8("EXPRESSION"));
        addQ(new QSlider(proc, "dynamics", u8("Dynamics"), u8("Even"), u8("Like yours"), [](double v) {
            return v < 0.1 ? u8("Every answer at the same strength.")
                 : v < 0.6 ? u8("Softer when you play soft, a little.")
                           : u8("Soft when you play soft, loud when you play loud.");
        }));
        addQ(new QSlider(proc, "sustain", u8("Held notes"), u8("Lets go"), u8("Holds with you"), [](double v) {
            return v < 0.03 ? u8("Its notes keep their own length.")
                 : v > 0.97 ? u8("While you hold a note, it holds its own, and lets go with you.")
                            : u8("Often holds its note as long as you hold yours (") + juce::String(juce::roundToInt(v * 100)) + " %).";
        }));
        addQ(new QSlider(proc, "bend", u8("Bends and vibrato"), u8("Ignores"), u8("Follows"), [](double v) {
            return v < 0.03 ? u8("Its notes stay straight.")
                            : u8("Follows your bends and vibrato with pitch bend (") + juce::String(juce::roundToInt(v * 100)) + " %).";
        }));
        bendChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("bendrange"), u8("Your synth's bend range (semitones)"),
                                                  juce::StringArray{u8("±2"), u8("±12"), u8("±24"), u8("±48")});
        bendChoice->labelHeight = 24;
        advInner.addAndMakeVisible(*bendChoice);
        heading(u8("RANGE OF ITS ANSWERS"));
        strip.proc = &proc;
        advInner.addAndMakeVisible(strip);
        auto fillBox = [this](juce::ComboBox& b, const char* id, const juce::String& title) {
            for (int m = 24; m <= 108; m += 12) b.addItem(noteName(m), m + 1);
            b.setTitle(title);
            b.onChange = [this, &b, id] { setParam(proc, id, (float)(b.getSelectedId() - 1)); strip.repaint(); };
            advInner.addAndMakeVisible(b);
        };
        advLabel("Lowest", true);
        advLabel(u8("Highest"), true);
        fillBox(lowBox, "low", u8("Lowest note"));
        fillBox(highBox, "high", u8("Highest note"));
        advLabel(u8("Notes outside are folded by octaves, never dropped."), false, 13.f);
        heading(u8("MEMORY AND STYLE"));
        addQ(new QSlider(proc, "memory", "It remembers", u8("Last notes"), "Everything", [](double v) {
            return u8("About ") + juce::String(juce::roundToInt(8.0 * std::pow(2.0, v * 9.0))) + u8(" notes.");
        }));
        addQ(new QSlider(proc, "fidelity", "It quotes", "Snippets", "Long passages", [](double v) {
            return v < 0.3 ? u8("It freely recombines your phrases.") : v < 0.7 ? u8("It quotes short fragments.") : u8("It quotes long passages.");
        }));
        addQ(new QSlider(proc, "scale", "It stays in the scale", "Free", "Always", [](double v) {
            return u8("In the scale ") + juce::String(juce::roundToInt(v * 100)) + u8("% of the time.");
        }));
        gridChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("grid"), u8("Lock to the song's grid"),
                                                  juce::StringArray{"Free", "1/4", "1/8", "1/16"});
        gridChoice->labelHeight = 24;
        advInner.addAndMakeVisible(*gridChoice);
        heading("MIDI");
        sourceChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("source"), u8("Listens to"),
                                                    juce::StringArray{"Audio", "MIDI", "Audio + MIDI"});
        sourceChoice->labelHeight = 24;
        advInner.addAndMakeVisible(*sourceChoice);
        advLabel(u8("Listens to channel"), true);
        advLabel(u8("Answers on channel"), true);
        inBox.addItem("All", 1);
        for (int i = 1; i <= 16; ++i) inBox.addItem(juce::String(i), i + 1);
        inBox.setTitle(u8("MIDI channel it listens to"));
        advInner.addAndMakeVisible(inBox);
        inAtt = std::make_unique<ComboAtt>(proc.apvts, "midiin", inBox);
        for (int i = 1; i <= 16; ++i) outBox.addItem(juce::String(i), i);
        outBox.setTitle(u8("MIDI channel for answers"));
        outBox.onChange = [this] { setParam(proc, "channel", (float)outBox.getSelectedId()); };
        advInner.addAndMakeVisible(outBox);
        thruBtn.getProperties().set("desc", u8("One synth plays both your notes and its answers."));
        thruBtn.getProperties().set("topAlign", true);
        thruBtn.setDescription(u8("One synth plays both your notes and its answers."));
        advInner.addAndMakeVisible(thruBtn);
        thruAtt = std::make_unique<ButtonAtt>(proc.apvts, "thru", thruBtn);
    }

    void layoutAdvanced() {
        auto top = advanced.getLocalBounds().removeFromTop(56).reduced(10, 6);
        advBack.setBounds(top.removeFromLeft(110));
        advView.setBounds(advanced.getLocalBounds().withTrimmedTop(58).reduced(2, 2));
        const int w = advView.getWidth() - 12;
        int y = 4;
        auto place = [&](juce::Component& c, int h) { c.setBounds(14, y, w - 28, h); y += h + 8; };
        int hi = 0, li = 0;
        place(*headings[hi++], 28);
        place(*earChoice, 56);
        place(*band, 100);
        place(*advSliders[0], 74);
        place(*headings[hi++], 28);
        place(*advSliders[1], 74);
        place(*advSliders[2], 74);
        place(*advSliders[3], 74);
        place(*bendChoice, 70);
        place(*headings[hi++], 28);
        place(strip, 34);
        {
            const int cw = (w - 28 - 10) / 2;
            advLabels[li]->setBounds(14, y, cw, 22); advLabels[li + 1]->setBounds(14 + cw + 10, y, cw, 22); li += 2; y += 24;
            lowBox.setBounds(14, y, cw, 44); highBox.setBounds(14 + cw + 10, y, cw, 44); y += 50;
        }
        place(*advLabels[li++], 20);
        place(*headings[hi++], 28);
        place(*advSliders[4], 74);
        place(*advSliders[5], 74);
        place(*advSliders[6], 74);
        place(*gridChoice, 70);
        place(*headings[hi++], 28);
        place(*sourceChoice, 70);
        {
            const int cw = (w - 28 - 10) / 2;
            advLabels[li]->setBounds(14, y, cw, 22); advLabels[li + 1]->setBounds(14 + cw + 10, y, cw, 22); li += 2; y += 24;
            inBox.setBounds(14, y, cw, 44); outBox.setBounds(14 + cw + 10, y, cw, 44); y += 52;
        }
        place(thruBtn, 52);
        advInner.setSize(w, y + 10);
    }

    void syncAdvancedBoxes() {
        const int lo = juce::roundToInt(getParam(proc, "low")), hi = juce::roundToInt(getParam(proc, "high"));
        auto pick = [](juce::ComboBox& b, int m) {
            const int id = juce::jlimit(24, 108, (m + 6) / 12 * 12) + 1;
            if (b.getSelectedId() != id) b.setSelectedId(id, juce::dontSendNotification);
        };
        pick(lowBox, lo);
        pick(highBox, hi);
        const int ch = juce::roundToInt(getParam(proc, "channel"));
        if (outBox.getSelectedId() != ch) outBox.setSelectedId(ch, juce::dontSendNotification);
    }

    // ================================================================ right: routing
    juce::TextButton testBtn{"Play a test note (C4)"};
    juce::OwnedArray<juce::TextButton> hostBtns;
    bool tested = false;
    juce::Rectangle<int> checksR, stepsR;

    void buildRouting() {
        addChildComponent(routing);
        routing.setTitle("Routing");
        routing.setFocusContainerType(FocusContainerType::focusContainer);
        styleButton(routeBack, "left", false, 14.f);
        routeBack.setTitle(u8("Back to character"));
        routeBack.onClick = [this] { showView(View::Character); };
        routing.addAndMakeVisible(routeBack);
        styleButton(testBtn, "play", true, 15.f);
        testBtn.setDescription(u8("Sends a C4 on the output channel. You should hear it from your synth."));
        testBtn.onClick = [this] {
            proc.requestTestNote();
            tested = true;
            testBtn.setButtonText("Play the test note again");
            routing.repaint();
            juce::AccessibilityHandler::postAnnouncement(u8("Test note sent."), juce::AccessibilityHandler::AnnouncementPriority::medium);
        };
        routing.addAndMakeVisible(testBtn);
        const char* hosts[] = {"Reaper", "Ableton Live", "Standalone"};
        for (int i = 0; i < 3; ++i) {
            auto* b = hostBtns.add(new juce::TextButton(hosts[i]));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5200);
            b->setToggleState(i == settings.host, juce::dontSendNotification);
            b->onClick = [this, i] {
                settings.host = i;
                settings.save();
                for (int k = 0; k < hostBtns.size(); ++k) hostBtns[k]->setToggleState(k == i, juce::dontSendNotification);
                routing.repaint();
            };
            routing.addAndMakeVisible(b);
        }
        routing.setPaintingIsUnclipped(false);
    }

    void layoutRouting() {
        auto r = routing.getLocalBounds();
        auto top = r.removeFromTop(56).reduced(10, 6);
        routeBack.setBounds(top.removeFromLeft(110));
        r = r.reduced(14, 6);
        checksR = r.removeFromTop(3 * 66);
        r.removeFromTop(10);
        testBtn.setBounds(r.removeFromTop(50));
        r.removeFromTop(30);
        r.removeFromTop(24);
        auto tabs = r.removeFromTop(44);
        const int bw = (tabs.getWidth() + 4) / 3;
        for (int i = 0; i < hostBtns.size(); ++i)
            hostBtns[i]->setBounds(tabs.getX() + i * (bw - 2), tabs.getY(), i == 2 ? tabs.getRight() - (tabs.getX() + i * (bw - 2)) : bw, 44);
        stepsR = r.withTrimmedTop(-2).withHeight(3 * 64 + 20);
    }

    void paintRouting(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        if (!routing.isVisible()) return;
        auto off = routing.getPosition();
        ui::text(g, "Routing", juce::Rectangle<int>(off.x + 130, off.y + 6, 240, 44), L.body(16.f, true), p.fg);
        const float gate = getParam(proc, "gate");
        const int src = juce::roundToInt(getParam(proc, "source"));
        const bool audioOk = src == 1 || peak > gate;
        struct C { juce::String title, state, detail; bool good; };
        const int ch = juce::roundToInt(getParam(proc, "channel"));
        C checks[3] = {
            {"Incoming sound", audioOk ? u8("received") : u8("nothing"),
             src == 1 ? u8("Listening to MIDI: play your keyboard.")
                      : (audioOk ? u8("Level ") + juce::String(juce::roundToInt(peak)) + u8(" dB, above the threshold (") + juce::String(juce::roundToInt(gate)) + " dB)."
                                 : u8("Nothing above the threshold. Check the track input, or lower the threshold.")), audioOk},
            {u8("Answers sent"), juce::String(sentLastMinute) + u8(" notes in the last minute"), u8("On channel ") + juce::String(ch) + ".", sentLastMinute > 0},
            {u8("Received by your synth"), tested ? u8("your call") : u8("to check"),
             tested ? u8("Did you hear the C4? If not, follow the steps below.") : u8("The plugin can't see your synth: play the test note."), tested},
        };
        auto r = checksR.translated(off.x, off.y);
        g.setColour(p.line);
        g.drawRect(r.toFloat(), p.lineW);
        for (int i = 0; i < 3; ++i) {
            auto row = r.removeFromTop(66);
            if (i) { g.setColour(p.hair); g.fillRect(row.getX(), row.getY(), row.getWidth(), 1); }
            row = row.reduced(12, 10);
            auto badge = row.removeFromLeft(32).removeFromTop(32);
            g.setColour(checks[i].good ? p.fg : p.signal);
            g.fillRect(badge);
            ui::drawIcon(g, checks[i].good ? "check" : "warn", badge.toFloat().reduced(6.f), checks[i].good ? p.bg : juce::Colour(0xff111111), 3.f);
            row.removeFromLeft(12);
            ui::text(g, checks[i].title + u8(" · ") + checks[i].state, row.removeFromTop(20), L.body(15.f, true), p.fg);
            ui::text(g, checks[i].detail, row, L.body(13.f), p.muted, juce::Justification::topLeft, 2);
        }
        auto t = testBtn.getBounds().translated(off.x, off.y + 54);
        if (tested) ui::text(g, u8("Test note sent on channel ") + juce::String(ch) + ".", t.withHeight(20), L.body(13.f), p.fg);
        ui::text(g, u8("Connect the output in…"), t.translated(0, 26).withHeight(22), L.body(15.f, true), p.fg);
        static const char* steps[3][3] = {
            {"On the plugin's track: Route button › Add new send to the synth track.",
             "In that send: Audio set to None, MIDI set to All › All.",
             "If the synth filters channels, use the same channel as \u201cAnswers on channel\u201d."},
            {"Live doesn't receive MIDI from a plugin: open LPF Dialogue as a standalone app.",
             "In its settings, MIDI output: IAC bus (Mac) or loopMIDI (Windows).",
             "In Live, on the synth track: MIDI From › that bus, Monitor set to In."},
            {"Options › Audio/MIDI Settings: pick the audio input and the MIDI output.",
             "Output to the Move over USB, or to a virtual bus.",
             "Use headphones so its answers don't go back into the mic."}};
        auto s = stepsR.translated(off.x, off.y);
        g.setColour(p.bg);
        g.fillRect(s);
        g.setColour(p.line);
        g.drawRect(s.toFloat(), p.lineW);
        s = s.reduced(12, 10);
        for (int i = 0; i < 3; ++i) {
            auto row = s.removeFromTop(64);
            ui::text(g, juce::String(i + 1) + ".", row.removeFromLeft(20), L.mono(13.f), p.fg, juce::Justification::topLeft);
            ui::text(g, u8(steps[settings.host][i]), row, L.body(14.f), p.fg, juce::Justification::topLeft, 3);
        }
    }

    void paintRightHeader(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        auto off = rightR;
        if (currentView == View::Character) {
            ui::text(g, u8("Its character"), juce::Rectangle<int>(off.getX() + 14, off.getY() + 10, 140, 24), L.body(16.f, true), p.fg);
            ui::text(g, u8("· ") + (presetIndex >= 0 ? u8(ui::presets()[(size_t)presetIndex].name) : juce::String("Custom")),
                     juce::Rectangle<int>(off.getX() + 140, off.getY() + 10, 220, 24), L.body(14.f), p.muted);
            g.setColour(p.line);
            g.fillRect(off.getX(), off.getBottom() - 50, off.getWidth(), (int)p.lineW);
        } else if (currentView == View::Advanced) {
            ui::text(g, u8("Advanced settings"), juce::Rectangle<int>(off.getX() + 130, off.getY() + 6, 240, 44), L.body(16.f, true), p.fg);
        }
        if (currentView != View::Character) {
            g.setColour(p.line);
            g.fillRect(off.getX(), off.getY() + 56, off.getWidth(), (int)p.lineW);
        }
    }

public:
    void showView(View v) {
        currentView = v;
        character.setVisible(v == View::Character);
        advanced.setVisible(v == View::Advanced);
        routing.setVisible(v == View::Routing);
        if (v == View::Advanced) syncAdvancedBoxes();
        repaint();
    }

private:
    // ================================================================ footer
    juce::TextButton diagBtn{u8("Nothing coming out? Routing check")};

    void buildFooter() {
        styleButton(diagBtn, "right", false, 13.5f);
        diagBtn.getProperties().set("flat", true);
        diagBtn.getProperties().set("iconRight", true);
        diagBtn.onClick = [this] { showView(View::Routing); };
        addAndMakeVisible(diagBtn);
    }

    void paintFooter(juce::Graphics& g, ui::Look& L, const ui::Palette& p) {
        auto r = juce::Rectangle<int>(0, H - kFooterH, W, kFooterH);
        g.setColour(p.panel);
        g.fillRect(r);
        g.setColour(p.line);
        g.fillRect(0, r.getY(), W, (int)p.lineW);
        r = r.reduced(14, 0);
        auto hint = [&](const juce::String& k, const juce::String& t) {
            const int kw = 10 + (int)juce::GlyphArrangement::getStringWidth(L.mono(11.5f), k);
            auto kr = r.removeFromLeft(kw).withSizeKeepingCentre(kw, 20);
            g.setColour(p.bg);
            g.fillRect(kr);
            g.setColour(p.line);
            g.drawRect(kr.toFloat(), 1.5f);
            ui::text(g, k, kr, L.mono(11.5f), p.fg, juce::Justification::centred);
            const int tw = 8 + (int)juce::GlyphArrangement::getStringWidth(L.body(13.f), t);
            ui::text(g, t, r.removeFromLeft(tw).withTrimmedLeft(6), L.body(13.f), p.muted);
            r.removeFromLeft(14);
        };
        hint("Tab", u8("next control"));
        hint(u8("← →"), u8("adjust"));
        hint("Shift", u8("fine"));
        hint("Space", u8("answer on/off"));
        hint("F", u8("freeze"));
        hint("?", "help");
    }

    // ================================================================ welcome
    struct Overlay : juce::Component {
        std::function<void(juce::Graphics&)> painter;
        bool dim = false;
        void paint(juce::Graphics& g) override {
            if (dim) g.fillAll(juce::Colours::black.withAlpha(0.55f));
            if (painter) painter(g);
        }
    };
    Overlay welcome;
    juce::OwnedArray<Card> welcomeSources, welcomePresets;
    juce::TextButton welcomeMeasure, welcomeStart;
    juce::Rectangle<int> wMain, wAside;

    void openWelcome() { welcome.setVisible(true); welcome.toFront(true); if (welcomeSources.size()) welcomeSources[0]->grabKeyboardFocus(); }

    void buildWelcome() {
        addChildComponent(welcome);
        welcome.setTitle("Welcome");
        welcome.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        welcome.painter = [this](juce::Graphics& g) { paintWelcome(g); };
        struct S { const char* t; const char* d; const char* icon; };
        const S srcs[] = {{"An instrument", "Guitar, piano, brass… through a mic or your audio interface.", "guitar"},
                          {"Voice or birds", "High, fast, gliding sounds. Bright listening.", "mic"},
                          {"A MIDI keyboard", "Exact notes, instant answers.", "keys"},
                          {"Both", "It listens to audio and MIDI at the same time.", "both"}};
        for (int i = 0; i < 4; ++i) {
            auto* c = welcomeSources.add(new Card(u8(srcs[i].t), u8(srcs[i].d), srcs[i].icon));
            c->setRadioGroupId(5300);
            c->onClick = [this, i] {
                setParam(proc, "source", i == 2 ? 1.f : (i == 3 ? 2.f : 0.f));
                setParam(proc, "mode", i == 1 ? 1.f : 0.f);
                setParam(proc, "lowcut", i == 1 ? 100.f : 55.f);
                setParam(proc, "highcut", i == 1 ? 10000.f : 2000.f);
                syncWelcome();
            };
            welcome.addAndMakeVisible(c);
        }
        styleButton(welcomeMeasure, "wave", false, 15.f);
        welcomeMeasure.setButtonText(u8("Listen to 3 s of silence"));
        welcomeMeasure.onClick = [this] { proc.requestCalibration(); };
        welcome.addAndMakeVisible(welcomeMeasure);
        for (int i = 0; i < (int)ui::presets().size(); ++i) {
            auto* c = welcomePresets.add(new Card(u8(ui::presets()[(size_t)i].name), u8(ui::presets()[(size_t)i].desc)));
            c->setRadioGroupId(5301);
            c->onClick = [this, i] { applyPreset(i); };
            welcome.addAndMakeVisible(c);
        }
        styleButton(welcomeStart, "right", true, 16.f);
        welcomeStart.getProperties().set("iconRight", true);
        welcomeStart.setButtonText(u8("Start playing"));
        welcomeStart.onClick = [this] {
            settings.welcomeDone = true;
            settings.save();
            welcome.setVisible(false);
            respondBtn.grabKeyboardFocus();
        };
        welcome.addAndMakeVisible(welcomeStart);
        syncWelcome();
    }

    void syncWelcome() {
        const int src = juce::roundToInt(getParam(proc, "source"));
        const bool bright = getParam(proc, "mode") > 0.5f;
        const int idx = src == 1 ? 2 : (src == 2 ? 3 : (bright ? 1 : 0));
        for (int i = 0; i < welcomeSources.size(); ++i) welcomeSources[i]->setToggleState(i == idx, juce::dontSendNotification);
    }

    void layoutWelcome() {
        auto r = welcome.getLocalBounds().reduced(16);
        wAside = r.removeFromRight(300);
        r.removeFromRight(16);
        wMain = r;
        r = r.reduced(24, 18);
        r.removeFromTop(70);       // title
        r.removeFromTop(36);       // step 1 title
        auto row = r.removeFromTop(118);
        const int cw = (row.getWidth() - 30) / 4;
        for (int i = 0; i < welcomeSources.size(); ++i) welcomeSources[i]->setBounds(row.getX() + i * (cw + 10), row.getY(), cw, row.getHeight());
        r.removeFromTop(14);
        r.removeFromTop(36);       // step 2 title
        auto m = r.removeFromTop(52);
        welcomeMeasure.setBounds(m.removeFromLeft(250));
        r.removeFromTop(14);
        r.removeFromTop(36);       // step 3 title
        auto grid = r.removeFromTop(132);
        const int pw = (grid.getWidth() - 20) / 3;
        for (int i = 0; i < welcomePresets.size(); ++i)
            welcomePresets[i]->setBounds(grid.getX() + (i % 3) * (pw + 10), grid.getY() + (i / 3) * 68, pw, 62);
        welcomeStart.setBounds(r.getX(), wMain.getBottom() - 18 - 52, 240, 52);
    }

    void paintWelcome(juce::Graphics& g) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        g.fillAll(p.bg);
        panel(g, p, wMain);
        auto r = wMain.reduced(24, 18);
        ui::text(g, u8("Welcome. You play, it answers."), r.removeFromTop(38), L.mono(24.f), p.fg);
        ui::text(g, u8("Three questions and you're playing together. Everything can be changed later; nothing can break."), r.removeFromTop(32).withTrimmedBottom(8), L.body(16.f), p.muted);
        auto step = [&](const char* n, const juce::String& t) {
            auto s = r.removeFromTop(36);
            auto b = s.removeFromLeft(28).withSizeKeepingCentre(28, 28);
            g.setColour(p.fg);
            g.fillRect(b);
            ui::text(g, n, b, L.mono(14.f), p.bg, juce::Justification::centred);
            ui::text(g, t, s.withTrimmedLeft(10), L.body(18.f, true), p.fg);
        };
        step("1", u8("What will you play?"));
        r.removeFromTop(118 + 14);
        step("2", u8("Teach it your silence"));
        auto m = r.removeFromTop(52);
        m.removeFromLeft(262);
        const int cs = proc.calibState.load();
        juce::String t = cs == 1 ? u8("Measuring… don't play (") + juce::String(juce::roundToInt(proc.calibProgress.load() * 100)) + " %)."
                       : cs == 2 ? u8("Room noise ") + juce::String(juce::roundToInt(proc.calibNoiseDb.load())) + u8(" dB. Threshold set just above, at ")
                                       + juce::String(juce::roundToInt(proc.calibResultDb.load())) + u8(" dB. Your breathing won't be mistaken for a note.")
                                 : u8("Don't play for 3 seconds: it measures the room noise and sets the threshold just above it.");
        ui::text(g, t, m, L.body(14.f), p.fg, juce::Justification::centredLeft, 2);
        r.removeFromTop(14);
        step("3", u8("What kind of partner do you want?"));
        ui::text(g, u8("It will start answering after your first phrase."), juce::Rectangle<int>(wMain.getX() + 290, wMain.getBottom() - 70, 420, 52),
                 L.body(14.f), p.muted);

        // aside
        g.setColour(p.fg);
        g.fillRect(wAside);
        auto a = wAside.reduced(22, 22);
        ui::text(g, u8("How it works"), a.removeFromTop(30), L.mono(17.f), p.bg);
        a.removeFromTop(10);
        const char* pts[3][2] = {{"It listens", "To your instrument, voice or keyboard, and remembers what you play."},
                                 {"It waits its turn", "Longer or shorter, depending on its patience."},
                                 {"It answers in MIDI", "With your own phrases, in your scale. The sound is whatever synth you connect."}};
        for (int i = 0; i < 3; ++i) {
            auto row = a.removeFromTop(104);
            ui::text(g, "0" + juce::String(i + 1), row.removeFromLeft(34), L.mono(14.f), p.signal, juce::Justification::topLeft);
            ui::text(g, u8(pts[i][0]), row.removeFromTop(22), L.body(15.f, true), p.bg, juce::Justification::topLeft);
            ui::text(g, u8(pts[i][1]).trimCharactersAtStart(", "), row, L.body(14.f), p.bg, juce::Justification::topLeft, 4);
        }
        ui::text(g, u8("Everything happens on your computer. No sound goes to the internet."), a.removeFromBottom(44), L.body(13.f), p.bg.withAlpha(0.85f),
                 juce::Justification::bottomLeft, 2);
    }

    // ================================================================ dialogs (help, display)
    Overlay help, display;
    juce::OwnedArray<juce::Component> helpItems;
    juce::TextButton helpClose, displayClose, helpWelcome{u8("Show the welcome again")}, helpDiag{u8("Routing check")};
    juce::OwnedArray<juce::TextButton> themeBtns, sizeBtns;
    juce::OwnedArray<juce::ToggleButton> displayToggles;
    juce::Rectangle<int> dialogR;
    juce::Component* lastFocusBeforeDialog = nullptr;

    void openDialog(Overlay& o) {
        lastFocusBeforeDialog = juce::Component::getCurrentlyFocusedComponent();
        o.setVisible(true);
        o.toFront(true);
        (&o == &help ? helpClose : displayClose).grabKeyboardFocus();
    }
    void closeDialog(Overlay& o) {
        o.setVisible(false);
        if (lastFocusBeforeDialog != nullptr && isParentOf(lastFocusBeforeDialog) && lastFocusBeforeDialog->isShowing())
            lastFocusBeforeDialog->grabKeyboardFocus();
    }

    void dialogFrame(juce::Graphics& g, const juce::String& title, const juce::String& sub) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        g.setColour(p.panel);
        g.fillRect(dialogR);
        g.setColour(p.line);
        g.fillRect(dialogR.getX(), 0, (int)p.lineW, H);
        auto r = dialogR.reduced(22, 16);
        ui::text(g, title, r.removeFromTop(28), L.mono(18.f), p.fg);
        ui::text(g, sub, r.removeFromTop(20), L.body(13.f), p.muted);
        g.setColour(p.line);
        g.fillRect(dialogR.getX(), 72, dialogR.getWidth(), (int)p.lineW);
    }

    void buildHelp() {
        addChildComponent(help);
        help.setTitle("Help");
        help.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        help.painter = [this](juce::Graphics& g) { paintHelp(g); };
        styleButton(helpClose, "close");
        helpClose.setTitle("Close help");
        helpClose.onClick = [this] { closeDialog(help); };
        help.addAndMakeVisible(helpClose);
        styleButton(helpWelcome, "right", false, 14.f);
        helpWelcome.onClick = [this] { closeDialog(help); openWelcome(); };
        help.addAndMakeVisible(helpWelcome);
        styleButton(helpDiag, "right", false, 14.f);
        helpDiag.onClick = [this] { closeDialog(help); showView(View::Routing); };
        help.addAndMakeVisible(helpDiag);
    }

    void layoutDialog(Overlay& o, juce::OwnedArray<juce::Component>&) {
        dialogR = juce::Rectangle<int>(W - 460, 0, 460, H);
        (&o == &help ? helpClose : displayClose).setBounds(W - 22 - 44, 14, 44, 44);
        if (&o == &help) {
            helpWelcome.setBounds(dialogR.getX() + 22, H - 22 - 104, dialogR.getWidth() - 44, 48);
            helpDiag.setBounds(dialogR.getX() + 22, H - 22 - 48, dialogR.getWidth() - 44, 48);
        }
    }

    void paintHelp(juce::Graphics& g) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
        dialogFrame(g, "HELP", u8("How it plays with you"));
        auto r = dialogR.reduced(22, 0).withTrimmedTop(90);
        const char* pts[4][2] = {
            {"It listens", "It remembers your notes, rhythms and dynamics, and recognises the scale and the chord."},
            {"It waits its turn", "\u201cWhen does it answer?\u201d sets how many notes or how much silence it waits for. The countdown shows when it's about to speak."},
            {"It answers in MIDI", "With your own phrases, quoted or reshuffled, imitated or contradicted. The sound comes from the synth on its output."},
            {"The conversation", "You and It blocks for the last 12 seconds, with the reason for each answer."}};
        for (auto& pt : pts) {
            auto row = r.removeFromTop(86);
            ui::text(g, u8(pt[0]), row.removeFromTop(22), L.body(15.5f, true), p.fg);
            ui::text(g, u8(pt[1]), row, L.body(14.f), p.fg, juce::Justification::topLeft, 3);
        }
        r.removeFromTop(6);
        ui::text(g, "Keyboard", r.removeFromTop(24), L.body(15.5f, true), p.fg);
        paintShortcuts(g, r.removeFromTop(5 * 30));
    }

    void paintShortcuts(juce::Graphics& g, juce::Rectangle<int> r) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        const char* keys[10][2] = {{"Tab", "Next control"}, {"Shift+Tab", "Previous control"}, {"← →", "Adjust"}, {"Shift", "Fine adjust"},
                                   {"Space", "Answer on / off"}, {"F", "Freeze memory"}, {"S", "Measure silence"}, {"1 to 6", "Pick a character"},
                                   {"Del", "Back to default"}, {"Esc", "Close, go back"}};
        g.setColour(p.hair);
        g.drawRect(r.toFloat(), 1.5f);
        const int cw = r.getWidth() / 2, rh = r.getHeight() / 5;
        for (int i = 0; i < 10; ++i) {
            auto c = juce::Rectangle<int>(r.getX() + (i % 2) * cw, r.getY() + (i / 2) * rh, cw, rh).reduced(8, 4);
            const int kw = 10 + (int)juce::GlyphArrangement::getStringWidth(L.mono(11.5f), u8(keys[i][0]));
            auto keyCol = c.removeFromLeft(72);
            auto k = keyCol.withWidth(kw).withSizeKeepingCentre(kw, 20).withX(keyCol.getX());
            g.setColour(p.line);
            g.drawRect(k.toFloat(), 1.5f);
            ui::text(g, u8(keys[i][0]), k, L.mono(11.5f), p.fg, juce::Justification::centred);
            ui::text(g, u8(keys[i][1]), c, L.body(13.5f), p.fg);
        }
    }

    void buildDisplay() {
        addChildComponent(display);
        display.setTitle(u8("Display and accessibility"));
        display.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        display.painter = [this](juce::Graphics& g) { paintDisplay(g); };
        styleButton(displayClose, "close");
        displayClose.setTitle(u8("Close display settings"));
        displayClose.onClick = [this] { closeDialog(display); };
        display.addAndMakeVisible(displayClose);
        const char* themes[] = {"Paper", "Ink", "Max contrast"};
        for (int i = 0; i < 3; ++i) {
            auto* b = themeBtns.add(new juce::TextButton(themes[i]));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5400);
            b->setDescription(u8("Theme"));
            b->onClick = [this, i] { settings.theme = (ui::Theme)i; settings.save(); };
            display.addAndMakeVisible(b);
        }
        const char* sizes[] = {"100 %", "125 %", "150 %"};
        for (int i = 0; i < 3; ++i) {
            auto* b = sizeBtns.add(new juce::TextButton(sizes[i]));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5401);
            b->setDescription(u8("Text size: the window grows with the text."));
            b->onClick = [this, i] { editor.applyScale(1.f + 0.25f * (float)i); };
            display.addAndMakeVisible(b);
        }
        const char* toggles[4][2] = {
            {"Announce what it does", "Your screen reader says \u201clistening\u201d, \u201canswering\u201d, and the scale or chord when they change."},
            {"Visual signal when it answers", "A steady orange frame around the plugin while it answers, visible from afar, no flashing."},
            {"Reduce motion", "The conversation moves in steps instead of scrolling."},
            {"Shapes as well as colours", "You \u25a0 and it \u25c6 differ by shape too, not only by colour."}};
        for (int i = 0; i < 4; ++i) {
            auto* t = displayToggles.add(new juce::ToggleButton(u8(toggles[i][0])));
            t->getProperties().set("desc", u8(toggles[i][1]));
            t->getProperties().set("topAlign", true);
            t->setDescription(u8(toggles[i][1]));
            t->onClick = [this, i, t] {
                const bool v = t->getToggleState();
                if (i == 0) settings.speak = v; else if (i == 1) settings.flash = v; else if (i == 2) settings.reduceMotion = v; else settings.shapes = v;
                settings.save();
            };
            display.addAndMakeVisible(t);
        }
        syncDisplay();
    }

public:
    void syncDisplay() {
        for (int i = 0; i < themeBtns.size(); ++i) themeBtns[i]->setToggleState(i == (int)settings.theme, juce::dontSendNotification);
        const int si = juce::jlimit(0, 2, juce::roundToInt((settings.scale - 1.f) / 0.25f));
        const bool exact = std::abs(settings.scale - (1.f + 0.25f * si)) < 0.02f;
        for (int i = 0; i < sizeBtns.size(); ++i) sizeBtns[i]->setToggleState(exact && i == si, juce::dontSendNotification);
        const bool vals[] = {settings.speak, settings.flash, settings.reduceMotion, settings.shapes};
        for (int i = 0; i < displayToggles.size(); ++i) displayToggles[i]->setToggleState(vals[i], juce::dontSendNotification);
        repaint();
    }

private:
    void layoutDisplay() {
        displayClose.setBounds(W - 22 - 44, 14, 44, 44);
        auto r = juce::Rectangle<int>(W - 460, 0, 460, H).reduced(22, 0).withTrimmedTop(84);
        r.removeFromTop(28);
        auto row = r.removeFromTop(48);
        auto lay3 = [](juce::OwnedArray<juce::TextButton>& bs, juce::Rectangle<int> rr) {
            const int bw = (rr.getWidth() + 4) / 3;
            for (int i = 0; i < 3; ++i)
                bs[i]->setBounds(rr.getX() + i * (bw - 2), rr.getY(), i == 2 ? rr.getRight() - (rr.getX() + i * (bw - 2)) : bw, rr.getHeight());
        };
        lay3(themeBtns, row);
        r.removeFromTop(16 + 28);
        lay3(sizeBtns, r.removeFromTop(48));
        r.removeFromTop(26 + 26);
        for (auto* t : displayToggles) { t->setBounds(r.removeFromTop(64)); r.removeFromTop(6); }
    }

    void paintDisplay(juce::Graphics& g) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        g.fillAll(juce::Colours::black.withAlpha(0.55f));
        dialogFrame(g, u8("DISPLAY AND ACCESSIBILITY"), u8("Kept for all your projects"));
        auto r = dialogR.reduced(22, 0).withTrimmedTop(84);
        ui::text(g, u8("Theme"), r.removeFromTop(28), L.body(15.5f, true), p.fg);
        r.removeFromTop(48 + 16);
        ui::text(g, "Text size", r.removeFromTop(28), L.body(15.5f, true), p.fg);
        r.removeFromTop(48);
        ui::text(g, u8("The window grows with the text: nothing gets cut off."), r.removeFromTop(26), L.body(13.f), p.muted);
        ui::text(g, "Seeing and hearing", r.removeFromTop(26), L.body(15.5f, true), p.fg);
        r.removeFromTop(4 * 70);
        r.removeFromTop(8);
        ui::text(g, "Keyboard", r.removeFromTop(24), L.body(15.5f, true), p.fg);
        paintShortcuts(g, r.removeFromTop(5 * 30));
    }

    // ================================================================ live repaint + announcements
    int lastPhase = -1, lastKeyRoot = -2, lastScale = -1, lastChordRoot = -2, lastChord = -1;
    double lastAnnounce = 0.0, harmonyChangedAt = 0.0;
    bool harmonyPending = false;

    bool lastAnswering = false;
    int liveTick = 0;
    void repaintLive() {
        ++liveTick;
        repaint(statusR);
        repaint(hearingR);
        if (timelineDirty) repaint(timelineR);
        if (routing.isVisible() && liveTick % 8 == 0) rightPainter.repaint();
        if (character.isVisible() && liveTick % 15 == 0) for (auto* q : charSliders) q->repaint();
        if (welcome.isVisible()) welcome.repaint();
        if (answeringNow != lastAnswering) { lastAnswering = answeringNow; repaint(); }
        if (advanced.isVisible() && liveTick % 15 == 0) syncAdvancedBoxes();
    }

    void announce() {
        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto st = statusText();
        setTitle(u8("LPF Dialogue. ") + st.text);
        if (!settings.speak) { lastPhase = phase; return; }
        if (phase != lastPhase && t - lastAnnounce > 1.0) {
            const juce::String w = phase == (int)lpfd::Engine::Phase::Answering ? u8("Answering")
                                 : phase == (int)lpfd::Engine::Phase::Listening ? u8("Listening")
                                 : phase == (int)lpfd::Engine::Phase::Waiting ? u8("Waiting") : juce::String();
            if (w.isNotEmpty()) juce::AccessibilityHandler::postAnnouncement(w, juce::AccessibilityHandler::AnnouncementPriority::low);
            lastAnnounce = t;
            lastPhase = phase;
        }
        if (keyRoot != lastKeyRoot || scaleType != lastScale || chordRoot != lastChordRoot || chordType != lastChord) {
            lastKeyRoot = keyRoot; lastScale = scaleType; lastChordRoot = chordRoot; lastChord = chordType;
            harmonyChangedAt = t;
            harmonyPending = true;
        }
        if (harmonyPending && t - harmonyChangedAt > 2.0 && keyRoot >= 0) {
            harmonyPending = false;
            juce::AccessibilityHandler::postAnnouncement(u8("Scale ") + keyText(keyRoot, scaleType) + u8(", chord ") + chordText(chordRoot, chordType),
                                                         juce::AccessibilityHandler::AnnouncementPriority::low);
        }
    }

    // ================================================================ state
    struct RightPainter : juce::Component {
        Content* c = nullptr;
        void paint(juce::Graphics& g) override {
            g.setOrigin(-getX(), -getY());
            c->paintRight(g);
        }
    } rightPainter;

public:
    void paintRight(juce::Graphics& g) {
        auto& L = lookOf(*this);
        paintRightHeader(g, L, L.pal());
        paintRouting(g, L, L.pal());
    }

private:
    DialogueProcessor& proc;
    ui::DisplaySettings& settings;
    DialogueEditor& editor;
    bool keyboardMode = false;
    int phase = 0, heard = 0, needed = 1, memSize = 0, keyRoot = -1, scaleType = 0, chordRoot = -1, chordType = 0;
    float waitProg = 0.f, waitRemain = 0.f, silenceWait = 1.f, level = -120.f, peak = -120.f, pitch = 0.f;
    bool hearing = false, answeringNow = false;
    int64_t nowS = 0;
    double sr = 44100.0;
    int sentLastMinute = 0;

    friend class DialogueEditor;
};

// =====================================================================================
DialogueEditor::DialogueEditor(DialogueProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    look.setPalette(ui::palette(settings->theme));
    setLookAndFeel(&look);
    content = std::make_unique<Content>(proc, *settings, *this, look);
    addAndMakeVisible(*content);

    setWantsKeyboardFocus(true);
    const float startScale = settings->scale;
    constructing = true;
    setResizable(true, true);
    setResizeLimits((int)(kW * 0.75f), (int)(kH * 0.75f), kW * 2, kH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)kW / (double)kH);
    setSize(juce::roundToInt(kW * startScale), juce::roundToInt(kH * startScale));
    constructing = false;
    juce::Desktop::getInstance().addFocusChangeListener(this);
    settings->addChangeListener(this);
    startTimerHz(30);
}

DialogueEditor::~DialogueEditor() {
    settings->removeChangeListener(this);
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    content.reset();
    setLookAndFeel(nullptr);
}

void DialogueEditor::applyScale(float s) {
    settings->scale = s;
    settings->save();
    setSize(juce::roundToInt(kW * s), juce::roundToInt(kH * s));
}

void DialogueEditor::resized() {
    if (!content) return;
    const float s = (float)getWidth() / (float)kW;
    content->setTransform(juce::AffineTransform::scale(s));
    content->setBounds(0, 0, kW, kH);
    if (!constructing && std::abs(s - settings->scale) > 0.01f) {
        settings->scale = s;
        content->syncDisplay();
    }
}

void DialogueEditor::paint(juce::Graphics& g) { g.fillAll(look.pal().bg); }

bool DialogueEditor::keyPressed(const juce::KeyPress& k) {
    content->setKeyboardMode(true);
    if (content->shortcut(k)) return true;
    return false;
}

void DialogueEditor::timerCallback() {
    ++tick;
    content->refresh(tick % 8 == 0);
}

void DialogueEditor::globalFocusChanged(juce::Component*) {
    if (!juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) content->setKeyboardMode(true);
    content->repaint();
}

void DialogueEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
    content->applyTheme();
    content->syncDisplay();
    look.setPalette(ui::palette(settings->theme));
    repaint();
}
