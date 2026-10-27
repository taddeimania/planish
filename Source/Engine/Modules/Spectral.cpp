#include "Spectral.h"

namespace planish
{
// ===========================================================================
// SpectralModule
// ===========================================================================
void SpectralModule::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    stft.prepare (kFrame, kHop, 4);
    scStft.prepare (kFrame, kHop, 1);
    lanes.prepare (sr);
    firstBin.assign (kBands, 1);
    lastBin.assign (kBands, 1);
    for (int b = 0; b < kBands; ++b)
    {
        int lo = (int) std::ceil (SpectralBands::edgeHz (b) * kFrame / sr);
        int hi = (int) std::floor (SpectralBands::edgeHz (b + 1) * kFrame / sr);
        lo = std::max (1, std::min (kFrame / 2, lo));
        hi = std::max (lo, std::min (kFrame / 2, hi));
        firstBin[(size_t) b] = lo;
        lastBin[(size_t) b] = hi;
    }
    // Narrow low bands may hold no bin of their own: give them the nearest.
    for (int b = 0; b < kBands; ++b)
        if (lastBin[(size_t) b] < firstBin[(size_t) b]) lastBin[(size_t) b] = firstBin[(size_t) b];
    binPos.assign ((size_t) kFrame / 2 + 1, 0.0f);
    for (int k = 0; k <= kFrame / 2; ++k)
        binPos[(size_t) k] = clampf (SpectralBands::bandPos ((float) (k * sr / kFrame)), 0.0f, (float) (kBands - 1));
    for (auto& b : scBuf) b.assign ((size_t) kHop, 0.0f);
    for (auto& d : zlDelay) d.prepare (4);
    reset();
}

void SpectralModule::reset()
{
    stft.reset();
    scStft.reset();
    lanes.reset();
    for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
    for (auto& row : specDb) for (auto& v : row) v = -100.0f;
    for (auto& v : scSpecDb) v = -100.0f;
    sidechainHasSignal = false;
}

void SpectralModule::bandLevels (const Stft::cpx* bins, float* outDb) const
{
    for (int b = 0; b < kBands; ++b)
    {
        double sum = 0;
        for (int k = firstBin[(size_t) b]; k <= lastBin[(size_t) b]; ++k) sum += std::norm (bins[k]);
        const double mean = sum / std::max (1, lastBin[(size_t) b] - firstBin[(size_t) b] + 1);
        // Pink noise falls 3 dB per octave per bin: add it back so pink reads flat.
        outDb[b] = 10.0f * std::log10 ((float) mean + 1e-14f) + 3.0f * std::log2 (SpectralBands::centreHz (b) / 1000.0f);
    }
}

void SpectralModule::onFrame (Stft::cpx** bins, int chans, int nb, int mode)
{
    const int groups = Lanes::numGroups (mode);
    float frameDb[2][kBands];
    float tmp[kBands];
    for (int g = 0; g < groups; ++g)
    {
        // The lanes of the group, their power averaged.
        int members[2] { 0, 1 }, count = 2;
        if (mode == MidSide) { members[0] = g; count = 1; }
        else if (mode == TransSus) { members[0] = 2 * g; members[1] = 2 * g + 1; }
        double acc[kBands] {};
        for (int m = 0; m < count; ++m)
        {
            bandLevels (bins[members[m]], tmp);
            for (int b = 0; b < kBands; ++b) acc[b] += std::pow (10.0, tmp[b] * 0.1);
        }
        for (int b = 0; b < kBands; ++b) frameDb[g][b] = 10.0f * (float) std::log10 (acc[b] / count + 1e-14);
        const int s = mode == Stereo ? 0 : g;
        computeGains (g, s, frameDb[g], gainDb[g]);
        // The range: no change outside it, with soft third-octave edges.
        const float lo = value (range[s].lo), hi = std::max (value (range[s].lo) * 1.05f, value (range[s].hi));
        for (int b = 0; b < kBands; ++b)
        {
            const float hz = SpectralBands::centreHz (b);
            const float edge = std::min (clampf (std::log2 (hz / lo) * 3.0f + 1.0f, 0.0f, 1.0f),
                                         clampf (std::log2 (hi / hz) * 3.0f + 1.0f, 0.0f, 1.0f));
            gainDb[g][b] *= edge;
        }
    }
    if (mode == Stereo) for (int b = 0; b < kBands; ++b) gainDb[1][b] = gainDb[0][b];

    const bool solo = soloRange.load (std::memory_order_relaxed);
    for (int c = 0; c < chans; ++c)
    {
        const int g = Lanes::group (mode, c);
        const int s = mode == Stereo ? 0 : g;
        const float* gd = gainDb[g];
        const float lo = value (range[s].lo), hi = value (range[s].hi);
        for (int k = 0; k < nb; ++k)
        {
            const float pos = binPos[(size_t) k];
            const int i = std::min (kBands - 2, (int) pos);
            const float f = pos - (float) i;
            float gl = dbToGain (gd[i] + (gd[i + 1] - gd[i]) * f);
            if (solo)
            {
                const float hz = (float) (k * sr / kFrame);
                if (hz < lo || hz > hi) gl = 0.0f;
            }
            bins[c][k] *= gl;
        }
    }
}

