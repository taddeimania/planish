#include "ModulePanels.h"

namespace planish::ui
{
using namespace juce;
using Rect = Rectangle<int>;

// ===========================================================================
// SculptorPanel
// ===========================================================================
SculptorPanel::SculptorPanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModSculptor, s), scu (p.chain.sculptor), diff (*this)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (scu.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int i) { viewSet = i; bindSet(); };
    sets.onSolo = [this] (int i) { scu.soloSet.store (i); };

    addAndMakeVisible (diff);
    diff.setBounds (kViewArea);
    diff.source = &spectrum;
    diff.colour = juce::Colour (scu.info().colour);
    diff.targetDb = [this] (float hz) { return scu.targetDb (viewSet, hz); };

    auto c = kControlArea;
    addChoice (targetChoice, pid (scu.pTarget), "TARGET", Rect (c.getX() + 20, c.getY() + 30, 240, 64),
               "The curve Sculptor shapes toward: an idealised tonal balance for each kind of source, or CUSTOM (from the assistant's reference).");
    targetChoice.fontH = 16.0f;
    addButton (soloBtn, Rect (c.getX() + 20, c.getY() + 120, 140, 36), "Hear only what Sculptor processes: the range between the handles.", [this] (const ModifierKeys&) { scu.soloRange.store (! scu.soloRange.load()); });
    soloBtn.setFontHeight (11.0f);
    addKnob (amountKnob, pid (scu.sp[0].amount), "AMOUNT", Rect (c.getX() + 330, c.getY() + 10, 150, 170), "How far toward the target.", false);
    addKnob (toneKnob, pid (scu.sp[0].tone), "TONE", Rect (c.getX() + 520, c.getY() + 10, 150, 170), "Tilts the target: down for a darker result, up for a brighter one.", false);
    addKnob (speedKnob, pid (scu.sp[0].speed), "SPEED", Rect (c.getX() + 710, c.getY() + 10, 150, 170), "How fast the shaping follows the music: slow is like an EQ, fast like a dynamic EQ on every band.", false);
    amountKnob.big = toneKnob.big = speedKnob.big = true;
    toneKnob.bipolar = true;
    for (auto* k : { &amountKnob, &toneKnob, &speedKnob }) k->accent = juce::Colour (scu.info().colour);
    bindSet();
}

void SculptorPanel::bindSet()
{
    rebind (amountKnob, pid (scu.sp[viewSet].amount));
    rebind (toneKnob, pid (scu.sp[viewSet].tone));
    rebind (speedKnob, pid (scu.sp[viewSet].speed));
    diff.lowParam = scu.range[viewSet].lo;
    diff.highParam = scu.range[viewSet].hi;
    repaint();
}

void SculptorPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = scu.channelMode();
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindSet(); }
    const int g = mode == Stereo ? 0 : viewSet;
    diff.gainDb = scu.gainDb[g];
    diff.specDb = scu.specDb[g];
    soloBtn.setLit (scu.soloRange.load());
    diff.repaint();
    float lo = 0.0f, hi = 0.0f;
    for (int b = 0; b < SpectralBands::kCount; ++b) { lo = jmin (lo, scu.gainDb[g][b]); hi = jmax (hi, scu.gainDb[g][b]); }
    if (proc.getParam (ids::zeroLatency) > 0.5f)
        statusLine = "ZERO LATENCY is on: Sculptor is switched off.";
    else
        statusLine = "Shaping toward " + targetChoice.getText().toLowerCase() + ": up to " + ids::decibels (hi) + " and " + ids::decibels (lo)
                   + ". The pale band is the target; drag the handles to limit the range.";
}

