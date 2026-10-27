#include "ModulePanels.h"
#include "../Settings.h"

namespace planish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

namespace
{
const char* kShapeBlurb[NumFilterShapes] {
    "PROPORTIONAL Q: the more you boost or cut, the narrower it gets. Tight, precise corrective cuts.",
    "BELL: a smooth boost or cut around the frequency; big moves change the colour more than Proportional Q.",
    "BAND SHELF: a bell with a flat top, for a block of frequencies.",
    "LOW SHELF ANALOG: everything below the frequency up or down, as an analog shelf does.",
    "LOW SHELF VINTAGE: the shelf with a complementary dip above it, as the passive program equalisers do: fat lows without mud.",
    "LOW SHELF BAXANDALL: a gentle first-order shelf, the most natural way to move the extreme lows. No Q.",
    "HIGH SHELF ANALOG: everything above the frequency up or down.",
    "HIGH SHELF VINTAGE: the shelf with a complementary dip below it.",
    "HIGH SHELF BAXANDALL: a gentle first-order shelf for air without harshness. No Q.",
    "HIGH PASS FLAT: a Butterworth high pass, no resonance. Takes the low end away cleanly.",
    "HIGH PASS RESONANT: a high pass with a bump at the cutoff; Q sets the bump.",
    "LOW PASS FLAT: a Butterworth low pass, no resonance.",
    "LOW PASS RESONANT: a low pass with a bump at the cutoff; Q sets the bump." };

void setFont (std::initializer_list<PanelButton*> bs, float h) { for (auto* b : bs) b->setFontHeight (h); }
} // namespace

// ===========================================================================
// SetSelector / BandButtons
// ===========================================================================
SetSelector::SetSelector()
{
    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (set[i]);
        addAndMakeVisible (solo[i]);
        set[i].setFontHeight (10.0f);
        solo[i].setFontHeight (10.0f);
        set[i].onClick = [this, i] (const ModifierKeys&) { selected = i; if (onSelect) onSelect (i); };
        solo[i].onClick = [this, i] (const ModifierKeys&) { soloed = soloed == i ? -1 : i; if (onSolo) onSolo (soloed); };
        set[i].setTooltip ("Show this part's controls.");
        solo[i].setTooltip ("Hear this part alone.");
    }
}

void SetSelector::update (int mode)
{
    const bool two = mode != Stereo;
    setVisible (two);
    if (! two && (selected != 0 || soloed >= 0))
    {
        selected = 0;
        soloed = -1;
        if (onSelect) onSelect (0);
        if (onSolo) onSolo (-1);
    }
    if (mode != lastMode)
    {
        lastMode = mode;
        set[0].setLabel (Lanes::setName (mode, 0));
        set[1].setLabel (Lanes::setName (mode, 1));
    }
    for (int i = 0; i < 2; ++i) { set[i].setLit (selected == i); solo[i].setLit (soloed == i); }
}

void SetSelector::resized()
{
    auto r = getLocalBounds();
    for (int i = 0; i < 2; ++i)
    {
        set[i].setBounds (r.removeFromLeft (52));
        solo[i].setBounds (r.removeFromLeft (24));
        r.removeFromLeft (6);
    }
}

BandButtons::BandButtons (int count, bool nodeColours)
{
    for (int i = 0; i < count; ++i)
    {
        auto b = std::make_unique<PanelButton> (String (i + 1));
        b->setFontHeight (11.0f);
        b->setLitColour (nodeColours ? colours::node (i) : colours::band (i));
        b->setTooltip ("Select band " + String (i + 1) + ".");
        b->onClick = [this, i] (const ModifierKeys&) { if (onSelect) onSelect (i); };
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
}

void BandButtons::update (int selected, int numShown, std::function<bool (int)> dimmed)
{
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        buttons[(size_t) i]->setVisible (i < numShown);
        buttons[(size_t) i]->setLit (i == selected);
        buttons[(size_t) i]->setDimmed (dimmed ? dimmed (i) : false);
    }
}

void BandButtons::resized()
{
    const int n = (int) buttons.size();
    const int cols = jmin (columns, n);
    const int rows = (n + cols - 1) / cols;
    const int w = getWidth() / cols, h = getHeight() / rows;
    for (int i = 0; i < n; ++i)
        buttons[(size_t) i]->setBounds ((i % cols) * w, (i / cols) * h, w - 3, h - 3);
}

