// Conversations — minimal radix-2 complex FFT (no dependencies).
// init() allocates and must be called outside the audio thread.
#pragma once
#include <cmath>
#include <vector>

namespace lpfd {

class FFT {
public:
    void init(int n) {
        n_ = n;
        log2n_ = 0;
        while ((1 << log2n_) < n) ++log2n_;
        cosT_.resize(n / 2);
        sinT_.resize(n / 2);
        for (int i = 0; i < n / 2; ++i) {
            double a = -2.0 * M_PI * i / n;
            cosT_[i] = (float)std::cos(a);
            sinT_[i] = (float)std::sin(a);
        }
        rev_.resize(n);
        for (int i = 0; i < n; ++i) {
            int r = 0;
            for (int b = 0; b < log2n_; ++b)
                if (i & (1 << b)) r |= 1 << (log2n_ - 1 - b);
            rev_[i] = r;
        }
    }

    int size() const { return n_; }

    // In-place transform. inverse=true computes the unnormalised inverse.
    void transform(float* re, float* im, bool inverse = false) const {
        const int n = n_;
        for (int i = 0; i < n; ++i) {
            int j = rev_[i];
            if (j > i) {
                float t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        const float sgn = inverse ? -1.f : 1.f;
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int k = 0; k < half; ++k) {
                    const float wr = cosT_[k * step];
                    const float wi = sgn * sinT_[k * step];
                    const int a = i + k, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                }
            }
        }
    }

private:
    int n_ = 0, log2n_ = 0;
    std::vector<float> cosT_, sinT_;
    std::vector<int> rev_;
};

} // namespace lpfd
