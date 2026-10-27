#include "PluginEditor.h"

using namespace planish;
using namespace planish::ui;
using juce::Rectangle;
using juce::String;
using Rect = juce::Rectangle<int>;

namespace
{
// Section boxes (design space).
const Rectangle<float> kChainBox  { 12.0f, 92.0f, 1104.0f, 84.0f };
const Rectangle<float> kModuleBox { 12.0f, 192.0f, 1104.0f, 696.0f };
const Rectangle<float> kIoBox     { 1130.0f, 92.0f, 258.0f, 796.0f };

constexpr float kTileW = 88.0f, kTileH = 66.0f, kTileGap = 7.0f;
} // namespace

// ===========================================================================
// ChainStrip
// ===========================================================================
ChainStrip::ChainStrip (PlanishProcessor& p) : proc (p)
{
    setTooltip ("The signal chain, left to right. Click a tile to open its module, drag it to reorder. On each tile: power, S (solo), x (remove), "
                "the difference it makes, and its MIX (drag; double-click for 100%). + adds a module.");
}

Rectangle<float> ChainStrip::tileRect (int index) const
{
    return { 6.0f + (float) index * (kTileW + kTileGap), 3.0f, kTileW, kTileH };
}
Rectangle<float> ChainStrip::addRect() const
{
    return { 6.0f + (float) order.size() * (kTileW + kTileGap), 3.0f, 36.0f, kTileH };
}
Rectangle<float> ChainStrip::powerRect (Rectangle<float> r) { return Rectangle<float> (18.0f, 18.0f).withCentre ({ r.getX() + 11.0f, r.getY() + 11.0f }); }
Rectangle<float> ChainStrip::soloRect (Rectangle<float> r)  { return { r.getRight() - 36.0f, r.getY() + 3.0f, 15.0f, 15.0f }; }
Rectangle<float> ChainStrip::removeRect (Rectangle<float> r) { return { r.getRight() - 18.0f, r.getY() + 3.0f, 15.0f, 15.0f }; }
Rectangle<float> ChainStrip::mixRect (Rectangle<float> r)   { return { r.getX() + 6.0f, r.getBottom() - 13.0f, r.getWidth() - 12.0f, 9.0f }; }

int ChainStrip::tileAt (juce::Point<float> p) const
{
    for (int i = 0; i < (int) order.size(); ++i)
        if (tileRect (i).contains (p)) return i;
    return -1;
}

void ChainStrip::refresh()
{
    const auto o = proc.getOrder();
    if (o != order) order = o;
    repaint();
}

void ChainStrip::paint (juce::Graphics& g)
{
    for (int i = 0; i < (int) order.size(); ++i)
    {
        const int id = order[(size_t) i];
        auto r = tileRect (i);
        if (dragging == i && dragMoved) r = r.withX (dragX - kTileW * 0.5f);
        const Module& m = proc.chain.module (id);
        const juce::Colour c (m.info().colour);
        const bool on = proc.getParam (proc.paramId (m, 0)) > 0.5f;
        const bool sel = id == selected;
        const bool soloed = proc.soloModule.load() == id;
        g.setColour (colours::black);
        g.fillRect (r);
        g.setColour (sel ? c : (hover == i ? colours::ink : colours::inkFaint));
        g.drawRect (r, sel ? 2.0f : 1.0f);
        if (sel)
        {
            g.setColour (c);
            g.fillRect (r.withHeight (3.0f));
        }
        drawLed (g, powerRect (r).getCentre(), 4.5f, c, on);
        drawText (g, Chain::moduleShortName (id), { r.getX() + 4.0f, r.getY() + 20.0f, r.getWidth() - 8.0f, 14.0f }, 10.5f, on ? c : colours::inkDim, true, juce::Justification::centredLeft);
        // Solo and remove.
        const auto sr = soloRect (r), xr = removeRect (r);
        g.setColour (soloed ? colours::ledYellow : colours::socket);
        g.fillRoundedRectangle (sr, 2.0f);
        drawText (g, "S", sr, 9.0f, soloed ? colours::black : colours::ink, true);
        g.setColour (colours::socket);
        g.fillRoundedRectangle (xr, 2.0f);
        drawText (g, "x", xr, 10.0f, colours::ink, true);
        // Difference meter: the level the module adds or takes away.
        const float d = juce::jlimit (-12.0f, 12.0f, proc.chain.moduleGainDb (id));
        const auto dr = Rectangle<float> (r.getX() + 6.0f, r.getY() + 38.0f, r.getWidth() - 12.0f, 6.0f);
        g.setColour (colours::socket);
        g.fillRect (dr);
        const float zero = dr.getCentreX(), x = dr.getX() + (d + 12.0f) / 24.0f * dr.getWidth();
        g.setColour (on ? c.withAlpha (0.85f) : colours::inkDim);
        g.fillRect (Rectangle<float> (juce::jmin (zero, x), dr.getY() + 1.0f, std::abs (x - zero), dr.getHeight() - 2.0f));
        g.setColour (colours::inkDim);
        g.drawVerticalLine ((int) zero, dr.getY(), dr.getBottom());
        drawText (g, (d > 0.05f ? "+" : "") + String (d, 1), { r.getX() + 4.0f, r.getY() + 20.0f, r.getWidth() - 8.0f, 14.0f }, 9.5f, colours::lcd, true, juce::Justification::centredRight);
        // MIX.
        const float mix = proc.getParam (proc.paramId (m, 1));
        const auto mr = mixRect (r);
        g.setColour (colours::socket);
        g.fillRect (mr);
        g.setColour (mix < 99.5f ? colours::lcd : colours::creamShade);
        g.fillRect (mr.reduced (1.0f).withWidth ((mr.getWidth() - 2.0f) * mix / 100.0f));
        drawText (g, "MIX " + String (juce::roundToInt (mix)), mr.translated (0.0f, -1.0f), 7.5f, colours::black, true);
        // Arrow to the next.
        g.setColour (colours::inkDim);
        const float ax = r.getRight() + kTileGap * 0.5f, ay = r.getCentreY();
        juce::Path tri;
        tri.addTriangle (ax - 2.0f, ay - 3.0f, ax - 2.0f, ay + 3.0f, ax + 2.0f, ay);
        g.fillPath (tri);
    }
    const auto a = addRect();
    g.setColour (hover == -2 ? colours::cream : colours::black);
    g.fillRect (a);
    g.setColour (colours::inkDim);
    g.drawRect (a, 1.0f);
    drawText (g, "+", a, 22.0f, hover == -2 ? colours::black : colours::ink, true);
    if (order.empty())
        drawText (g, "Empty chain. Press + to add a module, pick a preset, or ASSIST to have one built.", { a.getRight() + 16.0f, a.getY(), 640.0f, a.getHeight() }, 13.0f, colours::inkDim, false, juce::Justification::centredLeft);
}

