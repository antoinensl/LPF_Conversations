// LPF Dialogue — offline tool: play a WAV into the engine, get its answers as a MIDI file
// (and optionally a WAV with your audio on the left and a simple synth answer on the right).
//
//   lpf_dialogue_cli input.wav|input.mid answers.mid [--render out.wav] [--patience 0.5] [--interruption 0.3]
//                    [--length 1] [--memory 0.5] [--fidelity 0.6] [--contrast 0.2] [--scale 0.7]
//                    [--low 36] [--high 96] [--sensitivity 0.5] [--gate -55] [--seed 1]
//                    [--bright 1] [--random 0.2] [--humanize 0.3]
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include "../core/engine.h"

using namespace lpfd;

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

static bool readWav(const char* path, std::vector<float>& mono, double& sr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t r;
    while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + r);
    std::fclose(f);
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    int fmt = 0, ch = 0, bits = 0;
    size_t pos = 12;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    while (pos + 8 <= d.size()) {
        uint32_t len = rd32(&d[pos + 4]);
        const uint8_t* c = &d[pos + 8];
        if (!std::memcmp(&d[pos], "fmt ", 4)) {
            fmt = rd16(c); ch = rd16(c + 2); sr = rd32(c + 4); bits = rd16(c + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = rd16(c + 24);
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            data = c;
            dataLen = std::min<size_t>(len, d.size() - pos - 8);
        }
        pos += 8 + len + (len & 1);
    }
    if (!data || ch <= 0) return false;
    const int bps = bits / 8;
    const size_t frames = dataLen / (bps * ch);
    mono.assign(frames, 0.f);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0.f;
        for (int c = 0; c < ch; ++c) {
            const uint8_t* p = data + (i * ch + c) * bps;
            float v = 0.f;
            if (fmt == 3 && bits == 32) { float x; std::memcpy(&x, p, 4); v = x; }
            else if (fmt == 1 && bits == 16) v = (int16_t)rd16(p) / 32768.f;
            else if (fmt == 1 && bits == 24) v = ((int32_t)((p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8) / 8388608.f;
            else if (fmt == 1 && bits == 32) v = (int32_t)rd32(p) / 2147483648.f;
            else if (fmt == 1 && bits == 8) v = (p[0] - 128) / 128.f;
            else return false;
            acc += v;
        }
        mono[i] = acc / ch;
    }
    return true;
}

static void w32(FILE* f, uint32_t v) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; std::fwrite(b, 1, 4, f); }
static void w16(FILE* f, uint16_t v) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; std::fwrite(b, 1, 2, f); }

static bool writeWavStereo(const char* path, const std::vector<float>& l, const std::vector<float>& r, double sr) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint32_t frames = (uint32_t)l.size();
    std::fwrite("RIFF", 1, 4, f); w32(f, 36 + frames * 4); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(f, 16); w16(f, 1); w16(f, 2); w32(f, (uint32_t)sr); w32(f, (uint32_t)sr * 4); w16(f, 4); w16(f, 16);
    std::fwrite("data", 1, 4, f); w32(f, frames * 4);
    for (uint32_t i = 0; i < frames; ++i) {
        w16(f, (uint16_t)(int16_t)std::lround(std::clamp(l[i], -1.f, 1.f) * 32767.f));
        w16(f, (uint16_t)(int16_t)std::lround(std::clamp(r[i], -1.f, 1.f) * 32767.f));
    }
    std::fclose(f);
    return true;
}

struct Ev { int64_t t; bool on; int ch, pitch, vel; };

