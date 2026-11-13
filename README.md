# Conversations

By Life Pass Filter. In Reaper and other hosts it shows as **Conversations (Life Pass Filter)**. (Before v0.6.1 it was called LPF Dialogue: see "Install" to remove the old copy.)


A plugin that listens to what you play (any sound: guitar, voice, field recordings… or MIDI) and answers you in MIDI. Everything runs locally: no network, no remote AI.

- **Listening**: onsets (spectral flux), pitch (YIN), level, brightness, noisiness. Sounds without a clear pitch are heard too: their brightness becomes a pitch.
- **Memory**: remembers what you played (intervals, rhythms, durations, dynamics, notes used).
- **Answers**: a variable-order Markov chain over your own playing. It can quote or recombine, imitate or contradict.
- **Reactivity**: from "reflex" (an answer ~20 ms after each note) to "listening" (it waits for the end of your phrase), with "it cuts in" in between.
- **Harmony**: detects the scale (11 modes) and the current chord, including strummed chords. The interface shows them, and invented notes use them.
- **Human feel**: Randomness (invented notes, ornaments, rests, shifted rhythms) and Humanize (varying reaction delay, micro-timing, phrasing).
- **Two listening modes**: Instrument, or Bright for birds and voices, and a two-handle range for the pitches it listens for (the low cut also filters out rumble and wind).
- **Expression**: follows your dynamics (soft/loud), holds its note as long as you hold yours, and can follow your bends and vibrato with pitch bend.
- **MIDI input**: it also listens to a keyboard or a MIDI item, alone or together with audio. Exact pitches, zero-latency answers, held chords recognised exactly.

## The interface