// ===========================================================================
// EqPanel
// ===========================================================================
EqPanel::EqPanel (PlanishProcessor& p, SpectrumSource& out, SpectrumSource& in)
    : ModulePanel (p, ModEqualizer, out), eq (p.chain.eq), input (in), curve (*this)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (eq.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int s) { viewSet = s; bindBand(); };
    sets.onSolo = [this] (int s) { eq.soloSet.store (s); };
    h.removeFromLeft (8);
    addToggle (softSatBtn, pid (eq.pSoftSat), h.removeFromLeft (80), "SOFT SATURATION: gentle harmonics from the EQ stage, for a fuller sound and a little colour.");
    addButton (maskingBtn, h.removeFromLeft (80), "MASKING METER: see where this track and another Planish track compete for the same frequencies.",
               [this] (const ModifierKeys&) { maskingOn = ! maskingOn; if (! maskingOn) editOther = false; bindBand(); });
    addButton (learnBtn, h.removeFromLeft (64), "LEARN: listens for two seconds and moves the bands you have to the places that matter in this track: rumble, resonances, sibilance. It never adds or removes a band and leaves gain and Q alone.",
               [this] (const ModifierKeys&) { learning = ! learning; learnStarted = Time::getMillisecondCounterHiRes(); learnAcc.assign (SpectrumSource::kColumns, 0.0f); });
    h.removeFromLeft (8);
    scaleChoice.getText = [] { return String (frequencyScaleName (frequencyScale())); };
    scaleChoice.onOpen = [this]
    {
        PopupMenu m;
        for (int i = 0; i < NumScales; ++i)
            m.addItem (frequencyScaleName (i), true, frequencyScale() == i, [i] { setFrequencyScale (i); Settings().setFrequencyScale (i); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&scaleChoice));
    };
    scaleChoice.fontH = 11.0f;
    scaleChoice.setTooltip ("The frequency scale of every spectrum: logarithmic, mel (as we hear pitch), linear (detail in the highs) or with a piano roll.");
    addAndMakeVisible (scaleChoice);
    scaleChoice.setBounds (h.removeFromLeft (120));
    setFont ({ &softSatBtn, &maskingBtn, &learnBtn }, 10.0f);

    // Curve.
    addAndMakeVisible (curve);
    curve.setBounds (kViewArea);
    curve.source = &spectrum;
    curve.secondary = &input;
    curve.secondaryColour = colours::spectrum.darker (0.4f);
    curve.numBands = Equalizer::kBands;
    curve.getBand = [this] (int b)
    {
        EqCurveView::Band bd;
        bd.on = tpv (bandKey (b, "on")) > 0.5f;
        bd.hz = tpv (bandKey (b, "freq"));
        const int shape = (int) tpv (bandKey (b, "shape"));
        bd.hasGain = shapeHasGain (shape);
        bd.hasQ = shapeHasQ (shape);
        bd.gainDb = bd.hasGain ? tpv (bandKey (b, "gain")) : 0.0f;
        bd.q = tpv (bandKey (b, "q"));
        bd.dynamic = target().chain.eq.isDynamic (viewSet, b);
        bd.up = (int) tpv (bandKey (b, "direction")) == Equalizer::Up;
        bd.dynGainDb = target().chain.eq.dynGainDb[viewSet][b];
        bd.colour = colours::node (b);
        return bd;
    };
    curve.setBandFreqGain = [this] (int b, float hz, float g)
    {
        tsetp (bandKey (b, "freq"), hz);
        if (shapeHasGain ((int) tpv (bandKey (b, "shape")))) tsetp (bandKey (b, "gain"), g);
        // INVERSE LINK: the nearest node of the other EQ moves the opposite way.
        if (inverseLink && maskingOn && ! editOther)
            if (auto* other = LinkRegistry::get().find (maskSource))
            {
                if (linkedRemoteBand < 0)
                {
                    float bd = 1e9f;
                    for (int k = 0; k < Equalizer::kBands; ++k)
                    {
                        const String key = "eq.a.b" + String (k + 1) + ".";
                        if (other->getParam (key + "on") < 0.5f) continue;
                        const float d = std::abs (std::log2 (other->getParam (key + "freq") / hz));
                        if (d < bd) { bd = d; linkedRemoteBand = k; }
                    }
                }
                if (linkedRemoteBand >= 0)
                {
                    const String key = "eq.a.b" + String (linkedRemoteBand + 1) + ".";
                    other->setParam (key + "shape", tpv (bandKey (b, "shape")));
                    other->setParam (key + "freq", hz);
                    other->setParam (key + "gain", -g);
                }
            }
    };
    curve.setBandQ = [this] (int b, float q) { tsetp (bandKey (b, "q"), q); };
    curve.onSelect = [this] (int b) { linkedRemoteBand = -1; selectBand (b); };
    curve.onAdd = [this] (int b, float hz)
    {
        // Contextual shapes: far left a high pass (once) or low shelf, far right a
        // low pass (once) or high shelf, a proportional Q in between.
        bool haveHp = false, haveLp = false;
        for (int i = 0; i < Equalizer::kBands; ++i)
            if (tpv (bandKey (i, "on")) > 0.5f)
            {
                const int s = (int) tpv (bandKey (i, "shape"));
                haveHp = haveHp || shapeIsHighPass (s);
                haveLp = haveLp || shapeIsLowPass (s);
            }
        int shape = ProportionalQ;
        if (hz < 40.0f) shape = haveHp ? LowShelfAnalog : HighPassFlat;
        else if (hz > 14000.0f) shape = haveLp ? HighShelfAnalog : LowPassFlat;
        tsetp (bandKey (b, "shape"), (float) shape);
        tsetp (bandKey (b, "freq"), hz);
        tsetp (bandKey (b, "gain"), 0.0f);
        tsetp (bandKey (b, "q"), 1.0f);
        tsetp (bandKey (b, "dyn"), 0.0f);
        tsetp (bandKey (b, "on"), 1.0f);
    };
    curve.onRemove = [this] (int b) { tsetp (bandKey (b, "on"), 0.0f); };
    curve.onReset = [this] (int b) { tsetp (bandKey (b, "gain"), 0.0f); tsetp (bandKey (b, "q"), 1.0f); };
    curve.onDirection = [this] (int b, bool up) { tsetp (bandKey (b, "direction"), up ? 0.0f : 1.0f); };
    curve.compositeDb = [this] (float hz) { float o; target().chain.eq.curveDb (viewSet, &hz, &o, 1, false); return o; };
    curve.bandDb = [this] (int b, float hz) { float o; target().chain.eq.bandCurveDb (viewSet, b, &hz, &o, 1, false); return o; };
    curve.onAltSolo = [this] (float hz, bool on) { eq.altSoloQ = Settings().altSoloQ(); eq.altSoloHz.store (on ? hz : -1.0f); };
    curve.onHistogramClick = [this] { for (auto& v : histogram) v = 0.0f; };

    // HUD.
    auto c = kControlArea;
    addAndMakeVisible (bandBtns);
    bandBtns.columns = 6;
    bandBtns.setBounds (c.getX(), c.getY() + 10, 168, 56);
    bandBtns.onSelect = [this] (int b) { selectBand (b); curve.selected = b; };
    auto row = Rect (c.getX() + 190, c.getY() + 12, 880, 54);
    addButton (bandOnBtn, row.removeFromLeft (50).withHeight (36), "Switch the selected band on or off (a band that is off still shows, in grey).",
               [this] (const ModifierKeys&) { tsetp (bandKey (selected, "on"), tpv (bandKey (selected, "on")) > 0.5f ? 0.0f : 1.0f); });
    row.removeFromLeft (6);
    addButton (bandSoloBtn, row.removeFromLeft (56).withHeight (36), "Hear only the region this band affects. Alt-click a node does the same; alt-click anywhere opens a window you can sweep.",
               [this] (const ModifierKeys&)
               {
                   const bool on = eq.soloBand.load() != selected;
                   eq.soloBand.store (on ? selected : -1);
                   eq.soloSet.store (on && (int) pv (eq.pChMode) != Stereo ? viewSet : -1);
               });
    row.removeFromLeft (6);
    addButton (bandRemoveBtn, row.removeFromLeft (36).withHeight (36), "Remove the selected band.", [this] (const ModifierKeys&) { tsetp (bandKey (selected, "on"), 0.0f); });
    setFont ({ &bandOnBtn, &bandSoloBtn, &bandRemoveBtn }, 11.0f);
    row.removeFromLeft (16);
    addChoice (shapeChoice, pid (eq.band[0][0].shape), {}, row.removeFromLeft (190).withHeight (36), "The filter shape. The line at the foot of the panel says what each does.");
    shapeChoice.fontH = 13.0f;
    row.removeFromLeft (14);
    addReadout (freqRo, pid (eq.band[0][0].freq), "FREQ", row.removeFromLeft (104), "Centre or cutoff frequency, 20 Hz to 20 kHz. Drag to scrub, double-click to reset.");
    row.removeFromLeft (8);
    addReadout (gainRo, pid (eq.band[0][0].gain), "GAIN", row.removeFromLeft (96), "Boost or cut, -30 to +15 dB.");
    row.removeFromLeft (8);
    addReadout (qRo, pid (eq.band[0][0].q), "Q", row.removeFromLeft (84), "Width: higher is narrower. For resonant filters, the size of the bump.");
    row.removeFromLeft (8);
    addChoice (slopeChoice, pid (eq.band[0][0].slope), "SLOPE", row.removeFromLeft (112), "The slope of a pass filter, dB per octave.");

    // Advanced: dynamic mode and sidechain.
    auto adv = Rect (c.getX() + 190, c.getY() + 86, 880, 136);
    addButton (dynBtn, adv.removeFromLeft (84).withHeight (34), "DYNAMIC: the band moves with the level in its region (or its sidechain) once it passes the threshold. Not for pass filters.",
               [this] (const ModifierKeys&) { tsetp (bandKey (selected, "dyn"), tpv (bandKey (selected, "dyn")) > 0.5f ? 0.0f : 1.0f); });
    dynBtn.setFontHeight (11.0f);
    adv.removeFromLeft (16);
    addFader (thresholdFader, pid (eq.band[0][0].threshold), "THRESHOLD", adv.removeFromLeft (56), "The level in the band's region at which it starts to move.");
    addAndMakeVisible (thresholdMeter);
    thresholdMeter.setBounds (adv.removeFromLeft (26).withTrimmedBottom (16).reduced (2, 8));
    adv.removeFromLeft (16);
    auto col = adv.removeFromLeft (70);
    addButton (upBtn, col.removeFromTop (40), "UP: the gain rises when the signal passes the threshold.", [this] (const ModifierKeys&) { tsetp (bandKey (selected, "direction"), 0.0f); });
    col.removeFromTop (8);
    addButton (downBtn, col.removeFromTop (40), "DOWN: the gain falls when the signal passes the threshold.", [this] (const ModifierKeys&) { tsetp (bandKey (selected, "direction"), 1.0f); });
    setFont ({ &upBtn, &downBtn }, 11.0f);
    adv.removeFromLeft (16);
    col = adv.removeFromLeft (100);
    addReadout (attackRo, pid (eq.band[0][0].attack), "ATTACK", col.removeFromTop (54), "How fast the band reacts once the level passes the threshold.");
    col.removeFromTop (8);
    addReadout (releaseRo, pid (eq.band[0][0].release), "RELEASE", col.removeFromTop (54), "How fast it returns once the level drops back.");
    adv.removeFromLeft (16);
    addChoice (directionChoice, pid (eq.band[0][0].direction), {}, Rect(), {});
    directionChoice.setVisible (false);
    addChoice (sidechainChoice, pid (eq.band[0][0].sidechain), "SIDECHAIN", adv.removeFromLeft (170).withHeight (52),
               "What the band listens to: SELF (its own region), the full signal, another band's region, or the external sidechain (full, or through a band's region). A band that is off can still be a sidechain for another.");
    sidechainChoice.fontH = 12.0f;

    // Masking controls (over the HUD when on).
    maskSourceChoice.getText = [this]
    {
        if (auto* o = LinkRegistry::get().find (maskSource)) return o->instanceName().toUpperCase();
        return String ("CHOOSE A TRACK");
    };
    maskSourceChoice.onOpen = [this] { openMaskingSourceMenu(); };
    maskSourceChoice.caption = "COMPARE WITH";
    maskSourceChoice.setTooltip ("The other Planish track to compare with. Every Planish in the session is listed.");
    addAndMakeVisible (maskSourceChoice);
    maskSourceChoice.setBounds (c.getX() + 190, c.getY() + 12, 220, 54);
    sensitivityKnob.caption = "SENSITIVITY";
    sensitivityKnob.setRange (0.0, 100.0, 1.0);
    sensitivityKnob.setValue (50.0, dontSendNotification);
    sensitivityKnob.format = [] (double v) { return String (roundToInt (v)) + "%"; };
    sensitivityKnob.onValueChange = [this] { maskSensitivity = (float) sensitivityKnob.getValue() * 0.01f; };
    sensitivityKnob.setTooltip ("How little loudness loss counts. Higher shows more masking; it changes nothing in the sound.");
    addAndMakeVisible (sensitivityKnob);
    sensitivityKnob.setBounds (c.getX() + 430, c.getY() + 4, 76, 82);
    offsetKnob.caption = "GAIN OFFSET";
    offsetKnob.bipolar = true;
    offsetKnob.setRange (-24.0, 24.0, 0.5);
    offsetKnob.setValue (proc.maskingGainOffsetDb, dontSendNotification);
    offsetKnob.format = [] (double v) { return ids::decibels ((float) v); };
    offsetKnob.onValueChange = [this] { proc.maskingGainOffsetDb = (float) offsetKnob.getValue(); };
    offsetKnob.setTooltip ("Set to this track's fader in Live, so the meter compares the levels you actually hear.");
    addAndMakeVisible (offsetKnob);
    offsetKnob.setBounds (c.getX() + 512, c.getY() + 4, 76, 82);
    addButton (editSelfBtn, Rect (c.getX() + 610, c.getY() + 22, 90, 34), "Edit this track's EQ.", [this] (const ModifierKeys&) { editOther = false; bindBand(); });
    addButton (editOtherBtn, Rect (c.getX() + 704, c.getY() + 22, 90, 34), "Edit the other track's EQ from here (avoid automating it this way).", [this] (const ModifierKeys&) { editOther = true; bindBand(); });
    addButton (inverseBtn, Rect (c.getX() + 810, c.getY() + 22, 110, 34), "INVERSE LINK: moving a node here moves the nearest node of the other EQ to the same frequency with the opposite gain. Small, shared moves beat one big one.",
               [this] (const ModifierKeys&) { inverseLink = ! inverseLink; linkedRemoteBand = -1; });
    addButton (bypassEqsBtn, Rect (c.getX() + 926, c.getY() + 22, 110, 34), "Bypass both Equalizers, to check the moves help.", [this] (const ModifierKeys&)
    {
        eqsBypassed = ! eqsBypassed;
        proc.setParam ("eq.on", eqsBypassed ? 0.0f : 1.0f);
        if (auto* o = LinkRegistry::get().find (maskSource)) o->setParam ("eq.on", eqsBypassed ? 0.0f : 1.0f);
    });
    setFont ({ &editSelfBtn, &editOtherBtn, &inverseBtn, &bypassEqsBtn }, 10.0f);

    selectBand (0);
    setFrequencyScale (Settings().frequencyScale());
}