void ChainStrip::openAddMenu()
{
    juce::PopupMenu m;
    auto present = [this] (int id) { for (int o : order) if (o == id) return true; return false; };
    for (int id = 0; id < NumModules; ++id)
    {
        if (id == ModCompressor2) continue;
        if (id == ModCompressor1)
        {
            // The Compressor can be used twice.
            const bool one = present (ModCompressor1), two = present (ModCompressor2);
            const int next = ! one ? ModCompressor1 : ModCompressor2;
            m.addItem (one && ! two ? "COMPRESSOR (a second)" : "COMPRESSOR", ! (one && two), false, [this, next] { proc.addModule (next); if (onSelect) onSelect (next); });
            continue;
        }
        m.addItem (Chain::moduleName (id), ! present (id), false, [this, id] { proc.addModule (id); if (onSelect) onSelect (id); });
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addRect().toNearestInt())));
}

void ChainStrip::setMixFromX (int id, float x, Rectangle<float> bar)
{
    const float v = juce::jlimit (0.0f, 100.0f, (x - bar.getX()) / bar.getWidth() * 100.0f);
    proc.setParam (proc.paramId (proc.chain.module (id), 1), std::round (v));
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (addRect().contains (p)) { openAddMenu(); return; }
    const int i = tileAt (p);
    if (i < 0) return;
    const int id = order[(size_t) i];
    const auto r = tileRect (i);
    if (powerRect (r).contains (p))
    {
        const auto pidOn = proc.paramId (proc.chain.module (id), 0);
        proc.setParam (pidOn, proc.getParam (pidOn) > 0.5f ? 0.0f : 1.0f);
        return;
    }
    if (soloRect (r).contains (p)) { proc.soloModule.store (proc.soloModule.load() == id ? -1 : id); repaint(); return; }
    if (removeRect (r).contains (p))
    {
        if (proc.soloModule.load() == id) proc.soloModule.store (-1);
        if (proc.deltaModule.load() == id) proc.deltaModule.store (-1);
        proc.removeModule (id);
        if (selected == id)
        {
            const auto o = proc.getOrder();
            if (onSelect) onSelect (o.empty() ? -1 : o.front());
        }
        refresh();
        return;
    }
    if (mixRect (r).expanded (0.0f, 3.0f).contains (p))
    {
        mixDragging = i;
        setMixFromX (id, p.x, mixRect (r));
        return;
    }
    dragging = i;
    dragX = p.x;
    dragMoved = false;
    if (onSelect) onSelect (id);
}

void ChainStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = tileAt (e.position);
    if (i < 0) return;
    const auto r = tileRect (i);
    if (mixRect (r).expanded (0.0f, 3.0f).contains (e.position))
        proc.setParam (proc.paramId (proc.chain.module (order[(size_t) i]), 1), 100.0f);
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (mixDragging >= 0)
    {
        setMixFromX (order[(size_t) mixDragging], e.position.x, mixRect (tileRect (mixDragging)));
        repaint();
        return;
    }
    if (dragging < 0) return;
    if (std::abs (e.getDistanceFromDragStartX()) > 4) dragMoved = true;
    dragX = e.position.x;
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (dragging >= 0 && dragMoved)
    {
        const int id = order[(size_t) dragging];
        int pos = (int) ((e.position.x - 6.0f + kTileGap * 0.5f) / (kTileW + kTileGap));
        pos = juce::jlimit (0, (int) order.size() - 1, pos);
        proc.moveModule (id, pos);
    }
    dragging = -1;
    mixDragging = -1;
    dragMoved = false;
    refresh();
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = addRect().contains (e.position) ? -2 : tileAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

