#include "AssistantView.h"
#include "../Settings.h"

namespace planish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

namespace
{
const Rect kIntentBox  { 14, 24, 420, 690 };
const Rect kLibraryBox { 450, 24, 270, 690 };
const Rect kMeterBox   { 736, 24, 354, 690 };
constexpr int kRowH = 26;
}

AssistantView::AssistantView (PlanishProcessor& p, SpectrumSource& s) : proc (p), spectrum (s)
{
    setLookAndFeel (&lnf);
    setSize (1104, 796);

    // --- start ------------------------------------------------------------------------------
    goBtn.setFontHeight (40.0f);
    goBtn.setTooltip ("Play the track, then press GO: Planish listens, works out what the track is, and builds a starting point toward its target.");
    goBtn.onClick = [this] (const ModifierKeys&) { startRun(); };
    addAndMakeVisible (goBtn);
    goBtn.setBounds (432, 300, 240, 140);
    secondsKnob.caption = "LISTEN";
    secondsKnob.setRange (3.0, 30.0, 1.0);
    secondsKnob.setValue (Settings().assistantSeconds(), dontSendNotification);
    secondsKnob.format = [] (double v) { return String ((int) v) + " s"; };
    secondsKnob.onValueChange = [this] { Settings().setAssistantSeconds ((float) secondsKnob.getValue()); };
    secondsKnob.setTooltip ("How long it listens. Play a part that represents the track: a full verse or chorus.");
    addAndMakeVisible (secondsKnob);
    secondsKnob.setBounds (712, 320, 90, 110);
    cancelBtn.setFontHeight (12.0f);
    cancelBtn.onClick = [this] (const ModifierKeys&) { proc.assistant.cancel(); setPage (proc.assistant.built() ? Result : Start); };
    addAndMakeVisible (cancelBtn);
    cancelBtn.setBounds (482, 470, 140, 40);
    backBtn.setFontHeight (11.0f);
    backBtn.onClick = [this] (const ModifierKeys&) { if (onClose) onClose(); };
    addAndMakeVisible (backBtn);
    backBtn.setBounds (954, 740, 136, 40);

    // --- result: intent controls ------------------------------------------------------------
    auto fader = [this] (Fader& f, const String& tip, Rect r)
    {
        f.setTooltip (tip);
        addAndMakeVisible (f);
        f.setBounds (r);
    };
    const int x0 = kIntentBox.getX() + 20;
    fader (eqFader, "EQUALIZER: how much of the EQ the assistant set (the Equalizer's mix).", Rect (x0 + 110, 76, 270, 34));
    bind (eqFader, "eq.mix");
    fader (sculptorFader, "SCULPTOR: how far the spectral shaping goes toward the target.", Rect (x0 + 110, 116, 270, 34));
    bind (sculptorFader, "scu.a.amount");
    fader (densityFader, "DENSITY: the range of upward compression on the quiet parts.", Rect (x0 + 110, 218, 270, 34));
    densityFader.setRange (0.0, 20.0, 0.1);
    densityFader.format = [] (double v) { return ids::decibels ((float) v); };
    densityFader.onValueChange = [this] { if (densityFader.isMouseButtonDown()) setAllBands ("range", (float) densityFader.getValue()); };
    fader (compFader, "COMPRESSOR: how much of Compressor 1 is blended in.", Rect (x0 + 110, 258, 270, 34));
    bind (compFader, "cmp1.mix");
    driveKnob.caption = "DRIVE";
    driveKnob.setRange (0.0, 100.0, 1.0);
    driveKnob.format = [] (double v) { return String (roundToInt (v)) + "%"; };
    driveKnob.onValueChange = [this] { if (driveKnob.isMouseButtonDown()) setAllBands ("drive", (float) driveKnob.getValue(), true); };
    driveKnob.setTooltip ("DRIVE: the Exciter's drive on every band.");
    addAndMakeVisible (driveKnob);
    driveKnob.setBounds (x0 + 4, 350, 96, 110);
    classicBtn.setFontHeight (10.0f); trashBtn.setFontHeight (10.0f);
    classicBtn.onClick = [this] (const ModifierKeys&) { setAllBands ("family", 0.0f); };
    trashBtn.onClick = [this] (const ModifierKeys&) { setAllBands ("family", 1.0f); };
    classicBtn.setTooltip ("Subtle excitation: Retro, Tape, Tube and Warm.");
    trashBtn.setTooltip ("Dramatic distortion: Overdrive, Scream, Clipper and Scratch.");
    addAndMakeVisible (classicBtn); addAndMakeVisible (trashBtn);
    classicBtn.setBounds (x0 + 4, 470, 96, 28);
    trashBtn.setBounds (x0 + 4, 504, 96, 28);
    pad.get = [this] { return std::make_pair (proc.getParam ("exc.band1.x"), proc.getParam ("exc.band1.y")); };
    pad.set = [this] (float x, float y) { setAllBands ("x", x); setAllBands ("y", y); };
    pad.setTooltip ("Blends the harmonic profiles on every band of the Exciter.");
    addAndMakeVisible (pad);
    pad.setBounds (x0 + 110, 346, 160, 190);
    toneKnob.caption = "TONE";
    toneKnob.bipolar = true;
    toneKnob.setTooltip ("Where the distortion lands: below 0 the lows, above 0 the highs.");
    addAndMakeVisible (toneKnob);
    toneKnob.setBounds (x0 + 284, 370, 96, 120);
    bind (toneKnob, "exc.tone");
    fader (widthFader, "WIDTH: narrower (-100% is mono) or wider.", Rect (x0 + 110, 610, 270, 34));
    bind (widthFader, ids::width);
    widthFader.bipolar = true;

    // --- result: library and navigation ---------------------------------------------------------
    addAndMakeVisible (targets);
    targets.setBounds (kLibraryBox.getX() + 8, kLibraryBox.getY() + 18, kLibraryBox.getWidth() - 16, 560);
    addRefBtn.setFontHeight (10.0f);
    addRefBtn.setTooltip ("Make a target from an audio file: a mix or a stem whose sound you like. It learns the loudest eight seconds.");
    addRefBtn.onClick = [this] (const ModifierKeys&) { addReference(); };
    addAndMakeVisible (addRefBtn);
    addRefBtn.setBounds (kLibraryBox.getX() + 8, kLibraryBox.getBottom() - 100, 124, 30);
    removeRefBtn.setFontHeight (10.0f);
    removeRefBtn.setTooltip ("Delete the selected reference target.");
    removeRefBtn.onClick = [this] (const ModifierKeys&) { proc.assistant.removeCustomTarget (proc.assistant.target()); targets.repaint(); };
    addAndMakeVisible (removeRefBtn);
    removeRefBtn.setBounds (kLibraryBox.getX() + 138, kLibraryBox.getBottom() - 100, 124, 30);
    restoreBtn.setFontHeight (10.0f);
    restoreBtn.setTooltip ("The settings have moved away from what the assistant set for this target: put them back.");
    restoreBtn.onClick = [this] (const ModifierKeys&) { proc.assistant.restore(); };
    addAndMakeVisible (restoreBtn);
    restoreBtn.setBounds (kLibraryBox.getX() + 8, kLibraryBox.getBottom() - 60, 254, 30);
    meter.source = &spectrum;
    addAndMakeVisible (meter);
    meter.setBounds (kMeterBox.getX() + 8, kMeterBox.getY() + 40, kMeterBox.getWidth() - 16, 360);
    relearnBtn.setFontHeight (12.0f);
    relearnBtn.setTooltip ("Listen again, to new or different audio.");
    relearnBtn.onClick = [this] (const ModifierKeys&) { startRun(); };
    addAndMakeVisible (relearnBtn);
    relearnBtn.setBounds (14, 740, 136, 40);
    detailBtn.setFontHeight (11.0f);
    detailBtn.setTooltip ("The modules the assistant built, to fine-tune.");
    detailBtn.onClick = [this] (const ModifierKeys&) { if (onClose) onClose(); };
    addAndMakeVisible (detailBtn);
    detailBtn.setBounds (954, 740, 136, 40);

    setPage (proc.assistant.analysing() ? Listening : proc.assistant.built() ? Result : Start);
}

