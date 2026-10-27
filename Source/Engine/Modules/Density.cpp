#include "Density.h"

namespace planish
{
Density::Density()
    : Module ({ "den", "DENSITY", "DENSITY", 0xff4ccaa0u, kMidSideBit | kTransSusBit })
{
    pChMode = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit | kTransSusBit), 0);
    mb.declare (*this, 200.0f, 3000.0f);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < kBands; ++b)
        {
            const std::string p = std::string (s == 0 ? "a" : "b") + ".band" + std::to_string (b + 1) + ".";
            const std::string n = std::string (s == 0 ? "A " : "B ") + "Band " + std::to_string (b + 1) + " ";
            band[s][b].threshold = addDb (p + "threshold", n + "Threshold", -60.0f, 0.0f, -30.0f);
            band[s][b].range     = addDb (p + "range", n + "Range", 0.0f, 20.0f, 6.0f);
            band[s][b].ratio     = addFloat (p + "ratio", n + "Ratio", 1.0f, 10.0f, 2.0f, ":1", 1, 0.0f);
            band[s][b].speed     = addMs (p + "speed", n + "Speed", 1.0f, 300.0f, 50.0f);
        }
}

float Density::staticBoostDb (int set, int b, float level) const
{
    const float thr = value (band[set][b].threshold);
    const float under = thr - level;
    if (under <= 0.0f) return 0.0f;
    const float amt = 1.0f - 1.0f / std::max (1.0f, value (band[set][b].ratio));
    // Silence stays silent: fade the boost out from 40 to 60 dB under the threshold.
    const float fade = clampf ((60.0f - under) / 20.0f, 0.0f, 1.0f);
    return std::min (value (band[set][b].range), amt * under) * fade;
}

void Density::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    lanes.prepare (sr);
    lookahead = std::max (1, (int) std::lround (kLookaheadMs * 0.001 * sr));
    for (auto& row : st)
        for (auto& s : row) { s.det.prepare ((float) sr); s.det.mode = DetectTrue; s.gs.prepare (lookahead); }
    for (auto& row : delay) for (auto& d : row) d.prepare (lookahead + 2);
    lastWindow = -1;
    reset();
}

void Density::reset()
{
    split.reset();
    lanes.reset();
    for (auto& row : st) for (auto& s : row) { s.det.reset(); s.gs.reset(); }
    for (auto& row : delay) for (auto& d : row) d.reset();
    for (auto& row : boostDb) for (auto& v : row) v = 0.0f;
}

void Density::process (float* l, float* r, int n)
{
    const int win = ctx.zeroLatency ? 1 : lookahead;
    if (win != lastWindow)
    {
        lastWindow = win;
        for (auto& row : st) for (auto& s : row) s.gs.setWindow (win);
        for (auto& row : delay) for (auto& d : row) d.setDelay (ctx.zeroLatency ? 0 : lookahead);
    }
    if (! isOn())
    {
        if (! ctx.zeroLatency)
            for (int i = 0; i < n; ++i) { l[i] = delay[0][0].tick (l[i]); r[i] = delay[1][0].tick (r[i]); }
        for (auto& row : boostDb) for (auto& v : row) v = 0.0f;
        return;
    }
    const int mode = channelMode();
    const int nl = Lanes::count (mode);
    const int groups = Lanes::numGroups (mode);
    const int nb = mb.numBands (*this);
    const int solo = soloBand.load (std::memory_order_relaxed);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    constexpr float kHead = 20.0f;   // boosts are carried as gains under 1, 20 dB down

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        for (int g = 0; g < groups; ++g)
            for (int b = 0; b < nb; ++b)
                st[g][b].gs.setRelease (value (band[Lanes::setFor (mode, g * (mode == TransSus ? 2 : 1))][b].speed), (float) sr);

        for (int i = start; i < start + len; ++i)
        {
            float x[Lanes::kMax], bands[Lanes::kMax][4];
            lanes.split (mode, l[i], r[i], x);
            for (int ln = 0; ln < nl; ++ln) split.split (ln, x[ln], bands[ln]);
            float gain[2][kBands];
            for (int g = 0; g < groups; ++g)
            {
                const int set = mode == Stereo ? 0 : g;
                for (int b = 0; b < nb; ++b)
                {
                    float a;
                    if (mode == Stereo)        a = std::max (std::fabs (bands[0][b]), std::fabs (bands[1][b]));
                    else if (mode == MidSide)  a = bands[g][b];
                    else                       a = std::max (std::fabs (bands[2 * g][b]), std::fabs (bands[2 * g + 1][b]));
                    auto& s = st[g][b];
                    const float lev = s.det.next (a);
                    const float target = value (mb.pBandOn[b]) > 0.5f ? staticBoostDb (set, b, lev) : 0.0f;
                    const float held = s.gs.tick (dbToGain (target - kHead));
                    const float boost = gainToDb (held) + kHead;
                    gain[g][b] = dbToGain (std::max (0.0f, boost));
                    if (i == start) { boostDb[g][b] = std::max (0.0f, boost); levelDb[g][b] = lev; }
                }
            }
            for (int ln = 0; ln < nl; ++ln)
            {
                const int g = Lanes::group (mode, ln);
                float sum = 0.0f;
                for (int b = 0; b < nb; ++b)
                {
                    const float v = delay[ln][b].tick (bands[ln][b]) * gain[g][b];
                    if (solo < 0 || solo == b) sum += v;
                }
                x[ln] = sum;
            }
            LaneSplitter::soloSet (mode, x, sSet);
            LaneSplitter::join (mode, x, l[i], r[i]);
        }
    }
}
} // namespace planish
