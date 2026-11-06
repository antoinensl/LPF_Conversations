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
    addKnob(memory, "memory", u8("Mémoire"),
            u8("0 % : ne se souvient que des dernières notes. 100 % : de tout ce que tu as joué."));
    addKnob(fidelity, "fidelity", u8("Fidélité"),
            u8("0 % : recombine librement. 100 % : cite de longs fragments de ton jeu."));
    addKnob(contrast, "contrast", "Contraste",
            u8("0 % : imite. 100 % : s'oppose (registre, rythme, contour, nuances)."));
    addKnob(scale, "scale", u8("Tonalité"),
            u8("Probabilité de rester dans les notes que tu as jouées."));
    addKnob(sens, "sensitivity", u8("Sensibilité"), u8("Détection des attaques."));
    addKnob(gate, "gate", "Seuil", u8("En dessous de ce niveau, c'est du silence. Règle-le juste au-dessus du bruit de fond."));
    addKnob(low, "low", "Note basse", u8("Note la plus grave de la réponse."));
    addKnob(high, "high", "Note haute", u8("Note la plus aiguë de la réponse."));
    addKnob(channel, "channel", "Canal", u8("Canal MIDI de sortie."));

    grid.addItemList({u8("Libre"), "1/4", "1/8", "1/16"}, 1);
    addAndMakeVisible(grid);
    gridAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "grid", grid);
    gridLabel.setText("Grille", juce::dontSendNotification);
    gridLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(gridLabel);
    grid.setTooltip(u8("Cale les réponses sur la grille du séquenceur (transport en lecture)."));

    freeze.setButtonText(u8("Figer la mémoire"));
    respond.setButtonText(u8("Répondre"));
    addAndMakeVisible(freeze);
    addAndMakeVisible(respond);
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "freeze", freeze);
    respondAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "respond", respond);
    clear.setButtonText(u8("Effacer la mémoire"));
    clear.onClick = [this] { proc.requestClear(); };
    addAndMakeVisible(clear);

    setSize(640, 430);
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
    repaint(0, 0, getWidth(), 56);
}

static juce::String noteName(float midi) {
    static const char* names[] = {"Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "Sib", "Si"};
    const int m = juce::roundToInt(midi);
    return juce::String::fromUTF8(names[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1);
}

void DialogueEditor::paint(juce::Graphics& g) {
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
    if (pitch > 0.f && hearing) info << "  ·  " << noteName(pitch);
    g.drawText(info, 410, 14, 214, 24, juce::Justification::centredRight);

    // level bar
    const float lv = juce::jlimit(0.f, 1.f, (level + 70.f) / 70.f);
    g.setColour(juce::Colour(0xff2a2f37));
    g.fillRoundedRectangle(16.f, 44.f, 608.f, 4.f, 2.f);
    g.setColour(colours::listen.withAlpha(0.8f));
    g.fillRoundedRectangle(16.f, 44.f, 608.f * lv, 4.f, 2.f);
    const float gp = juce::jlimit(0.f, 1.f, ((float)gate.slider.getValue() + 70.f) / 70.f);
    g.setColour(colours::text);
    g.fillRect(16.f + 608.f * gp - 1.f, 41.f, 2.f, 10.f);

    auto section = [&](juce::Rectangle<int> r, const juce::String& title) {
        g.setColour(colours::panel);
        g.fillRoundedRectangle(r.toFloat(), 8.f);
        g.setColour(colours::dim);
        g.setFont(juce::FontOptions(11.f, juce::Font::bold));
        g.drawText(title, r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 14,
                   juce::Justification::centredLeft);
    };
    section({16, 60, 300, 160}, u8("RÉACTIVITÉ"));
    section({324, 60, 300, 160}, u8("CARACTÈRE"));
    section({16, 228, 608, 186}, u8("ÉCOUTE ET SORTIE"));
}

void DialogueEditor::resized() {
    auto place = [](Knob& k, int x, int y) {
        k.label.setBounds(x, y, 90, 16);
        k.slider.setBounds(x + 5, y + 16, 80, 96);
    };
    place(patience, 26, 84);
    place(interruption, 121, 84);
    place(length, 216, 84);
    place(memory, 330, 84);
    place(fidelity, 402, 84);
    place(contrast, 474, 84);
    place(scale, 546, 84);
    for (Knob* k : {&memory, &fidelity, &contrast, &scale}) {
        auto b = k->slider.getBounds();
        k->slider.setBounds(b.getX() - 4, b.getY(), 76, b.getHeight());
        k->label.setBounds(b.getX() - 9, k->label.getY(), 86, 16);
    }
    place(sens, 26, 252);
    place(gate, 116, 252);
    place(low, 206, 252);
    place(high, 296, 252);
    place(channel, 386, 252);
    gridLabel.setBounds(486, 252, 120, 16);
    grid.setBounds(491, 276, 110, 24);
    freeze.setBounds(30, 376, 170, 24);
    respond.setBounds(200, 376, 120, 24);
    clear.setBounds(470, 376, 140, 26);
}
