# LPF Dialogue

Un plugin qui écoute ce que tu joues (n'importe quel son : guitare, voix, field recording…) et qui te répond en MIDI. Tout se passe en local, sans réseau ni IA distante.

- **Écoute** : attaques (flux spectral), hauteur (YIN), niveau, brillance, bruit. Les sons sans hauteur nette sont aussi entendus : leur brillance est traduite en hauteur.
- **Mémoire** : retient ce que tu as joué (intervalles, rythmes, durées, nuances, notes utilisées).
- **Réponse** : chaîne de Markov à ordre variable sur ton propre jeu. Elle peut citer ou recombiner, imiter ou contredire.
- **Réactivité** : de « réflexe » (une réponse ~20 ms après chaque note) à « écoute » (il attend la fin de ta phrase), en passant par « il te coupe la parole ».

Formats : VST3, AU (macOS), Standalone. Le plugin laisse passer l'audio sans le modifier et ajoute une sortie MIDI.

## Réglages

| Réglage | Effet |
|---|---|
| **Patience** | 0 % : répond à chaque note. 100 % : attend ~2 s de silence. Entre les deux, il répond après quelques notes ou un court silence. |
| **Interruption** | 0 % : ne parle jamais en même temps que toi et **se tait dès que tu reprends**. 100 % : répond dès que la patience est atteinte, même si tu joues encore. |
| **Longueur** | Nombre de notes de la réponse par rapport à ce qu'il vient d'entendre (×0,25 à ×2). |
| **Mémoire** | 0 % : ne se souvient que des 8 dernières notes. 100 % : des 4096 dernières. |
| **Fidélité** | 0 % : recombine librement. 100 % : cite de longs fragments de ton jeu. |
| **Contraste** | 0 % : imite. 100 % : s'oppose (autre registre, rythme inverse de ta densité, contour renversé, nuances opposées). |
| **Tonalité** | Probabilité de rester dans les notes que tu as jouées. |
| **Note basse / haute** | Tessiture de la réponse (repli à l'octave). |
| **Sensibilité / Seuil** | Détection des attaques. Mets le seuil juste au-dessus du bruit de fond (la barre du haut montre le niveau d'entrée et le seuil). |
| **Grille** | Libre, ou réponses calées sur 1/4, 1/8, 1/16 quand le transport de l'hôte est en lecture. |
| **Canal MIDI**, **Figer la mémoire**, **Répondre**, **Effacer la mémoire** | — |

Quelques réglages de départ :
- **Ping-pong** : Patience 40 %, Interruption 0 %, Longueur ×1.
- **Écho / contrepoint** : Patience 0 %, Interruption 100 %, Longueur ×1, Contraste 30 %.
- **Il te coupe la parole** : Patience 50 %, Interruption 70 %.
- **Il ne fait que citer** : Fidélité 100 %, Contraste 0 %, Tonalité 100 %.

## Brancher dans Reaper

1. Piste 1 (ton instrument ou ton micro, monitoring activé) : insère **LPF Dialogue**.
2. Piste 2 : ton synthé (ou LPF Granu, un sampler…).
3. Envoi de la piste 1 vers la piste 2 : bouton Route → *Add new send* → audio **None**, MIDI **All → All**.
4. Joue.

Dans **Ableton Live**, un VST ne peut pas envoyer de MIDI vers une autre piste. Lance alors la version **Standalone** avec sa sortie MIDI vers un bus virtuel (IAC sur Mac), que Live reçoit comme un clavier. Un device Max for Live est une piste pour plus tard.

## Standalone → Ableton Move ou autre machine

La version Standalone écoute l'entrée de ta carte son. Dans *Options → Audio/MIDI Settings*, choisis la **sortie MIDI** : le Move branché en USB, un bus IAC, etc. Par défaut, JUCE coupe l'entrée audio en standalone pour éviter le larsen (bandeau jaune) : réactive-la si tu écoutes au casque.

Attention au larsen musical : si le synthé qui répond s'entend dans le micro, le plugin finira par se répondre à lui-même. Utilise un casque, ou monte le seuil.

## Outil hors ligne

`lpf_dialogue_cli` passe un fichier WAV dans le moteur et écrit les réponses dans un fichier MIDI. Il peut aussi faire un aperçu stéréo : ton son à gauche, la réponse jouée par un petit synthé à droite.

```
lpf_dialogue_cli phrase.wav reponse.mid --render apercu.wav --patience 0.6 --interruption 0 --contrast 0.3
```

Options : `--patience --interruption --length --memory --fidelity --contrast --scale --low --high --sensitivity --gate --seed`.

## Compiler

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

JUCE 8 est téléchargé automatiquement (ou `-DLPFD_JUCE_DIR=/chemin/vers/JUCE`). Sur Mac, la compilation est universelle (Apple Silicon et Intel). Les plugins sont dans `build/LPFDialogue_artefacts/Release/`. Copie le `.vst3` dans `~/Library/Audio/Plug-Ins/VST3` et le `.component` dans `~/Library/Audio/Plug-Ins/Components`.

Le workflow GitHub Actions (`.github/workflows/build.yml`) compile pour macOS, Windows et Linux, lance les tests et publie une release à chaque tag `v*`. Les binaires ne sont pas signés : sur Mac, après téléchargement, lance `xattr -dr com.apple.quarantine "LPF Dialogue.vst3"` (idem pour le `.component` et l'app).

Licence : JUCE 8 est sous AGPLv3 ou licence commerciale JUCE. Pour diffuser des binaires, publie ce code sous AGPLv3 ou prends une licence JUCE.

## Structure

- `core/` : moteur sans dépendance (écoute, mémoire, génération, ordonnancement). Temps réel : aucune allocation, aucun verrou dans `process()`.
- `plugin/` : emballage JUCE et interface.
- `tools/` : outil en ligne de commande.
- `tests/` : tests sur signaux synthétiques (justesse, latence, modes de réactivité, politesse, tessiture, tonalité, grille, équilibre note-on/note-off, absence d'allocation, fuzz).

## Mesures (tests, 44,1 kHz)

- Hauteur juste à ±0,06 demi-ton de 82 Hz à 1,3 kHz. Attaque datée à 0,3 ms près.
- Note confirmée 20 ms après l'attaque (32 à 38 ms pour les notes graves de guitare, en dessous de 110 Hz).
- Mode réflexe : réponse au plus 21 ms après la note entendue.
- Validé par pluginval (niveau 10, éditeur compris). Sortie MIDI vérifiée dans un hôte VST3.

## Limites actuelles / pistes

- Écoute **monophonique** pour la hauteur. Les accords sont entendus comme une attaque, avec une hauteur approximative ou dérivée de la brillance.
- La mémoire n'est pas sauvegardée avec le projet (seuls les réglages le sont).
- Pistes : réponse polyphonique, corpus MIDI chargé à l'avance (« apprendre » un style), timbre → CC (brillance, bruit) pour moduler le synthé, device Max for Live, module Schwung pour le Move.
