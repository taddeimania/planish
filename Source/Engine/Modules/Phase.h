// Phase.h — ROTATION turns the phase of every frequency by the same angle
// (both channels alike), which changes the shape of an asymmetrical
// waveform without moving it in time; DELAY shifts the signal by up to
// 20 ms. LEARN finds the rotation that lowers the peaks the most and,
// with a sidechain, the delay that best lines the track up with it (and
// whether its polarity is flipped).
//
// Rotation runs in the frequency domain (1024-point frames), so the
// module has 1024 samples of latency; in ZERO LATENCY it only delays.
#pragma once

#include "../Sidechain.h"
#include "../Stft.h"

#include <atomic>
#include <vector>

namespace planish
{
class Phase final : public Module
{
public:
    static constexpr int kFrame = 1024, kHop = 256;
    static constexpr float kMaxDelayMs = 20.0f;
    static constexpr int kLearnSamples = 1 << 16;

    Phase();

    int pRotation, pDelay, pScopeType, pScopeTime;

    void prepare (double sampleRate, int maxBlock) override;
    void reset() override;
    void process (float* l, float* r, int n) override;
    int  latency() const override { return ctx.zeroLatency ? 0 : kFrame; }

    // LEARN (message thread): request, then poll learnTick() from a timer;
    // it returns true once, when the result is ready.
    void requestLearn() { learnRequests.fetch_add (1); }
    bool learning() const { return learnState.load() != 0; }
    float learnProgress() const { return (float) learnFill.load() / (float) kLearnSamples; }
    bool learnTick();
    float learnedRotation = 0.0f, learnedDelayMs = 0.0f;
    bool learnedDelayValid = false, learnedPolarityFlip = false;
    float learnedPeakGainDb = 0.0f;   // how much lower the peaks get

    ScopeRecorder scope;
    float rotationNow = 0.0f;

    // Offline helpers (also used by the tests).
    static float bestRotation (const std::vector<float>& x, float& peakGainDb);
    static int bestDelay (const std::vector<float>& x, const std::vector<float>& sc, int maxLag, bool& flipped);

private:
    Stft stft;
    Delay delayLine[2];
    Smoother delaySm;
    float rotTarget = 0.0f;

    std::atomic<int> learnRequests { 0 };
    int seenRequests = 0;
    std::atomic<int> learnState { 0 };   // 0 idle, 1 recording, 2 ready to compute
    std::atomic<int> learnFill { 0 };
    std::vector<float> learnIn, learnSc;
    bool learnHadSc = false;
};
} // namespace planish
