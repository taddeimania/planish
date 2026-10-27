// Dynamics.h — level detection, gain computing and ballistics shared by the
// Gate, the Compressor, the Equalizer's dynamic bands and Density.
#pragma once

#include "Dsp.h"

namespace planish
{
// The Compressor's LEVEL DETECTION: RMS (averaged), PEAK (instant) and
// TRUE (an averaged rectified envelope that reads every frequency alike).
enum DetectMode { DetectRms = 0, DetectPeak = 1, DetectTrue = 2 };

// Level detector: returns the detected level in dB for each sample.
struct Detector
{
    int mode = DetectPeak;
    float peakRel = 0.0f, envCoef = 0.0f, rmsCoef = 0.0f;
    float peak = 0.0f, env = 0.0f, ms = 0.0f;

    void prepare (float sr)
    {
        peakRel = tauCoef (30.0f, sr);
        envCoef = tauCoef (8.0f, sr);
        rmsCoef = tauCoef (40.0f, sr);
    }
    void reset() { peak = env = ms = 0.0f; }

    float next (float x)   // x: the signal (not rectified)
    {
        const float ax = std::fabs (x);
        switch (mode)
        {
            case DetectRms:
                ms += rmsCoef * (x * x - ms);
                return gainToDb (std::sqrt (ms));
            case DetectTrue:
                env += envCoef * (ax - env);
                return gainToDb (env * 1.2533f);   // rectified mean of a sine -> its RMS
            default:
                if (ax > peak) peak = ax; else peak += peakRel * (ax - peak);
                return gainToDb (peak);
        }
    }
};

// Static curves. Positive ratios compress downward above the threshold;
// negative ratios (as on the panel: "(4):1") compress upward below it.
inline float downwardGainDb (float levelDb, float thresholdDb, float ratio, float kneeDb)
{
    const float over = levelDb - thresholdDb;
    const float slope = 1.0f / std::max (1.0f, ratio) - 1.0f;   // <= 0
    if (kneeDb <= 0.001f)
        return over > 0.0f ? slope * over : 0.0f;
    if (over <= -kneeDb * 0.5f) return 0.0f;
    if (over >= kneeDb * 0.5f)  return slope * over;
    const float t = over + kneeDb * 0.5f;
    return slope * t * t / (2.0f * kneeDb);
}

// Upward compression: raises what sits below the threshold toward it, by
// (1 - 1/ratio) of the distance, fading out 40 dB below so silence stays silent.
inline float upwardGainDb (float levelDb, float thresholdDb, float ratio, float kneeDb)
{
    const float under = thresholdDb - levelDb;
    if (under <= 0.0f) return 0.0f;
    const float amt = 1.0f - 1.0f / std::max (1.0f, ratio);
    float g;
    if (kneeDb <= 0.001f || under >= kneeDb * 0.5f)
        g = amt * under;
    else
        g = amt * under * under / kneeDb;   // a quadratic in the knee
    // Fade to nothing between 30 and 50 dB below the threshold.
    const float fade = clampf ((50.0f - under) / 20.0f, 0.0f, 1.0f);
    return std::min (g, 24.0f) * fade;
}

// Upward expansion: raises what exceeds the threshold.
inline float upwardExpandGainDb (float levelDb, float thresholdDb, float ratio, float kneeDb)
{
    const float over = levelDb - thresholdDb;
    if (over <= 0.0f) return 0.0f;
    const float amt = 1.0f - 1.0f / std::max (1.0f, ratio);
    const float g = kneeDb > 0.001f && over < kneeDb * 0.5f ? amt * over * over / kneeDb : amt * over;
    return std::min (g, 12.0f);
}

// Gain ballistics in dB: attack when the gain is falling, release when rising.
struct Ballistics
{
    float gainDb = 0.0f;
    float attCoef = 1.0f, relCoef = 0.01f;
    float sr = 48000.0f;
    void prepare (float sampleRate) { sr = sampleRate; }
    void set (float attackMs, float releaseMs) { attCoef = tauCoef (attackMs, sr); relCoef = tauCoef (releaseMs, sr); }
    void reset() { gainDb = 0.0f; }
    float next (float targetDb)
    {
        gainDb += (targetDb < gainDb ? attCoef : relCoef) * (targetDb - gainDb);
        return gainDb;
    }
};

// RMS-matching make-up: measures input and output power slowly and reports
// the dB difference, for AUTO GAIN switches.
struct AutoGain
{
    float inMs = 0.0f, outMs = 0.0f, coef = 0.0f, gainDb = 0.0f, smooth = 0.0f;
    void prepare (float sr) { coef = tauCoef (800.0f, sr); smooth = tauCoef (200.0f, sr); }
    void reset() { inMs = outMs = gainDb = 0.0f; }
    void push (float in, float out)
    {
        inMs += coef * (in * in - inMs);
        outMs += coef * (out * out - outMs);
        float target = 0.0f;
        if (inMs > 1e-8f && outMs > 1e-8f)
            target = clampf (10.0f * std::log10 (inMs / outMs), -24.0f, 24.0f);
        gainDb += smooth * (target - gainDb);
    }
};
} // namespace planish