// ===========================================================================
// IOPanel
// ===========================================================================
namespace
{
constexpr float kMeterTop = 48.0f, kMeterH = 200.0f;
}

IOPanel::IOPanel (PlanishProcessor& p) : proc (p)
{
    setSize (258, 796);
    for (auto* m : { &inL, &inR, &outL, &outR }) { addAndMakeVisible (*m); m->setInterceptsMouseClicks (false, false); }
    inL.setBounds (30, (int) kMeterTop, 12, (int) kMeterH); inR.setBounds (46, (int) kMeterTop, 12, (int) kMeterH);
    outL.setBounds (200, (int) kMeterTop, 12, (int) kMeterH); outR.setBounds (216, (int) kMeterTop, 12, (int) kMeterH);

    meterBtn.setFontHeight (9.0f);
    meterBtn.onClick = [this] (const juce::ModifierKeys&) { proc.setParam (ids::meterType, proc.geti (ids::meterType) == 0 ? 1.0f : 0.0f); };
    meterBtn.setTooltip ("What the bars and readouts show under the peaks: RMS, or SHORT-TERM loudness (LUFS).");
    addAndMakeVisible (meterBtn);
    meterBtn.setBounds (96, 18, 66, 20);

    addKnob (inKnob, ids::inGain, "INPUT", { 10, 282, 70, 76 }, "Input gain, before the first module.");
    addKnob (widthKnob, ids::width, "WIDTH", { 94, 282, 70, 76 }, "Stereo width: -100% is mono, +100% twice as wide.");
    addKnob (outKnob, ids::outGain, "OUTPUT", { 178, 282, 70, 76 }, "Output gain, after the modules (the Visual Mixer moves it too).");
    addKnob (panKnob, ids::pan, "PAN", { 94, 362, 70, 76 }, "Pan the track (the Visual Mixer moves it too).");
    for (auto* k : { &inKnob, &widthKnob, &outKnob, &panKnob }) k->bipolar = true;
    addToggle (invertBtn, ids::invert, { 14, 384, 74, 30 }, "Invert the polarity of both channels.");
    addToggle (swapBtn, ids::swap, { 170, 384, 74, 30 }, "Swap left and right.");

    addToggle (limiterBtn, ids::limiter, { 22, 466, 96, 28 }, "The output limiter: catches the peaks after the output gain.");
    grMeter.rangeMin = -12.0f; grMeter.rangeMax = 0.0f; grMeter.bipolar = false; grMeter.colour = colours::ledRed;
    addAndMakeVisible (grMeter);
    grMeter.setBounds (126, 474, 110, 12);
    addKnob (ceilingKnob, ids::ceiling, "CEILING", { 18, 500, 66, 76 }, "The highest the output may go.");
    addChoice (styleChoice, ids::limStyle, "STYLE", { 92, 504, 72, 44 }, "CLEAR keeps transients, SMOOTH is in between, THICK is denser and warmer.");
    addChoice (modeChoice, ids::limMode, "MODE", { 170, 504, 72, 44 }, "IRC II: lookahead, the cleanest. IRC LL: less lookahead. HARD: a fast clip-like limit. ZERO-L: no latency.");

    addToggle (bypassBtn, ids::bypass, { 14, 600, 230, 36 }, "Hear the input, delayed to line up, instead of the processed track.");
    bypassBtn.setFontHeight (13.0f);
    addToggle (matchBtn, ids::gainMatch, { 14, 642, 112, 28 }, "AUTO GAIN: turn the output to the loudness of the input, so BYPASS compares fairly.");
    addToggle (monoBtn, ids::mono, { 132, 642, 112, 28 }, "Sum the output to mono, to check it survives one speaker.");
    addToggle (zeroBtn, ids::zeroLatency, { 14, 676, 230, 26 }, "ZERO LATENCY: drops the lookahead and the oversampling to play live. Modules that need latency (Phase, spectral ones) are bypassed.");
    for (auto* b : { &matchBtn, &monoBtn, &invertBtn, &swapBtn, &limiterBtn, &zeroBtn }) b->setFontHeight (10.0f);
}

void IOPanel::addKnob (Knob& k, const char* id, const String& caption, Rect r, const String& tip)
{
    k.caption = caption;
    k.small = true;
    k.setTooltip (tip);
    if (auto* param = proc.param (id))
        k.format = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    addAndMakeVisible (k);
    k.setBounds (r);
    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
}

void IOPanel::addToggle (PanelButton& b, const char* id, Rect r, const String& tip)
{
    b.setTooltip (tip);
    const String pidStr (id);
    b.onClick = [this, pidStr] (const juce::ModifierKeys&) { proc.setParam (pidStr, proc.getParam (pidStr) > 0.5f ? 0.0f : 1.0f); };
    addAndMakeVisible (b);
    b.setBounds (r);
    toggles.push_back ({ &b, pidStr });
}

