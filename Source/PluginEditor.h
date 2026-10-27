// PluginEditor.h — the Planish panel: the header (track name, presets,
// history, sets A..D, ASSIST, MIXER, OPTIONS), the signal chain strip, the
// selected module's panel, and the I/O column with the meters, the output
// stage and the limiter. Drawn in a fixed design space scaled to the window.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Settings.h"
#include "UI/Analyzer.h"
#include "UI/AssistantView.h"
#include "UI/MixerView.h"
#include "UI/ModulePanels.h"
#include "UI/Widgets.h"

// ---------------------------------------------------------------------------
// The signal chain strip: one tile per module in use, drag to reorder, + to
// add. Each tile has power, solo, remove, a difference meter and its MIX.
// ---------------------------------------------------------------------------
class ChainStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ChainStrip (PlanishProcessor&);
    std::function<void (int)> onSelect;
    int selected = -1;
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

private:
    juce::Rectangle<float> tileRect (int index) const;
    juce::Rectangle<float> addRect() const;
    static juce::Rectangle<float> soloRect (juce::Rectangle<float> tile);
    static juce::Rectangle<float> removeRect (juce::Rectangle<float> tile);
    static juce::Rectangle<float> powerRect (juce::Rectangle<float> tile);
    static juce::Rectangle<float> mixRect (juce::Rectangle<float> tile);
    int tileAt (juce::Point<float>) const;
    void openAddMenu();
    void setMixFromX (int id, float x, juce::Rectangle<float> bar);
    PlanishProcessor& proc;
    std::vector<int> order;
    int dragging = -1, hover = -1, mixDragging = -1;
    float dragX = 0.0f;
    bool dragMoved = false;
};

// ---------------------------------------------------------------------------
// The I/O column: meters, input and output gain, width, pan, the output
// limiter, and the listening switches.
// ---------------------------------------------------------------------------
class IOPanel final : public juce::Component
{
public:
    explicit IOPanel (PlanishProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    using Knob = planish::ui::Knob;
    using PanelButton = planish::ui::PanelButton;
    using ChoiceDisplay = planish::ui::ChoiceDisplay;
    using Meter = planish::ui::Meter;
    void addKnob (Knob&, const char* id, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    void addToggle (PanelButton&, const char* id, juce::Rectangle<int>, const juce::String& tip);
    void addChoice (ChoiceDisplay&, const char* id, const juce::String& caption, juce::Rectangle<int>, const juce::String& tip);
    PlanishProcessor& proc;
    Meter inL, inR, outL, outR;
    Knob inKnob, outKnob, widthKnob, panKnob, ceilingKnob;
    PanelButton bypassBtn { "BYPASS" }, matchBtn { "AUTO GAIN" }, monoBtn { "MONO" }, swapBtn { "SWAP" }, invertBtn { "INVERT" },
                limiterBtn { "LIMITER" }, zeroBtn { "ZERO LATENCY" }, meterBtn { "RMS" };
    ChoiceDisplay styleChoice, modeChoice;
    planish::ui::BarMeter grMeter;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::vector<std::pair<PanelButton*, juce::String>> toggles;
    std::vector<ChoiceDisplay*> choices;
    planish::MeterReadings inRead, outRead;
    float inPeakHold = 0.0f, outPeakHold = 0.0f;
    juce::String shownText;
};

// ---------------------------------------------------------------------------
// The whole panel, in a fixed design space scaled to the window.
// ---------------------------------------------------------------------------
class PlanishPanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int designW = 1400;
    static constexpr int designH = 900;

    explicit PlanishPanel (PlanishProcessor&);
    ~PlanishPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override {}
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (bool)> onTooltips;
    void syncTooltips();

private:
    using PanelButton = planish::ui::PanelButton;
    using ChoiceDisplay = planish::ui::ChoiceDisplay;
    enum View { Modules = 0, AssistantPage, MixerPage };
    void timerCallback() override;
    void addButton (PanelButton&, juce::Rectangle<int>, const juce::String& tip, std::function<void (const juce::ModifierKeys&)>);
    void selectModule (int id);
    void showView (View);
    void openPresetMenu();
    void openHistoryMenu();
    void savePresetDialog();
    void openOptionsMenu();
    void applySettings();

    PlanishProcessor& proc;
    planish::ui::PanelLookAndFeel lnf;
    planish::Settings settings;
    planish::ui::SpectrumSource outputSpectrum, inputSpectrum;

    juce::Label nameLabel;
    ChoiceDisplay presetChoice;
    PanelButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, historyBtn { "HISTORY" }, undoBtn { "UNDO" },
                assistBtn { "ASSIST" }, mixerBtn { "MIXER" }, optionsBtn { "OPTIONS" }, tipsBtn { "TIPS" };
    PanelButton snapBtn[4] { PanelButton ("A"), PanelButton ("B"), PanelButton ("C"), PanelButton ("D") };

    ChainStrip chain;
    IOPanel io;
    std::array<std::unique_ptr<planish::ui::ModulePanel>, planish::NumModules> panels;
    planish::ui::AssistantView assistant;
    planish::ui::MixerView mixer;
    int selectedModule = -1;
    View view = Modules;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    int tooltipsApplied = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlanishPanel)
};

class PlanishEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PlanishEditor (PlanishProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PlanishPanel panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlanishEditor)
};
