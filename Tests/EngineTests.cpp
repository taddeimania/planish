// EngineTests.cpp — checks Planish's signal path without JUCE or a host.
// Run with ctest, or directly: ./PlanishEngineTests
#include "Engine/Chain.h"
#include "Engine/Presets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace planish;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL: %s\n", what.c_str()); }
}
void checkNear (double got, double want, double tol, const std::string& what)
{
    ++checks;
    if (! (std::fabs (got - want) <= tol)) { ++failures; std::printf ("FAIL: %s (got %.3f, want %.3f +- %.3f)\n", what.c_str(), got, want, tol); }
}

constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586;
constexpr int kBlock = 256;

struct Sine
{
    double phase = 0, inc; float amp;
    explicit Sine (double hz, float a = 1.0f) : inc (kTwoPi * hz / kSr), amp (a) {}
    float next() { const float v = amp * (float) std::sin (phase); phase += inc; if (phase > kTwoPi) phase -= kTwoPi; return v; }
};

struct NoiseGen
{
    uint32_t s = 12345u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 8388608.0f - 1.0f; }
};

// Runs a module on a generator; returns RMS (left) after `settle` seconds, and the peak.
template <typename Gen>
float runRms (Module& m, Gen gen, double seconds, double settle, float* peakOut = nullptr, const BlockContext* ctx = nullptr,
              std::vector<float>* sc = nullptr)
{
    std::vector<float> l (kBlock), r (kBlock);
    double sum = 0; long cnt = 0; float pk = 0.0f;
    const long total = (long) (kSr * seconds);
    for (long pos = 0; pos < total; pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i) gen (l[(size_t) i], r[(size_t) i], sc ? &(*sc)[(size_t) i] : nullptr);
        if (ctx)
        {
            BlockContext c = *ctx;
            if (sc) { c.scL = sc->data(); c.scR = sc->data(); c.scPresent = true; }
            m.setContext (c);
        }
        m.process (l.data(), r.data(), kBlock);
        if (pos > kSr * settle)
            for (int i = 0; i < kBlock; ++i) { sum += l[(size_t) i] * l[(size_t) i]; ++cnt; pk = std::max (pk, std::fabs (l[(size_t) i])); }
    }
    if (peakOut) *peakOut = pk;
    return (float) std::sqrt (sum / std::max (1L, cnt));
}

auto sineGen (double hz, float amp)
{
    auto s = std::make_shared<Sine> (hz, amp);
    return [s] (float& l, float& r, float*) { l = r = s->next(); };
}

float dbOf (float rms) { return 20.0f * std::log10 (std::max (rms, 1e-9f)); }
constexpr float kSineRmsDb = -3.0103f;   // a full-scale sine's RMS

// Feeds an impulse through a module and returns where the largest output lands.
int impulsePeak (Module& m, int length = 8192)
{
    std::vector<float> l ((size_t) length, 0.0f), r ((size_t) length, 0.0f);
    l[100] = r[100] = 0.5f;
    for (int pos = 0; pos < length; pos += kBlock) m.process (l.data() + pos, r.data() + pos, kBlock);
    int best = 0;
    for (int i = 0; i < length; ++i) if (std::fabs (l[(size_t) i]) > std::fabs (l[(size_t) best])) best = i;
    return best - 100;
}

// Harmonic energy (2nd + 3rd) relative to the fundamental, in dB.
float harmonicsDb (const std::vector<float>& x, double f0)
{
    auto bin = [&] (double hz)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < x.size(); ++i) { re += x[i] * std::cos (kTwoPi * hz * (double) i / kSr); im += x[i] * std::sin (kTwoPi * hz * (double) i / kSr); }
        return re * re + im * im;
    };
    const double f = bin (f0), h = bin (2 * f0) + bin (3 * f0);
    return (float) (10.0 * std::log10 ((h + 1e-20) / (f + 1e-20)));
}