// ===========================================================================
// UnmaskPanel
// ===========================================================================
UnmaskPanel::UnmaskPanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModUnmask, s), unm (p.chain.unmask), diff (*this)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (unm.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int i) { viewSet = i; bindSet(); };
    sets.onSolo = [this] (int i) { unm.soloSet.store (i); };

    addAndMakeVisible (diff);
    diff.setBounds (kViewArea);
    diff.source = &spectrum;
    diff.colour = juce::Colour (unm.info().colour);
    diff.scDb = unm.scSpecDb;

    auto c = kControlArea;
    addButton (soloBtn, Rect (c.getX() + 20, c.getY() + 120, 140, 36), "Hear only what Unmask processes: the range between the handles.", [this] (const ModifierKeys&) { unm.soloRange.store (! unm.soloRange.load()); });
    soloBtn.setFontHeight (11.0f);
    addKnob (amountKnob, pid (unm.sp[0].amount), "AMOUNT", Rect (c.getX() + 200, c.getY() + 10, 150, 170), "How deep the cuts go where this track masks the sidechain.", false);
    addKnob (sensKnob, pid (unm.sp[0].sensitivity), "SENSITIVITY", Rect (c.getX() + 380, c.getY() + 10, 150, 170), "How much masking counts: higher acts on smaller overlaps. Set it with AMOUNT.", false);
    addKnob (attackKnob, pid (unm.sp[0].attack), "ATTACK", Rect (c.getX() + 560, c.getY() + 10, 150, 170), "How fast the cuts come in when masking starts.", false);
    addKnob (releaseKnob, pid (unm.sp[0].release), "RELEASE", Rect (c.getX() + 740, c.getY() + 10, 150, 170), "How fast they let go when it stops.", false);
    for (auto* k : { &amountKnob, &sensKnob, &attackKnob, &releaseKnob }) { k->big = true; k->accent = juce::Colour (unm.info().colour); }
    bindSet();
}

void UnmaskPanel::bindSet()
{
    rebind (amountKnob, pid (unm.sp[viewSet].amount));
    rebind (sensKnob, pid (unm.sp[viewSet].sensitivity));
    rebind (attackKnob, pid (unm.sp[viewSet].attack));
    rebind (releaseKnob, pid (unm.sp[viewSet].release));
    diff.lowParam = unm.range[viewSet].lo;
    diff.highParam = unm.range[viewSet].hi;
    repaint();
}

void UnmaskPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = unm.channelMode();
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindSet(); }
    const int g = mode == Stereo ? 0 : viewSet;
    diff.gainDb = unm.gainDb[g];
    diff.specDb = unm.specDb[g];
    soloBtn.setLit (unm.soloRange.load());
    diff.repaint();
    float lo = 0.0f;
    for (int b = 0; b < SpectralBands::kCount; ++b) lo = jmin (lo, unm.gainDb[g][b]);
    if (proc.getParam (ids::zeroLatency) > 0.5f)
        statusLine = "ZERO LATENCY is on: Unmask is switched off.";
    else if (! proc.chain.sidechainPresent() || ! unm.sidechainPresent())
        statusLine = "Unmask needs a sidechain: in Live, open this device's sidechain menu and pick the track to make room for (a vocal, a kick).";
    else
        statusLine = "Making room for the sidechain (purple): up to " + ids::decibels (lo) + " where this track hides it.";
}

void UnmaskPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawText (g, "SIDECHAIN", { (float) kControlArea.getX() + 20.0f, (float) kControlArea.getY() + 34.0f, 150.0f, 14.0f }, 11.0f, colours::sidechain, true, Justification::centredLeft);
    drawText (g, proc.chain.sidechainPresent() && unm.sidechainPresent() ? "RECEIVING" : "NONE",
              { (float) kControlArea.getX() + 20.0f, (float) kControlArea.getY() + 52.0f, 150.0f, 30.0f }, 22.0f,
              proc.chain.sidechainPresent() && unm.sidechainPresent() ? colours::lcd : colours::inkDim, true, Justification::centredLeft);
}

// ===========================================================================
// ClipperPanel
// ===========================================================================
ClipperPanel::ClipperPanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModClipper, s), clp (p.chain.clipper), xover (*this, clp.mb, clp.soloBand)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (clp.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int i) { viewSet = i; bindSet(); };
    sets.onSolo = [this] (int i) { clp.soloSet.store (i); };
    h.removeFromLeft (8);
    addToggle (osBtn, pid (clp.pOversample), h.removeFromLeft (110), "OVERSAMPLING: clips at 4x the sample rate, so the new harmonics don't alias. 23 samples of latency.");
    h.removeFromLeft (8);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (84), i == 0 ? "The waveform with each band's gain: up from the boost, down from the clipping." : "Up to three bands over the spectrum.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (clp.mb); });
    osBtn.setFontHeight (10.0f); learnBtn.setFontHeight (10.0f);

    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.bipolar = true;
    trace.rangeDb = 12.0f;
    for (int b = 0; b < 3; ++b) trace.lanes.push_back ({ colours::band (b), {} });
    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;

    for (int b = 0; b < 3; ++b)
    {
        const int x = kControlArea.getX() + 10 + b * 356;
        const int y = kControlArea.getY() + 16;
        addKnob (boostKnob[b], pid (clp.band[0][b].boost), "SOFT CLIP BOOST", Rect (x, y, 110, 140), "Boost into the clipper's soft knee: more level and more power, the knee widening as you go.", false);
        boostKnob[b].accent = colours::band (b);
        addFader (thresholdFader[b], pid (clp.band[0][b].threshold), "THRESHOLD", Rect (x + 118, y, 56, 176), "Where the signal is hard clipped.");
        thresholdFader[b].fill = colours::band (b);
        addAndMakeVisible (curve[b]);
        curve[b].setBounds (x + 184, y + 4, 150, 150);
        curve[b].colour = colours::band (b);
        curve[b].rangeDb = 36.0f;
    }
    bindSet();
}

