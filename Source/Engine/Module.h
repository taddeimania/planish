// Module.h — what every Planish processing module has in common: a list of
// parameter descriptions (the plugin builds host parameters from them), a
// plain float array holding the current values, and a stereo in-place
// process call. No JUCE, so the tests drive modules directly.
//
// Every module's first two parameters are ON (index 0) and MIX (index 1).
// The chain reads both: ON is the tile's power switch, MIX the tile's
// wet/dry slider, blended against the module's latency-aligned input.
#pragma once

#include "Dsp.h"

#include <string>
#include <vector>

namespace planish
{
enum class ParamKind { Float, Bool, Choice };

struct ParamDesc
{
    std::string id;        // host id, e.g. "eq.a.b2.freq"
    std::string name;      // host name, e.g. "EQ A Band 2 Freq"
    ParamKind kind = ParamKind::Float;
    float lo = 0.0f, hi = 1.0f, def = 0.0f;
    float skew = 1.0f;     // >1 gives more travel to small values (log-like); 0 = true log
    std::string unit;      // "dB", "Hz", "ms", "%", ":1"
    int decimals = 1;
    std::vector<std::string> choices;
    bool automatable = true;
};

// Channel processing modes. Each module says which it offers.
enum ChannelMode { Stereo = 0, MidSide = 1, TransSus = 2 };
enum ChannelModeBits { kStereoOnly = 1, kMidSideBit = 2, kTransSusBit = 4 };

// Which bits of a module to show, for the panel.
struct ModuleInfo
{
    std::string id;          // "eq"
    std::string name;        // "EQUALIZER"
    std::string shortName;   // "EQ"
    unsigned colour = 0xffffffff;
    int channelModes = kStereoOnly;   // ChannelModeBits
};

// Sidechain and host context the chain hands every module each block.
struct BlockContext
{
    const float* scL = nullptr;   // external sidechain, or null
    const float* scR = nullptr;
    bool scPresent = false;       // the host is feeding the sidechain bus
    double bpm = 120.0;
    double ppq = 0.0;             // quarter notes since the song start, for synced scopes
    bool playing = false;
    bool zeroLatency = false;     // global ZERO LATENCY switch
};

class Module
{
public:
    virtual ~Module() = default;

    const ModuleInfo& info() const noexcept { return meta; }
    const std::vector<ParamDesc>& params() const noexcept { return descs; }
    int numParams() const noexcept { return (int) descs.size(); }

    // The live values, in the order of params(). The plugin writes them
    // from its host parameters before each block; tests write them directly.
    float* values() noexcept { return vals.data(); }
    float  value (int i) const noexcept { return vals[(size_t) i]; }
    void   setValue (int i, float v) noexcept { vals[(size_t) i] = v; }
    int    indexOf (const std::string& id) const;
    // By short id ("a.b2.freq"), for tests and the assistant.
    float  get (const std::string& shortId) const { const int i = indexOf (pid (shortId)); return i >= 0 ? value (i) : 0.0f; }
    void   set (const std::string& shortId, float v) { const int i = indexOf (pid (shortId)); if (i >= 0) setValue (i, v); }

    // Resets every value to its default.
    void setDefaults();

    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* l, float* r, int n) = 0;
    virtual int  latency() const { return 0; }

    // The chain sets this before process().
    void setContext (const BlockContext& c) noexcept { ctx = c; }
    const BlockContext& context() const noexcept { return ctx; }

    // Only valid after prepare().
    double sampleRate() const noexcept { return sr; }

    // Declaring parameters (constructors only). Each returns the index in values().
    int addFloat (const std::string& id, const std::string& name, float lo, float hi, float def,
                  const std::string& unit = {}, int decimals = 1, float skew = 1.0f);
    int addDb (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "dB", 1); }
    int addHz (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "Hz", 0, 0.0f); }
    int addMs (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "ms", 1, 0.0f); }
    int addPct (const std::string& id, const std::string& name, float lo, float hi, float def) { return addFloat (id, name, lo, hi, def, "%", 0); }
    int addBool (const std::string& id, const std::string& name, bool def);
    int addChoice (const std::string& id, const std::string& name, std::vector<std::string> choices, int def);

    std::string pid (const std::string& s) const { return meta.id + "." + s; }
    std::string pname (const std::string& s) const { return meta.shortName + " " + s; }

    // The two parameters every module has.
    int pOn = -1, pMix = -1;
    bool isOn() const { return value (pOn) > 0.5f; }

protected:
    explicit Module (ModuleInfo m);

    double sr = 48000.0;
    int maxBlockSize = 512;
    BlockContext ctx;

private:
    ModuleInfo meta;
    std::vector<ParamDesc> descs;
    std::vector<float> vals;
};

// Converts between dB/octave slope and a Butterworth order.
inline int slopeToOrder (float dbPerOct) { return std::max (1, std::min (8, (int) std::lround (dbPerOct / 6.0f))); }

// Channel-mode choices for a module's info bits, and the ChannelMode of a
// choice index (the list skips modes a module lacks).
std::vector<std::string> channelModeChoices (int bits);
int channelModeFromChoice (int bits, int choice);
} // namespace planish
