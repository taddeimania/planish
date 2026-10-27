// Sidechain.h — where a dynamics band takes its detection signal from, as
// the manual's sidechain menu offers it: the band itself, the full internal
// signal or another internal band, the full external sidechain or the
// external sidechain filtered through one of the module's bands. Also the
// detection filter parameters the Gate and Compressor share, and a small
// scope recorder for the tempo-synced oscilloscopes.
#pragma once

#include "Dsp.h"
#include "Filters.h"
#include "Module.h"

#include <array>
#include <atomic>

namespace planish
{
// Choices for a multiband module with `bands` bands.
inline std::vector<std::string> sidechainChoices (int bands, const char* bandWord = "Band")
{
    std::vector<std::string> v { "Self", "Int. Full" };
    for (int b = 0; b < bands; ++b) v.push_back (std::string ("Int. ") + bandWord + " " + std::to_string (b + 1));
    v.push_back ("Ext. Full");
    for (int b = 0; b < bands; ++b) v.push_back (std::string ("Ext. ") + bandWord + " " + std::to_string (b + 1));
    return v;
}

struct SidechainChoice
{
    bool external = false;
    int band = -1;          // -1 full
    bool self = false;
};
inline SidechainChoice decodeSidechain (int choice, int bands, int ownBand)
{
    SidechainChoice c;
    if (choice <= 0) { c.self = true; c.band = ownBand; return c; }
    if (choice == 1) return c;                                   // internal full
    if (choice < 2 + bands) { c.band = choice - 2; return c; }   // internal band
    c.external = true;
    if (choice == 2 + bands) return c;                           // external full
    c.band = std::min (bands - 1, choice - 3 - bands);
    return c;
}

// The detection filter: on, solo (UI), a resonant high-pass node and a
// resonant low-pass node.
struct DetectionParams
{
    int pOn = -1, pHpFreq = -1, pHpGain = -1, pLpFreq = -1, pLpGain = -1;
    template <typename M>
    void declare (M& m)
    {
        pOn     = m.addBool ("det.on", "Detection Filter On", false);
        pHpFreq = m.addHz ("det.hpfreq", "Detection HP Freq", 20.0f, 20000.0f, 20.0f);
        pHpGain = m.addDb ("det.hpgain", "Detection HP Resonance", -12.0f, 18.0f, 0.0f);
        pLpFreq = m.addHz ("det.lpfreq", "Detection LP Freq", 20.0f, 20000.0f, 20000.0f);
        pLpGain = m.addDb ("det.lpgain", "Detection LP Resonance", -12.0f, 18.0f, 0.0f);
    }
};

// Records the decimated output (and sidechain) of a module with its bar
// position, for the oscilloscopes that show one bar (or a fraction of it).
class ScopeRecorder
{
public:
    static constexpr int kSize = 4096;
    static constexpr int kDecim = 64;
    struct Entry { float lo = 0, hi = 0, scLo = 0, scHi = 0; double ppq = 0; };

    void push (float main, float sc, double ppq)
    {
        if (count == 0) { lo = hi = main; slo = shi = sc; }
        lo = std::min (lo, main); hi = std::max (hi, main);
        slo = std::min (slo, sc); shi = std::max (shi, sc);
        if (++count >= kDecim)
        {
            const int w = writePos.load (std::memory_order_relaxed);
            auto& e = ring[(size_t) (w % kSize)];
            e.lo = lo; e.hi = hi; e.scLo = slo; e.scHi = shi; e.ppq = ppq;
            writePos.store (w + 1, std::memory_order_release);
            count = 0;
        }
    }
    int position() const { return writePos.load (std::memory_order_acquire); }
    const Entry& at (int index) const { return ring[(size_t) (((index % kSize) + kSize) % kSize)]; }
    void clear() { for (auto& e : ring) e = {}; writePos.store (0); count = 0; }

private:
    std::array<Entry, kSize> ring {};
    std::atomic<int> writePos { 0 };
    int count = 0;
    float lo = 0, hi = 0, slo = 0, shi = 0;
};
} // namespace planish
