#include "Compressor.h"

namespace planish
{
Compressor::Compressor (int instance)
    : Module ({ instance == 1 ? "cmp1" : "cmp2", instance == 1 ? "COMPRESSOR 1" : "COMPRESSOR 2",
                instance == 1 ? "COMP 1" : "COMP 2", instance == 1 ? 0xffffae42u : 0xffffc46au, kStereoOnly }),
      instanceNo (instance)
{
    pMode     = addChoice ("mode", "Mode", { "Punch", "Modern", "Vintage" }, Modern);
    pDetect   = addChoice ("detect", "Level Detection", { "RMS", "Peak", "True" }, DetectRms);
    pAutoGain = addBool ("autogain", "Auto Gain", false);
    mb.declare (*this, 200.0f, 3000.0f);
    det.declare (*this);
    const auto sc = sidechainChoices (kBands);
    for (int b = 0; b < kBands; ++b)
    {
        const std::string p = "band" + std::to_string (b + 1) + ".";
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        auto& bi = band[b];
        bi.threshold   = addDb (p + "threshold", n + "Threshold", -60.0f, 0.0f, -20.0f);
        bi.knee        = addDb (p + "knee", n + "Knee", 0.0f, 24.0f, 6.0f);
        bi.ratio       = addFloat (p + "ratio", n + "Ratio", 0.5f, 20.0f, 2.0f, ":1", 2, 0.0f);
        bi.attack      = addMs (p + "attack", n + "Attack", 0.1f, 300.0f, 10.0f);
        bi.release     = addMs (p + "release", n + "Release", 5.0f, 3000.0f, 120.0f);
        bi.autoRelease = addBool (p + "autorelease", n + "Auto Release", false);
        bi.makeup      = addDb (p + "makeup", n + "Makeup Gain", -24.0f, 24.0f, 0.0f);
        bi.mix         = addPct (p + "mix", n + "Mix", 0.0f, 100.0f, 100.0f);
        bi.amount      = addPct (p + "amount", n + "Punch Amount", 0.0f, 200.0f, 100.0f);
        bi.punchAttack = addMs (p + "punchattack", n + "Punch Attack", 0.1f, 100.0f, 5.0f);
        bi.punchSustain = addMs (p + "punchsustain", n + "Punch Sustain", 5.0f, 1000.0f, 120.0f);
        bi.sidechain   = addChoice (p + "sc", n + "Sidechain", sc, 0);
    }
}

void Compressor::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    scSplit.prepare (sr);
    for (auto& b : st)
    {
        b.detL.prepare ((float) sr); b.detR.prepare ((float) sr);
        b.bal.prepare ((float) sr);
        b.ag.prepare ((float) sr);
        b.fastEnv.set (1.0f, 50.0f, (float) sr);
        b.slowEnv.set (50.0f, 400.0f, (float) sr);
    }
    for (auto& s : makeupSm) s.setTime (30.0f, (float) sr, kStep);
    for (auto& s : mixSm) s.setTime (30.0f, (float) sr, kStep);
    reset();
}

void Compressor::reset()
{
    split.reset();
    scSplit.reset();
    for (auto& row : detF) for (auto& f : row) f.reset();
    for (auto& b : st)
    {
        b.detL.reset(); b.detR.reset(); b.bal.reset(); b.ag.reset(); b.fastEnv.reset(); b.slowEnv.reset();
        b.crest = 0.0f; b.fbGain = 1.0f; b.avgDb = -100.0f; b.punchDb = 0.0f;
    }
    for (int b = 0; b < kBands; ++b) { makeupSm[b].snap (value (band[b].makeup)); mixSm[b].snap (value (band[b].mix) * 0.01f); }
    for (auto& t : tm) t = {};
    lastDet[0] = -1.0f;
    scope.clear();
}

float Compressor::staticGainDb (int b, float levelDb) const
{
    const auto& bi = band[b];
    const float ratio = value (bi.ratio);
    const float knee = (int) value (pMode) == Vintage ? 8.0f : value (bi.knee);
    const float thr = value (bi.threshold);
    if (ratio >= 1.0f)
        return downwardGainDb (levelDb, thr, ratio, knee);
    // Below 1:1: an expander, pushing what exceeds the threshold up.
    return upwardExpandGainDb (levelDb, thr, 1.0f / std::max (0.05f, ratio), knee);
}

void Compressor::sidechainDefaults (int b)
{
    const auto& bi = band[b];
    setValue (bi.sidechain, (float) (2 + kBands));   // Ext. Full
    setValue (bi.attack, 1.0f);
    setValue (bi.release, 80.0f);
    setValue (bi.ratio, 4.0f);
    setValue (bi.threshold, -30.0f);
    setValue (pAutoGain, 0.0f);
    setValue (pDetect, (float) DetectPeak);
}

