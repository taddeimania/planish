#include "Exciter.h"

namespace planish
{
Exciter::Exciter()
    : Module ({ "exc", "EXCITER", "EXCITER", 0xffe08a5cu, kStereoOnly })
{
    pTame       = addBool ("tame", "Tame", false);
    pOversample = addBool ("oversample", "Oversampling", true);
    pEmphasis   = addChoice ("emphasis", "Pre-Emphasis", { "Full", "Defined", "Clear", "Flat" }, Flat);
    pTone       = addFloat ("tone", "Tone", -100.0f, 100.0f, 0.0f, "", 0);
    pPostFreq   = addHz ("postfreq", "Post Filter Freq", 1000.0f, 20000.0f, 12000.0f);
    pPostGain   = addDb ("postgain", "Post Filter Gain", -12.0f, 0.0f, 0.0f);
    mb.declare (*this, 250.0f, 3000.0f);
    for (int b = 0; b < kBands; ++b)
    {
        const std::string p = "band" + std::to_string (b + 1) + ".";
        const std::string n = "Band " + std::to_string (b + 1) + " ";
        band[b].drive  = addPct (p + "drive", n + "Drive", 0.0f, 100.0f, 20.0f);
        band[b].family = addChoice (p + "family", n + "Processing", { "Classic", "Trash" }, Classic);
        band[b].x      = addFloat (p + "x", n + "X", 0.0f, 1.0f, 0.5f, "", 2);
        band[b].y      = addFloat (p + "y", n + "Y", 0.0f, 1.0f, 0.5f, "", 2);
        band[b].mix    = addPct (p + "mix", n + "Mix", 0.0f, 100.0f, 100.0f);
    }
}

const char* Exciter::shapeName (int s)
{
    static const char* n[NumShapes] { "RETRO", "TAPE", "TUBE", "WARM", "OVERDRIVE", "SCREAM", "CLIPPER", "SCRATCH" };
    return n[std::min (NumShapes - 1, std::max (0, s))];
}

void Exciter::padWeights (float x, float y, float* w)
{
    x = clampf (x, 0.0f, 1.0f); y = clampf (y, 0.0f, 1.0f);
    w[0] = (1.0f - x) * y;            // top-left
    w[1] = x * y;                     // top-right
    w[2] = (1.0f - x) * (1.0f - y);   // bottom-left
    w[3] = x * (1.0f - y);            // bottom-right
}

// Every shape has unity slope at zero, so the small-signal level is
// untouched and only the harmonics differ.
float Exciter::shape (int s, float v)
{
    switch (s)
    {
        case Retro:     { const float c = clampf (v, -1.5f, 1.5f); return c - c * c * c / 6.75f; }   // edgy, odd
        case Tape:      return std::atan (v * 1.3f) / 1.3f;                                            // bright, odd
        case Tube:      return softTanh (v + 0.15f * v * v);                                           // asymmetric
        case Warm:      { const float t = softTanh (v); return v * 0.15f + 0.85f * t + 0.35f * t * t; } // even
        case Overdrive: return softTanh (v);
        case Scream:    return std::sin (2.5f * softTanh (v)) / 2.5f;                                 // folds
        case Clipper:   return clampf (v, -1.0f, 1.0f);
        case Scratch:   return v >= 0.0f ? softTanh (v) : 0.4f * softTanh (2.5f * v);                 // rectifying fuzz
        default:        return v;
    }
}

int Exciter::latency() const
{
    return oversampling() ? Oversampler4x::kLatency : 0;
}

void Exciter::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxBlockSize = maxBlock;
    split.prepare (sr);
    for (auto& row : st)
        for (auto& b : row)
        {
            b.env.set (5.0f, 120.0f, (float) sr);
            b.trans.prepare ((float) sr);
        }
    for (auto& d : alignDry) d.prepare (64);
    for (auto& s : driveSm) s.setTime (20.0f, (float) sr, kStep);
    for (auto& s : mixSm) s.setTime (20.0f, (float) sr, kStep);
    reset();
}

void Exciter::reset()
{
    split.reset();
    for (auto& row : st)
        for (auto& b : row) { b.os.reset(); b.dc = 0.0f; b.env.reset(); b.trans.reset(); b.harmMs = b.dryMs = 0.0f; b.pre.reset(); b.post.reset(); }
    for (auto& f : toneLo) f.reset();
    for (auto& f : toneHi) f.reset();
    for (auto& f : postShelf) f.reset();
    for (auto& d : alignDry) d.reset();
    for (int b = 0; b < kBands; ++b) { driveSm[b].snap (value (band[b].drive) * 0.01f); mixSm[b].snap (value (band[b].mix) * 0.01f); }
    last[0] = -1.0f;
}

