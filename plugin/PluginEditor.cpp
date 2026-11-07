#include "PluginEditor.h"

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

static juce::String noteName(float midi) {
    const int m = juce::roundToInt(midi);
    return brutal::upper(juce::String::fromUTF8(lpfd::Harmony::pcName(((m % 12) + 12) % 12))) + juce::String(m / 12 - 1);
}

DialogueEditor::DialogueEditor(DialogueProcessor& p) : AudioProcessorEditor(&p), proc(p) {
    setLookAndFeel(&lnf);

    addCell(patience, "patience", "Patience", u8("0 % : répond à chaque note (réflexe). 100 % : attend un long silence."));
    addCell(interruption, "interruption", "Interruption",
            u8("0 % : ne parle jamais en même temps que toi et se tait quand tu reprends. "
               "100 % : répond dès que la patience est atteinte, même si tu joues encore."));
    addCell(length, "length", "Longueur", u8("Longueur de la réponse par rapport à ce qu'il vient d'entendre."));
    addCell(humanize, "humanize", "Humanisation",
            u8("0 % : machine. 100 % : délai de réaction variable, micro-timing, nuances qui respirent, dernière note tenue."));
    addCell(memory, "memory", u8("Mémoire"), u8("0 % : ne se souvient que des dernières notes. 100 % : de tout ce que tu as joué."));
    addCell(fidelity, "fidelity", u8("Fidélité"), u8("0 % : recombine librement. 100 % : cite de longs fragments de ton jeu."));
    addCell(contrast, "contrast", "Contraste", u8("0 % : imite. 100 % : s'oppose (registre, rythme, contour, nuances)."));
    addCell(scale, "scale", u8("Tonalité"), u8("Probabilité de rester dans la gamme détectée (ou les notes jouées tant qu'elle n'est pas sûre)."));
    addCell(random, "random", "Hasard",
            u8("Notes inventées dans la gamme et l'accord détectés, ornements, silences, rythmes déplacés, "
               "réponses parfois laissées passer."));
    addCell(sens, "sensitivity", u8("Sensibilité"), u8("Détection des attaques."));
    addCell(gate, "gate", "Seuil", u8("En dessous de ce niveau, c'est du silence. Règle-le juste au-dessus du bruit de fond (repère orange sur le vumètre)."));
    addCell(low, "low", "Note basse", u8("Note la plus grave de la réponse."));
    addCell(high, "high", "Note haute", u8("Note la plus aiguë de la réponse."));
    addCell(midiIn, "midiin", u8("Entrée MIDI"), u8("Canal MIDI écouté. Ses propres réponses qui reviendraient en boucle sont ignorées."));
    addCell(channel, "channel", "Canal sortie", u8("Canal MIDI des réponses."));

    auto seg = [this](const char* id, const juce::String& label, juce::StringArray names, const juce::String& tip) {
        auto s = std::make_unique<brutal::Segmented>(*proc.apvts.getParameter(id), label, names);
        s->setTooltip(tip);
        addAndMakeVisible(*s);
        return s;
    };
    mode = seg("mode", u8("Écoute"), {"INSTRUMENT", "BRIGHT"},
               u8("Instrument : 55 Hz - 2 kHz. Bright : oiseaux, voix parlée ou chantée (100 Hz - 10 kHz, trilles, glissandi)."));
    source = seg("source", "Source", {"AUDIO", "MIDI", "A+M"},
                 u8("Ce qu'il écoute : l'audio de la piste, le MIDI qui arrive, ou les deux."));
    grid = seg("grid", "Grille", {"LIBRE", "1/4", "1/8", "1/16"},
               u8("Cale les réponses sur la grille du séquenceur (transport en lecture)."));

    addToggle(freeze, "freeze", u8("Figer mémoire"), u8("N'apprend plus rien, mais continue d'écouter et de répondre."));
    addToggle(respond, "respond", u8("Répondre"), u8("Désactivé : il écoute et apprend sans répondre."));
    addToggle(thru, "thru", "MIDI thru", u8("Renvoie aussi le MIDI reçu sur la sortie, mélangé aux réponses."));

    clear.setName(u8("Mémoire"));
    clear.setButtonText(u8("× EFFACER"));
    clear.getProperties().set("action", true);
    clear.setTooltip(u8("Oublie tout ce qu'il a entendu."));
    clear.onClick = [this] { proc.requestClear(); };
    addAndMakeVisible(clear);

    setSize(780, 576);
    startTimerHz(24);
}

DialogueEditor::~DialogueEditor() { setLookAndFeel(nullptr); }

