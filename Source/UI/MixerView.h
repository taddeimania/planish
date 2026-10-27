// MixerView.h — the VISUAL MIXER: every Planish in the session as a node
// on a pad (left-right is pan, up-down is output gain, the handles are
// width), a list to show, hide and rename them, three mix snapshots, and
// activity metering. Its MIX ASSISTANT listens to the whole song through
// every Planish and sets a starting balance by group: the focus, voices,
// bass, percussion and the rest.
#pragma once

#include "../PluginProcessor.h"
#include "Widgets.h"

namespace planish::ui
{
class MixerView final : public juce::Component
{
public:
    explicit MixerView (PlanishProcessor&);
    ~MixerView() override;
    std::function<void()> onClose;
    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // The mix assistant's groups.
    enum Group { Focus = 0, Voice, Bass, Percussion, Musical, NumGroups };
    static const char* groupName (int g);

private:
    enum Stage { Off = 0, Setup, Waiting, Listening, Results };
    struct Track
    {
        int id = 0;
        bool include = true, focus = false;
        int group = Musical;
        float originalGain = 0.0f, loudness = -70.0f, newGain = 0.0f;
    };

    juce::Rectangle<float> padArea() const { return { 14.0f, 40.0f, 760.0f, 690.0f }; }
    juce::Rectangle<float> listArea() const { return { 790.0f, 40.0f, 300.0f, 520.0f }; }
    juce::Point<float> nodePos (PlanishProcessor&) const;
    float widthHandle (PlanishProcessor&) const;   // half-length of the width bar, px
    PlanishProcessor* nodeAt (juce::Point<float>, int& part) const;   // part 0 node, -1/1 width handles
    static float gainForY (float y, juce::Rectangle<float> pad);
    static float yForGain (float g, juce::Rectangle<float> pad);
    static int defaultGroup (PlanishProcessor&);
    juce::Colour nodeColour (PlanishProcessor&) const;

    void setStage (Stage);
    void buildTracks();
    void computeResults();
    void applyResults (bool bypassed);
    void restoreOriginals();
    void storeSnapshot (int slot);
    void recallSnapshot (int slot);

    PlanishProcessor& proc;
    PanelLookAndFeel lnf;
    PanelButton assistBtn { "MIX ASSISTANT" }, closeBtn { "BACK" }, setBtn { "SET" };
    PanelButton snapBtn[3] { PanelButton ("A"), PanelButton ("B"), PanelButton ("C") };
    PanelButton beginBtn { "BEGIN LISTENING" }, resultsBtn { "GO TO RESULTS" }, bypassBtn { "BYPASS ASSISTANT" }, acceptBtn { "ACCEPT" }, cancelBtn { "CANCEL" };
    Fader groupFader[NumGroups] { Fader (true), Fader (true), Fader (true), Fader (true), Fader (true) };
    std::map<int, float> activity;
    int dragId = -1, dragPart = 0, armedSnapshot = -1;
    float listScroll = 0.0f;
    Stage stage = Off;
    std::vector<Track> tracks;
    bool bypassed = false;
};
} // namespace planish::ui
