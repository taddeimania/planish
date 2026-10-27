// AssistantView.h — the ASSISTANT pages: GO (listen to the track), the
// listening progress, then the assistant view: intent controls for tone,
// dynamics, saturation and width that drive the modules it built, the
// target library (instrument targets and your reference files), and the
// tonal balance meter of the output against the target.
#pragma once

#include "../PluginProcessor.h"
#include "Analyzer.h"
#include "Views.h"
#include "Widgets.h"

namespace planish::ui
{
class AssistantView final : public juce::Component
{
public:
    AssistantView (PlanishProcessor&, SpectrumSource& outputSpectrum);
    ~AssistantView() override;

    enum Page { Start = 0, Listening, Result };
    Page page() const { return currentPage; }
    void showStart();
    void showResult();
    std::function<void()> onClose;   // back to the detailed view

    void refresh();   // 30 Hz while visible
    void paint (juce::Graphics&) override;

private:
    class TargetList final : public juce::Component
    {
    public:
        explicit TargetList (AssistantView& v) : view (v) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent& e) override { hoverRow = rowAt (e.position.y); repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override;
    private:
        int rowAt (float y) const;
        AssistantView& view;
        int hoverRow = -1;
        float scroll = 0.0f;
    };
    class BalanceMeter final : public SpectrumView
    {
    public:
        explicit BalanceMeter (AssistantView& v) : view (v) { showGainScale = false; showPeaks = false; fill = false; }
        void paintOverlay (juce::Graphics&) override;
    private:
        AssistantView& view;
    };

    void setPage (Page);
    void startRun();
    void addReference();
    void setAllBands (const juce::String& key, float v, bool skipZeroDrive = false);
    void bind (juce::Slider&, const char* id);

    PlanishProcessor& proc;
    SpectrumSource& spectrum;
    PanelLookAndFeel lnf;
    Page currentPage = Start;

    // Start / listening.
    PanelButton goBtn { "GO" }, cancelBtn { "CANCEL" }, backBtn { "DETAILED VIEW" };
    Knob secondsKnob;
    // Result.
    PanelButton relearnBtn { "RELEARN" }, detailBtn { "DETAILED VIEW" }, restoreBtn { "RESTORE" }, addRefBtn { "+ REFERENCE" }, removeRefBtn { "REMOVE" };
    PanelButton classicBtn { "CLASSIC" }, trashBtn { "TRASH" };
    Fader eqFader { false }, sculptorFader { false }, densityFader { false }, compFader { false }, widthFader { false };
    Knob driveKnob, toneKnob;
    XYPad pad;
    TargetList targets { *this };
    BalanceMeter meter { *this };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace planish::ui
