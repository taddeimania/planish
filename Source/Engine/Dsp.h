// Dsp.h — building blocks for Planish's modules: biquads and filter
// cascades, smoothers, delays, envelope followers, a Linkwitz-Riley
// crossover, mid/side helpers, oversampling and the 4x true-peak
// interpolator. No JUCE, no allocation after prepare().
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace planish
{
constexpr double kPi = 3.14159265358979323846;
constexpr float  kPif = 3.14159265358979323846f;

inline float clampf (float x, float lo, float hi) { return std::min (hi, std::max (lo, x)); }
inline float dbToGain (float db)                  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)                   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float lerp (float a, float b, float t)     { return a + (b - a) * t; }

// Time constant to a one-pole coefficient, per sample.
inline float tauCoef (float ms, float sr) { return ms <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (ms * 0.001f * sr)); }

// Fast-ish tanh: accurate to ~1e-4, no NaN for large inputs.
inline float softTanh (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2))) / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

// ---------------------------------------------------------------------------
// Biquad, transposed direct form II with double state. RBJ cookbook.
// ---------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    float tick (float xf)
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
        a1 = na1 / na0; a2 = na2 / na0;
    }
    void flat() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    static double clampHz (double hz, double sr) { return std::min (std::max (hz, 1.0), sr * 0.49); }

    void lowPass (double hz, double q, double sr)
    {
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double hz, double q, double sr)
    {
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandPass (double hz, double q, double sr)   // constant 0 dB peak gain
    {
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void allPass (double hz, double q, double sr)
    {
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double hz, double q, double db, double sr)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * std::max (0.05, q));
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double hz, double db, double sr, double q = 0.7071)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
             (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    void highShelf (double hz, double db, double sr, double q = 0.7071)
    {
        const double A = std::pow (10.0, db / 40.0);
        const double w = 2 * kPi * clampHz (hz, sr) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double s = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
             (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    // First-order shelves: the gentle Baxandall-style tone controls.
    void lowShelf1 (double hz, double db, double sr)
    {
        const double A = std::pow (10.0, db / 20.0);
        const double w = std::tan (kPi * clampHz (hz, sr) / sr);
        const double sa = std::sqrt (A);
        // H(s) = (s + A*w') / (s + w') with w' = w/sqrt(A) for symmetric shelving
        const double wl = w / sa, wh = w * sa;
        set (wh + 1, wh - 1, 0, wl + 1, wl - 1, 0);
    }
    void highShelf1 (double hz, double db, double sr)
    {
        // H(s) = A (s + w/sqrt(A)) / (s + w*sqrt(A)): unity at DC, A at HF.
        const double A = std::pow (10.0, db / 20.0);
        const double w = std::tan (kPi * clampHz (hz, sr) / sr);
        const double sa = std::sqrt (A);
        const double wl = w / sa, wh = w * sa;
        set (A * (1 + wl), A * (wl - 1), 0, 1 + wh, wh - 1, 0);
    }
    void lowPass1 (double hz, double sr)
    {
        const double w = std::tan (kPi * clampHz (hz, sr) / sr);
        set (w, w, 0, w + 1, w - 1, 0);
    }
    void highPass1 (double hz, double sr)
    {
        const double w = std::tan (kPi * clampHz (hz, sr) / sr);
        set (1, -1, 0, w + 1, w - 1, 0);
    }

    // Complex response at hz: returns magnitude^2 and phase.
    void response (double hz, double sr, double& mag2, double& phase) const
    {
        const double w = 2 * kPi * hz / sr;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        const double den = std::max (1e-30, dr * dr + di * di);
        mag2 = (nr * nr + ni * ni) / den;
        // (n / d): phase = arg(n) - arg(d)
        phase = std::atan2 (ni, nr) - std::atan2 (di, dr);
    }
    double responseDb (double hz, double sr) const
    {
        double m, p;
        response (hz, sr, m, p);
        return 10 * std::log10 (std::max (1e-30, m));
    }
};

// Butterworth Q values for cascaded biquads of a given order.
inline void butterworthQs (int order, double* qs, int& numStages, bool& hasFirstOrder)
{
    numStages = order / 2;
    hasFirstOrder = (order % 2) == 1;
    for (int k = 0; k < numStages; ++k)
        qs[k] = 1.0 / (2.0 * std::sin (kPi * (2.0 * k + 1.0) / (2.0 * order)));
}

// A cascade of biquads: up to 16th order high/low pass.
struct FilterCascade
{
    static constexpr int kMax = 8;
    std::array<Biquad, kMax> s;
    Biquad first;
    int stages = 0;
    bool useFirst = false;

    void reset() { for (auto& b : s) b.reset(); first.reset(); }
    void flat() { stages = 0; useFirst = false; }

    // order 1..16
    void highPass (double hz, double sr, int order, double resonanceQ = -1.0)
    {
        double qs[kMax] {}; butterworthQs (std::min (2 * kMax, std::max (1, order)), qs, stages, useFirst);
        for (int k = 0; k < stages; ++k)
            s[(size_t) k].highPass (hz, (resonanceQ > 0 && k == stages - 1) ? resonanceQ : qs[k], sr);
        if (useFirst) first.highPass1 (hz, sr);
    }
    void lowPass (double hz, double sr, int order, double resonanceQ = -1.0)
    {
        double qs[kMax] {}; butterworthQs (std::min (2 * kMax, std::max (1, order)), qs, stages, useFirst);
        for (int k = 0; k < stages; ++k)
            s[(size_t) k].lowPass (hz, (resonanceQ > 0 && k == stages - 1) ? resonanceQ : qs[k], sr);
        if (useFirst) first.lowPass1 (hz, sr);
    }
    float tick (float x)
    {
        for (int k = 0; k < stages; ++k) x = s[(size_t) k].tick (x);
        if (useFirst) x = first.tick (x);
        return x;
    }
    double responseDb (double hz, double sr) const
    {
        double d = 0;
        for (int k = 0; k < stages; ++k) d += s[(size_t) k].responseDb (hz, sr);
        if (useFirst) d += first.responseDb (hz, sr);
        return d;
    }
    void response (double hz, double sr, double& mag2, double& phase) const
    {
        mag2 = 1; phase = 0;
        for (int k = 0; k < stages; ++k) { double m, p; s[(size_t) k].response (hz, sr, m, p); mag2 *= m; phase += p; }
        if (useFirst) { double m, p; first.response (hz, sr, m, p); mag2 *= m; phase += p; }
    }
};

// ---------------------------------------------------------------------------
// One-pole smoother.
// ---------------------------------------------------------------------------
struct Smoother
{
    float value = 0.0f, target = 0.0f, coeff = 0.01f;
    void setTime (float ms, float sr, int step = 1)
    {
        coeff = 1.0f - std::exp (-(float) step / std::max (1.0f, ms * 0.001f * sr));
    }
    void snap (float v) { value = target = v; }
    float next()        { value += coeff * (target - value); return value; }
    bool settled() const { return std::fabs (target - value) < 1e-6f; }
};

// ---------------------------------------------------------------------------
// Fixed integer delay, up to a maximum set at prepare().
// ---------------------------------------------------------------------------
class Delay
{
public:
    void prepare (int maxDelay)
    {
        cap = std::max (1, maxDelay) + 1;
        buf.assign ((size_t) cap, 0.0f);
        w = 0;
        delay = std::min (delay, cap - 1);
    }
    void setDelay (int d) { delay = std::min (std::max (0, d), cap - 1); }
    int  getDelay() const noexcept { return delay; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    float tick (float x)
    {
        buf[(size_t) w] = x;
        const int r = (w - delay + cap) % cap;
        w = (w + 1) % cap;
        return buf[(size_t) r];
    }
    // Fractional read, for the Haas stereoizer.
    float readFrac (float d) const
    {
        d = clampf (d, 0.0f, (float) (cap - 2));
        const int i = (int) d;
        const float f = d - (float) i;
        const int r0 = (w - 1 - i + 2 * cap) % cap, r1 = (r0 - 1 + cap) % cap;
        return buf[(size_t) r0] + f * (buf[(size_t) r1] - buf[(size_t) r0]);
    }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) % cap; }

private:
    std::vector<float> buf;
    int cap = 2, w = 0, delay = 0;
};

// ---------------------------------------------------------------------------
// Envelope follower: peak (instant attack), or RMS-ish.
// ---------------------------------------------------------------------------
struct EnvFollower
{
    float env = 0.0f, att = 1.0f, rel = 0.001f;
    void set (float attackMs, float releaseMs, float sr) { att = tauCoef (attackMs, sr); rel = tauCoef (releaseMs, sr); }
    float next (float x)   // x already rectified or squared
    {
        env += (x > env ? att : rel) * (x - env);
        return env;
    }
    void reset() { env = 0.0f; }
};

// ---------------------------------------------------------------------------
// Linkwitz-Riley 4th order crossover: one split into low and high that sum
// flat (in magnitude, with an all-pass phase).
// ---------------------------------------------------------------------------
struct LR4Split
{
    Biquad lp1, lp2, hp1, hp2;
    void set (double hz, double sr)
    {
        lp1.lowPass (hz, 0.7071, sr); lp2 = lp1; lp2.reset();
        hp1.highPass (hz, 0.7071, sr); hp2 = hp1; hp2.reset();
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void tick (float x, float& lo, float& hi)
    {
        lo = lp2.tick (lp1.tick (x));
        hi = hp2.tick (hp1.tick (x));
    }
};

// The all-pass with the same phase as an LR4 split at hz, to keep already
// split bands aligned with later splits.
struct LR4AllPass
{
    Biquad a1, a2;
    void set (double hz, double sr) { a1.allPass (hz, 0.7071, sr); a2 = a1; a2.reset(); }
    void reset() { a1.reset(); a2.reset(); }
    float tick (float x) { return a2.tick (a1.tick (x)); }
};

// Up to four bands from three crossovers. Bands sum back to the input
// (through the all-pass phase of the crossovers).
class Crossover4
{
public:
    static constexpr int kMaxBands = 4;

    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset()
    {
        for (auto& s : split) s.reset();
        for (auto& row : ap) for (auto& a : row) a.reset();
    }

    // numBands 1..4, freqs ascending; only the first numBands-1 are used.
    void set (int numBands, const float* freqs)
    {
        bands = std::min (kMaxBands, std::max (1, numBands));
        double last = 20.0;
        for (int i = 0; i < bands - 1; ++i)
        {
            const double hz = std::max (last * 1.05, std::min ((double) freqs[i], sr * 0.45));
            last = hz;
            split[(size_t) i].set (hz, sr);
            for (int j = 0; j < i; ++j)
                ap[(size_t) i][(size_t) j].set (hz, sr);
        }
    }
    int numBands() const noexcept { return bands; }

    void tick (float x, float* out)
    {
        if (bands == 1) { out[0] = x; return; }
        float rest = x;
        for (int i = 0; i < bands - 1; ++i)
        {
            float lo, hi;
            split[(size_t) i].tick (rest, lo, hi);
            out[i] = lo;
            rest = hi;
            // Earlier bands pass through this crossover's all-pass so they stay
            // phase-aligned with the bands split after them.
            for (int j = 0; j < i; ++j)
                out[j] = ap[(size_t) i][(size_t) j].tick (out[j]);
        }
        out[bands - 1] = rest;
    }

private:
    double sr = 48000.0;
    int bands = 1;
    std::array<LR4Split, 3> split;
    std::array<std::array<LR4AllPass, 3>, 3> ap;   // [crossover][band]
};

// ---------------------------------------------------------------------------
// Mid/side.
// ---------------------------------------------------------------------------
inline void toMidSide (float l, float r, float& m, float& s) { m = 0.5f * (l + r); s = 0.5f * (l - r); }
inline void toLeftRight (float m, float s, float& l, float& r) { l = m + s; r = m - s; }

// ---------------------------------------------------------------------------
// Half-band FIR oversampler stage (2x), Kaiser-windowed: ~80 dB stopband.
// Its delay is (Taps - 1) / 2 samples at the higher rate, each direction.
// ---------------------------------------------------------------------------
template <int Taps>
class HalfBandT
{
public:
    static constexpr int kTaps = Taps;
    HalfBandT() { design(); }
    void reset() { std::fill (std::begin (up), std::end (up), 0.0f); std::fill (std::begin (dn), std::end (dn), 0.0f); upPos = dnPos = 0; }

    // One input sample -> two output samples (interpolated, at 2x).
    void upsample (float x, float& y0, float& y1)
    {
        push (up, upPos, 2.0f * x);
        y0 = convolve (up + upPos);
        push (up, upPos, 0.0f);
        y1 = convolve (up + upPos);
    }
    // Two input samples (at 2x) -> one output sample, kept on the phase of
    // the first so a round trip is exactly kTaps - 1 samples at the higher rate.
    float downsample (float x0, float x1)
    {
        push (dn, dnPos, x0);
        const float y = convolve (dn + dnPos);
        push (dn, dnPos, x1);
        return y;
    }
    static constexpr int latency() { return (kTaps - 1) / 2; }   // at the higher rate, each direction

private:
    // History kept twice over so a window is always contiguous.
    static void push (float* buf, int& pos, float x) { pos = (pos + kTaps - 1) % kTaps; buf[pos] = buf[pos + kTaps] = x; }
    float convolve (const float* x) const
    {
        float acc = 0.0f;
        for (int k = 0; k < numNz; ++k) acc += hz[k] * x[nzIdx[k]];
        return acc;
    }
    void design()
    {
        const double beta = 6.0;
        auto bessel0 = [] (double x) { double sum = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; } return sum; };
        const double mid = (kTaps - 1) / 2.0;
        double h[kTaps] {};
        double sum = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double t = n - mid;
            const double sinc = t == 0 ? 0.5 : std::sin (kPi * 0.5 * t) / (kPi * t);
            const double r = t / mid;
            const double win = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
            h[n] = sinc * win;
            sum += h[n];
        }
        // A half-band filter is zero at every other tap: keep only the rest.
        numNz = 0;
        for (int n = 0; n < kTaps; ++n)
            if (std::fabs (h[n]) > 1.0e-12)
            {
                hz[numNz] = (float) (h[n] / sum);
                nzIdx[numNz] = n;
                ++numNz;
            }
    }
    float hz[kTaps] {};
    int nzIdx[kTaps] {};
    int numNz = 0;
    float up[2 * kTaps] {}, dn[2 * kTaps] {};
    int upPos = 0, dnPos = 0;
};
using HalfBand = HalfBandT<31>;