AssistantView::~AssistantView()
{
    attachments.clear();
    setLookAndFeel (nullptr);
}

void AssistantView::bind (Slider& s, const char* id)
{
    if (auto* param = proc.param (id))
    {
        auto fmt = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
        if (auto* f = dynamic_cast<Fader*> (&s)) f->format = fmt;
        if (auto* k = dynamic_cast<Knob*> (&s)) k->format = fmt;
        attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, s));
    }
}

void AssistantView::setAllBands (const String& key, float v, bool skipZeroDrive)
{
    if (key == "range")
    {
        for (const char* s : { "a", "b" })
            for (int b = 1; b <= 3; ++b) proc.setParam ("den." + String (s) + ".band" + String (b) + ".range", v);
        return;
    }
    for (int b = 1; b <= 3; ++b)
    {
        const String id = "exc.band" + String (b) + "." + key;
        if (skipZeroDrive && proc.getParam (id) < 0.5f && b == 1 && proc.getParam ("exc.bands") > 0.5f) continue;   // a low band the assistant left clean
        proc.setParam (id, v);
    }
}

void AssistantView::showStart() { setPage (Start); }
void AssistantView::showResult() { setPage (proc.assistant.analysing() ? Listening : proc.assistant.built() ? Result : Start); }

void AssistantView::setPage (Page p)
{
    currentPage = p;
    showAll (p == Start, goBtn, secondsKnob, backBtn);
    cancelBtn.setVisible (p == Listening);
    const bool r = p == Result;
    showAll (r, relearnBtn, detailBtn, restoreBtn, addRefBtn, removeRefBtn, classicBtn, trashBtn, eqFader, sculptorFader, densityFader, compFader, widthFader,
             driveKnob, toneKnob, pad, targets, meter);
    repaint();
}

