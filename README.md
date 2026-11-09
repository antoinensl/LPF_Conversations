# LPF Dialogue

Un plugin qui écoute ce que tu joues (n'importe quel son : guitare, voix, field recording… ou du MIDI) et qui te répond en MIDI. Tout se passe en local, sans réseau ni IA distante.

- **Écoute** : attaques (flux spectral), hauteur (YIN), niveau, brillance, bruit. Les sons sans hauteur nette sont aussi entendus : leur brillance est traduite en hauteur.
- **Mémoire** : retient ce que tu as joué (intervalles, rythmes, durées, nuances, notes utilisées).
- **Réponse** : chaîne de Markov à ordre variable sur ton propre jeu. Elle peut citer ou recombiner, imiter ou contredire.
- **Réactivité** : de « réflexe » (une réponse ~20 ms après chaque note) à « écoute » (il attend la fin de ta phrase), en passant par « il te coupe la parole ».
- **Harmonie** : détecte la gamme (11 modes) et l'accord en cours, y compris sur des accords grattés. L'interface les affiche, et les notes inventées s'en servent.
- **Jeu humain** : Hasard (notes inventées, ornements, silences, rythmes déplacés) et Humanisation (délai de réaction variable, micro-timing, phrasé).
- **Deux modes d'écoute** : Instrument, ou Bright pour les oiseaux et les voix.
- **Entrée MIDI** : il écoute aussi un clavier ou un item MIDI, seul ou en même temps que l'audio. Hauteurs exactes, réponse sans latence, accords tenus reconnus exactement.

## L'interface

- **Premier lancement** : trois questions. Qu'allez-vous jouer ? (instrument, voix ou oiseaux, clavier MIDI, les deux). Apprenez-lui votre silence : il mesure 3 s de bruit de fond et règle le seuil juste au-dessus. Quel partenaire voulez-vous ? (six caractères prêts à jouer). L'accueil se rouvre depuis l'aide ou les trois pastilles du haut.
- **L'état, en clair** : ÉCOUTE, ATTEND (avec le compte à rebours du silence avant qu'il réponde), RÉPOND, ou « à vous ». La mémoire, Figer et Effacer (avec confirmation) sont à côté.
- **La conversation** : les 12 dernières secondes sur deux lignes, Vous et Lui, avec sous chaque réponse la raison pour laquelle il a parlé (« après 0,8 s de silence », « vous a coupé après 5 notes », « réflexe »).
- **Ce qu'il entend** : vumètre avec le seuil en orange, bouton Mesurer le silence, note, gamme et accord détectés.
- **Son caractère** : six caractères (Écho, Ping-pong, Partenaire, Contradicteur, Citations, Oiseau, raccourcis 1 à 6) et six curseurs posés comme des questions (« Quand répond-il ? », « Vous coupe-t-il la parole ? »…). Sous chaque curseur, une phrase dit ce qu'il fera.
- **Réglages avancés** : écoute Instrument/Bright, sensibilité, tessiture sur une bande d'octaves, mémoire, citation, gamme, grille, source, canaux MIDI, MIDI thru.
- **Branchement** (« Rien ne sort ? » en bas) : trois vérifications (son entrant, réponses envoyées dans la dernière minute, synthé), une note test, et les étapes pour Reaper, Ableton Live et la version autonome.
- **Affichage et accessibilité** (gardé pour tous les projets) : thèmes Papier, Encre et Contraste max ; taille 100, 125 ou 150 % (la fenêtre grandit, rien n'est coupé ; on peut aussi tirer le coin) ; annonces pour les lecteurs d'écran (état, gamme et accord) ; cadre orange fixe pendant qu'il répond ; animations réduites ; formes en plus des couleurs.
- **Clavier** : Tab / Maj+Tab pour passer d'un réglage à l'autre (cadre de focus visible), ← → pour régler, Maj pour le réglage fin, Suppr pour la valeur par défaut, Espace pour répondre oui/non, F pour figer, S pour mesurer le silence, 1 à 6 pour les caractères, ? pour l'aide, Échap pour fermer.
- Texte en Atkinson Hyperlegible (police conçue pour la lisibilité), titres et valeurs en Space Mono. Contrastes ≥ 4,5:1 dans les trois thèmes ; l'orange vif ne sert qu'en fond.

Formats : VST3, AU (macOS), Standalone. Le plugin laisse passer l'audio sans le modifier et ajoute une sortie MIDI.

## Les réglages (noms des paramètres dans l'hôte)

| Réglage | Effet |
|---|---|
| **Patience** | 0 % : répond à chaque note. 100 % : attend ~2 s de silence. Entre les deux, il répond après quelques notes ou un court silence. |
| **Interruption** | 0 % : ne parle jamais en même temps que toi et **se tait dès que tu reprends**. 100 % : répond dès que la patience est atteinte, même si tu joues encore. |
| **Longueur** | Nombre de notes de la réponse par rapport à ce qu'il vient d'entendre (×0,25 à ×2). |
| **Mémoire** | 0 % : ne se souvient que des 8 dernières notes. 100 % : des 4096 dernières. |
| **Fidélité** | 0 % : recombine librement. 100 % : cite de longs fragments de ton jeu. |
| **Contraste** | 0 % : imite. 100 % : s'oppose (autre registre, rythme inverse de ta densité, contour renversé, nuances opposées). |
| **Tonalité** | Probabilité de rester dans la gamme détectée (ou dans les notes jouées, tant que la gamme n'est pas sûre). |
| **Hasard** | Ajoute des notes inventées : notes de l'accord en début et fin de phrase, notes de la gamme ailleurs. Ajoute aussi des petites notes d'ornement, des silences, des rythmes déplacés, une longueur de réponse variable. Il laisse parfois passer une occasion de répondre, et en dit plus la fois suivante. |
| **Humanisation** | Délai de réaction variable (court en mode réflexe, plus long quand il est patient), micro-timing (±25 ms au maximum), nuances en arc sur la phrase, durées qui varient, dernière note tenue. |
| **Écoute** | **Instrument** : 55 Hz - 2 kHz, pour la guitare et les instruments. **Bright** : 100 Hz - 10 kHz, attaques séparées dès 15 ms (trilles), glissandi suivis note à note. Pour les oiseaux et la voix parlée ou chantée. |
| **Note basse / haute** | Tessiture de la réponse (repli à l'octave). |
| **Sensibilité / Seuil** | Détection des attaques. Mets le seuil juste au-dessus du bruit de fond (la barre du haut montre le niveau d'entrée et le seuil). |
| **Grille** | Libre, ou réponses calées sur 1/4, 1/8, 1/16 quand le transport de l'hôte est en lecture. |
| **Source** | Audio, MIDI, ou Audio + MIDI (par exemple guitare et clavier en même temps). |
| **Entrée MIDI** | Canal écouté (Tous ou 1-16). |
| **MIDI thru** | Renvoie aussi le MIDI reçu sur la sortie, mélangé aux réponses (mets alors les réponses sur un autre canal). |
| **Canal sortie**, **Figer la mémoire**, **Répondre**, **Effacer la mémoire** | — |

Quelques réglages de départ :
- **Ping-pong** : Patience 40 %, Interruption 0 %, Longueur ×1.
- **Écho / contrepoint** : Patience 0 %, Interruption 100 %, Longueur ×1, Contraste 30 %.
- **Il te coupe la parole** : Patience 50 %, Interruption 70 %.
- **Il ne fait que citer** : Fidélité 100 %, Contraste 0 %, Tonalité 100 %, Hasard 0 %.
- **Partenaire d'impro** : Patience 50 %, Interruption 30 %, Hasard 35 %, Humanisation 50 %, Tonalité 80 %.
- **Oiseaux** : Écoute Bright, Note haute au maximum (les réponses sont repliées par octaves dans la tessiture), Tonalité 0 %, Seuil juste au-dessus du vent.

## Brancher dans Reaper

1. Piste 1 (ton instrument ou ton micro, monitoring activé) : insère **LPF Dialogue**.
2. Piste 2 : ton synthé (ou LPF Granu, un sampler…).
3. Envoi de la piste 1 vers la piste 2 : bouton Route → *Add new send* → audio **None**, MIDI **All → All**.
4. Joue.

**En MIDI** : mets Source sur MIDI. La piste 1 reçoit ton clavier (armée, monitoring actif) ou contient un item MIDI. L'envoi vers la piste 2 reste le même. Avec MIDI thru, une seule piste de synthé peut jouer ton clavier et les réponses ; mets alors les réponses sur un autre canal de sortie si tu veux les séparer.

En MIDI, les notes jouées ensemble (à moins de 30 ms) comptent comme un seul évènement mélodique, la note du haut. Toutes les notes servent à reconnaître l'accord. Si ses réponses reviennent vers lui (boucle MIDI), il les reconnaît et les ignore. Un vrai clavier sur le même canal et la même note qu'une réponse en train de sonner serait aussi ignoré : mieux vaut des canaux différents.

Dans **Ableton Live**, un VST ne peut pas envoyer de MIDI vers une autre piste. Lance alors la version **Standalone** avec sa sortie MIDI vers un bus virtuel (IAC sur Mac), que Live reçoit comme un clavier. Un device Max for Live est une piste pour plus tard.

## Standalone → Ableton Move ou autre machine

La version Standalone écoute l'entrée de ta carte son et/ou une entrée MIDI. Dans *Options → Audio/MIDI Settings*, choisis l'**entrée MIDI** (ton clavier) et la **sortie MIDI** : le Move branché en USB, un bus IAC, etc. Par défaut, JUCE coupe l'entrée audio en standalone pour éviter le larsen (bandeau jaune) : réactive-la si tu écoutes au casque.

Attention au larsen musical : si le synthé qui répond s'entend dans le micro, le plugin finira par se répondre à lui-même. Utilise un casque, ou monte le seuil.

## Outil hors ligne

`lpf_dialogue_cli` passe un fichier WAV **ou MIDI** (.mid, format 0/1, changements de tempo gérés) dans le moteur et écrit les réponses dans un fichier MIDI. Il peut aussi faire un aperçu stéréo : ton jeu à gauche, la réponse jouée par un petit synthé à droite.

```
lpf_dialogue_cli phrase.wav reponse.mid --render apercu.wav --patience 0.6 --interruption 0 --contrast 0.3
```

Options : `--patience --interruption --length --memory --fidelity --contrast --scale --low --high --sensitivity --gate --seed --bright 1 --random --humanize`. L'outil affiche aussi la gamme et le dernier accord détectés.

## Compiler

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

JUCE 8 est téléchargé automatiquement (ou `-DLPFD_JUCE_DIR=/chemin/vers/JUCE`). Sur Mac, la compilation est universelle (Apple Silicon et Intel). Les plugins sont dans `build/LPFDialogue_artefacts/Release/`. Copie le `.vst3` dans `~/Library/Audio/Plug-Ins/VST3` et le `.component` dans `~/Library/Audio/Plug-Ins/Components`.

Le workflow GitHub Actions (`.github/workflows/build.yml`) compile pour macOS, Windows et Linux, lance les tests et publie une release à chaque tag `v*`. Les binaires ne sont pas signés : sur Mac, après téléchargement, lance `xattr -dr com.apple.quarantine "LPF Dialogue.vst3"` (idem pour le `.component` et l'app).

Polices : Atkinson Hyperlegible et Space Mono (SIL Open Font License, `assets/fonts/OFL-*.txt`), intégrées au plugin.

Licence : JUCE 8 est sous AGPLv3 ou licence commerciale JUCE. Pour diffuser des binaires, publie ce code sous AGPLv3 ou prends une licence JUCE.

## Structure

- `core/` : moteur sans dépendance (écoute, mémoire, génération, ordonnancement). Temps réel : aucune allocation, aucun verrou dans `process()`.
- `plugin/` : emballage JUCE et interface (`Ui.*` : thèmes, polices, réglages d'affichage, caractères ; `PluginEditor.*` : les écrans).
- `tools/` : outil en ligne de commande.
- `tests/` : tests sur signaux synthétiques : justesse, latence, modes de réactivité, politesse, tessiture, tonalité, grille, mode Bright (sifflements, trilles, glissandi, syllabes), accords et gammes, hasard et humanisation, équilibre note-on/note-off, absence d'allocation, fuzz.

## Mesures (tests, 44,1 kHz)

- Hauteur juste à ±0,06 demi-ton de 82 Hz à 1,3 kHz. Attaque datée à 0,3 ms près.
- Note confirmée 20 ms après l'attaque (32 à 38 ms pour les notes graves de guitare, en dessous de 110 Hz).
- Mode réflexe : réponse au plus 21 ms après la note entendue (35 ± 13 ms avec Humanisation à 100 %).
- Bright : sifflements de 2,5 à 9 kHz justes à ±0,2 demi-ton, note confirmée en ~7 ms. Trille de 14 notes de 30 ms entièrement entendue (le mode Instrument n'en entend que 9). Glissando 2 → 4 kHz suivi en 7 notes montantes.
- MIDI : réponse réflexe sur la même trame que la note reçue (0 ms). Accord tenu C-E-G-Bb reconnu C7. Boucle MIDI sans auto-dialogue. Filtre de canal, mélange audio + MIDI, attente des touches relâchées en mode écoute. MIDI thru vérifié dans un hôte VST3.
- Accords grattés reconnus : C, Am, G7, Fmaj7, plus un changement C → G. Arpège Am7 reconnu. Gammes reconnues : Ré dorien, La mineur harmonique, penta mineure.
- Validé par pluginval (niveau 10, éditeur compris). Sortie MIDI vérifiée dans un hôte VST3.

## Limites actuelles / pistes

- Les **notes** sont entendues une par une. Les accords, eux, sont reconnus pour l'harmonie (gamme, accord), mais pas encore rejoués comme accords.
- Accord de référence La 440 Hz ; un instrument très désaccordé brouille la détection d'harmonie.
- La mémoire n'est pas sauvegardée avec le projet (seuls les réglages le sont).
- Pistes : réponse polyphonique (accords), pitch bend pour les glissandi en mode Bright, corpus MIDI chargé à l'avance (« apprendre » un style), timbre → CC (brillance, bruit) pour moduler le synthé, device Max for Live, module Schwung pour le Move.
