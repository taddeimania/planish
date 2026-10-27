#include "Chain.h"

namespace planish
{
namespace
{
const char* kNames[NumModules] {
    "GATE", "EQUALIZER", "COMPRESSOR 1", "COMPRESSOR 2", "TRANSIENT SHAPER", "EXCITER",
    "SCULPTOR", "UNMASK", "DENSITY", "PHASE", "CLIPPER" };
const char* kShort[NumModules] {
    "GATE", "EQ", "COMP 1", "COMP 2", "TRANSIENT", "EXCITER",
    "SCULPTOR", "UNMASK", "DENSITY", "PHASE", "CLIPPER" };
constexpr int kMaxLatency = 8192;
}

const char* Chain::moduleName (int id) { return kNames[std::min (NumModules - 1, std::max (0, id))]; }
const char* Chain::moduleShortName (int id) { return kShort[std::min (NumModules - 1, std::max (0, id))]; }

Chain::Chain()
{
    mods[ModGate] = &gate;
    mods[ModEqualizer] = &eq;
    mods[ModCompressor1] = &comp1;
    mods[ModCompressor2] = &comp2;
    mods[ModTransientShaper] = &transient;
    mods[ModExciter] = &exciter;
    mods[ModSculptor] = &sculptor;
    mods[ModUnmask] = &unmask;
    mods[ModDensity] = &density;
    mods[ModPhase] = &phase;
    mods[ModClipper] = &clipper;
    // The default chain, as Neutron opens: an Equalizer.
    orderShared[0] = ModEqualizer;
    orderSharedCount = 1;
}

void Chain::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = maxBlockSize;
    for (auto* m : mods) m->prepare (sr, maxBlock);
    for (auto& d : dryDelay) d.prepare (kMaxLatency);
    for (auto& ms : mixState) { ms.dl.prepare (kMaxLatency); ms.dr.prepare (kMaxLatency); ms.mix.setTime (30.0f, (float) sr, 32); }
    dryL.assign ((size_t) maxBlock, 0.0f); dryR = dryL; modL = dryL; modR = dryL; scZeroL = dryL; scZeroR = dryL;
    limiter.prepare (sr);
    matchMeter.prepare (sr);
    matchMeter.setTruePeak (false);
    for (auto* s : { &inSm, &outSm }) s->setTime (30.0f, (float) sr, 32);
    for (auto* s : { &bypassSm, &monoSm, &panSm, &widthSm, &invertSm }) s->setTime (20.0f, (float) sr, 32);
    matchSm.setTime (300.0f, (float) sr, 32);
    inMeter.prepare (sr);
    outMeter.prepare (sr);
    analyser.prepare (sr);
    reset();
}

void Chain::reset()
{
    for (auto* m : mods) m->reset();
    for (auto& d : dryDelay) d.reset();
    for (int i = 0; i < NumModules; ++i)
    {
        mixState[(size_t) i].dl.reset(); mixState[(size_t) i].dr.reset();
        mixState[(size_t) i].mix.snap (mods[(size_t) i]->value (mods[(size_t) i]->pMix) * 0.01f);
        inMs[i] = outMs[i] = 0.0f; diffDb[i] = 0.0f;
    }
    limiter.reset();
    inMeter.reset(); outMeter.reset(); matchMeter.reset();
    matchDb = 0.0f;
    matchSm.snap (0.0f);
    bypassSm.snap (0.0f);
    monoSm.snap (0.0f);
    invertSm.snap (0.0f);
    inputTap.clear(); outputTap.clear(); moduleTap.clear(); moduleInTap.clear(); sidechainTap.clear();
    clipIn[0] = clipIn[1] = clipOut[0] = clipOut[1] = false;
}

// ---------------------------------------------------------------------------
void Chain::setOrder (const std::vector<int>& ids)
{
    while (orderLock.exchange (true, std::memory_order_acquire)) {}
    orderSharedCount = 0;
    for (int id : ids)
    {
        if (id < 0 || id >= NumModules || orderSharedCount >= kMaxChain) continue;
        bool dup = false;
        for (int i = 0; i < orderSharedCount; ++i) if (orderShared[(size_t) i] == id) dup = true;
        if (! dup) orderShared[(size_t) orderSharedCount++] = id;
    }
    orderLock.store (false, std::memory_order_release);
}

std::vector<int> Chain::getOrder() const
{
    while (orderLock.exchange (true, std::memory_order_acquire)) {}
    std::vector<int> v (orderShared.begin(), orderShared.begin() + orderSharedCount);
    orderLock.store (false, std::memory_order_release);
    return v;
}

bool Chain::inChain (int id) const
{
    for (int i : getOrder()) if (i == id) return true;
    return false;
}

int Chain::latencyFor (const ChainSettings& s) const
{
    int lat = 0;
    for (int i = 0; i < orderCount; ++i)
        lat += mods[(size_t) order[(size_t) i]]->latency();
    lat += limiter.latencyFor (s.zeroLatency ? OutputLimiter::ZeroL : s.limiterMode);
    return std::min (lat, kMaxLatency - 1);
}

