#include "pulse.h"
#include <algorithm>
#include <cmath>

namespace lpfd {

void PulseTracker::reset() {
    head_ = n_ = 0;
    dirty_ = false;
    lastUpdate_ = -1000000000;
    period_ = 0.0; phase_ = 0.0; div_ = 2;
    conf_ = 0.f; swing_ = 0.f; pending_ = 0.0;
}

void PulseTracker::addOnset(int64_t t, float velocity) {
    if (n_ && t <= t_[(head_ + kMax - 1) % kMax] + (int64_t)(0.03 * sr_)) return;   // a chord, a flam
    t_[head_] = t;
    v_[head_] = std::clamp(velocity, 0.05f, 1.f);
    head_ = (head_ + 1) % kMax;
    n_ = std::min(n_ + 1, kMax);
    dirty_ = true;
}

int64_t PulseTracker::nextBeat(int64_t t) const {
    if (period_ <= 0.0) return t;
    const double k = std::ceil(((double)t - phase_) / period_ - 1e-9);
    return (int64_t)std::llround(phase_ + k * period_);
}

int64_t PulseTracker::slotIndex(int64_t origin, int64_t t) const {
    if (period_ <= 0.0) return 0;
    return (int64_t)std::llround((double)(t - origin) / slot());
}

int64_t PulseTracker::slotTime(int64_t origin, int64_t index) const {
    double t = (double)origin + (double)index * slot();
    if (div_ % 2 == 0 && (index % 2 + 2) % 2 == 1) t += swing_ * slot();
    return (int64_t)std::llround(t);
}

void PulseTracker::update(int64_t now) {
    // the pulse fades when you stop playing
    if (n_ && (double)(now - lastOnset()) > 6.0 * sr_) conf_ = std::max(0.f, conf_ - 0.002f);
    if (!dirty_ || n_ < 6 || now - lastUpdate_ < (int64_t)(0.08 * sr_)) return;
    dirty_ = false;
    lastUpdate_ = now;

    // the attacks of the last 12 s, oldest first, in seconds
    double ts[kMax];
    float ws[kMax];
    int m = 0;
    for (int i = 0; i < n_; ++i) {
        const int k = (head_ + kMax - n_ + i) % kMax;
        if ((double)(now - t_[k]) > 12.0 * sr_) continue;
        ts[m] = (double)t_[k] / sr_;
        ws[m] = v_[k];
        ++m;
    }
    if (m < 6) return;

    // --- beat period: which period explains the distances between your attacks
    // (IOI clustering, every pair of attacks up to 4.5 s apart)
    double cand[kCand], score[kCand];
    for (int c = 0; c < kCand; ++c) {
        cand[c] = 0.18 * std::pow(1.6 / 0.18, (double)c / (kCand - 1));
        score[c] = 0.0;
    }
    const double sig2 = 2.0 * 0.022 * 0.022;
    for (int i = 0; i < m; ++i)
        for (int j = i + 1; j < m; ++j) {
            const double dt = ts[j] - ts[i];
            if (dt > 4.5) break;
            const double w = 0.15 + ws[i] * ws[j];          // accents (loud pairs) say most
            for (int c = 0; c < kCand; ++c) {
                const double k = std::round(dt / cand[c]);
                if (k < 1.0 || k > 8.0) continue;
                const double e = dt - k * cand[c];
                score[c] += w * std::exp(-e * e / sig2) / (1.0 + 0.2 * (k - 1.0));
            }
        }
    // most music has its beat between 50 and 170 BPM
    int best = 0;
    double bestS = -1.0;
    for (int c = 0; c < kCand; ++c) {
        const double o = std::log2(cand[c] / 0.6);
        score[c] *= std::exp(-o * o / (2.0 * 0.8 * 0.8));
        if (score[c] > bestS) { bestS = score[c]; best = c; }
    }
    double P = cand[best];
    // a subdivision may have won: prefer the beat at 2x or 3x when it explains almost as much
    auto scoreAt = [&](double p) {
        int c = (int)std::lround(std::log(p / 0.18) / std::log(1.6 / 0.18) * (kCand - 1));
        if (c < 0 || c >= kCand) return 0.0;
        double s = 0.0;
        for (int d = -1; d <= 1; ++d) if (c + d >= 0 && c + d < kCand) s = std::max(s, score[c + d]);
        return s;
    };
    if (P < 0.34) {
        double pick = P, pickS = 0.5 * bestS;
        for (double mult : {2.0, 3.0, 4.0})
            if (P * mult <= 1.1 && scoreAt(P * mult) > pickS) { pickS = scoreAt(P * mult); pick = P * mult; }
        P = pick;
    }

    // --- division of the beat: 2, 3 or 4 (or 1): the finest grid that your attacks sit on
    // share of attacks within a few ms of a grid line, above what chance alone would give
    auto fit = [&](double grid, double ph) {
        const double tol = std::min(0.035, 0.25 * grid);
        int on = 0;
        for (int i = 0; i < m; ++i) {
            double x = std::fmod(ts[i] - ph, grid);
            if (x < 0) x += grid;
            if (std::min(x, grid - x) < tol) ++on;
        }
        const double chance = std::min(0.95, 2.0 * tol / grid);
        return ((double)on / m - chance) / (1.0 - chance);
    };
    // --- where the beats fall: the phase that puts most (and loudest) attacks on beats
    double ph = ts[m - 1], bestP = -1.0;
    for (int k = 0; k < 32; ++k) {
        const double cph = ts[m - 1] + P * k / 32.0;
        double s = 0.0;
        for (int i = 0; i < m; ++i) {
            double x = std::fmod(ts[i] - cph, P);
            if (x < 0) x += P;
            const double d = std::min(x, P - x) / P;
            s += (0.3 + ws[i]) * std::exp(-d * d / (2.0 * 0.05 * 0.05));
        }
        if (s > bestP) { bestP = s; ph = cph; }
    }
    int div = 1;
    double bestFit = fit(P, ph);
    for (int d : {2, 3, 4}) {
        const double f = fit(P / d, ph) - 0.03 * (d - 1);
        if (f > bestFit) { bestFit = f; div = d; }
    }
    // swing: offbeats (odd slots) played late, by how much (in slots)
    float sw = 0.f;
    if (div % 2 == 0) {
        const double sl = P / div;
        double sum = 0.0;
        int cnt = 0;
        for (int i = 0; i < m; ++i) {
            const double x = (ts[i] - ph) / sl;
            const double k = std::round(x);
            if (((int64_t)k % 2 + 2) % 2 == 1) { sum += x - k; ++cnt; }
        }
        if (cnt >= 3) sw = (float)std::clamp(sum / cnt, -0.4, 0.4);
        if (std::fabs(sw) < 0.08f) sw = 0.f;             // that is just your timing, not swing
    }
    // --- refine tempo and phase: least squares of your attacks against their grid lines
    // (the candidate grids above are coarse: a 2 % tempo error is 30 ms after a bar)
    double slot = P / div;
    for (int it = 0; it < 2; ++it) {
        double sk = 0, st = 0, skk = 0, skt = 0;
        int cnt = 0;
        for (int i = 0; i < m; ++i) {
            const double x = (ts[i] - ph) / slot;
            const double k = std::round(x - (div % 2 == 0 ? 0.5 * sw : 0.0));
            const bool odd = ((int64_t)k % 2 + 2) % 2 == 1 && div % 2 == 0;
            const double tt = ts[i] - (odd ? sw * slot : 0.0);
            if (std::fabs((tt - ph) / slot - k) > 0.25) continue;
            sk += k; st += tt; skk += k * k; skt += k * tt; ++cnt;
        }
        const double den = cnt * skk - sk * sk;
        if (cnt < 4 || std::fabs(den) < 1e-9) break;
        const double b1 = (cnt * skt - sk * st) / den;
        const double a1 = (st - b1 * sk) / cnt;
        if (b1 <= 0.0 || std::fabs(b1 / slot - 1.0) > 0.06) break;
        slot = b1; ph = a1;
    }
    P = slot * div;
    const double sl = slot;

    // --- how sure: share of attacks on the (swung) grid, above chance, and enough of them
    double onGrid = 0.0;
    const double tol = std::min(0.035, 0.25 * sl);
    for (int i = 0; i < m; ++i) {
        const double x = (ts[i] - ph) / sl;
        double d = 1.0;
        for (double k = std::floor(x) - 1.0; k <= std::ceil(x) + 1.0; k += 1.0) {
            const bool odd = ((int64_t)k % 2 + 2) % 2 == 1;
            d = std::min(d, std::fabs(x - (k + (odd && div % 2 == 0 ? sw : 0.0))));
        }
        if (d * sl < tol) onGrid += 1.0;
    }
    const double chance = std::min(0.95, 2.0 * tol / sl);
    const float c = (float)std::clamp((onGrid / m - chance) / (1.0 - chance), 0.0, 1.0) * (float)std::min(1.0, m / 8.0);

    // keep the tempo steady: small drift is followed, a jump must be confirmed
    const double oldP = period_ / sr_;
    double newP = P;
    if (oldP > 0.0 && conf_ > 0.3f) {
        const double ratio = std::fabs(std::log(P / oldP));
        if (ratio < 0.1) newP = 0.6 * oldP + 0.4 * P;
        else if (pending_ > 0.0 && std::fabs(std::log(P / pending_)) < 0.1) pending_ = 0.0;
        else { pending_ = P; newP = oldP; }
    }
    // the phase goes with the kept period: shift it to sit best on your attacks
    if (std::fabs(newP - P) > 1e-9) {
        const double nsl = newP / div;
        // anchor on the latest attack's grid line, then average the residuals
        double sum = 0.0;
        int cnt = 0;
        for (int i = 0; i < m; ++i) {
            const double x = (ts[i] - ph) / nsl;
            const double k = std::round(x);
            const bool odd = ((int64_t)k % 2 + 2) % 2 == 1 && div % 2 == 0;
            const double r = (x - k) - (odd ? sw : 0.0);
            if (std::fabs(r) < 0.3) { sum += r; ++cnt; }
        }
        if (cnt) ph += sum / cnt * nsl;
    }
    period_ = newP * sr_;
    phase_ = ph * sr_;
    div_ = div;
    swing_ = sw;
    conf_ = c;
}

} // namespace lpfd
