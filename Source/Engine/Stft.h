// Stft.h — overlap-add short-time Fourier transform for the spectral
// modules (Sculptor, Unmask and Phase), up to four lanes. Square-root Hann on both sides; a callback edits the
// bins of each frame. Latency is one frame.
#pragma once

#include "Fft.h"

#include <array>
#include <functional>
#include <vector>

namespace planish
{
class Stft
{
public:
    using cpx = Fft::cpx;
    static constexpr int kMaxChannels = 4;

    void prepare (int frameSize, int hopSize, int channels)
    {
        frame = frameSize;
        hop = hopSize;
        chans = std::min (kMaxChannels, std::max (1, channels));
        fft.prepare (frame);
        window.resize ((size_t) frame);
        for (int i = 0; i < frame; ++i)
            window[(size_t) i] = std::sqrt (0.5f * (1.0f - std::cos (2.0f * 3.14159265f * (float) i / (float) frame)));
        // Normalise so that overlapping analysis*synthesis windows sum to 1.
        float sum = 0.0f;
        for (int k = 0; k * hop < frame; ++k)
            sum += window[(size_t) (k * hop)] * window[(size_t) (k * hop)];
        norm = 1.0f / sum;
        for (int c = 0; c < kMaxChannels; ++c)
        {
            in[(size_t) c].assign ((size_t) frame, 0.0f);
            out[(size_t) c].assign ((size_t) frame * 2, 0.0f);
            bins[(size_t) c].assign ((size_t) frame / 2 + 1, cpx (0.0f, 0.0f));
        }
        tmp.assign ((size_t) frame, 0.0f);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            std::fill (in[(size_t) c].begin(), in[(size_t) c].end(), 0.0f);
            std::fill (out[(size_t) c].begin(), out[(size_t) c].end(), 0.0f);
        }
        inPos = 0;
        sinceHop = 0;
        t = 0;
    }

    int latency() const noexcept { return frame; }
    int frameSize() const noexcept { return frame; }
    int numBins() const noexcept { return frame / 2 + 1; }
    int hopSize() const noexcept { return hop; }

    // Processes `n` samples of up to two channels in place. `perFrame` is
    // called with the bins of all channels (bins[c][k]) whenever a frame
    // completes; it may edit them in place.
    template <typename Fn>
    void process (float* const* data, int n, Fn&& perFrame)
    {
        const int out2 = frame * 2;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < chans; ++c)
                in[(size_t) c][(size_t) inPos] = data[c][i];
            inPos = (inPos + 1) % frame;
            ++t;

            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                // Analysis: the last `frame` samples, oldest first, windowed.
                for (int c = 0; c < chans; ++c)
                {
                    for (int k = 0; k < frame; ++k)
                        tmp[(size_t) k] = in[(size_t) c][(size_t) ((inPos + k) % frame)] * window[(size_t) k];
                    fft.forwardReal (tmp.data(), bins[(size_t) c].data());
                }
                cpx* b[kMaxChannels] { bins[0].data(), bins[1].data(), bins[2].data(), bins[3].data() };
                perFrame (b, chans, numBins());
                // Synthesis: overlap-add into positions t-frame .. t-1.
                for (int c = 0; c < chans; ++c)
                {
                    fft.inverseReal (bins[(size_t) c].data(), tmp.data());
                    const long start = t - frame + out2 * 2;   // kept positive for the modulo
                    for (int k = 0; k < frame; ++k)
                        out[(size_t) c][(size_t) ((start + k) % out2)] += tmp[(size_t) k] * window[(size_t) k] * norm;
                }
            }

            // Emit the sample from `frame` ago, now final.
            const size_t e = (size_t) ((t - 1 - frame + out2 * 4) % out2);
            for (int c = 0; c < chans; ++c)
            {
                data[c][i] = out[(size_t) c][e];
                out[(size_t) c][e] = 0.0f;
            }
        }
    }

    // Frequency of bin k.
    static float binHz (int k, int frameSize, double sr) { return (float) (k * sr / frameSize); }

private:
    Fft fft;
    int frame = 2048, hop = 512, chans = 2;
    int inPos = 0, sinceHop = 0;
    long t = 0;
    float norm = 1.0f;
    std::vector<float> window, tmp;
    std::array<std::vector<float>, kMaxChannels> in, out;
    std::array<std::vector<cpx>, kMaxChannels> bins;
};

// ---------------------------------------------------------------------------
// Log-spaced band energies from a spectrum, for the adaptive modules and
// the analyser: `bandsPerOctave` bands from 20 Hz to 20 kHz.
// ---------------------------------------------------------------------------
struct LogBands
{
    std::vector<float> centreHz;      // per band
    std::vector<int> firstBin, lastBin;
    int count = 0;

    void prepare (int bandsPerOctave, int frameSize, double sr)
    {
        centreHz.clear(); firstBin.clear(); lastBin.clear();
        const double ratio = std::pow (2.0, 1.0 / bandsPerOctave);
        const double half = std::sqrt (ratio);
        for (double f = 20.0; f < 20480.0 * 1.001; f *= ratio)
        {
            int lo = (int) std::ceil (f / half * frameSize / sr);
            int hi = (int) std::floor (f * half * frameSize / sr);
            lo = std::max (1, lo);
            hi = std::min (frameSize / 2, std::max (lo, hi));
            if (lo > hi) lo = hi;   // above Nyquist: an empty band at the top bin
            centreHz.push_back ((float) f);
            firstBin.push_back (lo);
            lastBin.push_back (hi);
        }
        count = (int) centreHz.size();
    }

    // Mean power (|X|^2) in each band.
    void energies (const Fft::cpx* bins, float* out) const
    {
        for (int b = 0; b < count; ++b)
        {
            double sum = 0;
            for (int k = firstBin[(size_t) b]; k <= lastBin[(size_t) b]; ++k)
                sum += std::norm (bins[k]);
            out[b] = (float) (sum / std::max (1, lastBin[(size_t) b] - firstBin[(size_t) b] + 1));
        }
    }

    // Which band a bin belongs to (-1 outside 20 Hz .. 20 kHz).
    int bandOfBin (int k) const
    {
        for (int b = 0; b < count; ++b)
            if (k >= firstBin[(size_t) b] && k <= lastBin[(size_t) b]) return b;
        return -1;
    }
};
} // namespace planish