void SpectralModule::process (float* l, float* r, int n)
{
    if (ctx.zeroLatency)
    {
        // ZERO LATENCY switches the spectral modules off.
        for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
        return;
    }
    const bool on = isOn();
    const int mode = channelMode();
    const int nl = Lanes::count (mode);
    const int sSet = soloSet.load (std::memory_order_relaxed);
    const bool needSc = wantsSidechain();

    float laneBuf[Lanes::kMax][kHop];
    for (int start = 0; start < n; start += kHop)
    {
        const int len = std::min (kHop, n - start);
        // The sidechain's analysis first, so its frame is ready when the
        // track's frame (at the same sample) needs it.
        if (needSc)
        {
            for (int i = 0; i < len; ++i)
                scBuf[0][(size_t) i] = ctx.scL != nullptr ? 0.5f * (ctx.scL[start + i] + ctx.scR[start + i]) : 0.0f;
            float* d[1] { scBuf[0].data() };
            scStft.process (d, len, [this] (Stft::cpx** bins, int, int)
            {
                bandLevels (bins[0], scSpecDb);
                float mx = -200.0f;
                for (int b = 0; b < kBands; ++b) mx = std::max (mx, scSpecDb[b]);
                sidechainHasSignal = mx > -110.0f;
            });
        }
        for (int i = 0; i < len; ++i)
        {
            float x[Lanes::kMax];
            if (on) lanes.split (mode, l[start + i], r[start + i], x);
            else { x[0] = l[start + i]; x[1] = r[start + i]; x[2] = x[3] = 0.0f; }
            for (int c = 0; c < Lanes::kMax; ++c) laneBuf[c][i] = c < (on ? nl : 2) ? x[c] : 0.0f;
        }
        float* d[Lanes::kMax] { laneBuf[0], laneBuf[1], laneBuf[2], laneBuf[3] };
        if (on)
            stft.process (d, len, [this, mode] (Stft::cpx** bins, int chans, int nb) { onFrame (bins, chans, nb, mode); });
        else
            stft.process (d, len, [] (Stft::cpx**, int, int) {});
        for (int i = 0; i < len; ++i)
        {
            float x[Lanes::kMax] { laneBuf[0][i], laneBuf[1][i], laneBuf[2][i], laneBuf[3][i] };
            if (on)
            {
                LaneSplitter::soloSet (mode, x, sSet);
                LaneSplitter::join (mode, x, l[start + i], r[start + i]);
            }
            else { l[start + i] = x[0]; r[start + i] = x[1]; }
        }
    }
    if (! on) for (auto& row : gainDb) for (auto& v : row) v = 0.0f;
}

// ===========================================================================
// Sculptor
// ===========================================================================
Sculptor::Sculptor()
    : SpectralModule ({ "scu", "SCULPTOR", "SCULPTOR", 0xffa98dffu, kMidSideBit | kTransSusBit })
{
    pChMode = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit | kTransSusBit), 0);
    std::vector<std::string> t;
    for (int i = 0; i < InstrumentTargets::NumTargets; ++i) t.push_back (InstrumentTargets::name (i));
    t.push_back ("Custom");
    pTarget = addChoice ("target", "Target", t, InstrumentTargets::FullMix);
    for (int s = 0; s < 2; ++s)
    {
        const std::string p = s == 0 ? "a." : "b.";
        const std::string n = s == 0 ? "A " : "B ";
        sp[s].amount = addPct (p + "amount", n + "Amount", 0.0f, 100.0f, 50.0f);
        sp[s].tone   = addFloat (p + "tone", n + "Tone", -100.0f, 100.0f, 0.0f, "", 0);
        sp[s].speed  = addPct (p + "speed", n + "Speed", 0.0f, 100.0f, 50.0f);
        range[s].lo   = addHz (p + "lo", n + "Range Low", 20.0f, 20000.0f, 20.0f);
        range[s].hi   = addHz (p + "hi", n + "Range High", 20.0f, 20000.0f, 20000.0f);
    }
}

float Sculptor::targetDb (int s, float hz) const
{
    const int t = (int) value (pTarget);
    float base;
    if (t >= InstrumentTargets::NumTargets)
        base = hasCustom.load (std::memory_order_relaxed) ? custom.curveDb (hz) : InstrumentTargets::curveDb (InstrumentTargets::FullMix, hz);
    else
        base = InstrumentTargets::curveDb (t, hz);
    // TONE tilts the target about 1 kHz, up to 2 dB per octave.
    return base + value (sp[s].tone) * 0.02f * std::log2 (hz / 1000.0f);
}