- **First run**: three questions. What will you play? (instrument, voice or birds, MIDI keyboard, both). Teach it your silence: it measures 3 s of room noise and sets the threshold just above it. What kind of partner do you want? (six ready-made characters). The welcome screen can be reopened from Help or from the three chips at the top.
- **The state, in plain words**: LISTENING, WAITING (with a countdown of the silence before it answers), ANSWERING, or "your turn". Memory, Freeze and Clear (with confirmation) sit next to it.
- **The conversation**: the last 12 seconds on two lanes, You and It, with the reason under each answer ("after 0.8 s of silence", "cut in after 5 notes", "reflex").
- **What it hears**: level meter with the threshold in orange, a Measure silence button, the detected note, scale and chord.
- **Its character**: six characters (Echo, Ping-pong, Partner, Contrarian, Quotes, Bird; shortcuts 1 to 6) and six sliders phrased as questions ("When does it answer?", "Does it cut in?"…). Under each slider, a sentence says what it will do.
- **Advanced settings**: Instrument/Bright listening, the pitches it listens for (two handles on a frequency axis, with a mark on the note it hears), sensitivity, expression (dynamics, held notes, bends and vibrato, your synth's bend range), answer range on an octave strip, memory, quoting, scale lock, grid, source, MIDI channels, MIDI thru.
- **Routing check** ("Nothing coming out?" at the bottom): three checks (incoming sound, answers sent in the last minute, your synth), a test note, and the steps for Reaper, Ableton Live and the standalone app.
- **Display and accessibility** (kept for all projects): Paper, Ink and Max contrast themes; 100, 125 or 150 % size (the window grows, nothing gets cut off; you can also drag the corner); screen-reader announcements (state, scale and chord); a steady orange frame while it answers; reduced motion; shapes as well as colours.
- **Keyboard**: Tab / Shift+Tab to move between controls (visible focus frame), ← → to adjust, Shift for fine adjustment, Del for the default value, Space to turn answering on/off, F to freeze, S to measure silence, 1 to 6 for the characters, ? for help, Esc to close.
- Text in Atkinson Hyperlegible (a typeface designed for legibility), titles and values in Space Mono. Contrast ≥ 4.5:1 in all three themes; the bright orange is only used as a background.

Formats: VST3, AU (macOS), Standalone. The plugin passes audio through untouched and adds a MIDI output.

## Install

Download the archive for your system from the latest release: https://github.com/antoinensl/LPF_Conversations/releases/latest

**What's in the archive**

| File | What it's for |
|---|---|
| `Conversations.vst3` | The plugin, for Reaper and other music software. The only file you really need. |
| `Conversations.component` (Mac) | The same plugin in AU format, for Logic or GarageBand. |
| `Conversations.app` (Mac) / `Conversations.exe` (Windows) | The standalone app: the plugin without any music software. For Ableton Live (which doesn't receive MIDI from a plugin), or to play straight into a synth or the Ableton Move over USB. Optional. |
| `conversations_cli` / `conversations_cli.exe` | The command-line tool (see "Offline tool" below): a WAV or MIDI file in, its answers out as a MIDI file. Optional. |
| `README.md` | This manual. |

**Mac**

1. Unzip `LPF-Conversations-macOS.zip`.
2. Copy `Conversations.vst3` to `~/Library/Audio/Plug-Ins/VST3` and `Conversations.component` to `~/Library/Audio/Plug-Ins/Components`.
3. The files are not signed. In Terminal, once per file:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Conversations.vst3"` (same for the `.component` and the app).
4. In Reaper: Options › Preferences › Plug-ins › VST › Re-scan.

**Windows**

1. Before unzipping: right-click `LPF-Conversations-Windows.zip` › Properties › tick **Unblock** › OK.
2. Unzip, then copy the whole `Conversations.vst3` folder to `C:\Program Files\Common Files\VST3` (Windows asks for administrator permission).
3. In Reaper: Options › Preferences › Plug-ins › VST: check that this folder is in the list, then **Re-scan**. The plugin is called "Conversations (Life Pass Filter)".
4. The standalone app is not signed: on first launch, "Windows protected your PC" › **More info › Run anyway**.
5. If a message mentions `VCRUNTIME140.dll`: install the "Microsoft Visual C++ Redistributable" (x64) from Microsoft's website.
6. For Ableton Live: run the standalone app, send its MIDI output to a **loopMIDI** virtual port (free), and pick that port as the MIDI input of the synth track in Live.

**Coming from LPF Dialogue (v0.6.0 or earlier)**: delete the old `LPF Dialogue.vst3` (and `LPF Dialogue.component` on Mac), then re-scan. Projects that used it open with Conversations and keep their settings: only the name changed.

**Linux**: copy `Conversations.vst3` to `~/.vst3`.

## Settings (parameter names in the host)

| Setting | Effect |
|---|---|
| **Patience** | 0 %: answers every note. 100 %: waits for ~2 s of silence. In between, it answers after a few notes or a short silence. |
| **Interruption** | 0 %: never plays at the same time as you and **stops as soon as you start again**. Up to 50 %: cuts in now and then (twice the value, in % of the chances), and still stops when you start again. From 50 %: answers every time its patience is reached, even if you're still playing. Above 0 %, a note left ringing at the end of a phrase counts as the end of the phrase (it doesn't wait for it to die away). |
| **Length** | Number of notes in the answer relative to what it just heard (×0.25 to ×2). |
| **Memory** | 0 %: only remembers the last 8 notes. 100 %: the last 4096. |
| **Fidelity** | 0 %: recombines freely. 100 %: quotes long fragments of your playing. |
| **Contrast** | 0 %: imitates. 100 %: opposes (other register, rhythm opposite to your density, inverted contour, opposite dynamics). |
| **Scale lock** | Probability of staying in the detected scale (or in the notes you played, until the scale is certain). |
| **Randomness** | Adds invented notes: chord tones at the start and end of phrases, scale notes elsewhere. Also adds grace notes, rests, shifted rhythms and a varying answer length. It sometimes lets a chance to answer go by, and says more the next time. |
| **Humanize** | Varying reaction delay (short in reflex mode, longer when it's patient), micro-timing (±25 ms at most), dynamics arching over the phrase, varying durations, last note held. |
| **Listening mode** | **Instrument**: for guitar and instruments. **Bright**: onsets separated from 15 ms (trills), glides followed note by note. For birds and spoken or sung voice. Picking one also sets its usual band (55 Hz - 2 kHz, or 100 Hz - 10 kHz). |
| **Low cut / High cut** | The pitches it listens for. Below the low cut, sound is filtered out before listening (rumble, wind, handling noise). Above the high cut, notes are not heard as pitches. |
| **Dynamics** | 0 %: every answer at the same velocity. 100 %: soft when you play soft, loud when you play loud, spread over the whole velocity range (it learns the range of your attacks, so it works whatever your mic gain). |
| **Held notes** | The chance that, while you are still holding your note, its answer note is held too and released with yours (8 s at most). At 100 % in reflex mode, every note you hold, it holds. |
| **Bends** / **Bend range** | Follows your bends, slides and vibrato (audio) or your pitch wheel (MIDI) with pitch bend on its sounding notes. Set Bend range to your synth's pitch bend range (±2 by default on most synths). |
| **Lowest note / Highest note** | Range of the answers (folded by octaves). |
| **Sensitivity / Threshold** | Onset detection. Put the threshold just above the room noise (the meter shows the input level and the threshold). |
| **Grid** | Free, or answers locked to 1/4, 1/8, 1/16 while the host transport is playing. |
| **Source** | Audio, MIDI, or Audio + MIDI (for example guitar and keyboard together). |
| **Input channel** | MIDI channel it listens to (All or 1-16). |
| **MIDI thru** | Also sends the incoming MIDI to the output, mixed with the answers (then put the answers on another channel). |
| **Output channel**, **Freeze memory**, **Answer**, **Clear memory** | — |

Some starting points:
- **Ping-pong**: Patience 40 %, Interruption 0 %, Length ×1.
- **Echo / counterpoint**: Patience 0 %, Interruption 100 %, Length ×1, Contrast 30 %.
- **It cuts in**: Patience 50 %, Interruption 70 %.
- **Quotes only**: Fidelity 100 %, Contrast 0 %, Scale lock 100 %, Randomness 0 %.
- **Improv partner**: Patience 50 %, Interruption 30 %, Randomness 35 %, Humanize 50 %, Scale lock 80 %.
- **Birds**: Bright listening, Highest note at maximum (answers are folded by octaves into the range), Scale lock 0 %, Threshold just above the wind.

## Routing in Reaper

1. Track 1 (your instrument or mic, monitoring on): insert **Conversations**.
2. Track 2: your synth (or LPF Granu, a sampler…).
3. Send from track 1 to track 2: Route button → *Add new send* → audio **None**, MIDI **All → All**.
4. Play.

**With MIDI**: set Source to MIDI. Track 1 receives your keyboard (armed, monitoring on) or holds a MIDI item. The send to track 2 stays the same. With MIDI thru, a single synth track can play both your keyboard and the answers; put the answers on another output channel if you want to keep them apart.

In MIDI, notes played together (within 30 ms) count as one melodic event, the top note. All notes are used to recognise the chord. If its answers come back to it (MIDI loop), it recognises and ignores them: a note on its output channel and pitch arriving less than 100 ms after it played that note is taken as an echo. Separate channels are still better. The pitch wheel of your keyboard is heard too (assumed ±2 semitones), for Bends.

In **Ableton Live**, a VST can't send MIDI to another track. Run the **Standalone** app instead, with its MIDI output going to a virtual bus (IAC on Mac, loopMIDI on Windows), which Live receives like a keyboard. A Max for Live device is an idea for later.

## Standalone → Ableton Move or another machine

The standalone app listens to your audio interface input and/or a MIDI input. In *Options → Audio/MIDI Settings*, pick the **MIDI input** (your keyboard) and the **MIDI output**: the Move over USB, an IAC bus, etc. By default JUCE mutes the audio input in standalone mode to avoid feedback (yellow banner): turn it back on if you're using headphones.

Watch out for musical feedback: if the answering synth can be heard in the mic, the plugin will end up answering itself. Use headphones, or raise the threshold.

## Offline tool

`conversations_cli` runs a WAV **or MIDI** file (.mid, format 0/1, tempo changes supported) through the engine and writes the answers to a MIDI file. It can also render a stereo preview: your playing on the left, the answers played by a small synth on the right.

```
conversations_cli phrase.wav answers.mid --render preview.wav --patience 0.6 --interruption 0 --contrast 0.3
```

Options: `--patience --interruption --length --memory --fidelity --contrast --scale --low --high --sensitivity --gate --seed --bright 1 --random --humanize --lowcut --highcut --dynamics --sustain --bend --bendrange`. Pitch bend is written to the MIDI file. The tool also prints the detected scale and the last chord.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

JUCE 8 is downloaded automatically (or use `-DLPFD_JUCE_DIR=/path/to/JUCE`). On Mac the build is universal (Apple Silicon and Intel). The plugins end up in `build/LPFConversations_artefacts/Release/`.

The GitHub Actions workflow (`.github/workflows/build.yml`) builds for macOS, Windows and Linux and runs the tests. On every push to main it publishes (or updates) the release `v<version>`, with the version number read from `CMakeLists.txt` (`project(LPFConversations VERSION …)`): to ship a new version, bump that number. The binaries are not signed (see "Install").

Extra fuzzing: `LPFD_FUZZ_SEEDS=40 ./build/lpfd_tests` runs the fuzz test with 40 random seeds.

Fonts: Atkinson Hyperlegible and Space Mono (SIL Open Font License, `assets/fonts/OFL-*.txt`), embedded in the plugin.


## Layout

- `core/`: dependency-free engine (listening, memory, generation, scheduling). Real-time safe: no allocation, no locks in `process()`.
- `plugin/`: JUCE wrapper and interface (`Ui.*`: themes, fonts, display settings, characters; `PluginEditor.*`: the screens).
- `tools/`: command-line tool.
- `tests/`: tests on synthetic signals: pitch accuracy, latency, reactivity modes, politeness, range, scale lock, grid, Bright mode (whistles, trills, glides, syllables), chords and scales, randomness and humanize, MIDI input, note-on/note-off balance, no allocation, fuzzing.

## Measurements (tests, 44.1 kHz)

- Pitch accurate to ±0.06 semitone from 82 Hz to 1.3 kHz. Onsets timed to within 0.3 ms.
- Note confirmed 20 ms after the attack (32 to 38 ms for low guitar notes, below 110 Hz).
- Reflex mode: answer at most 21 ms after the note it heard (35 ± 13 ms with Humanize at 100 %). On a plucked phrase (notes ringing into each other), every note answered within 33 ms with "Does it cut in?" at 50 % or 80 %.
- Same note re-plucked while it still rings, at the same loudness: 8 attacks heard out of 8.
- A phrase ending on a note held 4 s: answered 1.2 s after its attack instead of after the note dies away.
- Expression: soft/loud playing gives answers from velocity 46 to 112 (all at 89 with Dynamics at 0). A held note of 1.5 s gets an answer held 1.51 s. A bend of +0.6 semitone is followed to +0.56; a MIDI pitch wheel to +1 semitone to +0.99.
- Bright: whistles from 2.5 to 9 kHz accurate to ±0.2 semitone, note confirmed in ~7 ms. A 14-note trill of 30 ms notes fully heard (Instrument mode hears only 9). A 2 → 4 kHz glide followed as 7 rising notes.
- MIDI: reflex answer in the same block as the incoming note (0 ms). Held C-E-G-Bb recognised as C7. MIDI loop without self-dialogue. Channel filter, audio + MIDI mix, waits for keys to be released in listening mode. MIDI thru checked in a VST3 host.
- Strummed chords recognised: C, Am, G7, Fmaj7, plus a C → G change. Am7 arpeggio recognised. Scales recognised: D dorian, A harmonic minor, minor pentatonic.
- Passes pluginval (strictness 10, editor included). MIDI output checked in a VST3 host.

## Current limits / ideas

- **Notes** are heard one at a time. Chords are recognised for harmony (scale, chord) but not yet played back as chords.
- Tuning reference A = 440 Hz; a badly detuned instrument confuses harmony detection.
- The memory is not saved with the project (only the settings are).
- Bends are measured from where your note started: a slide wide enough to be heard as a new note starts a new answer instead of bending the old one.
- Ideas: polyphonic answers (chords), glides of its own between its notes, a MIDI corpus loaded in advance ("learn" a style), timbre → CC (brightness, noisiness) to modulate the synth, a Max for Live device, a Schwung module for the Move.

## License

© 2026 Antoine Boucherikha (Life Pass Filter). Conversations is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License, version 3 (see `LICENSE`). It comes with no warranty.

It is built with JUCE (used under AGPLv3) and other open-source components listed in `THIRD_PARTY_NOTICES.md`. Every release zip includes these files.
