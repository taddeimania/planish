// Assistant.h — the track ASSISTANT: listens to the track, works out what
// it is (a vocal, a kick, a bass...), and builds a starting point toward
// the instrument target: an Equalizer shaped toward the target, a
// Compressor set from the track's levels, an Exciter, Density and the
// Sculptor aimed at the target (plus a Gate and the Transient Shaper for
// drums). The intent controls on the assistant page then steer the
// modules it built. A custom target can be learned from a reference file.
// Message thread.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Engine/Analysis.h"
#include "Engine/Chain.h"

#include <functional>

namespace planish
{
class Assistant
{
public:
    // The processor supplies these: set and read a parameter by id (plain
    // values), and replace the chain order.
    std::function<void (const juce::String&, float)> setParam;
    std::function<float (const juce::String&)> getParam;
    std::function<void (const std::vector<int>&)> setOrder;
    std::function<std::vector<int>()> getOrder;

    explicit Assistant (Chain& chain);

    // Targets: the instrument targets, then custom ones from reference files.
    int numTargets() const { return InstrumentTargets::NumTargets + (int) customs.size(); }
    juce::String targetName (int index) const;
    bool isCustom (int index) const { return index >= InstrumentTargets::NumTargets; }
    float targetDb (int index, float hz) const;
    void loadCustomTargets();
    bool addReferenceTarget (const juce::File& audioFile, juce::String& error);   // learns the loudest 8 s
    void removeCustomTarget (int index);
    static juce::File targetsFolder();

    // The run: listen to the chain's input for `seconds`.
    void start (float seconds);
    void cancel();
    bool analysing() const { return running; }
    float progress() const;
    void tick();                         // call from a timer; finishes the analysis

    bool hasResult() const { return lastResult.valid; }
    const AnalysisResult& result() const { return lastResult; }
    int detected() const { return detectedTarget; }
    int target() const { return currentTarget; }
    bool built() const { return hasBuilt; }

    // After a run: pick another target (rebuilds from the stored analysis).
    void setTarget (int index);
    // The settings have moved away from what was built ("dirty"), and back.
    bool dirty() const;
    void restore();
    juce::String summary() const { return summaryText; }

    // Which target a track sounds like (an InstrumentTargets id).
    static int detectInstrument (const AnalysisResult&);

private:
    void build();
    void apply (const juce::String& id, float v);

    Chain& chain;
    std::vector<CustomTarget> customs;
    bool running = false, hasBuilt = false;
    float seconds = 8.0f;
    AnalysisResult lastResult;
    int detectedTarget = InstrumentTargets::FullMix, currentTarget = InstrumentTargets::FullMix;
    std::vector<std::pair<juce::String, float>> builtValues;
    std::vector<int> builtOrder;
    juce::String summaryText;
};
} // namespace planish
