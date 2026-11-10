#include "analyzer.h"
#include <algorithm>
#include <cmath>

namespace lpfd {

static inline float dbFromPower(float p) { return 10.f * std::log10(p + 1e-12f); }
static inline float midiFromHz(float hz) { return 69.f + 12.f * std::log2(hz / 440.f); }

void Analyzer::prepare(double sampleRate) {
    sr_ = sampleRate;
    const bool high = sampleRate > 70000.0;
    hop_ = high ? 256 : 128;
    onsetN_ = 4 * hop_;
    yinW_ = high ? 2048 : 1024;
    yinFftN_ = 4 * yinW_;
    pitchEvery_ = 2;
    tauMin_ = std::max(2, (int)(sr_ / 2000.0));
    tauMax_ = std::min(yinW_ - 2, (int)(sr_ / 55.0));

    int ringSize = 1;
    while (ringSize < 2 * yinFftN_) ringSize <<= 1;
    ring_.assign(ringSize, 0.f);
    ringMask_ = ringSize - 1;

    onsetFft_.init(onsetN_);
    yinFft_.init(yinFftN_);
    win_.resize(onsetN_);
    for (int i = 0; i < onsetN_; ++i)
        win_[i] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / onsetN_);
    re_.assign(onsetN_, 0.f);
    im_.assign(onsetN_, 0.f);
    prevLogMag_.assign(onsetN_ / 2, 0.f);
    prev2LogMag_.assign(onsetN_ / 2, 0.f);
    curLogMag_.assign(onsetN_ / 2, 0.f);
    xre_.assign(yinFftN_, 0.f); xim_.assign(yinFftN_, 0.f);
    yre_.assign(yinFftN_, 0.f); yim_.assign(yinFftN_, 0.f);
    cumSq_.assign(2 * yinW_ + 1, 0.f);
    dprime_.assign(yinW_ + 1, 1.f);
    fluxHist_.assign(16, 0.f);
    chromaFft_.init(yinFftN_);
    chromaWin_.resize(yinFftN_);
    for (int i = 0; i < yinFftN_; ++i)
        chromaWin_[i] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / yinFftN_);
    chRe_.assign(yinFftN_, 0.f); chIm_.assign(yinFftN_, 0.f); chMag_.assign(yinFftN_ / 2, 0.f);
    binPc_.assign(yinFftN_ / 2, -1);
    binW_.assign(yinFftN_ / 2, 0.f);
    for (int k = 1; k < yinFftN_ / 2; ++k) {
        const double f = k * sr_ / yinFftN_;
        if (f < 80.0) continue;
        const double m = 69.0 + 12.0 * std::log2(f / 440.0);
        const double r = std::round(m);
        binPc_[k] = (((int)r % 12) + 12) % 12;
        binW_[k] = (float)std::max(0.0, 1.0 - 2.0 * std::fabs(m - r)); // triangle: in tune counts most
    }
    setSettings(settings_);
    applyMode();
    reset();
}

void Analyzer::setSettings(const AnalyzerSettings& in) {
    AnalyzerSettings s = in;
    if (s.lowHz <= 0.f) s.lowHz = s.bright ? 100.f : 55.f;
    if (s.highHz <= 0.f) s.highHz = s.bright ? 10000.f : 2000.f;
    s.lowHz = std::clamp(s.lowHz, 30.f, 4000.f);
    s.highHz = std::clamp(s.highHz, 100.f, 16000.f);
    if (s.highHz < s.lowHz * 1.5f) s.highHz = s.lowHz * 1.5f;
    const bool changed = s.bright != settings_.bright || s.lowHz != settings_.lowHz || s.highHz != settings_.highHz;
    settings_ = s;
    if (changed) applyMode();
}

bool Analyzer::noteDeviation(float& semis) const {
    if (noteState_ != Active || !notePitched_ || lastConf_ < 0.8f || lastF0Midi_ <= 0.f) return false;
    semis = std::clamp(lastF0Midi_ - notePitch_, -12.f, 12.f);
    return true;
}

