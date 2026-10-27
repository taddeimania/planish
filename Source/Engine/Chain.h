// Chain.h — the whole signal path, without JUCE:
//
//   in -> INPUT GAIN -> [modules, in the chosen order, each with its own
//      wet/dry MIX] -> WIDTH / PAN / INVERT / SWAP / MONO -> OUTPUT GAIN
//      -> LIMITER -> (BYPASS) -> out
//
// Every module exists once (the Compressor twice); `order` says which are
// in use and where. SOLO plays one module alone, DELTA plays the
// difference one module makes, BYPASS plays the input delayed to line up
// (gain matched to the output when GAIN MATCH is on). The sidechain bus
// reaches every module through its BlockContext.
#pragma once

#include "Analysis.h"
#include "Loudness.h"
#include "Module.h"
#include "OutputLimiter.h"
#include "Modules/Clipper.h"
#include "Modules/Compressor.h"
#include "Modules/Density.h"
#include "Modules/Equalizer.h"
#include "Modules/Exciter.h"
#include "Modules/Gate.h"
#include "Modules/Phase.h"
#include "Modules/Spectral.h"
#include "Modules/TransientShaper.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace planish
{
enum ModuleId
{
    ModGate = 0, ModEqualizer, ModCompressor1, ModCompressor2, ModTransientShaper, ModExciter,
    ModSculptor, ModUnmask, ModDensity, ModPhase, ModClipper, NumModules
};
constexpr int kMaxChain = NumModules;

struct ChainSettings
{
    float inGainDb = 0.0f, outGainDb = 0.0f;
    bool bypass = false, gainMatch = false, mono = false, swap = false, invert = false;
    float pan = 0.0f;          // -100 .. 100
    float width = 0.0f;        // -100 (mono) .. 100
    bool limiterOn = false;
    float ceilingDb = -0.3f;
    int limiterMode = OutputLimiter::Irc2, limiterStyle = OutputLimiter::Smooth;
    bool zeroLatency = false;
    int soloModule = -1;       // ModuleId or -1
    int deltaModule = -1;
    bool analysing = false;    // feed the assistant's analyser
    int meterType = 0;         // 0 RMS + peak, 1 short-term + peak
};

// Lock-free ring of stereo samples for the panel's analyser and scopes.
class Tap
{
public:
    static constexpr int kSize = 16384;
    void push (const float* l, const float* r, int n)
    {
        int w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            left[(size_t) (w % kSize)] = l[i];
            right[(size_t) (w % kSize)] = r[i];
            ++w;
        }
        writePos.store (w, std::memory_order_release);
    }
    // Copies the latest `count` samples (oldest first).
    void latest (float* l, float* r, int count) const
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const int idx = ((w - count + i) % kSize + kSize) % kSize;
            l[i] = left[(size_t) idx];
            r[i] = right[(size_t) idx];
        }
    }
    // Peak of the samples written since the last call, for scrolling traces.
    float takePeak() const
    {
        const int w = writePos.load (std::memory_order_acquire);
        int count = std::min (kSize, w - lastPeakPos);
        if (count <= 0) return 0.0f;
        float pk = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const int idx = ((w - count + i) % kSize + kSize) % kSize;
            pk = std::max (pk, std::max (std::fabs (left[(size_t) idx]), std::fabs (right[(size_t) idx])));
        }
        lastPeakPos = w;
        return pk;
    }
    int position() const { return writePos.load (std::memory_order_acquire); }
    void clear() { std::fill (left.begin(), left.end(), 0.0f); std::fill (right.begin(), right.end(), 0.0f); writePos.store (0); lastPeakPos = 0; }

private:
    // On the heap: a Chain holds five Taps, and the tests build Chains on the
    // stack (1 MB by default on Windows).
    std::vector<float> left = std::vector<float> (kSize, 0.0f), right = std::vector<float> (kSize, 0.0f);
    std::atomic<int> writePos { 0 };
    mutable int lastPeakPos = 0;
};

