// LPF Dialogue — listens to audio, remembers, answers in MIDI.
// prepare() allocates; process() is real-time safe.
#pragma once
#include <cstdint>
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

    void process(const float* monoIn, int n, const Transport& tr, MidiSink& out);
    void allNotesOff(int sampleOffset, MidiSink& out);

    // For UI / tests
    int memorySize() const { return memory_.size(); }
    int64_t now() const { return now_; }
    bool hearing() const { return analyzer_.isActive(); }
    bool answering() const { return answering_; }
    int eventsNeeded() const;
    double silenceWaitSeconds() const;
    const Analyzer& analyzer() const { return analyzer_; }

    // Exposed for tests: generate an answer from the current memory.
    int generate(int n, GenNote* out);

private:
    struct Slot {
        int64_t time = 0;
        uint32_t id = 0;
        uint8_t pitch = 0, vel = 0;
        bool on = false, used = false;
    };

    void onHeard(const HeardEvent& e, const Transport& tr, int64_t blockStart);
    void trigger(int64_t t, const Transport& tr, int64_t blockStart);
    void yieldAt(int64_t t);
    int64_t snapToGrid(int64_t t, const Transport& tr, int64_t blockStart) const;
    void schedule(int64_t t, uint32_t id, int pitch, int vel, bool on);
    void emit(int64_t blockStart, int n, MidiSink& out);
    float rnd();
    int foldToRange(int p) const;
    int snapToScale(int p, const float* pcWeight, int dir) const;

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

    Slot pool_[kPool];
    uint32_t soundId_[128] = {};
    Slot emitBuf_[kPool];
};

} // namespace lpfd
