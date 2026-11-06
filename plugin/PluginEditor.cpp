#include "PluginEditor.h"

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

namespace colours {
const juce::Colour bg{0xff16181d}, panel{0xff1f232a}, text{0xffe8e6e1}, dim{0xff8b9099},
    listen{0xff4fc3a1}, answer{0xfff0a35e};
}

DialogueEditor::DialogueEditor(DialogueProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    lnf.setColour(juce::Slider::rotarySliderFillColourId, colours::listen);
    lnf.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff343a44));
    lnf.setColour(juce::Slider::thumbColourId, colours::text);
    lnf.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lnf.setColour(juce::Slider::textBoxTextColourId, colours::dim);
    lnf.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lnf.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    lnf.setColour(juce::Label::textColourId, colours::text);
    lnf.setColour(juce::ToggleButton::textColourId, colours::text);
    lnf.setColour(juce::ToggleButton::tickColourId, colours::listen);
    lnf.setColour(juce::ComboBox::backgroundColourId, colours::panel);
    lnf.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff343a44));
    lnf.setColour(juce::TextButton::buttonColourId, colours::panel);
    setLookAndFeel(&lnf);

    addKnob(patience, "patience", "Patience",
            u8("0 % : répond à chaque note (réflexe). 100 % : attend un long silence."));
    addKnob(interruption, "interruption", "Interruption",
            u8("0 % : ne parle jamais en même temps que toi et se tait quand tu reprends. "
               "100 % : répond dès que la patience est atteinte, même si tu joues encore."));
    addKnob(length, "length", "Longueur",
            u8("Longueur de la réponse par rapport à ce qu'il vient d'entendre."));
    addKnob(humanize, "humanize", "Humanisation",
            u8("0 % : machine. 100 % : délai de réaction variable, micro-timing, nuances qui respirent, "
               "dernière note tenue."));
    addKnob(random, "random", "Hasard",
            u8("Notes inventées dans la gamme et l'accord détectés, ornements, silences, "
               "rythmes déplacés, réponses parfois laissées passer."));
    addKnob(memory, "memory", u8("Mémoire"),
            u8("0 % : ne se souvient que des dernières notes. 100 % : de tout ce que tu as joué."));
    addKnob(fidelity, "fidelity", u8("Fidélité"),
            u8("0 % : recombine librement. 100 % : cite de longs fragments de ton jeu."));
    addKnob(contrast, "contrast", "Contraste",
            u8("0 % : imite. 100 % : s'oppose (registre, rythme, contour, nuances)."));
    addKnob(scale, "scale", u8("Tonalité"),
            u8("Probabilité de rester dans la gamme détectée (ou les notes jouées tant qu'elle n'est pas sûre)."));
    addKnob(sens, "sensitivity", u8("Sensibilité"), u8("Détection des attaques."));
    addKnob(gate, "gate", "Seuil", u8("En dessous de ce niveau, c'est du silence. Règle-le juste au-dessus du bruit de fond."));
    addKnob(low, "low", "Note basse", u8("Note la plus grave de la réponse."));
    addKnob(high, "high", "Note haute", u8("Note la plus aiguë de la réponse."));
    addKnob(channel, "channel", "Canal sortie", u8("Canal MIDI des réponses."));

    grid.addItemList({u8("Libre"), "1/4", "1/8", "1/16"}, 1);
    addAndMakeVisible(grid);
    gridAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "grid", grid);
    gridLabel.setText("Grille", juce::dontSendNotification);
    gridLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gridLabel);
    grid.setTooltip(u8("Cale les réponses sur la grille du séquenceur (transport en lecture)."));

    mode.addItemList({"Instrument", "Bright"}, 1);
    addAndMakeVisible(mode);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "mode", mode);
    modeLabel.setText(u8("Écoute"), juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modeLabel);
    mode.setTooltip(u8("Instrument : 55 Hz - 2 kHz. Bright : oiseaux, voix parlée ou chantée "
                       "(100 Hz - 10 kHz, trilles, glissandi suivis note à note)."));

    auto combo = [this](juce::ComboBox& c, juce::Label& l, const juce::String& text, const juce::StringArray& items,
                        const juce::String& id, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att,
                        const juce::String& tip) {
        c.addItemList(items, 1);
        addAndMakeVisible(c);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, id, c);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
        c.setTooltip(tip);
    };
    combo(source, sourceLabel, "Source", {"Audio", "MIDI", "Audio + MIDI"}, "source", sourceAtt,
          u8("Ce qu'il écoute : l'audio de la piste, le MIDI qui arrive (clavier, item MIDI), ou les deux."));
    {
        juce::StringArray ch{u8("Tous")};
        for (int i = 1; i <= 16; ++i) ch.add(juce::String(i));
        combo(midiIn, midiInLabel, u8("Entrée MIDI"), ch, "midiin", midiInAtt,
              u8("Canal MIDI écouté. Ses propres réponses qui reviendraient en boucle sont ignorées."));
    }
    thru.setButtonText("MIDI thru");
    thru.setTooltip(u8("Renvoie aussi le MIDI reçu sur la sortie, mélangé aux réponses."));
    addAndMakeVisible(thru);
    thruAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "thru", thru);

    freeze.setButtonText(u8("Figer la mémoire"));
    respond.setButtonText(u8("Répondre"));
    addAndMakeVisible(freeze);
    addAndMakeVisible(respond);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "freeze", freeze);
    respondAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "respond", respond);
    clear.setButtonText(u8("Effacer la mémoire"));
    clear.onClick = [this] { proc.requestClear(); };
    addAndMakeVisible(clear);

    setSize(760, 470);
    startTimerHz(20);
}

DialogueEditor::~DialogueEditor() { setLookAndFeel(nullptr); }