void Sculptor::computeGains (int g, int s, const float* frameDb, float* out)
{
    const float dt = frameSeconds();
    const float speed = value (sp[s].speed) * 0.01f;
    const float tauMs = 2000.0f * std::pow (0.02f, speed);   // 2 s .. 40 ms
    const float coef = 1.0f - std::exp (-dt / (tauMs * 0.001f));
    float* sm = smooth[g];
    if (! primed[g]) { for (int b = 0; b < kBands; ++b) sm[b] = frameDb[b]; primed[g] = true; }
    float mx = -200.0f;
    for (int b = 0; b < kBands; ++b)
    {
        sm[b] += coef * (frameDb[b] - sm[b]);
        specDb[g][b] = sm[b];
        mx = std::max (mx, sm[b]);
    }
    const float lo = value (range[s].lo), hi = std::max (lo * 1.05f, value (range[s].hi));
    float target[kBands];
    bool use[kBands];
    double sumDev = 0; int cnt = 0;
    for (int b = 0; b < kBands; ++b)
    {
        const float hz = SpectralBands::centreHz (b);
        target[b] = targetDb (s, hz);
        use[b] = hz >= lo && hz <= hi && sm[b] > mx - 60.0f && mx > -120.0f;
        if (use[b]) { sumDev += sm[b] - target[b]; ++cnt; }
    }
    const float level = cnt > 0 ? (float) (sumDev / cnt) : 0.0f;
    const float amount = value (sp[s].amount) * 0.01f;
    float raw[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        float gdb = 0.0f;
        if (use[b])
        {
            float dev = sm[b] - (target[b] + level);   // + means too much
            dev = dev > 0.5f ? dev - 0.5f : dev < -0.5f ? dev + 0.5f : 0.0f;
            gdb = clampf (-dev * 0.7f * amount, -12.0f, 6.0f);
        }
        raw[b] = gdb;
    }
    for (int b = 0; b < kBands; ++b)
        out[b] = 0.25f * raw[std::max (0, b - 1)] + 0.5f * raw[b] + 0.25f * raw[std::min (kBands - 1, b + 1)];
    // Leave the overall level alone.
    if (cnt > 0)
    {
        float mean = 0.0f;
        for (int b = 0; b < kBands; ++b) if (use[b]) mean += out[b];
        mean /= (float) cnt;
        for (int b = 0; b < kBands; ++b) if (use[b]) out[b] -= mean;
    }
}

// ===========================================================================
// Unmask
// ===========================================================================
Unmask::Unmask()
    : SpectralModule ({ "unm", "UNMASK", "UNMASK", 0xffc882e8u, kMidSideBit | kTransSusBit })
{
    pChMode = addChoice ("chmode", "Channel Mode", channelModeChoices (kMidSideBit | kTransSusBit), 0);
    for (int s = 0; s < 2; ++s)
    {
        const std::string p = s == 0 ? "a." : "b.";
        const std::string n = s == 0 ? "A " : "B ";
        sp[s].amount      = addPct (p + "amount", n + "Amount", 0.0f, 100.0f, 50.0f);
        sp[s].sensitivity = addPct (p + "sensitivity", n + "Sensitivity", 0.0f, 100.0f, 50.0f);
        sp[s].attack      = addMs (p + "attack", n + "Attack", 5.0f, 500.0f, 30.0f);
        sp[s].release     = addMs (p + "release", n + "Release", 10.0f, 2000.0f, 200.0f);
        range[s].lo        = addHz (p + "lo", n + "Range Low", 20.0f, 20000.0f, 20.0f);
        range[s].hi        = addHz (p + "hi", n + "Range High", 20.0f, 20000.0f, 20000.0f);
    }
}

void Unmask::computeGains (int g, int s, const float* frameDb, float* out)
{
    const float dt = frameSeconds();
    const float aC = 1.0f - std::exp (-dt / (value (sp[s].attack) * 0.001f));
    const float rC = 1.0f - std::exp (-dt / (value (sp[s].release) * 0.001f));
    const float amount = value (sp[s].amount) * 0.01f;
    const float sens = value (sp[s].sensitivity) * 0.01f;
    const float thresh = 6.0f - 18.0f * sens;   // the track within this many dB of the sidechain masks it
    float scMax = -200.0f;
    for (int b = 0; b < kBands; ++b) scMax = std::max (scMax, scSpecDb[b]);
    float raw[kBands];
    for (int b = 0; b < kBands; ++b)
    {
        specDb[g][b] = frameDb[b];
        float target = 0.0f;
        // Only where the sidechain has something to lose: its own strong bands.
        if (sidechainHasSignal && scSpecDb[b] > scMax - 24.0f && frameDb[b] > -130.0f)
        {
            const float excess = (frameDb[b] - scSpecDb[b]) - thresh;
            target = -clampf (excess * 0.6f, 0.0f, 18.0f) * amount;
        }
        gr[g][b] += (target < gr[g][b] ? aC : rC) * (target - gr[g][b]);
        raw[b] = gr[g][b];
    }
    for (int b = 0; b < kBands; ++b)
        out[b] = 0.25f * raw[std::max (0, b - 1)] + 0.5f * raw[b] + 0.25f * raw[std::min (kBands - 1, b + 1)];
}
} // namespace planish