// ===========================================================================
void testBuildingBlocks()
{
    Biquad b;
    b.peak (1000.0, 1.0, 6.0, kSr);
    checkNear (b.responseDb (1000.0, kSr), 6.0, 0.05, "peak filter gain at centre");
    checkNear (b.responseDb (50.0, kSr), 0.0, 0.1, "peak filter flat far away");

    // Oversampler: a whole number of samples of latency, unity gain.
    Oversampler4x os;
    std::vector<float> y (400);
    for (int i = 0; i < 400; ++i) y[(size_t) i] = os.process (i == 50 ? 1.0f : 0.0f, [] (float v) { return v; });
    int best = 0; double sum = 0;
    for (int i = 0; i < 400; ++i) { if (std::fabs (y[(size_t) i]) > std::fabs (y[(size_t) best])) best = i; sum += y[(size_t) i]; }
    check (best - 50 == Oversampler4x::kLatency, "4x oversampler peak lands at its latency (" + std::to_string (best - 50) + ")");
    checkNear (sum, 1.0, 0.01, "4x oversampler unity DC gain");

    // STFT with four lanes is an identity with one frame of latency.
    Stft st;
    st.prepare (1024, 256, 4);
    std::vector<float> lanes[4];
    NoiseGen ng;
    for (auto& v : lanes) { v.resize (8192); for (auto& s : v) s = ng.next(); }
    auto ref = lanes[3];
    float* d[4] { lanes[0].data(), lanes[1].data(), lanes[2].data(), lanes[3].data() };
    st.process (d, 8192, [] (Stft::cpx**, int, int) {});
    double err = 0;
    for (int i = 2048; i < 8192; ++i) err = std::max (err, (double) std::fabs (lanes[3][(size_t) i] - ref[(size_t) i - 1024]));
    check (err < 1e-3, "four-lane STFT identity with one-frame latency");

    // Lanes: every mode joins back to the input.
    LaneSplitter ls;
    ls.prepare (kSr);
    Sine s1 (110.0), s2 (3000.0, 0.3f);
    double maxErr = 0;
    for (int mode : { (int) Stereo, (int) MidSide, (int) TransSus })
        for (int i = 0; i < 20000; ++i)
        {
            const float env = (i % 6000) < 200 ? 1.0f : 0.2f;
            const float l = s1.next() * env, r = s2.next();
            float x[4], ol, orr;
            ls.split (mode, l, r, x);
            LaneSplitter::join (mode, x, ol, orr);
            maxErr = std::max (maxErr, (double) std::max (std::fabs (ol - l), std::fabs (orr - r)));
        }
    check (maxErr < 1e-5, "stereo, mid/side and trans/sus lanes rejoin exactly");
    // The transient lane catches the onset, the sustain lane the tail.
    ls.reset();
    double tEarly = 0, tLate = 0;
    Sine s3 (220.0);
    for (int i = 0; i < 24000; ++i)
    {
        const float env = std::exp (-(float) i / 6000.0f);
        float x[4];
        ls.split (TransSus, s3.next() * env, 0.0f, x);
        if (i < 480) tEarly += x[0] * x[0];
        else if (i > 4800 && i < 5280) tLate += x[0] * x[0];
    }
    check (tEarly > 10.0 * tLate, "transient lane holds the onset");
}

// ===========================================================================
void testGate()
{
    Gate g;
    g.prepare (kSr, kBlock);
    g.set ("band1.open", -30.0f);
    g.set ("band1.close", -36.0f);
    g.set ("band1.ratio", 100.0f);
    const float quiet = dbOf (runRms (g, sineGen (440.0, 0.01f), 1.0, 0.5));   // -40 dBFS peak
    check (quiet < -70.0f, "gate attenuates below the close threshold (" + std::to_string (quiet) + ")");
    g.reset();
    const float loud = dbOf (runRms (g, sineGen (440.0, 0.5f), 1.0, 0.5));
    checkNear (loud, -6.02f + kSineRmsDb, 0.3, "gate passes a loud signal");

    // Hysteresis: open loud, then drop between close and open: stays open.
    g.reset();
    {
        auto s = std::make_shared<Sine> (440.0, 0.1f);
        auto n = std::make_shared<long> (0);
        auto gen = [s, n] (float& l, float& r, float*) { const long i = (*n)++; s->amp = i < 24000 ? 0.1f : dbToGain (-33.0f); l = r = s->next(); };
        const float mid = dbOf (runRms (g, gen, 1.2, 0.9));
        checkNear (mid, -33.0f + kSineRmsDb, 0.5, "gate stays open between close and open thresholds");
    }

    // External sidechain: the gate opens when the sidechain is loud.
    g.reset();
    g.set ("band1.sc", (float) (2 + Gate::kBands));   // Ext. Full
    BlockContext ctx;
    std::vector<float> sc (kBlock);
    auto s = std::make_shared<Sine> (440.0, 0.01f);
    auto t = std::make_shared<Sine> (100.0, 0.5f);
    auto gen = [s, t] (float& l, float& r, float* scv) { l = r = s->next(); if (scv) *scv = t->next(); };
    const float keyed = dbOf (runRms (g, gen, 1.0, 0.5, nullptr, &ctx, &sc));
    checkNear (keyed, -40.0f + kSineRmsDb, 0.5, "external sidechain opens the gate");
}

