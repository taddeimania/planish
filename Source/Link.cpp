#include "Link.h"
#include "PluginProcessor.h"

namespace planish
{
LinkRegistry& LinkRegistry::get()
{
    static LinkRegistry r;
    return r;
}

int LinkRegistry::add (PlanishProcessor* p)
{
    const juce::ScopedLock sl (lock);
    list.push_back (p);
    return ++counter;
}

void LinkRegistry::remove (PlanishProcessor* p)
{
    const juce::ScopedLock sl (lock);
    list.erase (std::remove (list.begin(), list.end(), p), list.end());
}

std::vector<PlanishProcessor*> LinkRegistry::all() const
{
    const juce::ScopedLock sl (lock);
    return list;
}

PlanishProcessor* LinkRegistry::find (int instanceId) const
{
    const juce::ScopedLock sl (lock);
    for (auto* p : list)
        if (p->instanceId() == instanceId) return p;
    return nullptr;
}

bool LinkRegistry::contains (const PlanishProcessor* p) const
{
    const juce::ScopedLock sl (lock);
    return std::find (list.begin(), list.end(), p) != list.end();
}

int LinkRegistry::nextNumber() const
{
    const juce::ScopedLock sl (lock);
    return counter + 1;
}

// ---------------------------------------------------------------------------
namespace
{
const float kBarkEdges[Masking::kBands + 1] {
    20, 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720, 2000, 2320, 2700,
    3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500 };
}

float Masking::bandLowHz (int b) { return kBarkEdges[juce::jlimit (0, kBands - 1, b)]; }
float Masking::bandHighHz (int b) { return kBarkEdges[juce::jlimit (1, kBands, b + 1)]; }

void Masking::loudnessLoss (const float* sourceDb, const float* maskerDb, float* loss)
{
    constexpr double alpha = 0.23;
    for (int b = 0; b < kBands; ++b)
    {
        // Energies relative to an assumed threshold of hearing near -90 dBFS.
        const double es = std::pow (10.0, (sourceDb[b] + 90.0) / 10.0);
        const double em = std::pow (10.0, (maskerDb[b] + 90.0) / 10.0);
        if (es < 1.0) { loss[b] = 0.0f; continue; }
        const double alone = std::pow (es, alpha);
        const double partial = std::max (1e-9, std::pow (es + em, alpha) - std::pow (em, alpha));
        // Sones to phons: 10 phons per doubling of loudness.
        loss[b] = (float) juce::jlimit (0.0, 60.0, 10.0 * std::log2 (alone / partial));
    }
}
} // namespace planish
