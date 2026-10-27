// Exciter.h — up to three bands of harmonic distortion. Each band has a
// DRIVE, a processing family (CLASSIC: Retro, Tape, Tube, Warm; TRASH:
// Overdrive, Scream, Clipper, Scratch) blended on an X/Y pad, and a MIX.
// Around them: TAME (even distortion at any level, dynamics restored),
// 4x OVERSAMPLING, PRE-EMPHASIS (Full, Defined, Clear, Flat), a TONE tilt
// on the wet signal and a high-shelf POST FILTER.
#pragma once

#include "../Channels.h"
#include "../Multiband.h"

#include <atomic>

namespace planish
{
class Exciter final : public Module
{
public:
    static constexpr int kBands = MultibandParams::kMaxBands;
    enum Family { Classic = 0, Trash = 1 };
    enum Shape { Retro = 0, Tape, Tube, Warm, Overdrive, Scream, Clipper, Scratch, NumShapes };
    enum Emphasis { Full = 0, Defined, Clear, Flat };

    Exciter();

    int pTame, pOversample, pEmphasis, pTone, pPostFreq, pPostGain;
    MultibandParams mb;
    struct BandIdx { int drive, family, x, y, mix; };
    BandIdx band[kBands];

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int latency() const override;

    std::atomic<int> soloBand { -1 };
    float harmonicLevel[kBands] {};   // added harmonics per band, 0..1-ish

    // The corner shapes, for tests and the pad's legend.
    static float shape (int shape, float x);
    static const char* shapeName (int s);
    // Corner weights of the pad: top-left, top-right, bottom-left, bottom-right.
    static void padWeights (float x, float y, float* w);

private:
    static constexpr int kStep = 32;
    bool oversampling() const { return value (pOversample) > 0.5f && ! ctx.zeroLatency; }
    MultibandSplit split;
    struct BandState
    {
        Oversampler4x os;
        float dc = 0.0f;
        EnvFollower env;          // tame: the band's level
        TransientDetector trans;  // tube: attacks drive harder
        float harmMs = 0.0f, dryMs = 0.0f;
        Biquad pre, post;         // pre-emphasis and its inverse
    };
    BandState st[2][kBands];
    Biquad toneLo[2], toneHi[2], postShelf[2];
    Delay alignDry[2];
    float last[4] { -1, -1, -1, -1 };
    Smoother driveSm[kBands], mixSm[kBands];
};
} // namespace planish