// ===========================================================================
void testEqualizer()
{
    Equalizer eq;
    eq.prepare (kSr, kBlock);
    eq.set ("a.b2.shape", (float) Bell);
    eq.set ("a.b2.freq", 1000.0f);
    eq.set ("a.b2.gain", 6.0f);
    const float at = dbOf (runRms (eq, sineGen (1000.0, 0.25f), 0.5, 0.2));
    checkNear (at - dbOf (0.25f * 0.70711f), 6.0, 0.15, "EQ bell +6 dB at its centre");
    const float away = dbOf (runRms (eq, sineGen (60.0, 0.25f), 0.5, 0.2));
    checkNear (away - dbOf (0.25f * 0.70711f), 0.0, 0.3, "EQ bell leaves 60 Hz alone");

    // Every shape stays finite and has a sensible response.
    for (int sh = 0; sh < NumFilterShapes; ++sh)
    {
        FilterBand f;
        f.design (sh, 1000.0, 6.0, 1.0, 3, kSr);
        const double db = f.responseDb (1000.0, kSr);
        check (std::isfinite (db) && db > -40.0 && db < 20.0, std::string ("filter shape response: ") + filterShapeName (sh));
    }
    FilterBand hp;
    hp.design (HighPassFlat, 100.0, 0.0, 1.0, 3, kSr);
    check (hp.responseDb (25.0, kSr) < -40.0, "24 dB/oct high pass, two octaves down");

    // Mid/side: a cut on the side leaves a mono signal untouched.
    eq.setDefaults();
    eq.reset();
    eq.set ("chmode", 1.0f);
    eq.set ("b.b2.shape", (float) Bell); eq.set ("b.b2.freq", 1000.0f); eq.set ("b.b2.gain", -12.0f);
    const float mono = dbOf (runRms (eq, sineGen (1000.0, 0.25f), 0.5, 0.2));
    checkNear (mono - dbOf (0.25f * 0.70711f), 0.0, 0.1, "mid/side EQ: side cut leaves mono alone");

    // Dynamic: a DOWN band with a cut acts only when the level is high.
    eq.setDefaults();
    eq.reset();
    eq.set ("a.b2.shape", (float) Bell); eq.set ("a.b2.freq", 3000.0f); eq.set ("a.b2.gain", -9.0f); eq.set ("a.b2.q", 1.0f);
    eq.set ("a.b2.dyn", 1.0f); eq.set ("a.b2.threshold", -30.0f); eq.set ("a.b2.direction", (float) Equalizer::Down);
    const float quietDyn = dbOf (runRms (eq, sineGen (3000.0, dbToGain (-50.0f)), 0.6, 0.3));
    checkNear (quietDyn, -50.0f + kSineRmsDb, 0.5, "dynamic EQ: quiet signal untouched");
    eq.reset();
    const float loudDyn = dbOf (runRms (eq, sineGen (3000.0, dbToGain (-6.0f)), 0.6, 0.3));
    checkNear (loudDyn, -6.0f + kSineRmsDb - 9.0f, 1.0, "dynamic EQ: loud signal gets the full cut");

    // Trans/sus: a boost on the sustain set only; a steady tone is all sustain.
    eq.setDefaults();
    eq.reset();
    eq.set ("chmode", 2.0f);
    eq.set ("b.b2.shape", (float) Bell); eq.set ("b.b2.freq", 1000.0f); eq.set ("b.b2.gain", 6.0f);
    const float steady = dbOf (runRms (eq, sineGen (1000.0, 0.25f), 0.6, 0.3));
    checkNear (steady - dbOf (0.25f * 0.70711f), 6.0, 0.5, "trans/sus EQ: a steady tone is all sustain");
}

