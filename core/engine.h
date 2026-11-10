// LPF Dialogue — listens to audio, remembers, answers in MIDI.
// prepare() allocates; process() is real-time safe.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "analyzer.h"
#include "memory.h"

namespace lpfd {

struct Params {
    float patience = 0.5f;      // 0 = reflex (answers every note) .. 1 = waits for long silences
    float interruption = 0.3f;  // 0 = never talks over you / yields .. 1 = talks over you
    float length = 1.0f;        // answer length relative to what it just heard (0.25..2)
    float memory = 0.5f;        // 0 = only the last few notes .. 1 = everything heard
    float fidelity = 0.6f;      // 0 = free recombination .. 1 = quotes long fragments
    float contrast = 0.2f;      // 0 = imitates .. 1 = opposes (register, rhythm, contour, dynamics)
    float scaleLock = 0.7f;     // probability to snap to the pitch classes you played
    int rangeLow = 36, rangeHigh = 96;
    float sensitivity = 0.5f;   // onset sensitivity
    float gateDb = -55.f;
    int grid = 0;               // 0 off, 1 = 1/4, 2 = 1/8, 3 = 1/16 (needs host transport)
    int channel = 1;            // 1..16
    bool freeze = false;        // stop learning (still listens and answers)
    bool respond = true;        // false = listen only
    bool bright = false;        // listening mode for birds / voices (high, fast, gliding)
    float randomness = 0.f;     // 0 = only what it learnt .. 1 = many invented notes (in scale / chord)
    float humanize = 0.f;       // 0 = machine .. 1 = loose timing, reaction delay, phrasing
    int source = 0;             // what it listens to: 0 = audio, 1 = MIDI, 2 = both
    int midiInChannel = 0;      // 0 = any channel, else 1..16
    float lowCutHz = 0.f;       // pitches it listens for (0 = automatic for the listening mode)
    float highCutHz = 0.f;
    // expression
    float dynamics = 0.7f;      // 0 = even velocity .. 1 = follows your dynamics (over your own range)
    float sustain = 0.5f;       // chance that an answer note is held as long as you hold yours
    float bend = 0.f;           // 0 = ignores your bends/vibrato .. 1 = follows them (pitch bend)
    int bendRange = 2;          // synth's pitch bend range, semitones
};

// What the engine thinks you are playing in.
struct Harmony {
    enum Scale { Major, Minor, Dorian, Mixolydian, Phrygian, Lydian, HarmonicMinor,
                 MajorPenta, MinorPenta, Blues, WholeTone, NumScales };
    enum Chord { Maj, Min, Dom7, Maj7, Min7, Dim, Sus4, Power, NumChords };
    int keyRoot = -1;           // pitch class 0..11, -1 = unknown
    int scale = Major;
    uint16_t scaleMask = 0;     // bit per pitch class
    float keyConfidence = 0.f;  // 0..1
    int chordRoot = -1;
    int chord = Maj;
    uint16_t chordMask = 0;
    float chordConfidence = 0.f;
    static const char* scaleName(int s);
    static const char* chordSuffix(int c);
    static const char* pcName(int pc);     // C, C#, D, ... (same as pcLetter)
    static const char* pcLetter(int pc);   // C, D, ... (chord symbols)
};

struct Transport {
    bool valid = false;
    bool playing = false;
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;
};

class MidiSink {
public:
    virtual ~MidiSink() = default;
    virtual void noteOn(int sampleOffset, int channel, int pitch, int velocity) = 0;
    virtual void noteOff(int sampleOffset, int channel, int pitch) = 0;
    virtual void pitchBend(int /*sampleOffset*/, int /*channel*/, int /*value 0..16383, 8192 = centre*/) {}
};

// What happened, for the conversation display. Written by the audio thread, read by the UI
// (each entry is two atomics; a reader detects entries overwritten while it was reading).
struct TimelineEvent {
    enum Kind : uint8_t { PlayerOn, PlayerOff, AnswerOn, AnswerOff, Trigger };
    enum Reason : uint8_t { Silence, Interjection, Reflex, Held };
    int64_t time = 0;   // samples
    uint8_t kind = 0, pitch = 0, reason = 0;
    float info = 0.f;   // Trigger: seconds waited (Silence, Held) or notes heard (Interjection/Reflex)
};

class Timeline {
public:
    static constexpr int kSize = 2048;
    Timeline() = default;
    Timeline(const Timeline&) {}                                 // a copy starts empty
    Timeline& operator=(const Timeline&) { clear(); return *this; }
    void clear() { written_.store(0, std::memory_order_relaxed); }
    void push(const TimelineEvent& e) {
        const uint64_t n = written_.load(std::memory_order_relaxed);
        Slot& s = slots_[n % kSize];
        uint32_t infoBits;
        std::memcpy(&infoBits, &e.info, 4);
        s.a.store((uint64_t)e.time, std::memory_order_relaxed);
        s.b.store((uint64_t)e.kind | (uint64_t)e.pitch << 8 | (uint64_t)e.reason << 16 | (uint64_t)infoBits << 32,
                  std::memory_order_relaxed);
        written_.store(n + 1, std::memory_order_release);
    }
    // Copies up to `max` of the most recent events, oldest first. Returns how many.
    int read(TimelineEvent* out, int max) const {
        const uint64_t end = written_.load(std::memory_order_acquire);
        const uint64_t count = std::min<uint64_t>({end, (uint64_t)max, (uint64_t)kSize - 64});
        const uint64_t begin = end - count;
        for (uint64_t i = begin; i < end; ++i) {
            const Slot& s = slots_[i % kSize];
            TimelineEvent& e = out[i - begin];
            e.time = (int64_t)s.a.load(std::memory_order_relaxed);
            const uint64_t b = s.b.load(std::memory_order_relaxed);
            e.kind = (uint8_t)b; e.pitch = (uint8_t)(b >> 8); e.reason = (uint8_t)(b >> 16);
            const uint32_t infoBits = (uint32_t)(b >> 32);
            std::memcpy(&e.info, &infoBits, 4);
        }
        // anything the writer may have lapped meanwhile is dropped
        const uint64_t after = written_.load(std::memory_order_acquire);
        const uint64_t safeFrom = after > (uint64_t)kSize - 64 ? after - ((uint64_t)kSize - 64) : 0;
        if (safeFrom > begin) {
            const uint64_t drop = std::min<uint64_t>(safeFrom - begin, count);
            for (uint64_t i = drop; i < count; ++i) out[i - drop] = out[i];
            return (int)(count - drop);
        }
        return (int)count;
    }

private:
    struct Slot { std::atomic<uint64_t> a{0}, b{0}; };
    Slot slots_[kSize];
    std::atomic<uint64_t> written_{0};
};

struct GenNote {
    float time = 0.f;  // seconds from answer start
    float dur = 0.2f;
    int pitch = 60;
    int velocity = 90;
};

class Engine {
public:
    static constexpr int kMemCapacity = 4096;
    static constexpr int kMaxAnswer = 32;
    static constexpr int kPool = 512;