void Analyzer::applyMode() {
    modeBright_ = settings_.bright;
    const float lo = settings_.lowHz, hi = std::min<float>(settings_.highHz, (float)(sr_ * 0.4));
    // the pitch window must hold a couple of periods of the lowest note
    const int needW = (int)(sr_ / lo) + 4;
    if (!modeBright_) {
        wEff_ = yinW_;
        pitchEveryEff_ = 2;
        minIoiSec_ = 0.045f; legatoSemis_ = 0.8f; legatoFrames_ = 3;
        pitchWaitSec_ = 0.040f;
    } else {
        wEff_ = needW > yinW_ / 2 ? yinW_ : yinW_ / 2;
        pitchEveryEff_ = 1;
        minIoiSec_ = 0.015f; legatoSemis_ = 1.5f; legatoFrames_ = 2;
        pitchWaitSec_ = 0.020f;
    }
    tauMinEff_ = std::max(2, (int)(sr_ / hi));
    tauMaxEff_ = std::min(wEff_ - 2, (int)(sr_ / lo));
    maxMidi_ = midiFromHz(hi) + 0.5f;
    minMidi_ = midiFromHz(std::max(lo, (float)(sr_ / (wEff_ - 2)))) - 0.5f;
    // high-pass a little under the lowest note (RBJ cookbook, Q = 0.707): rumble, wind, handling
    auto design = [this](Biquad& f, float fc, bool high) {
        const double w = 2.0 * M_PI * fc / sr_, c = std::cos(w), al = std::sin(w) / (2.0 * 0.7071);
        const double a0 = 1.0 + al;
        const double b0 = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
        const double b1 = high ? -(1.0 + c) : 1.0 - c;
        f.b0 = (float)(b0 / a0); f.b1 = (float)(b1 / a0); f.b2 = (float)(b0 / a0);
        f.a1 = (float)(-2.0 * c / a0); f.a2 = (float)((1.0 - al) / a0);
    };
    design(hp_, lo * 0.7f, true);
    // (no low-pass: the attacks of the notes live in the highs)
    lpOn_ = false;
    tauMin_ = tauMinEff_;
    tauMax_ = tauMaxEff_;
    const double hz = modeBright_ ? 8000.0 : 5000.0;
    chromaLo_ = std::max(1, (int)(80.0 * yinFftN_ / sr_));
    chromaHi_ = std::min(yinFftN_ / 2 - 1, (int)(hz * yinFftN_ / sr_));
    const double dt = 4.0 * hop_ * pitchEveryEff_ / sr_;
    aChord_ = (float)std::exp(-dt / 1.0);
    aKey_ = (float)std::exp(-dt / 20.0);
}

// Harmonic colour: Hann-windowed spectrum (~90 ms), spectral peaks only, higher partials
// weighted down so that fundamentals dominate. Runs every 4th pitch frame.
void Analyzer::chromaAnalysis() {
    const int N = yinFftN_;
    for (int i = 0; i < N; ++i) {
        chRe_[i] = sampleAt(N - i) * chromaWin_[i];
        chIm_[i] = 0.f;
    }
    chromaFft_.transform(chRe_.data(), chIm_.data());
    float mx = 0.f;
    for (int k = chromaLo_ - 1; k <= chromaHi_ + 1; ++k) {
        chMag_[k] = std::sqrt(chRe_[k] * chRe_[k] + chIm_[k] * chIm_[k]);
        mx = std::max(mx, chMag_[k]);
    }
    // spectral peaks (strongest first would be nicer; in frequency order is enough here)
    struct Peak { float f, m; int pc; float bw; };
    Peak pk[96];
    int np = 0;
    if (mx > 1e-6f) {
        for (int k = chromaLo_; k <= chromaHi_ && np < 96; ++k) {
            const float m = chMag_[k];
            if (m < 0.02f * mx || m <= chMag_[k - 1] || m < chMag_[k + 1]) continue;
            if (binPc_[k] < 0) continue;
            // parabolic interpolation of the peak frequency
            const float a = chMag_[k - 1], c = chMag_[k + 1];
            const float den = a - 2.f * m + c;
            const float off = std::fabs(den) > 1e-12f ? std::clamp(0.5f * (a - c) / den, -0.5f, 0.5f) : 0.f;
            pk[np++] = {(float)((k + off) * sr_ / N), m, binPc_[k], binW_[k]};
        }
    }
    float fr[12] = {};
    float tot = 0.f;
    for (int i = 0; i < np; ++i) {
        float w = pk[i].m * pk[i].bw / (1.f + pk[i].f / 600.f);
        // a 3rd, 5th, 6th or 7th partial of a stronger lower peak is colour, not a note
        for (int j = 0; j < i; ++j) {
            if (pk[j].m < 0.3f * pk[i].m) continue;
            const float ratio = pk[i].f / pk[j].f;
            const float n = std::round(ratio);
            if ((n == 3.f || n == 5.f || n == 6.f || n == 7.f) && std::fabs(ratio - n) < 0.015f * n) {
                w *= 0.2f;
                break;
            }
        }
        fr[pk[i].pc] += w; tot += w;
    }
    const float w = levelDb_ > settings_.gateDb && tot > 1e-9f ? std::max(0.f, 1.f - 2.f * flatness_) : 0.f;
    for (int i = 0; i < 12; ++i) {
        const float v = w > 0.f ? w * fr[i] / tot : 0.f;
        chordC_[i] = aChord_ * chordC_[i] + v;
        keyC_[i] = aKey_ * keyC_[i] + v;
    }
    chordW_ = aChord_ * chordW_ + w;
    keyW_ = aKey_ * keyW_ + w;
}