// ===========================================================================
void testCompressor()
{
    Compressor c (1);
    c.prepare (kSr, kBlock);
    c.set ("band1.threshold", -20.0f);
    c.set ("band1.ratio", 4.0f);
    c.set ("band1.knee", 0.0f);
    c.set ("band1.attack", 1.0f);
    c.set ("band1.release", 50.0f);
    c.set ("detect", (float) DetectPeak);
    // -6 dBFS peak sine: 14 dB over, so 10.5 dB of reduction.
    const float out = dbOf (runRms (c, sineGen (200.0, 0.5f), 1.5, 1.0));
    checkNear (out, -6.02f + kSineRmsDb - 10.5f, 1.0, "modern 4:1 compression above threshold");
    c.reset();
    const float under = dbOf (runRms (c, sineGen (200.0, 0.05f), 1.0, 0.5));
    checkNear (under, -26.02f + kSineRmsDb, 0.3, "no compression under the threshold");

    // Ratio below 1:1 expands what exceeds the threshold.
    c.reset();
    c.set ("band1.ratio", 0.5f);
    const float ex = dbOf (runRms (c, sineGen (200.0, 0.2f), 1.5, 1.0));
    check (ex > -13.98f + kSineRmsDb + 2.0f, "ratio under 1:1 expands");

    // Punch at 100% is neutral; above, the hits get louder.
    c.setDefaults();
    c.reset();
    c.set ("mode", (float) Compressor::Punch);
    auto hits = [] () {
        auto s = std::make_shared<Sine> (150.0);
        auto n = std::make_shared<int> (0);
        return [s, n] (float& l, float& r, float*) { const int i = (*n)++; const float env = std::exp (-(float) (i % 12000) / 800.0f); l = r = 0.5f * env * s->next(); };
    };
    float pkDry = 0, pk100 = 0, pk160 = 0;
    c.set ("on", 0.0f);
    runRms (c, hits(), 2.0, 1.0, &pkDry);
    c.set ("on", 1.0f);
    c.reset();
    runRms (c, hits(), 2.0, 1.0, &pk100);
    c.reset();
    c.set ("band1.amount", 160.0f);
    runRms (c, hits(), 2.0, 1.0, &pk160);
    checkNear (pk100, pkDry, 0.01f, "punch 100% leaves the hits alone");
    check (pk160 > pk100 * 1.15f, "punch above 100% pushes the hits out");

    // Vintage compresses too.
    c.setDefaults();
    c.reset();
    c.set ("mode", (float) Compressor::Vintage);
    c.set ("band1.threshold", -20.0f); c.set ("band1.ratio", 4.0f);
    const float vint = dbOf (runRms (c, sineGen (200.0, 0.5f), 1.5, 1.0));
    check (vint < -6.02f + kSineRmsDb - 4.0f, "vintage mode compresses");

    // Three bands with nothing over threshold sum flat.
    c.setDefaults();
    c.set ("bands", 2.0f);
    c.set ("band1.threshold", 0.0f); c.set ("band2.threshold", 0.0f); c.set ("band3.threshold", 0.0f);
    for (double hz : { 60.0, 400.0, 2500.0, 9000.0 })
    {
        c.reset();
        const float o = dbOf (runRms (c, sineGen (hz, 0.1f), 0.6, 0.3));
        checkNear (o, -20.0f + kSineRmsDb, 0.15, "three-band compressor sums flat at " + std::to_string ((int) hz) + " Hz");
    }

    // External sidechain ducks the track.
    c.setDefaults();
    c.reset();
    c.sidechainDefaults (0);
    BlockContext ctx;
    std::vector<float> sc (kBlock);
    auto s = std::make_shared<Sine> (300.0, 0.1f);
    auto key = std::make_shared<Sine> (60.0, 0.8f);
    auto gen = [s, key] (float& l, float& r, float* scv) { l = r = s->next(); if (scv) *scv = key->next(); };
    const float ducked = dbOf (runRms (c, gen, 1.5, 1.0, nullptr, &ctx, &sc));
    check (ducked < -20.0f + kSineRmsDb - 6.0f, "external sidechain ducks the compressor's input");
}

// ===========================================================================
void testTransientShaper()
{
    TransientShaper t;
    t.prepare (kSr, kBlock);
    auto hits = [] () {
        auto s = std::make_shared<Sine> (180.0);
        auto n = std::make_shared<int> (0);
        return [s, n] (float& l, float& r, float*) { const int i = (*n)++; const float env = std::exp (-(float) (i % 12000) / 1500.0f); l = r = 0.4f * env * s->next(); };
    };
    float pk0 = 0, pkA = 0;
    const float rms0 = runRms (t, hits(), 2.0, 1.0, &pk0);
    t.reset();
    t.set ("a.band1.attack", 9.0f);
    runRms (t, hits(), 2.0, 1.0, &pkA);
    checkNear (pk0, 0.4f, 0.02f, "transient shaper at 0 dB is neutral");
    check (pkA > pk0 * 1.4f, "attack +9 dB lifts the hits");
    t.reset();
    t.set ("a.band1.attack", 0.0f);
    t.set ("a.band1.sustain", 9.0f);
    const float rmsS = runRms (t, hits(), 2.0, 1.0);
    check (rmsS > rms0 * 1.12f, "sustain +9 dB lifts the tails (" + std::to_string (rmsS / rms0) + "x)");
}

