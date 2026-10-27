// Multiband.h — the band count and crossovers shared by the multiband
// modules (Gate, Compressor, Transient Shaper, Exciter, Clipper, Density):
// up to three bands, two crossover points, an on/off per band.
#pragma once

#include "Channels.h"
#include "Dsp.h"
#include "Module.h"

namespace planish
{
struct MultibandParams
{
    static constexpr int kMaxBands = 3;
    int pNumBands = -1;
    int pXover[2] { -1, -1 };
    int pBandOn[kMaxBands] { -1, -1, -1 };

    // Call from a module constructor.
    template <typename M>
    void declare (M& m, float f1 = 200.0f, float f2 = 3000.0f)
    {
        pNumBands = m.addChoice ("bands", "Bands", { "1", "2", "3" }, 0);
        pXover[0] = m.addHz ("xover1", "Crossover 1", 20.0f, 20000.0f, f1);
        pXover[1] = m.addHz ("xover2", "Crossover 2", 20.0f, 20000.0f, f2);
        for (int b = 0; b < kMaxBands; ++b)
            pBandOn[b] = m.addBool ("band" + std::to_string (b + 1) + ".on", "Band " + std::to_string (b + 1) + " On", true);
    }
    int numBands (const Module& m) const { return std::min (kMaxBands, std::max (1, (int) m.value (pNumBands) + 1)); }
};

// Splits up to four lanes into up to three bands each.
class MultibandSplit
{
public:
    void prepare (double sr) { for (auto& x : xo) x.prepare (sr); last[0] = -1.0f; }
    void reset() { for (auto& x : xo) x.reset(); }

    // Recomputes the crossovers only when something changed.
    void update (int numBands, float f1, float f2)
    {
        const float v[3] { (float) numBands, f1, f2 };
        bool changed = false;
        for (int i = 0; i < 3; ++i) if (v[i] != last[i]) { changed = true; last[i] = v[i]; }
        if (! changed) return;
        const float f[3] { std::min (f1, f2 / 1.05f), f2, 20000.0f };
        for (auto& x : xo) x.set (numBands, f);
        bands = std::min (3, std::max (1, numBands));
    }
    int numBands() const noexcept { return bands; }
    void split (int lane, float x, float* out) { xo[(size_t) lane].tick (x, out); }

private:
    Crossover4 xo[Lanes::kMax];
    float last[3] { -1.0f, -1.0f, -1.0f };
    int bands = 1;
};
} // namespace planish