// 4x oversampling for a per-sample nonlinearity, from a 31-tap stage at 2x
// and a 33-tap stage at 4x, so the round trip is a whole number of
// samples at the base rate: 15 + 8 = 23.
class Oversampler4x
{
public:
    static constexpr int kLatency = HalfBandT<31>::latency() + (2 * HalfBandT<33>::latency()) / 4;
    void reset() { s1.reset(); s2.reset(); }
    template <typename Fn>
    float process (float x, Fn&& fn)
    {
        float a0, a1, b0, b1, b2, b3;
        s1.upsample (x, a0, a1);
        s2.upsample (a0, b0, b1);
        s2.upsample (a1, b2, b3);
        const float c0 = s2.downsample (fn (b0), fn (b1));
        const float c1 = s2.downsample (fn (b2), fn (b3));
        return s1.downsample (c0, c1);
    }
private:
    HalfBandT<31> s1;
    HalfBandT<33> s2;
};

// ---------------------------------------------------------------------------
// 4x interpolator for true-peak detection (ITU-R BS.1770 annex 2 style):
// a 49-tap Kaiser-windowed sinc, run as four polyphase branches. Its group
// delay is exactly 6 input samples.
// ---------------------------------------------------------------------------
class TruePeak4x
{
public:
    static constexpr int kTaps = 49;
    static constexpr int kHist = 13;     // ceil(49 / 4)
    static constexpr int kDelay = 6;