void DialogueEditor::addKnob(Knob& k, const juce::String& id, const juce::String& text, const juce::String& tip) {
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setTooltip(tip);
    addAndMakeVisible(k.slider);
    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::FontOptions(13.f));
    addAndMakeVisible(k.label);
    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, id, k.slider);
}

void DialogueEditor::timerCallback() {
    hearing = proc.uiHearing.load();
    answering = proc.uiAnswering.load();
    memSize = proc.uiMemory.load();
    level = proc.uiLevelDb.load();
    pitch = proc.uiPitch.load();
    keyRoot = proc.uiKeyRoot.load(); scaleType = proc.uiScale.load();
    chordRoot = proc.uiChordRoot.load(); chordType = proc.uiChord.load();
    repaint(0, 0, getWidth(), 76);
}

static juce::String noteName(float midi) {
    static const char* names[] = {"Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "Sib", "Si"};
    const int m = juce::roundToInt(midi);
    return juce::String::fromUTF8(names[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1);
}

void DialogueEditor::paint(juce::Graphics& g) {
    const int W = getWidth();
    g.fillAll(colours::bg);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(20.f, juce::Font::bold));
    g.drawText("LPF Dialogue", 16, 12, 220, 28, juce::Justification::centredLeft);

    auto lamp = [&](int x, juce::Colour c, bool on, const juce::String& txt) {
        g.setColour(on ? c : c.withAlpha(0.18f));
        g.fillEllipse((float)x, 21.f, 10.f, 10.f);
        g.setColour(on ? colours::text : colours::dim);
        g.setFont(juce::FontOptions(13.f));
        g.drawText(txt, x + 15, 14, 90, 24, juce::Justification::centredLeft);
    };
    lamp(230, colours::listen, hearing, u8("Écoute"));
    lamp(320, colours::answer, answering, u8("Répond"));

    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(13.f));
    juce::String info = juce::String(memSize) + u8(" notes en mémoire");
    if (pitch > 0.f && hearing) info << u8("  ·  ") << noteName(pitch);
    g.drawText(info, W - 330, 14, 314, 24, juce::Justification::centredRight);

    // level bar + gate marker
    const float bw = (float)W - 32.f;
    const float lv = juce::jlimit(0.f, 1.f, (level + 70.f) / 70.f);
    g.setColour(juce::Colour(0xff2a2f37));
    g.fillRoundedRectangle(16.f, 44.f, bw, 4.f, 2.f);
    g.setColour(colours::listen.withAlpha(0.8f));
    g.fillRoundedRectangle(16.f, 44.f, bw * lv, 4.f, 2.f);
    const float gp = juce::jlimit(0.f, 1.f, ((float)gate.slider.getValue() + 70.f) / 70.f);
    g.setColour(colours::text);
    g.fillRect(16.f + bw * gp - 1.f, 41.f, 2.f, 10.f);

    // what it thinks you are playing in
    g.setFont(juce::FontOptions(13.f));
    juce::String key = keyRoot >= 0
        ? juce::String::fromUTF8(lpfd::Harmony::pcName(keyRoot)) + " " + juce::String::fromUTF8(lpfd::Harmony::scaleName(scaleType))
        : juce::String(u8("…"));
    juce::String chord = chordRoot >= 0
        ? juce::String(lpfd::Harmony::pcLetter(chordRoot)) + lpfd::Harmony::chordSuffix(chordType)
        : juce::String(u8("…"));
    g.setColour(colours::dim);
    g.drawText("Gamme", 16, 54, 50, 18, juce::Justification::centredLeft);
    g.drawText("Accord", 260, 54, 50, 18, juce::Justification::centredLeft);
    g.setColour(colours::text);
    g.drawText(key, 66, 54, 190, 18, juce::Justification::centredLeft);
    g.drawText(chord, 312, 54, 120, 18, juce::Justification::centredLeft);

    auto section = [&](juce::Rectangle<int> r, const juce::String& title) {
        g.setColour(colours::panel);
        g.fillRoundedRectangle(r.toFloat(), 8.f);
        g.setColour(colours::dim);
        g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        g.drawText(title, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14, juce::Justification::centredLeft);
    };
    section({16, 80, 330, 160}, u8("RÉACTIVITÉ"));
    section({354, 80, 390, 160}, u8("CARACTÈRE"));
    section({16, 248, 728, 206}, u8("ÉCOUTE ET SORTIE"));
}

void DialogueEditor::resized() {
    auto place = [](Knob& k, int x, int y, int w) {
        k.label.setBounds(x, y, w, 16);
        k.slider.setBounds(x + (w - 76) / 2, y + 16, 76, 96);
    };
    int x = 24;
    for (Knob* k : {&patience, &interruption, &length, &humanize}) { place(*k, x, 104, 78); x += 79; }
    x = 360;
    for (Knob* k : {&memory, &fidelity, &contrast, &scale, &random}) { place(*k, x, 104, 76); x += 76; }
    modeLabel.setBounds(26, 276, 120, 16);
    mode.setBounds(30, 298, 112, 24);
    x = 158;
    for (Knob* k : {&sens, &gate, &low, &high, &channel}) { place(*k, x, 272, 84); x += 86; }
    gridLabel.setBounds(600, 276, 130, 16);
    grid.setBounds(610, 298, 110, 24);
    sourceLabel.setBounds(26, 334, 120, 16);
    source.setBounds(30, 356, 112, 24);
    midiInLabel.setBounds(600, 334, 130, 16);
    midiIn.setBounds(610, 356, 110, 24);
    freeze.setBounds(30, 414, 160, 24);
    respond.setBounds(190, 414, 110, 24);
    thru.setBounds(300, 414, 110, 24);
    clear.setBounds(590, 412, 140, 26);
}