float Analyzer::chroma(bool longTerm, float out[12]) const {
    const float* c = longTerm ? keyC_ : chordC_;
    float sum = 0.f;
    for (int i = 0; i < 12; ++i) sum += c[i];
    for (int i = 0; i < 12; ++i) out[i] = sum > 1e-9f ? c[i] / sum : 0.f;
    return longTerm ? keyW_ : chordW_;
}

void Analyzer::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.f);
    std::fill(prevLogMag_.begin(), prevLogMag_.end(), 0.f);
    std::fill(prev2LogMag_.begin(), prev2LogMag_.end(), 0.f);
    std::fill(fluxHist_.begin(), fluxHist_.end(), 0.f);
    hp_.z1 = hp_.z2 = lp_.z1 = lp_.z2 = 0.f;
    writePos_ = 0; written_ = 0; hopCount_ = 0; hopIndex_ = 0; fluxPos_ = 0;
    prevFlux_ = 0.f; lastOnset_ = -1000000;
    levelDb_ = prevLevelDb_ = -120.f;
    std::fill(slowHist_, slowHist_ + 8, -120.f);
    levelPos_ = 0; slowDb_ = -120.f; candTime_ = -1;
    std::fill(centHist_, centHist_ + 8, 0.f); centPos_ = 0; candCentroid_ = 0.f; centSmooth_ = 0.f;
    noteState_ = Idle; pendCount_ = 0; deviateCount_ = 0;
    lastActive_ = -1000000; numEvents_ = 0;
    std::fill(chordC_, chordC_ + 12, 0.f);
    std::fill(keyC_, keyC_ + 12, 0.f);
    chordW_ = keyW_ = 0.f;
}

float Analyzer::sampleAt(int back) const {
    return ring_[(writePos_ - back) & ringMask_];
}

void Analyzer::push(const HeardEvent& e) {
    if (numEvents_ < 64) events_[numEvents_++] = e;
}

void Analyzer::process(const float* in, int n, int64_t blockStart) {
    written_ = blockStart; // keep absolute time aligned with the host
    for (int i = 0; i < n; ++i) {
        float s = in[i];
        if (!std::isfinite(s)) s = 0.f;
        s = hp_.run(s);
        if (lpOn_) s = lp_.run(s);
        if (!std::isfinite(s)) { hp_.z1 = hp_.z2 = lp_.z1 = lp_.z2 = 0.f; s = 0.f; }
        ring_[writePos_] = s;
        writePos_ = (writePos_ + 1) & ringMask_;
        ++written_;
        if (++hopCount_ >= hop_) {
            hopCount_ = 0;
            analyseFrame(written_);
        }
    }
}