void AssistantView::startRun()
{
    proc.startAssistant ((float) secondsKnob.getValue());
    setPage (Listening);
}

void AssistantView::addReference()
{
    chooser = std::make_unique<FileChooser> ("Pick a reference: a mix or a stem whose sound you want", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File()) return;
        String err;
        if (! proc.assistant.addReferenceTarget (f, err))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't add the reference", err);
        targets.repaint();
    });
}

void AssistantView::refresh()
{
    if (currentPage == Listening && ! proc.assistant.analysing())
        setPage (proc.assistant.built() ? Result : Start);
    if (currentPage == Result)
    {
        if (! densityFader.isMouseButtonDown()) densityFader.setValue (proc.getParam ("den.a.band1.range"), dontSendNotification);
        if (! driveKnob.isMouseButtonDown())
        {
            float mx = 0.0f;
            for (int b = 1; b <= 3; ++b) mx = jmax (mx, proc.getParam ("exc.band" + String (b) + ".drive"));
            driveKnob.setValue (mx, dontSendNotification);
        }
        const bool trash = proc.getParam ("exc.band1.family") > 0.5f && proc.getParam ("exc.band" + String (proc.getParam ("exc.bands") > 0.5f ? 2 : 1) + ".family") > 0.5f;
        classicBtn.setLit (! trash);
        trashBtn.setLit (trash);
        const int base = trash ? 4 : 0;
        pad.corners = { Exciter::shapeName (base), Exciter::shapeName (base + 1), Exciter::shapeName (base + 2), Exciter::shapeName (base + 3) };
        pad.repaint();
        // Intent controls only work on modules that are in the chain.
        eqFader.setEnabled (proc.chain.inChain (ModEqualizer));
        sculptorFader.setEnabled (proc.chain.inChain (ModSculptor));
        densityFader.setEnabled (proc.chain.inChain (ModDensity));
        compFader.setEnabled (proc.chain.inChain (ModCompressor1));
        enableAll (proc.chain.inChain (ModExciter), driveKnob, toneKnob, pad, classicBtn, trashBtn);
        restoreBtn.setEnabled (proc.assistant.dirty());
        removeRefBtn.setEnabled (proc.assistant.isCustom (proc.assistant.target()));
        meter.repaint();
        targets.repaint();
    }
    repaint();
}