void Compressor::process (float* l, float* r, int n)
{
    const double ppqPerSample = ctx.bpm / 60.0 / sr;
    if (! isOn())
    {
        for (int i = 0; i < n; ++i)
            scope.push (0.5f * (l[i] + r[i]), ctx.scL != nullptr ? 0.5f * (ctx.scL[i] + ctx.scR[i]) : 0.0f, ctx.ppq + i * ppqPerSample);
        return;
    }
    const int nb = mb.numBands (*this);
    const int mode = (int) value (pMode);
    const int detMode = (int) value (pDetect);
    const bool autoGain = value (pAutoGain) > 0.5f;
    const int solo = soloBand.load (std::memory_order_relaxed);
    const bool dsolo = detSolo.load (std::memory_order_relaxed);
    const bool detOn = value (det.pOn) > 0.5f;
    const float* scL = ctx.scL;
    const float* scR = ctx.scR;
    bool anyExternal = false;

    float minGain[kBands] { 0.0f, 0.0f, 0.0f };
    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        scSplit.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        {
            const float dv[4] { value (det.pHpFreq), value (det.pHpGain), value (det.pLpFreq), value (det.pLpGain) };
            bool ch = false;
            for (int i = 0; i < 4; ++i) if (dv[i] != lastDet[i]) { ch = true; lastDet[i] = dv[i]; }
            if (ch)
                for (auto& row : detF) for (auto& f : row) f.design (dv[0], dv[1], dv[2], dv[3], sr);
        }
        SidechainChoice src[kBands];
        float punchAtt[kBands], punchRel[kBands], avgAtt = tauCoef (80.0f, (float) sr), avgRel = tauCoef (300.0f, (float) sr);
        float gMakeup[kBands], gMix[kBands];
        for (int b = 0; b < nb; ++b)
        {
            auto& bs = st[b];
            src[b] = decodeSidechain ((int) value (band[b].sidechain), nb, b);
            anyExternal = anyExternal || src[b].external;
            bs.detL.mode = bs.detR.mode = detMode;
            float rel = value (band[b].release);
            if (mode == Modern && value (band[b].autoRelease) > 0.5f)
                rel *= clampf (1.6f - bs.crest * 1.2f, 0.35f, 1.6f);   // transients: shorter; sustained: longer
            if (mode == Vintage)
                rel *= 1.0f + 1.5f * clampf (-bs.bal.gainDb / 12.0f, 0.0f, 1.0f);   // slows under heavy compression
            bs.bal.set (value (band[b].attack), rel);
            punchAtt[b] = tauCoef (value (band[b].punchAttack), (float) sr);
            punchRel[b] = tauCoef (value (band[b].punchSustain), (float) sr);
            makeupSm[b].target = value (band[b].makeup);
            mixSm[b].target = value (band[b].mix) * 0.01f;
            gMakeup[b] = dbToGain (makeupSm[b].next());
            gMix[b] = mixSm[b].next();
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
                auto& bs = st[b];
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
                // Vintage detects on its own output (feedback).
                if (mode == Vintage && ! s.external) { d0 *= bs.fbGain; d1 *= bs.fbGain; }
                const float lev = std::max (bs.detL.next (d0), bs.detR.next (d1));
                if (mode == Modern && value (band[b].autoRelease) > 0.5f)
                {
                    const float ad = 0.5f * (std::fabs (d0) + std::fabs (d1));
                    const float f = bs.fastEnv.next (ad), sl = bs.slowEnv.next (ad);
                    bs.crest = clampf (f / (sl + 1e-6f) - 1.0f, 0.0f, 1.0f);
                }

                const bool bandOn = value (mb.pBandOn[b]) > 0.5f;
                float gDb = 0.0f;
                if (bandOn)
                {
                    if (mode == Punch)
                    {
                        // The hits relative to the programme level: below 100% they
                        // are pulled in, above 100% pushed out.
                        bs.avgDb += (lev > bs.avgDb ? avgAtt : avgRel) * (lev - bs.avgDb);
                        const float amt = value (band[b].amount) * 0.01f - 1.0f;
                        const float hit = std::max (0.0f, lev - bs.avgDb);
                        const float quiet = clampf ((lev + 70.0f) / 20.0f, 0.0f, 1.0f);
                        const float target = clampf (amt * hit * 1.2f, -18.0f, 9.0f) * quiet;
                        bs.punchDb += (std::fabs (target) > std::fabs (bs.punchDb) ? punchAtt[b] : punchRel[b]) * (target - bs.punchDb);
                        gDb = bs.punchDb;
                    }
                    else
                    {
                        // Attack while the gain moves away from 0 dB (either way), release back.
                        const float t = std::min (12.0f, staticGainDb (b, lev));
                        auto& gb = bs.bal.gainDb;
                        gb += (std::fabs (t) > std::fabs (gb) ? bs.bal.attCoef : bs.bal.relCoef) * (t - gb);
                        gDb = gb;
                    }
                }
                bs.fbGain = dbToGain (gDb);
                const float g = bs.fbGain;
                float wl = bl[b] * g, wr = br[b] * g;
                if (mode == Vintage)
                {
                    // Colour that grows with the compression.
                    const float k = 0.6f + 0.6f * clampf (-gDb / 12.0f, 0.0f, 1.0f);
                    wl = softTanh (wl * k) / k;
                    wr = softTanh (wr * k) / k;
                }
                const float m = gMix[b];
                wl = wl * m + bl[b] * (1.0f - m);
                wr = wr * m + br[b] * (1.0f - m);
                wl *= gMakeup[b]; wr *= gMakeup[b];
                if (autoGain && bandOn)
                {
                    bs.ag.push (0.5f * (bl[b] + br[b]), 0.5f * (wl + wr));
                    const float ag = dbToGain (bs.ag.gainDb);
                    wl *= ag; wr *= ag;
                }
                else bs.ag.gainDb = 0.0f;
                minGain[b] = std::min (minGain[b], gDb);
                if (solo < 0 || solo == b) { outL += wl; outR += wr; }
                if (i == start) { tm[b].levelDb = lev; tm[b].gainDb = gDb; tm[b].autoDb = bs.ag.gainDb; }
            }
            l[i] = outL; r[i] = outR;
            scope.push (0.5f * (outL + outR), 0.5f * (exL + exR), ctx.ppq + i * ppqPerSample);
        }
    }
    sidechainActive = anyExternal;
    for (int b = 0; b < kBands; ++b) tm[b].minGainDb = minGain[b];
}
} // namespace planish