void DialogueEditor::addCell(ParamCell& c, const juce::String& id, const juce::String& label, const juce::String& tip) {
    c.cell.setName(label);
    c.cell.setTooltip(tip);
    addAndMakeVisible(c.cell);
    c.att = std::make_unique<SliderAtt>(proc.apvts, id, c.cell);
    if (auto* prm = proc.apvts.getParameter(id))
        c.cell.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
}

void DialogueEditor::addToggle(Toggle& t, const juce::String& id, const juce::String& label, const juce::String& tip) {
    t.button.setName(label);
    t.button.setClickingTogglesState(true);
    t.button.setTooltip(tip);
    addAndMakeVisible(t.button);
    t.att = std::make_unique<ButtonAtt>(proc.apvts, id, t.button);
}

void DialogueEditor::timerCallback() {
    hearing = proc.uiHearing.load();
    answering = proc.uiAnswering.load();
    memSize = proc.uiMemory.load();
    level = proc.uiLevelDb.load();
    pitch = proc.uiPitch.load();
    keyRoot = proc.uiKeyRoot.load(); scaleType = proc.uiScale.load();
    chordRoot = proc.uiChordRoot.load(); chordType = proc.uiChord.load();
    repaint(headerR.getUnion(meterR).getUnion(keyR).getUnion(chordR));
}

void DialogueEditor::paint(juce::Graphics& g) {
    const auto& c = brutal::palette();
    g.fillAll(c.paper);

    // ---- header: name, version, status blocks
    {
        auto r = headerR.reduced(14, 0);
        g.setColour(c.ink);
        g.setFont(lnf.mono(30.f, true));
        g.drawText("LPF/DIALOGUE", r.withWidth(300), juce::Justification::centredLeft);
        g.setColour(c.mid);
        g.setFont(lnf.mono(10.f, true));
        g.drawText(u8("V0.4 — LIFE PASS FILTER"), r.withX(r.getX() + 2).withTop(headerR.getBottom() - 18).withHeight(12),
                   juce::Justification::centredLeft);

        auto block = [&](juce::Rectangle<int> b, const juce::String& text, bool on, juce::Colour onColour) {
            g.setColour(on ? onColour : c.paper);
            g.fillRect(b);
            g.setColour(c.ink);
            g.drawRect(b, 2);
            g.setColour(on ? c.paper : c.ink);
            g.setFont(lnf.mono(13.f, true));
            g.drawText(text, b, juce::Justification::centred);
        };
        auto right = headerR.reduced(14, 16);
        auto ansB = right.removeFromRight(104);
        right.removeFromRight(8);
        auto hearB = right.removeFromRight(104);
        block(hearB, u8("ÉCOUTE"), hearing, c.ink);
        block(ansB, u8("RÉPOND"), answering, c.signal);

        right.removeFromRight(16);
        g.setColour(c.ink);
        g.setFont(lnf.mono(13.f, true));
        juce::String mem = juce::String(memSize).paddedLeft('0', 4) + " NOTES";
        g.drawText(mem, right.removeFromRight(110), juce::Justification::centredRight);
        right.removeFromRight(12);
        g.setColour(hearing && pitch > 0.f ? c.ink : c.mid);
        g.drawText(hearing && pitch > 0.f ? noteName(pitch) : juce::String("---"), right.removeFromRight(60),
                   juce::Justification::centredRight);
    }

    // ---- meter: raw segments, gate marker in signal colour
    {
        auto r = meterR.reduced(14, 9);
        const int n = 64;
        const float segW = (float)r.getWidth() / n;
        const float lv = juce::jlimit(0.f, 1.f, (level + 70.f) / 70.f);
        const float gp = juce::jlimit(0.f, 1.f, ((float)gate.cell.getValue() + 70.f) / 70.f);
        for (int i = 0; i < n; ++i) {
            const float x0 = r.getX() + i * segW;
            const bool lit = (i + 0.5f) / n <= lv;
            g.setColour(lit ? c.ink : c.ink.withAlpha(0.08f));
            g.fillRect(x0 + 1.f, (float)r.getY(), segW - 2.f, (float)r.getHeight());
        }
        g.setColour(c.signal);
        g.fillRect(r.getX() + r.getWidth() * gp - 1.5f, (float)meterR.getY() + 4.f, 3.f, (float)meterR.getHeight() - 8.f);
        g.setColour(c.mid);
        g.setFont(lnf.mono(9.f, true));
        g.drawText("-70", meterR.withTrimmedLeft(14).withHeight(10).translated(0, meterR.getHeight() - 10),
                   juce::Justification::centredLeft);
        g.drawText("0 DB", meterR.withTrimmedRight(14).withHeight(10).translated(0, meterR.getHeight() - 10),
                   juce::Justification::centredRight);
    }

    // ---- what it thinks you are playing in, big
    auto bigCell = [&](juce::Rectangle<int> r, const juce::String& label, const juce::String& value, bool known) {
        g.setColour(c.mid);
        g.setFont(lnf.mono(10.f, true));
        g.drawText(label, r.reduced(14, 0).withTop(r.getY() + 8).withHeight(12), juce::Justification::centredLeft);
        g.setColour(known ? c.ink : c.mid);
        g.setFont(lnf.mono(34.f, true));
        g.drawFittedText(value, r.reduced(14, 0).withTop(r.getY() + 20).withBottom(r.getBottom() - 4),
                         juce::Justification::centredLeft, 1, 0.7f);
    };
    const juce::String key = keyRoot >= 0
        ? brutal::upper(juce::String::fromUTF8(lpfd::Harmony::pcName(keyRoot)) + " " +
                        juce::String::fromUTF8(lpfd::Harmony::scaleName(scaleType)))
        : juce::String("---");
    const juce::String chord = chordRoot >= 0
        ? juce::String(lpfd::Harmony::pcLetter(chordRoot)) + lpfd::Harmony::chordSuffix(chordType)
        : juce::String("---");
    bigCell(keyR, "GAMME", key, keyRoot >= 0);
    bigCell(chordR, "ACCORD", chord, chordRoot >= 0);

    // ---- row heads: number + section name
    for (auto& h : rowHeads) {
        g.setColour(c.ink);
        g.fillRect(h.r);
        g.setColour(c.signal);
        g.setFont(lnf.mono(22.f, true));
        g.drawText(h.num, h.r.reduced(10, 0).withTop(h.r.getY() + 8).withHeight(26), juce::Justification::centredLeft);
        g.setColour(c.paper);
        g.setFont(lnf.mono(10.f, true));
        g.drawText(h.name, h.r.reduced(10, 0).withTop(h.r.getBottom() - 22).withHeight(12), juce::Justification::centredLeft);
    }

    // footer
    g.setColour(c.mid);
    g.setFont(lnf.mono(9.f, true));
    auto foot = getLocalBounds().removeFromBottom(26).reduced(16, 0);
    g.drawText(u8("ÉCOUTE AUDIO/MIDI → RÉPONSE MIDI · GLISSER POUR RÉGLER · DOUBLE-CLIC = DÉFAUT"), foot,
               juce::Justification::centredLeft);
    g.drawText("LIFEPASSFILTER.COM", foot, juce::Justification::centredRight);
}

