#include "Phase.h"
#include "../Fft.h"

namespace planish
{
Phase::Phase()
    : Module ({ "phs", "PHASE", "PHASE", 0xff7fd0f0u, kStereoOnly })
{
    pRotation  = addFloat ("rotation", "Rotation", -180.0f, 180.0f, 0.0f, "deg", 0);
    pDelay     = addFloat ("delay", "Delay", 0.0f, kMaxDelayMs, 0.0f, "ms", 2);
    pScopeType = addChoice ("scope.type", "Oscilloscope Type", { "Main", "SC", "Main & SC", "Combined" }, 2);
    pScopeTime = addChoice ("scope.time", "Oscilloscope Time", { "1 Bar", "1/2 Bar", "1/4 Bar", "1/8 Bar", "1/16 Bar" }, 2);
}

void Phase::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    stft.prepare (kFrame, kHop, 2);
    const int maxDelay = (int) std::ceil (kMaxDelayMs * 0.001 * sr) + 4;
    for (auto& d : delayLine) d.prepare (maxDelay);
    delaySm.setTime (60.0f, (float) sr, 1);
    learnIn.assign ((size_t) kLearnSamples, 0.0f);
    learnSc.assign ((size_t) kLearnSamples, 0.0f);
    reset();
}

void Phase::reset()
{
    stft.reset();
    for (auto& d : delayLine) d.reset();
    delaySm.snap (value (pDelay) * 0.001f * (float) sr);
    rotationNow = rotTarget = value (pRotation);
    scope.clear();
}

void Phase::process (float* l, float* r, int n)
{
    // LEARN: record a snippet of the input (and sidechain).
    const int req = learnRequests.load (std::memory_order_relaxed);
    if (req != seenRequests)
    {
        seenRequests = req;
        if (learnState.load() == 0) { learnState.store (1); learnFill.store (0); learnHadSc = false; }
        else if (learnState.load() == 1) learnState.store (0);   // a second press cancels
    }
    if (learnState.load (std::memory_order_relaxed) == 1)
    {
        int fill = learnFill.load (std::memory_order_relaxed);
        for (int i = 0; i < n && fill < kLearnSamples; ++i, ++fill)
        {
            learnIn[(size_t) fill] = 0.5f * (l[i] + r[i]);
            const float s = ctx.scL != nullptr ? 0.5f * (ctx.scL[i] + ctx.scR[i]) : 0.0f;
            learnSc[(size_t) fill] = s;
            if (std::fabs (s) > 1.0e-4f) learnHadSc = true;
        }
        learnFill.store (fill, std::memory_order_relaxed);
        if (fill >= kLearnSamples) learnState.store (2);
    }

    const bool on = isOn();
    rotTarget = on ? value (pRotation) : 0.0f;
    if (! ctx.zeroLatency)
    {
        float* d[2] { l, r };
        stft.process (d, n, [this] (Stft::cpx** bins, int chans, int nb)
        {
            // Glide toward the target so moving the control doesn't click.
            const float step = 12.0f;
            rotationNow += clampf (rotTarget - rotationNow, -step, step);
            if (std::fabs (rotationNow) < 1.0e-3f) return;
            const float th = rotationNow * kPif / 180.0f;
            const Stft::cpx rot (std::cos (th), std::sin (th));
            for (int c = 0; c < chans; ++c)
                for (int k = 1; k < nb - 1; ++k)
                    bins[c][k] *= rot;
        });
    }
    else
        rotationNow = 0.0f;

    delaySm.target = on ? value (pDelay) * 0.001f * (float) sr : 0.0f;
    const double ppqPerSample = ctx.bpm / 60.0 / sr;
    for (int i = 0; i < n; ++i)
    {
        const float dl = delaySm.next();
        delayLine[0].push (l[i]);
        delayLine[1].push (r[i]);
        l[i] = delayLine[0].readFrac (dl);
        r[i] = delayLine[1].readFrac (dl);
        const float sc = ctx.scL != nullptr ? 0.5f * (ctx.scL[i] + ctx.scR[i]) : 0.0f;
        scope.push (0.5f * (l[i] + r[i]), sc, ctx.ppq + i * ppqPerSample);
    }
}

