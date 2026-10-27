// ModulePanels.h — one panel per module. All are 1104 x 680 in the panel's
// design space and are created once by the main panel, which shows the
// one for the selected module.
#pragma once

#include "Views.h"

namespace planish::ui
{
std::unique_ptr<ModulePanel> makeModulePanel (int moduleId, PlanishProcessor&, SpectrumSource& output, SpectrumSource& input);

// Shared layout constants.
inline constexpr int kPanelW = 1104, kPanelH = 680;
inline const juce::Rectangle<int> kViewArea { 14, 52, 1076, 352 };
inline const juce::Rectangle<int> kControlArea { 14, 420, 1076, 232 };

// The A/B set selector of the modules with channel modes: two buttons named
// for the mode (MID / SIDE, TRANS / SUS), each with a solo.
class SetSelector final : public juce::Component
{
public:
    SetSelector();
    std::function<void (int)> onSelect;
    std::function<void (int)> onSolo;      // -1: none
    int selected = 0, soloed = -1;
    void update (int mode);                // hides itself in STEREO
    void resized() override;
private:
    PanelButton set[2] { PanelButton ("A"), PanelButton ("B") }, solo[2] { PanelButton ("S"), PanelButton ("S") };
    int lastMode = -1;
};

// Band selector buttons 1..n.
class BandButtons final : public juce::Component
{
public:
    explicit BandButtons (int count, bool nodeColours = false);
    std::function<void (int)> onSelect;
    void update (int selected, int numShown, std::function<bool (int)> dimmed = {});
    void resized() override;
    int columns = 12;
private:
    std::vector<std::unique_ptr<PanelButton>> buttons;
};

// --- Equalizer ----------------------------------------------------------------
class EqPanel final : public ModulePanel
{
public:
    EqPanel (PlanishProcessor&, SpectrumSource& output, SpectrumSource& input);
    ~EqPanel() override;
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    void bindBand();
    void learnNodes();
    // The Equalizer being edited: this instance's, or (masking meter, OTHER)
    // the selected instance's.
    PlanishProcessor& target();
    bool editingOther() const;
    float tpv (const juce::String& shortId);
    void tsetp (const juce::String& shortId, float v);
    juce::String bandKey (int b, const char* key) const;
    void openMaskingSourceMenu();
    void updateMasking();

