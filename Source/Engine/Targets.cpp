#include "Targets.h"

#include <algorithm>

namespace planish
{
namespace
{
// Octave points 20 Hz .. 20 kHz, dB from pink. Shapes of each source as it
// sits in a finished mix: where its energy lives and how it rolls off.
const float kCurve[InstrumentTargets::NumTargets][InstrumentTargets::kPoints] {
    { -30.0f, -22.0f, -10.0f, -3.0f,  0.0f,  1.0f,  1.0f,  0.0f, -2.0f,  -6.0f, -16.0f },   // Vocal
    { -32.0f, -24.0f, -14.0f, -6.0f, -1.0f,  1.0f,  1.0f,  0.0f, -1.0f,  -4.0f, -12.0f },   // Backing vocals
    { -36.0f, -26.0f, -12.0f, -4.0f,  0.0f,  1.0f,  1.5f,  0.0f, -3.0f,  -9.0f, -22.0f },   // Dialogue
    {  -6.0f,   4.0f,   6.0f,  0.0f, -6.0f, -9.0f, -9.0f, -6.0f, -7.0f, -10.0f, -18.0f },   // Kick
    { -24.0f, -14.0f,  -4.0f,  2.0f,  3.0f,  1.0f, -1.0f, -1.0f, -2.0f,  -5.0f, -14.0f },   // Snare
    { -10.0f,  -1.0f,   3.0f,  1.0f, -1.0f, -2.0f, -2.0f, -2.0f, -2.0f,  -4.0f, -12.0f },   // Drum bus
    { -24.0f, -16.0f,  -8.0f, -3.0f, -1.0f, -1.0f,  0.0f,  0.0f,  0.0f,  -2.0f,  -9.0f },   // Overheads
    {  -8.0f,   2.0f,   4.0f,  2.0f, -2.0f, -6.0f, -9.0f,-12.0f,-15.0f, -20.0f, -30.0f },   // Bass
    { -30.0f, -20.0f,  -6.0f,  0.0f,  2.0f,  2.0f,  1.0f,  0.0f, -4.0f, -12.0f, -26.0f },   // Electric guitar
    { -30.0f, -20.0f,  -6.0f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f, -1.0f,  -4.0f, -14.0f },   // Acoustic guitar
    { -20.0f,  -8.0f,  -1.0f,  1.0f,  1.0f,  0.0f, -1.0f, -2.0f, -4.0f,  -8.0f, -18.0f },   // Piano
    { -16.0f,  -6.0f,   0.0f,  1.0f,  0.0f,  0.0f,  0.0f, -1.0f, -2.0f,  -5.0f, -14.0f },   // Synth
    { -30.0f, -18.0f,  -5.0f,  0.0f,  1.0f,  1.0f,  0.0f, -1.0f, -3.0f,  -7.0f, -18.0f },   // Strings
    { -30.0f, -22.0f,  -8.0f, -1.0f,  1.0f,  2.0f,  2.0f,  1.0f, -2.0f,  -8.0f, -20.0f },   // Brass
    { -26.0f, -16.0f,  -8.0f, -3.0f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,  -2.0f, -10.0f },   // Percussion
    {  -6.0f,   1.0f,   3.0f,  1.0f,  0.0f, -1.0f, -2.0f, -3.0f, -5.0f,  -8.0f, -16.0f },   // Full mix
};
const char* kNames[InstrumentTargets::NumTargets] {
    "Vocal", "Backing Vocals", "Dialogue", "Kick", "Snare", "Drum Bus", "Overheads", "Bass",
    "Electric Guitar", "Acoustic Guitar", "Piano", "Synth", "Strings", "Brass", "Percussion", "Full Mix" };
const char* kCategory[InstrumentTargets::NumTargets] {
    "VOICE", "VOICE", "VOICE", "DRUMS", "DRUMS", "DRUMS", "DRUMS", "BASS",
    "MUSICAL", "MUSICAL", "MUSICAL", "MUSICAL", "MUSICAL", "MUSICAL", "DRUMS", "MIX" };
} // namespace

const char* InstrumentTargets::name (int t) { return kNames[std::min ((int) NumTargets - 1, std::max (0, t))]; }
const char* InstrumentTargets::category (int t) { return kCategory[std::min ((int) NumTargets - 1, std::max (0, t))]; }
const float* InstrumentTargets::points (int t) { return kCurve[std::min ((int) NumTargets - 1, std::max (0, t))]; }

float InstrumentTargets::curveDb (int target, float hz)
{
    const float* p = points (target);
    const float pos = std::log2 (std::max (20.0f, std::min (20480.0f, hz)) / 20.0f);
    const int i = std::min (kPoints - 2, (int) pos);
    const float f = pos - (float) i;
    return p[i] + (p[i + 1] - p[i]) * f;
}
} // namespace planish