// ===========================================================================
void testExciter()
{
    Exciter e;
    e.prepare (kSr, kBlock);
    check (e.latency() == Oversampler4x::kLatency, "exciter latency with oversampling");
    e.set ("band1.drive", 0.0f);
    check (impulsePeak (e) == e.latency(), "exciter at zero drive is a pure delay");

    for (int fam = 0; fam < 2; ++fam)
        for (float x : { 0.0f, 1.0f })
            for (float y : { 0.0f, 1.0f })
            {
                e.reset();
                e.set ("band1.drive", 60.0f); e.set ("band1.family", (float) fam); e.set ("band1.x", x); e.set ("band1.y", y);
                std::vector<float> l ((size_t) kSr / 2), r ((size_t) kSr / 2);
                Sine s (500.0, 0.5f);
                for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = s.next();
                for (size_t pos = 0; pos + kBlock <= l.size(); pos += kBlock) e.process (l.data() + pos, r.data() + pos, kBlock);
                std::vector<float> tail (l.begin() + 12000, l.end());
                const float h = harmonicsDb (tail, 500.0);
                bool finite = true;
                for (float v : l) finite = finite && std::isfinite (v);
                const int corner = y > 0.5f ? (x > 0.5f ? 1 : 0) : (x > 0.5f ? 3 : 2);
                check (finite && h > -50.0f, std::string ("exciter corner makes harmonics: ") + Exciter::shapeName (fam * 4 + corner) + " (" + std::to_string (h) + " dB)");
            }
    // Tame keeps the output level following the input's dynamics.
    e.setDefaults();
    e.reset();
    e.set ("band1.drive", 80.0f); e.set ("band1.family", 1.0f); e.set ("tame", 1.0f);
    const float loudT = runRms (e, sineGen (300.0, 0.5f), 0.6, 0.3);
    e.reset();
    const float quietT = runRms (e, sineGen (300.0, 0.05f), 0.6, 0.3);
    checkNear (dbOf (loudT) - dbOf (quietT), 20.0, 2.0, "tame: 20 dB in, about 20 dB out");
}

// ===========================================================================
void testClipper()
{
    Clipper c;
    c.prepare (kSr, kBlock);
    c.set ("a.band1.threshold", -6.0f);
    float pk = 0;
    runRms (c, sineGen (200.0, 1.0f), 0.5, 0.2, &pk);
    check (dbOf (pk) < -5.6f, "clipper keeps peaks at the threshold (" + std::to_string (dbOf (pk)) + " dB)");
    c.reset();
    c.set ("a.band1.threshold", 0.0f);
    c.set ("a.band1.boost", 6.0f);
    const float boosted = runRms (c, sineGen (200.0, 0.25f), 0.5, 0.2);
    check (dbOf (boosted) > dbOf (0.25f * 0.70711f) + 4.0f, "soft clip boost adds level");
    check (Clipper::clip (0.1f, 0.0f, 0.0f) == 0.1f, "clipper transparent under the knee");
    check (std::fabs (Clipper::clip (5.0f, 6.0f, -3.0f)) <= dbToGain (-3.0f) + 1e-6f, "clipper curve never exceeds its threshold");
}

// ===========================================================================
void testDensity()
{
    Density d;
    d.prepare (kSr, kBlock);
    d.set ("a.band1.threshold", -20.0f);
    d.set ("a.band1.range", 6.0f);
    d.set ("a.band1.ratio", 10.0f);
    d.set ("a.band1.speed", 20.0f);
    const float quiet = dbOf (runRms (d, sineGen (300.0, dbToGain (-40.0f)), 1.5, 1.0));
    checkNear (quiet - (-40.0f + kSineRmsDb), 6.0, 0.6, "density lifts quiet content by its range");
    d.reset();
    const float loud = dbOf (runRms (d, sineGen (300.0, dbToGain (-6.0f)), 1.0, 0.5));
    checkNear (loud, -6.0f + kSineRmsDb, 0.2, "density leaves content above the threshold alone");
    check (d.latency() > 0, "density has a look-ahead");
}