void Exciter::process (float* l, float* r, int n)
{
    if (! isOn())
    {
        // Keep the latency while off so the chain stays aligned.
        if (latency() > 0)
            for (int i = 0; i < n; ++i) { l[i] = alignDry[0].tick (l[i]); r[i] = alignDry[1].tick (r[i]); }
        return;
    }
    const int nb = mb.numBands (*this);
    const bool os = oversampling();
    const int lat = latency();
    for (auto& d : alignDry) d.setDelay (lat);
    const bool tame = value (pTame) > 0.5f;
    const int solo = soloBand.load (std::memory_order_relaxed);

    for (int start = 0; start < n; start += kStep)
    {
        const int len = std::min (kStep, n - start);
        split.update (nb, value (mb.pXover[0]), value (mb.pXover[1]));
        const float v[4] { value (pEmphasis), value (pTone), value (pPostFreq), value (pPostGain) };
        if (v[0] != last[0] || v[1] != last[1] || v[2] != last[2] || v[3] != last[3])
        {
            for (int i = 0; i < 4; ++i) last[i] = v[i];
            const int e = (int) v[0];
            for (auto& row : st)
                for (auto& b : row)
                {
                    // The emphasis shapes what the shaper sees; its inverse
                    // follows on the harmonics, so the dry tone stays put.
                    if (e == Full)         { b.pre.peak (250.0, 0.8, 5.0, sr); b.post.peak (250.0, 0.8, -5.0, sr); }
                    else if (e == Defined) { b.pre.peak (2500.0, 0.8, 5.0, sr); b.post.peak (2500.0, 0.8, -5.0, sr); }
                    else if (e == Clear)   { b.pre.peak (300.0, 0.7, -5.0, sr); b.post.peak (300.0, 0.7, 5.0, sr); }
                    else                   { b.pre.flat(); b.post.flat(); }
                }
            const float t = v[1] * 0.01f;
            for (int c = 0; c < 2; ++c)
            {
                toneLo[c].lowShelf (700.0, -t * 5.0, sr);
                toneHi[c].highShelf (1400.0, t * 5.0, sr);
                postShelf[c].highShelf (v[2], v[3], sr);
            }
        }
        float drive[kBands], mix[kBands], w[kBands][4];
        int family[kBands];
        for (int b = 0; b < nb; ++b)
        {
            driveSm[b].target = value (band[b].drive) * 0.01f;
            mixSm[b].target = value (band[b].mix) * 0.01f;
            const float a = driveSm[b].next();
            family[b] = (int) value (band[b].family);
            drive[b] = 1.0f + a * a * (family[b] == Trash ? 40.0f : 11.0f);
            mix[b] = mixSm[b].next() * clampf (a * 4.0f, 0.0f, 1.0f);   // drive 0 is silent
            padWeights (value (band[b].x), value (band[b].y), w[b]);
        }

        for (int i = start; i < start + len; ++i)
        {
            float x[2] { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                float bands[4];
                split.split (c, x[c], bands);
                float dryOut = 0.0f, harmSum = 0.0f;
                for (int b = 0; b < nb; ++b)
                {
                    auto& bs = st[c][b];
                    const float dry = bands[b];
                    float harm = 0.0f;
                    const float envNow = bs.env.next (std::fabs (dry));
                    if (value (mb.pBandOn[b]) > 0.5f && mix[b] > 0.0f)
                    {
                        float pre = 1.0f;
                        if (tame) pre = clampf (0.25f / (envNow + 1.0e-5f), 1.0f / 16.0f, 16.0f);
                        float dr = drive[b] * pre;
                        // Tube leans on the attacks.
                        dr *= 1.0f + 1.2f * w[b][2] * (family[b] == Classic ? bs.trans.next (std::fabs (dry)) : 0.0f);
                        const int fam = family[b] * 4;
                        const float* wb = w[b];
                        auto fn = [fam, dr, wb] (float u)
                        {
                            const float d = u * dr;
                            const float y = wb[0] * shape (fam + 0, d) + wb[1] * shape (fam + 1, d) + wb[2] * shape (fam + 2, d) + wb[3] * shape (fam + 3, d);
                            return y / dr - u;   // the harmonics alone
                        };
                        const float in = bs.pre.tick (dry);
                        float h = os ? bs.os.process (in, fn) : fn (in);
                        h = bs.post.tick (h);
                        bs.dc += 0.0005f * (h - bs.dc);
                        h -= bs.dc;
                        harm = h * mix[b];
                        bs.harmMs += 0.0005f * (harm * harm - bs.harmMs);
                        bs.dryMs += 0.0005f * (dry * dry - bs.dryMs);
                    }
                    else if (os)
                        bs.os.process (0.0f, [] (float) { return 0.0f; });   // keep the filters flowing
                    if (solo < 0 || solo == b)
                    {
                        dryOut += dry;
                        harmSum += harm;
                    }
                }
                harmSum = postShelf[c].tick (toneHi[c].tick (toneLo[c].tick (harmSum)));
                x[c] = alignDry[c].tick (dryOut) + harmSum;
            }
            l[i] = x[0]; r[i] = x[1];
        }
    }
    for (int b = 0; b < kBands; ++b)
    {
        const auto& bs = st[0][b];
        harmonicLevel[b] = bs.dryMs > 1e-9f ? clampf (std::sqrt (bs.harmMs / bs.dryMs) * 2.0f, 0.0f, 1.0f) : 0.0f;
    }
}
} // namespace planish