void IOPanel::addChoice (ChoiceDisplay& c, const char* id, const String& caption, Rect r, const String& tip)
{
    c.caption = caption;
    c.fontH = 11.0f;
    c.setTooltip (tip);
    const String pidStr (id);
    c.getText = [this, pidStr] { if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.param (pidStr))) return p->getCurrentChoiceName().toUpperCase(); return String(); };
    c.onOpen = [this, pidStr, &c]
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.param (pidStr));
        if (param == nullptr) return;
        juce::PopupMenu m;
        for (int i = 0; i < param->choices.size(); ++i)
            m.addItem (param->choices[i], true, i == param->getIndex(), [this, pidStr, i] { proc.setParam (pidStr, (float) i); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c));
    };
    addAndMakeVisible (c);
    c.setBounds (r);
    choices.push_back (&c);
}

void IOPanel::refresh()
{
    for (auto& [b, id] : toggles) b->setLit (proc.getParam (id) > 0.5f);
    for (auto* c : choices) c->refresh();
    const bool stereo = proc.isStereo();
    enableAll (stereo, widthKnob, panKnob, swapBtn);
    const bool lim = proc.getParam (ids::limiter) > 0.5f;
    enableAll (lim, ceilingKnob, styleChoice, modeChoice);
    styleChoice.setEnabled (lim && proc.geti (ids::limMode) < 2);
    grMeter.set (lim ? -std::abs (proc.chain.limiterGrDb()) : 0.0f);

    inRead = proc.chain.takeInputMeters();
    outRead = proc.chain.takeOutputMeters();
    const int type = proc.geti (ids::meterType);
    meterBtn.setLabel (type == 0 ? "RMS" : "SHORT TERM");
    inL.push (inRead.peak[0]); inR.push (stereo ? inRead.peak[1] : inRead.peak[0]);
    outL.push (outRead.peak[0]); outR.push (stereo ? outRead.peak[1] : outRead.peak[0]);
    inPeakHold = juce::jmax (inPeakHold * 0.97f, inRead.peak[0], inRead.peak[1]);
    outPeakHold = juce::jmax (outPeakHold * 0.97f, outRead.peak[0], outRead.peak[1]);
    const auto t = String (inRead.lufsShort, 1) + String (outRead.lufsShort, 1) + String (outRead.truePeakDb, 1) + String (gainToDb (inPeakHold), 1)
                 + String (gainToDb (outPeakHold), 1) + String ((int) inRead.clipped[0] + (int) outRead.clipped[0]) + String (proc.chain.latency()) + String (type);
    if (t != shownText) { shownText = t; repaint(); }
}

void IOPanel::mouseDown (const juce::MouseEvent& e)
{
    // Click the readouts to clear the clip lights and the integrated loudness.
    if (e.position.y < 280.0f || e.position.y > 706.0f) proc.resetMeters();
}

