// LPF Dialogue — tests on synthetic signals.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
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
    int k = e.generate(12, g);
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
    testNoAllocAndFuzz();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
