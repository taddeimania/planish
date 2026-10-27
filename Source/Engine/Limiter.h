// Limiter.h — the look-ahead peak limiter core of the output LIMITER and
// the Clipper's safety stage. One stage finds the
// gain every sample needs (true peak or sample peak against a ceiling), a
// moving minimum over the look-ahead window makes it reach the peak in
// time, a release lets it recover, and a box smoothing over the same
// window keeps the gain curve free of corners. Per channel, so callers can
// link or unlink the sides as they like.
#pragma once

#include "Dsp.h"

#include <vector>

namespace planish
{
class GainSmoother
{
public:
    void prepare (int maxWindow)
    {
        cap = std::max (1, maxWindow) + 1;
        box.assign ((size_t) cap, 1.0f);
        dqVal.assign ((size_t) cap + 1, 1.0f);
        dqIdx.assign ((size_t) cap + 1, 0);
        setWindow (maxWindow);
        reset();
    }
    void setWindow (int w)
    {
        w = std::min (std::max (1, w), cap - 1);
        if (w != window) { window = w; reset(); }
    }
    int  getWindow() const noexcept { return window; }
    void setRelease (float ms, float sr) { relCoef = tauCoef (ms, sr); }
    void setReleaseCoef (float c) { relCoef = c; }
    void reset()
    {
        std::fill (box.begin(), box.end(), 1.0f);
        dqHead = dqCount = 0;
        t = 0;
        boxSum = window;
        hold = lastGain = 1.0f;
        gw = refresh = 0;
    }

    // Feed the gain this sample wants (<= 1); returns the smoothed gain to
    // apply to the signal delayed by `window` samples.
    float tick (float g)
    {
        const int capq = window + 1;
        while (dqCount > 0 && dqVal[(size_t) ((dqHead + dqCount - 1) % capq)] >= g)
            --dqCount;
        dqVal[(size_t) ((dqHead + dqCount) % capq)] = g;
        dqIdx[(size_t) ((dqHead + dqCount) % capq)] = t;
        ++dqCount;
        while (dqCount > 0 && dqIdx[(size_t) dqHead] <= t - window)
        {
            dqHead = (dqHead + 1) % capq;
            --dqCount;
        }
        ++t;
        const float gmin = dqVal[(size_t) dqHead];

        if (gmin < hold) hold = gmin;
        else             hold += (gmin - hold) * relCoef;

        boxSum += hold - box[(size_t) gw];
        box[(size_t) gw] = hold;
        gw = (gw + 1) % window;
        if (++refresh >= 8192)
        {
            refresh = 0;
            boxSum = 0;
            for (int i = 0; i < window; ++i) boxSum += box[(size_t) i];
        }
        lastGain = std::min (1.0f, (float) (boxSum / window));
        return lastGain;
    }
    float gain() const noexcept { return lastGain; }
    float heldGain() const noexcept { return hold; }

private:
    std::vector<float> box, dqVal;
    std::vector<long> dqIdx;
    int cap = 2, window = 1, dqHead = 0, dqCount = 0, gw = 0, refresh = 0;
    long t = 0;
    double boxSum = 1;
    float hold = 1.0f, relCoef = 0.001f, lastGain = 1.0f;
};

// Stereo look-ahead limiter with true-peak detection and adjustable
// stereo independence.
class StereoLimiter
{
public:
    void prepare (double sampleRate, int maxWindowSamples)
    {
        sr = sampleRate;
        maxWindow = std::max (1, maxWindowSamples);
        for (auto& g : gs) g.prepare (maxWindow);
        const int size = maxWindow + TruePeak4x::kDelay + 2;
        for (auto& d : delay) d.prepare (size);
        setWindow (maxWindow);
        reset();
    }
    void reset()
    {
        for (auto& g : gs) g.reset();
        for (auto& d : delay) d.reset();
        tpl.reset(); tpr.reset();
        sustain = 0.0f;
        grNow = 0.0f;
    }
    void setWindow (int samples)
    {
        window = std::min (std::max (1, samples), maxWindow);
        for (auto& g : gs) g.setWindow (window);
        for (auto& d : delay) d.setDelay (window + TruePeak4x::kDelay);
    }
    void setRelease (float ms) { for (auto& g : gs) g.setRelease (ms, (float) sr); }
    void setReleaseCoefficient (float c) { for (auto& g : gs) g.setReleaseCoef (c); }
    void setTruePeak (bool on) { truePeak = on; }
    int  latency() const { return window + TruePeak4x::kDelay; }

    // Processes one sample pair in place. `independence` 0 links the sides,
    // 1 limits each on its own. Returns the gain reduction in dB (linked).
    float tick (float& l, float& r, float ceiling, float independence)
    {
        // Detect on the undelayed input; the true-peak filter adds 6 samples
        // of delay, which the signal delay accounts for.
        float pl = std::fabs (l), pr = std::fabs (r);
        if (truePeak)
        {
            pl = std::max (pl, tpl.push (l));
            pr = std::max (pr, tpr.push (r));
        }
        else { tpl.push (l); tpr.push (r); }
        const float gl = pl > ceiling ? ceiling / pl : 1.0f;
        const float gr = pr > ceiling ? ceiling / pr : 1.0f;
        const float gLink = std::min (gl, gr);
        const float wl = lerp (gLink, gl, independence), wr = lerp (gLink, gr, independence);
        const float sl = gs[0].tick (wl), sr_ = gs[1].tick (wr);
        const float dl = delay[0].tick (l), dr = delay[1].tick (r);
        l = dl * sl;
        r = dr * sr_;
        grNow = gainToDb (std::min (sl, sr_));
        return grNow;
    }
    float gainReductionDb() const noexcept { return grNow; }
    float gainL() const { return gs[0].gain(); }
    float gainR() const { return gs[1].gain(); }

private:
    double sr = 48000.0;
    int window = 1, maxWindow = 1;
    bool truePeak = true;
    GainSmoother gs[2];
    Delay delay[2];
    TruePeak4x tpl, tpr;
    float sustain = 0.0f, grNow = 0.0f;
};
} // namespace planish
