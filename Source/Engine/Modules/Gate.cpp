#include "Gate.h"

namespace planish
{
Gate::Gate()
    : Module ({ "gate", "GATE", "GATE", 0xff9ad75cu, kStereoOnly })
{
    mb.declare (*this, 200.0f, 3000.0f);
    det.declare (*this);
    const auto sc = sidechainChoices (kBands);
    for (int b = 0; b < kBands; ++b)
    {
        const std::string p = "band" + std::to_string (b + 1) + ".";
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        auto& bi = band[b];
        bi.open    = addDb (p + "open", n + "Open Threshold", -80.0f, 0.0f, -40.0f);
        bi.close   = addDb (p + "close", n + "Close Threshold", -80.0f, 0.0f, -46.0f);
        bi.ratio   = addFloat (p + "ratio", n + "Ratio", 1.0f, 100.0f, 20.0f, ":1", 1, 0.0f);
        bi.attack  = addMs (p + "attack", n + "Attack", 0.01f, 100.0f, 0.5f);
        bi.hold    = addMs (p + "hold", n + "Hold", 0.0f, 2000.0f, 40.0f);
        bi.release = addMs (p + "release", n + "Release", 1.0f, 5000.0f, 120.0f);
        bi.sidechain = addChoice (p + "sc", n + "Sidechain", sc, 0);
    }
}

void Gate::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    scSplit.prepare (sr);
    peakRel = tauCoef (8.0f, (float) sr);
    reset();
}

void Gate::reset()
{
    split.reset();
    scSplit.reset();
    for (auto& row : detF) for (auto& f : row) f.reset();
    for (auto& s : st) s = {};
    for (auto& t : tm) t = {};
    lastDet[0] = -1.0f;
}

float Gate::closedGainDb (int b, float levelDb) const
{
    const float ratio = value (band[b].ratio);
    return std::max (-80.0f, std::min (0.0f, (levelDb - value (band[b].open)) * (ratio - 1.0f)));
}

void Gate::process (float* l, float* r, int n)
{
    if (! isOn())
        return;
    const int nb = mb.numBands (*this);
    const int solo = soloBand.load (std::memory_order_relaxed);
    const bool dsolo = detSolo.load (std::memory_order_relaxed);
    const bool detOn = value (det.pOn) > 0.5f;
    const float* scL = ctx.scL;
    const float* scR = ctx.scR;

    float minGain[kBands] { 0.0f, 0.0f, 0.0f };
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        scSplit.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        {
            const float dv[5] { value (det.pHpFreq), value (det.pHpGain), value (det.pLpFreq), value (det.pLpGain), 1.0f };
            bool ch = false;
            for (int i = 0; i < 5; ++i) if (dv[i] != lastDet[i]) { ch = true; lastDet[i] = dv[i]; }
            if (ch)
                for (auto& row : detF) for (auto& f : row) f.design (dv[0], dv[1], dv[2], dv[3], sr);
        }
        float att[kBands], rel[kBands];
        int holdSamples[kBands];
        SidechainChoice src[kBands];
        for (int b = 0; b < nb; ++b)
        {
            att[b] = tauCoef (value (band[b].attack), (float) sr);
            rel[b] = tauCoef (value (band[b].release), (float) sr);
            holdSamples[b] = (int) (value (band[b].hold) * 0.001f * (float) sr);
            src[b] = decodeSidechain ((int) value (band[b].sidechain), nb, b);
        }

        for (int i = start; i < start + len; ++i)
        {
            float bl[4], br[4], el[4] {}, er[4] {};
            split.split (0, l[i], bl);
            split.split (1, r[i], br);
            const float exL = scL != nullptr ? scL[i] : 0.0f, exR = scR != nullptr ? scR[i] : 0.0f;
            scSplit.split (0, exL, el);
            scSplit.split (1, exR, er);

            float outL = 0.0f, outR = 0.0f;
            for (int b = 0; b < nb; ++b)
            {
                const auto& s = src[b];
                float d0, d1;
                if (s.external) { d0 = s.band < 0 ? exL : el[s.band]; d1 = s.band < 0 ? exR : er[s.band]; }
                else            { d0 = s.band < 0 ? l[i] : bl[s.band]; d1 = s.band < 0 ? r[i] : br[s.band]; }
                if (detOn) { d0 = detF[b][0].tick (d0); d1 = detF[b][1].tick (d1); }
                if (dsolo)
                {
                    if (solo < 0 || solo == b) { outL += d0; outR += d1; }
                    continue;
                }
                auto& bs = st[b];
                const float a = std::max (std::fabs (d0), std::fabs (d1));
                if (a > bs.peak) bs.peak = a; else bs.peak += peakRel * (a - bs.peak);
                const float lev = gainToDb (bs.peak);
                if (lev >= value (band[b].open)) { bs.open = true; bs.holdLeft = holdSamples[b]; }
                else if (lev < value (band[b].close))
                {
                    if (bs.holdLeft > 0) --bs.holdLeft;
                    else bs.open = false;
                }
                const bool bandOn = value (mb.pBandOn[b]) > 0.5f;
                const float target = (bs.open || ! bandOn) ? 0.0f : closedGainDb (b, lev);
                bs.gainDb += (target > bs.gainDb ? att[b] : rel[b]) * (target - bs.gainDb);
                minGain[b] = std::min (minGain[b], bs.gainDb);
                const float g = dbToGain (bs.gainDb);
                if (solo < 0 || solo == b) { outL += bl[b] * g; outR += br[b] * g; }
                if (i == start) { tm[b].levelDb = lev; tm[b].gainDb = bs.gainDb; tm[b].open = bs.open; }
            }
            l[i] = outL; r[i] = outR;
        }
    }
    for (int b = 0; b < kBands; ++b) tm[b].minGainDb = minGain[b];
}
} // namespace planish
