#include "Analysis.h"

namespace planish
{
float AnalysisResult::spectrumAt (float hz) const
{
    const float pos = clampf (std::log2 (std::max (20.0f, hz) / 20.0f) * 6.0f, 0.0f, (float) (kBands - 1));
    const int i = std::min (kBands - 2, (int) pos);
    const float f = pos - (float) i;
    return spectrumDb[(size_t) i] + (spectrumDb[(size_t) i + 1] - spectrumDb[(size_t) i]) * f;
}

CustomTarget AnalysisResult::asTarget (const std::string& name) const
{
    CustomTarget t;
    t.name = name;
    for (int p = 0; p < InstrumentTargets::kPoints; ++p)
    {
        // Average a third of an octave either side of each octave point.
        const float hz = 20.0f * std::pow (2.0f, (float) p);
        float sum = 0.0f; int cnt = 0;
        for (int k = -2; k <= 2; ++k)
        {
            const int b = p * 6 + k;
            if (b < 0 || b >= kBands) continue;
            sum += spectrumDb[(size_t) b]; ++cnt;
        }
        t.points[(size_t) p] = cnt > 0 ? sum / (float) cnt : spectrumAt (hz);
    }
    return t;
}

// ---------------------------------------------------------------------------
void Analyzer::prepare (double sampleRate)
{
    sr = sampleRate;
    fft.prepare (kFrame);
    bands.prepare (6, kFrame, sr);
    window.resize (kFrame);
    for (int i = 0; i < kFrame; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * kPif * (float) i / (float) kFrame));
    frameBuf.assign (kFrame, 0.0f);
    bins.assign (kFrame / 2 + 1, Fft::cpx (0.0f, 0.0f));
    acc.assign ((size_t) std::max (bands.count, AnalysisResult::kBands), 0.0);
    frameEnergies.assign ((size_t) std::max (bands.count, AnalysisResult::kBands), 0.0f);
    ring.assign (kFrame, 0.0f);
    meter.prepare (sr);
    meter.setTruePeak (false);
    blockSize = std::max (64, (int) (0.05 * sr));
    fastEnv.set (0.5f, 30.0f, (float) sr);
    slowEnv.set (25.0f, 250.0f, (float) sr);
    reset();
}

void Analyzer::reset()
{
    samples = 0;
    std::fill (acc.begin(), acc.end(), 0.0);
    centroidNum = centroidDen = lowE = highE = totalE = 0;
    frames = 0; inPos = 0; sinceHop = 0;
    std::fill (ring.begin(), ring.end(), 0.0f);
    meter.reset();
    stMax = -70.0f; peak = 0.0f;
    hist.fill (0);
    blockSum = 0; blockLen = 0; blockPeak = 0.0f;
    midE = sideE = 0;
    fastEnv.reset(); slowEnv.reset();
    inOnset = false; onsets = 0;
    crestAcc = 0; crestCount = 0;
}