EqPanel::~EqPanel() = default;

PlanishProcessor& EqPanel::target()
{
    if (editingOther())
        if (auto* o = LinkRegistry::get().find (maskSource)) return *o;
    return proc;
}

bool EqPanel::editingOther() const { return maskingOn && editOther && LinkRegistry::get().find (maskSource) != nullptr; }

float EqPanel::tpv (const String& shortId) { return target().getParam ("eq." + shortId); }
void EqPanel::tsetp (const String& shortId, float v) { target().setParam ("eq." + shortId, v); }

String EqPanel::bandKey (int b, const char* key) const
{
    // The other EQ is edited in its first set: a remote instance shows its A controls.
    const int s = editingOther() ? 0 : viewSet;
    return String (s == 0 ? "a" : "b") + ".b" + String (b + 1) + "." + key;
}

void EqPanel::selectBand (int b)
{
    selected = jlimit (0, Equalizer::kBands - 1, b);
    curve.selected = selected;
    bindBand();
}

void EqPanel::bindBand()
{
    const auto& bi = eq.band[viewSet][selected];
    rebind (freqRo, pid (bi.freq));
    rebind (gainRo, pid (bi.gain));
    rebind (qRo, pid (bi.q));
    rebind (thresholdFader, pid (bi.threshold));
    rebind (attackRo, pid (bi.attack));
    rebind (releaseRo, pid (bi.release));
    rebindChoice (shapeChoice, pid (bi.shape));
    rebindChoice (slopeChoice, pid (bi.slope));
    rebindChoice (sidechainChoice, pid (bi.sidechain));
    shapeChoice.getText = [this] { return String (filterShapeName ((int) pv (eq.band[viewSet][selected].shape))).toUpperCase(); };
    // Masking layout.
    const bool hud = ! maskingOn;
    showAll (hud, bandOnBtn, bandSoloBtn, bandRemoveBtn, shapeChoice, freqRo, gainRo, qRo, slopeChoice, dynBtn, thresholdFader, thresholdMeter,
             upBtn, downBtn, attackRo, releaseRo, sidechainChoice);
    bandBtns.setVisible (hud);
    showAll (maskingOn, maskSourceChoice, sensitivityKnob, offsetKnob, editSelfBtn, editOtherBtn, inverseBtn, bypassEqsBtn);
    repaint();
}

void EqPanel::openMaskingSourceMenu()
{
    PopupMenu m;
    m.addItem ("None", true, maskSource < 0, [this] { maskSource = -1; editOther = false; });
    for (auto* o : LinkRegistry::get().all())
    {
        if (o == &proc) continue;
        const int oid = o->instanceId();
        const bool hasEq = o->chain.inChain (ModEqualizer);
        m.addItem (o->instanceName() + (hasEq ? "" : "  (no Equalizer)"), true, maskSource == oid, [this, oid] { maskSource = oid; for (auto& v : histogram) v = 0.0f; linkedRemoteBand = -1; });
    }
    if (LinkRegistry::get().all().size() < 2)
        m.addItem ("(put Planish on another track)", false, false, [] {});
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&maskSourceChoice));
}

