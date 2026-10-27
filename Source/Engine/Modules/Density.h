// Density.h — up to three bands of upward compression in STEREO, MID/SIDE
// or TRANS/SUS: what falls below the THRESHOLD is raised by RATIO, at most
// by RANGE, at a SPEED from 1 to 300 ms. A short look-ahead lets the boost
// fall away before loud content arrives, so anything above the threshold
// is left untouched.
#pragma once

#include "../Channels.h"
#include "../Dynamics.h"
#include "../Limiter.h"
#include "../Multiband.h"

#include <atomic>

namespace planish
{
class Density final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    static constexpr float kLookaheadMs = 3.0f;
    Density();

    int pChMode;
    MultibandParams mb;
    struct BandIdx { int threshold, range, ratio, speed; };
    BandIdx band[2][kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int latency() const override { return ctx.zeroLatency ? 0 : lookahead; }

    std::atomic<int> soloBand { -1 }, soloSet { -1 };
    float boostDb[2][kBands] {};      // per lane group and band, now
    float levelDb[2][kBands] {};

    int channelMode() const { return channelModeFromChoice (info().channelModes, (int) value (pChMode)); }
    float staticBoostDb (int set, int b, float level) const;

private:
    static constexpr int kStep = 32;
    LaneSplitter lanes;
    MultibandSplit split;
    struct State { Detector det; GainSmoother gs; };
    State st[2][kBands];
    Delay delay[Lanes::kMax][kBands];
    int lookahead = 144, lastWindow = -1;
};
} // namespace planish
