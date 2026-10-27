#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

#include "Assistant.h"
#include "Engine/Chain.h"
#include "Engine/Presets.h"
#include "Link.h"
#include "Params.h"

class PlanishProcessor final : public juce::AudioProcessor,
                               private juce::Timer,
                               private juce::AudioProcessorValueTreeState::Listener
{
public:
    PlanishProcessor();
    ~PlanishProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, factoryIndex); }
    void setCurrentProgram (int index) override { loadFactory (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- parameters (message thread) ---------------------------------------------
    void setParam (const juce::String& id, float plainValue);
    float getParam (const juce::String& id) const;
    int   geti (const juce::String& id) const { return juce::roundToInt (getParam (id)); }
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    juce::String paramId (const planish::Module& m, int index) const { return m.params()[(size_t) index].id; }

    // --- the chain ----------------------------------------------------------------------
    planish::Chain chain;
    std::vector<int> getOrder() const { return chain.getOrder(); }
    void setOrder (const std::vector<int>& order);
    void addModule (int id, int position = -1);
    void removeModule (int id);
    void moveModule (int id, int newPosition);
    void resetModule (int id);                 // every parameter to its default

    // Listening.
    std::atomic<int> soloModule { -1 }, deltaModule { -1 };

    // --- presets --------------------------------------------------------------------------
    void loadFactory (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name, juce::String& error);
    void stepPreset (int delta);
    juce::String presetName() const { return currentName; }
    int  presetNumber() const;
    bool presetEdited() const;
    void loadModulePreset (int module, const planish::ModulePreset&);
    bool loadUserModulePreset (int module, const juce::File&);
    bool saveUserModulePreset (int module, const juce::String& name, juce::String& error);
    static juce::File userPresetFolder();
    static juce::File userModulePresetFolder (int module);
    static juce::Array<juce::File> userPresets();
    static juce::Array<juce::File> userModulePresets (int module);

    // --- undo history and sets A..D ----------------------------------------------------
    struct HistoryItem { juce::String label; std::unique_ptr<juce::XmlElement> state; };
    void undo();
    bool canUndo() const { return ! history.empty(); }
    const std::vector<HistoryItem>& historyItems() const { return history; }
    void revertTo (int historyIndex);
    void clearHistory() { history.clear(); }
    void setSnapshot (int slot);          // A..D = 0..3
    void recallSnapshot (int slot);
    bool hasSnapshot (int slot) const { return snapshots[(size_t) slot] != nullptr; }
    int  activeSnapshot() const { return activeSnap; }

    // --- the assistant ------------------------------------------------------------------
    planish::Assistant assistant;
    void startAssistant (float seconds);
    double hostBpm() const { return bpm.load(); }
    bool hostPlaying() const { return playing.load(); }

    // --- linking with the other instances ------------------------------------------------
    int instanceId() const { return linkId; }
    juce::String instanceName() const { return name; }
    void setInstanceName (const juce::String& n) { name = n.substring (0, 40); nameEdited = true; }
    // The host's track name and colour, when it tells us (Live does).
    void updateTrackProperties (const TrackProperties&) override;
    juce::Colour trackColour() const { return trackCol; }
    bool isStereo() const { return getTotalNumOutputChannels() > 1; }
    // Critical-band spectrum of the output (dB), refreshed at 20 Hz.
    const float* maskingSpectrum() const { return maskBands; }
    float maskingGainOffsetDb = 0.0f;     // OPTIONS: match the track fader
    // Output activity for the Visual Mixer: peak since the last read.
    float takeActivity() { return activity.exchange (0.0f); }
    int mixGroup = -1;                    // the mix assistant's class (-1: from the assistant)
    bool mixerHidden = false;             // hidden in the Visual Mixer
    // The output loudness over a listening pass (the mix assistant).
    void startMixListening() { meterResets.fetch_add (1); }
    float mixLoudness() { return chain.outputLoudness().hasIntegrated() ? chain.outputLoudness().integrated() : -70.0f; }

    // Meter resets (message thread).
    void resetMeters() { meterResets.fetch_add (1); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override;
    void cacheParams();
    void readSettings();
    void pushUndo (const juce::String& label);
    std::unique_ptr<juce::XmlElement> stateXml();
    void applyStateXml (const juce::XmlElement&, bool includeOrder);
    std::vector<float> snapshot() const;
    void syncOrderFromState();
    void updateMaskingSpectrum();

    // Module parameter pointers, in each module's own order.
    std::vector<std::vector<std::atomic<float>*>> moduleParams;
    struct
    {
        std::atomic<float>* in = nullptr; std::atomic<float>* out = nullptr; std::atomic<float>* bypass = nullptr; std::atomic<float>* mono = nullptr;
        std::atomic<float>* pan = nullptr; std::atomic<float>* invert = nullptr; std::atomic<float>* width = nullptr; std::atomic<float>* swap = nullptr;
        std::atomic<float>* limiter = nullptr; std::atomic<float>* ceiling = nullptr; std::atomic<float>* limStyle = nullptr; std::atomic<float>* limMode = nullptr;
        std::atomic<float>* zeroLatency = nullptr; std::atomic<float>* gainMatch = nullptr; std::atomic<float>* meterType = nullptr;
    } fast;
    planish::ChainSettings settings;
    std::atomic<bool> analysing { false };
    std::atomic<int> meterResets { 0 };
    int seenMeterResets = 0;
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> playing { false };
    std::atomic<float> activity { 0.0f };
    juce::AudioBuffer<float> monoScratch;
    int lastLatency = -1, maxBlock = 512;
    bool wasPlaying = false;

    // Linking.
    int linkId = 0;
    juce::String name;
    bool nameEdited = false;
    juce::Colour trackCol { 0 };
    std::shared_ptr<int> aliveToken = std::make_shared<int> (0);
    float maskBands[planish::Masking::kBands] {};
    planish::Fft maskFft;
    std::vector<float> maskL, maskR, maskWin;
    std::vector<planish::Fft::cpx> maskBins;

    // Presets.
    int factoryIndex = 0;
    juce::File userFile;
    juce::String currentName;
    std::vector<float> loadedValues;
    std::vector<int> loadedOrder;

    // Undo.
    std::vector<HistoryItem> history;
    std::unique_ptr<juce::XmlElement> snapshots[4];
    int activeSnap = -1;
    std::atomic<bool> dirty { false };
    juce::SpinLock labelLock;
    juce::String pendingLabel;
    std::atomic<juce::RangedAudioParameter*> lastChanged { nullptr };
    bool applying = false;
    double lastPushMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlanishProcessor)
};