void ClipperPanel::bindSet()
{
    for (int b = 0; b < 3; ++b)
    {
        rebind (boostKnob[b], pid (clp.band[viewSet][b].boost));
        rebind (thresholdFader[b], pid (clp.band[viewSet][b].threshold));
        curve[b].curve = [this, b] (float in)
        {
            const float x = dbToGain (in);
            const float y = Clipper::clip (x, pv (clp.band[viewSet][b].boost), pv (clp.band[viewSet][b].threshold));
            return gainToDb (std::max (std::fabs (y), 1.0e-6f)) - in;
        };
    }
    repaint();
}

void ClipperPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = clp.channelMode();
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindSet(); }
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    trace.setVisible (view == 0); xover.setVisible (view == 1);
    const int nb = clp.mb.numBands (clp);
    float lanes[3] {};
    for (int b = 0; b < 3; ++b)
    {
        showAll (b < nb, boostKnob[b], thresholdFader[b], curve[b]);
        lanes[b] = b < nb ? clp.gainDb (viewSet, b) : 0.0f;
        curve[b].inputDb = gainToDb (std::max (clp.peakIn[viewSet][b], 1.0e-6f));
        curve[b].gainDb = clp.gainDb (viewSet, b);
        curve[b].repaint();
    }
    if (view == 0) trace.push (proc.chain.moduleInTap.takePeak(), proc.chain.moduleTap.takePeak(), lanes);
    xover.repaint();
    statusLine = "Band 1: " + ids::decibels (lanes[0]) + " at the peaks (boost minus clipping)." + (pv (clp.pOversample) > 0.5f ? "" : "   Oversampling is off: hard clipping will alias.");
}

void ClipperPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    const int nb = clp.mb.numBands (clp);
    for (int b = 0; b < nb; ++b)
        drawSectionLocal (g, Rect (kControlArea.getX() + b * 356, kControlArea.getY(), 344, 208).toFloat(), "BAND " + String (b + 1), colours::band (b));
}

// ===========================================================================
// DensityPanel
// ===========================================================================
DensityPanel::DensityPanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModDensity, s), den (p.chain.density), xover (*this, den.mb, den.soloBand)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader (den.pChMode);
    addAndMakeVisible (sets);
    sets.setBounds (h.removeFromLeft (164));
    sets.onSelect = [this] (int i) { viewSet = i; bindSet(); };
    sets.onSolo = [this] (int i) { den.soloSet.store (i); };
    h.removeFromLeft (8);
    for (int i = 0; i < 2; ++i)
    {
        addButton (viewBtn[i], h.removeFromLeft (84), i == 0 ? "The waveform with the upward gain of each band." : "Up to three bands over the spectrum.",
                   [this, i] (const ModifierKeys&) { view = i; refresh(); });
        viewBtn[i].setFontHeight (10.0f);
    }
    addButton (learnBtn, h.removeFromLeft (60), "Place the crossovers at natural dips in what is playing.", [this] (const ModifierKeys&) { learnCrossovers (den.mb); });
    learnBtn.setFontHeight (10.0f);

    addAndMakeVisible (trace);
    trace.setBounds (kViewArea);
    trace.bipolar = true;
    trace.rangeDb = 20.0f;
    for (int b = 0; b < 3; ++b) trace.lanes.push_back ({ colours::band (b), {} });
    addAndMakeVisible (xover);
    xover.setBounds (kViewArea);
    xover.source = &spectrum;

    for (int b = 0; b < 3; ++b)
    {
        const int x = kControlArea.getX() + 10 + b * 356;
        const int y = kControlArea.getY() + 16;
        addFader (thresholdFader[b], pid (den.band[0][b].threshold), "THRESHOLD", Rect (x, y, 56, 180), "Below this level the band is lifted. Above it nothing changes.");
        thresholdFader[b].fill = colours::band (b);
        addAndMakeVisible (meter[b]);
        meter[b].setBounds (Rect (x + 58, y, 26, 180).withTrimmedBottom (16).reduced (2, 8));
        meter[b].colour = colours::band (b);
        addKnob (rangeKnob[b], pid (den.band[0][b].range), "RANGE", Rect (x + 96, y, 76, 86), "The most it will lift, 0 to 20 dB.");
        addKnob (ratioKnob[b], pid (den.band[0][b].ratio), "RATIO", Rect (x + 176, y, 76, 86), "How strongly quiet parts are lifted: 2:1 raises 1 dB for every 2 dB under the threshold.");
        addKnob (speedKnob[b], pid (den.band[0][b].speed), "SPEED", Rect (x + 136, y + 92, 76, 86), "How fast the lift follows: 1 ms is quick and aggressive, 300 ms slow and smooth.");
        for (auto* k : { &rangeKnob[b], &ratioKnob[b], &speedKnob[b] }) k->accent = colours::band (b);
    }
    bindSet();
}