// Standard MIDI File reader (format 0/1, tempo map). Notes only, times in seconds.
struct InNote { double t; bool on; int ch, pitch, vel; };
static bool readMidi(const char* path, std::vector<InNote>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t r;
    while ((r = std::fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + r);
    std::fclose(f);
    auto be32 = [&](size_t p) { return (uint32_t)d[p] << 24 | (uint32_t)d[p + 1] << 16 | (uint32_t)d[p + 2] << 8 | d[p + 3]; };
    if (d.size() < 14 || std::memcmp(d.data(), "MThd", 4)) return false;
    const int ntr = d[10] << 8 | d[11];
    const int division = d[12] << 8 | d[13];
    if (division & 0x8000) return false;  // SMPTE time not supported
    struct Raw { uint64_t tick; int order; int kind; int a, b, c; };  // kind 0 note, 1 tempo
    std::vector<Raw> raw;
    size_t pos = 8 + be32(4);
    int order = 0;
    for (int t = 0; t < ntr && pos + 8 <= d.size(); ++t) {
        if (std::memcmp(&d[pos], "MTrk", 4)) return false;
        const size_t len = be32(pos + 4), end = std::min(d.size(), pos + 8 + len);
        size_t p = pos + 8;
        uint64_t tick = 0;
        int status = 0;
        auto vlq = [&]() { uint32_t v = 0; while (p < end) { uint8_t c = d[p++]; v = v << 7 | (c & 0x7F); if (!(c & 0x80)) break; } return v; };
        while (p < end) {
            tick += vlq();
            if (p >= end) break;
            uint8_t c = d[p];
            if (c & 0x80) { status = c; ++p; }
            if (status == 0xFF) {
                if (p >= end) break;
                const uint8_t type = d[p++];
                const uint32_t l = vlq();
                if (type == 0x51 && l == 3 && p + 3 <= end)
                    raw.push_back({tick, order++, 1, (int)(d[p] << 16 | d[p + 1] << 8 | d[p + 2]), 0, 0});
                p += l;
                status = 0;
            } else if (status == 0xF0 || status == 0xF7) {
                p += vlq();
                status = 0;
            } else if (status >= 0x80) {
                const int hi = status & 0xF0, ch = (status & 15) + 1;
                const int n = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                if (p + n > end) break;
                const int a = d[p], b = n > 1 ? d[p + 1] : 0;
                p += n;
                if (hi == 0x90) raw.push_back({tick, order++, 0, ch, a, b});
                else if (hi == 0x80) raw.push_back({tick, order++, 0, ch, a, 0});
            } else {
                ++p; // garbage
            }
        }
        pos += 8 + len;
    }
    std::stable_sort(raw.begin(), raw.end(), [](const Raw& x, const Raw& y) { return x.tick < y.tick; });
    double usPerQ = 500000.0, sec = 0.0;
    uint64_t lastTick = 0;
    for (auto& e : raw) {
        sec += (double)(e.tick - lastTick) * usPerQ / 1e6 / division;
        lastTick = e.tick;
        if (e.kind == 1) usPerQ = e.a;
        else out.push_back({sec, e.c > 0, e.a, e.b, e.c});
    }
    return true;
}
struct Collect : MidiSink {
    std::vector<Ev> ev;
    int64_t base = 0;
    void noteOn(int o, int ch, int p, int v) override { ev.push_back({base + o, true, ch, p, v}); }
    void noteOff(int o, int ch, int p) override { ev.push_back({base + o, false, ch, p, 0}); }
};

static void writeVarLen(std::vector<uint8_t>& o, uint32_t v) {
    uint8_t tmp[5]; int n = 0;
    tmp[n++] = v & 0x7F;
    while (v >>= 7) tmp[n++] = 0x80 | (v & 0x7F);
    while (n--) o.push_back(tmp[n]);
}

static bool writeMidi(const char* path, const std::vector<Ev>& ev, double sr) {
    const int ppq = 480;
    const double ticksPerSample = ppq * 2.0 / sr;  // 120 BPM
    std::vector<uint8_t> trk;
    // tempo 120 BPM
    trk.insert(trk.end(), {0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
    int64_t lastTick = 0;
    for (auto& e : ev) {
        int64_t tick = (int64_t)std::llround(e.t * ticksPerSample);
        writeVarLen(trk, (uint32_t)std::max<int64_t>(0, tick - lastTick));
        lastTick = std::max(lastTick, tick);
        trk.push_back((uint8_t)((e.on ? 0x90 : 0x80) | ((e.ch - 1) & 15)));
        trk.push_back((uint8_t)e.pitch);
        trk.push_back((uint8_t)(e.on ? e.vel : 64));
    }
    trk.insert(trk.end(), {0x00, 0xFF, 0x2F, 0x00});
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    const uint8_t hdr[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, (uint8_t)(ppq >> 8), (uint8_t)ppq};
    std::fwrite(hdr, 1, sizeof hdr, f);
    std::fwrite("MTrk", 1, 4, f);
    uint32_t n = (uint32_t)trk.size();
    const uint8_t len[] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
    std::fwrite(len, 1, 4, f);
    std::fwrite(trk.data(), 1, trk.size(), f);
    std::fclose(f);
    return true;
}

// simple plucked synth for the preview render
static void renderAnswer(const std::vector<Ev>& ev, std::vector<float>& out, double sr) {
    struct Voice { int64_t on, off; int pitch; float vel; };
    std::vector<Voice> v;
    for (size_t i = 0; i < ev.size(); ++i) {
        if (!ev[i].on) continue;
        int64_t off = (int64_t)out.size();
        for (size_t j = i + 1; j < ev.size(); ++j)
            if (!ev[j].on && ev[j].pitch == ev[i].pitch) { off = ev[j].t; break; }
        v.push_back({ev[i].t, off, ev[i].pitch, ev[i].vel / 127.f});
    }
    for (auto& x : v) {
        const double hz = 440.0 * std::pow(2.0, (x.pitch - 69) / 12.0);
        const int64_t end = std::min<int64_t>((int64_t)out.size(), x.off + (int64_t)(0.15 * sr));
        for (int64_t t = x.on; t < end; ++t) {
            const double s = (t - x.on) / sr;
            double env = std::min(1.0, s / 0.004) * std::exp(-2.5 * s);
            if (t > x.off) env *= std::exp(-(t - x.off) / (0.03 * sr));
            const double ph = 2.0 * M_PI * hz * s;
            out[t] += (float)(0.25 * x.vel * env * (std::sin(ph) + 0.3 * std::sin(2 * ph) + 0.1 * std::sin(3 * ph)));
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s input.wav answers.mid [--render out.wav] [--patience x] ...\n", argv[0]);
        return 2;
    }
    Params p;
    const char* render = nullptr;
    uint32_t seed = 1;
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        double v = std::atof(argv[i + 1]);
        if (k == "--render") render = argv[i + 1];
        else if (k == "--patience") p.patience = (float)v;
        else if (k == "--interruption") p.interruption = (float)v;
        else if (k == "--length") p.length = (float)v;
        else if (k == "--memory") p.memory = (float)v;
        else if (k == "--fidelity") p.fidelity = (float)v;
        else if (k == "--contrast") p.contrast = (float)v;
        else if (k == "--scale") p.scaleLock = (float)v;
        else if (k == "--low") p.rangeLow = (int)v;
        else if (k == "--high") p.rangeHigh = (int)v;
        else if (k == "--sensitivity") p.sensitivity = (float)v;
        else if (k == "--gate") p.gateDb = (float)v;
        else if (k == "--seed") seed = (uint32_t)v;
        else if (k == "--bright") p.bright = v > 0.5;
        else if (k == "--random") p.randomness = (float)v;
        else if (k == "--humanize") p.humanize = (float)v;
        else { std::fprintf(stderr, "unknown option %s\n", k.c_str()); return 2; }
    }
    std::vector<float> in;
    std::vector<InNote> midiIn;
    double sr = 44100.0;
    const std::string inPath = argv[1];
    const bool midiFile = inPath.size() > 4 && (inPath.substr(inPath.size() - 4) == ".mid" ||
                                                inPath.substr(inPath.size() - 5) == ".midi");
    if (midiFile) {
        if (!readMidi(argv[1], midiIn)) { std::fprintf(stderr, "cannot read %s (standard MIDI file)\n", argv[1]); return 1; }
        double endT = 0.0;
        for (auto& m : midiIn) endT = std::max(endT, m.t);
        in.assign((size_t)((endT + 1.0) * sr), 0.f);
        p.source = 1;
    } else if (!readWav(argv[1], in, sr)) {
        std::fprintf(stderr, "cannot read %s (PCM 8/16/24/32 or float32 WAV)\n", argv[1]);
        return 1;
    }
    in.resize(in.size() + (size_t)(sr * 6.0), 0.f);  // room for the last answer

    Engine e;
    e.prepare(sr);
    e.setParams(p);
    e.setSeed(seed);
    Collect c;
    Transport tr;
    int heard = 0;
    const int B = 256;
    size_t nextMidi = 0;
    for (int64_t pos = 0; pos < (int64_t)in.size(); pos += B) {
        int n = (int)std::min<int64_t>(B, in.size() - pos);
        while (nextMidi < midiIn.size() && (int64_t)(midiIn[nextMidi].t * sr) < pos + n) {
            const InNote& m = midiIn[nextMidi++];
            e.addMidiInput((int)std::max<int64_t>(0, (int64_t)(m.t * sr) - pos), m.on, m.ch, m.pitch, m.vel);
        }
        c.base = pos;
        e.process(in.data() + pos, n, tr, c);
        heard = e.memorySize();
    }
    int answers = 0;
    for (auto& x : c.ev) if (x.on) ++answers;
    std::printf("heard %d notes/sounds, answered with %d notes\n", heard, answers);
    {
        e.updateHarmony();
        const Harmony& h = e.harmony();
        if (h.keyRoot >= 0) std::printf("scale: %s %s\n", Harmony::pcName(h.keyRoot), Harmony::scaleName(h.scale));
        if (h.chordRoot >= 0) std::printf("last chord: %s%s\n", Harmony::pcLetter(h.chordRoot), Harmony::chordSuffix(h.chord));
    }
    for (auto& x : c.ev)
        if (x.on) std::printf("  %8.3f s  note %3d  vel %3d\n", x.t / sr, x.pitch, x.vel);
    if (!writeMidi(argv[2], c.ev, sr)) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    if (render) {
        std::vector<float> r(in.size(), 0.f);
        renderAnswer(c.ev, r, sr);
        if (midiFile) {   // play the input notes too, on the left
            std::vector<Ev> ie;
            for (auto& m : midiIn) ie.push_back({(int64_t)(m.t * sr), m.on, m.ch, m.pitch, m.vel});
            renderAnswer(ie, in, sr);
        }
        if (!writeWavStereo(render, in, r, sr)) { std::fprintf(stderr, "cannot write %s\n", render); return 1; }
    }
    return 0;
}