void AssistantView::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    if (currentPage == Start)
    {
        drawText (g, "TRACK ASSISTANT", { 0.0f, 130.0f, (float) getWidth(), 40.0f }, 30.0f, colours::brand, true);
        g.setColour (colours::ink);
        g.setFont (panelFont (15.0f, false));
        g.drawFittedText ("Play the track (a part that sounds like the rest of it) and press GO. Planish listens, works out what the track is "
                          "(a vocal, a kick, a bass, a guitar...), and builds a starting point toward that instrument's target: "
                          "an Equalizer, a Compressor, the Exciter, Density and Sculptor, with a Gate and the Transient Shaper for drums.",
                          Rect (190, 180, 724, 80), Justification::centred, 4, 1.0f);
        g.setColour (colours::inkDim);
        g.setFont (panelFont (12.0f, false));
        g.drawFittedText ("It replaces the modules it uses. UNDO (or HISTORY) brings back what you had.", Rect (190, 540, 724, 20), Justification::centred, 1, 1.0f);
        return;
    }
    if (currentPage == Listening)
    {
        drawText (g, "LISTENING", { 0.0f, 250.0f, (float) getWidth(), 40.0f }, 30.0f, colours::brand, true);
        const auto bar = Rectangle<float> (282.0f, 320.0f, 540.0f, 28.0f);
        drawLcdBox (g, bar);
        g.setColour (colours::lcd);
        g.fillRect (bar.reduced (3.0f).withWidth ((bar.getWidth() - 6.0f) * proc.assistant.progress()));
        drawText (g, proc.hostPlaying() || proc.chain.analyser.seconds() > 0.1f ? "Keep it playing..." : "Start playback in Live.", { 0.0f, 370.0f, (float) getWidth(), 24.0f }, 15.0f, colours::ink, false);
        return;
    }

    // Result.
    drawSection (g, kIntentBox.toFloat(), "ASSISTANT");
    drawSection (g, kLibraryBox.toFloat(), "TARGET LIBRARY");
    drawSection (g, kMeterBox.toFloat(), "TONAL BALANCE");
    const float x0 = (float) kIntentBox.getX() + 20.0f;
    auto heading = [&] (const String& t, float y) { drawText (g, t, { x0, y, 200.0f, 16.0f }, 12.0f, colours::brand, true, Justification::centredLeft); };
    auto label = [&] (const String& t, float y) { drawText (g, t, { x0, y, 100.0f, 34.0f }, 11.0f, colours::ink, true, Justification::centredLeft); };
    heading ("TONE", 52.0f);           label ("EQUALIZER", 76.0f);  label ("SCULPTOR", 116.0f);
    heading ("DYNAMICS", 194.0f);      label ("DENSITY", 218.0f);   label ("COMPRESSOR", 258.0f);
    heading ("SATURATION", 322.0f);
    heading ("WIDTH", 586.0f);         label ("WIDTH", 610.0f);
    // The summary.
    g.setColour (colours::inkDim);
    g.setFont (panelFont (11.5f, false));
    g.drawFittedText (proc.assistant.summary(), Rect (kMeterBox.getX() + 12, kMeterBox.getY() + 420, kMeterBox.getWidth() - 24, 250), Justification::topLeft, 16, 1.0f);
    drawText (g, "TARGET: " + proc.assistant.targetName (proc.assistant.target()).toUpperCase() + (proc.assistant.dirty() ? "  *" : ""),
              { (float) kMeterBox.getX() + 12.0f, (float) kMeterBox.getY() + 14.0f, 330.0f, 18.0f }, 12.0f, colours::lcd, true, Justification::centredLeft);
}

