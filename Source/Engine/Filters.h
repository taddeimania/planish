// Filters.h — one EQ band in every shape the Equalizer offers (the
// thirteen of the manual: three peaks, three low shelves, three high
// shelves, two high passes, two low passes), as a small cascade of
// biquads, plus its frequency response for drawing.
#pragma once

#include "Dsp.h"

namespace planish
{
enum FilterShape
{
    ProportionalQ = 0, Bell, BandShelf,
    LowShelfAnalog, LowShelfVintage, LowShelfBaxandall,
    HighShelfAnalog, HighShelfVintage, HighShelfBaxandall,
    HighPassFlat, HighPassResonant,
    LowPassFlat, LowPassResonant,
    NumFilterShapes
};

inline const char* filterShapeName (int s)
{
    static const char* names[NumFilterShapes] {
        "Proportional Q", "Bell", "Band Shelf",
        "Low Shelf Analog", "Low Shelf Vintage", "Low Shelf Baxandall",
        "High Shelf Analog", "High Shelf Vintage", "High Shelf Baxandall",
        "High Pass Flat", "High Pass Resonant",
        "Low Pass Flat", "Low Pass Resonant" };
    return names[std::min (NumFilterShapes - 1, std::max (0, s))];
}
inline const char* filterShapeShort (int s)
{
    static const char* names[NumFilterShapes] {
        "PROP Q", "BELL", "BAND SHELF",
        "LOW ANALOG", "LOW VINTAGE", "LOW BAX",
        "HIGH ANALOG", "HIGH VINTAGE", "HIGH BAX",
        "HP FLAT", "HP RESO",
        "LP FLAT", "LP RESO" };
    return names[std::min (NumFilterShapes - 1, std::max (0, s))];
}
inline bool shapeIsPass (int s)      { return s >= HighPassFlat; }
inline bool shapeIsHighPass (int s)  { return s == HighPassFlat || s == HighPassResonant; }
inline bool shapeIsLowPass (int s)   { return s == LowPassFlat || s == LowPassResonant; }
inline bool shapeIsLowShelf (int s)  { return s >= LowShelfAnalog && s <= LowShelfBaxandall; }
inline bool shapeIsHighShelf (int s) { return s >= HighShelfAnalog && s <= HighShelfBaxandall; }
inline bool shapeIsPeak (int s)      { return s <= BandShelf; }
inline bool shapeHasGain (int s)     { return ! shapeIsPass (s); }
inline bool shapeHasQ (int s)        { return s != LowShelfBaxandall && s != HighShelfBaxandall && s != HighPassFlat && s != LowPassFlat; }
inline bool shapeUsesSlope (int s)   { return shapeIsPass (s); }
inline bool shapeCanBeDynamic (int s) { return ! shapeIsPass (s); }

// Slope choices for pass filters, dB/oct.
inline const float kSlopes[] { 6.0f, 12.0f, 18.0f, 24.0f, 36.0f, 48.0f };
inline constexpr int kNumSlopes = 6;

struct FilterBand
{
    Biquad f1, f2;          // most shapes use one; vintage/band-shelf use two
    FilterCascade cascade;  // pass filters
    int kind = 0;           // 0 none, 1 one biquad, 2 two biquads, 3 cascade

    void reset() { f1.reset(); f2.reset(); cascade.reset(); }