void IOPanel::paint (juce::Graphics& g)
{
    drawText (g, "IN", { 24.0f, 20.0f, 40.0f, 14.0f }, 11.0f, colours::ink, true);
    drawText (g, "OUT", { 194.0f, 20.0f, 40.0f, 14.0f }, 11.0f, colours::ink, true);
    const int type = proc.geti (ids::meterType);
    auto readout = [&] (float x, const MeterReadings& r, float hold)
    {
        const bool clip = r.clipped[0] || r.clipped[1];
        const auto box = Rectangle<float> (x, 254.0f, 62.0f, 16.0f);
        g.setColour (clip ? colours::ledRed : colours::black);
        g.fillRect (box);
        g.setColour (colours::inkDim);
        g.drawRect (box, 1.0f);
        drawText (g, clip ? "CLIP" : String (gainToDb (juce::jmax (hold, 1e-5f)), 1), box, 10.0f, clip ? colours::black : colours::lcd, true);
        const float v = type == 0 ? gainToDb (juce::jmax (r.rms[0], r.rms[1], 1e-5f)) : r.lufsShort;
        drawText (g, (type == 0 ? "RMS " : "ST ") + String (juce::jmax (-70.0f, v), 1), box.translated (0.0f, 17.0f), 9.0f, colours::inkDim, true);
    };
    readout (4.0f, inRead, inPeakHold);
    readout (192.0f, outRead, outPeakHold);
    for (int db = 0; db >= -60; db -= 12)
    {
        const float y = kMeterTop + (float) (-db) / 60.0f * kMeterH;
        drawText (g, String (db), { 62.0f, y - 6.0f, 36.0f, 12.0f }, 9.0f, colours::inkDim, false, juce::Justification::centredLeft);
        drawText (g, String (db), { 162.0f, y - 6.0f, 36.0f, 12.0f }, 9.0f, colours::inkDim, false, juce::Justification::centredRight);
    }
    // The limiter section.
    drawSection (g, { 14.0f, 456.0f, 230.0f, 128.0f }, "OUTPUT LIMITER", colours::brand);
    drawText (g, "GR " + String (proc.getParam (ids::limiter) > 0.5f ? -std::abs (proc.chain.limiterGrDb()) : 0.0f, 1), { 126.0f, 488.0f, 110.0f, 12.0f }, 9.0f, colours::lcd, true, juce::Justification::centredRight);

    // Loudness.
    const auto lcd = Rectangle<float> (14.0f, 710.0f, 230.0f, 62.0f);
    drawLcdBox (g, lcd);
    drawText (g, String (outRead.lufsIntegrated, 1), { 22.0f, 714.0f, 96.0f, 28.0f }, 24.0f, colours::lcd, true, juce::Justification::centredLeft);
    drawText (g, "LUFS INTEGRATED", { 22.0f, 742.0f, 110.0f, 10.0f }, 8.0f, colours::inkDim, true, juce::Justification::centredLeft);
    drawText (g, "SHORT " + String (outRead.lufsShort, 1), { 132.0f, 716.0f, 106.0f, 12.0f }, 10.0f, colours::lcd, true, juce::Justification::centredLeft);
    const bool over = outRead.truePeakDb > -0.05f;
    drawText (g, "TP " + String (outRead.truePeakDb, 1) + " dBTP", { 132.0f, 732.0f, 106.0f, 12.0f }, 10.0f, over ? colours::ledRed : colours::lcd, true, juce::Justification::centredLeft);
    drawText (g, proc.getParam (ids::gainMatch) > 0.5f ? "AUTO GAIN " + ids::decibels (proc.chain.gainMatchDb()) : String ("click to reset"),
              { 22.0f, 756.0f, 216.0f, 12.0f }, 9.0f, colours::inkDim, true, juce::Justification::centredLeft);
    const double sr = juce::jmax (1.0, proc.getSampleRate());
    drawText (g, "LATENCY " + String (proc.chain.latency()) + " smp  (" + String (proc.chain.latency() * 1000.0 / sr, 1) + " ms)",
              { 14.0f, 776.0f, 230.0f, 14.0f }, 9.5f, colours::inkDim, true);
}

