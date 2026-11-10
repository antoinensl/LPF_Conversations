// LPF Dialogue — editor: a conversation you can read, settings in plain words.
#include "PluginEditor.h"

using ui::u8;
using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

namespace {

constexpr int W = DialogueEditor::kW, H = DialogueEditor::kH;
constexpr int kHeaderH = 64, kFooterH = 32;

juce::String fmtS(float s) { return juce::String(s, 1).replaceCharacter('.', ',') + " s"; }

juce::String noteName(int m) {
    static const char* n[] = {"Do", "Do#", "Ré", "Mib", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "Sib", "Si"};
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
        ui::text(g, u8("Répondre"), r.removeFromTop(r.getHeight() / 2).withTrimmedTop(4), L.body(12.f), ink, juce::Justification::bottomLeft);
        ui::text(g, on ? "OUI" : "NON", r.withTrimmedBottom(4), L.body(15.f, true), ink, juce::Justification::topLeft);
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
        ui::text(g, u8("Un partenaire qui écoute et répond"), {16, 36, 230, 18}, L.body(12.5f), p.muted);

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
    juce::TextButton helpBtn{"Aide"}, displayBtn{"Affichage"};

    void buildHeader() {
        const char* labels[] = {"Il écoute", "Il répond sur", "Caractère"};
        for (int i = 0; i < 3; ++i) {
            auto* c = chips.add(new StepChip(juce::String(i + 1), u8(labels[i])));
            c->onClick = [this, i] {
                if (i == 1) showView(View::Routing);
                else openWelcome();
            };
            addAndMakeVisible(c);
        }
        respondBtn.setTitle(u8("Répondre"));
        respondBtn.setDescription(u8("Coupé : il écoute et apprend sans répondre. Raccourci : Espace."));
        addAndMakeVisible(respondBtn);
        respondAtt = std::make_unique<ButtonAtt>(proc.apvts, "respond", respondBtn);
        styleButton(helpBtn, "help");
        styleButton(displayBtn, "display");
        helpBtn.onClick = [this] { openDialog(help); };
        displayBtn.onClick = [this] { openDialog(display); };
        helpBtn.setDescription(u8("Comment ça marche, raccourcis clavier. Raccourci : ?"));
        displayBtn.setDescription(u8("Thème, taille du texte, accessibilité."));
        addAndMakeVisible(helpBtn);
        addAndMakeVisible(displayBtn);
    }

    void updateChips() {
        const int src = juce::roundToInt(getParam(proc, "source"));
        const bool bright = getParam(proc, "mode") > 0.5f;
        juce::String s = src == 1 ? "Clavier MIDI" : (src == 2 ? "Audio + MIDI" : (bright ? u8("Audio · Bright") : u8("Audio · instrument")));
        juce::String r = u8("canal ") + juce::String(juce::roundToInt(getParam(proc, "channel")));
        juce::String c = presetIndex >= 0 ? u8(ui::presets()[(size_t)presetIndex].name) : juce::String("Perso");
        const juce::String vals[] = {s, r, c};
        for (int i = 0; i < 3; ++i)
            if (chips[i]->value != vals[i]) {
                chips[i]->value = vals[i];
                chips[i]->setTitle(chips[i]->label + " : " + vals[i]);
                chips[i]->repaint();
            }
    }

    // ================================================================ status
    juce::TextButton freezeBtn{"Figer"}, clearBtn{u8("Effacer…")};
    std::unique_ptr<ButtonAtt> freezeAtt;
    bool clearArmed = false;
    juce::Rectangle<int> statusR, timelineR, hearingR, rightR, body;

    void buildStatus() {
        freezeBtn.setClickingTogglesState(true);
        styleButton(freezeBtn, {}, false, 14.f);
        freezeBtn.setTitle(u8("Figer la mémoire"));
        freezeBtn.setDescription(u8("Il n'apprend plus rien, mais continue d'écouter et de répondre. Raccourci : F."));
        freezeAtt = std::make_unique<ButtonAtt>(proc.apvts, "freeze", freezeBtn);
        addAndMakeVisible(freezeBtn);
        styleButton(clearBtn, {}, false, 14.f);
        clearBtn.setTitle(u8("Effacer la mémoire"));
        clearBtn.setDescription(u8("Oublie tout ce qu'il a entendu. Demande une confirmation."));
        clearBtn.onClick = [this] {
            if (!clearArmed) {
                clearArmed = true;
                clearBtn.setButtonText(u8("Confirmer"));
                clearBtn.getProperties().set("accent", true);
                clearBtn.repaint();
                juce::Component::SafePointer<Content> safe(this);
                juce::Timer::callAfterDelay(3000, [safe] {
                    if (safe != nullptr) safe->disarmClear();
                });
            } else {
                proc.requestClear();
                disarmClear();
                juce::AccessibilityHandler::postAnnouncement(u8("Mémoire effacée."), juce::AccessibilityHandler::AnnouncementPriority::medium);
            }
        };
        addAndMakeVisible(clearBtn);
    }

    void disarmClear() {
        clearArmed = false;
        clearBtn.setButtonText(u8("Effacer…"));
        clearBtn.getProperties().remove("accent");
        clearBtn.repaint();
    }

    struct StatusText { juce::String word, iconName, text, sub; bool countdown = false; int style = 0; };

    StatusText statusText() const {
        StatusText s;
        const bool respond = getParam(const_cast<DialogueProcessor&>(proc), "respond") > 0.5f;
        const float interrupt = getParam(const_cast<DialogueProcessor&>(proc), "interruption");
        const juce::String key = keyRoot >= 0 ? u8("en ") + keyText(keyRoot, scaleType) : juce::String();
        switch ((lpfd::Engine::Phase)phase) {
        case lpfd::Engine::Phase::Listening:
            s = {u8("ÉCOUTE"), "listen", u8("Il vous écoute."), {}, false, 1};
            if (!respond) s.sub = u8("Répondre est coupé : il apprend sans répondre.");
            else if (interrupt <= 0.01f || needed >= 99) s.sub = juce::String(heard) + u8(" notes entendues. Il attend la fin de votre phrase.");
            else s.sub = juce::String(heard) + u8(" notes entendues. Il peut répondre à partir de ") + juce::String(needed) + ".";
            break;
        case lpfd::Engine::Phase::Waiting:
            s = {u8("ATTEND"), "wait", respond ? u8("Phrase terminée. Il répond dans ") + fmtS(waitRemain) + "."
                                               : u8("Phrase terminée. Répondre est coupé."), {}, respond, 2};
            break;
        case lpfd::Engine::Phase::Answering:
            s = {u8("RÉPOND"), "answer", u8("Il vous répond") + (key.isNotEmpty() ? ", " + key : juce::String()) + ".",
                 interrupt < 0.5f ? u8("Il se taira si vous reprenez.") : u8("Il continue même si vous reprenez."), false, 3};
            break;
        case lpfd::Engine::Phase::Silence:
        default:
            if (memSize == 0 && peak < getParam(const_cast<DialogueProcessor&>(proc), "gate"))
                s = {u8("SILENCE"), "silence", u8("Il n'entend rien."), u8("Jouez quelque chose, ou baissez le seuil de silence si votre son est faible."), false, 0};
            else
                s = {u8("À VOUS"), "listen", u8("À vous de jouer."), respond ? u8("Il répondra quand vous aurez joué.") : u8("Répondre est coupé."), false, 0};
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
        ui::text(g, u8("Mémoire"), mem.removeFromTop(18), L.body(13.f), p.muted);
        const bool frozen = getParam(proc, "freeze") > 0.5f;
        ui::text(g, juce::String(memSize) + " notes" + (frozen ? u8(" · figée") : juce::String()), mem.removeFromTop(26), L.mono(17.f), p.fg);

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
        ui::text(g, u8("La conversation"), head.removeFromLeft(140), L.body(16.f, true), p.fg);
        ui::text(g, u8("· 12 dernières secondes"), head.removeFromLeft(200), L.body(14.f), p.muted);
        {
            auto lg = head.removeFromRight(150);
            auto a = lg.removeFromLeft(75), b = lg;
            g.setColour(p.fg);
            g.fillRect(a.removeFromLeft(14).withSizeKeepingCentre(12, 12));
            ui::text(g, "Vous", a.withTrimmedLeft(6), L.body(14.f), p.fg);
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
            ui::text(g, "Lui", b.withTrimmedLeft(6), L.body(14.f), p.fg);
        }
        g.setColour(p.line);
        g.fillRect(timelineR.getX(), timelineR.getY() + 40, timelineR.getWidth(), (int)p.lineW);

        auto axis = r.removeFromBottom(26);
        auto lanes = r;
        auto labels = lanes.removeFromLeft(64);
        g.fillRect(labels.getRight() - (int)p.lineW, lanes.getY(), (int)p.lineW, lanes.getHeight() + 26);
        auto you = lanes.removeFromTop(lanes.getHeight() / 2), him = lanes;
        ui::text(g, "VOUS", labels.removeFromTop(you.getHeight()), L.mono(13.f), p.fg, juce::Justification::centred);
        ui::text(g, "LUI", labels, L.mono(13.f), p.fg, juce::Justification::centred);
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
                        juce::String why = t.reason == lpfd::TimelineEvent::Silence ? u8("après ") + fmtS(t.info) + u8(" de silence")
                                         : t.reason == lpfd::TimelineEvent::Reflex ? u8("réflexe")
                                         : u8("vous a coupé après ") + juce::String((int)t.info) + " notes";
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
        ui::text(g, "MAINTENANT", tag, L.mono(11.f), juce::Colour(0xff111111), juce::Justification::centred);

        // summary for screen readers
        juce::String sum = phrasesYou == 0 && phrasesHim == 0 ? u8("Rien depuis 12 secondes.")
                         : u8("Dans les 12 dernières secondes : vous avez joué ") + juce::String(phrasesYou) + u8(" phrase(s), il a répondu ")
                               + juce::String(phrasesHim) + u8(" fois.");
        if (sum != timelineSummary) { timelineSummary = sum; timelineAcc.setTitle(sum); }
    }
    int phrasesYou = 0, phrasesHim = 0;
    juce::Component timelineAcc;   // an invisible element that carries the summary

    // ================================================================ hearing
    ui::Slider gateSlider;
    std::unique_ptr<SliderAtt> gateAtt;
    juce::TextButton measureBtn{"Mesurer le silence"};
    int calibShown = 0;

    void buildHearing() {
        addAndMakeVisible(gateSlider);
        gateAtt = std::make_unique<SliderAtt>(proc.apvts, "gate", gateSlider);
        gateSlider.defaultValue = -55.0;
        gateSlider.setTitle("Seuil de silence");
        gateSlider.setDescription(u8("En dessous de ce niveau, c'est du silence. Le repère orange sur le vumètre."));
        gateSlider.textFromValueFunction = [](double v) { return juce::String(juce::roundToInt(v)) + " dB"; };
        gateSlider.onValueChange = [this] { repaint(hearingR); };
        styleButton(measureBtn, "wave", false, 14.f);
        measureBtn.setDescription(u8("Ne jouez pas pendant 3 secondes : il mesure le bruit de la pièce et place le seuil juste au-dessus. Raccourci : S."));
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
        cell(read.removeFromLeft(170), "Gamme", ui::upper(keyText(keyRoot, scaleType)), true);
        cell(read, "Accord", chordText(chordRoot, chordType), false);

        r = r.reduced(14, 10);
        auto title = r.removeFromTop(20);
        ui::text(g, u8("Ce qu'il entend"), title.removeFromLeft(130), L.body(16.f, true), p.fg);
        const float gate = getParam(proc, "gate");
        ui::text(g, "seuil " + juce::String(juce::roundToInt(gate)) + " dB", title, L.body(13.f), p.muted, juce::Justification::centredRight);
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
        ui::text(g, "Seuil", row.removeFromLeft(44), L.body(13.f), p.muted);
        const int cs = proc.calibState.load();
        juce::String note;
        if (cs == 1) note = u8("Mesure du silence… ne jouez pas (") + juce::String(juce::roundToInt(proc.calibProgress.load() * 100)) + " %)";
        else if (cs == 2) note = u8("Bruit de fond ") + juce::String(juce::roundToInt(proc.calibNoiseDb.load())) + u8(" dB : seuil placé à ")
                                 + juce::String(juce::roundToInt(proc.calibResultDb.load())) + " dB.";
        if (note.isNotEmpty()) ui::text(g, note, r.removeFromTop(18), L.body(12.5f), cs == 1 ? p.signalText : p.muted);
    }

    // ================================================================ right: character
    juce::Component character, advanced, routing;
    juce::OwnedArray<juce::TextButton> presetBtns;
    juce::OwnedArray<QSlider> charSliders;
    juce::TextButton advancedBtn{u8("Réglages avancés : oreille, tessiture, MIDI")};
    int presetIndex = -1;
    View currentView = View::Character;

    void buildCharacter() {
        addAndMakeVisible(character);
        character.setTitle(u8("Son caractère"));
        character.setFocusContainerType(FocusContainerType::focusContainer);
        for (int i = 0; i < (int)ui::presets().size(); ++i) {
            auto* b = presetBtns.add(new juce::TextButton(u8(ui::presets()[(size_t)i].name)));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5100);
            b->setDescription(u8(ui::presets()[(size_t)i].desc) + u8(" Raccourci : ") + juce::String(i + 1) + ".");
            b->onClick = [this, i] { applyPreset(i); };
            character.addAndMakeVisible(b);
        }
        auto key = [this] { return keyRoot >= 0 ? keyText(keyRoot, scaleType) : u8("la gamme détectée"); };
        auto add = [this](QSlider* q) { charSliders.add(q); character.addAndMakeVisible(q); };
        add(new QSlider(proc, "patience", u8("Quand répond-il ?"), u8("À chaque note"), u8("À la fin"), [](double v) {
            if (v < 0.04) return u8("Il répond à chaque note, sans attendre.");
            return u8("Il écoute environ ") + juce::String(1 + juce::roundToInt(v * v * 15)) + u8(" notes, ou ")
                   + fmtS((float)(0.08 + 1.92 * std::pow(v, 1.5))) + u8(" de silence.");
        }));
        add(new QSlider(proc, "interruption", u8("Vous coupe-t-il la parole ?"), "Jamais", "Souvent", [](double v) {
            return v < 0.15 ? u8("Jamais par-dessus vous, et il se tait si vous reprenez.")
                 : v < 0.5 ? u8("Il vous coupe parfois, mais se tait si vous reprenez.")
                           : u8("Il n'hésite pas à jouer par-dessus vous.");
        }));
        add(new QSlider(proc, "contrast", u8("Comment répond-il ?"), "Il imite", "Il contredit", [](double v) {
            return v < 0.25 ? u8("Il reprend votre registre, votre rythme, votre contour.")
                 : v < 0.6 ? u8("Tantôt il imite, tantôt il s'oppose.")
                           : u8("Il part à l'opposé : autre registre, autre rythme.");
        }));
        add(new QSlider(proc, "random", u8("D'où viennent ses notes ?"), "De vous", u8("Inventées"), [key](double v) {
            return v < 0.15 ? u8("Seulement des enchaînements entendus chez vous.")
                 : v < 0.55 ? u8("Il ajoute quelques notes de ") + key() + "."
                            : u8("Il invente beaucoup, en restant dans ") + key() + ".";
        }));
        add(new QSlider(proc, "length", u8("Longueur de ses réponses"), "Court", "Long", [](double v) {
            return u8("Environ ") + juce::String(juce::jmax(1, juce::roundToInt(6 * v))) + u8(" notes pour 6 entendues.");
        }));
        add(new QSlider(proc, "humanize", "Jeu", "Machine", "Humain", [](double v) {
            return v < 0.1 ? u8("Réaction immédiate, tempo strict, nuances égales.")
                           : u8("Réagit avec un léger temps variable, respire, varie les nuances.");
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
            juce::AccessibilityHandler::postAnnouncement(u8("Caractère : ") + u8(pr.name), juce::AccessibilityHandler::AnnouncementPriority::medium);
    }

private:
    // ================================================================ right: advanced
    juce::Viewport advView;
    juce::Component advInner;
    juce::TextButton advBack{"Retour"}, routeBack{"Retour"};
    std::unique_ptr<ui::Choice> earChoice, gridChoice, sourceChoice;
    juce::OwnedArray<QSlider> advSliders;
    juce::ComboBox lowBox, highBox, inBox, outBox;
    std::unique_ptr<ComboAtt> inAtt;
    juce::ToggleButton thruBtn{u8("Renvoyer aussi votre jeu MIDI")};
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
                ui::text(g, "Do" + juce::String(o), c.toNearestInt(), L.mono(11.f), in ? p.bg : p.muted, juce::Justification::centred);
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
        advanced.setTitle(u8("Réglages avancés"));
        advanced.setFocusContainerType(FocusContainerType::focusContainer);
        styleButton(advBack, "left", false, 14.f);
        advBack.setTitle(u8("Retour au caractère"));
        advBack.onClick = [this] { showView(View::Character); };
        advanced.addAndMakeVisible(advBack);
        advanced.addAndMakeVisible(advView);
        advView.setViewedComponent(&advInner, false);
        advView.setScrollBarsShown(true, false);

        auto heading = [this](const juce::String& t) { auto* h = headings.add(new Heading()); h->t = t; advInner.addAndMakeVisible(h); };
        heading(u8("OREILLE"));
        earChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("mode"), u8("Type d'écoute"),
                                                 juce::StringArray{"Instrument", "Bright"},
                                                 juce::StringArray{u8("55 Hz – 2 kHz"), u8("Voix, oiseaux · jusqu'à 10 kHz")});
        advInner.addAndMakeVisible(*earChoice);
        auto addQ = [this](QSlider* q) { advSliders.add(q); advInner.addAndMakeVisible(q); };
        addQ(new QSlider(proc, "sensitivity", u8("Sensibilité aux attaques"), "Peu", "Beaucoup", [](double v) {
            return v > 0.7 ? u8("Entend les notes douces, mais aussi les bruits de doigts.")
                 : v < 0.3 ? u8("Seulement les attaques franches.") : u8("Équilibré.");
        }));
        heading(u8("TESSITURE DE SES RÉPONSES"));
        strip.proc = &proc;
        advInner.addAndMakeVisible(strip);
        auto fillBox = [this](juce::ComboBox& b, const char* id, const juce::String& title) {
            for (int m = 24; m <= 108; m += 12) b.addItem(noteName(m), m + 1);
            b.setTitle(title);
            b.onChange = [this, &b, id] { setParam(proc, id, (float)(b.getSelectedId() - 1)); strip.repaint(); };
            advInner.addAndMakeVisible(b);
        };
        advLabel("Plus grave", true);
        advLabel(u8("Plus aiguë"), true);
        fillBox(lowBox, "low", u8("Note la plus grave"));
        fillBox(highBox, "high", u8("Note la plus aiguë"));
        advLabel(u8("Ce qui dépasse est replié à l'octave, jamais coupé."), false, 13.f);
        heading(u8("MÉMOIRE ET STYLE"));
        addQ(new QSlider(proc, "memory", "Il se souvient de", u8("Dernières notes"), "Tout", [](double v) {
            return u8("Environ ") + juce::String(juce::roundToInt(8.0 * std::pow(2.0, v * 9.0))) + u8(" dernières notes.");
        }));
        addQ(new QSlider(proc, "fidelity", "Il cite", "Des bribes", "De longs bouts", [](double v) {
            return v < 0.3 ? u8("Il recombine librement vos tournures.") : v < 0.7 ? u8("Il cite des fragments courts.") : u8("Il cite de longs passages.");
        }));
        addQ(new QSlider(proc, "scale", "Il reste dans la gamme", "Libre", "Toujours", [](double v) {
            return u8("Dans la gamme ") + juce::String(juce::roundToInt(v * 100)) + u8(" % du temps.");
        }));
        gridChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("grid"), u8("Caler sur la grille du morceau"),
                                                  juce::StringArray{"Libre", "Noire", "Croche", "Double"});
        gridChoice->labelHeight = 24;
        advInner.addAndMakeVisible(*gridChoice);
        heading("MIDI");
        sourceChoice = std::make_unique<ui::Choice>(*proc.apvts.getParameter("source"), u8("Il écoute"),
                                                    juce::StringArray{"Audio", "MIDI", "Audio + MIDI"});
        sourceChoice->labelHeight = 24;
        advInner.addAndMakeVisible(*sourceChoice);
        advLabel(u8("Écoute le canal"), true);
        advLabel(u8("Répond sur le canal"), true);
        inBox.addItem("Tous", 1);
        for (int i = 1; i <= 16; ++i) inBox.addItem(juce::String(i), i + 1);
        inBox.setTitle(u8("Canal MIDI écouté"));
        advInner.addAndMakeVisible(inBox);
        inAtt = std::make_unique<ComboAtt>(proc.apvts, "midiin", inBox);
        for (int i = 1; i <= 16; ++i) outBox.addItem(juce::String(i), i);
        outBox.setTitle(u8("Canal MIDI des réponses"));
        outBox.onChange = [this] { setParam(proc, "channel", (float)outBox.getSelectedId()); };
        advInner.addAndMakeVisible(outBox);
        thruBtn.getProperties().set("desc", u8("Un seul synthé joue vos notes et ses réponses."));
        thruBtn.getProperties().set("topAlign", true);
        thruBtn.setDescription(u8("Un seul synthé joue vos notes et ses réponses."));
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
        place(*advSliders[0], 74);
        place(*headings[hi++], 28);
        place(strip, 34);
        {
            const int cw = (w - 28 - 10) / 2;
            advLabels[li]->setBounds(14, y, cw, 22); advLabels[li + 1]->setBounds(14 + cw + 10, y, cw, 22); li += 2; y += 24;
            lowBox.setBounds(14, y, cw, 44); highBox.setBounds(14 + cw + 10, y, cw, 44); y += 50;
        }
        place(*advLabels[li++], 20);
        place(*headings[hi++], 28);
        place(*advSliders[1], 74);
        place(*advSliders[2], 74);
        place(*advSliders[3], 74);
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
    juce::TextButton testBtn{"Jouer une note test (Do4)"};
    juce::OwnedArray<juce::TextButton> hostBtns;
    bool tested = false;
    juce::Rectangle<int> checksR, stepsR;

    void buildRouting() {
        addChildComponent(routing);
        routing.setTitle("Branchement");
        routing.setFocusContainerType(FocusContainerType::focusContainer);
        styleButton(routeBack, "left", false, 14.f);
        routeBack.setTitle(u8("Retour au caractère"));
        routeBack.onClick = [this] { showView(View::Character); };
        routing.addAndMakeVisible(routeBack);
        styleButton(testBtn, "play", true, 15.f);
        testBtn.setDescription(u8("Envoie un Do4 sur le canal de sortie. Vous devez l'entendre dans votre synthé."));
        testBtn.onClick = [this] {
            proc.requestTestNote();
            tested = true;
            testBtn.setButtonText("Rejouer la note test");
            routing.repaint();
            juce::AccessibilityHandler::postAnnouncement(u8("Note test envoyée."), juce::AccessibilityHandler::AnnouncementPriority::medium);
        };
        routing.addAndMakeVisible(testBtn);
        const char* hosts[] = {"Reaper", "Ableton Live", "Autonome"};
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
        ui::text(g, "Branchement", juce::Rectangle<int>(off.x + 130, off.y + 6, 240, 44), L.body(16.f, true), p.fg);
        const float gate = getParam(proc, "gate");
        const int src = juce::roundToInt(getParam(proc, "source"));
        const bool audioOk = src == 1 || peak > gate;
        struct C { juce::String title, state, detail; bool good; };
        const int ch = juce::roundToInt(getParam(proc, "channel"));
        C checks[3] = {
            {"Son entrant", audioOk ? u8("reçu") : u8("rien"),
             src == 1 ? u8("Écoute MIDI : jouez sur votre clavier.")
                      : (audioOk ? u8("Niveau ") + juce::String(juce::roundToInt(peak)) + u8(" dB, au-dessus du seuil (") + juce::String(juce::roundToInt(gate)) + " dB)."
                                 : u8("Rien au-dessus du seuil. Vérifiez l'entrée de la piste, ou baissez le seuil.")), audioOk},
            {u8("Réponses envoyées"), juce::String(sentLastMinute) + u8(" notes depuis 1 min"), u8("Sur le canal ") + juce::String(ch) + ".", sentLastMinute > 0},
            {u8("Reçues par votre synthé"), tested ? u8("à vous de dire") : u8("à vérifier"),
             tested ? u8("Vous avez entendu le Do4 ? Sinon, suivez les étapes ci-dessous.") : u8("Le plugin ne voit pas votre synthé : jouez la note test."), tested},
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
        if (tested) ui::text(g, u8("Note test envoyée sur le canal ") + juce::String(ch) + ".", t.withHeight(20), L.body(13.f), p.fg);
        ui::text(g, u8("Brancher la sortie dans…"), t.translated(0, 26).withHeight(22), L.body(15.f, true), p.fg);
        static const char* steps[3][3] = {
            {"Piste du plugin : bouton Route › Add new send vers la piste du synthé.",
             "Dans cet envoi : Audio sur None, MIDI sur All › All.",
             "Choisissez le même canal que « Répond sur le canal » si le synthé filtre."},
            {"Live ne reçoit pas le MIDI d'un plugin : ouvrez LPF Dialogue en application autonome.",
             "Dans ses réglages, Sortie MIDI : bus IAC (Mac) ou loopMIDI (Windows).",
             "Dans Live, piste du synthé : MIDI From › ce bus, Monitor sur In."},
            {"Options › Réglages audio/MIDI : choisissez l'entrée audio et la sortie MIDI.",
             "Sortie vers le Move en USB, ou vers un bus virtuel.",
             "Écoutez au casque pour que ses réponses ne repassent pas par le micro."}};
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
            ui::text(g, u8("Son caractère"), juce::Rectangle<int>(off.getX() + 14, off.getY() + 10, 140, 24), L.body(16.f, true), p.fg);
            ui::text(g, u8("· ") + (presetIndex >= 0 ? u8(ui::presets()[(size_t)presetIndex].name) : juce::String("Perso")),
                     juce::Rectangle<int>(off.getX() + 140, off.getY() + 10, 220, 24), L.body(14.f), p.muted);
            g.setColour(p.line);
            g.fillRect(off.getX(), off.getBottom() - 50, off.getWidth(), (int)p.lineW);
        } else if (currentView == View::Advanced) {
            ui::text(g, u8("Réglages avancés"), juce::Rectangle<int>(off.getX() + 130, off.getY() + 6, 240, 44), L.body(16.f, true), p.fg);
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
    juce::TextButton diagBtn{u8("Rien ne sort ? Diagnostic du branchement")};

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
        hint("Tab", u8("réglage suivant"));
        hint(u8("← →"), u8("régler"));
        hint("Maj", u8("fin"));
        hint("Espace", u8("répondre oui/non"));
        hint("F", u8("figer"));
        hint("?", "aide");
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
        welcome.setTitle("Bienvenue");
        welcome.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        welcome.painter = [this](juce::Graphics& g) { paintWelcome(g); };
        struct S { const char* t; const char* d; const char* icon; };
        const S srcs[] = {{"Un instrument", "Guitare, piano, cuivre… par le micro ou la carte son.", "guitar"},
                          {"Voix ou oiseaux", "Sons aigus, rapides, glissés. Écoute Bright.", "mic"},
                          {"Un clavier MIDI", "Notes exactes, réponse instantanée.", "keys"},
                          {"Les deux", "Il écoute l'audio et le MIDI en même temps.", "both"}};
        for (int i = 0; i < 4; ++i) {
            auto* c = welcomeSources.add(new Card(u8(srcs[i].t), u8(srcs[i].d), srcs[i].icon));
            c->setRadioGroupId(5300);
            c->onClick = [this, i] {
                setParam(proc, "source", i == 2 ? 1.f : (i == 3 ? 2.f : 0.f));
                setParam(proc, "mode", i == 1 ? 1.f : 0.f);
                syncWelcome();
            };
            welcome.addAndMakeVisible(c);
        }
        styleButton(welcomeMeasure, "wave", false, 15.f);
        welcomeMeasure.setButtonText(u8("Écouter 3 s de silence"));
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
        welcomeStart.setButtonText(u8("Commencer à jouer"));
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
        ui::text(g, u8("Bienvenue. Jouez, il répond."), r.removeFromTop(38), L.mono(24.f), p.fg);
        ui::text(g, u8("Trois questions et vous dialoguez. Tout se change plus tard, rien ne peut casser."), r.removeFromTop(32).withTrimmedBottom(8), L.body(16.f), p.muted);
        auto step = [&](const char* n, const juce::String& t) {
            auto s = r.removeFromTop(36);
            auto b = s.removeFromLeft(28).withSizeKeepingCentre(28, 28);
            g.setColour(p.fg);
            g.fillRect(b);
            ui::text(g, n, b, L.mono(14.f), p.bg, juce::Justification::centred);
            ui::text(g, t, s.withTrimmedLeft(10), L.body(18.f, true), p.fg);
        };
        step("1", u8("Qu'allez-vous jouer ?"));
        r.removeFromTop(118 + 14);
        step("2", u8("Apprenez-lui votre silence"));
        auto m = r.removeFromTop(52);
        m.removeFromLeft(262);
        const int cs = proc.calibState.load();
        juce::String t = cs == 1 ? u8("Mesure en cours… ne jouez pas (") + juce::String(juce::roundToInt(proc.calibProgress.load() * 100)) + " %)."
                       : cs == 2 ? u8("Bruit de fond ") + juce::String(juce::roundToInt(proc.calibNoiseDb.load())) + u8(" dB. Seuil placé juste au-dessus, à ")
                                       + juce::String(juce::roundToInt(proc.calibResultDb.load())) + u8(" dB. Il ne prendra plus votre souffle pour une note.")
                                 : u8("Ne jouez pas pendant 3 secondes : il mesure le bruit de la pièce et place le seuil juste au-dessus.");
        ui::text(g, t, m, L.body(14.f), p.fg, juce::Justification::centredLeft, 2);
        r.removeFromTop(14);
        step("3", u8("Quel partenaire voulez-vous ?"));
        ui::text(g, u8("Il commencera à répondre dès votre première phrase."), juce::Rectangle<int>(wMain.getX() + 290, wMain.getBottom() - 70, 420, 52),
                 L.body(14.f), p.muted);

        // aside
        g.setColour(p.fg);
        g.fillRect(wAside);
        auto a = wAside.reduced(22, 22);
        ui::text(g, u8("Comment ça marche"), a.removeFromTop(30), L.mono(17.f), p.bg);
        a.removeFromTop(10);
        const char* pts[3][2] = {{"Il écoute", "votre instrument, votre voix ou votre clavier, et retient ce que vous jouez."},
                                 {"Il attend son tour", ", plus ou moins longtemps selon sa patience."},
                                 {"Il répond en MIDI", " avec vos propres tournures, dans votre gamme. Le son, c'est le synthé que vous branchez."}};
        for (int i = 0; i < 3; ++i) {
            auto row = a.removeFromTop(104);
            ui::text(g, "0" + juce::String(i + 1), row.removeFromLeft(34), L.mono(14.f), p.signal, juce::Justification::topLeft);
            ui::text(g, u8(pts[i][0]), row.removeFromTop(22), L.body(15.f, true), p.bg, juce::Justification::topLeft);
            ui::text(g, u8(pts[i][1]).trimCharactersAtStart(", "), row, L.body(14.f), p.bg, juce::Justification::topLeft, 4);
        }
        ui::text(g, u8("Tout se passe sur votre ordinateur. Aucun son ne part sur Internet."), a.removeFromBottom(44), L.body(13.f), p.bg.withAlpha(0.85f),
                 juce::Justification::bottomLeft, 2);
    }

    // ================================================================ dialogs (help, display)
    Overlay help, display;
    juce::OwnedArray<juce::Component> helpItems;
    juce::TextButton helpClose, displayClose, helpWelcome{u8("Revoir l'accueil")}, helpDiag{u8("Diagnostic du branchement")};
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
        help.setTitle("Aide");
        help.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        help.painter = [this](juce::Graphics& g) { paintHelp(g); };
        styleButton(helpClose, "close");
        helpClose.setTitle("Fermer l'aide");
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
        dialogFrame(g, "AIDE", u8("Comment il joue avec vous"));
        auto r = dialogR.reduced(22, 0).withTrimmedTop(90);
        const char* pts[4][2] = {
            {"Il écoute", "Il retient vos notes, vos rythmes, vos nuances, et reconnaît la gamme et l'accord."},
            {"Il attend son tour", "« Quand répond-il ? » règle combien de notes ou de silence il attend. Le compte à rebours vous montre quand il va parler."},
            {"Il répond en MIDI", "Avec vos propres tournures, plus ou moins fidèles, imitées ou contredites. Le son vient du synthé branché sur sa sortie."},
            {"La conversation", "Les blocs Vous et Lui des 12 dernières secondes, avec la raison de chaque réponse."}};
        for (auto& pt : pts) {
            auto row = r.removeFromTop(86);
            ui::text(g, u8(pt[0]), row.removeFromTop(22), L.body(15.5f, true), p.fg);
            ui::text(g, u8(pt[1]), row, L.body(14.f), p.fg, juce::Justification::topLeft, 3);
        }
        r.removeFromTop(6);
        ui::text(g, "Clavier", r.removeFromTop(24), L.body(15.5f, true), p.fg);
        paintShortcuts(g, r.removeFromTop(5 * 30));
    }

    void paintShortcuts(juce::Graphics& g, juce::Rectangle<int> r) {
        auto& L = lookOf(*this);
        const auto& p = L.pal();
        const char* keys[10][2] = {{"Tab", "Réglage suivant"}, {"Maj+Tab", "Réglage précédent"}, {"← →", "Régler"}, {"Maj", "Réglage fin"},
                                   {"Espace", "Répondre oui / non"}, {"F", "Figer la mémoire"}, {"S", "Mesurer le silence"}, {"1 à 6", "Choisir un caractère"},
                                   {"Suppr", "Remettre par défaut"}, {"Échap", "Fermer, revenir"}};
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
        display.setTitle(u8("Affichage et accessibilité"));
        display.setFocusContainerType(FocusContainerType::keyboardFocusContainer);
        display.painter = [this](juce::Graphics& g) { paintDisplay(g); };
        styleButton(displayClose, "close");
        displayClose.setTitle(u8("Fermer les réglages d'affichage"));
        displayClose.onClick = [this] { closeDialog(display); };
        display.addAndMakeVisible(displayClose);
        const char* themes[] = {"Papier", "Encre", "Contraste max"};
        for (int i = 0; i < 3; ++i) {
            auto* b = themeBtns.add(new juce::TextButton(themes[i]));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5400);
            b->setDescription(u8("Thème"));
            b->onClick = [this, i] { settings.theme = (ui::Theme)i; settings.save(); };
            display.addAndMakeVisible(b);
        }
        const char* sizes[] = {"100 %", "125 %", "150 %"};
        for (int i = 0; i < 3; ++i) {
            auto* b = sizeBtns.add(new juce::TextButton(sizes[i]));
            styleButton(*b, {}, false, 14.f);
            b->setRadioGroupId(5401);
            b->setDescription(u8("Taille du texte : la fenêtre grandit avec le texte."));
            b->onClick = [this, i] { editor.applyScale(1.f + 0.25f * (float)i); };
            display.addAndMakeVisible(b);
        }
        const char* toggles[4][2] = {
            {"Annoncer ce qu'il fait", "Le lecteur d'écran dit « il écoute », « il répond », et la gamme ou l'accord quand ils changent."},
            {"Signal visuel quand il répond", "Un cadre orange fixe autour du plugin pendant sa réponse, visible de loin, sans clignoter."},
            {"Réduire les animations", "La conversation avance par pas au lieu de défiler."},
            {"Formes en plus des couleurs", "Vous ■ et lui ◆ se distinguent aussi par la forme, pas seulement par la couleur."}};
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
        dialogFrame(g, u8("AFFICHAGE ET ACCESSIBILITÉ"), u8("Gardé pour tous vos projets"));
        auto r = dialogR.reduced(22, 0).withTrimmedTop(84);
        ui::text(g, u8("Thème"), r.removeFromTop(28), L.body(15.5f, true), p.fg);
        r.removeFromTop(48 + 16);
        ui::text(g, "Taille du texte", r.removeFromTop(28), L.body(15.5f, true), p.fg);
        r.removeFromTop(48);
        ui::text(g, u8("La fenêtre grandit avec le texte : rien n'est coupé."), r.removeFromTop(26), L.body(13.f), p.muted);
        ui::text(g, "Voir et entendre", r.removeFromTop(26), L.body(15.5f, true), p.fg);
        r.removeFromTop(4 * 70);
        r.removeFromTop(8);
        ui::text(g, "Clavier", r.removeFromTop(24), L.body(15.5f, true), p.fg);
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
            const juce::String w = phase == (int)lpfd::Engine::Phase::Answering ? u8("Il répond")
                                 : phase == (int)lpfd::Engine::Phase::Listening ? u8("Il écoute")
                                 : phase == (int)lpfd::Engine::Phase::Waiting ? u8("Il attend") : juce::String();
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
            juce::AccessibilityHandler::postAnnouncement(u8("Gamme ") + keyText(keyRoot, scaleType) + u8(", accord ") + chordText(chordRoot, chordType),
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
