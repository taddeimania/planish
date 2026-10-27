// OutputLimiter.h — the LIMITER at the end of the I/O panel: a true-peak
// limiter after the output gain, with a CEILING from 0 to -20 dB, three
// styles (CLEAR fast, SMOOTH the middle, THICK slow) and four modes:
//
//   IRC II   look-ahead, true peak, adaptive release      (2.5 ms)
//   IRC LL   short look-ahead, true peak                  (1 ms)
//   HARD     look-ahead on sample peaks, then a hard clip (4 ms)
//   ZERO-L   no look-ahead: an instant gain computer and a safety clip
#pragma once

#include "Limiter.h"

namespace planish
{
class OutputLimiter
{
public:
    enum Mode { Irc2 = 0, IrcLL, Hard, ZeroL };
    enum Style { Clear = 0, Smooth, Thick };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lim.prepare (sr, std::max (4, (int) std::lround (0.004 * sr)));
        lastMode = -1;
        reset();
    }
    void reset() { lim.reset(); zlGain = 1.0f; grDb = 0.0f; sustain = 0.0f; }

    int latencyFor (int mode) const { return mode == ZeroL ? 0 : windowFor (mode) + TruePeak4x::kDelay; }

    // In place. Returns nothing; grDb holds the block's deepest reduction.
    void process (float* l, float* r, int n, int mode, int style, float ceilingDb)
    {
        if (mode != lastMode)
        {
            lastMode = mode;
            if (mode != ZeroL) lim.setWindow (windowFor (mode));
            lim.setTruePeak (mode == Irc2 || mode == IrcLL);
        }
        const float ceiling = dbToGain (ceilingDb);
        const float baseMs = style == Clear ? 15.0f : style == Smooth ? 60.0f : 200.0f;
        float deepest = 0.0f;
        if (mode == ZeroL)
        {
            const float rel = tauCoef (baseMs, (float) sr);
            for (int i = 0; i < n; ++i)
            {
                const float pk = std::max (std::fabs (l[i]), std::fabs (r[i]));
                const float want = pk > ceiling ? ceiling / pk : 1.0f;
                if (want < zlGain) zlGain = want; else zlGain += rel * (want - zlGain);
                l[i] = clampf (l[i] * zlGain, -ceiling, ceiling);
                r[i] = clampf (r[i] * zlGain, -ceiling, ceiling);
                deepest = std::min (deepest, gainToDb (zlGain));
            }
        }
        else
        {
            const float fast = tauCoef (baseMs * 0.4f, (float) sr), slow = tauCoef (baseMs * 3.0f, (float) sr);
            for (int i = 0; i < n; ++i)
            {
                if (mode == Irc2)
                {
                    // Release: fast for brief peaks, slow while limiting persists.
                    const bool limiting = lim.gainReductionDb() < -0.1f;
                    sustain += 0.0005f * ((limiting ? 1.0f : 0.0f) - sustain);
                    lim.setReleaseCoefficient (lerp (fast, slow, clampf (sustain * 1.5f, 0.0f, 1.0f)));
                }
                else
                    lim.setReleaseCoefficient (tauCoef (baseMs, (float) sr));
                float a = l[i], b = r[i];
                lim.tick (a, b, ceiling, 0.0f);
                if (mode == Hard) { a = clampf (a, -ceiling, ceiling); b = clampf (b, -ceiling, ceiling); }
                l[i] = a; r[i] = b;
                deepest = std::min (deepest, lim.gainReductionDb());
            }
        }
        grDb = deepest;
    }
    // Keeps the delay line moving (and the latency) while switched off.
    void passThrough (float* l, float* r, int n, int mode)
    {
        if (mode == ZeroL) { grDb = 0.0f; return; }
        if (mode != lastMode) { lastMode = mode; lim.setWindow (windowFor (mode)); }
        for (int i = 0; i < n; ++i) lim.tick (l[i], r[i], 1.0e9f, 0.0f);
        grDb = 0.0f;
    }

    float grDb = 0.0f;

private:
    int windowFor (int mode) const
    {
        const double ms = mode == Irc2 ? 2.5 : mode == IrcLL ? 1.0 : 4.0;
        return std::max (4, (int) std::lround (ms * 0.001 * sr));
    }
    double sr = 48000.0;
    StereoLimiter lim;
    int lastMode = -1;
    float zlGain = 1.0f, sustain = 0.0f;
};
} // namespace planish
