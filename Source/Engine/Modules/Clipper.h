// Clipper.h — up to three bands of clipping in STEREO, MID/SIDE or
// TRANS/SUS. SOFT CLIP BOOST pushes the band into a soft knee that grows
// with the boost; THRESHOLD is where it is hard clipped. 4x OVERSAMPLING.
#pragma once

#include "../Channels.h"
#include "../Multiband.h"

#include <atomic>

namespace planish
{
class Clipper final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    Clipper();

    int pChMode, pOversample;
    MultibandParams mb;
    struct BandIdx { int boost, threshold; };
    BandIdx band[2][kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int latency() const override;

    std::atomic<int> soloBand { -1 }, soloSet { -1 };

    // Per set and band: peak in and out of the clipper this block (linear).
    float peakIn[2][kBands] {}, peakOut[2][kBands] {};
    float gainDb (int set, int b) const;   // out peak over in peak: boost minus what was clipped

    int channelMode() const { return channelModeFromChoice (info().channelModes, (int) value (pChMode)); }

    // The transfer curve of one set and band, for the panel.
    static float clip (float x, float boostDb, float thresholdDb);

private:
    static constexpr int kStep = 32;
    bool oversampling() const { return value (pOversample) > 0.5f && ! ctx.zeroLatency; }
    LaneSplitter lanes;
    MultibandSplit split;
    Oversampler4x os1[Lanes::kMax][kBands];
    Delay offDelay[2];
};
} // namespace planish
