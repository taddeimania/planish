// Params.h — the host parameters: every module's, from its descriptions,
// plus the global I/O panel.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Chain.h"

namespace planish::ids
{
inline constexpr const char* inGain    = "io.in";
inline constexpr const char* outGain   = "io.out";
inline constexpr const char* bypass    = "io.bypass";
inline constexpr const char* mono      = "io.mono";
inline constexpr const char* pan       = "io.pan";
inline constexpr const char* invert    = "io.invert";
inline constexpr const char* width     = "io.width";
inline constexpr const char* swap      = "io.swap";
inline constexpr const char* limiter   = "io.limiter";
inline constexpr const char* ceiling   = "io.ceiling";
inline constexpr const char* limStyle  = "io.limstyle";
inline constexpr const char* limMode   = "io.limmode";
inline constexpr const char* zeroLatency = "io.zerolatency";
inline constexpr const char* gainMatch = "io.gainmatch";
inline constexpr const char* meterType = "io.metertype";

// Not part of a preset: listening controls, and where the track sits in
// the mix (the Visual Mixer's business).
inline bool notInPreset (const juce::String& id)
{
    return id == bypass || id == mono || id == swap || id == outGain || id == pan;
}

inline juce::String decibels (float v, int places = 1)
{
    const float k = std::pow (10.0f, (float) places);
    const float r = std::round (v * k) / k;
    return (r > 0.0f ? "+" : "") + juce::String (r, places) + " dB";
}
inline juce::String hertz (float v)
{
    if (v >= 10000.0f) return juce::String (v / 1000.0f, 1) + " kHz";
    if (v >= 1000.0f)  return juce::String (v / 1000.0f, 2) + " kHz";
    return juce::String (juce::roundToInt (v)) + " Hz";
}
inline juce::String millis (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " s";
    return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
}
inline juce::String ratio (float v)
{
    if (v < 0.0f) return "(" + juce::String (-v, 1) + "):1";
    return juce::String (v, 1) + ":1";
}
} // namespace planish::ids

namespace planish
{
// Builds a host parameter from a module description.
inline std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamDesc& d)
{
    using namespace juce;
    const ParameterID pid { d.id, 1 };
    if (d.kind == ParamKind::Bool)
        return std::make_unique<AudioParameterBool> (pid, d.name, d.def > 0.5f);
    if (d.kind == ParamKind::Choice)
    {
        StringArray choices;
        for (const auto& c : d.choices) choices.add (c);
        return std::make_unique<AudioParameterChoice> (pid, d.name, choices, (int) d.def);
    }
    NormalisableRange<float> range (d.lo, d.hi, 0.0f);
    if (d.skew == 0.0f && d.lo > 0.0f)
        range.setSkewForCentre (std::sqrt (d.lo * d.hi));   // log-ish
    else if (d.skew == 0.0f)
        range.skew = 0.4f;                                   // log-ish from zero
    else if (d.skew != 1.0f)
        range.skew = d.skew;
    const std::string unit = d.unit;
    const int decimals = d.decimals;
    auto text = [unit, decimals] (float v, int)
    {
        if (unit == "dB")  return ids::decibels (v, decimals);
        if (unit == "Hz")  return ids::hertz (v);
        if (unit == "ms")  return ids::millis (v);
        if (unit == ":1")  return ids::ratio (v);
        if (unit == "%")   return String (roundToInt (v)) + "%";
        if (unit == "LUFS") return String (v, 1) + " LUFS";
        if (unit == "dB/oct") return (v > 0 ? "+" : "") + String (v, 1) + " dB/oct";
        if (unit == "deg") return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + String (CharPointer_UTF8 ("\xc2\xb0"));
        return String (v, decimals);
    };
    return std::make_unique<AudioParameterFloat> (pid, d.name, range, d.def,
        AudioParameterFloatAttributes().withStringFromValueFunction (text));
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const Chain& chain)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto db = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return ids::decibels (v); }));
    };
    auto pct = [] (const char* id, const char* name, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.0f), def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.5f ? "+" : "") + String (roundToInt (v)) + "%"; }));
    };
    auto toggle = [] (const char* id, const char* name, bool def)
    {
        return std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def);
    };
    auto choice = [] (const char* id, const char* name, StringArray c, int def)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def);
    };
    layout.add (db (ids::inGain, "Input Gain", -24.0f, 24.0f, 0.0f));
    layout.add (db (ids::outGain, "Output Gain", -36.0f, 24.0f, 0.0f));
    layout.add (toggle (ids::bypass, "Bypass", false));
    layout.add (toggle (ids::mono, "Sum To Mono", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::pan, 1 }, "Pan", NormalisableRange<float> (-100.0f, 100.0f, 0.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const int p = roundToInt (v);
            return p == 0 ? String ("C") : p < 0 ? String (-p) + "L" : String (p) + "R";
        })));
    layout.add (toggle (ids::invert, "Invert Phase", false));
    layout.add (pct (ids::width, "Width", -100.0f, 100.0f, 0.0f));
    layout.add (toggle (ids::swap, "Swap Channels", false));
    layout.add (toggle (ids::limiter, "Limiter", false));
    layout.add (db (ids::ceiling, "Limiter Ceiling", -20.0f, 0.0f, -0.3f));
    layout.add (choice (ids::limStyle, "Limiter Style", { "Clear", "Smooth", "Thick" }, 1));
    layout.add (choice (ids::limMode, "Limiter Mode", { "IRC II", "IRC LL", "Hard", "Zero-L" }, 0));
    layout.add (toggle (ids::zeroLatency, "Zero Latency", false));
    layout.add (toggle (ids::gainMatch, "Auto Gain", false));
    layout.add (choice (ids::meterType, "Meter Type", { "RMS + Peak", "Short Term + Peak" }, 0));

    for (int m = 0; m < NumModules; ++m)
        for (const auto& d : chain.module (m).params())
            layout.add (makeParameter (d));
    return layout;
}
} // namespace planish