void DialogueEditor::paintOverChildren(juce::Graphics& g) {
    // the grid: every cell edge is a 2 px ink line, shared between neighbours
    g.setColour(brutal::palette().ink);
    for (auto& r : grid_) g.drawRect(r.toFloat().expanded(1.f), 2.f);
}

void DialogueEditor::resized() {
    grid_.clear();
    rowHeads.clear();
    const int x0 = 16, W = getWidth() - 32;
    int y = 16;

    headerR = {x0, y, W, 64};           grid_.push_back(headerR); y += 64;
    meterR = {x0, y, W, 30};            grid_.push_back(meterR);  y += 30;
    const int keyW = W * 62 / 100;
    keyR = {x0, y, keyW, 72};           grid_.push_back(keyR);
    chordR = {x0 + keyW, y, W - keyW, 72}; grid_.push_back(chordR); y += 72;

    const int headW = 92, rowH = 70;
    auto row = [&](const char* num, const juce::String& name, std::initializer_list<juce::Component*> comps) {
        juce::Rectangle<int> head{x0, y, headW, rowH};
        rowHeads.push_back({head, num, name});
        grid_.push_back(head);
        const int n = (int)comps.size();
        const int avail = W - headW;
        int i = 0, x = x0 + headW;
        for (auto* comp : comps) {
            const int w = (i == n - 1) ? (x0 + W - x) : avail / n;
            juce::Rectangle<int> r{x, y, w, rowH};
            comp->setBounds(r.reduced(1));
            grid_.push_back(r);
            x += w; ++i;
        }
        y += rowH;
    };
    row("01", u8("RÉACTIVITÉ"), {&patience.cell, &interruption.cell, &length.cell, &humanize.cell});
    row("02", u8("CARACTÈRE"), {&memory.cell, &fidelity.cell, &contrast.cell, &scale.cell, &random.cell});
    row("03", u8("OREILLE"), {mode.get(), &sens.cell, &gate.cell, &low.cell, &high.cell});
    row("04", "ROUTAGE", {source.get(), &midiIn.cell, &channel.cell, grid.get()});
    row("05", u8("MÉMOIRE"), {&freeze.button, &respond.button, &thru.button, &clear});
}
