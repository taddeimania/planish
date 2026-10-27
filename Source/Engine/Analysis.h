// Analysis.h — what the assistant listens for in a track: its tonal
// balance (sixth-octave spectrum, pink-relative), loudness, the spread of
// its levels (quiet floor to loud peaks), how often it has onsets, its
// crest factor, width, and where its energy sits. Fed blocks on the
// audio thread, or offline from a reference file.
#pragma once

#include "Dsp.h"
#include "Fft.h"
#include "Loudness.h"
#include "Stft.h"
#include "Targets.h"

#include <array>

namespace planish
{
struct AnalysisResult
{
    static constexpr int kBands = 61;   // sixth octaves, 20 Hz .. 20 kHz
    static float bandHz (int b) { return 20.0f * std::pow (2.0f, (float) b / 6.0f); }
    std::array<float, kBands> spectrumDb {};   // pink-relative, mean over 100 Hz..8 kHz = 0
    float integratedLufs = -70.0f;
    float shortTermMaxLufs = -70.0f;
    float peakDb = -100.0f;
    float floorDb = -100.0f;      // 10th percentile of 50 ms RMS levels (while not silent)
    float bodyDb = -100.0f;       // 50th percentile
    float loudDb = -100.0f;       // 90th percentile
    float crestDb = 12.0f;        // peak over RMS, averaged
    float onsetsPerSecond = 0.0f;
    float widthDb = -30.0f;       // side over mid energy
    float centroidHz = 1000.0f;
    float lowShare = 0.0f;        // energy below 120 Hz, 0..1
    float highShare = 0.0f;       // energy above 5 kHz, 0..1
    float seconds = 0.0f;
    bool  valid = false;

    // The spectrum at a frequency (interpolated), pink-relative dB.
    float spectrumAt (float hz) const;
    // Eleven octave points, as a target curve.
    CustomTarget asTarget (const std::string& name) const;
};

class Analyzer
{
public:
    static constexpr int kFrame = 4096, kHop = 1024;
    void prepare (double sampleRate);
    void reset();
    void push (const float* l, const float* r, int n);   // accumulate
    AnalysisResult result() const;
    float seconds() const { return (float) samples / (float) sr; }

private:
    double sr = 48000.0;
    long samples = 0;
    Fft fft;
    LogBands bands;
    std::vector<float> window, frameBuf, frameEnergies, ring;
    std::vector<Fft::cpx> bins;
    std::vector<double> acc;
    double centroidNum = 0, centroidDen = 0, lowE = 0, highE = 0, totalE = 0;
    int frames = 0, inPos = 0, sinceHop = 0;
    LoudnessMeter meter;
    float stMax = -70.0f, peak = 0.0f;
    // Level histogram of 50 ms RMS blocks: -100 .. 0 dBFS in 0.5 dB bins.
    static constexpr int kHist = 200;
    std::array<int, kHist> hist {};
    double blockSum = 0; int blockLen = 0, blockSize = 2400;
    double midE = 0, sideE = 0;
    EnvFollower fastEnv, slowEnv;
    bool inOnset = false;
    int onsets = 0;
    double crestAcc = 0; long crestCount = 0;
    float blockPeak = 0.0f;
};
} // namespace planish
