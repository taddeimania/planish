// Gate.h — up to three bands of gating with an OPEN and a lower CLOSE
// threshold (hysteresis), RATIO, ATTACK, HOLD and RELEASE, a sidechain per
// band (internal or external, full or through a band) and a detection
// filter. Stereo, linked.
#pragma once

#include "../Dynamics.h"
#include "../Multiband.h"
#include "../Sidechain.h"

#include <atomic>

namespace planish
{
class Gate final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    Gate();

    MultibandParams mb;
    DetectionParams det;
    struct BandIdx { int open, close, ratio, attack, hold, release, sidechain; };
    BandIdx band[kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    std::atomic<bool> detSolo { false };

    // Telemetry for the panel.
    struct Telemetry { float levelDb = -100.0f, gainDb = 0.0f, minGainDb = 0.0f; bool open = false; };
    Telemetry tm[kBands];

    // The gain the static curve gives a band at a level, closed state.
    float closedGainDb (int b, float levelDb) const;

private:
    static constexpr int kStep = 32;
    MultibandSplit split, scSplit;
    DetectionFilter detF[kBands][2];
    float lastDet[5] { -1, -1, -1, -1, -1 };
    struct BandState
    {
        float peak = 0.0f;
        bool open = false;
        int holdLeft = 0;
        float gainDb = -80.0f;
    };
    BandState st[kBands];
    float peakRel = 0.0f;
};
} // namespace planish
