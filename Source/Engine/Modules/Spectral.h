// Spectral.h — the two 32-band spectral shapers, on one STFT (2048-point
// frames, 512 hop) with gains per band interpolated across the bins:
//
//   SCULPTOR  compresses each band toward the selected instrument target
//             (or a custom curve), within a range, with AMOUNT, TONE and
//             SPEED; the overall level is left where it was.
//   UNMASK    attenuates the track where it masks the sidechain: the bands
//             where both carry energy and the track is near or above the
//             sidechain, with AMOUNT, SENSITIVITY, ATTACK and RELEASE.
//
// Both offer STEREO, MID/SIDE and TRANS/SUS, with A and B controls.
#pragma once

#include "../Channels.h"
#include "../Stft.h"
#include "../Targets.h"

#include <array>
#include <atomic>

namespace planish
{
struct SpectralBands
{
    static constexpr int kCount = 32;
    static float edgeHz (int i) { return 20.0f * std::pow (1000.0f, (float) i / (float) kCount); }
    static float centreHz (int b) { return std::sqrt (edgeHz (b) * edgeHz (b + 1)); }
    // Position of a frequency in bands (0 = first centre), for interpolation.
    static float bandPos (float hz) { return std::log (std::max (20.0f, hz) / 20.0f) / std::log (1000.0f) * (float) kCount - 0.5f; }
};

class SpectralModule : public Module
{
public:
    static constexpr int kFrame = 2048, kHop = 512;
    static constexpr int kBands = SpectralBands::kCount;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return ctx.zeroLatency ? 0 : kFrame; }

    int pChMode = -1;
    struct SetIdx { int lo = -1, hi = -1; };
    SetIdx range[2];

    // Display: per lane group (0/1) and band.
    float gainDb[2][kBands] {};
    float specDb[2][kBands] {};    // the module's smoothed input spectrum, pink-relative
    float scSpecDb[kBands] {};     // Unmask: the sidechain

    std::atomic<bool> soloRange { false };
    std::atomic<int> soloSet { -1 };
    int channelMode() const { return channelModeFromChoice (info().channelModes, (int) value (pChMode)); }

protected:
    explicit SpectralModule (ModuleInfo m) : Module (std::move (m)) {}
    // Fill out[] (dB per band) for lane group g, set s, from this frame's
    // pink-relative band levels.
    virtual void computeGains (int g, int s, const float* frameDb, float* out) = 0;
    virtual bool wantsSidechain() const { return false; }
    float frameSeconds() const { return (float) kHop / (float) sr; }
    bool sidechainHasSignal = false;

private:
    void onFrame (Stft::cpx** bins, int chans, int nb, int mode);
    void bandLevels (const Stft::cpx* bins, float* outDb) const;
    Stft stft, scStft;
    LaneSplitter lanes;
    std::vector<int> firstBin, lastBin;
    std::vector<float> binPos;     // band position of each bin
    std::vector<float> scBuf[2];
    Delay zlDelay[2];
};

class Sculptor final : public SpectralModule
{
public:
    Sculptor();
    int pTarget;
    struct SetParams { int amount, tone, speed; };
    SetParams sp[2];
    // Custom target (from the assistant or a reference), chosen by the
    // last entry of the target menu.
    CustomTarget custom;
    std::atomic<bool> hasCustom { false };
    float targetDb (int s, float hz) const;   // the tone-tilted target at hz
protected:
    void computeGains (int g, int s, const float* frameDb, float* out) override;
private:
    float smooth[2][kBands] {};
    bool primed[2] { false, false };
};

class Unmask final : public SpectralModule
{
public:
    Unmask();
    struct SetParams { int amount, sensitivity, attack, release; };
    SetParams sp[2];
    bool sidechainPresent() const { return sidechainHasSignal; }
protected:
    void computeGains (int g, int s, const float* frameDb, float* out) override;
    bool wantsSidechain() const override { return true; }
private:
    float gr[2][kBands] {};
};
} // namespace planish
