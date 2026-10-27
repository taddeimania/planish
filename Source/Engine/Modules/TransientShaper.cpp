#include "TransientShaper.h"

namespace planish
{
TransientShaper::TransientShaper()
    : Module ({ "trs", "TRANSIENT SHAPER", "TRANSIENT", 0xffff7088u, kMidSideBit })
{
    pChMode  = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit), 0);
    pEnvMode = addChoice ("envmode", "Envelope Mode", { "Precise", "Balanced", "Loose" }, Balanced);
    mb.declare (*this, 150.0f, 2500.0f);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            band[s][b].attack  = addDb (p + "attack", n + "Attack", -15.0f, 15.0f, 0.0f);
            band[s][b].sustain = addDb (p + "sustain", n + "Sustain", -15.0f, 15.0f, 0.0f);
            band[s][b].contour = addChoice (p + "contour", n + "Contour", { "Sharp", "Medium", "Smooth" }, Medium);
        }
}

void TransientShaper::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    smooth = tauCoef (0.6f, (float) sr);
    reset();
}

void TransientShaper::reset()
{
    split.reset();
    for (auto& row : st) for (auto& s : row) { s.fast.reset(); s.slow.reset(); s.sus.reset(); s.g = 0.0f; }
    for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
    lastTimes[0] = -1;
}

void TransientShaper::process (float* l, float* r, int n)
{
    for (auto& row : peakGainDb) for (auto& v : row) v = 0.0f;
    if (! isOn())
        return;
    const int mode = channelMode();
    const int nb = mb.numBands (*this);
    const int env = (int) value (pEnvMode);
    const int solo = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    const float modeScale = env == Precise ? 0.6f : env == Balanced ? 1.0f : 1.9f;
    const float slowAttack = env == Precise ? 10.0f : env == Balanced ? 18.0f : 32.0f;

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        float att[2][kBands], sus[2][kBands];
        for (int g = 0; g < 2; ++g)
        {
            const int s = Lanes::setFor (mode, g);
            for (int b = 0; b < nb; ++b)
            {
                const int contour = (int) value (band[s][b].contour);
                const float relFast = (contour == Sharp ? 15.0f : contour == Medium ? 45.0f : 120.0f) * modeScale;
                auto& stt = st[g][b];
                stt.fast.set (0.2f, relFast, (float) sr);
                stt.slow.set (slowAttack, relFast, (float) sr);
                stt.sus.set (0.2f, 260.0f * modeScale, (float) sr);
                att[g][b] = value (band[s][b].attack);
                sus[g][b] = value (band[s][b].sustain);
            }
        }

        for (int i = start; i < start + len; ++i)
        {
            float x[2];
            if (mode == MidSide) { x[0] = 0.5f * (l[i] + r[i]); x[1] = 0.5f * (l[i] - r[i]); }
            else { x[0] = l[i]; x[1] = r[i]; }
            float bands[2][4];
            split.split (0, x[0], bands[0]);
            split.split (1, x[1], bands[1]);
            float out[2] { 0.0f, 0.0f };
            const int groups = Lanes::numGroups (mode);
            for (int b = 0; b < nb; ++b)
            {
                const bool bandOn = value (mb.pBandOn[b]) > 0.5f;
                for (int g = 0; g < groups; ++g)
                {
                    auto& stt = st[g][b];
                    const float a = mode == Stereo ? std::max (std::fabs (bands[0][b]), std::fabs (bands[1][b])) : std::fabs (bands[g][b]);
                    const float f = stt.fast.next (a), s = stt.slow.next (a), su = stt.sus.next (a);
                    float target = 0.0f;
                    if (bandOn && f > 3.0e-4f)
                    {
                        const float attW = clampf ((gainToDb (f) - gainToDb (s)) / 12.0f, 0.0f, 1.0f);
                        const float susW = clampf ((gainToDb (su) - gainToDb (f)) / 12.0f, 0.0f, 1.0f);
                        target = att[g][b] * attW + sus[g][b] * susW;
                    }
                    stt.g += smooth * (target - stt.g);
                    if (std::fabs (stt.g) > std::fabs (peakGainDb[g][b])) peakGainDb[g][b] = stt.g;
                    if (i == start) gainDb[g][b] = stt.g;
                }
                const float g0 = dbToGain (st[0][b].g);
                const float g1 = mode == Stereo ? g0 : dbToGain (st[1][b].g);
                const bool audible = solo < 0 || solo == b;
                if (audible)
                {
                    out[0] += bands[0][b] * g0;
                    out[1] += bands[1][b] * g1;
                }
            }
            if (mode == MidSide)
            {
                if (sSet == 0) out[1] = 0.0f; else if (sSet == 1) out[0] = 0.0f;
                l[i] = out[0] + out[1]; r[i] = out[0] - out[1];
            }
            else { l[i] = out[0]; r[i] = out[1]; }
        }
    }
}
} // namespace planish
