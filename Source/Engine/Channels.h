// Channels.h — the channel processing modes shared by most modules, as
// "lanes": the signals a module actually processes.
//
//   STEREO     2 lanes, left and right, one control set (A), detection linked
//   MID/SIDE   2 lanes, mid (set A) and side (set B), each on its own
//   TRANS/SUS  4 lanes: the transient part of left and right (set A) and
//              the sustain part of left and right (set B). The split is a
//              weight from a fast and a slow envelope of the stereo signal,
//              so the two parts always sum back to the input exactly.
#pragma once

#include "Dsp.h"
#include "Module.h"

namespace planish
{
// Transient weight 0..1 per sample, linked across the stereo pair.
struct TransientSplitter
{
    EnvFollower fast, slow;
    float w = 0.0f, wAtt = 1.0f, wRel = 0.01f;

    void prepare (float sr)
    {
        // Same release on both: they only part at an onset, when the slow
        // one lags behind the attack.
        fast.set (0.1f, 30.0f, sr);
        slow.set (12.0f, 30.0f, sr);
        wAtt = tauCoef (0.3f, sr);
        wRel = tauCoef (20.0f, sr);
    }
    void reset() { fast.reset(); slow.reset(); w = 0.0f; }
    float next (float l, float r)
    {
        const float a = 0.5f * (std::fabs (l) + std::fabs (r));
        const float f = fast.next (a), s = slow.next (a);
        const float raw = clampf (((f - s) / (f + 1.0e-9f) - 0.2f) * 2.5f, 0.0f, 1.0f);
        w += (raw > w ? wAtt : wRel) * (raw - w);
        return w;
    }
};

struct Lanes
{
    static constexpr int kMax = 4;
    static int count (int mode) { return mode == TransSus ? 4 : 2; }
    // Control set (0 = A, 1 = B) a lane uses.
    static int setFor (int mode, int lane) { return mode == Stereo ? 0 : mode == MidSide ? lane : lane / 2; }
    // Lanes in the same group share one detector level (linked stereo).
    static int group (int mode, int lane) { return mode == Stereo ? 0 : mode == MidSide ? lane : lane / 2; }
    static int numGroups (int mode) { return mode == Stereo ? 1 : 2; }
    static bool twoSets (int mode) { return mode != Stereo; }
    static const char* setName (int mode, int set)
    {
        if (mode == MidSide) return set == 0 ? "MID" : "SIDE";
        if (mode == TransSus) return set == 0 ? "TRANS" : "SUS";
        return "A";
    }
};

class LaneSplitter
{
public:
    void prepare (double sr) { ts.prepare ((float) sr); }
    void reset() { ts.reset(); }

    // Fills x[0..count-1]. The transient weight of the last sample is kept
    // for display.
    void split (int mode, float l, float r, float* x)
    {
        if (mode == MidSide) { x[0] = 0.5f * (l + r); x[1] = 0.5f * (l - r); return; }
        if (mode == TransSus)
        {
            const float w = ts.next (l, r);
            lastWeight = w;
            x[0] = l * w; x[1] = r * w; x[2] = l - x[0]; x[3] = r - x[1];
            return;
        }
        x[0] = l; x[1] = r;
    }
    static void join (int mode, const float* x, float& l, float& r)
    {
        if (mode == MidSide) { l = x[0] + x[1]; r = x[0] - x[1]; return; }
        if (mode == TransSus) { l = x[0] + x[2]; r = x[1] + x[3]; return; }
        l = x[0]; r = x[1];
    }
    // Silences the lanes of the set that is not soloed (-1: none).
    static void soloSet (int mode, float* x, int set)
    {
        if (set < 0 || mode == Stereo) return;
        for (int i = 0; i < count (mode); ++i)
            if (Lanes::setFor (mode, i) != set) x[i] = 0.0f;
    }
    static int count (int mode) { return Lanes::count (mode); }
    float lastWeight = 0.0f;

private:
    TransientSplitter ts;
};
} // namespace planish