// ===========================================================================
// PlanishPanel
// ===========================================================================
PlanishPanel::PlanishPanel (PlanishProcessor& p)
    : proc (p), chain (p), io (p), assistant (p, outputSpectrum), mixer (p)
{
    setLookAndFeel (&lnf);
    setSize (designW, designH);
    setWantsKeyboardFocus (true);

    // --- header -------------------------------------------------------------------
    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (panelFont (14.0f));
    nameLabel.setJustificationType (juce::Justification::centredLeft);
    nameLabel.setColour (juce::Label::textColourId, colours::ink);
    nameLabel.setColour (juce::Label::backgroundColourId, colours::black);
    nameLabel.setColour (juce::Label::outlineColourId, colours::inkFaint);
    nameLabel.setColour (juce::Label::textWhenEditingColourId, colours::ink);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, colours::black);
    nameLabel.setColour (juce::TextEditor::highlightColourId, colours::brand.withAlpha (0.4f));
    nameLabel.setTooltip ("This track's name, as the other Planish show it (masking meter, Visual Mixer). From the host when it tells us; double-click to rename.");
    nameLabel.onTextChange = [this] { const auto n = nameLabel.getText().trim(); if (n.isNotEmpty()) proc.setInstanceName (n); };
    addAndMakeVisible (nameLabel);
    nameLabel.setBounds (300, 24, 168, 30);

    addButton (assistBtn, { 478, 20, 84, 38 }, "ASSIST: listen to the track and build a chain toward a target for what it is.", [this] (const juce::ModifierKeys&)
    {
        showView (view == AssistantPage ? Modules : AssistantPage);
    });
    assistBtn.setFontHeight (14.0f);

    presetChoice.getText = [this]
    {
        const int n = proc.presetNumber();
        return (n > 0 ? String (n).paddedLeft ('0', 2) + "   " : String()) + proc.presetName().toUpperCase() + (proc.presetEdited() ? "  *" : "");
    };
    presetChoice.fontH = 14.0f;
    presetChoice.onOpen = [this] { openPresetMenu(); };
    presetChoice.setTooltip ("Presets: a chain and the settings of every module in it. A * means you've changed it since loading.");
    addAndMakeVisible (presetChoice);
    presetChoice.setBounds (608, 20, 250, 38);
    addButton (prevBtn, { 572, 20, 32, 38 }, "Previous preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (-1); });
    addButton (nextBtn, { 862, 20, 32, 38 }, "Next preset.", [this] (const juce::ModifierKeys&) { proc.stepPreset (1); });
    prevBtn.setFontHeight (15.0f); nextBtn.setFontHeight (15.0f);
    addButton (saveBtn, { 902, 23, 50, 32 }, "Save these settings as a preset of your own.", [this] (const juce::ModifierKeys&) { savePresetDialog(); });
    addButton (historyBtn, { 956, 23, 64, 32 }, "HISTORY: every change you've made, by name. Pick one to go back to how it was then.", [this] (const juce::ModifierKeys&) { openHistoryMenu(); });
    addButton (undoBtn, { 1024, 23, 50, 32 }, "Undo the last change (Cmd/Ctrl-Z).", [this] (const juce::ModifierKeys&) { proc.undo(); });
    for (auto* b : { &saveBtn, &historyBtn, &undoBtn }) b->setFontHeight (9.5f);
    for (int i = 0; i < 4; ++i)
    {
        addButton (snapBtn[i], { 1084 + i * 31, 23, 29, 32 }, "Set " + String::charToString ((juce::juce_wchar) ('A' + i)) + ": click to recall, shift-click to store the current settings here.",
                   [this, i] (const juce::ModifierKeys& m) { if (m.isShiftDown() || ! proc.hasSnapshot (i)) proc.setSnapshot (i); else proc.recallSnapshot (i); });
        snapBtn[i].setFontHeight (11.0f);
    }
    addButton (mixerBtn, { 1218, 20, 70, 38 }, "VISUAL MIXER: every Planish in the session on one pad: level, pan and width, and the MIX ASSISTANT.", [this] (const juce::ModifierKeys&)
    {
        showView (view == MixerPage ? Modules : MixerPage);
    });
    mixerBtn.setFontHeight (12.0f);
    addButton (optionsBtn, { 1294, 23, 54, 32 }, "Options: the analyser, the frequency scale, metering.", [this] (const juce::ModifierKeys&) { openOptionsMenu(); });
    optionsBtn.setFontHeight (8.5f);
    addButton (tipsBtn, { 1352, 23, 36, 32 }, "Show or hide these tips (remembered for every Planish).", [this] (const juce::ModifierKeys&)
    {
        settings.setTooltipsEnabled (! settings.tooltipsEnabled());
        syncTooltips();
    });
    tipsBtn.setFontHeight (8.5f);

    // --- chain, modules, I/O ------------------------------------------------------------
    addAndMakeVisible (chain);
    chain.setBounds (kChainBox.toNearestInt().reduced (4, 6));
    chain.onSelect = [this] (int id) { selectModule (id); };
    for (int id = 0; id < NumModules; ++id)
    {
        panels[(size_t) id] = makeModulePanel (id, proc, outputSpectrum, inputSpectrum);
        if (panels[(size_t) id] != nullptr)
        {
            addChildComponent (*panels[(size_t) id]);
            panels[(size_t) id]->setBounds (Rect (12, 200, kPanelW, kPanelH));
        }
    }
    addAndMakeVisible (io);
    io.setBounds (kIoBox.toNearestInt());
    addChildComponent (assistant);
    assistant.setBounds (Rect (12, 96, 1104, 796));
    assistant.onClose = [this] { showView (Modules); };
    addChildComponent (mixer);
    mixer.setBounds (Rect (12, 96, 1104, 796));
    mixer.onClose = [this] { showView (Modules); };

    applySettings();
    const auto order = proc.getOrder();
    selectModule (order.empty() ? -1 : order.front());
    if (proc.assistant.analysing()) showView (AssistantPage);
    startTimerHz (30);
    timerCallback();
}

PlanishPanel::~PlanishPanel()
{
    stopTimer();
    proc.chain.tapModule.store (-1);
    // Detach every parameter binding while every slider still exists.
    for (auto& p : panels)
        if (p != nullptr)
            p->releaseAttachments();
    setLookAndFeel (nullptr);
}

void PlanishPanel::applySettings()
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    for (auto* s : { &outputSpectrum, &inputSpectrum })
    {
        s->setSampleRate (sr);
        s->setAverageSeconds (Settings::averageSeconds (settings.spectrumAverage()));
        s->setPeakHold (settings.spectrumPeakHold());
        s->setPeakHoldSeconds (Settings::holdSeconds (settings.peakHoldTime()));
        s->setTilt (settings.spectrumTilt());
    }
    setSpectrumType (settings.spectrumType());
    setFrequencyScale (settings.frequencyScale());
    repaint();
}

void PlanishPanel::addButton (PanelButton& b, Rect r, const String& tip, std::function<void (const juce::ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    b.setBounds (r);
}

void PlanishPanel::selectModule (int id)
{
    selectedModule = id;
    chain.selected = id;
    for (int i = 0; i < NumModules; ++i)
        if (panels[(size_t) i] != nullptr)
            panels[(size_t) i]->setVisible (i == id && view == Modules);
    // The module's own views show its input and output.
    proc.chain.tapModule.store (view == Modules ? id : -1);
    repaint();
}

void PlanishPanel::showView (View v)
{
    view = v;
    if (v == AssistantPage) assistant.showResult();
    assistant.setVisible (v == AssistantPage);
    mixer.setVisible (v == MixerPage);
    chain.setVisible (v == Modules);
    assistBtn.setLit (v == AssistantPage);
    mixerBtn.setLit (v == MixerPage);
    selectModule (selectedModule);
}

bool PlanishPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo(); return true; }
    return false;
}