void Analyzer::onsetAnalysis(int64_t frameEnd, bool& onset) {
    const int N = onsetN_;
    for (int i = 0; i < N; ++i) {
        re_[i] = sampleAt(N - i) * win_[i];
        im_[i] = 0.f;
    }
    onsetFft_.transform(re_.data(), im_.data());
    const float norm = 4.f / N;
    float flux = 0.f, sumMag = 0.f, sumPow = 0.f, sumFMag = 0.f, sumLog = 0.f;
    const int bins = N / 2;
    for (int k = 1; k < bins; ++k) {
        float mag = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]) * norm;
        curLogMag_[k] = std::log1p(100.f * mag);
        sumMag += mag;
        sumPow += mag * mag;
        sumFMag += mag * mag * (float)(k * sr_ / N);
        sumLog += std::log(mag + 1e-9f);
    }
    // SuperFlux-style: compare with the max of neighbouring bins two frames back,
    // which ignores the frame-to-frame ripple of low notes and vibrato
    for (int k = 1; k < bins; ++k) {
        float ref = prev2LogMag_[k];
        if (k > 1) ref = std::max(ref, prev2LogMag_[k - 1]);
        if (k + 1 < bins) ref = std::max(ref, prev2LogMag_[k + 1]);
        float d = curLogMag_[k] - ref;
        if (d > 0.f) flux += d;
    }
    std::swap(prev2LogMag_, prevLogMag_);
    std::swap(prevLogMag_, curLogMag_);
    const int cnt = bins - 1;
    centroid_ = sumPow > 1e-12f ? sumFMag / sumPow : 0.f;
    const float arith = sumMag / cnt + 1e-9f;
    flatness_ = std::min(1.f, std::exp(sumLog / cnt) / arith);

    float mean = 0.f;
    for (float f : fluxHist_) mean += f;
    mean /= (float)fluxHist_.size();
    const float s = std::clamp(settings_.sensitivity, 0.f, 1.f);
    const float delta = 6.f - 5.2f * s;            // 6 .. 0.8
    const float mult = 1.8f - 0.6f * s;            // 1.8 .. 1.2
    const float thresh = mean * mult + delta;
    const int64_t minIoi = (int64_t)(minIoiSec_ * sr_);

    const bool loud = levelDb_ > settings_.gateDb;
    const bool fluxHit = flux > thresh && prevFlux_ <= thresh;
    const bool gateHit = prevLevelDb_ < settings_.gateDb && levelDb_ >= settings_.gateDb + 3.f;
    float minRecent = 1e9f;
    for (float l : slowHist_) minRecent = std::min(minRecent, l);
    const bool spaced = (frameEnd - lastOnset_) > minIoi;
    onset = false;
    if (noteState_ == Idle) {
        onset = loud && spaced && (fluxHit || gateHit);
        candTime_ = -1;
    } else {
        // while something sounds, a spectral change only counts as a new attack
        // if the (slow) level rises shortly after it
        float minCentroid = 1e9f;
        for (float c : centHist_) minCentroid = std::min(minCentroid, c);
        if (fluxHit && candTime_ < 0 && spaced) {
            candTime_ = frameEnd; candBase_ = minRecent; candHops_ = 0;
            candCentroid_ = minCentroid;
        }
        if (candTime_ >= 0) {
            // re-plucking the same note at the same loudness: no level rise, but the sound
            // gets brighter again (the high partials had faded). Vibrato does not do that.
            const bool brighter = flatness_ < 0.4f && candCentroid_ > 0.f && centSmooth_ > 1.35f * candCentroid_;
            if (slowDb_ - candBase_ > 2.f || brighter) { onset = loud; candOnsetTime_ = candTime_; candTime_ = -1; }
            else if (++candHops_ > 6) candTime_ = -1;
        }
    }
    if (onset) lastOnset_ = frameEnd;

    centSmooth_ += 0.4f * (centroid_ - centSmooth_);
    centHist_[centPos_] = centSmooth_;
    centPos_ = (centPos_ + 1) & 7;
    fluxHist_[fluxPos_] = flux;
    fluxPos_ = (fluxPos_ + 1) % (int)fluxHist_.size();
    prevFlux_ = flux;
}

