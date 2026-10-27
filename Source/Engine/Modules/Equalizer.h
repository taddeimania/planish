// Equalizer.h — twelve bands in thirteen shapes, in STEREO, MID/SIDE or
// TRANS/SUS, with SOFT SATURATION. Any band but a pass filter can be made
// DYNAMIC: past its THRESHOLD it moves UP or DOWN toward (or away from)
// its gain, listening to its own region, the full signal, another band's
// region, or the external sidechain, full or through a band's region.
#pragma once

#include "../Channels.h"
#include "../Filters.h"
#include "../Module.h"
#include "../Sidechain.h"

#include <atomic>

namespace planish
{
class Equalizer final : public Module
{
public:
    static constexpr int kBands = 12;
    enum Direction { Up = 0, Down = 1 };

    Equalizer();

    int pChMode, pSoftSat;
    struct BandIdx { int on, shape, freq, gain, q, slope, dyn, threshold, direction, attack, release, sidechain; };
    BandIdx band[2][kBands];   // [set][band]

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;

    // UI: solo one band of a set (-1 none), solo a set (-1 none), and the
    // alt-solo window (a bandpass that follows the mouse).
    std::atomic<int> soloBand { -1 }, soloSet { -1 };
    std::atomic<float> altSoloHz { -1.0f };
    float altSoloQ = 2.0f;

    // Dynamic telemetry per lane group and band.
    float dynGainDb[2][kBands] {};
    float levelDb[2][kBands] {};

    int channelMode() const { return channelModeFromChoice (info().channelModes, (int) value (pChMode)); }

    // Responses of a set at the given frequencies, from the current values.
    // `dynamic` uses the gains the dynamic bands have now (group = set).
    void curveDb (int set, const float* hz, float* outDb, int count, bool dynamic = false) const;
    void bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool dynamic = false) const;
    bool isDynamic (int set, int b) const;

private:
    static constexpr int kStep = 32;
    void updateStatic (bool force);
    float dynamicTarget (int set, int b, float lev) const;

    LaneSplitter lanes;
    FilterBand bands[Lanes::kMax][kBands];
    SoloFilter detF[Lanes::kMax][kBands], extF[2][kBands];
    SoloFilter soloF[Lanes::kMax], altF[Lanes::kMax];
    float env[2][kBands] {};
    float lastVals[2][kBands][6] {};
    float lastDetVals[2][kBands][4] {};
    int lastMode = -1;
    float lastAlt = -2.0f;
    int lastSolo = -2;
    float satDc[Lanes::kMax] {};
};
} // namespace planish
