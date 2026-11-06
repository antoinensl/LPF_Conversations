#include "engine.h"
#include <algorithm>
#include <climits>
#include <cmath>

namespace lpfd {

void Engine::prepare(double sampleRate) {
    sr_ = sampleRate;
    analyzer_.prepare(sampleRate);
    reset();
}

void Engine::reset() {
    analyzer_.reset();
    memory_.clear();
    recent_.clear();
    lastStartTime_ = -1;
    now_ = 0;
    newEvents_ = 0;
    answering_ = false;
    for (auto& s : pool_) s.used = false;
    std::fill(soundId_, soundId_ + 128, 0u);
    harmony_ = Harmony();
    midiQn_ = 0; heldCount_ = 0;
    std::fill(held_, held_ + 128, false);
    std::fill(echoHeld_, echoHeld_ + 128, false);
    std::fill(lastEmitOn_, lastEmitOn_ + 128, (int64_t)-1000000);
    midiLastActive_ = -1000000; lastMidiStart_ = -1000000; lastMidiEventPitch_ = -1;
    std::fill(midiKeyHist_, midiKeyHist_ + 12, 0.f);
    std::fill(midiChordC_, midiChordC_ + 12, 0.f);
}

void Engine::setParams(const Params& p) {
    p_ = p;
    p_.patience = std::clamp(p_.patience, 0.f, 1.f);
    p_.interruption = std::clamp(p_.interruption, 0.f, 1.f);
    p_.length = std::clamp(p_.length, 0.1f, 4.f);
    p_.memory = std::clamp(p_.memory, 0.f, 1.f);
    p_.fidelity = std::clamp(p_.fidelity, 0.f, 1.f);
    p_.contrast = std::clamp(p_.contrast, 0.f, 1.f);
    p_.scaleLock = std::clamp(p_.scaleLock, 0.f, 1.f);
    p_.rangeLow = std::clamp(p_.rangeLow, 0, 127);
    p_.rangeHigh = std::clamp(p_.rangeHigh, 0, 127);
    p_.channel = std::clamp(p_.channel, 1, 16);
    p_.grid = std::clamp(p_.grid, 0, 3);
    p_.randomness = std::clamp(p_.randomness, 0.f, 1.f);
    p_.humanize = std::clamp(p_.humanize, 0.f, 1.f);
    p_.source = std::clamp(p_.source, 0, 2);
    p_.midiInChannel = std::clamp(p_.midiInChannel, 0, 16);
}

float Engine::rnd() {
    uint32_t x = rng_;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_ = x;
    return (x >> 8) * (1.f / 16777216.f);
}

int Engine::eventsNeeded() const {
    if (p_.patience >= 0.98f) return INT_MAX;
    return 1 + (int)std::lround(p_.patience * p_.patience * 15.f);
}

double Engine::silenceWaitSeconds() const {
    return 0.08 + 1.92 * std::pow((double)p_.patience, 1.5);
}

int Engine::foldToRange(int p) const {
    const int lo = std::min(p_.rangeLow, p_.rangeHigh);
    const int hi = std::max(p_.rangeLow, p_.rangeHigh);
    if (hi - lo < 12) return std::clamp(p, lo, hi);
    while (p > hi) p -= 12;
    while (p < lo) p += 12;
    return p;
}

// ------------------------------------------------------------------ harmony
static const uint16_t kScaleMasks[Harmony::NumScales] = {
    0b101010110101, // major        0 2 4 5 7 9 11
    0b010110101101, // minor        0 2 3 5 7 8 10
    0b011010101101, // dorian       0 2 3 5 7 9 10
    0b011010110101, // mixolydian   0 2 4 5 7 9 10
    0b010110101011, // phrygian     0 1 3 5 7 8 10
    0b101011010101, // lydian       0 2 4 6 7 9 11
    0b100110101101, // harm. minor  0 2 3 5 7 8 11
    0b001010010101, // maj penta    0 2 4 7 9
    0b010010101001, // min penta    0 3 5 7 10
    0b010011101001, // blues        0 3 5 6 7 10
    0b010101010101, // whole tone   0 2 4 6 8 10
};
static const uint16_t kChordMasks[Harmony::NumChords] = {
    (1 << 0) | (1 << 4) | (1 << 7),             // maj
    (1 << 0) | (1 << 3) | (1 << 7),             // min
    (1 << 0) | (1 << 4) | (1 << 7) | (1 << 10), // 7
    (1 << 0) | (1 << 4) | (1 << 7) | (1 << 11), // maj7
    (1 << 0) | (1 << 3) | (1 << 7) | (1 << 10), // m7
    (1 << 0) | (1 << 3) | (1 << 6),             // dim
    (1 << 0) | (1 << 5) | (1 << 7),             // sus4
    (1 << 0) | (1 << 7),                        // 5
};

static uint16_t rotateMask(uint16_t m, int root) {
    uint32_t x = ((uint32_t)m << root) | ((uint32_t)m >> (12 - root));
    return (uint16_t)(x & 0xFFF);
}
static int popcount12(uint16_t m) { int c = 0; for (int i = 0; i < 12; ++i) c += (m >> i) & 1; return c; }

const char* Harmony::scaleName(int s) {
    static const char* n[] = {"majeur", "mineur", "dorien", "mixolydien", "phrygien", "lydien",
                              "mineur harmonique", "penta majeure", "penta mineure", "blues", "par tons"};
    return s >= 0 && s < NumScales ? n[s] : "";
}
const char* Harmony::chordSuffix(int c) {
    static const char* n[] = {"", "m", "7", "maj7", "m7", "dim", "sus4", "5"};
    return c >= 0 && c < NumChords ? n[c] : "";
}
const char* Harmony::pcName(int pc) {
    static const char* n[] = {"Do", "Do#", "R\xc3\xa9", "Mib", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "Sib", "Si"};
    return pc >= 0 && pc < 12 ? n[pc] : "";
}
const char* Harmony::pcLetter(int pc) {
    static const char* n[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    return pc >= 0 && pc < 12 ? n[pc] : "";
}

// remove the faint floor (leakage, stray harmonics) so that only real notes vote
static void floor12(float* v, float f) {
    for (int i = 0; i < 12; ++i) v[i] = std::max(0.f, v[i] - f);
    float s = 0.f;
    for (int i = 0; i < 12; ++i) s += v[i];
    if (s > 1e-9f) for (int i = 0; i < 12; ++i) v[i] /= s;
}

static void normalise12(float* v) {
    float s = 0.f;
    for (int i = 0; i < 12; ++i) s += v[i];
    if (s > 1e-9f) for (int i = 0; i < 12; ++i) v[i] /= s;
}

void Engine::updateHarmony() {
    // --- key: long-term audio chroma + notes in memory
    float audioK[12], notesK[12] = {}, keyV[12] = {};
    const float wA = analyzer_.chroma(true, audioK);
    float wN = 0.f;
    const int64_t cnt = memory_.count();
    const int64_t window = (int64_t)std::lround(8.0 * std::pow(2.0, p_.memory * 9.0));
    for (int64_t s = std::max(memory_.first(), cnt - std::max<int64_t>(window, 32)); s < cnt; ++s) {
        const MemEvent& e = memory_.at(s);
        if (!e.pitched) continue;
        const int pc = (((int)std::lround(e.pitch)) % 12 + 12) % 12;
        const float w = e.velocity * (e.dur > 0.f ? std::min(e.dur, 1.f) : 0.25f) + 0.01f;
        notesK[pc] += w; wN += 1.f;
    }
    normalise12(notesK);
    const bool useAudio = p_.source != 1, useMidi = p_.source != 0;
    float midiK[12];
    float wM = 0.f;
    for (int i = 0; i < 12; ++i) { midiK[i] = midiKeyHist_[i]; wM += midiK[i]; }
    normalise12(midiK);
    const bool haveA = useAudio && wA > 20.f, haveN = wN >= 1.f, haveM = useMidi && wM > 1.f;
    {
        const int k = (int)haveA + (int)haveN + (int)haveM;
        for (int i = 0; i < 12; ++i)
            keyV[i] = k ? ((haveA ? audioK[i] : 0.f) + (haveN ? notesK[i] : 0.f) + (haveM ? midiK[i] : 0.f)) / k : 0.f;
    }
    if (haveM) wN += wM;   // MIDI notes are certain: they count as evidence
    floor12(keyV, 0.03f);
    harmony_.keyRoot = -1; harmony_.scaleMask = 0; harmony_.keyConfidence = 0.f;
    if ((haveA || haveN || haveM) && !(wN >= 6.f || wA > 100.f)) {
        // not enough heard yet to name a scale: stay on the notes actually played
        for (int i = 0; i < 12; ++i) if (keyV[i] > 0.05f) harmony_.scaleMask |= (uint16_t)(1 << i);
    } else if (haveA || haveN || haveM) {
        float best = -1e9f;
        for (int sc = 0; sc < Harmony::NumScales; ++sc)
            for (int root = 0; root < 12; ++root) {
                const uint16_t m = rotateMask(kScaleMasks[sc], root);
                float in = 0.f;
                for (int i = 0; i < 12; ++i) if (m >> i & 1) in += keyV[i];
                const float score = in - 1.2f * (1.f - in) - 0.02f * popcount12(m) +
                                    0.25f * keyV[root] + 0.08f * keyV[(root + 7) % 12];
                if (score > best) {
                    best = score;
                    harmony_.keyRoot = root; harmony_.scale = sc; harmony_.scaleMask = m;
                    harmony_.keyConfidence = std::clamp(2.f * in - 1.f, 0.f, 1.f);
                }
            }
    }

    // --- chord: short-term audio chroma + the last notes, weighted by how recent they are
    float audioC[12], notesC[12] = {}, chordV[12] = {};
    const float wC = analyzer_.chroma(false, audioC);
    float wR = 0.f;
    for (int64_t s = recent_.first(); s < recent_.count(); ++s) {
        const MemEvent& e = recent_.at(s);
        if (!e.pitched) continue;
        const float age = (float)((now_ - e.time) / sr_);
        const float w = e.velocity * std::exp(-std::max(0.f, age) / 1.5f) *
                        (e.dur > 0.f ? std::min(e.dur, 1.f) : 0.3f);
        notesC[(((int)std::lround(e.pitch)) % 12 + 12) % 12] += w;
        wR += w;
    }
    normalise12(notesC);
    const bool haveC = useAudio && wC > 5.f, haveR = wR > 0.05f;
    float midiC[12], heldC[12] = {};
    float wMC = 0.f, wH = 0.f;
    for (int i = 0; i < 12; ++i) { midiC[i] = midiChordC_[i]; wMC += midiC[i]; }
    normalise12(midiC);
    if (useMidi)
        for (int p = 0; p < 128; ++p)
            if (held_[p]) { heldC[p % 12] += 0.3f + heldVel_[p]; wH += 1.f; }
    normalise12(heldC);
    const bool haveMC = useMidi && wMC > 0.2f, haveH = wH > 0.f;
    {
        const int k = (int)haveC + (int)haveR + (int)haveMC;
        for (int i = 0; i < 12; ++i) {
            const float rest = k ? ((haveC ? audioC[i] : 0.f) + (haveR ? notesC[i] : 0.f) + (haveMC ? midiC[i] : 0.f)) / k : 0.f;
            // notes held on the keyboard are the chord, the rest only colours it
            chordV[i] = haveH ? 0.75f * heldC[i] + 0.25f * rest : rest;
        }
    }
    floor12(chordV, 0.04f);
    harmony_.chordRoot = -1; harmony_.chordMask = 0; harmony_.chordConfidence = 0.f;
    if (haveC || haveR || haveMC || haveH) {
        float best = -1e9f;
        for (int ch = 0; ch < Harmony::NumChords; ++ch)
            for (int root = 0; root < 12; ++root) {
                const uint16_t m = rotateMask(kChordMasks[ch], root);
                float in = 0.f;
                for (int i = 0; i < 12; ++i) if (m >> i & 1) in += chordV[i];
                const float score = in - (1.f - in) - 0.04f * popcount12(m) + 0.3f * chordV[root];
                if (score > best) {
                    best = score;
                    harmony_.chordRoot = root; harmony_.chord = ch; harmony_.chordMask = m;
                    harmony_.chordConfidence = std::clamp(2.f * in - 1.f, 0.f, 1.f);
                }
            }
    }
}

int Engine::snapToMask(int p, uint16_t mask, int dir) {
    if (!mask) return p;
    const int s = dir >= 0 ? 1 : -1;
    for (int d = 0; d <= 6; ++d) {
        const int a = p + d * s, b = p - d * s;
        if (mask >> (((a % 12) + 12) % 12) & 1) return a;
        if (mask >> (((b % 12) + 12) % 12) & 1) return b;
    }
    return p;
}

// An invented note near `prev`: a chord tone (leap) or a scale step.
int Engine::randomTone(int prev, bool chordTone) {
    const uint16_t mask = chordTone && harmony_.chordMask ? harmony_.chordMask : harmony_.scaleMask;
    if (!mask) return prev;
    int cand[16];
    int nc = 0;
    const int span = chordTone ? 9 : 4;
    for (int d = -span; d <= span && nc < 16; ++d) {
        if (d == 0 && rnd() < 0.7f) continue;     // prefer moving
        const int q = prev + d;
        if (mask >> (((q % 12) + 12) % 12) & 1) cand[nc++] = q;
    }
    if (!nc) return snapToMask(prev, mask, 1);
    return cand[std::min(nc - 1, (int)(rnd() * nc))];
}

int Engine::generate(int n, GenNote* out, int cap) {
    if (memory_.empty()) return 0;
    cap = std::clamp(cap, 1, kMaxAnswer);
    n = std::clamp(n, 1, cap);
    updateHarmony();
    const int64_t cnt = memory_.count();
    const int64_t window = (int64_t)std::lround(8.0 * std::pow(2.0, p_.memory * 9.0));
    const int64_t base = std::max(memory_.first(), cnt - window);
    const uint16_t scaleMask = harmony_.scaleMask;
    const float r = p_.randomness, h = p_.humanize;

    // context = what was just played
    int8_t ctxI[16], ctxR[16];
    int ctxLen = 0;
    float meanPitch = 0.f, meanIoi = 0.f;
    int nIoi = 0;
    for (int64_t s = recent_.first(); s < recent_.count(); ++s) {
        const MemEvent& e = recent_.at(s);
        ctxI[ctxLen] = e.interval; ctxR[ctxLen] = e.ioiClass; ++ctxLen;
        meanPitch += e.pitch;
        if (s > recent_.first() && e.ioi < 3.f) { meanIoi += e.ioi; ++nIoi; }
    }
    int prev;
    if (ctxLen > 0) {
        meanPitch /= ctxLen;
        prev = (int)std::lround(recent_.last().pitch);
    } else {
        meanPitch = memory_.last().pitch;
        prev = (int)std::lround(meanPitch);
    }
    meanIoi = nIoi ? meanIoi / nIoi : 0.3f;

    const float c = p_.contrast;
    const float center = 0.5f * (p_.rangeLow + p_.rangeHigh);
    if (rnd() < c * 0.8f) prev += meanPitch < center ? 12 : -12;
    prev = foldToRange(prev);
    const float target = meanIoi < 0.2f ? 2.f : (meanIoi > 0.6f ? 0.5f : 1.f);
    const float factor = 1.f + (target - 1.f) * c;

    const int maxOrder = (int)std::lround(p_.fidelity * 4.f);
    float t = 0.f;
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) {
        int ord = maxOrder;
        if (rnd() < (1.f - p_.fidelity) * 0.25f) ord = 0;
        int64_t pick = -1;
        for (int order = std::min(ord, ctxLen); order >= 1 && pick < 0; --order) {
            int matches = 0;
            for (int64_t s = base + order; s < cnt; ++s) {
                bool ok = true;
                for (int j = 1; j <= order && ok; ++j) {
                    const MemEvent& e = memory_.at(s - j);
                    ok = e.interval == ctxI[ctxLen - j] &&
                         std::abs(e.ioiClass - ctxR[ctxLen - j]) <= 1;
                }
                if (ok) {
                    ++matches;
                    if (rnd() * matches < 1.f) pick = s;
                }
            }
        }
        if (pick < 0) pick = base + std::min<int64_t>(cnt - base - 1, (int64_t)(rnd() * (cnt - base)));
        const MemEvent& e = memory_.at(pick);

        int interval = e.interval;
        if (rnd() < c * 0.6f) interval = -interval;

        // rhythm (with occasional random displacement)
        if (i > 0) {
            float ioi = e.ioi * factor;
            if (rnd() < r * 0.25f) {
                const float m[] = {0.5f, 1.5f, 2.f};
                ioi *= m[std::min(2, (int)(rnd() * 3.f))];
            }
            t += std::clamp(ioi, 0.03f, 2.f);
        }

        // pitch: learnt, then maybe invented, then maybe pulled into the scale
        int pitch = foldToRange(prev + interval);
        const bool edge = i == 0 || i == n - 1;
        if (scaleMask && rnd() < r * 0.5f)
            pitch = foldToRange(randomTone(prev, edge || rnd() < 0.4f));
        if (scaleMask && rnd() < p_.scaleLock)
            pitch = foldToRange(snapToMask(pitch, scaleMask, interval));
        if (edge && harmony_.chordMask && rnd() < r * 0.6f)       // land on a chord tone
            pitch = foldToRange(snapToMask(pitch, harmony_.chordMask, interval));

        // a breath: skip a note now and then (keeps its time slot)
        const bool rest = i > 0 && i < n - 1 && rnd() < r * 0.12f;

        float dur = e.dur > 0.f ? e.dur : std::min(e.ioi, 0.3f);
        dur *= factor;
        dur *= 1.f + h * 0.3f * rndSigned();
        if (i == n - 1) dur *= 1.f + 0.8f * h;                     // let the last note ring
        dur = std::clamp(dur, 0.03f, 4.f);

        float vel = e.velocity;
        vel = vel + ((1.05f - vel) - vel) * c * 0.7f;
        const float arc = std::sin(3.14159265f * (i + 0.5f) / n);  // phrase shape
        vel *= 1.f + h * 0.3f * (arc - 0.5f);
        vel += h * 0.12f * rndSigned();
        const int midiVel = std::clamp((int)std::lround(1.f + std::clamp(vel, 0.f, 1.f) * 126.f), 1, 127);

        float when = t;
        if (i > 0) when = std::max(0.f, t + h * 0.025f * 0.5f * (rndSigned() + rndSigned()));

        if (!rest) {
            // ornament: a quick neighbour note just before
            if (scaleMask && when > 0.08f && k + 1 < cap && rnd() < r * 0.12f) {
                const int dir = rnd() < 0.5f ? 1 : -1;
                int g = snapToMask(pitch + dir, scaleMask, dir);
                if (g == pitch) g = snapToMask(pitch + 2 * dir, scaleMask, dir);
                if (g != pitch) {
                    out[k].time = when - 0.06f;
                    out[k].dur = 0.05f;
                    out[k].pitch = std::clamp(foldToRange(g), 0, 127);
                    out[k].velocity = std::max(1, midiVel * 3 / 4);
                    ++k;
                }
            }
            out[k].time = when;
            out[k].dur = dur;
            out[k].pitch = std::clamp(pitch, 0, 127);
            out[k].velocity = midiVel;
            ++k;
            prev = pitch;
        }

        if (ctxLen == 16) {
            for (int q = 1; q < 16; ++q) { ctxI[q - 1] = ctxI[q]; ctxR[q - 1] = ctxR[q]; }
            --ctxLen;
        }
        ctxI[ctxLen] = (int8_t)std::clamp(interval, -24, 24);
        ctxR[ctxLen] = e.ioiClass;
        ++ctxLen;
    }
    return k;
}

int64_t Engine::snapToGrid(int64_t t, const Transport& tr, int64_t blockStart) const {
    if (p_.grid == 0 || !tr.valid || !tr.playing || tr.bpm <= 0.0) return t;
    const double spb = sr_ * 60.0 / tr.bpm;
    const double step = p_.grid == 1 ? 1.0 : (p_.grid == 2 ? 0.5 : 0.25);
    const double ppq = tr.ppqAtBlockStart + (double)(t - blockStart) / spb;
    const double next = std::ceil(ppq / step - 1e-6) * step;
    return blockStart + (int64_t)std::llround((next - tr.ppqAtBlockStart) * spb);
}

void Engine::schedule(int64_t t, uint32_t id, int pitch, int vel, bool on) {
    for (auto& s : pool_) {
        if (!s.used) {
            s.used = true; s.time = t; s.id = id;
            s.pitch = (uint8_t)pitch; s.vel = (uint8_t)vel; s.on = on;
            return;
        }
    }
}

void Engine::trigger(int64_t t, const Transport& tr, int64_t blockStart) {
    float len = newEvents_ * p_.length;
    len *= 1.f + 0.5f * p_.randomness * rndSigned();
    const int n = std::clamp((int)std::lround(len), 1, kMaxAnswer);
    newEvents_ = 0;
    if (!p_.respond) return;
    // a human does not answer after exactly the same delay every time
    t += (int64_t)(p_.humanize * rnd() * (0.05 + 0.25 * p_.patience) * sr_);
    GenNote notes[kMaxAnswer];
    const int k = generate(n, notes, kMaxAnswer);
    int freeSlots = 0;
    for (auto& s : pool_) if (!s.used) ++freeSlots;
    for (int i = 0; i < k && freeSlots >= 2; ++i, freeSlots -= 2) {
        uint32_t id = nextId_++;
        if (id == 0) id = nextId_++;
        int64_t on = snapToGrid(t + (int64_t)(notes[i].time * sr_), tr, blockStart);
        int64_t off = on + std::max<int64_t>(1, (int64_t)(notes[i].dur * sr_));
        schedule(on, id, notes[i].pitch, notes[i].velocity, true);
        schedule(off, id, notes[i].pitch, 0, false);
        answerEnd_ = std::max(answerEnd_, off);
    }
    if (k > 0) answering_ = true;
}

void Engine::yieldAt(int64_t t) {
    for (auto& s : pool_) {
        if (!s.used) continue;
        if (s.on && s.time >= t) s.used = false;
        else if (!s.on && s.time > t) s.time = t;
    }
}

void Engine::onHeard(const HeardEvent& e, const Transport& tr, int64_t blockStart) {
    if (e.type == HeardEvent::End) {
        auto close = [&](auto& ring) {
            if (!ring.empty() && ring.last().dur <= 0.f)
                ring.last().dur = std::max(0.01f, (float)((e.time - lastStartTime_) / sr_));
        };
        if (lastStartTime_ >= 0) {
            close(recent_);
            if (!p_.freeze) close(memory_);
        }
        return;
    }

    MemEvent m;
    m.pitch = e.pitch;
    m.pitched = e.pitched;
    m.velocity = e.velocity;
    m.ioi = lastStartTime_ < 0 ? 0.25f
                               : std::min(4.f, (float)((e.time - lastStartTime_) / sr_));
    m.ioiClass = ioiClassOf(m.ioi);
    m.time = e.time;
    m.interval = recent_.empty()
                     ? 0
                     : (int8_t)std::clamp((int)std::lround(e.pitch - recent_.last().pitch), -24, 24);
    auto legato = [&](auto& ring) {
        if (!ring.empty() && ring.last().dur <= 0.f) ring.last().dur = std::max(0.01f, m.ioi);
    };
    legato(recent_);
    recent_.add(m);
    if (!p_.freeze) { legato(memory_); memory_.add(m); }
    lastStartTime_ = e.time;
    ++newEvents_;

    if (answering_ && p_.interruption < 0.5f) yieldAt(e.commitTime);

    if (newEvents_ >= eventsNeeded() &&
        (p_.interruption >= 1.f || rnd() < p_.interruption)) {
        // with randomness, it sometimes lets a chance to answer go by (and says more next time)
        if (!(p_.randomness > 0.f && rnd() < p_.randomness * 0.2f))
            trigger(std::max(e.commitTime, blockStart), tr, blockStart);
    }
}

void Engine::addMidiInput(int offset, bool on, int ch, int pitch, int vel) {
    if (midiQn_ >= 1024 || pitch < 0 || pitch > 127) return;
    if (on && vel <= 0) on = false;
    midiQ_[midiQn_++] = {std::max(0, offset), on, (uint8_t)std::clamp(ch, 1, 16), (uint8_t)pitch,
                         (uint8_t)std::clamp(vel, 0, 127)};
}

void Engine::onMidi(const MidiIn& m, const Transport& tr, int64_t blockStart) {
    if (p_.midiInChannel != 0 && m.ch != p_.midiInChannel) return;
    const int64_t t = blockStart + m.offset;
    const int p = m.pitch;
    if (!m.on) {
        if (echoHeld_[p]) { echoHeld_[p] = false; return; }
        if (!held_[p]) return;
        held_[p] = false;
        if (--heldCount_ <= 0) { heldCount_ = 0; midiLastActive_ = t; }
        if (p == lastMidiEventPitch_) {
            HeardEvent e;
            e.type = HeardEvent::End; e.time = t; e.commitTime = t; e.pitch = (float)p; e.pitched = true;
            onHeard(e, tr, blockStart);
            lastMidiEventPitch_ = -1;
        }
        return;
    }
    // our own answer coming back (MIDI loop): ignore it
    if (m.ch == p_.channel && (soundId_[p] != 0 || t - lastEmitOn_[p] < (int64_t)(0.05 * sr_))) {
        echoHeld_[p] = true;
        return;
    }
    if (held_[p]) return;                       // repeated note-on without note-off
    held_[p] = true;
    heldVel_[p] = m.vel / 127.f;
    ++heldCount_;
    midiLastActive_ = t;
    const float w = 0.3f + m.vel / 127.f;
    midiKeyHist_[p % 12] += w;
    midiChordC_[p % 12] += w;

    // notes struck together (< 30 ms) are one event: the top note carries the melody
    if (t - lastMidiStart_ < (int64_t)(0.03 * sr_) && lastMidiEventPitch_ >= 0 && !recent_.empty()) {
        if (p > lastMidiEventPitch_) {
            auto raise = [&](auto& ring) {
                if (ring.empty() || ring.last().time != lastMidiStart_) return;
                MemEvent& last = ring.last();
                const float prevPitch = ring.size() >= 2 ? ring.at(ring.count() - 2).pitch : (float)p;
                last.pitch = (float)p;
                last.interval = ring.size() >= 2 ? (int8_t)std::clamp((int)std::lround(p - prevPitch), -24, 24) : 0;
                last.velocity = std::max(last.velocity, m.vel / 127.f);
            };
            raise(recent_);
            if (!p_.freeze) raise(memory_);
            lastMidiEventPitch_ = p;
        }
        return;
    }
    lastMidiStart_ = t;
    lastMidiEventPitch_ = p;
    HeardEvent e;
    e.type = HeardEvent::Start;
    e.time = t; e.commitTime = t;
    e.pitch = (float)p; e.pitched = true;
    e.velocity = m.vel / 127.f;
    onHeard(e, tr, blockStart);
}

void Engine::emit(int64_t blockStart, int n, MidiSink& out) {
    const int64_t end = blockStart + n;
    int k = 0;
    for (auto& s : pool_) {
        if (s.used && s.time < end) { emitBuf_[k++] = s; s.used = false; }
    }
    // sort by time, note-offs first at equal time
    for (int i = 1; i < k; ++i) {
        Slot x = emitBuf_[i];
        int j = i - 1;
        while (j >= 0 && (emitBuf_[j].time > x.time ||
                          (emitBuf_[j].time == x.time && emitBuf_[j].on && !x.on))) {
            emitBuf_[j + 1] = emitBuf_[j];
            --j;
        }
        emitBuf_[j + 1] = x;
    }
    const int ch = p_.channel;
    for (int i = 0; i < k; ++i) {
        const Slot& s = emitBuf_[i];
        const int off = (int)std::clamp<int64_t>(s.time - blockStart, 0, n - 1);
        if (s.on) {
            if (soundId_[s.pitch] != 0) out.noteOff(off, ch, s.pitch);
            out.noteOn(off, ch, s.pitch, s.vel);
            soundId_[s.pitch] = s.id;
            lastEmitOn_[s.pitch] = blockStart + off;
        } else if (soundId_[s.pitch] == s.id) {
            out.noteOff(off, ch, s.pitch);
            soundId_[s.pitch] = 0;
        }
    }
    bool any = false;
    for (auto& s : pool_) if (s.used) { any = true; break; }
    if (!any) for (uint32_t id : soundId_) if (id) { any = true; break; }
    answering_ = any;
}

void Engine::allNotesOff(int sampleOffset, MidiSink& out) {
    for (int p = 0; p < 128; ++p)
        if (soundId_[p]) { out.noteOff(sampleOffset, lastChannel_, p); soundId_[p] = 0; }
    for (auto& s : pool_) s.used = false;
    answering_ = false;
}

void Engine::process(const float* in, int n, const Transport& tr, MidiSink& out) {
    if (n <= 0) return;
    if (p_.channel != lastChannel_) {
        allNotesOff(0, out);
        lastChannel_ = p_.channel;
    }
    if (clearRequested_) {
        clearRequested_ = false;
        memory_.clear(); recent_.clear();
        lastStartTime_ = -1; newEvents_ = 0;
        std::fill(midiKeyHist_, midiKeyHist_ + 12, 0.f);
        std::fill(midiChordC_, midiChordC_ + 12, 0.f);
        lastMidiEventPitch_ = -1;
    }
    AnalyzerSettings as;
    as.sensitivity = p_.sensitivity;
    as.gateDb = p_.gateDb;
    as.bright = p_.bright;
    analyzer_.setSettings(as);

    const int64_t blockStart = now_;
    const bool useAudio = p_.source != 1, useMidi = p_.source != 0;
    analyzer_.process(in, n, blockStart);

    // MIDI in time order (stable), then merged with what the ears heard
    for (int i = 1; i < midiQn_; ++i) {
        MidiIn x = midiQ_[i];
        int j = i - 1;
        while (j >= 0 && midiQ_[j].offset > x.offset) { midiQ_[j + 1] = midiQ_[j]; --j; }
        midiQ_[j + 1] = x;
    }
    {
        const int na = useAudio ? analyzer_.numEvents() : 0;
        const int nm = useMidi ? midiQn_ : 0;
        int ia = 0, im = 0;
        while (ia < na || im < nm) {
            const bool takeMidi = im < nm && (ia >= na || blockStart + std::min(midiQ_[im].offset, n - 1) <=
                                                              analyzer_.event(ia).commitTime);
            if (takeMidi) {
                MidiIn m = midiQ_[im++];
                m.offset = std::min(m.offset, n - 1);
                onMidi(m, tr, blockStart);
            } else {
                onHeard(analyzer_.event(ia++), tr, blockStart);
            }
        }
    }
    midiQn_ = 0;
    analyzer_.clearEvents();
    if (heldCount_ > 0) midiLastActive_ = blockStart + n;

    const bool active = (useAudio && analyzer_.isActive()) || (useMidi && heldCount_ > 0);
    if (newEvents_ > 0 && !active) {
        int64_t last = -1000000;
        if (useAudio) last = std::max(last, analyzer_.lastActiveTime());
        if (useMidi) last = std::max(last, midiLastActive_);
        const int64_t tSil = last + (int64_t)(silenceWaitSeconds() * sr_);
        if (tSil < blockStart + n) trigger(std::max(tSil, blockStart), tr, blockStart);
    }
    {
        const float kk = std::exp(-(float)n / (float)(20.0 * sr_));
        const float kc = std::exp(-(float)n / (float)(1.0 * sr_));
        for (int i = 0; i < 12; ++i) { midiKeyHist_[i] *= kk; midiChordC_[i] *= kc; }
    }

    emit(blockStart, n, out);
    now_ += n;
    harmonyCountdown_ -= n;
    if (harmonyCountdown_ <= 0) {          // ~20 times a second, for the display
        harmonyCountdown_ = (int)(sr_ * 0.05);
        updateHarmony();
    }
}

} // namespace lpfd
