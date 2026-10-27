// Targets.h — the instrument targets: an idealised tonal balance for each
// kind of source, as dB deviation from pink noise (pink reads flat).
// Sculptor aims at them, the assistant picks one for the track, and the
// tonal balance meter draws the selected one.
#pragma once

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace planish
{
struct InstrumentTargets
{
    enum Id
    {
        Vocal = 0, BackingVocals, Dialogue, Kick, Snare, DrumBus, Overheads, Bass,
        ElectricGuitar, AcousticGuitar, Piano, Synth, Strings, Brass, Percussion, FullMix,
        NumTargets
    };
    static constexpr int kPoints = 11;   // 20, 40, 80 ... 20480 Hz, one per octave
    static const char* name (int t);
    static const char* category (int t);  // "VOICE", "DRUMS", "BASS", "MUSICAL", "MIX"
    // The target at a frequency (log-interpolated between the octave points).
    static float curveDb (int target, float hz);
    static const float* points (int target);
};

// A curve from a reference file (or the assistant), the same shape as a
// target: dB deviation from pink at the eleven octave points.
struct CustomTarget
{
    std::string name;
    std::array<float, InstrumentTargets::kPoints> points {};
    float curveDb (float hz) const
    {
        const float pos = std::log2 (std::max (20.0f, std::min (20480.0f, hz)) / 20.0f);
        const int i = std::min (InstrumentTargets::kPoints - 2, (int) pos);
        const float f = pos - (float) i;
        return points[(size_t) i] + (points[(size_t) i + 1] - points[(size_t) i]) * f;
    }
};
} // namespace planish
