// Views.h — the panel pieces shared by the module panels: the module
// panel base (parameter binding, the standard header), the multiband
// crossover spectrum, the EQ curve editor, the detection filter view, the
// scrolling waveform with its gain traces, the tempo-synced oscilloscope,
// the spectral difference meter, the X/Y pad and the dynamics curve.
#pragma once

#include "../PluginProcessor.h"
#include "Analyzer.h"
#include "Widgets.h"

#include <array>
#include <map>

namespace planish::ui
{
// ---------------------------------------------------------------------------
// ModulePanel: one module's controls. The main panel shows one at a time
// and calls refresh() from its timer.
// ---------------------------------------------------------------------------
class ModulePanel : public juce::Component
{
public:
    ModulePanel (PlanishProcessor&, int moduleId, SpectrumSource& spectrum);
    ~ModulePanel() override;

    // Must be called before a panel is destroyed. The attachments live here in
    // the base class but the sliders they hold are members of the subclass,
    // which C++ destroys first; an attachment outliving its slider crashes
    // the host when it unhooks itself. PlanishPanel calls this on every panel.
    void releaseAttachments();

    int moduleId() const { return id; }
    virtual void refresh();          // 30 Hz while visible
    void paint (juce::Graphics&) override;

    // Binding helpers. `pid` is the full parameter id.
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    void bindSlider (juce::Slider&, const juce::String& pid, juce::Rectangle<int>, const juce::String& tip);
    void addKnob (Knob&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip, bool small = true);
    void addFader (Fader&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addReadout (Readout&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addToggle (PanelButton&, const juce::String& pid, juce::Rectangle<int>, const juce::String& tip);
    void addChoice (ChoiceDisplay&, const juce::String& pid, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addButton (PanelButton&, juce::Rectangle<int>, const juce::String& tip, std::function<void (const juce::ModifierKeys&)> click);
    void rebind (juce::Slider&, const juce::String& pid);   // swap a slider to another parameter
    void rebindChoice (ChoiceDisplay&, const juce::String& pid);
    juce::String pid (int index) const { return proc.paramId (proc.chain.module (id), index); }
    float pv (int index) const { return proc.getParam (pid (index)); }
    void setp (int index, float v) { proc.setParam (pid (index), v); }
    juce::String pidOf (const juce::String& shortId) const { return juce::String (proc.chain.module (id).info().id) + "." + shortId; }
    PlanishProcessor& processor() { return proc; }

    // Standard header pieces: DELTA, the channel mode, PRESETS and RESET.
    // Returns the header area left between them.
    juce::Rectangle<int> addStandardHeader (int chModeParam = -1);
    juce::Rectangle<int> headerArea() const { return { 14, 12, getWidth() - 28, 32 }; }

    // Text drawn at the foot of the panel (what a mode means, the readings).
    juce::String statusLine;

    // Learn crossovers: put them at dips in the spectrum of what is playing.
    void learnCrossovers (const MultibandParams& mb);

protected:
    PlanishProcessor& proc;
    int id;
    SpectrumSource& spectrum;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::vector<std::pair<juce::Slider*, juce::String>> sliders;
    std::vector<std::pair<PanelButton*, juce::String>> toggles;
    std::vector<std::pair<ChoiceDisplay*, juce::String>> choices;
    PanelButton deltaBtn { "DELTA" }, resetBtn { "RESET" }, presetBtn { "PRESETS" };
    ChoiceDisplay chModeChoice;
    void openModulePresets();
    void openChoiceMenu (const juce::String& pid, juce::Component* target);
    void drawSectionLocal (juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour tab = colours::ink) const;
};

// ---------------------------------------------------------------------------
// CrossoverView: band regions over the spectrum. Drag the handles, hover
// for + to add a band, x to remove, ON and S per band; click a band to
// select it. Double-click a handle to type its frequency.
// ---------------------------------------------------------------------------
class CrossoverView : public SpectrumView
{
public:
    CrossoverView (ModulePanel&, const MultibandParams& mb, std::atomic<int>& soloBand);
    int selectedBand = 0;
    std::function<void (int)> onSelect;
    // Optional per-band gain line (dB on the gain scale), or NaN for none.
    std::function<float (int band)> bandLineDb;
    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    int numBands() const;

protected:
    float xoverHz (int i) const;
    void setXover (int i, float hz);
    int handleAt (juce::Point<float>) const;
    int bandAt (float x) const;
    juce::Rectangle<float> powerRect (int band) const;
    juce::Rectangle<float> soloRect (int band) const;
    juce::Rectangle<float> removeRect (int band) const;
    juce::Rectangle<float> addRect() const;

    ModulePanel& panel;
    const MultibandParams& mb;
    std::atomic<int>& solo;
    int dragging = -1;
    juce::Point<float> hover { -1.0f, -1.0f };
    bool hovering = false;
};

// ---------------------------------------------------------------------------
// EqCurveView: the EQ spectrum with draggable nodes, through callbacks so it
// can edit this instance's Equalizer or another one's (the masking meter).
// ---------------------------------------------------------------------------
class EqCurveView final : public SpectrumView
{
public:
    struct Band
    {
        bool on = false;
        float hz = 1000.0f, gainDb = 0.0f, q = 1.0f;
        bool hasGain = true, hasQ = true, dynamic = false, up = false;
        float dynGainDb = 0.0f;
        juce::Colour colour;
    };
    explicit EqCurveView (ModulePanel&);
    int numBands = 12;
    std::function<Band (int)> getBand;
    std::function<void (int, float hz, float gainDb)> setBandFreqGain;
    std::function<void (int, float q)> setBandQ;
    std::function<void (int)> onSelect;
    std::function<void (int, float hz)> onAdd;      // enable band `i` at hz (i = first free)
    std::function<void (int)> onRemove;
    std::function<void (int)> onReset;             // double-click a node
    std::function<void (int, bool up)> onDirection;   // the dynamic arrows
    std::function<float (float hz)> compositeDb;
    std::function<float (float hz)> dynamicDb;     // the moving curve of the dynamic bands
    std::function<float (int, float hz)> bandDb;
    std::function<void (float hz, bool)> onAltSolo;
    // A second EQ's composite, drawn faintly (the masking meter's other track).
    std::function<float (float hz)> otherCompositeDb;
    // Masking overlay: brightness 0..1 per critical band, and histogram fill.
    const float* masking = nullptr;
    const float* histogram = nullptr;
    std::function<void()> onHistogramClick;
    int selected = -1;
    std::vector<int> multiSelected;

    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int nodeAt (juce::Point<float>) const;
    juce::Point<float> nodePos (const Band&) const;
    juce::Rectangle<float> arrowRect (const Band&, bool up) const;
    ModulePanel& panel;
    int dragging = -1, dragQ = 0;   // dragQ: -1 left handle, +1 right handle
    juce::Point<float> dragStart;
    float startQ = 1.0f;
    juce::Point<float> hover { -1.0f, -1.0f };
    bool hovering = false, altSoloing = false, boxSelecting = false;
    juce::Rectangle<float> box;
};

// ---------------------------------------------------------------------------
// DetectionFilterView: the detection filter's response with its two nodes
// (high-pass and low-pass: drag left/right for frequency, up/down for the
// resonance).
// ---------------------------------------------------------------------------
class DetectionFilterView final : public SpectrumView
{
public:
    DetectionFilterView (ModulePanel&, const DetectionParams&);
    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
private:
    juce::Point<float> node (int which) const;
    ModulePanel& panel;
    const DetectionParams& det;
    int dragging = -1;
};

// ---------------------------------------------------------------------------
// TraceView: the scrolling waveform (input dark behind the output light,
// newest on the right) with gain traces drawn over it.
// ---------------------------------------------------------------------------
class TraceView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kHistory = 360;   // 12 s at 30 Hz
    struct Lane { juce::Colour colour; std::array<float, kHistory> values {}; };
    float rangeDb = 24.0f;                  // gain trace scale (0 dB at the centre line)
    bool bipolar = false;                   // traces go up and down from the middle
    std::vector<Lane> lanes;
    int highlight = -1;
    // Optional threshold lines, dB on the waveform's level scale (-1000: none).
    std::array<float, 2> thresholdDb { -1000.0f, -1000.0f };
    std::function<void (int which, float db)> onDragThreshold;
    // Push one frame: the input and output peaks (linear) and each lane's value (dB).
    void push (float inPeak, float outPeak, const float* laneValues, float scPeak = 0.0f);
    bool showSidechain = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }
    void reset();
    float yForLevel (float db) const;
    float levelForY (float y) const;

private:
    std::array<float, kHistory> inWave {}, outWave {}, scWave {};
    int writePos = 0, dragging = -1;
};

// ---------------------------------------------------------------------------
// Oscilloscope: one bar (or a fraction of one) of a module's output, synced
// to the host tempo, with the sidechain in purple.
// ---------------------------------------------------------------------------
class Oscilloscope final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Type { Main = 0, Sidechain, MainAndSidechain, Combined };
    const ScopeRecorder* recorder = nullptr;
    int type = MainAndSidechain;
    double barFraction = 1.0;                 // 1, 1/2, 1/4, 1/8, 1/16
    std::function<double()> ppqNow;
    void paint (juce::Graphics&) override;
};

// ---------------------------------------------------------------------------
// DifferenceView: gain per band of the spectral modules over their input
// spectrum, with range handles and an optional target or sidechain curve.
// ---------------------------------------------------------------------------
class DifferenceView final : public SpectrumView
{
public:
    explicit DifferenceView (ModulePanel&);
    const float* gainDb = nullptr;       // 32 bands
    const float* specDb = nullptr;       // the module's own spectrum (pink-relative)
    const float* scDb = nullptr;         // the sidechain's (Unmask)
    juce::Colour colour = colours::lcd;
    int lowParam = -1, highParam = -1;   // range params (Hz)
    std::function<float (float hz)> targetDb;   // Sculptor's target, pink-relative
    void paintOverlay (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    ModulePanel& panel;
    int dragging = -1;
    float dragOffsetOct = 0.0f;
};

// ---------------------------------------------------------------------------
// XYPad: blends four corner profiles (the Exciter's harmonic shapes).
// ---------------------------------------------------------------------------
class XYPad final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<std::pair<float, float>()> get;
    std::function<void (float, float)> set;
    std::array<juce::String, 4> corners;   // top-left, top-right, bottom-left, bottom-right
    juce::Colour colour = colours::lcd;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (set) set (0.5f, 0.5f); }
};

// ---------------------------------------------------------------------------
// DynamicsCurveView: input vs output with the static curve of a band.
// ---------------------------------------------------------------------------
class DynamicsCurveView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<float (float inDb)> curve;
    float inputDb = -90.0f, gainDb = 0.0f;
    juce::Colour colour = colours::band (0);
    float rangeDb = 60.0f;
    void paint (juce::Graphics&) override;
};

// ---------------------------------------------------------------------------
// ThresholdMeter: the input level outside and the gain change inside.
// ---------------------------------------------------------------------------
class ThresholdMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    float inputDb = -90.0f, gainDb = 0.0f, floorDb = -60.0f;
    juce::Colour colour = colours::band (0);
    void paint (juce::Graphics&) override;
};
} // namespace planish::ui