    Equalizer& eq;
    SpectrumSource& input;
    EqCurveView curve;
    SetSelector sets;
    BandButtons bandBtns { Equalizer::kBands, true };
    ChoiceDisplay scaleChoice, shapeChoice, slopeChoice, directionChoice, sidechainChoice, maskSourceChoice;
    PanelButton softSatBtn { "SOFT SAT" }, maskingBtn { "MASKING" }, learnBtn { "LEARN" };
    PanelButton bandOnBtn { "ON" }, bandSoloBtn { "SOLO" }, bandRemoveBtn { "X" }, dynBtn { "DYNAMIC" }, upBtn { "UP" }, downBtn { "DOWN" };
    PanelButton editSelfBtn { "THIS EQ" }, editOtherBtn { "OTHER EQ" }, inverseBtn { "INVERSE LINK" }, bypassEqsBtn { "BYPASS EQs" };
    Readout freqRo, gainRo, qRo, attackRo, releaseRo;
    Fader thresholdFader;
    ThresholdMeter thresholdMeter;
    Knob sensitivityKnob, offsetKnob;
    int viewSet = 0, selected = 1;
    bool learning = false;
    double learnStarted = 0.0;
    std::vector<float> learnAcc;
    // Masking.
    bool maskingOn = false, editOther = false, inverseLink = false, eqsBypassed = false;
    int maskSource = -1;
    float maskSensitivity = 0.5f;
    float maskNow[Masking::kBands] {}, histogram[Masking::kBands] {};
    double lastMaskMs = 0.0;
    int linkedRemoteBand = -1;
};

// --- Gate --------------------------------------------------------------------------
class GatePanel final : public ModulePanel
{
public:
    GatePanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    Gate& gate;
    TraceView trace;
    CrossoverView xover;
    DetectionFilterView detView;
    PanelButton viewBtn[3] { PanelButton ("WAVEFORM"), PanelButton ("CROSSOVER"), PanelButton ("DETECTION") };
    PanelButton learnBtn { "LEARN" }, detOnBtn { "FILTER ON" }, detSoloBtn { "SOLO" };
    BandButtons bandBtns { 3 };
    Fader openFader, closeFader;
    ThresholdMeter meter;
    Readout ratioRo, attackRo, holdRo, releaseRo;
    BarMeter grMeter;
    ChoiceDisplay scChoice;
    int view = 0, selected = 0;
};

// --- Compressor 1 and 2 -------------------------------------------------------------
class CompressorPanel final : public ModulePanel
{
public:
    CompressorPanel (PlanishProcessor&, int moduleId, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void selectBand (int b);
    void bindBand();
    Compressor& comp;
    TraceView trace;
    CrossoverView xover;
    DetectionFilterView detView;
    Oscilloscope scope;
    PanelButton viewBtn[4] { PanelButton ("WAVEFORM"), PanelButton ("CROSSOVER"), PanelButton ("DETECTION"), PanelButton ("SCOPE") };
    PanelButton learnBtn { "LEARN" }, autoGainBtn { "AUTO GAIN" }, detOnBtn { "FILTER ON" }, detSoloBtn { "SOLO" }, autoRelBtn { "AUTO" };
    ChoiceDisplay modeChoice, detectChoice, scChoice;
    BandButtons bandBtns { 3 };
    Fader thresholdFader, mixFader, makeupFader;
    ThresholdMeter meter;
    Readout ratioRo, kneeRo, attackRo, releaseRo, punchAttackRo, punchSustainRo;
    Knob amountKnob;
    DynamicsCurveView curveView;
    BarMeter grMeter;
    int view = 0, selected = 0, lastScChoice[3] { 0, 0, 0 };
};

// --- Transient Shaper ---------------------------------------------------------------
class TransientPanel final : public ModulePanel
{
public:
    TransientPanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindSet();
    TransientShaper& ts;
    TraceView trace;
    CrossoverView xover;
    SetSelector sets;
    PanelButton viewBtn[2] { PanelButton ("WAVEFORM"), PanelButton ("CROSSOVER") };
    PanelButton learnBtn { "LEARN" };
    ChoiceDisplay envChoice;
    Knob attackKnob[3], sustainKnob[3];
    PanelButton contourBtn[3][3];
    BarMeter gainMeter[3];
    int view = 0, viewSet = 0;
};

// --- Exciter ----------------------------------------------------------------------------
class ExciterPanel final : public ModulePanel
{
public:
    ExciterPanel (PlanishProcessor&, SpectrumSource& output, SpectrumSource& input);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    // The crossover view with the post filter's node on top.
    class PostView final : public CrossoverView
    {
    public:
        PostView (ExciterPanel& p, const MultibandParams& m, std::atomic<int>& s) : CrossoverView (p, m, s), owner (p) {}
        void paintOverlay (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        juce::Point<float> postNode() const;
        ExciterPanel& owner;
        bool draggingPost = false;
    };
    Exciter& exc;
    PostView view;
    PanelButton tameBtn { "TAME" }, osBtn { "4X OVERSAMPLE" }, learnBtn { "LEARN" };
    ChoiceDisplay emphasisChoice;
    Knob driveKnob[3], toneKnob;
    PanelButton classicBtn[3], trashBtn[3];
    XYPad pad[3];
    Fader mixFader[3];
    friend class PostView;
};

// --- Sculptor ------------------------------------------------------------------------------
class SculptorPanel final : public ModulePanel
{
public:
    SculptorPanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
private:
    void bindSet();
    Sculptor& scu;
    DifferenceView diff;
    SetSelector sets;
    ChoiceDisplay targetChoice;
    Knob amountKnob, toneKnob, speedKnob;
    PanelButton soloBtn { "SOLO RANGE" };
    int viewSet = 0;
};

// --- Unmask ---------------------------------------------------------------------------------
class UnmaskPanel final : public ModulePanel
{
public:
    UnmaskPanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindSet();
    Unmask& unm;
    DifferenceView diff;
    SetSelector sets;
    Knob amountKnob, sensKnob, attackKnob, releaseKnob;
    PanelButton soloBtn { "SOLO RANGE" };
    int viewSet = 0;
};

// --- Clipper ---------------------------------------------------------------------------------
class ClipperPanel final : public ModulePanel
{
public:
    ClipperPanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindSet();
    Clipper& clp;
    TraceView trace;
    CrossoverView xover;
    SetSelector sets;
    PanelButton viewBtn[2] { PanelButton ("WAVEFORM"), PanelButton ("CROSSOVER") };
    PanelButton osBtn { "4X OVERSAMPLE" }, learnBtn { "LEARN" };
    Knob boostKnob[3];
    Fader thresholdFader[3];
    DynamicsCurveView curve[3];
    int view = 0, viewSet = 0;
};

// --- Density ---------------------------------------------------------------------------------
class DensityPanel final : public ModulePanel
{
public:
    DensityPanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    void bindSet();
    Density& den;
    TraceView trace;
    CrossoverView xover;
    SetSelector sets;
    PanelButton viewBtn[2] { PanelButton ("WAVEFORM"), PanelButton ("CROSSOVER") };
    PanelButton learnBtn { "LEARN" };
    Fader thresholdFader[3];
    ThresholdMeter meter[3];
    Knob rangeKnob[3], ratioKnob[3], speedKnob[3];
    int view = 0, viewSet = 0;
};

// --- Phase ------------------------------------------------------------------------------------
class PhasePanel final : public ModulePanel
{
public:
    PhasePanel (PlanishProcessor&, SpectrumSource&);
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    Phase& phs;
    Oscilloscope scope;
    ChoiceDisplay typeChoice, timeChoice;
    PanelButton learnBtn { "LEARN" }, invertBtn { "INVERT" };
    Knob rotationKnob, delayKnob;
};
} // namespace planish::ui