struct MeterReadings
{
    float peak[2] { 0.0f, 0.0f }, rms[2] { 0.0f, 0.0f };   // linear, since last read (peak) / averaged (rms)
    float lufsShort = -70.0f, lufsMomentary = -70.0f, lufsIntegrated = -70.0f, truePeakDb = -100.0f;
    bool clipped[2] { false, false };
};

class Chain
{
public:
    Chain();

    void prepare (double sampleRate, int maxBlock);
    void reset();
    // scL/scR: the sidechain bus, or null.
    void process (float* l, float* r, int n, const float* scL, const float* scR, const ChainSettings&);

    // Host transport, for the synced scopes.
    void setTransport (double bpm, double ppq, bool playing) { hostBpm = bpm; hostPpq = ppq; hostPlaying = playing; }
    double bpm() const { return hostBpm; }

    // Modules.
    Module& module (int id) { return *mods[(size_t) id]; }
    const Module& module (int id) const { return *mods[(size_t) id]; }
    static const char* moduleName (int id);
    static const char* moduleShortName (int id);

    Compressor& compressor (int instance) { return instance == 1 ? comp1 : comp2; }
    Gate gate;
    Equalizer eq;
    Compressor comp1 { 1 }, comp2 { 2 };
    TransientShaper transient;
    Exciter exciter;
    Sculptor sculptor;
    Unmask unmask;
    Density density;
    Phase phase;
    Clipper clipper;

    // The order (message thread writes under the lock; the audio thread
    // copies it at the start of each block).
    void setOrder (const std::vector<int>& ids);
    std::vector<int> getOrder() const;
    bool inChain (int id) const;
    int  latency() const { return latencyNow.load (std::memory_order_relaxed); }
    int  latencyFor (const ChainSettings&) const;

    // Meters and taps, read by the panel.
    MeterReadings takeInputMeters();
    MeterReadings takeOutputMeters();
    void resetMeters();
    float moduleGainDb (int id) const { return diffDb[(size_t) id]; }   // difference meter
    float gainMatchDb() const { return matchDb; }
    float limiterGrDb() const { return limiter.grDb; }
    bool sidechainPresent() const { return scPresent; }
    Tap inputTap, outputTap, moduleInTap, moduleTap, sidechainTap;
    std::atomic<int> tapModule { -1 };   // whose in/out feed moduleInTap/moduleTap (-1: chain in/out)
    LoudnessMeter& outputLoudness() { return outMeter; }

    Analyzer analyser;   // fed while settings.analysing

private:
    void updateLatency (const ChainSettings&);

    double sr = 48000.0;
    int maxBlock = 512;
    std::array<Module*, NumModules> mods {};

    // Order handoff.
    mutable std::atomic<bool> orderLock { false };
    std::array<int, kMaxChain> orderShared {};
    int orderSharedCount = 0;
    std::array<int, kMaxChain> order {};
    int orderCount = 0;
    std::atomic<int> latencyNow { 0 };

    double hostBpm = 120.0, hostPpq = 0.0, freePpq = 0.0;
    bool hostPlaying = false, scPresent = false;

    // Per-module mix: the module's input, delayed by its latency.
    struct MixState { Delay dl, dr; Smoother mix; };
    std::array<MixState, NumModules> mixState;
    // Per-module in/out levels for the difference meters.
    float inMs[NumModules] {}, outMs[NumModules] {};
    float diffDb[NumModules] {};

    OutputLimiter limiter;
    Delay dryDelay[2];
    std::vector<float> dryL, dryR, modL, modR, scZeroL, scZeroR;
    LoudnessMeter matchMeter;
    Smoother inSm, outSm, bypassSm, monoSm, matchSm, panSm, widthSm, invertSm;
    LoudnessMeter inMeter, outMeter;
    MeterReadings inRead, outRead;
    float inMsM[2] {}, outMsM[2] {};
    float matchDb = 0.0f;
    bool  clipIn[2] {}, clipOut[2] {};
};
} // namespace planish