    void prepare(double sampleRate);
    void reset();
    void setParams(const Params& p);
    void requestClearMemory() { clearRequested_ = true; }
    void setSeed(uint32_t s) { rng_ = s ? s : 1u; }

    // Incoming MIDI for the next process() call (offset within that block). Real-time safe.
    void addMidiInput(int sampleOffset, bool noteOn, int channel, int pitch, int velocity);
    void addMidiPitchBend(int sampleOffset, int channel, int value);   // 0..16383
    void process(const float* monoIn, int n, const Transport& tr, MidiSink& out);
    void allNotesOff(int sampleOffset, MidiSink& out);

    // For UI / tests
    enum class Phase { Silence, Listening, Waiting, Answering };
    Phase phase() const { return phase_; }
    float waitProgress() const { return waitProgress_; }      // 0..1 while Waiting
    float waitRemainingSeconds() const { return waitRemaining_; }
    int heardInPhrase() const { return newEvents_; }
    uint64_t notesSent() const { return notesSent_; }
    const Timeline& timeline() const { return timeline_; }
    void requestTestNote() { testNoteRequested_ = true; }
    int outputChannel() const { return p_.channel; }

    int memorySize() const { return memory_.size(); }
    int64_t now() const { return now_; }
    bool hearing() const { return (p_.source != 1 && analyzer_.isActive()) || (p_.source != 0 && heldCount_ > 0); }
    bool answering() const { return answering_; }
    int eventsNeeded() const;
    double silenceWaitSeconds() const;
    const Analyzer& analyzer() const { return analyzer_; }
    const Harmony& harmony() const { return harmony_; }
    void updateHarmony();