// ===========================================================================
void testPhase()
{
    Phase p;
    p.prepare (kSr, kBlock);
    check (impulsePeak (p) == Phase::kFrame, "phase at 0 degrees is a delay of one frame");
    p.set ("rotation", 180.0f);
    p.reset();
    {
        std::vector<float> l (16384), r (16384), ref (16384);
        Sine s (440.0, 0.5f);
        for (size_t i = 0; i < l.size(); ++i) ref[i] = l[i] = r[i] = s.next();
        for (size_t pos = 0; pos < l.size(); pos += kBlock) p.process (l.data() + pos, r.data() + pos, kBlock);
        double err = 0;
        for (size_t i = 6000; i < l.size(); ++i) err = std::max (err, (double) std::fabs (l[i] + ref[i - Phase::kFrame]));
        check (err < 0.01, "phase at 180 degrees inverts");
    }
    // An asymmetric waveform gets lower peaks.
    std::vector<float> x (1 << 15);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / kSr;
        x[i] = (float) (0.5 * std::cos (kTwoPi * 200.0 * t) + 0.5 * std::cos (kTwoPi * 400.0 * t));
    }
    float gainDb = 0;
    const float rot = Phase::bestRotation (x, gainDb);
    check (gainDb < -0.5f, "rotation learn lowers the peaks (" + std::to_string (gainDb) + " dB at " + std::to_string (rot) + " deg)");
    // Delay learn finds a known lag, and a polarity flip.
    std::vector<float> sc (x.size(), 0.0f), nx (x.size());
    const int lag = 137;
    NoiseGen ng;
    for (auto& v : nx) v = ng.next() * 0.3f;
    for (size_t i = lag; i < x.size(); ++i) sc[i] = -nx[i - lag];
    bool flipped = false;
    const int found = Phase::bestDelay (nx, sc, 960, flipped);
    check (found == lag, "delay learn finds the lag (" + std::to_string (found) + ")");
    check (flipped, "delay learn notices the polarity flip");
}

// ===========================================================================
void testSpectral()
{
    Sculptor s;
    s.prepare (kSr, kBlock);
    s.set ("a.amount", 0.0f);
    check (impulsePeak (s, 16384) == SpectralModule::kFrame, "sculptor at amount 0 is a delay of one frame");
    // With amount, white noise (much brighter than any target) gets its top cut.
    s.reset();
    s.set ("a.amount", 100.0f);
    s.set ("target", (float) InstrumentTargets::FullMix);
    s.set ("a.speed", 100.0f);
    NoiseGen ng;
    auto gen = [&ng] (float& l, float& r, float*) { l = r = 0.05f * ng.next(); };
    runRms (s, gen, 3.0, 2.5);
    float highGain = 0.0f;
    for (int b = 0; b < SpectralBands::kCount; ++b) if (SpectralBands::centreHz (b) > 8000.0f) highGain = std::min (highGain, s.gainDb[0][b]);
    check (highGain < -3.0f, "sculptor pulls bright noise toward the target (" + std::to_string (highGain) + " dB up top)");

    // Unmask: attenuates where the sidechain sits, nothing elsewhere.
    Unmask u;
    u.prepare (kSr, kBlock);
    u.set ("a.amount", 100.0f);
    u.set ("a.sensitivity", 80.0f);
    BlockContext ctx;
    std::vector<float> sc (kBlock);
    NoiseGen n2, n3;
    n3.s = 999u;
    Biquad bp; bp.bandPass (1000.0, 2.0, kSr);
    // The track is louder than the sidechain around 1 kHz, so it masks it there.
    auto both = [&n2, &n3, &bp] (float& l, float& r, float* scv) { l = r = 0.3f * n2.next(); if (scv) *scv = 0.25f * bp.tick (n3.next()); };
    runRms (u, both, 3.0, 2.5, nullptr, &ctx, &sc);
    float atK = 0.0f, at100 = 0.0f;
    for (int b = 0; b < SpectralBands::kCount; ++b)
    {
        const float hz = SpectralBands::centreHz (b);
        if (hz > 800.0f && hz < 1300.0f) atK = std::min (atK, u.gainDb[0][b]);
        if (hz < 100.0f) at100 = std::min (at100, u.gainDb[0][b]);
    }
    check (atK < -3.0f, "unmask cuts where the sidechain sits (" + std::to_string (atK) + " dB)");
    check (at100 > atK + 3.0f, "unmask leaves the rest alone (" + std::to_string (at100) + " dB)");
}