// ---------------------------------------------------------------------------
float Phase::bestRotation (const std::vector<float>& x, float& peakGainDb)
{
    int n = 1;
    while (n < (int) x.size()) n <<= 1;
    Fft fft;
    fft.prepare (n);
    std::vector<Fft::cpx> z ((size_t) n, Fft::cpx (0.0f, 0.0f));
    for (size_t i = 0; i < x.size(); ++i) z[i] = Fft::cpx (x[i], 0.0f);
    fft.transform (z.data(), false);
    // Analytic signal: double the positive frequencies, drop the negative.
    for (int k = 1; k < n / 2; ++k) z[(size_t) k] *= 2.0f;
    for (int k = n / 2 + 1; k < n; ++k) z[(size_t) k] = 0.0f;
    fft.transform (z.data(), true);

    // Skip the edges, where the circular transform wraps.
    const int edge = std::min ((int) x.size() / 8, 2048);
    const int i0 = edge, i1 = (int) x.size() - edge;
    auto peakAt = [&] (float deg)
    {
        const float th = deg * kPif / 180.0f, c = std::cos (th), s = std::sin (th);
        float pk = 0.0f;
        for (int i = i0; i < i1; ++i)
            pk = std::max (pk, std::fabs (z[(size_t) i].real() * c - z[(size_t) i].imag() * s));
        return pk;
    };
    const float p0 = peakAt (0.0f);
    float best = 0.0f, bestPeak = p0;
    for (int d = -180; d < 180; d += 2)
    {
        const float pk = peakAt ((float) d);
        if (pk < bestPeak - 1.0e-6f) { bestPeak = pk; best = (float) d; }
    }
    // Refine around the best.
    for (float d = best - 2.0f; d <= best + 2.0f; d += 0.5f)
    {
        const float pk = peakAt (d);
        if (pk < bestPeak - 1.0e-6f) { bestPeak = pk; best = d; }
    }
    peakGainDb = p0 > 1.0e-9f ? gainToDb (bestPeak / p0) : 0.0f;
    if (peakGainDb > -0.3f) { peakGainDb = 0.0f; return 0.0f; }   // not worth it
    if (best >= 180.0f) best -= 360.0f;
    return best;
}

int Phase::bestDelay (const std::vector<float>& x, const std::vector<float>& sc, int maxLag, bool& flipped)
{
    flipped = false;
    int n = 1;
    while (n < 2 * (int) x.size()) n <<= 1;
    Fft fft;
    fft.prepare (n);
    std::vector<Fft::cpx> a ((size_t) n, Fft::cpx (0.0f, 0.0f)), b ((size_t) n, Fft::cpx (0.0f, 0.0f));
    double ea = 0, eb = 0;
    for (size_t i = 0; i < x.size(); ++i) { a[i] = Fft::cpx (x[i], 0.0f); ea += (double) x[i] * x[i]; }
    for (size_t i = 0; i < sc.size(); ++i) { b[i] = Fft::cpx (sc[i], 0.0f); eb += (double) sc[i] * sc[i]; }
    if (ea < 1e-9 || eb < 1e-9) return -1;
    fft.transform (a.data(), false);
    fft.transform (b.data(), false);
    for (int k = 0; k < n; ++k) a[(size_t) k] = std::conj (a[(size_t) k]) * b[(size_t) k];
    fft.transform (a.data(), true);
    // a[d] = sum x(t) sc(t + d): the sidechain arrives d samples after the track.
    int best = 0; float bestV = 0.0f;
    for (int d = 0; d <= maxLag && d < n; ++d)
    {
        const float v = a[(size_t) d].real();
        if (std::fabs (v) > std::fabs (bestV)) { bestV = v; best = d; }
    }
    const float norm = (float) (bestV / std::sqrt (ea * eb));
    if (std::fabs (norm) < 0.1f) return -1;
    flipped = norm < 0.0f;
    return best;
}

bool Phase::learnTick()
{
    if (learnState.load() != 2) return false;
    learnedRotation = bestRotation (learnIn, learnedPeakGainDb);
    learnedDelayValid = false;
    learnedPolarityFlip = false;
    if (learnHadSc)
    {
        bool flip = false;
        const int d = bestDelay (learnIn, learnSc, (int) (kMaxDelayMs * 0.001 * sr), flip);
        if (d >= 0)
        {
            learnedDelayValid = true;
            learnedDelayMs = (float) (d * 1000.0 / sr);
            learnedPolarityFlip = flip;
            // With a sidechain, the polarity decides the rotation.
            learnedRotation = flip ? 180.0f : 0.0f;
        }
    }
    learnState.store (0);
    return true;
}
} // namespace planish
