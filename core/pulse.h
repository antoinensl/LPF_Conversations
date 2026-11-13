// Conversations — the pulse of your playing (not the host tempo).
// From the attacks you play it estimates a beat period, where the beats fall, how the beat is
// divided (2, 3 or 4) and your swing. It follows you when you speed up or slow down.
// Real-time safe: fixed arrays, no allocation.
#pragma once
#include <cstdint>

namespace lpfd {

class PulseTracker {
public:
    void prepare(double sampleRate) { sr_ = sampleRate; reset(); }
    void reset();
    void addOnset(int64_t t, float velocity);
    // Re-estimates if new attacks came in (cheap otherwise). Call once per block.
    void update(int64_t now);

    bool valid() const { return conf_ >= 0.6f && period_ > 0.0; }
    float confidence() const { return conf_; }
    double period() const { return period_; }          // samples per beat
    double bpm() const { return period_ > 0.0 ? 60.0 * sr_ / period_ : 0.0; }
    int division() const { return div_; }              // slots per beat
    double slot() const { return period_ / div_; }      // samples per slot
    float swing() const { return swing_; }              // delay of odd slots, in slots (-0.4..0.4)
    // first beat at or after t
    int64_t nextBeat(int64_t t) const;
    // index of the slot nearest to t, counted from beat `origin`, and the time of a slot
    int64_t slotIndex(int64_t origin, int64_t t) const;
    int64_t slotTime(int64_t origin, int64_t index) const;  // includes swing
    int64_t lastOnset() const { return n_ ? t_[(head_ + kMax - 1) % kMax] : -1; }

private:
    static constexpr int kMax = 40;
    static constexpr int kCand = 90;
    double sr_ = 44100.0;
    int64_t t_[kMax] = {};
    float v_[kMax] = {};
    int head_ = 0, n_ = 0;
    bool dirty_ = false;
    int64_t lastUpdate_ = -1000000000;
    double period_ = 0.0, phase_ = 0.0;   // phase_: sample time of a beat
    int div_ = 2;
    float conf_ = 0.f, swing_ = 0.f;
    double pending_ = 0.0;                // a different tempo must win twice before it is taken
};

} // namespace lpfd
