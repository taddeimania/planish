#include "Clipper.h"

namespace planish
{
Clipper::Clipper()
    : Module ({ "clp", "CLIPPER", "CLIPPER", 0xffe2574cu, kMidSideBit | kTransSusBit })
{
    pChMode     = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit | kTransSusBit), 0);
    pOversample = addBool ("oversample", "Oversampling", true);
    mb.declare (*this, 150.0f, 3000.0f);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            band[s][b].boost     = addDb (p + "boost", n + "Soft Clip Boost", 0.0f, 12.0f, 0.0f);
            band[s][b].threshold = addDb (p + "threshold", n + "Threshold", -24.0f, 0.0f, 0.0f);
        }
}

int Clipper::latency() const { return oversampling() ? Oversampler4x::kLatency : 0; }

float Clipper::clip (float x, float boostDb, float thresholdDb)
{
    const float t = dbToGain (thresholdDb);
    const float u = x * dbToGain (boostDb) / t;
    const float k = clampf (boostDb / 12.0f, 0.0f, 0.85f);   // the knee widens with the boost
    const float au = std::fabs (u);
    float y = au;
    if (k > 1.0e-4f && au > 1.0f - k)
        y = (1.0f - k) + k * softTanh ((au - (1.0f - k)) / k);
    y = std::min (y, 1.0f);
    return (u < 0.0f ? -y : y) * t;
}

void Clipper::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    lanes.prepare (sr);
    for (auto& d : offDelay) d.prepare (64);
    reset();
}

void Clipper::reset()
{
    split.reset();
    lanes.reset();
    for (auto& row : os1) for (auto& o : row) o.reset();
    for (auto& d : offDelay) d.reset();
}

float Clipper::gainDb (int set, int b) const
{
    if (peakIn[set][b] < 1.0e-5f) return 0.0f;
    return gainToDb (std::max (peakOut[set][b], 1.0e-6f)) - gainToDb (peakIn[set][b]);
}

void Clipper::process (float* l, float* r, int n)
{
    for (auto& row : peakIn) for (auto& v : row) v = 0.0f;
    for (auto& row : peakOut) for (auto& v : row) v = 0.0f;
    const bool os = oversampling();
    for (auto& d : offDelay) d.setDelay (latency());
    if (! isOn())
    {
        if (os) for (int i = 0; i < n; ++i) { l[i] = offDelay[0].tick (l[i]); r[i] = offDelay[1].tick (r[i]); }
        return;
    }
    const int mode = channelMode();
    const int nl = Lanes::count (mode);
    const int nb = mb.numBands (*this);
    const int solo = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        float boost[2][kBands], thr[2][kBands];
        for (int s = 0; s < 2; ++s)
            for (int b = 0; b < nb; ++b) { boost[s][b] = value (band[s][b].boost); thr[s][b] = value (band[s][b].threshold); }

        for (int i = start; i < start + len; ++i)
        {
            float x[Lanes::kMax];
            lanes.split (mode, l[i], r[i], x);
            for (int ln = 0; ln < nl; ++ln)
            {
                const int s = Lanes::setFor (mode, ln);
                float bands[4];
                split.split (ln, x[ln], bands);
                float sum = 0.0f;
                for (int b = 0; b < nb; ++b)
                {
                    float y;
                    if (value (mb.pBandOn[b]) > 0.5f)
                    {
                        const float bb = boost[s][b], tt = thr[s][b];
                        auto fn = [bb, tt] (float v) { return clip (v, bb, tt); };
                        y = os ? os1[ln][b].process (bands[b], fn) : fn (bands[b]);
                        peakIn[s][b] = std::max (peakIn[s][b], std::fabs (bands[b]));
                        peakOut[s][b] = std::max (peakOut[s][b], std::fabs (y));
                    }
                    else
                        y = os ? os1[ln][b].process (bands[b], [] (float v) { return v; }) : bands[b];
                    if (solo < 0 || solo == b) sum += y;
                }
                x[ln] = sum;
            }
            LaneSplitter::soloSet (mode, x, sSet);
            LaneSplitter::join (mode, x, l[i], r[i]);
        }
    }
}
} // namespace planish
