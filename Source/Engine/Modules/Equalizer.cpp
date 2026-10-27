#include "Equalizer.h"

namespace planish
{
namespace
{
std::vector<std::string> shapeChoices()
{
    std::vector<std::string> v;
    for (int i = 0; i < NumFilterShapes; ++i) v.push_back (filterShapeName (i));
    return v;
}
} // namespace

Equalizer::Equalizer()
    : Module ({ "eq", "EQUALIZER", "EQ", 0xff5cc8f2u, kMidSideBit | kTransSusBit })
{
    pChMode  = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit | kTransSusBit), 0);
    pSoftSat = addBool ("softsat", "Soft Saturation", false);

    // Four gentle bands on at 0 dB, eight free.
    const int defShape[kBands] { LowShelfAnalog, ProportionalQ, ProportionalQ, HighShelfAnalog,
                                 ProportionalQ, ProportionalQ, ProportionalQ, ProportionalQ, ProportionalQ, ProportionalQ, ProportionalQ, ProportionalQ };
    const float defFreq[kBands] { 100.0f, 400.0f, 2500.0f, 8000.0f, 60.0f, 200.0f, 800.0f, 1500.0f, 4000.0f, 6000.0f, 12000.0f, 16000.0f };
    const auto sc = sidechainChoices (kBands);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".b" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            auto& bi = band[s][b];
            bi.on        = addBool (p + "on", n + "On", b < 4);
            bi.shape     = addChoice (p + "shape", n + "Shape", shapeChoices(), defShape[b]);
            bi.freq      = addHz (p + "freq", n + "Freq", 20.0f, 20000.0f, defFreq[b]);
            bi.gain      = addDb (p + "gain", n + "Gain", -30.0f, 15.0f, 0.0f);
            bi.q         = addFloat (p + "q", n + "Q", 0.1f, 40.0f, 1.0f, "", 2, 0.0f);
            bi.slope     = addChoice (p + "slope", n + "Slope", { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 3);
            bi.dyn       = addBool (p + "dyn", n + "Dynamic", false);
            bi.threshold = addDb (p + "threshold", n + "Threshold", -60.0f, 0.0f, -24.0f);
            bi.direction = addChoice (p + "direction", n + "Direction", { "Up", "Down" }, Down);
            bi.attack    = addMs (p + "attack", n + "Attack", 0.1f, 500.0f, 10.0f);
            bi.release   = addMs (p + "release", n + "Release", 5.0f, 2000.0f, 120.0f);
            bi.sidechain = addChoice (p + "sc", n + "Sidechain", sc, 0);
        }
}

void Equalizer::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    lanes.prepare (sr);
    reset();
}

void Equalizer::reset()
{
    lanes.reset();
    for (auto& row : bands) for (auto& b : row) b.reset();
    for (auto& row : detF) for (auto& f : row) f.reset();
    for (auto& row : extF) for (auto& f : row) f.reset();
    for (auto& f : soloF) f.reset();
    for (auto& f : altF) f.reset();
    for (auto& row : env) for (auto& v : row) v = 0.0f;
    for (auto& row : dynGainDb) for (auto& v : row) v = 0.0f;
    for (auto& v : satDc) v = 0.0f;
    lastMode = -1;
    for (auto& a : lastVals) for (auto& b : a) b[0] = -1.0f;
    for (auto& a : lastDetVals) for (auto& b : a) b[0] = -99.0f;
}

bool Equalizer::isDynamic (int set, int b) const
{
    const auto& bi = band[set][b];
    return value (bi.on) > 0.5f && value (bi.dyn) > 0.5f && shapeCanBeDynamic ((int) value (bi.shape));
}

float Equalizer::dynamicTarget (int set, int b, float lev) const
{
    const auto& bi = band[set][b];
    const float g = value (bi.gain), thr = value (bi.threshold);
    // How far past the threshold, 0..1 over 12 dB, with a 3 dB soft start.
    const float over = lev - thr;
    float t = over <= 0.0f ? 0.0f : over < 3.0f ? over * over / 6.0f / 12.0f : (over - 1.5f) / 12.0f;
    t = clampf (t, 0.0f, 1.0f);
    const bool up = (int) value (bi.direction) == Up;
    // UP above zero and DOWN below zero: flat until triggered, then toward the
    // gain. The other two: at the gain, moving to flat when triggered.
    return (up == (g >= 0.0f)) ? g * t : g * (1.0f - t);
}