// ---------------------------------------------------------------------------
void AssistantView::BalanceMeter::paintOverlay (Graphics& g)
{
    // The target as a band either side of its curve, fitted to the output's level.
    auto& a = view.proc.assistant;
    const int t = a.target();
    float fit = 0.0f; int n = 0;
    for (float hz = 100.0f; hz <= 8000.0f; hz *= 1.26f)
    {
        if (source == nullptr) break;
        fit += source->dbAtHz (hz) - a.targetDb (t, hz);
        ++n;
    }
    fit = n > 0 ? fit / (float) n : -40.0f;
    Path band;
    for (int i = 0; i <= 100; ++i)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) i / 100.0f);
        const float y = yForDb (a.targetDb (t, hz) + fit + 4.0f);
        if (i == 0) band.startNewSubPath (xForHz (hz), y); else band.lineTo (xForHz (hz), y);
    }
    for (int i = 100; i >= 0; --i)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) i / 100.0f);
        band.lineTo (xForHz (hz), yForDb (a.targetDb (t, hz) + fit - 4.0f));
    }
    band.closeSubPath();
    g.setColour (juce::Colour (0xffa98dff).withAlpha (0.25f));
    g.fillPath (band);
    g.setColour (juce::Colour (0xffa98dff).withAlpha (0.7f));
    g.strokePath (band, PathStrokeType (1.0f));
    drawText (g, "the target band, and the output (grey line)", { plotArea().getX() + 8.0f, plotArea().getY() + 6.0f, 300.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
}

// ---------------------------------------------------------------------------
int AssistantView::TargetList::rowAt (float y) const
{
    const int r = (int) ((y + scroll) / (float) kRowH);
    return r >= 0 && r < view.proc.assistant.numTargets() ? r : -1;
}

void AssistantView::TargetList::paint (Graphics& g)
{
    auto& a = view.proc.assistant;
    g.setColour (colours::black);
    g.fillRect (getLocalBounds());
    g.saveState();
    g.reduceClipRegion (getLocalBounds());
    String lastCat;
    for (int i = 0; i < a.numTargets(); ++i)
    {
        const float y = (float) (i * kRowH) - scroll;
        if (y + kRowH < 0 || y > (float) getHeight()) continue;
        const auto row = Rectangle<float> (0.0f, y, (float) getWidth(), (float) kRowH);
        const bool sel = i == a.target();
        if (sel) { g.setColour (colours::cream); g.fillRect (row.reduced (1.0f)); }
        else if (i == hoverRow) { g.setColour (colours::socket); g.fillRect (row.reduced (1.0f)); }
        const String cat = a.isCustom (i) ? "CUSTOM" : InstrumentTargets::category (i);
        const Colour ink = sel ? colours::black : colours::ink;
        drawText (g, a.targetName (i), row.withTrimmedLeft (10.0f).withTrimmedRight (90.0f), 12.0f, ink, sel, Justification::centredLeft);
        String tag = i == a.detected() && a.hasResult() ? "SUGGESTED" : cat;
        if (sel && a.dirty()) tag = "* EDITED";
        drawText (g, tag, row.withTrimmedRight (8.0f).withLeft (row.getRight() - 90.0f), 9.0f, sel ? colours::black : (tag == "SUGGESTED" ? colours::lcd : colours::inkDim), true, Justification::centredRight);
        ignoreUnused (lastCat);
    }
    g.restoreState();
    g.setColour (colours::inkDim);
    g.drawRect (getLocalBounds(), 1);
}

void AssistantView::TargetList::mouseDown (const MouseEvent& e)
{
    const int r = rowAt (e.position.y);
    if (r < 0) return;
    view.proc.assistant.setTarget (r);
    repaint();
}

void AssistantView::TargetList::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    const float maxScroll = jmax (0.0f, (float) (view.proc.assistant.numTargets() * kRowH - getHeight()));
    scroll = jlimit (0.0f, maxScroll, scroll - w.deltaY * 120.0f);
    repaint();
}
} // namespace planish::ui