void Chain::updateLatency (const ChainSettings& s)
{
    latencyNow.store (latencyFor (s), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
MeterReadings Chain::takeInputMeters()
{
    MeterReadings r = inRead;
    r.lufsShort = inMeter.shortTerm();
    r.lufsMomentary = inMeter.momentary();
    r.lufsIntegrated = inMeter.integrated();
    r.truePeakDb = inMeter.truePeakDb();
    r.clipped[0] = clipIn[0]; r.clipped[1] = clipIn[1];
    inRead.peak[0] = inRead.peak[1] = 0.0f;
    return r;
}

MeterReadings Chain::takeOutputMeters()
{
    MeterReadings r = outRead;
    r.lufsShort = outMeter.shortTerm();
    r.lufsMomentary = outMeter.momentary();
    r.lufsIntegrated = outMeter.integrated();
    r.truePeakDb = outMeter.truePeakDb();
    r.clipped[0] = clipOut[0]; r.clipped[1] = clipOut[1];
    outRead.peak[0] = outRead.peak[1] = 0.0f;
    return r;
}

void Chain::resetMeters()
{
    inMeter.resetIntegrated();
    outMeter.resetIntegrated();
    clipIn[0] = clipIn[1] = clipOut[0] = clipOut[1] = false;
}

// ---------------------------------------------------------------------------
void Chain::process (float* l, float* r, int n, const float* scL, const float* scR, const ChainSettings& s)
{
    // Pick up a new order if the panel changed it.
    if (! orderLock.exchange (true, std::memory_order_acquire))
    {
        order = orderShared;
        orderCount = orderSharedCount;
        orderLock.store (false, std::memory_order_release);
    }

    // Context for every module (latency depends on ZERO LATENCY).
    BlockContext ctx;
    scPresent = scL != nullptr && scR != nullptr;
    if (! scPresent)
    {
        std::fill (scZeroL.begin(), scZeroL.begin() + n, 0.0f);
        std::fill (scZeroR.begin(), scZeroR.begin() + n, 0.0f);
    }
    ctx.scL = scPresent ? scL : scZeroL.data();
    ctx.scR = scPresent ? scR : scZeroR.data();
    ctx.scPresent = scPresent;
    ctx.bpm = hostBpm > 1.0 ? hostBpm : 120.0;
    ctx.playing = hostPlaying;
    ctx.ppq = hostPlaying ? hostPpq : freePpq;
    freePpq += n * ctx.bpm / 60.0 / sr;
    ctx.zeroLatency = s.zeroLatency;
    for (auto* m : mods) m->setContext (ctx);
    sidechainTap.push (ctx.scL, ctx.scR, n);

    updateLatency (s);
    const int lat = latencyNow.load (std::memory_order_relaxed);

    // Input gain, metering, taps.
    inSm.target = s.inGainDb;
    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float g = dbToGain (inSm.next());
        for (int i = start; i < start + len; ++i) { l[i] *= g; r[i] *= g; }
    }
    inputTap.push (l, r, n);
    inMeter.process (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float al = std::fabs (l[i]), ar = std::fabs (r[i]);
        inRead.peak[0] = std::max (inRead.peak[0], al);
        inRead.peak[1] = std::max (inRead.peak[1], ar);
        inMsM[0] += 0.0003f * (l[i] * l[i] - inMsM[0]);
        inMsM[1] += 0.0003f * (r[i] * r[i] - inMsM[1]);
        if (al > 1.0f) clipIn[0] = true;
        if (ar > 1.0f) clipIn[1] = true;
    }
    inRead.rms[0] = std::sqrt (inMsM[0]); inRead.rms[1] = std::sqrt (inMsM[1]);
    if (s.analysing)
        analyser.push (l, r, n);

    // Dry copy for BYPASS, delayed to line up with the processed signal.
    for (auto& d : dryDelay) d.setDelay (lat);
    for (int i = 0; i < n; ++i)
    {
        dryL[(size_t) i] = dryDelay[0].tick (l[i]);
        dryR[(size_t) i] = dryDelay[1].tick (r[i]);
    }

    // Modules.
    const int tap = tapModule.load (std::memory_order_relaxed);
    if (tap < 0) moduleInTap.push (l, r, n);
    bool pastDelta = false;
    for (int k = 0; k < orderCount; ++k)
    {
        const int id = order[(size_t) k];
        Module& m = *mods[(size_t) id];
        const bool active = (s.soloModule < 0 || s.soloModule == id) && ! pastDelta;
        if (tap == id) moduleInTap.push (l, r, n);

        float inSum = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            modL[(size_t) i] = l[i]; modR[(size_t) i] = r[i];
            inSum += l[i] * l[i] + r[i] * r[i];
        }

        if (active)
            m.process (l, r, n);
        else
        {
            const float was = m.value (m.pOn);
            m.setValue (m.pOn, 0.0f);
            m.process (l, r, n);
            m.setValue (m.pOn, was);
        }

        // The tile's MIX, against the module's input delayed by its latency.
        auto& ms = mixState[(size_t) id];
        const int ml = std::min (m.latency(), kMaxLatency - 1);
        ms.dl.setDelay (ml); ms.dr.setDelay (ml);
        ms.mix.target = active && m.isOn() ? clampf (m.value (m.pMix) * 0.01f, 0.0f, 1.0f) : 1.0f;
        const bool delta = s.deltaModule == id && active;
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min (32, n - start);
            const float mix = ms.mix.next();
            for (int i = start; i < start + len; ++i)
            {
                const float dl = ms.dl.tick (modL[(size_t) i]), dr = ms.dr.tick (modR[(size_t) i]);
                float a = dl + (l[i] - dl) * mix, b = dr + (r[i] - dr) * mix;
                if (delta) { a -= dl; b -= dr; }
                l[i] = a; r[i] = b;
            }
        }
        if (delta) pastDelta = true;

        float outSum = 0.0f;
        for (int i = 0; i < n; ++i) outSum += l[i] * l[i] + r[i] * r[i];
        inMs[id] += 0.05f * (inSum / (float) n - inMs[id]);
        outMs[id] += 0.05f * (outSum / (float) n - outMs[id]);
        diffDb[id] = (active && ! delta && inMs[id] > 1e-9f && outMs[id] > 1e-9f) ? clampf (10.0f * std::log10 (outMs[id] / inMs[id]), -30.0f, 30.0f) : 0.0f;
        if (tap == id)
            moduleTap.push (l, r, n);
    }

    // Channel operations, then output gain.
    widthSm.target = 1.0f + clampf (s.width, -100.0f, 100.0f) * 0.01f;
    panSm.target = clampf (s.pan, -100.0f, 100.0f) * 0.01f;
    invertSm.target = s.invert ? -1.0f : 1.0f;
    monoSm.target = s.mono ? 1.0f : 0.0f;
    outSm.target = s.outGainDb;
    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float w = widthSm.next(), p = panSm.next(), inv = invertSm.next(), mono = monoSm.next();
        const float g = dbToGain (outSm.next());
        const float gl = g * std::min (1.0f, 1.0f - p), gr = g * std::min (1.0f, 1.0f + p);
        for (int i = start; i < start + len; ++i)
        {
            float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
            side *= 1.0f - mono;
            float a = (mid + side) * inv, b = (mid - side) * inv;
            if (s.swap) std::swap (a, b);
            l[i] = a * gl; r[i] = b * gr;
        }
    }

    // Limiter (its delay line runs while off, so the latency holds).
    const int limMode = s.zeroLatency ? OutputLimiter::ZeroL : s.limiterMode;
    if (s.limiterOn) limiter.process (l, r, n, limMode, s.limiterStyle, clampf (s.ceilingDb, -20.0f, 0.0f));
    else             limiter.passThrough (l, r, n, limMode);

    // Gain match: bring the bypassed signal to the processed loudness.
    matchMeter.process (l, r, n);
    {
        const float inL = inMeter.shortTerm(), outL = matchMeter.shortTerm();
        if (inL > -60.0f && outL > -60.0f)
            matchDb = clampf (outL - inL, -24.0f, 24.0f);
    }
    matchSm.target = s.gainMatch ? matchDb : 0.0f;
    bypassSm.target = s.bypass ? 1.0f : 0.0f;
    for (int start = 0; start < n; start += 32)
    {
        const int len = std::min (32, n - start);
        const float match = dbToGain (matchSm.next());
        const float byp = bypassSm.next();
        if (byp <= 0.0f && bypassSm.settled()) continue;
        for (int i = start; i < start + len; ++i)
        {
            l[i] = lerp (l[i], dryL[(size_t) i] * match, byp);
            r[i] = lerp (r[i], dryR[(size_t) i] * match, byp);
        }
    }

    // Metering, taps.
    outMeter.process (l, r, n);
    for (int i = 0; i < n; ++i)
    {
        const float al = std::fabs (l[i]), ar = std::fabs (r[i]);
        outRead.peak[0] = std::max (outRead.peak[0], al);
        outRead.peak[1] = std::max (outRead.peak[1], ar);
        outMsM[0] += 0.0003f * (l[i] * l[i] - outMsM[0]);
        outMsM[1] += 0.0003f * (r[i] * r[i] - outMsM[1]);
        if (al > 1.0f) clipOut[0] = true;
        if (ar > 1.0f) clipOut[1] = true;
    }
    outRead.rms[0] = std::sqrt (outMsM[0]); outRead.rms[1] = std::sqrt (outMsM[1]);
    outputTap.push (l, r, n);
    if (tap < 0)
        moduleTap.push (l, r, n);
}
} // namespace planish