void EqPanel::updateMasking()
{
    auto* other = LinkRegistry::get().find (maskSource);
    curve.masking = nullptr;
    curve.histogram = nullptr;
    curve.otherCompositeDb = nullptr;
    if (! maskingOn || other == nullptr) return;
    float loss[Masking::kBands];
    Masking::loudnessLoss (proc.maskingSpectrum(), other->maskingSpectrum(), loss);
    const double now = Time::getMillisecondCounterHiRes();
    const float dt = lastMaskMs > 0.0 ? (float) jlimit (0.0, 0.2, (now - lastMaskMs) * 0.001) : 0.033f;
    lastMaskMs = now;
    const int holdIdx = Settings().histogramHold();
    const float window = holdIdx == 0 ? 0.4f : holdIdx == 1 ? 3.0f : -1.0f;
    const float display = 30.0f * (1.25f - maskSensitivity);   // phons for full brightness
    const float collision = 22.0f * (1.35f - maskSensitivity);  // phons for a collision
    for (int b = 0; b < Masking::kBands; ++b)
    {
        maskNow[b] += 0.3f * (jlimit (0.0f, 1.0f, loss[b] / display) - maskNow[b]);
        if (window > 0.0f) histogram[b] *= std::exp (-dt / window);
        if (loss[b] > collision) histogram[b] = jmin (1.0f, histogram[b] + 0.04f);
    }
    curve.masking = maskNow;
    curve.histogram = histogram;
    if (other->chain.inChain (ModEqualizer) && ! editingOther())
        curve.otherCompositeDb = [other] (float hz) { float o; other->chain.eq.curveDb (0, &hz, &o, 1, false); return o; };
    else if (editingOther())
        curve.otherCompositeDb = [this] (float hz) { float o; eq.curveDb (viewSet, &hz, &o, 1, false); return o; };
}

void EqPanel::learnNodes()
{
    // Places of interest in what was heard: the rumble, resonant peaks, sibilance.
    std::vector<float> cand;
    const int n = SpectrumSource::kColumns;
    std::vector<float> sm ((size_t) n);
    for (int c = 0; c < n; ++c)
    {
        float s = 0.0f; int k = 0;
        for (int j = -10; j <= 10; ++j) if (c + j >= 0 && c + j < n) { s += learnAcc[(size_t) (c + j)]; ++k; }
        sm[(size_t) c] = s / (float) k;
    }
    for (int c = 2; c < n - 2; ++c)
    {
        const float hz = SpectrumSource::columnHz (c);
        const float v = learnAcc[(size_t) c];
        const bool peak = v > learnAcc[(size_t) c - 1] && v > learnAcc[(size_t) c + 1] && v > learnAcc[(size_t) c - 2] && v > learnAcc[(size_t) c + 2];
        if (! peak) continue;
        if (hz < 60.0f && v > sm[(size_t) c] - 3.0f) cand.push_back (hz);                   // rumble
        else if (hz > 4500.0f && hz < 10000.0f && v > sm[(size_t) c] + 3.0f) cand.push_back (hz);   // sibilance
        else if (v > sm[(size_t) c] + 5.0f) cand.push_back (hz);                             // resonance
    }
    std::sort (cand.begin(), cand.end());
    std::vector<bool> used (cand.size(), false);
    std::vector<std::pair<float, int>> bandsByFreq;
    for (int b = 0; b < Equalizer::kBands; ++b)
        if (pv (eq.band[viewSet][b].on) > 0.5f && ! shapeIsPass ((int) pv (eq.band[viewSet][b].shape)))
            bandsByFreq.push_back ({ pv (eq.band[viewSet][b].freq), b });
    std::sort (bandsByFreq.begin(), bandsByFreq.end());
    for (auto& [hz, b] : bandsByFreq)
    {
        int best = -1; float bd = 2.0f;
        for (size_t i = 0; i < cand.size(); ++i)
        {
            const float d = std::abs (std::log2 (cand[i] / hz));
            if (! used[i] && d < bd) { bd = d; best = (int) i; }
        }
        if (best >= 0) { used[(size_t) best] = true; setp (eq.band[viewSet][b].freq, cand[(size_t) best]); }
    }
}

void EqPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = (int) channelModeFromChoice (eq.info().channelModes, (int) pv (eq.pChMode));
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindBand(); }
    if (! sets.isVisible()) eq.soloSet.store (-1);
    sets.setEnabled (! editingOther());

    if (learning)
    {
        const float* cv = spectrum.curve();
        for (int c = 0; c < SpectrumSource::kColumns; ++c) learnAcc[(size_t) c] += 0.05f * (cv[c] - learnAcc[(size_t) c]);
        if (Time::getMillisecondCounterHiRes() - learnStarted > 2000.0) { learning = false; learnNodes(); }
    }
    learnBtn.setLit (learning);
    maskingBtn.setLit (maskingOn);
    editSelfBtn.setLit (! editOther);
    editOtherBtn.setLit (editOther);
    editOtherBtn.setEnabled (LinkRegistry::get().find (maskSource) != nullptr);
    inverseBtn.setLit (inverseLink);
    bypassEqsBtn.setLit (eqsBypassed);
    updateMasking();
    maskSourceChoice.refresh();

    const bool on = tpv (bandKey (selected, "on")) > 0.5f;
    bandOnBtn.setLit (on);
    bandSoloBtn.setLit (eq.soloBand.load() == selected);
    bandBtns.update (selected, Equalizer::kBands, [this] (int b) { return tpv (bandKey (b, "on")) < 0.5f; });
    const int shape = (int) tpv (bandKey (selected, "shape"));
    gainRo.setEnabled (shapeHasGain (shape));
    qRo.setEnabled (shapeHasQ (shape));
    slopeChoice.setVisible (! maskingOn && shapeUsesSlope (shape));
    const bool canDyn = shapeCanBeDynamic (shape);
    const bool dyn = tpv (bandKey (selected, "dyn")) > 0.5f && canDyn;
    dynBtn.setEnabled (canDyn);
    dynBtn.setLit (dyn);
    enableAll (dyn, thresholdFader, upBtn, downBtn, attackRo, releaseRo, sidechainChoice);
    const bool up = (int) tpv (bandKey (selected, "direction")) == Equalizer::Up;
    upBtn.setLit (dyn && up);
    downBtn.setLit (dyn && ! up);
    const int g = mode == Stereo ? 0 : viewSet;
    thresholdMeter.inputDb = eq.levelDb[g][selected];
    thresholdMeter.gainDb = dyn ? eq.dynGainDb[g][selected] - tpv (bandKey (selected, "gain")) * (up == (tpv (bandKey (selected, "gain")) >= 0.0f) ? 0.0f : 1.0f) : 0.0f;
    thresholdMeter.colour = colours::node (selected);
    thresholdMeter.repaint();
    bool anyDyn = false;
    for (int b = 0; b < Equalizer::kBands; ++b) anyDyn = anyDyn || target().chain.eq.isDynamic (viewSet, b);
    if (anyDyn) curve.dynamicDb = [this] (float hz) { float o; target().chain.eq.curveDb (viewSet, &hz, &o, 1, true); return o; };
    else curve.dynamicDb = nullptr;
    curve.showPeaks = Settings().spectrumPeakHold();

    if (editingOther())
        statusLine = "Editing " + target().instanceName() + "'s Equalizer: drag its nodes here. Its other controls are on its own panel.";
    else if (maskingOn && LinkRegistry::get().find (maskSource) == nullptr)
        statusLine = "MASKING: choose another track with Planish on it. Bright areas are where it hides this one; red bars from the top count the worst collisions (click them to clear).";
    else if (maskingOn)
        statusLine = "MASKING: bright where " + LinkRegistry::get().find (maskSource)->instanceName() + " takes loudness from this track. Its EQ curve is drawn in purple.";
    else if (learning)
        statusLine = "LEARN: listening...";
    else
        statusLine = String (kShapeBlurb[jlimit (0, NumFilterShapes - 1, shape)]) + (dyn ? "   Dynamic: " + ids::decibels (eq.dynGainDb[g][selected]) + " now." : "");
    curve.repaint();
}

void EqPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    if (! maskingOn)
    {
        drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (180).withHeight (70).toFloat(), "BAND " + String (selected + 1), colours::node (selected));
        drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (180).withTrimmedTop (82).withHeight (146).toFloat(), "ADVANCED");
    }
    else
        drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (180).withHeight (96).toFloat(), "MASKING METER", colours::ledYellow);
}

// ===========================================================================
// GatePanel
// ===========================================================================
GatePanel::GatePanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModGate, s), gate (p.chain.gate), xover (*this, gate.mb, gate.soloBand), detView (*this, gate.det)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader();
    for (int i = 0; i < 3; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (i == 0 ? 84 : 90), i == 0 ? "The waveform (input dark, output light) with the gain reduction of each band, and the thresholds to drag."
                   : i == 1 ? "Up to three bands over the spectrum." : "The filter in front of the gate's detector.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    h.removeFromLeft (10);
    addToggle (detOnBtn, pid (gate.det.pOn), h.removeFromLeft (80), "Switch the detection filter on.");
    addButton (detSoloBtn, h.removeFromLeft (50), "Hear the detection signal (what the gate listens to).", [this] (const ModifierKeys&) { gate.detSolo.store (! gate.detSolo.load()); });
    h.removeFromLeft (10);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (gate.mb); });
    setFont ({ &detOnBtn, &detSoloBtn, &learnBtn }, 10.0f);

    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.rangeDb = 48.0f;
    for (int b = 0; b < 3; ++b) trace.lanes.push_back ({ colours::band (b), {} });
    trace.onDragThreshold = [this] (int which, float db)
    {
        if (which == 0) { setp (gate.band[selected].open, db); if (pv (gate.band[selected].close) > db) setp (gate.band[selected].close, db); }
        else setp (gate.band[selected].close, jmin (db, pv (gate.band[selected].open)));
    };
    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;
    xover.onSelect = [this] (int b) { selectBand (b); };
    addAndMakeVisible (detView);
    detView.setBounds (kViewArea);
    detView.source = &spectrum;

    auto c = kControlArea;
    addAndMakeVisible (bandBtns);
    bandBtns.setBounds (c.getX(), c.getY() + 14, 120, 30);
    bandBtns.onSelect = [this] (int b) { selectBand (b); };
    auto row = Rect (c.getX() + 150, c.getY() + 10, 900, 210);
    addFader (openFader, pid (gate.band[0].open), "OPEN", row.removeFromLeft (56), "OPEN THRESHOLD: above it the gate opens and lets the signal through.");
    addAndMakeVisible (meter);
    meter.setBounds (row.removeFromLeft (30).withTrimmedBottom (16).reduced (3, 8));
    meter.floorDb = -80.0f;
    addFader (closeFader, pid (gate.band[0].close), "CLOSE", row.removeFromLeft (56), "CLOSE THRESHOLD: below it the gate closes. Set under OPEN (hysteresis) so a signal near the threshold doesn't chatter.");
    row.removeFromLeft (24);
    auto col = row.removeFromLeft (110);
    addReadout (ratioRo, pid (gate.band[0].ratio), "RATIO", col.removeFromTop (54), "How hard it turns down what falls below: 100:1 is a gate, low ratios a gentle expander.");
    col.removeFromTop (12);
    addReadout (attackRo, pid (gate.band[0].attack), "ATTACK", col.removeFromTop (54), "How fast the gate opens once the signal passes OPEN.");
    row.removeFromLeft (12);
    col = row.removeFromLeft (110);
    addReadout (holdRo, pid (gate.band[0].hold), "HOLD", col.removeFromTop (54), "How long it stays open after the signal drops below CLOSE.");
    col.removeFromTop (12);
    addReadout (releaseRo, pid (gate.band[0].release), "RELEASE", col.removeFromTop (54), "How fast it closes.");
    row.removeFromLeft (24);
    col = row.removeFromLeft (220);
    addChoice (scChoice, pid (gate.band[0].sidechain), "SIDECHAIN", col.removeFromTop (52), "What opens this band: itself, the full signal, another band, or the external sidechain (full or through a band).");
    col.removeFromTop (30);
    addAndMakeVisible (grMeter);
    grMeter.setBounds (col.removeFromTop (20));
    grMeter.rangeMin = -60.0f; grMeter.rangeMax = 0.0f; grMeter.bipolar = false;
    selectBand (0);
}

void GatePanel::selectBand (int b)
{
    selected = jlimit (0, gate.mb.numBands (gate) - 1, b);
    xover.selectedBand = selected;
    const auto& bi = gate.band[selected];
    rebind (openFader, pid (bi.open)); rebind (closeFader, pid (bi.close));
    rebind (ratioRo, pid (bi.ratio)); rebind (attackRo, pid (bi.attack)); rebind (holdRo, pid (bi.hold)); rebind (releaseRo, pid (bi.release));
    rebindChoice (scChoice, pid (bi.sidechain));
    openFader.fill = closeFader.fill = colours::band (selected);
    meter.colour = grMeter.colour = colours::band (selected);
    trace.highlight = selected;
    repaint();
}

void GatePanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 3; ++i) viewBtn[i].setLit (view == i);
    trace.setVisible (view == 0); xover.setVisible (view == 1); detView.setVisible (view == 2);
    showAll (view == 2, detOnBtn, detSoloBtn);
    detSoloBtn.setLit (gate.detSolo.load());
    const int nb = gate.mb.numBands (gate);
    if (selected >= nb) selectBand (nb - 1);
    bandBtns.update (selected, nb);
    const auto& t = gate.tm[selected];
    meter.inputDb = t.levelDb; meter.gainDb = t.gainDb; meter.repaint();
    grMeter.set (t.minGainDb);
    trace.thresholdDb = { pv (gate.band[selected].open), pv (gate.band[selected].close) };
    if (view == 0)
    {
        const float lanes[3] { gate.tm[0].minGainDb, nb > 1 ? gate.tm[1].minGainDb : 0.0f, nb > 2 ? gate.tm[2].minGainDb : 0.0f };
        trace.push (proc.chain.moduleInTap.takePeak(), proc.chain.moduleTap.takePeak(), lanes);
    }
    xover.repaint(); detView.repaint();
    statusLine = "Band " + String (selected + 1) + (t.open ? ": open" : ": closed") + (t.gainDb < -0.5f ? ", " + ids::decibels (t.gainDb) + " of reduction." : ".")
               + "  Level " + String (t.levelDb, 1) + " dB.";
}

void GatePanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (140).toFloat(), "BAND " + String (selected + 1), colours::band (selected));
    drawText (g, "GAIN REDUCTION", { (float) kControlArea.getX() + 540.0f + 150.0f, (float) kControlArea.getY() + 74.0f, 220.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
}

// ===========================================================================
// CompressorPanel
// ===========================================================================
CompressorPanel::CompressorPanel (PlanishProcessor& p, int moduleId, SpectrumSource& s)
    : ModulePanel (p, moduleId, s), comp (p.chain.compressor (moduleId == ModCompressor1 ? 1 : 2)),
      xover (*this, comp.mb, comp.soloBand), detView (*this, comp.det)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader();
    for (int i = 0; i < 4; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (i == 3 ? 58 : 80), i == 0 ? "The waveform (input dark, output light, sidechain purple) with each band's gain reduction."
                   : i == 1 ? "Up to three bands over the spectrum." : i == 2 ? "The filter in front of the detector (Modern and Vintage)." : "One bar of the output, synced to Live's tempo, with the sidechain.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (9.5f);
    }
    h.removeFromLeft (8);
    addChoice (modeChoice, pid (comp.pMode), {}, h.removeFromLeft (96), "PUNCH: one AMOUNT for the hits, no threshold. MODERN: clean and transparent. VINTAGE: livelier and coloured, a feedback design.");
    modeChoice.fontH = 12.0f;
    h.removeFromLeft (6);
    addChoice (detectChoice, pid (comp.pDetect), {}, h.removeFromLeft (70), "LEVEL DETECTION: RMS averages (raise level without changing character), PEAK catches transients, TRUE averages and reads every frequency alike.");
    detectChoice.fontH = 12.0f;
    h.removeFromLeft (6);
    addToggle (autoGainBtn, pid (comp.pAutoGain), h.removeFromLeft (72), "AUTO GAIN: make up the level the compression takes away.");
    addToggle (detOnBtn, pid (comp.det.pOn), h.removeFromLeft (70), "Switch the detection filter on.");
    addButton (detSoloBtn, h.removeFromLeft (40), "Hear what the detector hears.", [this] (const ModifierKeys&) { comp.detSolo.store (! comp.detSolo.load()); });
    addButton (learnBtn, h.removeFromLeft (52), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (comp.mb); });
    setFont ({ &autoGainBtn, &detOnBtn, &detSoloBtn, &learnBtn }, 9.5f);

    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    for (int b = 0; b < 3; ++b) trace.lanes.push_back ({ colours::band (b), {} });
    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;
    xover.showGainScale = true; xover.gainTop = 6.0f; xover.gainBottom = -24.0f;
    xover.onSelect = [this] (int b) { selectBand (b); };
    xover.bandLineDb = [this] (int b) { return comp.tm[b].gainDb; };
    addAndMakeVisible (detView);
    detView.setBounds (kViewArea);
    detView.source = &spectrum;
    addAndMakeVisible (scope);
    scope.setBounds (kViewArea);
    scope.recorder = &comp.scope;
    scope.ppqNow = [this]
    {
        const int pos = comp.scope.position();
        return pos > 0 ? comp.scope.at (pos - 1).ppq : 0.0;
    };

    auto c = kControlArea;
    addAndMakeVisible (bandBtns);
    bandBtns.setBounds (c.getX(), c.getY() + 14, 120, 30);
    bandBtns.onSelect = [this] (int b) { selectBand (b); };
    auto row = Rect (c.getX() + 150, c.getY() + 10, 910, 212);
    addFader (thresholdFader, pid (comp.band[0].threshold), "THRESHOLD", row.removeFromLeft (60), "Where compression starts (Modern and Vintage).");
    addAndMakeVisible (meter);
    meter.setBounds (row.removeFromLeft (30).withTrimmedBottom (16).reduced (3, 8));
    addAndMakeVisible (amountKnob);
    addKnob (amountKnob, pid (comp.band[0].amount), "AMOUNT", Rect (c.getX() + 150, c.getY() + 16, 130, 150),
             "PUNCH AMOUNT: under 100% the hits are pulled in (consistent, compressed); over 100% they are pushed out (punchy).", false);
    amountKnob.big = true;
    row.removeFromLeft (16);
    auto col = row.removeFromLeft (100);
    addReadout (ratioRo, pid (comp.band[0].ratio), "RATIO", col.removeFromTop (50), "How much gain reduction past the threshold. 10:1 and up limits; under 1:1 expands.");
    col.removeFromTop (8);
    addReadout (kneeRo, pid (comp.band[0].knee), "KNEE", col.removeFromTop (50), "Soft knee: compression eases in around the threshold (Modern only).");
    row.removeFromLeft (10);
    col = row.removeFromLeft (100);
    addReadout (attackRo, pid (comp.band[0].attack), "ATTACK", col.removeFromTop (50), "Time to reach full compression past the threshold.");
    col.removeFromTop (8);
    addReadout (releaseRo, pid (comp.band[0].release), "RELEASE", col.removeFromTop (50), "Time to recover.");
    addButton (autoRelBtn, Rect (col.getX(), col.getY() + 6, 100, 26), "AUTO RELEASE (Modern): scales the release with the material to avoid pumping.",
               [this] (const ModifierKeys&) { const auto& bi = comp.band[selected]; setp (bi.autoRelease, pv (bi.autoRelease) > 0.5f ? 0.0f : 1.0f); });
    autoRelBtn.setFontHeight (10.0f);
    addReadout (punchAttackRo, pid (comp.band[0].punchAttack), "ATTACK", Rect (c.getX() + 300, c.getY() + 16, 100, 50), "PUNCH ATTACK: the rise time of the processing.");
    addReadout (punchSustainRo, pid (comp.band[0].punchSustain), "SUSTAIN", Rect (c.getX() + 300, c.getY() + 74, 100, 50), "PUNCH SUSTAIN: the fall time.");
    row.removeFromLeft (14);
    addAndMakeVisible (curveView);
    curveView.setBounds (row.removeFromLeft (190).withTrimmedBottom (16));
    curveView.curve = [this] (float in) { return comp.staticGainDb (selected, in); };
    row.removeFromLeft (14);
    addFader (makeupFader, pid (comp.band[0].makeup), "MAKEUP", row.removeFromLeft (56), "Gain after compression.");
    makeupFader.bipolar = true;
    addFader (mixFader, pid (comp.band[0].mix), "MIX", row.removeFromLeft (56), "This band's dry/wet: blend the compressed band with the original (parallel compression).");
    row.removeFromLeft (14);
    col = row.removeFromLeft (180);
    addChoice (scChoice, pid (comp.band[0].sidechain), "SIDECHAIN", col.removeFromTop (52),
               "What drives this band: itself, the full signal, another band, or the external sidechain from Live (full or through a band). Choosing an external one sets fast, peak, 4:1 starting values.");
    col.removeFromTop (26);
    addAndMakeVisible (grMeter);
    grMeter.setBounds (col.removeFromTop (20));
    grMeter.rangeMin = -24.0f; grMeter.rangeMax = 12.0f;
    selectBand (0);
}

void CompressorPanel::selectBand (int b)
{
    selected = jlimit (0, comp.mb.numBands (comp) - 1, b);
    xover.selectedBand = selected;
    bindBand();
}

void CompressorPanel::bindBand()
{
    const auto& bi = comp.band[selected];
    rebind (thresholdFader, pid (bi.threshold)); rebind (ratioRo, pid (bi.ratio)); rebind (kneeRo, pid (bi.knee));
    rebind (attackRo, pid (bi.attack)); rebind (releaseRo, pid (bi.release)); rebind (makeupFader, pid (bi.makeup)); rebind (mixFader, pid (bi.mix));
    rebind (amountKnob, pid (bi.amount)); rebind (punchAttackRo, pid (bi.punchAttack)); rebind (punchSustainRo, pid (bi.punchSustain));
    rebindChoice (scChoice, pid (bi.sidechain));
    lastScChoice[selected] = (int) pv (bi.sidechain);
    const Colour col = colours::band (selected);
    thresholdFader.fill = makeupFader.fill = mixFader.fill = col;
    meter.colour = curveView.colour = grMeter.colour = col;
    amountKnob.accent = col;
    trace.highlight = selected;
    repaint();
}

