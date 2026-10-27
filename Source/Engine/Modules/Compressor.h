// Compressor.h — up to three bands of compression in three processing
// modes: PUNCH (an intent control on the hits, no threshold), MODERN
// (transparent feed-forward with knee and auto release) and VINTAGE (a
// coloured feedback design). LEVEL DETECTION RMS, PEAK or TRUE; AUTO GAIN;
// a detection filter; a sidechain per band; ratios under 1:1 expand.
// Two instances exist, Compressor 1 and Compressor 2.
#pragma once

#include "../Dynamics.h"
#include "../Multiband.h"
#include "../Sidechain.h"

#include <atomic>

namespace planish
{
class Compressor final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    enum Mode { Punch = 0, Modern, Vintage };

    explicit Compressor (int instance);

    int pMode, pDetect, pAutoGain;
    MultibandParams mb;
    DetectionParams det;
    struct BandIdx { int threshold, knee, ratio, attack, release, autoRelease, makeup, mix, amount, punchAttack, punchSustain, sidechain; };
    BandIdx band[kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    std::atomic<int> soloBand { -1 };
    std::atomic<bool> detSolo { false };

    struct Telemetry { float levelDb = -100.0f, gainDb = 0.0f, minGainDb = 0.0f, autoDb = 0.0f; };
    Telemetry tm[kBands];
    ScopeRecorder scope;
    bool sidechainActive = false;   // a band listens to the external input

    // Static curve (Modern/Vintage), for the curve view.
    float staticGainDb (int b, float levelDb) const;

    // Called when the user switches the sidechain on for a band: the
    // settings the manual describes (external full, peak, no auto gain).
    void sidechainDefaults (int b);

private:
    static constexpr int kStep = 32;
    MultibandSplit split, scSplit;
    DetectionFilter detF[kBands][2];
    float lastDet[4] { -1, -1, -1, -1 };
    struct BandState
    {
        Detector detL, detR;
        Ballistics bal;
        AutoGain ag;
        EnvFollower fastEnv, slowEnv;
        float crest = 0.0f;
        float fbGain = 1.0f;     // vintage feedback
        float avgDb = -100.0f;   // punch: programme level
        float punchDb = 0.0f;
    };
    BandState st[kBands];
    Smoother makeupSm[kBands], mixSm[kBands];
    int instanceNo;
};
} // namespace planish
