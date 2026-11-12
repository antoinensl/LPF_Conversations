// Conversations — real-time listening: onsets, pitch (YIN), level, brightness, noisiness.
// prepare() allocates; process() is real-time safe (no allocation, no locks, no I/O).
#pragma once
#include <cstdint>
#include <vector>
#include "fft.h"

namespace lpfd {

struct HeardEvent {
    enum Type : uint8_t { Start, End } type = Start;
    int64_t time = 0;        // sample time of the attack (Start) or release (End)
    int64_t commitTime = 0;  // sample time at which the analyser was sure (>= time)
    float pitch = 60.f;      // MIDI pitch: detected, or derived from brightness if !pitched
    bool pitched = false;
    float velocity = 0.5f;   // 0..1, from peak level
    float centroidHz = 0.f;  // spectral centroid
    float flatness = 0.f;    // 0 = tonal, 1 = noise
    bool midi = false;       // came from MIDI input (exact), not from the ears
};

struct AnalyzerSettings {
    float sensitivity = 0.5f;  // 0..1, onset detection
    float gateDb = -55.f;      // below this, silence
    bool bright = false;       // false: instruments (steady). true: birds, voices (fast, gliding)
    float lowHz = 0.f;         // lowest fundamental it listens for; below, filtered out (rumble, wind).
    float highHz = 0.f;        // highest fundamental. 0 = automatic: 55 Hz-2 kHz, or 100 Hz-10 kHz in bright mode
};

class Analyzer {
public:
    void prepare(double sampleRate);
    void reset();
    void setSettings(const AnalyzerSettings& s);

    // Feed mono input. blockStart = absolute sample time of in[0].
    void process(const float* in, int n, int64_t blockStart);

    // Event queue, drained by the caller after each process() call.
    int numEvents() const { return numEvents_; }
    const HeardEvent& event(int i) const { return events_[i]; }
    void clearEvents() { numEvents_ = 0; }

    // Activity for silence detection.
    bool isActive() const { return noteState_ != Idle; }
    int64_t lastActiveTime() const { return lastActive_; }
    float levelDb() const { return levelDb_; }
    float lastPitch() const { return lastF0Midi_; }
    float lastConfidence() const { return lastConf_; }
    // How far the note being played has moved from where it started (bends, vibrato, slides),
    // in semitones. False when no pitched note sounds.
    bool noteDeviation(float& semis) const;

    // Harmonic colour heard in the audio (works on chords too), normalised to sum 1.
    // longTerm = false: last ~1 s (chord), true: last ~20 s (key). Returns total weight.
    float chroma(bool longTerm, float out[12]) const;

    int hop() const { return hop_; }
    double sampleRate() const { return sr_; }

private:
    void analyseFrame(int64_t frameEnd);
    void onsetAnalysis(int64_t frameEnd, bool& onset);
    void pitchAnalysis(float& midi, float& conf);
    void startPending(int64_t t);
    void commitPending(int64_t now, bool pitched, float pitch);
    void endNote(int64_t t);
    void push(const HeardEvent& e);
    float sampleAt(int back) const; // back = 1 -> newest sample

    enum NoteState { Idle, Pending, Active } noteState_ = Idle;

    double sr_ = 44100.0;
    AnalyzerSettings settings_;
    int hop_ = 128, onsetN_ = 512, yinW_ = 1024, yinFftN_ = 4096;
    int pitchEvery_ = 2, hopCount_ = 0, hopIndex_ = 0;
    int tauMin_ = 20, tauMax_ = 900;
    // effective values for the current listening mode
    int wEff_ = 1024, tauMinEff_ = 20, tauMaxEff_ = 900, pitchEveryEff_ = 2;
    float minIoiSec_ = 0.045f, legatoSemis_ = 0.8f, pitchWaitSec_ = 0.040f, maxMidi_ = 110.f;
    int legatoFrames_ = 3;
    bool modeBright_ = false;
    float minMidi_ = 20.f;
    // band filter in front of the analysis (12 dB/oct high-pass, 12 dB/oct low-pass)
    struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
                    float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Biquad hp_, lp_;
    bool lpOn_ = false;
    void applyMode();
    // chroma
    void chromaAnalysis();
    FFT chromaFft_;
    std::vector<float> chromaWin_, chRe_, chIm_, chMag_;
    int chromaCount_ = 0;
    std::vector<int> binPc_;
    std::vector<float> binW_;
    int chromaLo_ = 1, chromaHi_ = 1;
    float chordC_[12] = {}, keyC_[12] = {};
    float chordW_ = 0.f, keyW_ = 0.f, aChord_ = 0.99f, aKey_ = 0.999f;

    std::vector<float> ring_;
    int ringMask_ = 0, writePos_ = 0;
    int64_t written_ = 0;

    FFT onsetFft_, yinFft_;
    std::vector<float> win_, re_, im_, prevLogMag_, prev2LogMag_, curLogMag_, yre_, yim_, xre_, xim_, cumSq_, dprime_;
    std::vector<float> fluxHist_;
    int fluxPos_ = 0;
    float prevFlux_ = 0.f;
    int64_t lastOnset_ = -1000000;
    float levelDb_ = -120.f, prevLevelDb_ = -120.f;
    float slowDb_ = -120.f;      // RMS over ~23 ms, steady even for low notes
    float slowHist_[8];
    int levelPos_ = 0;
    int64_t candTime_ = -1;      // attack candidate waiting for a level rise
    float candBase_ = 0.f;
    int candHops_ = 0;
    int64_t candOnsetTime_ = -1;
    float centHist_[8] = {};     // spectral centroid of the last hops
    int centPos_ = 0;
    float candCentroid_ = 0.f, centSmooth_ = 0.f;
    float centroid_ = 0.f, flatness_ = 0.f;

    // pending/active note
    int64_t pendStart_ = 0;
    float pendPeakDb_ = -120.f;
    float pendPitches_[16];
    int pendCount_ = 0;
    float pendCentroidSum_ = 0.f, pendFlatSum_ = 0.f;
    int pendSpecCount_ = 0;
    float notePitch_ = 0.f, notePeakDb_ = -120.f;
    bool notePitched_ = false;
    int deviateCount_ = 0;
    float deviatePitch_ = 0.f;
    int64_t deviateStart_ = 0;
    int64_t lastActive_ = -1000000;
    float lastF0Midi_ = 0.f, lastConf_ = 0.f;

    HeardEvent events_[64];
    int numEvents_ = 0;
};

} // namespace lpfd
