// LPF Dialogue — tests on synthetic signals.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <algorithm>
#include <random>
#include <string>
#include <vector>
#include "../core/engine.h"

using namespace lpfd;

// ---------- allocation counter (to prove process() never allocates) ----------
static std::atomic<bool> g_countAllocs{false};
static std::atomic<long> g_allocs{0};
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
void* operator new(std::size_t sz) {
    if (g_countAllocs) ++g_allocs;
    if (void* p = std::malloc(sz ? sz : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#define LPFD_ALLOC_CHECK 1
#endif

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (cond) ++g_pass;                                                   \
        else { ++g_fail; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);   \
               std::printf(__VA_ARGS__); std::printf("\n"); }                 \
    } while (0)

static const double SR = 44100.0;

// ---------- signal helpers ----------
struct Note { double start, dur, hz, amp; };

static double midiToHz(double m) { return 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }

// harmonic tone, 5 ms attack, exponential-ish release 20 ms at the end
static void addTone(std::vector<float>& buf, const Note& n, bool pluck = false) {
    const int s0 = (int)(n.start * SR), len = (int)(n.dur * SR);
    const int rel = (int)(0.02 * SR), att = (int)(0.005 * SR);
    double ph = 0.0;
    for (int i = 0; i < len + rel && s0 + i < (int)buf.size(); ++i) {
        double env = i < att ? (double)i / att : 1.0;
        if (pluck) env *= std::exp(-3.0 * i / SR);
        if (i >= len) env *= 1.0 - (double)(i - len) / rel;
        double v = 0.0;
        for (int k = 1; k <= 6; ++k) v += std::sin(k * ph) / k;
        buf[s0 + i] += (float)(n.amp * env * v * 0.6);
        ph += 2.0 * M_PI * n.hz / SR;
    }
}

static void addNoise(std::vector<float>& buf, double start, double dur, double amp,
                     bool bright, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> nd(0.f, 1.f);
    const int s0 = (int)(start * SR), len = (int)(dur * SR);
    float lp = 0.f, lp2 = 0.f, prev = 0.f;
    for (int i = 0; i < len && s0 + i < (int)buf.size(); ++i) {
        float w = nd(rng);
        float v;
        if (bright) { v = w - prev; prev = w; }          // high-pass-ish
        else { lp += 0.03f * (w - lp); lp2 += 0.03f * (lp - lp2); v = lp2 * 10.f; } // low-pass
        float env = std::min(1.f, i / (float)(0.003 * SR));
        buf[s0 + i] += (float)(amp * env * v);
    }
}

static std::vector<HeardEvent> analyse(const std::vector<float>& sig, AnalyzerSettings st = {}) {
    Analyzer a;
    a.prepare(SR);
    a.setSettings(st);
    std::vector<HeardEvent> out;
    const int B = 256;
    for (int64_t pos = 0; pos < (int64_t)sig.size(); pos += B) {
        int n = (int)std::min<int64_t>(B, sig.size() - pos);
        a.process(sig.data() + pos, n, pos);
        for (int i = 0; i < a.numEvents(); ++i) out.push_back(a.event(i));
        a.clearEvents();
    }
    return out;
}

static std::vector<HeardEvent> starts(const std::vector<HeardEvent>& ev) {
    std::vector<HeardEvent> s;
    for (auto& e : ev) if (e.type == HeardEvent::Start) s.push_back(e);
    return s;
}

// ---------- MIDI capture ----------
struct Midi { int64_t t; bool on; int ch, pitch, vel; };
struct Capture : MidiSink {
    std::vector<Midi> ev;
    int64_t base = 0;
    Capture() { ev.reserve(100000); }
    void noteOn(int o, int ch, int p, int v) override { ev.push_back({base + o, true, ch, p, v}); }
    void noteOff(int o, int ch, int p) override { ev.push_back({base + o, false, ch, p, 0}); }
};

static Capture runEngine(Engine& e, std::vector<float> sig, int B = 256,
                         Transport tr = {}, bool countAllocs = false) {
    Capture cap;
    sig.resize(sig.size() + (size_t)(SR * 10.0), 0.f); // let every answer finish
    for (int64_t pos = 0; pos < (int64_t)sig.size(); pos += B) {
        int n = (int)std::min<int64_t>(B, sig.size() - pos);
        cap.base = pos;
        if (tr.valid) tr.ppqAtBlockStart = pos / (SR * 60.0 / tr.bpm);
        if (countAllocs) g_countAllocs = true;
        e.process(sig.data() + pos, n, tr, cap);
        g_countAllocs = false;
    }
    return cap;
}

static std::vector<Midi> ons(const Capture& c) {
    std::vector<Midi> r;
    for (auto& m : c.ev) if (m.on) r.push_back(m);
    return r;
}

static bool balanced(const Capture& c, std::string& why) {
    int sounding[16][128] = {};
    int64_t lastT = -1;
    for (auto& m : c.ev) {
        if (m.t < lastT) { why = "events out of order"; return false; }
        lastT = m.t;
        if (m.on) {
            if (sounding[m.ch - 1][m.pitch]) { why = "note-on while already sounding"; return false; }
            sounding[m.ch - 1][m.pitch] = 1;
        } else {
            sounding[m.ch - 1][m.pitch] = 0;
        }
    }
    for (int c2 = 0; c2 < 16; ++c2)
        for (int p = 0; p < 128; ++p)
            if (sounding[c2][p]) { why = "stuck note " + std::to_string(p); return false; }
    return true;
}

// a simple melody
static std::vector<Note> melody(double t0, int count, double step, double dur,
                                const std::vector<int>& pitches, double amp = 0.3) {
    std::vector<Note> v;
    for (int i = 0; i < count; ++i)
        v.push_back({t0 + i * step, dur, midiToHz(pitches[i % pitches.size()]), amp});
    return v;
}

// =====================================================================
static void testPitchAccuracy() {
    std::printf("[pitch accuracy & latency]\n");
    const double freqs[] = {82.41, 110.0, 196.0, 329.63, 440.0, 880.0, 1318.5};
    double worstLat = 0.0;
    for (double hz : freqs) {
        std::vector<float> sig((size_t)(SR * 0.8), 0.f);
        addTone(sig, {0.2, 0.35, hz, 0.3});
        auto s = starts(analyse(sig));
        const double truth = 69.0 + 12.0 * std::log2(hz / 440.0);
        CHECK(s.size() == 1, "%.1f Hz: %zu starts", hz, s.size());
        if (s.empty()) continue;
        CHECK(s[0].pitched, "%.1f Hz not pitched", hz);
        CHECK(std::fabs(s[0].pitch - truth) < 0.3, "%.1f Hz: got %.2f want %.2f", hz, s[0].pitch, truth);
        const double onErr = (s[0].time / SR - 0.2) * 1000.0;
        const double lat = (s[0].commitTime / SR - 0.2) * 1000.0;
        CHECK(std::fabs(onErr) < 12.0, "%.1f Hz: onset error %.1f ms", hz, onErr);
        worstLat = std::max(worstLat, lat);
        std::printf("  %7.1f Hz -> %.2f (truth %.2f) onset %+.1f ms, sure after %.1f ms\n", hz,
                    s[0].pitch, truth, onErr, lat);
    }
    CHECK(worstLat < 60.0, "worst commit latency %.1f ms", worstLat);
}

static void testMelodyAndRepeats() {
    std::printf("[melody, repeated plucks, legato]\n");
    {
        std::vector<int> pitches = {60, 62, 64, 67, 69, 67, 64, 62};
        std::vector<float> sig((size_t)(SR * 2.5), 0.f);
        for (auto& n : melody(0.1, 8, 0.25, 0.2, pitches)) addTone(sig, n);
        auto s = starts(analyse(sig));
        CHECK(s.size() == 8, "melody: %zu starts", s.size());
        int good = 0;
        for (size_t i = 0; i < s.size() && i < 8; ++i)
            if (std::fabs(s[i].pitch - pitches[i]) < 0.4) ++good;
        CHECK(good == 8, "melody: %d/8 pitches right", good);
    }
    {
        // same note plucked 6 times, no silence between
        std::vector<float> sig((size_t)(SR * 2.0), 0.f);
        for (int i = 0; i < 6; ++i) addTone(sig, {0.1 + i * 0.25, 0.25, 196.0, 0.3}, true);
        auto s = starts(analyse(sig));
        CHECK(s.size() == 6, "repeated plucks: %zu starts", s.size());
    }
    {
        // legato: continuous phase, pitch jumps without amplitude dip
        std::vector<float> sig((size_t)(SR * 1.2), 0.f);
        double ph = 0.0;
        for (int i = (int)(0.1 * SR); i < (int)(1.0 * SR); ++i) {
            double hz = i < (int)(0.55 * SR) ? 220.0 : 293.66;
            double env = std::min(1.0, (i - 0.1 * SR) / (0.005 * SR));
            double v = 0;
            for (int k = 1; k <= 6; ++k) v += std::sin(k * ph) / k;
            sig[i] = (float)(0.2 * env * v);
            ph += 2.0 * M_PI * hz / SR;
        }
        auto s = starts(analyse(sig));
        CHECK(s.size() == 2, "legato: %zu starts", s.size());
        if (s.size() == 2) CHECK(std::fabs(s[1].pitch - 62.0) < 0.4, "legato 2nd pitch %.2f", s[1].pitch);
    }
}

static void testExpressive() {
    std::printf("[fast re-plucks, vibrato]\n");
    {
        std::vector<float> sig((size_t)(SR * 2.0), 0.f);
        for (int i = 0; i < 8; ++i) addTone(sig, {0.1 + i * 0.15, 0.15, 146.83, 0.3}, true);
        auto s = starts(analyse(sig));
        CHECK(s.size() == 8, "re-plucks every 150 ms: %zu starts", s.size());
    }
    {
        std::vector<float> sig((size_t)(SR * 2.0), 0.f);
        double ph = 0.0;
        for (int i = (int)(0.1 * SR); i < (int)(1.6 * SR); ++i) {
            double t = i / SR;
            double hz = 261.63 * std::pow(2.0, 0.4 / 12.0 * std::sin(2 * M_PI * 5.5 * t));
            double env = std::min(1.0, (i - 0.1 * SR) / (0.005 * SR));
            double v = 0;
            for (int k = 1; k <= 6; ++k) v += std::sin(k * ph) / k;
            sig[i] = (float)(0.2 * env * v);
            ph += 2.0 * M_PI * hz / SR;
        }
        auto s = starts(analyse(sig));
        CHECK(s.size() == 1, "vibrato +-0.4 st: %zu starts", s.size());
    }
}

static void testRobustness() {
    std::printf("[quiet playing, background noise, 48/96 kHz]\n");
    std::vector<int> pitches = {55, 59, 62, 66, 64, 60, 57, 52};
    for (double amp : {0.3, 0.02}) {
        std::vector<float> sig((size_t)(SR * 2.5), 0.f);
        addNoise(sig, 0.0, 2.5, amp * 0.03, true, 9);   // ~ -30 dB below the notes
        for (auto& n : melody(0.1, 8, 0.25, 0.2, pitches, amp)) addTone(sig, n);
        AnalyzerSettings st;
        st.gateDb = amp > 0.1 ? -32.f : -55.f;   // gate set just above the room noise
        auto s = starts(analyse(sig, st));
        int good = 0;
        for (size_t i = 0; i < s.size() && i < 8; ++i)
            if (s[i].pitched && std::fabs(s[i].pitch - pitches[i]) < 0.4) ++good;
        CHECK(s.size() == 8 && good == 8, "amp %.2f + noise: %zu starts, %d right", amp, s.size(), good);
    }
    for (double sr : {48000.0, 96000.0}) {
        Analyzer a; a.prepare(sr);
        std::vector<float> sig((size_t)(sr * 1.0), 0.f);
        double ph = 0;
        for (int i = (int)(0.2 * sr); i < (int)(0.6 * sr); ++i) {
            double v = 0; for (int k = 1; k <= 6; ++k) v += std::sin(k * ph) / k;
            sig[i] = (float)(0.2 * std::min(1.0, (i - 0.2 * sr) / (0.005 * sr)) * v);
            ph += 2 * M_PI * 220.0 / sr;
        }
        std::vector<HeardEvent> ev;
        for (int64_t p = 0; p < (int64_t)sig.size(); p += 512) {
            a.process(sig.data() + p, (int)std::min<int64_t>(512, sig.size() - p), p);
            for (int i = 0; i < a.numEvents(); ++i) if (a.event(i).type == HeardEvent::Start) ev.push_back(a.event(i));
            a.clearEvents();
        }
        CHECK(ev.size() == 1 && std::fabs(ev[0].pitch - 57.f) < 0.3, "%.0f Hz: %zu starts", sr, ev.size());
    }
}

static void testNoiseAndSilence() {
    std::printf("[noise / brightness, silence]\n");
    std::vector<float> sig((size_t)(SR * 1.5), 0.f);
    addNoise(sig, 0.2, 0.3, 0.15, false, 1);
    addNoise(sig, 0.9, 0.3, 0.15, true, 2);
    auto s = starts(analyse(sig));
    CHECK(s.size() == 2, "noise: %zu starts", s.size());
    if (s.size() == 2) {
        CHECK(!s[0].pitched && !s[1].pitched, "noise flagged pitched");
        CHECK(s[1].pitch > s[0].pitch + 12.f, "bright %.1f should be well above dark %.1f",
              s[1].pitch, s[0].pitch);
        std::printf("  dark noise -> %.1f (centroid %.0f Hz), bright -> %.1f (%.0f Hz)\n",
                    s[0].pitch, s[0].centroidHz, s[1].pitch, s[1].centroidHz);
    }
    std::vector<float> quiet((size_t)(SR * 1.0), 0.f);
    addNoise(quiet, 0.0, 1.0, 0.0003, true, 3); // ~ -70 dB
    CHECK(starts(analyse(quiet)).empty(), "events on near-silence");
}

static Engine makeEngine(Params p) {
    Engine e;
    e.prepare(SR);
    e.setParams(p);
    e.setSeed(1234);
    return e;
}

static void testReflex() {
    std::printf("[reactivity: reflex]\n");
    Params p;
    p.patience = 0.f; p.interruption = 1.f; p.length = 1.f;
    Engine e = makeEngine(p);
    std::vector<int> pitches = {60, 64, 67, 72, 67, 64};
    std::vector<float> sig((size_t)(SR * 3.0), 0.f);
    auto mel = melody(0.2, 6, 0.4, 0.3, pitches);
    for (auto& n : mel) addTone(sig, n);
    auto cap = runEngine(e, sig);
    auto o = ons(cap);
    CHECK(o.size() == 6, "reflex: %zu answer notes for 6 heard", o.size());
    double worst = 0;
    for (auto& n : mel) {
        double best = 1e9;
        for (auto& m : o) {
            double d = m.t / SR - n.start;
            if (d >= 0 && d < best) best = d;
        }
        worst = std::max(worst, best);
    }
    std::printf("  worst delay heard->answered: %.1f ms\n", worst * 1000.0);
    CHECK(worst < 0.06, "reflex delay %.1f ms", worst * 1000.0);
    std::string why;
    CHECK(balanced(cap, why), "reflex: %s", why.c_str());
}

static void testListenMode() {
    std::printf("[reactivity: waits for the end of the phrase]\n");
    Params p;
    p.patience = 1.f; p.interruption = 0.f; p.length = 1.f;
    Engine e = makeEngine(p);
    std::vector<int> pitches = {57, 60, 62, 64, 62, 60, 57, 55};
    std::vector<float> sig((size_t)(SR * 8.0), 0.f);
    for (auto& n : melody(0.2, 8, 0.25, 0.2, pitches)) addTone(sig, n);
    // phrase ends at ~2.2 s
    auto cap = runEngine(e, sig);
    auto o = ons(cap);
    CHECK(!o.empty(), "listen: no answer");
    if (!o.empty()) {
        const double first = o[0].t / SR;
        std::printf("  phrase ends 2.22 s, answer starts %.2f s (wait %.2f s), %zu notes\n",
                    first, first - 2.22, o.size());
        CHECK(first > 2.22 + e.silenceWaitSeconds() - 0.1, "answered too early at %.2f", first);
        CHECK(first < 2.22 + e.silenceWaitSeconds() + 0.15, "answered too late at %.2f", first);
    }
    CHECK(o.size() == 8, "listen: %zu notes, expected 8 (length 1)", o.size());
    std::string why;
    CHECK(balanced(cap, why), "listen: %s", why.c_str());
}

static void testInterjection() {
    std::printf("[reactivity: answers mid-phrase]\n");
    Params p;
    p.patience = 0.5f; p.interruption = 1.f; p.length = 1.f;
    Engine e = makeEngine(p);
    std::vector<float> sig((size_t)(SR * 4.0), 0.f);
    std::vector<int> pitches = {60, 62, 63, 65, 67, 68, 70, 72, 70, 68, 67, 65};
    for (auto& n : melody(0.2, 12, 0.2, 0.15, pitches)) addTone(sig, n);
    auto cap = runEngine(e, sig);
    auto o = ons(cap);
    int during = 0;
    for (auto& m : o) if (m.t / SR < 2.6) ++during;
    std::printf("  needs %d notes, answered %d notes while you were playing\n", e.eventsNeeded(), during);
    CHECK(during > 0, "should talk while the phrase goes on");
}

static void testYield() {
    std::printf("[politeness: stops when you start again]\n");
    Params p;
    p.patience = 0.1f; p.interruption = 0.f; p.length = 2.f;
    Engine e = makeEngine(p);
    std::vector<float> sig((size_t)(SR * 6.0), 0.f);
    std::vector<int> pitches = {60, 64, 67, 64};
    for (auto& n : melody(0.2, 4, 0.3, 0.25, pitches)) addTone(sig, n);   // ends ~1.4 s
    // answer: 8 notes from ~1.5 s; player resumes at 1.9 s
    for (auto& n : melody(1.9, 4, 0.3, 0.25, pitches)) addTone(sig, n);   // ends ~3.1 s
    auto cap = runEngine(e, sig);
    auto o = ons(cap);
    int before = 0, overlap = 0;
    for (auto& m : o) {
        double t = m.t / SR;
        if (t < 1.9) ++before;
        else if (t > 1.95 && t < 3.1) ++overlap;
    }
    // sounding at 1.96 s?
    int sounding[128] = {};
    for (auto& m : cap.ev) {
        if (m.t / SR > 1.96) break;
        sounding[m.pitch] = m.on ? 1 : 0;
    }
    int still = 0;
    for (int v : sounding) still += v;
    std::printf("  %d notes before you resumed, %d while you played, %d still sounding\n", before,
                overlap, still);
    CHECK(before > 0, "should have started answering");
    CHECK(overlap == 0, "kept talking over you (%d notes)", overlap);
    CHECK(still == 0, "%d notes still sounding after you resumed", still);
    std::string why;
    CHECK(balanced(cap, why), "yield: %s", why.c_str());
}

static void testRangeScaleBalance() {
    std::printf("[range, scale lock, note balance]\n");
    Params p;
    p.patience = 0.3f; p.interruption = 0.5f; p.length = 2.f;
    p.rangeLow = 48; p.rangeHigh = 72; p.scaleLock = 1.f; p.contrast = 0.6f;
    p.fidelity = 0.3f;
    Engine e = makeEngine(p);
    // C major pentatonic: C D E G A
    std::vector<int> pitches = {60, 62, 64, 67, 69, 72, 69, 67, 64, 62, 60, 57};
    std::vector<float> sig((size_t)(SR * 12.0), 0.f);
    for (int r = 0; r < 3; ++r)
        for (auto& n : melody(0.2 + r * 3.5, 12, 0.18, 0.15, pitches)) addTone(sig, n);
    auto cap = runEngine(e, sig);
    auto o = ons(cap);
    int outRange = 0, outScale = 0;
    for (auto& m : o) {
        if (m.pitch < 48 || m.pitch > 72) ++outRange;
        int pc = m.pitch % 12;
        if (!(pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9)) ++outScale;
    }
    std::printf("  %zu answer notes, %d out of range, %d out of scale\n", o.size(), outRange, outScale);
    CHECK(!o.empty(), "no answer");
    CHECK(outRange == 0, "%d out of range", outRange);
    CHECK(outScale == 0, "%d out of scale", outScale);
    std::string why;
    CHECK(balanced(cap, why), "balance: %s", why.c_str());
}

static void testFidelity() {
    std::printf("[fidelity: quotes what you played]\n");
    Params p;
    p.fidelity = 1.f; p.contrast = 0.f; p.scaleLock = 0.f; p.memory = 1.f;
    Engine e = makeEngine(p);
    std::vector<int> motif = {62, 65, 69, 67, 64, 60};
    std::vector<float> sig((size_t)(SR * 6.0), 0.f);
    for (auto& n : melody(0.2, 24, 0.2, 0.15, motif)) addTone(sig, n);
    runEngine(e, sig);
    GenNote g[16];
    int k = e.generate(12, g, 16);
    // intervals of the motif (cyclic)
    std::vector<int> iv;
    for (size_t i = 0; i < motif.size(); ++i)
        iv.push_back(motif[(i + 1) % motif.size()] - motif[i]);
    int fit = 0;
    for (int i = 1; i < k; ++i) {
        int d = g[i].pitch - g[i - 1].pitch;
        for (int x : iv) if (d == x || d == x + 12 || d == x - 12) { ++fit; break; }
    }
    std::printf("  %d/%d answer intervals come from the motif\n", fit, k - 1);
    CHECK(k == 12, "generated %d", k);
    CHECK(fit == k - 1, "only %d/%d intervals from motif", fit, k - 1);
}

static void testGrid() {
    std::printf("[grid sync]\n");
    Params p;
    p.patience = 0.f; p.interruption = 1.f; p.grid = 3;
    Engine e = makeEngine(p);
    std::vector<float> sig((size_t)(SR * 3.0), 0.f);
    for (auto& n : melody(0.13, 6, 0.37, 0.2, {60, 63, 67})) addTone(sig, n);
    Transport tr; tr.valid = true; tr.playing = true; tr.bpm = 120.0;
    auto cap = runEngine(e, sig, 256, tr);
    const double step = SR * 60.0 / 120.0 / 4.0;
    int off = 0;
    auto o = ons(cap);
    for (auto& m : o) {
        double r = std::fmod((double)m.t, step);
        if (std::min(r, step - r) > 1.0) ++off;
    }
    CHECK(!o.empty(), "grid: no notes");
    CHECK(off == 0, "grid: %d notes off the 1/16 grid", off);
}

static void testNoAllocAndFuzz() {
    std::printf("[real-time safety, fuzz]\n");
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    for (int run = 0; run < 12; ++run) {
        Params p;
        p.patience = u(rng); p.interruption = u(rng); p.length = 0.25f + 1.75f * u(rng);
        p.memory = u(rng); p.fidelity = u(rng); p.contrast = u(rng); p.scaleLock = u(rng);
        p.rangeLow = 20 + (int)(u(rng) * 60); p.rangeHigh = p.rangeLow + (int)(u(rng) * 50);
        p.sensitivity = u(rng); p.grid = run % 4; p.channel = 1 + run % 16;
        p.bright = run % 3 == 1; p.randomness = u(rng); p.humanize = u(rng);
        Engine e = makeEngine(p);
        std::vector<float> sig((size_t)(SR * 6.0), 0.f);
        for (int i = 0; i < 40; ++i) {
            double t = u(rng) * 5.5;
            if (u(rng) < 0.6f) addTone(sig, {t, 0.05 + 0.4 * u(rng), 60.0 + 1500.0 * u(rng), 0.05 + 0.3 * u(rng)}, u(rng) < 0.5f);
            else addNoise(sig, t, 0.05 + 0.3 * u(rng), 0.1 * u(rng), u(rng) < 0.5f, run * 100 + i);
        }
        if (run == 5) for (int i = 1000; i < 1200; ++i) sig[i] = NAN;
        if (run == 6) for (auto& s : sig) s *= 8.f;  // clipping-level input
        Transport tr; tr.valid = run % 2 == 0; tr.playing = true; tr.bpm = 90 + run * 7;
        g_allocs = 0;
        int B = 32 << (run % 5);
        auto cap = runEngine(e, sig, B, tr, true);
        Capture& all = cap;
        std::string why;
        CHECK(balanced(all, why), "fuzz run %d: %s", run, why.c_str());
#ifdef LPFD_ALLOC_CHECK
        CHECK(g_allocs == 0, "fuzz run %d: %ld allocations in process()", run, (long)g_allocs);
#endif
    }
}

// ---- v0.2: bright mode, harmony, randomness, humanize ----
static void addSine(std::vector<float>& buf, double start, double dur, double hz0, double hz1, double amp) {
    const int s0 = (int)(start * SR), len = (int)(dur * SR), ramp = (int)(0.003 * SR);
    double ph = 0.0;
    for (int i = 0; i < len && s0 + i < (int)buf.size(); ++i) {
        double env = std::min(1.0, std::min((double)i / ramp, (double)(len - i) / ramp));
        double hz = hz0 * std::pow(hz1 / hz0, (double)i / len);
        buf[s0 + i] += (float)(amp * env * std::sin(ph));
        ph += 2.0 * M_PI * hz / SR;
    }
}

static std::vector<HeardEvent> analyseBright(const std::vector<float>& sig) {
    AnalyzerSettings st; st.bright = true;
    return analyse(sig, st);
}

static void testBright() {
    std::printf("[bright mode: birds and voices]\n");
    for (double hz : {2500.0, 4200.0, 6000.0, 9000.0}) {
        std::vector<float> sig((size_t)(SR * 0.6), 0.f);
        addSine(sig, 0.1, 0.2, hz, hz, 0.2);
        auto s = starts(analyseBright(sig));
        const double truth = 69.0 + 12.0 * std::log2(hz / 440.0);
        CHECK(s.size() == 1 && s[0].pitched && std::fabs(s[0].pitch - truth) < 0.35,
              "whistle %.0f Hz: %zu starts, pitch %.2f (truth %.2f)", hz, s.size(),
              s.empty() ? 0.f : s[0].pitch, truth);
        if (!s.empty())
            std::printf("  whistle %5.0f Hz -> %.2f (truth %.2f), sure after %.1f ms\n", hz, s[0].pitch,
                        truth, (s[0].commitTime / SR - 0.1) * 1000.0);
    }
    {
        // trill: 14 notes of 30 ms, 12 ms apart, alternating 4.0 / 4.6 kHz
        std::vector<float> sig((size_t)(SR * 1.0), 0.f);
        for (int i = 0; i < 14; ++i) addSine(sig, 0.1 + i * 0.042, 0.030, i % 2 ? 4600.0 : 4000.0, i % 2 ? 4600.0 : 4000.0, 0.2);
        auto sb = starts(analyseBright(sig));
        auto si = starts(analyse(sig));
        std::printf("  trill of 14 notes: bright hears %zu, instrument mode hears %zu\n", sb.size(), si.size());
        CHECK(sb.size() >= 13, "trill: bright heard %zu of 14", sb.size());
    }
    {
        // rising chirp 2 -> 4 kHz in 200 ms: heard as a contour of several notes
        std::vector<float> sig((size_t)(SR * 0.6), 0.f);
        addSine(sig, 0.1, 0.2, 2000.0, 4000.0, 0.2);
        auto s = starts(analyseBright(sig));
        bool rising = s.size() >= 3;
        for (size_t i = 1; i < s.size(); ++i) rising = rising && s[i].pitch > s[i - 1].pitch;
        std::printf("  chirp 2->4 kHz: %zu notes\n", s.size());
        CHECK(rising, "chirp: %zu notes, should be >= 3 and rising", s.size());
    }
    {
        // sung syllables around 150-250 Hz, 90 ms each
        std::vector<int> pitches = {50, 52, 55, 57, 55, 52};
        std::vector<float> sig((size_t)(SR * 1.2), 0.f);
        for (auto& n : melody(0.1, 6, 0.13, 0.09, pitches, 0.25)) addTone(sig, n);
        auto s = starts(analyseBright(sig));
        int good = 0;
        for (size_t i = 0; i < s.size() && i < 6; ++i) if (std::fabs(s[i].pitch - pitches[i]) < 0.4) ++good;
        CHECK(s.size() == 6 && good == 6, "syllables: %zu starts, %d right", s.size(), good);
    }
}

struct NullSink : MidiSink {
    void noteOn(int, int, int, int) override {}
    void noteOff(int, int, int) override {}
};

static Harmony listen(const std::vector<float>& sig) {
    Params p; p.respond = false;
    Engine e = makeEngine(p);
    NullSink n;
    Transport tr;
    for (int64_t pos = 0; pos < (int64_t)sig.size(); pos += 256)
        e.process(sig.data() + pos, (int)std::min<int64_t>(256, sig.size() - pos), tr, n);
    e.updateHarmony();
    return e.harmony();
}

static std::string chordName(const Harmony& h) {
    return h.chordRoot < 0 ? "?" : std::string(Harmony::pcLetter(h.chordRoot)) + Harmony::chordSuffix(h.chord);
}
static std::string keyName(const Harmony& h) {
    return h.keyRoot < 0 ? "?" : std::string(Harmony::pcName(h.keyRoot)) + " " + Harmony::scaleName(h.scale);
}

static void testHarmony() {
    std::printf("[scale and chord detection]\n");
    struct C { const char* name; std::vector<int> notes; int root, type, alt; };
    std::vector<C> chords = {
        {"C (strummed)", {48, 52, 55, 60}, 0, Harmony::Maj, -1},
        {"Am (strummed)", {45, 52, 57, 60, 64}, 9, Harmony::Min, Harmony::Min7},
        {"G7 (strummed)", {43, 47, 50, 53, 55}, 7, Harmony::Dom7, -1},
        {"Fmaj7 (strummed)", {41, 48, 52, 57, 60}, 5, Harmony::Maj7, -1},
    };
    for (auto& c : chords) {
        std::vector<float> sig((size_t)(SR * 2.0), 0.f);
        for (int i = 0; i < (int)c.notes.size(); ++i)
            addTone(sig, {0.1 + i * 0.015, 1.6, midiToHz(c.notes[i]), 0.12}, true);
        Harmony h = listen(sig);
        std::printf("  %-18s -> %s\n", c.name, chordName(h).c_str());
        CHECK(h.chordRoot == c.root && (h.chord == c.type || h.chord == c.alt), "%s detected as %s", c.name,
              chordName(h).c_str());
    }
    {
        // chord change: C then G, the detection follows
        std::vector<float> sig((size_t)(SR * 3.2), 0.f);
        for (int i = 0; i < 4; ++i) addTone(sig, {0.1 + i * 0.015, 1.4, midiToHz(std::vector<int>{48, 52, 55, 60}[i]), 0.12}, true);
        for (int i = 0; i < 4; ++i) addTone(sig, {1.6 + i * 0.015, 1.4, midiToHz(std::vector<int>{43, 47, 50, 55}[i]), 0.12}, true);
        Harmony h = listen(sig);
        std::printf("  %-18s -> %s\n", "C then G", chordName(h).c_str());
        CHECK(h.chordRoot == 7 && h.chord == Harmony::Maj, "C then G detected as %s", chordName(h).c_str());
    }
    {
        // a melody over a held Dm7 pad: the pitch tracker can hear a 'missing fundamental' (Bb),
        // the spectrum knows better
        std::vector<float> sig((size_t)(SR * 3.05), 0.f);
        for (int n : {50, 57, 62, 65}) addTone(sig, {0.0, 3.0, midiToHz(n), 0.05});
        for (auto& n : melody(0.0, 12, 0.25, 0.2, {62, 65, 69, 72, 69, 67, 65, 64, 62, 60, 62, 65}, 0.15)) addTone(sig, n);
        Harmony h = listen(sig);
        std::printf("  %-18s -> %s\n", "melody over Dm7", chordName(h).c_str());
        CHECK(h.chordRoot == 2 && (h.chord == Harmony::Min || h.chord == Harmony::Min7),
              "melody over Dm7 pad detected as %s", chordName(h).c_str());
    }
    {
        // arpeggiated Am7, monophonic
        std::vector<float> sig((size_t)(SR * 2.5), 0.f);
        for (auto& n : melody(0.1, 8, 0.25, 0.22, {57, 60, 64, 67})) addTone(sig, n);
        Harmony h = listen(sig);
        std::printf("  %-18s -> %s\n", "Am7 (arpeggio)", chordName(h).c_str());
        CHECK(h.chordRoot == 9 && (h.chord == Harmony::Min7 || h.chord == Harmony::Min) ,
              "Am7 arpeggio detected as %s", chordName(h).c_str());
    }
    struct S { const char* name; std::vector<int> mel; uint16_t mask; };
    // D dorian melody (white keys centred on D) and A harmonic minor (with G#)
    std::vector<S> scales = {
        {"D dorian melody", {62, 64, 65, 67, 69, 71, 72, 74, 72, 69, 65, 62, 64, 62, 69, 62}, 0b101010110101},
        {"A harm. minor", {57, 59, 60, 62, 64, 65, 68, 69, 68, 64, 60, 57, 59, 68, 69, 57}, 0},
    };
    // A harmonic minor = pcs 9 11 0 2 4 5 8
    scales[1].mask = (1 << 9) | (1 << 11) | (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 8);
    for (auto& sc : scales) {
        std::vector<float> sig((size_t)(SR * 5.0), 0.f);
        for (auto& n : melody(0.1, 16, 0.25, 0.22, sc.mel)) addTone(sig, n);
        Harmony h = listen(sig);
        std::printf("  %-18s -> %s\n", sc.name, keyName(h).c_str());
        CHECK(h.scaleMask == sc.mask, "%s: mask %03x want %03x (%s)", sc.name, h.scaleMask, sc.mask, keyName(h).c_str());
    }
}

static void testRandomAndHumanize() {
    std::printf("[randomness and humanize]\n");
    std::vector<int> penta = {60, 62, 64, 67, 69, 72, 69, 67, 64, 62, 60, 57};
    std::vector<float> sig((size_t)(SR * 14.0), 0.f);
    for (int r = 0; r < 3; ++r)
        for (auto& n : melody(0.2 + r * 4.0, 12, 0.18, 0.15, penta)) addTone(sig, n);
    auto run = [&](float rnd, float hum) {
        Params p;
        p.patience = 0.3f; p.interruption = 0.f; p.length = 1.5f;
        p.scaleLock = 1.f; p.randomness = rnd; p.humanize = hum;
        Engine e = makeEngine(p);
        return runEngine(e, sig);
    };
    auto plain = ons(run(0.f, 0.f)), wild = ons(run(1.f, 0.f));
    int outScale = 0;
    for (auto& m : wild) {
        const int pc = m.pitch % 12;
        if (!(pc == 0 || pc == 2 || pc == 4 || pc == 7 || pc == 9)) ++outScale;
    }
    int same = 0;
    for (size_t i = 0; i < std::min(plain.size(), wild.size()); ++i) same += plain[i].pitch == wild[i].pitch;
    std::printf("  randomness 0: %zu notes, randomness 1: %zu notes (%d out of scale, %d/%zu identical)\n",
                plain.size(), wild.size(), outScale, same, std::min(plain.size(), wild.size()));
    CHECK(outScale == 0, "random notes out of the scale: %d", outScale);
    CHECK(same < (int)std::min(plain.size(), wild.size()) * 0.7, "randomness changed too little");

    // reflex delays: steady without humanize, varied (but still quick) with it
    auto delays = [&](float hum, double& mean, double& sd, double& vsd) {
        Params p; p.patience = 0.f; p.interruption = 1.f; p.humanize = hum;
        Engine e = makeEngine(p);
        std::vector<float> s2((size_t)(SR * 6.0), 0.f);
        auto mel = melody(0.2, 12, 0.4, 0.3, {60, 64, 67, 72});
        for (auto& n : mel) addTone(s2, n);
        auto o = ons(runEngine(e, s2));
        std::vector<double> d;
        std::vector<double> v;
        for (auto& n : mel) {
            double best = 1e9;
            for (auto& m : o) { double x = m.t / SR - n.start; if (x >= 0 && x < best) best = x; }
            if (best < 1.0) d.push_back(best);
        }
        for (auto& m : o) v.push_back(m.vel);
        mean = 0; for (double x : d) mean += x; mean /= std::max<size_t>(1, d.size());
        sd = 0; for (double x : d) sd += (x - mean) * (x - mean); sd = std::sqrt(sd / std::max<size_t>(1, d.size()));
        double vm = 0; for (double x : v) vm += x; vm /= std::max<size_t>(1, v.size());
        vsd = 0; for (double x : v) vsd += (x - vm) * (x - vm); vsd = std::sqrt(vsd / std::max<size_t>(1, v.size()));
    };
    double m0, s0, v0, m1, s1, v1;
    delays(0.f, m0, s0, v0);
    delays(1.f, m1, s1, v1);
    std::printf("  reflex delay: machine %.0f +- %.1f ms, humanized %.0f +- %.1f ms; velocity spread %.1f -> %.1f\n",
                m0 * 1000, s0 * 1000, m1 * 1000, s1 * 1000, v0, v1);
    CHECK(s0 < 0.003, "machine delay should be steady (%.1f ms)", s0 * 1000);
    CHECK(s1 > 0.008 && m1 < 0.09, "humanized delay %.0f +- %.1f ms", m1 * 1000, s1 * 1000);
    CHECK(v1 > v0 + 2.0, "humanize should spread velocities (%.1f vs %.1f)", v1, v0);
}

// ---- v0.3: MIDI input ----
struct MIn { double t; bool on; int ch, pitch, vel; };

static std::vector<MIn> midiPhrase(double t0, const std::vector<int>& pitches, double step, double dur,
                                   int ch = 1, int vel = 90) {
    std::vector<MIn> v;
    for (size_t i = 0; i < pitches.size(); ++i) {
        v.push_back({t0 + i * step, true, ch, pitches[i], vel});
        v.push_back({t0 + i * step + dur, false, ch, pitches[i], 0});
    }
    return v;
}

// Runs the engine with MIDI input (and optional audio). feedback: the engine's own output
// is sent back to its input one block later (a MIDI loop).
static Capture runMidi(Engine& e, std::vector<MIn> in, double seconds, bool feedback = false,
                       const std::vector<float>* audio = nullptr, bool countAllocs = false) {
    std::sort(in.begin(), in.end(), [](const MIn& a, const MIn& b) { return a.t < b.t; });
    Capture cap;
    const int B = 256;
    const int64_t total = (int64_t)(seconds * SR);
    std::vector<float> zeros(B, 0.f);
    size_t next = 0;
    std::vector<Midi> loop;
    for (int64_t pos = 0; pos < total; pos += B) {
        while (next < in.size() && (int64_t)(in[next].t * SR) < pos + B) {
            const MIn& m = in[next++];
            e.addMidiInput((int)std::max<int64_t>(0, (int64_t)(m.t * SR) - pos), m.on, m.ch, m.pitch, m.vel);
        }
        for (auto& m : loop) e.addMidiInput((int)(m.t - (pos - B)), m.on, m.ch, m.pitch, m.vel);
        loop.clear();
        const size_t before = cap.ev.size();
        cap.base = pos;
        const float* a = audio && pos + B <= (int64_t)audio->size() ? audio->data() + pos : zeros.data();
        if (countAllocs) g_countAllocs = true;
        e.process(a, B, Transport{}, cap);
        g_countAllocs = false;
        if (feedback) loop.assign(cap.ev.begin() + (long)before, cap.ev.end());
    }
    return cap;
}

static void testMidiInput() {
    std::printf("[MIDI input]\n");
    {
        Params p; p.source = 1; p.patience = 0.f; p.interruption = 1.f;
        Engine e = makeEngine(p);
        auto in = midiPhrase(0.2, {60, 64, 67, 72, 67, 64}, 0.4, 0.3);
        auto cap = runMidi(e, in, 6.0);
        auto o = ons(cap);
        double worst = 0;
        for (auto& m : in) {
            if (!m.on) continue;
            double best = 1e9;
            for (auto& x : o) { double d = x.t / SR - m.t; if (d >= -1e-9 && d < best) best = d; }
            worst = std::max(worst, best);
        }
        std::printf("  reflex on MIDI: %zu answers for 6 notes, worst delay %.2f ms\n", o.size(), worst * 1000);
        CHECK(o.size() == 6, "MIDI reflex: %zu answers", o.size());
        CHECK(worst < 0.001, "MIDI reflex delay %.2f ms", worst * 1000);
        std::string why;
        CHECK(balanced(cap, why), "MIDI reflex: %s", why.c_str());
    }
    {
        // held chord + scale run
        Params p; p.source = 1; p.respond = false;
        Engine e = makeEngine(p);
        auto in = midiPhrase(0.1, {62, 64, 65, 67, 69, 71, 72, 74, 72, 69, 65, 62}, 0.2, 0.18);
        for (int n : {48, 52, 55, 58}) { in.push_back({3.0, true, 1, n, 80}); in.push_back({5.5, false, 1, n, 0}); }
        Capture dummy;
        runMidi(e, in, 4.5);
        e.updateHarmony();
        const Harmony& h = e.harmony();
        std::printf("  held C E G Bb -> %s, scale %s\n", chordName(h).c_str(), keyName(h).c_str());
        CHECK(h.chordRoot == 0 && h.chord == Harmony::Dom7, "held chord detected as %s", chordName(h).c_str());
        CHECK((h.scaleMask & 0xFFF) != 0, "no scale");
        CHECK(e.memorySize() == 13, "memory %d events (12 notes + 1 chord expected)", e.memorySize());
    }
    {
        // a MIDI loop must not make it talk to itself
        Params p; p.source = 1; p.patience = 0.2f; p.interruption = 1.f; p.length = 2.f;
        Engine e = makeEngine(p);
        auto in = midiPhrase(0.2, {60, 62, 64, 65, 67}, 0.25, 0.2);
        auto cap = runMidi(e, in, 10.0, true);
        std::printf("  MIDI loop: %d notes in memory for 5 played, %zu answer notes\n", e.memorySize(), ons(cap).size());
        CHECK(e.memorySize() == 5, "loop: memory grew to %d", e.memorySize());
        CHECK(ons(cap).size() < 40, "loop: %zu notes, it is answering itself", ons(cap).size());
    }
    {
        // input channel filter
        Params p; p.source = 1; p.midiInChannel = 2; p.respond = false;
        Engine e = makeEngine(p);
        auto in = midiPhrase(0.2, {60, 62, 64}, 0.25, 0.2, 1);
        auto in2 = midiPhrase(1.2, {67, 69}, 0.25, 0.2, 2);
        in.insert(in.end(), in2.begin(), in2.end());
        runMidi(e, in, 3.0);
        CHECK(e.memorySize() == 2, "channel filter: %d events (want 2)", e.memorySize());
    }
    {
        // audio only ignores MIDI; both hears both
        std::vector<float> sig((size_t)(SR * 4.0), 0.f);
        for (auto& n : melody(0.2, 4, 0.3, 0.25, {57, 60, 62, 64})) addTone(sig, n);
        auto in = midiPhrase(2.2, {72, 74, 76}, 0.3, 0.25);
        for (int src : {0, 2}) {
            Params p; p.source = src; p.respond = false;
            Engine e = makeEngine(p);
            runMidi(e, in, 4.0, false, &sig);
            CHECK(e.memorySize() == (src == 0 ? 4 : 7), "source %d: %d events", src, e.memorySize());
        }
    }
    {
        // MIDI only: listen-mode waits for the keys to be released
        Params p; p.source = 1; p.patience = 1.f; p.interruption = 0.f;
        Engine e = makeEngine(p);
        std::vector<MIn> in = midiPhrase(0.2, {60, 63, 67}, 0.3, 0.25);
        in.push_back({1.2, true, 1, 48, 70});
        in.push_back({3.0, false, 1, 48, 0});      // a long bass note held until 3 s
        auto o = ons(runMidi(e, in, 8.0));
        CHECK(!o.empty() && o[0].t / SR > 3.0 + e.silenceWaitSeconds() - 0.05,
              "answered at %.2f s while a key was held", o.empty() ? 0.0 : o[0].t / SR);
    }
    {
        // allocation-free with MIDI + fuzz
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        std::vector<MIn> in;
        for (int i = 0; i < 400; ++i) {
            double t = u(rng) * 9.0;
            int pch = 20 + (int)(u(rng) * 90);
            in.push_back({t, true, 1 + (int)(u(rng) * 3), pch, (int)(u(rng) * 127)});
            in.push_back({t + u(rng) * 0.5, false, 1 + (int)(u(rng) * 3), pch, 0});
        }
        Params p; p.source = 2; p.patience = 0.3f; p.interruption = 0.6f; p.randomness = 0.5f; p.humanize = 0.5f;
        Engine e = makeEngine(p);
        g_allocs = 0;
        auto cap = runMidi(e, in, 16.0, true, nullptr, true);
        std::string why;
        CHECK(balanced(cap, why), "MIDI fuzz: %s", why.c_str());
#ifdef LPFD_ALLOC_CHECK
        CHECK(g_allocs == 0, "MIDI fuzz: %ld allocations", (long)g_allocs);
#endif
    }
}

static void testTimelineAndPhase() {
    std::printf("[timeline and phase for the display]\n");
    Params p; p.patience = 0.6f; p.interruption = 0.f;
    Engine e = makeEngine(p);
    std::vector<float> sig((size_t)(SR * 6.0), 0.f);
    for (auto& n : melody(0.2, 5, 0.25, 0.2, {60, 62, 64, 65, 67})) addTone(sig, n);   // ends ~1.4 s
    Capture cap;
    bool sawWaiting = false, sawListening = false, sawAnswering = false;
    float maxProg = 0.f;
    for (int64_t pos = 0; pos + 256 <= (int64_t)sig.size(); pos += 256) {
        cap.base = pos;
        e.process(sig.data() + pos, 256, Transport{}, cap);
        sawListening |= e.phase() == Engine::Phase::Listening;
        if (e.phase() == Engine::Phase::Waiting) { sawWaiting = true; maxProg = std::max(maxProg, e.waitProgress()); }
        sawAnswering |= e.phase() == Engine::Phase::Answering;
    }
    CHECK(sawListening && sawWaiting && sawAnswering, "phases seen: listen %d wait %d answer %d", sawListening, sawWaiting, sawAnswering);
    CHECK(maxProg > 0.8f, "wait progress reached %.2f", maxProg);
    std::vector<TimelineEvent> tl(512);
    const int n = e.timeline().read(tl.data(), 512);
    int pOn = 0, aOn = 0, trig = 0;
    for (int i = 0; i < n; ++i) {
        pOn += tl[i].kind == TimelineEvent::PlayerOn;
        aOn += tl[i].kind == TimelineEvent::AnswerOn;
        if (tl[i].kind == TimelineEvent::Trigger) {
            ++trig;
            CHECK(tl[i].reason == TimelineEvent::Silence && tl[i].info > 0.5f, "trigger reason %d info %.2f", tl[i].reason, tl[i].info);
        }
    }
    std::printf("  timeline: %d heard, %d answered, %d triggers; sent counter %llu\n", pOn, aOn, trig,
                (unsigned long long)e.notesSent());
    CHECK(pOn == 5 && trig == 1 && aOn == (int)ons(cap).size() && e.notesSent() == (uint64_t)aOn, "timeline counts");
    // test note
    e.requestTestNote();
    Capture c2;
    std::vector<float> z(256, 0.f);
    c2.base = 0;
    e.process(z.data(), 256, Transport{}, c2);
    CHECK(!c2.ev.empty() && c2.ev[0].on && c2.ev[0].pitch == 60, "test note not sent");
    // ring wrap: many events, reader stays consistent
    Timeline t;
    for (int i = 0; i < 5000; ++i) { TimelineEvent x; x.time = i; x.kind = TimelineEvent::PlayerOn; t.push(x); }
    std::vector<TimelineEvent> r(3000);
    const int m = t.read(r.data(), 3000);
    bool inc = m > 0 && r[m - 1].time == 4999;
    for (int i = 1; i < m; ++i) inc = inc && r[i].time == r[i - 1].time + 1;
    CHECK(inc, "timeline wrap: %d events, consistent %d", m, inc);
}

int main() {
    testPitchAccuracy();
    testMelodyAndRepeats();
    testExpressive();
    testRobustness();
    testNoiseAndSilence();
    testReflex();
    testListenMode();
    testInterjection();
    testYield();
    testRangeScaleBalance();
    testFidelity();
    testGrid();
    testBright();
    testHarmony();
    testRandomAndHumanize();
    testMidiInput();
    testTimelineAndPhase();
    testNoAllocAndFuzz();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