    // Exposed for tests: generate an answer from the current memory.
    int generate(int n, GenNote* out, int capacity);

private:
    struct Slot {
        int64_t time = 0;
        uint32_t id = 0;
        uint8_t pitch = 0, vel = 0;
        bool on = false, used = false;
        bool tied = false;          // note-off waiting for you to let go of your note
        int64_t onTime = 0, plannedOff = 0;
    };

    struct MidiIn { int offset; bool on; uint8_t ch, pitch, vel; };
    void onHeard(const HeardEvent& e, const Transport& tr, int64_t blockStart);
    void onMidi(const MidiIn& m, const Transport& tr, int64_t blockStart);
    void trigger(int64_t t, const Transport& tr, int64_t blockStart, int reason = 0, float info = 0.f);
    void yieldAt(int64_t t);
    void releaseTied(int64_t t);
    bool playerHolding() const;
    void updateBend(int n, MidiSink& out);
    int64_t snapToGrid(int64_t t, const Transport& tr, int64_t blockStart) const;
    void schedule(int64_t t, uint32_t id, int pitch, int vel, bool on, bool tied = false, int64_t onTime = 0, int64_t plannedOff = 0);
    void emit(int64_t blockStart, int n, MidiSink& out);
    float rnd();
    int foldToRange(int p) const;
    static int snapToMask(int p, uint16_t mask, int dir);
    int randomTone(int prev, bool chordTone);
    float rndSigned() { return rnd() * 2.f - 1.f; }

    double sr_ = 44100.0;
    Params p_;
    Analyzer analyzer_;
    EventRing<kMemCapacity> memory_;
    EventRing<8> recent_;
    int64_t lastStartTime_ = -1;
    int64_t now_ = 0;
    int newEvents_ = 0;
    bool clearRequested_ = false;
    bool answering_ = false;
    int64_t answerEnd_ = 0;
    uint32_t rng_ = 0x9E3779B9u;
    uint32_t nextId_ = 1;
    int lastChannel_ = 1;
    Harmony harmony_;
    int harmonyCountdown_ = 0;
    Timeline timeline_;
    Phase phase_ = Phase::Silence;
    float waitProgress_ = 0.f, waitRemaining_ = 0.f;
    uint64_t notesSent_ = 0;
    bool testNoteRequested_ = false;
    int64_t lastAttack_ = -1000000;
    // dynamics: the range of your attacks (audio), to spread answers over the whole velocity range
    float velLo_ = -1.f, velHi_ = -1.f;
    // bends
    float midiBendSemis_ = 0.f;
    float bendSmooth_ = 0.f;
    int lastBendOut_ = 8192;

    // MIDI input
    MidiIn midiQ_[1024];
    int midiQn_ = 0;
    bool held_[128] = {};
    float heldVel_[128] = {};
    int heldCount_ = 0;
    bool echoHeld_[128] = {};
    int64_t lastEmitOn_[128] = {};
    int64_t midiLastActive_ = -1000000;
    int64_t lastMidiStart_ = -1000000;
    int lastMidiEventPitch_ = -1;
    float midiKeyHist_[12] = {}, midiChordC_[12] = {};

    Slot pool_[kPool];
    uint32_t soundId_[128] = {};
    Slot emitBuf_[kPool];
};

} // namespace lpfd