void CompressorPanel::refresh()
{
    ModulePanel::refresh();
    for (int i = 0; i < 4; ++i) viewBtn[i].setLit (view == i);
    const int mode = (int) pv (comp.pMode);
    if (view == 2 && mode == Compressor::Punch) view = 0;
    viewBtn[2].setEnabled (mode != Compressor::Punch);
    trace.setVisible (view == 0); xover.setVisible (view == 1); detView.setVisible (view == 2); scope.setVisible (view == 3);
    showAll (view == 2, detOnBtn, detSoloBtn);
    detSoloBtn.setLit (comp.detSolo.load());
    const int nb = comp.mb.numBands (comp);
    if (selected >= nb) selectBand (nb - 1);
    bandBtns.update (selected, nb);
    learnBtn.setEnabled (nb > 1);

    const bool punch = mode == Compressor::Punch;
    showAll (! punch, thresholdFader, meter, ratioRo, kneeRo, attackRo, releaseRo, curveView);
    autoRelBtn.setVisible (mode == Compressor::Modern);
    autoRelBtn.setLit (pv (comp.band[selected].autoRelease) > 0.5f);
    kneeRo.setEnabled (mode == Compressor::Modern);
    showAll (punch, amountKnob, punchAttackRo, punchSustainRo);
    thresholdFader.setEnabled (! punch);

    // Choosing an external sidechain sets the manual's starting values.
    const int sc = (int) pv (comp.band[selected].sidechain);
    if (sc != lastScChoice[selected])
    {
        const auto was = decodeSidechain (lastScChoice[selected], nb, selected), now = decodeSidechain (sc, nb, selected);
        lastScChoice[selected] = sc;
        if (now.external && ! was.external)
        {
            setp (comp.band[selected].attack, 1.0f); setp (comp.band[selected].release, 80.0f);
            setp (comp.band[selected].ratio, 4.0f); setp (comp.band[selected].threshold, -30.0f);
            setp (comp.pAutoGain, 0.0f); setp (comp.pDetect, (float) DetectPeak);
        }
    }

    const auto& t = comp.tm[selected];
    meter.inputDb = t.levelDb; meter.gainDb = t.gainDb; meter.repaint();
    curveView.inputDb = t.levelDb; curveView.gainDb = t.gainDb; curveView.repaint();
    grMeter.set (t.minGainDb);
    trace.showSidechain = comp.sidechainActive;
    if (view == 0)
    {
        const float lanes[3] { comp.tm[0].minGainDb, nb > 1 ? comp.tm[1].minGainDb : 0.0f, nb > 2 ? comp.tm[2].minGainDb : 0.0f };
        trace.push (proc.chain.moduleInTap.takePeak(), proc.chain.moduleTap.takePeak(), lanes, proc.chain.sidechainTap.takePeak());
    }
    if (view == 3) scope.repaint();
    xover.repaint(); detView.repaint();
    String scNote;
    if (comp.sidechainActive && ! proc.chain.sidechainPresent()) scNote = "   No sidechain is coming in: pick a source in Live's sidechain menu for this device.";
    statusLine = "Band " + String (selected + 1) + ": " + (t.gainDb < -0.05f ? ids::decibels (t.gainDb) + " now" : t.gainDb > 0.05f ? ids::decibels (t.gainDb) + " of boost now" : "no gain change now")
               + (pv (comp.pAutoGain) > 0.5f ? ", auto gain " + ids::decibels (t.autoDb) : "") + "." + scNote;
}

void CompressorPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawSectionLocal (g, Rect (kControlArea).withTrimmedLeft (140).toFloat(), "BAND " + String (selected + 1), colours::band (selected));
    drawText (g, "GAIN REDUCTION", { (float) kControlArea.getX() + 912.0f - 20.0f, (float) kControlArea.getY() + 74.0f, 180.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
}

// ===========================================================================
// TransientPanel
// ===========================================================================
TransientPanel::TransientPanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModTransientShaper, s), ts (p.chain.transient), xover (*this, ts.mb, ts.soloBand)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (ts.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int i) { viewSet = i; bindSet(); };
    sets.onSolo = [this] (int i) { ts.soloSet.store (i); };
    h.removeFromLeft (8);
    addChoice (envChoice, pid (ts.pEnvMode), {}, h.removeFromLeft (110), "ENVELOPE: PRECISE recovers fastest between hits, BALANCED is the middle, LOOSE recovers slowly and adds the most sustain.");
    envChoice.fontH = 12.0f;
    h.removeFromLeft (8);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (84), i == 0 ? "The waveform with each band's gain change." : "Up to three bands over the spectrum.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (ts.mb); });
    learnBtn.setFontHeight (10.0f);

    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.bipolar = true;
    trace.rangeDb = 15.0f;
    for (int b = 0; b < 3; ++b) trace.lanes.push_back ({ colours::band (b), {} });
    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;
    xover.showGainScale = true; xover.gainTop = 15.0f; xover.gainBottom = -15.0f;
    xover.bandLineDb = [this] (int b) { return ts.peakGainDb[(int) pv (ts.pChMode) == 0 ? 0 : viewSet][b]; };

    for (int b = 0; b < 3; ++b)
    {
        const int x = kControlArea.getX() + 10 + b * 356;
        const int y = kControlArea.getY() + 16;
        addKnob (attackKnob[b], pid (ts.band[0][b].attack), "ATTACK", Rect (x, y, 96, 112), "ATTACK: up for an exaggerated, punchy hit; down for a softer, warmer one.", false);
        addKnob (sustainKnob[b], pid (ts.band[0][b].sustain), "SUSTAIN", Rect (x + 104, y, 96, 112), "SUSTAIN: up for a denser, airier tail; down for a tighter, more aggressive sound.", false);
        attackKnob[b].bipolar = sustainKnob[b].bipolar = true;
        attackKnob[b].accent = sustainKnob[b].accent = colours::band (b);
        const char* names[3] { "SHARP", "MEDIUM", "SMOOTH" };
        const char* tips[3] { "SHARP: fast, tight release, for short, staccato sounds.", "MEDIUM: transparent, for most material.", "SMOOTH: the slowest; lets more of the attack into the sustain, for sustained sounds." };
        for (int k = 0; k < 3; ++k)
        {
            contourBtn[b][k].setLabel (names[k]);
            contourBtn[b][k].setFontHeight (9.5f);
            contourBtn[b][k].setLitColour (colours::band (b));
            addButton (contourBtn[b][k], Rect (x + 214, y + 8 + k * 34, 86, 28), tips[k], [this, b, k] (const ModifierKeys&) { setp (ts.band[viewSet][b].contour, (float) k); });
        }
        addAndMakeVisible (gainMeter[b]);
        gainMeter[b].setBounds (x, y + 140, 300, 18);
        gainMeter[b].rangeMin = -15.0f; gainMeter[b].rangeMax = 15.0f;
        gainMeter[b].colour = colours::band (b);
    }
    bindSet();
}

void TransientPanel::bindSet()
{
    for (int b = 0; b < 3; ++b)
    {
        rebind (attackKnob[b], pid (ts.band[viewSet][b].attack));
        rebind (sustainKnob[b], pid (ts.band[viewSet][b].sustain));
    }
    repaint();
}

void TransientPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = ts.channelMode();
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindSet(); }
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    trace.setVisible (view == 0); xover.setVisible (view == 1);
    const int nb = ts.mb.numBands (ts);
    const int g = mode == Stereo ? 0 : viewSet;
    for (int b = 0; b < 3; ++b)
    {
        const bool shown = b < nb;
        showAll (shown, attackKnob[b], sustainKnob[b], gainMeter[b]);
        for (int k = 0; k < 3; ++k)
        {
            contourBtn[b][k].setVisible (shown);
            contourBtn[b][k].setLit ((int) pv (ts.band[viewSet][b].contour) == k);
        }
        gainMeter[b].set (ts.peakGainDb[g][b]);
    }
    if (view == 0)
    {
        const float lanes[3] { ts.peakGainDb[g][0], nb > 1 ? ts.peakGainDb[g][1] : 0.0f, nb > 2 ? ts.peakGainDb[g][2] : 0.0f };
        trace.push (proc.chain.moduleInTap.takePeak(), proc.chain.moduleTap.takePeak(), lanes);
    }
    xover.repaint();
    statusLine = "The meters show the gain each band applies now: up on the attacks, then up or down on the sustain.";
}

void TransientPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    const int nb = ts.mb.numBands (ts);
    for (int b = 0; b < nb; ++b)
        drawSectionLocal (g, Rect (kControlArea.getX() + b * 356, kControlArea.getY(), 340, 186).toFloat(), "BAND " + String (b + 1), colours::band (b));
}

// ===========================================================================
// ExciterPanel
// ===========================================================================
Point<float> ExciterPanel::PostView::postNode() const
{
    const float hz = owner.pv (owner.exc.pPostFreq), db = owner.pv (owner.exc.pPostGain);
    return { xForHz (hz), yForGain (db) };
}

void ExciterPanel::PostView::paintOverlay (Graphics& g)
{
    CrossoverView::paintOverlay (g);
    // Harmonic activity per band.
    const auto r = plotArea();
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
    {
        const float x0 = b == 0 ? r.getX() : xForHz (xoverHz (b - 1));
        const float x1 = b == nb - 1 ? r.getRight() : xForHz (xoverHz (b));
        g.setColour (colours::band (b).withAlpha (0.35f * owner.exc.harmonicLevel[b]));
        g.fillRect (Rectangle<float> (x0, r.getY() + 40.0f, x1 - x0, r.getHeight() - 120.0f));
    }
    // The post filter: a high shelf on the wet signal.
    Biquad shelf;
    const double sr = owner.proc.getSampleRate() > 0 ? owner.proc.getSampleRate() : 48000.0;
    shelf.highShelf (owner.pv (owner.exc.pPostFreq), owner.pv (owner.exc.pPostGain), sr);
    drawCurve (g, [&] (float hz) { return (float) shelf.responseDb (hz, sr); }, colours::lcd, 1.6f, false);
    const auto p = postNode();
    g.setColour (colours::lcd);
    g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (p));
    g.setColour (colours::black);
    g.drawEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (p), 1.5f);
    drawText (g, "POST FILTER", { p.x - 50.0f, p.y + 9.0f, 100.0f, 12.0f }, 9.0f, colours::lcd, true);
}

void ExciterPanel::PostView::mouseDown (const MouseEvent& e)
{
    draggingPost = postNode().getDistanceFrom (e.position) < 12.0f;
    if (! draggingPost) CrossoverView::mouseDown (e);
}

void ExciterPanel::PostView::mouseDrag (const MouseEvent& e)
{
    if (! draggingPost) { CrossoverView::mouseDrag (e); return; }
    owner.setp (owner.exc.pPostFreq, jlimit (1000.0f, 20000.0f, hzForX (e.position.x)));
    owner.setp (owner.exc.pPostGain, jlimit (-12.0f, 0.0f, gainForY (e.position.y)));
}

void ExciterPanel::PostView::mouseUp (const MouseEvent& e)
{
    draggingPost = false;
    CrossoverView::mouseUp (e);
}

ExciterPanel::ExciterPanel (PlanishProcessor& p, SpectrumSource& out, SpectrumSource& in)
    : ModulePanel (p, ModExciter, out), exc (p.chain.exciter), view (*this, exc.mb, exc.soloBand)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader();
    addToggle (tameBtn, pid (exc.pTame), h.removeFromLeft (64), "TAME: distortion applied evenly whatever the level, and the original dynamics put back. For parts with soft and loud passages.");
    addToggle (osBtn, pid (exc.pOversample), h.removeFromLeft (110), "OVERSAMPLING: runs the distortion at 4x the sample rate so new harmonics don't alias. 23 samples of latency.");
    h.removeFromLeft (8);
    addChoice (emphasisChoice, pid (exc.pEmphasis), {}, h.removeFromLeft (100), "PRE-EMPHASIS: FULL leans the saturation on the low mids (body), DEFINED on the high mids (presence), CLEAR away from the low mids, FLAT not at all.");
    emphasisChoice.fontH = 12.0f;
    h.removeFromLeft (8);
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (exc.mb); });
    setFont ({ &tameBtn, &osBtn, &learnBtn }, 10.0f);

    addAndMakeVisible (view);
    view.setBounds (kViewArea);
    view.source = &spectrum;
    view.secondary = &in;
    view.secondaryColour = colours::spectrum.darker (0.4f);
    view.showGainScale = true; view.gainTop = 6.0f; view.gainBottom = -18.0f;

    for (int b = 0; b < 3; ++b)
    {
        const int x = kControlArea.getX() + 6 + b * 300;
        const int y = kControlArea.getY() + 14;
        addKnob (driveKnob[b], pid (exc.band[b].drive), "DRIVE", Rect (x, y, 80, 96), "How much excitation.", false);
        driveKnob[b].accent = colours::band (b);
        classicBtn[b].setLabel ("CLASSIC"); trashBtn[b].setLabel ("TRASH");
        classicBtn[b].setFontHeight (9.5f); trashBtn[b].setFontHeight (9.5f);
        addButton (classicBtn[b], Rect (x, y + 108, 80, 26), "CLASSIC: Retro (edgy fuzz), Tape (bright odd harmonics), Tube (clear, on the attacks), Warm (even harmonics).", [this, b] (const ModifierKeys&) { setp (exc.band[b].family, 0.0f); });
        addButton (trashBtn[b], Rect (x, y + 138, 80, 26), "TRASH: Overdrive, Scream, Clipper and Scratch, for dramatic distortion.", [this, b] (const ModifierKeys&) { setp (exc.band[b].family, 1.0f); });
        addAndMakeVisible (pad[b]);
        pad[b].setBounds (x + 88, y, 140, 164);
        pad[b].colour = colours::band (b);
        pad[b].get = [this, b] { return std::make_pair (pv (exc.band[b].x), pv (exc.band[b].y)); };
        pad[b].set = [this, b] (float px, float py) { setp (exc.band[b].x, px); setp (exc.band[b].y, py); };
        pad[b].setTooltip ("Blend the four harmonic profiles: drag the dot toward a corner. Double-click for the middle.");
        addFader (mixFader[b], pid (exc.band[b].mix), "MIX", Rect (x + 234, y, 46, 180), "How much of the excited signal is blended in. Lower for a more integrated effect.");
        mixFader[b].fill = colours::band (b);
    }
    addKnob (toneKnob, pid (exc.pTone), "TONE", Rect (kControlArea.getRight() - 160, kControlArea.getY() + 14, 150, 160),
             "TONE: below 0 the distortion leans on the lows, above 0 on the highs. It shapes the whole wet signal.", false);
    toneKnob.bipolar = true;
    toneKnob.big = true;
}

void ExciterPanel::refresh()
{
    ModulePanel::refresh();
    const int nb = exc.mb.numBands (exc);
    for (int b = 0; b < 3; ++b)
    {
        const bool shown = b < nb;
        showAll (shown, driveKnob[b], classicBtn[b], trashBtn[b], pad[b], mixFader[b]);
        const bool trash = (int) pv (exc.band[b].family) == Exciter::Trash;
        classicBtn[b].setLit (! trash);
        trashBtn[b].setLit (trash);
        const int base = trash ? 4 : 0;
        pad[b].corners = { Exciter::shapeName (base), Exciter::shapeName (base + 1), Exciter::shapeName (base + 2), Exciter::shapeName (base + 3) };
        pad[b].repaint();
    }
    view.repaint();
    statusLine = "The light area over each band shows the harmonics it adds. The lit node is the post filter: drag it to tame the top of the excited signal.";
}

void ExciterPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    const int nb = exc.mb.numBands (exc);
    for (int b = 0; b < nb; ++b)
        drawSectionLocal (g, Rect (kControlArea.getX() + b * 300, kControlArea.getY(), 290, 208).toFloat(), "BAND " + String (b + 1), colours::band (b));
}
} // namespace planish::ui
