// Link.h — how Planish instances in one session know about each other,
// for the masking meter and the Visual Mixer: every instance registers
// itself here when it is created and leaves when it is destroyed. A DAW
// loads all instances of a plugin into one process, so a plain shared
// list is enough. Message thread only (the lock is for safety when a host
// builds instances on another thread).
#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <map>
#include <vector>

class PlanishProcessor;

namespace planish
{
class LinkRegistry
{
public:
    static LinkRegistry& get();

    int add (PlanishProcessor*);          // returns the instance's number (1, 2, ...)
    void remove (PlanishProcessor*);
    std::vector<PlanishProcessor*> all() const;
    PlanishProcessor* find (int instanceId) const;
    bool contains (const PlanishProcessor*) const;
    int nextNumber() const;

    // The Visual Mixer's snapshots, for the whole session: per instance,
    // output gain, pan, width and hidden.
    struct MixSnapshot { std::map<int, std::array<float, 4>> values; bool set = false; };
    MixSnapshot snapshots[3];

private:
    mutable juce::CriticalSection lock;
    std::vector<PlanishProcessor*> list;
    int counter = 0;
};

// Masking: the loudness a source loses in each critical band to a masker,
// from the two spectra (dB, same scale). A simplified partial-loudness
// model: loudness grows as energy^0.23; the masker's energy in a band takes
// its share of the band's loudness away from the source.
struct Masking
{
    static constexpr int kBands = 24;   // critical bands (Bark), 20 Hz .. 15.5 kHz
    static float bandLowHz (int b);
    static float bandHighHz (int b);
    static float bandCentreHz (int b) { return std::sqrt (bandLowHz (b) * bandHighHz (b)); }
    // Loudness loss in phons for each band.
    static void loudnessLoss (const float* sourceDb, const float* maskerDb, float* lossPhons);
};
} // namespace planish
