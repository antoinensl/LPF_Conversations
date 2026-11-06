// LPF Dialogue — memory of what the player played (fixed capacity, real-time safe).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lpfd {

struct MemEvent {
    float pitch = 60.f;   // MIDI (detected, or derived from brightness)
    bool pitched = false;
    float velocity = 0.5f;
    float ioi = 0.25f;    // seconds since the previous event's attack
    float dur = 0.f;      // seconds; 0 while the note is still sounding
    int8_t interval = 0;  // semitones from previous event
    int8_t ioiClass = 0;  // log2 rhythmic class (half-octave steps around 1/8 s)
};

inline int8_t ioiClassOf(float ioi) {
    float c = 2.f * std::log2(std::max(ioi, 0.01f) / 0.125f);
    return (int8_t)std::clamp((int)std::lround(c), -6, 8);
}

template <int Capacity>
class EventRing {
public:
    void clear() { count_ = 0; }
    int64_t count() const { return count_; }                // total ever added
    int64_t first() const { return std::max<int64_t>(0, count_ - Capacity); }
    int size() const { return (int)(count_ - first()); }
    bool empty() const { return count_ == 0; }
    const MemEvent& at(int64_t seq) const { return buf_[seq % Capacity]; }
    MemEvent& at(int64_t seq) { return buf_[seq % Capacity]; }
    const MemEvent& last() const { return at(count_ - 1); }
    MemEvent& last() { return at(count_ - 1); }
    void add(const MemEvent& e) { buf_[count_ % Capacity] = e; ++count_; }

private:
    MemEvent buf_[Capacity];
    int64_t count_ = 0;
};

} // namespace lpfd
