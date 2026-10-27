// TransientShaper.h — up to three bands of attack and sustain shaping, in
// STEREO or MID/SIDE, with three envelope modes (PRECISE, BALANCED, LOOSE)
// and a CONTOUR per band (SHARP, MEDIUM, SMOOTH). No threshold: the gain
// follows the difference between fast and slow envelopes, so it works at
// any level.
#pragma once

#include "../Channels.h"
#include "../Multiband.h"

#include <atomic>

namespace planish
{
class TransientShaper final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    enum EnvMode { Precise = 0, Balanced, Loose };
    enum Contour { Sharp = 0, Medium, Smooth };

    TransientShaper();

    int pChMode, pEnvMode;
    MultibandParams mb;
    struct BandIdx { int attack, sustain, contour; };
    BandIdx band[2][kBands];   // [set][band]

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    std::atomic<int> soloSet { -1 };

    // Gain applied now per lane group (0: stereo or mid, 1: side) and band, dB.
    float gainDb[2][kBands] {};
    float peakGainDb[2][kBands] {};   // largest |gain| this block, signed

    int channelMode() const { return channelModeFromChoice (info().channelModes, (int) value (pChMode)); }

private:
    static constexpr int kStep = 32;
    MultibandSplit split;
    struct State { EnvFollower fast, slow, sus; float g = 0.0f; };
    State st[2][kBands];
    int lastTimes[2] { -1, -1 };
    float smooth = 0.0f;
};
} // namespace planish