    TruePeak4x() { design(); }
    void reset() { std::fill (std::begin (hist), std::end (hist), 0.0f); pos = 0; }

    // Pushes one sample and returns the largest absolute value of the four
    // interpolated points it completes.
    float push (float x)
    {
        pos = (pos + kHist - 1) % kHist;
        hist[pos] = hist[pos + kHist] = x;
        const float* hx = hist + pos;
        float m = 0.0f;
        for (int p = 0; p < 4; ++p)
        {
            float acc = 0.0f;
            const float* h = phase[p];
            for (int k = 0; k < kHist; ++k)
                acc += h[k] * hx[k];
            m = std::max (m, std::fabs (acc));
        }
        return m;
    }

private:
    void design()
    {
        const double beta = 7.0, fc = 0.5 * 0.9 / 4.0;
        auto bessel0 = [] (double x) { double sum = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; } return sum; };
        double h[kTaps];
        const double mid = (kTaps - 1) / 2.0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double t = n - mid;
            const double sinc = 2 * n == kTaps - 1 ? 2 * fc : std::sin (2 * kPi * fc * t) / (kPi * t);
            const double r = t / mid;
            const double win = bessel0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / bessel0 (beta);
            h[n] = sinc * win;
        }
        for (int p = 0; p < 4; ++p)
        {
            double s = 0;
            for (int k = 0; k < kHist; ++k)
                if (p + 4 * k < kTaps) s += h[p + 4 * k];
            for (int k = 0; k < kHist; ++k)
                phase[p][k] = (p + 4 * k < kTaps) ? (float) (h[p + 4 * k] / s) : 0.0f;
        }
    }
    float phase[4][kHist] {};
    float hist[2 * kHist] {};
    int pos = 0;
};

// ---------------------------------------------------------------------------
// Transient/sustain split: a fast and a slow envelope on the signal; the
// ratio marks transients.
// ---------------------------------------------------------------------------
struct TransientDetector
{
    EnvFollower fast, slow;
    void prepare (float sr) { fast.set (0.5f, 30.0f, sr); slow.set (20.0f, 200.0f, sr); }
    void reset() { fast.reset(); slow.reset(); }
    // Returns a 0..1 "transientness" for the rectified input.
    float next (float ax)
    {
        const float f = fast.next (ax), s = slow.next (ax);
        const float r = f / (s + 1e-6f);
        return clampf ((r - 1.2f) / 2.0f, 0.0f, 1.0f);
    }
};

// Simple xorshift noise, for dither and the stereoizer.
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    float next()   // uniform -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s & 0xffffff) / 8388608.0f - 1.0f;
    }
};
} // namespace planish