void Equalizer::updateStatic (bool force)
{
    const int mode = channelMode();
    if (mode != lastMode) force = true;
    lastMode = mode;
    const int nl = Lanes::count (mode);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const auto& bi = band[s][b];
            const float v[6] { value (bi.on), value (bi.shape), value (bi.freq), value (bi.gain), value (bi.q), value (bi.slope) + 10.0f * value (bi.dyn) };
            bool changed = force;
            for (int k = 0; k < 6; ++k)
                if (v[k] != lastVals[s][b][k]) { changed = true; lastVals[s][b][k] = v[k]; }
            if (! changed || isDynamic (s, b)) continue;
            for (int ln = 0; ln < nl; ++ln)
            {
                if (Lanes::setFor (mode, ln) != s) continue;
                if (v[0] > 0.5f)
                    bands[ln][b].design ((int) v[1], v[2], v[3], v[4], (int) value (bi.slope), sr);
                else
                    bands[ln][b].kind = 0;
            }
        }
}

void Equalizer::process (float* l, float* r, int n)
{
    if (! isOn())
        return;
    const int mode = channelMode();
    const int nl = Lanes::count (mode);
    const int groups = Lanes::numGroups (mode);
    const bool sat = value (pSoftSat) > 0.5f;
    const int sBand = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    const float alt = altSoloHz.load (std::memory_order_relaxed);
    const float* scL = ctx.scL;
    const float* scR = ctx.scR;

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        updateStatic (false);

        // Dynamic bands: detectors where their sidechain points, coefficients from now.
        bool anyDyn = false;
        SidechainChoice src[2][kBands];
        bool dynOn[2][kBands];
        float att[2][kBands], rel[2][kBands];
        for (int s = 0; s < 2; ++s)
            for (int b = 0; b < kBands; ++b)
            {
                dynOn[s][b] = isDynamic (s, b);
                if (! dynOn[s][b]) continue;
                anyDyn = true;
                const auto& bi = band[s][b];
                src[s][b] = decodeSidechain ((int) value (bi.sidechain), kBands, b);
                att[s][b] = tauCoef (value (bi.attack), (float) sr);
                rel[s][b] = tauCoef (value (bi.release), (float) sr);
                const int ref = src[s][b].band;
                const float dv[4] { (float) ref + (src[s][b].external ? 100.0f : 0.0f),
                                    ref >= 0 ? value (band[s][ref].shape) : -1.0f, ref >= 0 ? value (band[s][ref].freq) : 0.0f, ref >= 0 ? value (band[s][ref].q) : 0.0f };
                bool ch = false;
                for (int k = 0; k < 4; ++k) if (dv[k] != lastDetVals[s][b][k]) { ch = true; lastDetVals[s][b][k] = dv[k]; }
                if (ch && ref >= 0)
                {
                    for (int ln = 0; ln < Lanes::kMax; ++ln) detF[ln][b].design ((int) dv[1], dv[2], dv[3], sr);
                    extF[s][b].design ((int) dv[1], dv[2], dv[3], sr);
                }
            }
        // Re-design the dynamic bands with the gain they have now.
        if (anyDyn)
            for (int ln = 0; ln < nl; ++ln)
            {
                const int s = Lanes::setFor (mode, ln), g = Lanes::group (mode, ln);
                for (int b = 0; b < kBands; ++b)
                    if (dynOn[s][b])
                    {
                        const auto& bi = band[s][b];
                        bands[ln][b].design ((int) value (bi.shape), value (bi.freq), dynGainDb[g][b], value (bi.q), (int) value (bi.slope), sr);
                    }
            }
        if (sBand >= 0 && sBand != lastSolo)
        {
            const int s = sSet >= 0 ? sSet : 0;
            const auto& bi = band[s][sBand];
            for (auto& f : soloF) f.design ((int) value (bi.shape), value (bi.freq), value (bi.q), sr);
        }
        lastSolo = sBand;
        if (alt > 0.0f && alt != lastAlt)
            for (auto& f : altF) f.design (Bell, alt, altSoloQ, sr);
        lastAlt = alt;

        for (int i = start; i < start + len; ++i)
        {
            float x[Lanes::kMax];
            lanes.split (mode, l[i], r[i], x);
            if (anyDyn)
            {
                const float ext = (scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f);
                for (int g = 0; g < groups; ++g)
                {
                    const int s = mode == Stereo ? 0 : g;
                    for (int b = 0; b < kBands; ++b)
                    {
                        if (! dynOn[s][b]) continue;
                        const auto& c = src[s][b];
                        float a;
                        if (c.external)
                            a = std::fabs (c.band >= 0 ? extF[s][b].tick (ext) : ext);
                        else
                        {
                            auto sig = [&] (int ln) { return c.band >= 0 ? detF[ln][b].tick (x[ln]) : x[ln]; };
                            if (mode == Stereo)       a = std::max (std::fabs (sig (0)), std::fabs (sig (1)));
                            else if (mode == MidSide) a = std::fabs (sig (g));
                            else                      a = std::max (std::fabs (sig (2 * g)), std::fabs (sig (2 * g + 1)));
                        }
                        float& e = env[g][b];
                        e += (a > e ? att[s][b] : rel[s][b]) * (a - e);
                        if (i == start + len - 1)
                        {
                            const float lev = gainToDb (e * 1.4142f);   // peak of a sine reads as its level
                            levelDb[g][b] = lev;
                            dynGainDb[g][b] = dynamicTarget (s, b, lev);
                        }
                    }
                }
            }
            for (int ln = 0; ln < nl; ++ln)
            {
                float v = x[ln];
                for (int b = 0; b < kBands; ++b) v = bands[ln][b].tick (v);
                if (sat)
                {
                    v = softTanh (v + 0.05f) - softTanh (0.05f);
                    satDc[ln] += 0.0005f * (v - satDc[ln]);
                    v -= satDc[ln];
                }
                if (sBand >= 0 && (sSet < 0 || mode == Stereo || Lanes::setFor (mode, ln) == sSet))
                    v = soloF[ln].tick (v);
                if (alt > 0.0f)
                    v = altF[ln].tick (v);
                x[ln] = v;
            }
            if (sBand < 0) LaneSplitter::soloSet (mode, x, sSet);
            LaneSplitter::join (mode, x, l[i], r[i]);
        }
    }
    if (mode == Stereo)
        for (int b = 0; b < kBands; ++b) { dynGainDb[1][b] = dynGainDb[0][b]; levelDb[1][b] = levelDb[0][b]; }
}

void Equalizer::bandCurveDb (int set, int b, const float* hz, float* outDb, int count, bool dynamic) const
{
    const auto& bi = band[set][b];
    if (value (bi.on) < 0.5f)
    {
        for (int i = 0; i < count; ++i) outDb[i] = 0.0f;
        return;
    }
    FilterBand f;
    const int shape = (int) value (bi.shape);
    float g = shapeHasGain (shape) ? value (bi.gain) : 0.0f;
    if (dynamic && isDynamic (set, b)) g = dynGainDb[set][b];
    f.design (shape, value (bi.freq), g, value (bi.q), (int) value (bi.slope), sr);
    for (int i = 0; i < count; ++i)
        outDb[i] = (float) f.responseDb (hz[i], sr);
}

void Equalizer::curveDb (int set, const float* hz, float* outDb, int count, bool dynamic) const
{
    for (int i = 0; i < count; ++i) outDb[i] = 0.0f;
    std::vector<float> tmp ((size_t) count);
    for (int b = 0; b < kBands; ++b)
    {
        bandCurveDb (set, b, hz, tmp.data(), count, dynamic);
        for (int i = 0; i < count; ++i) outDb[i] += tmp[(size_t) i];
    }
}
} // namespace planish