void PlanishPanel::openPresetMenu()
{
    juce::PopupMenu m;
    const int cur = proc.presetNumber();
    m.addSectionHeader ("FACTORY");
    const auto& f = factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i)
        m.addItem (String (i + 1).paddedLeft ('0', 2) + "   " + f[(size_t) i].name, true, cur == i + 1, [this, i] { proc.loadFactory (i); });
    const auto users = PlanishProcessor::userPresets();
    m.addSectionHeader ("YOURS");
    if (users.isEmpty())
        m.addItem ("(none yet: press SAVE)", false, false, [] {});
    for (int i = 0; i < users.size(); ++i)
    {
        const auto file = users[i];
        const int n = (int) f.size() + i + 1;
        m.addItem (String (n).paddedLeft ('0', 2) + "   " + file.getFileNameWithoutExtension(), true, cur == n, [this, file] { proc.loadUserPreset (file); });
    }
    m.addSeparator();
    m.addItem ("Save as...", [this] { savePresetDialog(); });
    m.addItem ("Show presets folder", []
    {
        const auto folder = PlanishProcessor::userPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetChoice));
}

void PlanishPanel::openHistoryMenu()
{
    juce::PopupMenu m;
    const auto& h = proc.historyItems();
    m.addSectionHeader ("GO BACK TO BEFORE...");
    if (h.empty())
        m.addItem ("(no changes yet)", false, false, [] {});
    // Newest first; the list is capped by the processor.
    for (int i = (int) h.size() - 1; i >= 0 && i >= (int) h.size() - 40; --i)
        m.addItem (h[(size_t) i].label, [this, i] { proc.revertTo (i); });
    m.addSeparator();
    m.addItem ("CLEAR HISTORY", ! h.empty(), false, [this] { proc.clearHistory(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyBtn));
}

void PlanishPanel::savePresetDialog()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "Name it. Saving under an existing name replaces it.", juce::MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.presetName());
    saveWindow->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr)
        {
            String error;
            if (! proc.saveUserPreset (saveWindow->getTextEditorContents ("name"), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", error);
        }
        saveWindow.reset();
    }), false);
}

void PlanishPanel::openOptionsMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SPECTRUM");
    juce::PopupMenu type;
    for (int i = 0; i < NumSpectrumTypes; ++i)
        type.addItem (spectrumTypeName (i), true, settings.spectrumType() == i, [this, i] { settings.setSpectrumType (i); applySettings(); });
    m.addSubMenu ("Display", type);
    juce::PopupMenu avg;
    const char* avgNames[] { "Real time", "1 second", "3 seconds", "5 seconds", "10 seconds", "Infinite" };
    for (int i = 0; i < 6; ++i)
        avg.addItem (avgNames[i], true, settings.spectrumAverage() == i, [this, i] { settings.setSpectrumAverage (i); applySettings(); });
    m.addSubMenu ("Averaging", avg);
    juce::PopupMenu hold;
    hold.addItem ("Off", true, ! settings.spectrumPeakHold(), [this] { settings.setSpectrumPeakHold (false); applySettings(); });
    const char* holdNames[] { "5 ms", "250 ms", "500 ms", "1 second", "5 seconds", "Infinite" };
    for (int i = 0; i < 6; ++i)
        hold.addItem (holdNames[i], true, settings.spectrumPeakHold() && settings.peakHoldTime() == i, [this, i]
        {
            settings.setSpectrumPeakHold (true);
            settings.setPeakHoldTime (i);
            applySettings();
        });
    m.addSubMenu ("Peak hold", hold);
    juce::PopupMenu tilt;
    for (float t : { 0.0f, 3.0f, 4.5f })
        tilt.addItem (String (t, 1) + " dB/oct" + (t == 3.0f ? " (pink noise reads flat)" : ""), true, std::abs (settings.spectrumTilt() - t) < 0.1f,
                      [this, t] { settings.setSpectrumTilt (t); applySettings(); });
    m.addSubMenu ("Tilt", tilt);
    m.addItem ("Reset peak hold", [this] { outputSpectrum.resetPeaks(); inputSpectrum.resetPeaks(); });

    m.addSectionHeader ("EQUALIZER");
    juce::PopupMenu scale;
    for (int i = 0; i < NumScales; ++i)
        scale.addItem (frequencyScaleName (i), true, settings.frequencyScale() == i, [this, i] { settings.setFrequencyScale (i); applySettings(); });
    m.addSubMenu ("Frequency scale", scale);
    m.addItem ("Show the spectrum behind the curve", true, settings.eqSpectrum(), [this] { settings.setEqSpectrum (! settings.eqSpectrum()); });
    juce::PopupMenu q;
    for (float v : { 1.0f, 2.0f, 4.0f, 8.0f })
        q.addItem ("Q " + String (v, 0), true, std::abs (settings.altSoloQ() - v) < 0.01f, [this, v] { settings.setAltSoloQ (v); });
    m.addSubMenu ("Alt-click solo width", q);
    juce::PopupMenu hist;
    const char* histNames[] { "400 ms", "3 seconds", "Infinite" };
    for (int i = 0; i < 3; ++i)
        hist.addItem (histNames[i], true, settings.histogramHold() == i, [this, i] { settings.setHistogramHold (i); });
    m.addSubMenu ("Masking histogram hold", hist);

    m.addSectionHeader ("METERS");
    m.addItem ("RMS + Peak", true, proc.geti (ids::meterType) == 0, [this] { proc.setParam (ids::meterType, 0.0f); });
    m.addItem ("Short Term + Peak", true, proc.geti (ids::meterType) == 1, [this] { proc.setParam (ids::meterType, 1.0f); });
    m.addItem ("Auto gain (match output to input)", true, proc.getParam (ids::gainMatch) > 0.5f,
               [this] { proc.setParam (ids::gainMatch, proc.getParam (ids::gainMatch) > 0.5f ? 0.0f : 1.0f); });
    m.addSeparator();
    m.addItem ("Show settings folder", [] { Settings::file().getParentDirectory().createDirectory(); Settings::file().getParentDirectory().revealToUser(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsBtn));
}