void Analyzer::pitchAnalysis(float& midi, float& conf) {
    const int W = wEff_;
    const int L = W + tauMax_ + 1;        // samples used, newest first
    const int F = yinFftN_;
    for (int i = 0; i < F; ++i) {
        float v = i < L ? sampleAt(i + 1) : 0.f;
        xre_[i] = v; xim_[i] = 0.f;
        yre_[i] = i < W ? v : 0.f; yim_[i] = 0.f;
    }
    cumSq_[0] = 0.f;
    for (int i = 0; i < L; ++i) cumSq_[i + 1] = cumSq_[i] + xre_[i] * xre_[i];
    const float e1 = cumSq_[W];
    if (e1 < 1e-7f * W) { midi = 0.f; conf = 0.f; return; }

    yinFft_.transform(xre_.data(), xim_.data());

    yinFft_.transform(yre_.data(), yim_.data());
    for (int i = 0; i < F; ++i) {
        // X * conj(Y)
        const float a = xre_[i], b = xim_[i], c = yre_[i], d = yim_[i];
        xre_[i] = a * c + b * d;
        xim_[i] = b * c - a * d;
    }
    yinFft_.transform(xre_.data(), xim_.data(), true);
    const float invF = 1.f / F;

    float running = 0.f;
    dprime_[0] = 1.f;
    for (int tau = 1; tau <= tauMax_; ++tau) {
        const float r = xre_[tau] * invF;
        const float e2 = cumSq_[tau + W] - cumSq_[tau];
        const float d = std::max(0.f, e1 + e2 - 2.f * r);
        running += d;
        dprime_[tau] = running > 0.f ? d * tau / running : 1.f;
    }

    const float thr = 0.12f;
    int best = -1;
    for (int tau = tauMin_; tau <= tauMax_; ++tau) {
        if (dprime_[tau] < thr) {
            while (tau + 1 <= tauMax_ && dprime_[tau + 1] < dprime_[tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        float mn = 2.f;
        for (int tau = tauMin_; tau <= tauMax_; ++tau)
            if (dprime_[tau] < mn) { mn = dprime_[tau]; best = tau; }
    }
    if (best < 0) { midi = 0.f; conf = 0.f; return; }

    float t = (float)best;
    if (best > tauMin_ && best < tauMax_) {
        const float a = dprime_[best - 1], b = dprime_[best], c = dprime_[best + 1];
        const float den = a - 2.f * b + c;
        if (std::fabs(den) > 1e-9f) t += std::clamp(0.5f * (a - c) / den, -1.f, 1.f);
    }
    conf = std::clamp(1.f - dprime_[best], 0.f, 1.f);
    midi = midiFromHz((float)(sr_ / t));
}

void Analyzer::startPending(int64_t t) {
    noteState_ = Pending;
    pendStart_ = t;
    pendPeakDb_ = levelDb_;
    pendCount_ = 0;
    pendCentroidSum_ = 0.f; pendFlatSum_ = 0.f; pendSpecCount_ = 0;
}

static float velocityFromDb(float db) { return std::clamp((db + 60.f) / 60.f, 0.05f, 1.f); }

void Analyzer::commitPending(int64_t now, bool pitched, float pitch) {
    HeardEvent e;
    e.type = HeardEvent::Start;
    e.time = pendStart_;
    e.commitTime = now;
    e.velocity = velocityFromDb(pendPeakDb_);
    e.centroidHz = pendSpecCount_ ? pendCentroidSum_ / pendSpecCount_ : centroid_;
    e.flatness = pendSpecCount_ ? pendFlatSum_ / pendSpecCount_ : flatness_;
    e.pitched = pitched;
    if (pitched) e.pitch = pitch;
    else {
        // brightness -> pitch: 100 Hz .. 10 kHz spread over MIDI 24 .. 108
        const float x = std::log2(std::max(100.f, e.centroidHz) / 100.f) / std::log2(100.f);
        e.pitch = std::clamp(24.f + 84.f * x, 24.f, 108.f);
    }
    push(e);
    noteState_ = Active;
    notePitch_ = e.pitch;
    notePitched_ = pitched;
    notePeakDb_ = pendPeakDb_;
    deviateCount_ = 0;
}

void Analyzer::endNote(int64_t t) {
    HeardEvent e;
    e.type = HeardEvent::End;
    e.time = t; e.commitTime = t;
    e.pitch = notePitch_; e.pitched = notePitched_;
    push(e);
    noteState_ = Idle;
}

void Analyzer::analyseFrame(int64_t frameEnd) {
    // level over the last two hops
    float p = 0.f;
    const int m = 2 * hop_;
    for (int i = 1; i <= m; ++i) { float s = sampleAt(i); p += s * s; }
    prevLevelDb_ = levelDb_;
    levelDb_ = dbFromPower(p / m);
    {
        const int ms = std::min(4 * hop_ * 2, yinW_);
        float q = 0.f;
        for (int i = 1; i <= ms; ++i) { float s = sampleAt(i); q += s * s; }
        slowHist_[levelPos_] = slowDb_;
        levelPos_ = (levelPos_ + 1) & 7;
        slowDb_ = dbFromPower(q / ms);
    }

    bool onset = false;
    candOnsetTime_ = -1;
    onsetAnalysis(frameEnd, onset);

    float f0 = 0.f, conf = 0.f;
    bool havePitch = false;
    // while a new note waits for its pitch, look every hop: the answer comes sooner
    if (++hopIndex_ >= pitchEveryEff_ || noteState_ == Pending) {
        hopIndex_ = 0;
        pitchAnalysis(f0, conf);
        havePitch = true;
        if (++chromaCount_ >= 4) { chromaCount_ = 0; chromaAnalysis(); }
        lastF0Midi_ = f0; lastConf_ = conf;
    }
    const bool confident = havePitch && conf > 0.8f && f0 > minMidi_ && f0 < maxMidi_;
    const int64_t onsetTime = (candOnsetTime_ >= 0 ? candOnsetTime_ : frameEnd) - hop_;
    const int64_t pitchWait = (int64_t)(pitchWaitSec_ * sr_);
    const int64_t settle = wEff_ / 2;

    if (onset) {
        if (noteState_ == Pending) commitPending(frameEnd, false, 0.f);
        if (noteState_ == Active) endNote(onsetTime);
        startPending(onsetTime);
    }

    if (noteState_ != Idle && levelDb_ > settings_.gateDb) lastActive_ = frameEnd;

    if (noteState_ == Pending) {
        pendPeakDb_ = std::max(pendPeakDb_, levelDb_);
        pendCentroidSum_ += centroid_; pendFlatSum_ += flatness_; ++pendSpecCount_;
        if (confident && frameEnd >= pendStart_ + settle && pendCount_ < 16)
            pendPitches_[pendCount_++] = f0;
        bool done = false;
        if (pendCount_ >= 2 &&
            std::fabs(pendPitches_[pendCount_ - 1] - pendPitches_[pendCount_ - 2]) < 0.5f) {
            commitPending(frameEnd, true,
                          0.5f * (pendPitches_[pendCount_ - 1] + pendPitches_[pendCount_ - 2]));
            done = true;
        } else if (frameEnd - pendStart_ >= pitchWait + settle || levelDb_ < settings_.gateDb) {
            if (pendCount_ > 0) {
                float tmp[16];
                std::copy(pendPitches_, pendPitches_ + pendCount_, tmp);
                std::nth_element(tmp, tmp + pendCount_ / 2, tmp + pendCount_);
                commitPending(frameEnd, true, tmp[pendCount_ / 2]);
            } else {
                commitPending(frameEnd, false, 0.f);
            }
            done = true;
        }
        if (done && levelDb_ < settings_.gateDb) endNote(frameEnd);
        return;
    }

    if (noteState_ == Active) {
        notePeakDb_ = std::max(notePeakDb_, levelDb_);
        if (levelDb_ < settings_.gateDb || slowDb_ < notePeakDb_ - 35.f) {
            endNote(frameEnd);
            return;
        }
        // legato: a stable new pitch without a new attack
        if (notePitched_ && confident) {
            if (std::fabs(f0 - notePitch_) > legatoSemis_) {
                if (deviateCount_ > 0 && std::fabs(f0 - deviatePitch_) < 0.5f) {
                    if (++deviateCount_ >= legatoFrames_) {
                        endNote(deviateStart_);
                        startPending(deviateStart_);
                        pendPeakDb_ = levelDb_;
                        commitPending(frameEnd, true, f0);
                    }
                } else {
                    deviateCount_ = 1; deviatePitch_ = f0;
                    deviateStart_ = frameEnd - hop_ * pitchEveryEff_;
                }
            } else {
                deviateCount_ = 0;
            }
        }
    }
}

} // namespace lpfd