// ===========================================================================
void testChain()
{
    std::vector<float> l (kBlock), r (kBlock);
    {
        Chain chain;
        chain.prepare (kSr, kBlock);
        ChainSettings s;
        chain.setOrder ({ ModEqualizer, ModExciter, ModPhase });
        chain.process (l.data(), r.data(), kBlock, nullptr, nullptr, s);
        OutputLimiter probe; probe.prepare (kSr);
        const int expectLat = chain.eq.latency() + chain.exciter.latency() + chain.phase.latency() + probe.latencyFor (s.limiterMode);
        check (chain.latency() == expectLat, "chain latency sums the modules and the limiter");
    }

    // An impulse through the chain lands at its latency.
    {
        Chain c2;
        c2.prepare (kSr, kBlock);
        c2.setOrder ({ ModEqualizer, ModExciter, ModPhase, ModDensity, ModClipper });
        ChainSettings s2;
        std::vector<float> a (16384, 0.0f), b (16384, 0.0f);
        a[300] = b[300] = 0.5f;
        for (int pos = 0; pos < 16384; pos += kBlock) c2.process (a.data() + pos, b.data() + pos, kBlock, nullptr, nullptr, s2);
        int best = 0;
        for (int i = 0; i < 16384; ++i) if (std::fabs (a[(size_t) i]) > std::fabs (a[(size_t) best])) best = i;
        check (best - 300 == c2.latency(), "an impulse comes out at the reported latency (" + std::to_string (best - 300) + " vs " + std::to_string (c2.latency()) + ")");
    }

    // Module MIX at 0% gives the aligned dry signal, even with latency.
    {
        Chain c3;
        c3.prepare (kSr, kBlock);
        c3.setOrder ({ ModExciter, ModPhase });
        c3.exciter.set ("band1.drive", 80.0f);
        c3.exciter.set ("mix", 0.0f);
        c3.phase.set ("rotation", 90.0f);
        c3.phase.set ("mix", 0.0f);
        c3.reset();
        ChainSettings s3;
        std::vector<float> a (16384), b (16384), ref (16384);
        Sine sn (700.0, 0.5f);
        for (size_t i = 0; i < a.size(); ++i) ref[i] = a[i] = b[i] = sn.next();
        for (int pos = 0; pos < 16384; pos += kBlock) c3.process (a.data() + pos, b.data() + pos, kBlock, nullptr, nullptr, s3);
        const int lat = c3.latency();
        double err = 0;
        for (size_t i = 8000; i < a.size(); ++i) err = std::max (err, (double) std::fabs (a[i] - ref[i - (size_t) lat]));
        check (err < 1e-3, "module mix at 0% is the latency-aligned dry signal");
    }

    // Bypass plays the delayed input.
    {
        Chain c4;
        c4.prepare (kSr, kBlock);
        c4.setOrder ({ ModEqualizer, ModPhase });
        c4.eq.set ("a.b2.gain", 9.0f);
        ChainSettings s4; s4.bypass = true;
        std::vector<float> a (16384), b (16384), ref (16384);
        Sine sn (440.0, 0.4f);
        for (size_t i = 0; i < a.size(); ++i) ref[i] = a[i] = b[i] = sn.next();
        c4.reset();
        for (int pos = 0; pos < 16384; pos += kBlock) c4.process (a.data() + pos, b.data() + pos, kBlock, nullptr, nullptr, s4);
        double err = 0;
        for (size_t i = 8000; i < a.size(); ++i) err = std::max (err, (double) std::fabs (a[i] - ref[i - (size_t) c4.latency()]));
        check (err < 1e-3, "bypass plays the delayed input");
    }

    // Limiter ceiling in every mode, width, pan, invert.
    {
        Chain c5;
        c5.prepare (kSr, kBlock);
        c5.setOrder ({});
        ChainSettings s5; s5.limiterOn = true; s5.ceilingDb = -6.0f; s5.outGainDb = 6.0f;
        Sine sn (100.0, 0.9f);
        for (int mode = 0; mode < 4; ++mode)
        {
            c5.reset();
            s5.limiterMode = mode;
            float pk = 0;
            for (int blk = 0; blk < 300; ++blk)
            {
                for (int i = 0; i < kBlock; ++i) l[(size_t) i] = r[(size_t) i] = sn.next();
                c5.process (l.data(), r.data(), kBlock, nullptr, nullptr, s5);
                if (blk > 50) for (int i = 0; i < kBlock; ++i) pk = std::max (pk, std::fabs (l[(size_t) i]));
            }
            check (dbOf (pk) < -5.7f, "limiter mode " + std::to_string (mode) + " holds the ceiling (" + std::to_string (dbOf (pk)) + ")");
        }
        ChainSettings s6; s6.width = -100.0f;
        c5.reset();
        for (int k = 0; k < 40; ++k) { for (int i = 0; i < kBlock; ++i) { l[(size_t) i] = 0.5f; r[(size_t) i] = -0.1f; } c5.process (l.data(), r.data(), kBlock, nullptr, nullptr, s6); }
        checkNear (l[100] - r[100], 0.0, 1e-3, "width -100% is mono");
        ChainSettings s7; s7.invert = true; s7.pan = 100.0f;
        for (int k = 0; k < 40; ++k) { for (int i = 0; i < kBlock; ++i) { l[(size_t) i] = 0.5f; r[(size_t) i] = 0.5f; } c5.process (l.data(), r.data(), kBlock, nullptr, nullptr, s7); }
        checkNear (l[100], 0.0, 1e-3, "pan hard right silences the left");
        checkNear (r[100], -0.5, 1e-3, "invert flips the polarity");
    }

    // ZERO LATENCY: spectral modules, phase rotation and look-aheads drop out.
    {
        Chain c6;
        c6.prepare (kSr, kBlock);
        c6.setOrder ({ ModSculptor, ModUnmask, ModPhase, ModExciter, ModClipper, ModDensity });
        ChainSettings s8; s8.zeroLatency = true; s8.limiterOn = true;
        c6.process (l.data(), r.data(), kBlock, nullptr, nullptr, s8);
        check (c6.latency() == 0, "zero latency means zero latency (" + std::to_string (c6.latency()) + ")");
    }

    // Every preset: its parameters exist and it runs finite.
    for (const auto& p : factoryPresets())
    {
        Chain c;
        c.prepare (kSr, kBlock);
        for (const auto& [id, v] : p.values)
        {
            const std::string sid (id);
            bool found = sid.rfind ("io.", 0) == 0;
            for (int m = 0; m < NumModules && ! found; ++m)
            {
                const int idx = c.module (m).indexOf (sid);
                if (idx >= 0) { c.module (m).setValue (idx, v); found = true; }
            }
            check (found, std::string ("preset parameter exists: ") + p.name + " " + id);
        }
        c.setOrder (p.order);
        c.reset();
        ChainSettings cs;
        NoiseGen ng;
        Sine sn (110.0, 0.3f);
        bool finite = true;
        std::vector<float> scv (kBlock);
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int i = 0; i < kBlock; ++i) { const float v = sn.next() + 0.05f * ng.next(); l[(size_t) i] = v; r[(size_t) i] = v * 0.8f; scv[(size_t) i] = 0.3f * ng.next(); }
            c.process (l.data(), r.data(), kBlock, scv.data(), scv.data(), cs);
            for (int i = 0; i < kBlock; ++i) finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]) && std::fabs (l[(size_t) i]) < 20.0f;
        }
        check (finite, std::string ("preset runs finite: ") + p.name);
    }
    {
        Chain c;
        for (const auto& mp : factoryModulePresets())
            for (const auto& [id, v] : mp.values)
                check (c.module (mp.module).indexOf (id) >= 0, std::string ("module preset parameter exists: ") + mp.name + " " + id);
    }

    // Parameter ids are unique, and every module starts with ON and MIX.
    std::vector<std::string> ids;
    Chain c;
    for (int m = 0; m < NumModules; ++m)
    {
        check (c.module (m).pOn == 0 && c.module (m).pMix == 1, std::string ("on and mix first: ") + Chain::moduleName (m));
        for (const auto& d : c.module (m).params()) ids.push_back (d.id);
    }
    std::sort (ids.begin(), ids.end());
    check (std::adjacent_find (ids.begin(), ids.end()) == ids.end(), "parameter ids unique");
    std::printf ("%d parameters across %d modules\n", (int) ids.size(), (int) NumModules);
}