    // gain in dB; q is Q for bells/shelves, resonance for resonant passes;
    // slope index 0..5 for passes.
    void design (int shape, double hz, double gainDb, double q, int slopeIdx, double sr)
    {
        q = std::max (0.05, q);
        slopeIdx = std::min (kNumSlopes - 1, std::max (0, slopeIdx));
        kind = 1;
        switch (shape)
        {
            case Bell:          f1.peak (hz, q, gainDb, sr); break;
            case ProportionalQ:
            {
                // Narrows as the gain grows: tight, precise cuts.
                const double qe = q * clampf (0.45f + (float) std::fabs (gainDb) / 9.0f, 0.45f, 3.5f);
                f1.peak (hz, qe, gainDb, sr);
                break;
            }
            case BandShelf:
            {
                // Flat top: a high shelf up at f_lo and the complement down at f_hi.
                const double w = 1.6 / q;   // width in octaves
                const double lo = hz / std::pow (2.0, w * 0.5), hi = hz * std::pow (2.0, w * 0.5);
                f1.highShelf (lo, gainDb, sr, 0.9);
                f2.highShelf (hi, -gainDb, sr, 0.9);
                kind = 2;
                break;
            }
            case LowShelfAnalog:    f1.lowShelf (hz, gainDb, sr, std::min (2.0, 0.5 + q * 0.2)); break;
            case LowShelfBaxandall: f1.lowShelf1 (hz, gainDb, sr); break;
            case LowShelfVintage:
                // A Pultec-style shelf: the boost with a complementary dip above it.
                f1.lowShelf (hz, gainDb, sr, 0.7071);
                f2.peak (hz * 2.8, 0.9 * std::max (0.4, q), -gainDb * 0.4, sr);
                kind = 2;
                break;
            case HighShelfAnalog:    f1.highShelf (hz, gainDb, sr, std::min (2.0, 0.5 + q * 0.2)); break;
            case HighShelfBaxandall: f1.highShelf1 (hz, gainDb, sr); break;
            case HighShelfVintage:
                f1.highShelf (hz, gainDb, sr, 0.7071);
                f2.peak (hz / 2.8, 0.9 * std::max (0.4, q), -gainDb * 0.4, sr);
                kind = 2;
                break;
            case HighPassFlat:      cascade.highPass (hz, sr, slopeToOrderLocal (kSlopes[slopeIdx])); kind = 3; break;
            case HighPassResonant:  cascade.highPass (hz, sr, std::max (2, slopeToOrderLocal (kSlopes[slopeIdx])), 0.5 + q); kind = 3; break;
            case LowPassFlat:       cascade.lowPass (hz, sr, slopeToOrderLocal (kSlopes[slopeIdx])); kind = 3; break;
            case LowPassResonant:   cascade.lowPass (hz, sr, std::max (2, slopeToOrderLocal (kSlopes[slopeIdx])), 0.5 + q); kind = 3; break;
            default: kind = 0; break;
        }
    }

    float tick (float x)
    {
        switch (kind)
        {
            case 1: return f1.tick (x);
            case 2: return f2.tick (f1.tick (x));
            case 3: return cascade.tick (x);
            default: return x;
        }
    }

    double responseDb (double hz, double sr) const
    {
        switch (kind)
        {
            case 1: return f1.responseDb (hz, sr);
            case 2: return f1.responseDb (hz, sr) + f2.responseDb (hz, sr);
            case 3: return cascade.responseDb (hz, sr);
            default: return 0.0;
        }
    }

private:
    static int slopeToOrderLocal (float dbPerOct) { return std::max (1, std::min (8, (int) std::lround (dbPerOct / 6.0f))); }
};

// Bandpass used to solo a band, and as a dynamic band's detector: the
// region the band affects.
struct SoloFilter
{
    Biquad bp1, bp2;
    FilterCascade pass;
    int mode = 0;   // 0 bandpass, 1 lowpass, 2 highpass
    void design (int shape, double hz, double q, double sr)
    {
        if (shapeIsLowShelf (shape) || shapeIsLowPass (shape)) { mode = 1; pass.lowPass (hz, sr, 4); }
        else if (shapeIsHighShelf (shape) || shapeIsHighPass (shape)) { mode = 2; pass.highPass (hz, sr, 4); }
        else { mode = 0; bp1.bandPass (hz, std::max (0.3, q), sr); bp2 = bp1; }
    }
    void reset() { bp1.reset(); bp2.reset(); pass.reset(); }
    float tick (float x) { return mode == 0 ? bp2.tick (bp1.tick (x)) : pass.tick (x); }
};

// The detection filter of the Gate and Compressor: a resonant high-pass and
// a resonant low-pass, each with a node (frequency, resonance gain).
struct DetectionFilter
{
    Biquad hp, lp;
    void design (double hpHz, double hpGainDb, double lpHz, double lpGainDb, double sr)
    {
        hp.highPass (hpHz, qFor (hpGainDb), sr);
        lp.lowPass (lpHz, qFor (lpGainDb), sr);
    }
    // A node's height is the resonance: 0 dB is a Butterworth corner.
    static double qFor (double db) { return 0.7071 * std::pow (10.0, std::max (-12.0, std::min (18.0, db)) / 20.0); }
    void reset() { hp.reset(); lp.reset(); }
    float tick (float x) { return lp.tick (hp.tick (x)); }
    double responseDb (double hz, double sr) const { return hp.responseDb (hz, sr) + lp.responseDb (hz, sr); }
};
} // namespace planish