void PlanishPanel::syncTooltips()
{
    const bool on = settings.tooltipsEnabled();
    tipsBtn.setLit (on);
    if ((int) on != tooltipsApplied && onTooltips)
    {
        tooltipsApplied = (int) on;
        onTooltips (on);
    }
}

void PlanishPanel::timerCallback()
{
    if (proc.getSampleRate() > 0) { outputSpectrum.setSampleRate (proc.getSampleRate()); inputSpectrum.setSampleRate (proc.getSampleRate()); }
    outputSpectrum.update (proc.chain.moduleTap);
    inputSpectrum.update (proc.chain.moduleInTap);
    presetChoice.refresh();
    if (! nameLabel.isBeingEdited() && nameLabel.getText() != proc.instanceName())
        nameLabel.setText (proc.instanceName(), juce::dontSendNotification);
    undoBtn.setDimmed (! proc.canUndo());
    historyBtn.setDimmed (! proc.canUndo());
    for (int i = 0; i < 4; ++i)
    {
        snapBtn[i].setLit (proc.activeSnapshot() == i);
        snapBtn[i].setDimmed (! proc.hasSnapshot (i));
    }
    // A module removed elsewhere (undo, preset, assistant): pick another.
    const auto order = proc.getOrder();
    if (selectedModule >= 0 && std::find (order.begin(), order.end(), selectedModule) == order.end())
        selectModule (order.empty() ? -1 : order.front());
    else if (selectedModule < 0 && ! order.empty())
        selectModule (order.front());
    chain.refresh();
    io.refresh();
    if (view == AssistantPage) assistant.refresh();
    else if (view == MixerPage) mixer.refresh();
    else if (selectedModule >= 0 && panels[(size_t) selectedModule] != nullptr)
        panels[(size_t) selectedModule]->refresh();
    if (proc.assistant.analysing() && view != AssistantPage)
        showView (AssistantPage);
}

void PlanishPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    drawText (g, "PLANISH", { 24.0f, 16.0f, 160.0f, 44.0f }, 34.0f, colours::brand, true, juce::Justification::centredLeft);
    drawText (g, "mixing suite", { 176.0f, 26.0f, 120.0f, 28.0f }, 14.0f, colours::ink, true, juce::Justification::centredLeft);
    drawText (g, "TRACK", { 300.0f, 10.0f, 100.0f, 12.0f }, 8.5f, colours::inkDim, true, juce::Justification::centredLeft);

    if (view == Modules)
    {
        drawSection (g, kChainBox, "SIGNAL CHAIN");
        if (selectedModule >= 0)
            drawSection (g, kModuleBox, Chain::moduleName (selectedModule), juce::Colour (proc.chain.module (selectedModule).info().colour));
        else
        {
            drawSection (g, kModuleBox, "MODULE");
            g.setColour (colours::inkDim);
            g.setFont (panelFont (14.0f, false));
            g.drawFittedText ("No module selected. Add one with + in the signal chain, pick a preset, or press ASSIST to have a chain built for the track.",
                              kModuleBox.toNearestInt().reduced (40), juce::Justification::centred, 3, 1.0f);
        }
    }
    // The assistant and the mixer draw their own sections.
    drawSection (g, kIoBox, "I/O");
}

// ===========================================================================
PlanishEditor::PlanishEditor (PlanishProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.onTooltips = [this] (bool on)
    {
        if (on && tooltips == nullptr) tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
        else if (! on) tooltips.reset();
    };
    panel.syncTooltips();
    setResizable (true, true);
    const double ratio = (double) PlanishPanel::designW / PlanishPanel::designH;
    setResizeLimits (840, (int) (840 / ratio), 2800, (int) (2800 / ratio));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);
    setSize (1260, (int) (1260 / ratio));
}

void PlanishEditor::paint (juce::Graphics& g) { g.fillAll (colours::panel); }

void PlanishEditor::resized()
{
    const float s = (float) getWidth() / (float) PlanishPanel::designW;
    panel.setTransform (juce::AffineTransform::scale (s));
    panel.setBounds (0, 0, PlanishPanel::designW, PlanishPanel::designH);
}