void DensityPanel::bindSet()
{
    for (int b = 0; b < 3; ++b)
    {
        rebind (thresholdFader[b], pid (den.band[viewSet][b].threshold));
        rebind (rangeKnob[b], pid (den.band[viewSet][b].range));
        rebind (ratioKnob[b], pid (den.band[viewSet][b].ratio));
        rebind (speedKnob[b], pid (den.band[viewSet][b].speed));
    }
    repaint();
}

void DensityPanel::refresh()
{
    ModulePanel::refresh();
    const int mode = den.channelMode();
    sets.update (mode);
    if (mode == Stereo && viewSet != 0) { viewSet = 0; bindSet(); }
    for (int i = 0; i < 2; ++i) viewBtn[i].setLit (view == i);
    trace.setVisible (view == 0); xover.setVisible (view == 1);
    const int nb = den.mb.numBands (den);
    const int g = mode == Stereo ? 0 : viewSet;
    float lanes[3] {};
    for (int b = 0; b < 3; ++b)
    {
        showAll (b < nb, thresholdFader[b], meter[b], rangeKnob[b], ratioKnob[b], speedKnob[b]);
        lanes[b] = b < nb ? den.boostDb[g][b] : 0.0f;
        meter[b].inputDb = den.levelDb[g][b];
        meter[b].gainDb = den.boostDb[g][b];
        meter[b].repaint();
    }
    if (view == 0) trace.push (proc.chain.moduleInTap.takePeak(), proc.chain.moduleTap.takePeak(), lanes);
    xover.repaint();
    statusLine = "Lifting band 1 by " + ids::decibels (lanes[0]) + " now. A 3 ms look-ahead lets the lift fall away before loud parts arrive.";
}

void DensityPanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    const int nb = den.mb.numBands (den);
    for (int b = 0; b < nb; ++b)
        drawSectionLocal (g, Rect (kControlArea.getX() + b * 356, kControlArea.getY(), 344, 208).toFloat(), "BAND " + String (b + 1), colours::band (b));
}

// ===========================================================================
// PhasePanel
// ===========================================================================
PhasePanel::PhasePanel (PlanishProcessor& p, SpectrumSource& s)
    : ModulePanel (p, ModPhase, s), phs (p.chain.phase)
{
    setSize (kPanelW, kPanelH);
    auto h = addStandardHeader();
    addChoice (typeChoice, pid (phs.pScopeType), {}, h.removeFromLeft (120), "What the oscilloscope shows: this track, the sidechain, both, or their sum (how they combine at the mix bus).");
    typeChoice.fontH = 12.0f;
    h.removeFromLeft (8);
    addChoice (timeChoice, pid (phs.pScopeTime), {}, h.removeFromLeft (100), "How much of a bar the oscilloscope spans, synced to Live's tempo.");
    timeChoice.fontH = 12.0f;

    addAndMakeVisible (scope);
    scope.setBounds (kViewArea);
    scope.recorder = &phs.scope;
    scope.ppqNow = [this] { const int pos = phs.scope.position(); return pos > 0 ? phs.scope.at (pos - 1).ppq : 0.0; };

    auto c = kControlArea;
    addButton (learnBtn, Rect (c.getX() + 20, c.getY() + 30, 140, 44),
               "LEARN: listens for about a second and a half, then sets ROTATION to the angle that lowers the peaks most. With a sidechain it sets DELAY to line this track up with it, and flips the polarity if they fight.",
               [this] (const ModifierKeys&) { phs.requestLearn(); });
    learnBtn.setFontHeight (13.0f);
    addButton (invertBtn, Rect (c.getX() + 20, c.getY() + 90, 140, 36), "Flip the polarity (ROTATION to 180 degrees, or back to 0).", [this] (const ModifierKeys&)
    {
        setp (phs.pRotation, std::abs (pv (phs.pRotation)) > 179.0f ? 0.0f : 180.0f);
    });
    invertBtn.setFontHeight (11.0f);
    addKnob (rotationKnob, pid (phs.pRotation), "ROTATION", Rect (c.getX() + 360, c.getY() + 6, 170, 190),
             "Turns the phase of every frequency by the same angle, both channels alike: an asymmetric waveform evens out, gaining headroom, with no shift in time.", false);
    rotationKnob.bipolar = true;
    rotationKnob.big = true;
    addKnob (delayKnob, pid (phs.pDelay), "DELAY", Rect (c.getX() + 620, c.getY() + 6, 170, 190), "Shifts this track later by up to 20 ms, to line it up with another (a multi-miked drum, say).", false);
    delayKnob.big = true;
    rotationKnob.accent = delayKnob.accent = juce::Colour (phs.info().colour);
}

