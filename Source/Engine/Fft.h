// Fft.h — a plain radix-2 complex FFT and a real-signal wrapper. Sizes are
// powers of two up to 16384. No JUCE, so the spectral modules and the
// tests share one implementation.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace planish
{
class Fft
{
public:
    using cpx = std::complex<float>;

    void prepare (int size)
    {
        n = size;
        log2n = 0;
        while ((1 << log2n) < n) ++log2n;
        twiddle.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
            twiddle[(size_t) k] = std::polar (1.0f, (float) (-2.0 * 3.14159265358979323846 * k / n));
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < log2n; ++b) if (i & (1 << b)) r |= 1 << (log2n - 1 - b);
            rev[(size_t) i] = r;
        }
        work.resize ((size_t) n);
    }
    int size() const noexcept { return n; }

    // In place. inverse = true divides by n.
    void transform (cpx* x, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
            if (rev[(size_t) i] > i) std::swap (x[i], x[rev[(size_t) i]]);
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    cpx w = twiddle[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const cpx u = x[i + j], v = x[i + j + half] * w;
                    x[i + j] = u + v;
                    x[i + j + half] = u - v;
                }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) x[i] *= s;
        }
    }

    // Real forward: n real samples -> n/2+1 bins.
    void forwardReal (const float* in, cpx* bins)
    {
        for (int i = 0; i < n; ++i) work[(size_t) i] = cpx (in[i], 0.0f);
        transform (work.data(), false);
        for (int k = 0; k <= n / 2; ++k) bins[k] = work[(size_t) k];
    }
    // Real inverse: n/2+1 bins -> n real samples.
    void inverseReal (const cpx* bins, float* out)
    {
        for (int k = 0; k <= n / 2; ++k) work[(size_t) k] = bins[k];
        for (int k = 1; k < n / 2; ++k) work[(size_t) (n - k)] = std::conj (bins[k]);
        transform (work.data(), true);
        for (int i = 0; i < n; ++i) out[i] = work[(size_t) i].real();
    }

private:
    int n = 0, log2n = 0;
    std::vector<cpx> twiddle, work;
    std::vector<int> rev;
};
} // namespace planish