void Analyzer::push (const float* l, const float* r, int n)
{
    meter.process (l, r, n);
    stMax = std::max (stMax, meter.shortTerm());
    const double binHz = sr / kFrame;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
        const float a = std::max (std::fabs (l[i]), std::fabs (r[i]));
        peak = std::max (peak, a);
        midE += (double) m * m;
        sideE += (double) s * s;

        // Levels and crest, per 50 ms block.
        blockSum += (double) m * m;
        blockPeak = std::max (blockPeak, std::fabs (m));
        if (++blockLen >= blockSize)
        {
            const double rms = std::sqrt (blockSum / blockLen);
            const float db = gainToDb ((float) rms);
            if (db > -90.0f)
            {
                hist[(size_t) std::min (kHist - 1, std::max (0, (int) ((db + 100.0f) * 2.0f)))]++;
                crestAcc += gainToDb (blockPeak) - db;
                ++crestCount;
            }
            blockSum = 0; blockLen = 0; blockPeak = 0.0f;
        }

        // Onsets: the fast envelope jumping 6 dB over the slow one.
        const float f = fastEnv.next (std::fabs (m)), sl = slowEnv.next (std::fabs (m));
        const bool onsetNow = f > 2.0f * sl && f > 1.0e-3f;
        if (onsetNow && ! inOnset) ++onsets;
        inOnset = onsetNow ? true : (f < 1.3f * sl ? false : inOnset);

        ring[(size_t) inPos] = m;
        inPos = (inPos + 1) % kFrame;
        if (++sinceHop >= kHop)
        {
            sinceHop = 0;
            for (int k = 0; k < kFrame; ++k)
                frameBuf[(size_t) k] = ring[(size_t) ((inPos + k) % kFrame)] * window[(size_t) k];
            fft.forwardReal (frameBuf.data(), bins.data());
            bands.energies (bins.data(), frameEnergies.data());
            for (int b = 0; b < bands.count && b < AnalysisResult::kBands; ++b)
                acc[(size_t) b] += frameEnergies[(size_t) b];
            for (int k = 1; k <= kFrame / 2; ++k)
            {
                const double e = std::norm (bins[(size_t) k]);
                const double hz = k * binHz;
                centroidNum += e * hz; centroidDen += e;
                totalE += e;
                if (hz < 120.0) lowE += e;
                if (hz > 5000.0) highE += e;
            }
            ++frames;
        }
    }
    samples += n;
}

AnalysisResult Analyzer::result() const
{
    AnalysisResult r;
    r.seconds = seconds();
    if (frames == 0 || samples < (long) (sr * 0.5))
        return r;
    for (int b = 0; b < AnalysisResult::kBands; ++b)
        r.spectrumDb[(size_t) b] = 10.0f * std::log10 ((float) (acc[(size_t) b] / frames) + 1e-14f)
                                 + 3.0f * std::log2 (AnalysisResult::bandHz (b) / 1000.0f);
    double sum = 0; int cnt = 0;
    for (int b = 0; b < AnalysisResult::kBands; ++b)
    {
        const float hz = AnalysisResult::bandHz (b);
        if (hz >= 100.0f && hz <= 8000.0f) { sum += r.spectrumDb[(size_t) b]; ++cnt; }
    }
    const float mean = cnt > 0 ? (float) (sum / cnt) : 0.0f;
    for (auto& v : r.spectrumDb) v -= mean;

    r.integratedLufs = meter.hasIntegrated() ? meter.integrated() : meter.shortTerm();
    r.shortTermMaxLufs = stMax;
    r.peakDb = gainToDb (std::max (peak, 1.0e-6f));
    int total = 0;
    for (int v : hist) total += v;
    auto percentile = [&] (float p)
    {
        if (total == 0) return -100.0f;
        int seen = 0;
        for (int i = 0; i < kHist; ++i)
        {
            seen += hist[(size_t) i];
            if ((float) seen >= p * (float) total) return -100.0f + (float) i * 0.5f + 0.25f;
        }
        return 0.0f;
    };
    r.floorDb = percentile (0.1f);
    r.bodyDb = percentile (0.5f);
    r.loudDb = percentile (0.9f);
    r.crestDb = crestCount > 0 ? (float) (crestAcc / (double) crestCount) : 12.0f;
    r.onsetsPerSecond = (float) onsets / std::max (0.5f, seconds());
    r.widthDb = (midE > 1e-12 && sideE > 1e-12) ? (float) (10.0 * std::log10 (sideE / midE)) : -30.0f;
    r.centroidHz = centroidDen > 0 ? (float) (centroidNum / centroidDen) : 1000.0f;
    r.lowShare = totalE > 0 ? (float) (lowE / totalE) : 0.0f;
    r.highShare = totalE > 0 ? (float) (highE / totalE) : 0.0f;
    r.valid = true;
    return r;
}
} // namespace planish