void PhasePanel::refresh()
{
    ModulePanel::refresh();
    scope.type = (int) pv (phs.pScopeType);
    const double fr[5] { 1.0, 0.5, 0.25, 0.125, 0.0625 };
    scope.barFraction = fr[jlimit (0, 4, (int) pv (phs.pScopeTime))];
    scope.repaint();
    learnBtn.setLit (phs.learning());
    learnBtn.setLabel (phs.learning() ? "LISTENING " + String (roundToInt (phs.learnProgress() * 100.0f)) + "%" : String ("LEARN"));
    invertBtn.setLit (std::abs (pv (phs.pRotation)) > 179.0f);
    if (proc.getParam (ids::zeroLatency) > 0.5f)
        statusLine = "ZERO LATENCY is on: rotation needs a frame of look-ahead, so only DELAY works.";
    else if (phs.learnedPeakGainDb < -0.1f || phs.learnedDelayValid)
        statusLine = "Learned: " + String (phs.learnedRotation, 0) + " degrees" + (phs.learnedPeakGainDb < -0.1f ? " (peaks " + ids::decibels (phs.learnedPeakGainDb) + ")" : String())
                   + (phs.learnedDelayValid ? ", delay " + String (phs.learnedDelayMs, 2) + " ms" + (phs.learnedPolarityFlip ? ", polarity flipped" : "") : String()) + ".";
    else
        statusLine = proc.chain.sidechainPresent() ? "A sidechain is coming in: LEARN will line this track up with it." : "Route another track to the sidechain to learn a delay against it.";
}

void PhasePanel::paint (Graphics& g)
{
    ModulePanel::paint (g);
    drawText (g, String (roundToInt (pv (phs.pRotation))) + String (CharPointer_UTF8 ("\xc2\xb0")),
              { (float) kControlArea.getX() + 200.0f, (float) kControlArea.getY() + 60.0f, 140.0f, 50.0f }, 34.0f, colours::lcd, true);
}

// ===========================================================================
std::unique_ptr<ModulePanel> makeModulePanel (int id, PlanishProcessor& p, SpectrumSource& s, SpectrumSource& in)
{
    switch (id)
    {
        case ModGate:            return std::make_unique<GatePanel> (p, s);
        case ModEqualizer:       return std::make_unique<EqPanel> (p, s, in);
        case ModCompressor1:     return std::make_unique<CompressorPanel> (p, ModCompressor1, s);
        case ModCompressor2:     return std::make_unique<CompressorPanel> (p, ModCompressor2, s);
        case ModTransientShaper: return std::make_unique<TransientPanel> (p, s);
        case ModExciter:         return std::make_unique<ExciterPanel> (p, s, in);
        case ModSculptor:        return std::make_unique<SculptorPanel> (p, s);
        case ModUnmask:          return std::make_unique<UnmaskPanel> (p, s);
        case ModDensity:         return std::make_unique<DensityPanel> (p, s);
        case ModPhase:           return std::make_unique<PhasePanel> (p, s);
        case ModClipper:         return std::make_unique<ClipperPanel> (p, s);
        default:                 return nullptr;
    }
}
} // namespace planish::ui