// ===========================================================================
void testAnalysis()
{
    Analyzer a;
    a.prepare (kSr);
    Sine s (220.0, 0.3f);
    NoiseGen ng;
    std::vector<float> l (kBlock), r (kBlock);
    for (int blk = 0; blk < (int) (kSr * 6 / kBlock); ++blk)
    {
        for (int i = 0; i < kBlock; ++i) { const float v = s.next() * ((blk / 40) % 2 ? 1.0f : 0.2f) + 0.01f * ng.next(); l[(size_t) i] = v; r[(size_t) i] = v; }
        a.push (l.data(), r.data(), kBlock);
    }
    const auto res = a.result();
    check (res.valid, "analysis valid after 6 s");
    check (res.integratedLufs > -40.0f && res.integratedLufs < 0.0f, "analysis loudness plausible");
    check (res.loudDb > res.floorDb + 6.0f, "analysis level spread");
    check (res.widthDb < -20.0f, "analysis width: mono");
    checkNear (res.centroidHz, 220.0, 400.0, "analysis centroid near the tone");
    const auto t = res.asTarget ("Test");
    check (std::isfinite (t.points[5]), "analysis makes a target curve");
    check (std::string (InstrumentTargets::name (InstrumentTargets::Kick)) == "Kick", "instrument target names");
}
} // namespace

int main()
{
    testBuildingBlocks();
    testGate();
    testEqualizer();
    testCompressor();
    testTransientShaper();
    testExciter();
    testClipper();
    testDensity();
    testPhase();
    testSpectral();
    testChain();
    testAnalysis();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
