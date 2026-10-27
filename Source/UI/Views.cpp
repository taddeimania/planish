#include "Views.h"

namespace planish::ui
{
using namespace juce;

// ===========================================================================
// ModulePanel
// ===========================================================================
ModulePanel::ModulePanel (PlanishProcessor& p, int moduleId, SpectrumSource& s) : proc (p), id (moduleId), spectrum (s)
{
    setInterceptsMouseClicks (false, true);
}

ModulePanel::~ModulePanel()
{
    jassert (attachments.empty());   // releaseAttachments() must run before the subclass's sliders go
}

void ModulePanel::releaseAttachments()
{
    attachments.clear();
    sliders.clear();
}

void ModulePanel::paint (Graphics& g)
{
    if (statusLine.isNotEmpty())
    {
        g.setColour (colours::inkDim);
        g.setFont (panelFont (12.0f, false));
        g.drawFittedText (statusLine, getLocalBounds().withTrimmedTop (getHeight() - 22).withTrimmedLeft (16).withTrimmedRight (16).withHeight (18),
                          Justification::centredLeft, 1, 1.0f);
    }
}

void ModulePanel::drawSectionLocal (Graphics& g, Rectangle<float> r, const String& title, Colour tab) const
{
    drawSection (g, r, title, tab);
}

void ModulePanel::bindSlider (Slider& s, const String& parameterId, Rectangle<int> r, const String& tip)
{
    s.setTooltip (tip);
    addAndMakeVisible (s);
    s.setBounds (r);
    rebind (s, parameterId);
}

namespace
{
void setFormat (Slider& s, RangedAudioParameter* param)
{
    auto f = [param] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };
    if (auto* k = dynamic_cast<Knob*> (&s)) k->format = f;
    if (auto* fd = dynamic_cast<Fader*> (&s)) fd->format = f;
    if (auto* o = dynamic_cast<Readout*> (&s)) o->format = f;
}
} // namespace

void ModulePanel::rebind (Slider& s, const String& parameterId)
{
    for (size_t i = 0; i < sliders.size(); ++i)
        if (sliders[i].first == &s)
        {
            if (sliders[i].second == parameterId) return;
            attachments[i].reset();
            sliders[i].second = parameterId;
            if (auto* param = proc.param (parameterId))
            {
                s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
                setFormat (s, param);
                attachments[i] = std::make_unique<SliderAttachment> (proc.apvts, parameterId, s);
            }
            return;
        }
    if (auto* param = proc.param (parameterId))
    {
        s.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        setFormat (s, param);
        sliders.push_back ({ &s, parameterId });
        attachments.push_back (std::make_unique<SliderAttachment> (proc.apvts, parameterId, s));
    }
    else jassertfalse;
}

void ModulePanel::rebindChoice (ChoiceDisplay& c, const String& parameterId)
{
    for (auto& [cd, idStr] : choices)
        if (cd == &c) idStr = parameterId;
    c.getText = [this, parameterId]
    {
        if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.param (parameterId)))
            return p->getCurrentChoiceName().toUpperCase();
        return String();
    };
    c.onOpen = [this, parameterId, &c] { openChoiceMenu (parameterId, &c); };
    c.refresh();
}

void ModulePanel::addKnob (Knob& k, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip, bool small)
{
    k.caption = caption;
    k.small = small;
    bindSlider (k, parameterId, r, tip);
}

void ModulePanel::addFader (Fader& f, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    f.caption = caption;
    bindSlider (f, parameterId, r, tip);
}

void ModulePanel::addReadout (Readout& o, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    o.caption = caption;
    bindSlider (o, parameterId, r, tip);
}

void ModulePanel::addToggle (PanelButton& b, const String& parameterId, Rectangle<int> r, const String& tip)
{
    addButton (b, r, tip, [this, &b] (const ModifierKeys&)
    {
        for (auto& [bb, idStr] : toggles)
            if (bb == &b) proc.setParam (idStr, proc.getParam (idStr) > 0.5f ? 0.0f : 1.0f);
    });
    toggles.push_back ({ &b, parameterId });
}

void ModulePanel::addButton (PanelButton& b, Rectangle<int> r, const String& tip, std::function<void (const ModifierKeys&)> click)
{
    b.setTooltip (tip);
    b.onClick = std::move (click);
    addAndMakeVisible (b);
    b.setBounds (r);
}

void ModulePanel::openChoiceMenu (const String& parameterId, Component* target)
{
    auto* param = dynamic_cast<AudioParameterChoice*> (proc.param (parameterId));
    if (param == nullptr) return;
    PopupMenu m;
    const int cur = param->getIndex();
    for (int i = 0; i < param->choices.size(); ++i)
        m.addItem (param->choices[i], true, i == cur, [this, parameterId, i] { proc.setParam (parameterId, (float) i); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

void ModulePanel::addChoice (ChoiceDisplay& c, const String& parameterId, const String& caption, Rectangle<int> r, const String& tip)
{
    c.caption = caption;
    c.setTooltip (tip);
    addAndMakeVisible (c);
    c.setBounds (r);
    choices.push_back ({ &c, parameterId });
    rebindChoice (c, parameterId);
}

Rectangle<int> ModulePanel::addStandardHeader (int chModeParam)
{
    auto h = headerArea();
    addButton (deltaBtn, h.removeFromLeft (64), "DELTA: hear only what this module changes, the difference between its output and its input.",
               [this] (const ModifierKeys&) { proc.deltaModule.store (proc.deltaModule.load() == id ? -1 : id); });
    deltaBtn.setFontHeight (11.0f);
    h.removeFromLeft (8);
    if (chModeParam >= 0)
    {
        addChoice (chModeChoice, pid (chModeParam), {}, h.removeFromLeft (130), "Channel mode. STEREO: one set of controls for both sides. "
                   "MID/SIDE: the centre and the sides each get their own. TRANS/SUS: the attacks and what rings on after them each get their own.");
        chModeChoice.fontH = 13.0f;
        h.removeFromLeft (8);
    }
    addButton (resetBtn, h.removeFromRight (60), "RESET: every control in this module back to its default (UNDO brings them back).",
               [this] (const ModifierKeys&) { proc.resetModule (id); });
    resetBtn.setFontHeight (11.0f);
    h.removeFromRight (6);
    addButton (presetBtn, h.removeFromRight (74), "Presets for this module alone.", [this] (const ModifierKeys&) { openModulePresets(); });
    presetBtn.setFontHeight (11.0f);
    h.removeFromRight (8);
    return h;
}

void ModulePanel::openModulePresets()
{
    PopupMenu m;
    m.addSectionHeader ("FACTORY");
    const bool isComp = id == ModCompressor1 || id == ModCompressor2;
    for (const auto& p : factoryModulePresets())
        if (p.module == id || (isComp && p.module == ModCompressor1))
        {
            const ModulePreset* pp = &p;
            m.addItem (p.name, [this, pp] { proc.loadModulePreset (id, *pp); });
        }
    m.addSectionHeader ("YOURS");
    const auto users = PlanishProcessor::userModulePresets (id);
    if (users.isEmpty())
        m.addItem ("(none yet: save one below)", false, false, [] {});
    for (const auto& f : users)
        m.addItem (f.getFileNameWithoutExtension(), [this, f] { proc.loadUserModulePreset (id, f); });
    m.addSeparator();
    m.addItem ("Save module preset...", [this]
    {
        auto* w = new AlertWindow ("Save module preset", "Name it.", MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&getLookAndFeel());
        w->addTextEditor ("name", Chain::moduleShortName (id));
        w->addButton ("SAVE", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("CANCEL", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                String err;
                if (! proc.saveUserModulePreset (id, w->getTextEditorContents ("name"), err))
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save", err);
            }
        }), true);
    });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn));
}

void ModulePanel::refresh()
{
    for (auto& [b, parameterId] : toggles)
        b->setLit (proc.getParam (parameterId) > 0.5f);
    for (auto& [c, parameterId] : choices)
        c->refresh();
    deltaBtn.setLit (proc.deltaModule.load() == id);
}

void ModulePanel::learnCrossovers (const MultibandParams& mb)
{
    // Crossovers at spectral minima between 60 Hz and 12 kHz, near where they are now.
    const int nb = (int) pv (mb.pNumBands) + 1;
    if (nb < 2) return;
    std::vector<std::pair<float, float>> minima;   // (hz, db)
    const float* cv = spectrum.curve();
    for (int c = 8; c < SpectrumSource::kColumns - 8; ++c)
    {
        const float hz = SpectrumSource::columnHz (c);
        if (hz < 60.0f || hz > 12000.0f) continue;
        bool isMin = true;
        for (int k = -6; k <= 6; ++k) if (cv[c + k] < cv[c]) isMin = false;
        if (isMin) minima.push_back ({ hz, cv[c] });
    }
    float last = 20.0f;
    for (int i = 0; i < nb - 1; ++i)
    {
        const float want = pv (mb.pXover[i]);
        float best = want, bd = 1e9f;
        for (auto& [hz, db] : minima)
        {
            const float d = std::abs (std::log2 (hz / want));
            if (hz > last * 1.5f && d < bd && d < 1.5f) { bd = d; best = hz; }
        }
        setp (mb.pXover[i], best);
        last = best;
    }
}

// ===========================================================================
// CrossoverView
// ===========================================================================
CrossoverView::CrossoverView (ModulePanel& p, const MultibandParams& m, std::atomic<int>& s) : panel (p), mb (m), solo (s)
{
    showLabels = true;
}

int CrossoverView::numBands() const { return (int) panel.pv (mb.pNumBands) + 1; }
float CrossoverView::xoverHz (int i) const { return panel.pv (mb.pXover[i]); }

void CrossoverView::setXover (int i, float hz)
{
    const int nb = numBands();
    const float lo = i > 0 ? xoverHz (i - 1) * 1.12f : 22.0f;
    const float hi = i < nb - 2 ? xoverHz (i + 1) / 1.12f : 19000.0f;
    panel.setp (mb.pXover[i], jlimit (lo, jmax (lo, hi), hz));
}

int CrossoverView::handleAt (Point<float> p) const
{
    for (int i = 0; i < numBands() - 1; ++i)
        if (std::abs (xForHz (xoverHz (i)) - p.x) < 7.0f) return i;
    return -1;
}

int CrossoverView::bandAt (float x) const
{
    const int nb = numBands();
    for (int i = 0; i < nb - 1; ++i)
        if (x < xForHz (xoverHz (i))) return i;
    return nb - 1;
}

Rectangle<float> CrossoverView::powerRect (int b) const
{
    const auto r = plotArea();
    const int nb = numBands();
    const float x0 = b == 0 ? r.getX() : xForHz (xoverHz (b - 1));
    const float x1 = b == nb - 1 ? r.getRight() : xForHz (xoverHz (b));
    return { (x0 + x1) * 0.5f - 34.0f, r.getY() + 6.0f, 22.0f, 16.0f };
}
Rectangle<float> CrossoverView::soloRect (int b) const { return powerRect (b).translated (26.0f, 0.0f); }
Rectangle<float> CrossoverView::removeRect (int b) const { return powerRect (b).translated (48.0f, 0.0f); }
Rectangle<float> CrossoverView::addRect() const { return { hover.x - 10.0f, plotArea().getBottom() - 40.0f, 20.0f, 20.0f }; }

void CrossoverView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
    {
        const float x0 = b == 0 ? r.getX() : xForHz (xoverHz (b - 1));
        const float x1 = b == nb - 1 ? r.getRight() : xForHz (xoverHz (b));
        const Colour c = colours::band (b);
        const bool on = panel.pv (mb.pBandOn[b]) > 0.5f;
        const bool sel = b == selectedBand;
        g.setColour (c.withAlpha (sel ? 0.16f : 0.06f));
        g.fillRect (Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
        if (bandLineDb)
        {
            const float db = bandLineDb (b);
            if (std::isfinite (db))
            {
                const float y = yForGain (db);
                g.setColour (c);
                g.drawLine (x0 + 2.0f, y, x1 - 2.0f, y, 2.0f);
            }
        }
        auto pr = powerRect (b), sr = soloRect (b), xr = removeRect (b);
        g.setColour (on ? c : colours::socket);
        g.fillRoundedRectangle (pr, 3.0f);
        drawText (g, "ON", pr, 9.0f, on ? colours::black : colours::inkDim, true);
        const bool soloed = solo.load() == b;
        g.setColour (soloed ? colours::ledYellow : colours::black);
        g.fillRoundedRectangle (sr, 3.0f);
        g.setColour (colours::inkDim);
        g.drawRoundedRectangle (sr, 3.0f, 1.0f);
        drawText (g, "S", sr, 10.0f, soloed ? colours::black : colours::ink, true);
        if (nb > 1)
        {
            g.setColour (colours::black);
            g.fillRoundedRectangle (xr, 3.0f);
            g.setColour (colours::inkDim);
            g.drawRoundedRectangle (xr, 3.0f, 1.0f);
            drawText (g, "x", xr, 11.0f, colours::ink, true);
        }
        drawText (g, "BAND " + String (b + 1), { x0 + 6.0f, r.getBottom() - 22.0f, 60.0f, 14.0f }, 10.0f, c, true, Justification::centredLeft);
    }
    for (int i = 0; i < nb - 1; ++i)
    {
        const float x = xForHz (xoverHz (i));
        g.setColour (colours::ink.withAlpha (dragging == i ? 1.0f : 0.7f));
        g.drawLine (x, r.getY(), x, r.getBottom(), dragging == i ? 2.0f : 1.2f);
        const auto h = Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getBottom() - 46.0f });
        g.setColour (colours::cream);
        g.fillEllipse (h);
        drawText (g, ids::hertz (xoverHz (i)), { x - 40.0f, r.getBottom() - 70.0f, 80.0f, 14.0f }, 10.0f, colours::ink, true);
    }
    if (hovering && nb < MultibandParams::kMaxBands && handleAt (hover) < 0)
    {
        const auto a = addRect();
        g.setColour (colours::cream);
        g.fillEllipse (a);
        drawText (g, "+", a, 14.0f, colours::black, true);
    }
}

void CrossoverView::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
    {
        if (powerRect (b).contains (p)) { panel.setp (mb.pBandOn[b], panel.pv (mb.pBandOn[b]) > 0.5f ? 0.0f : 1.0f); return; }
        if (soloRect (b).contains (p)) { solo.store (solo.load() == b ? -1 : b); repaint(); return; }
        if (nb > 1 && removeRect (b).contains (p))
        {
            // Remove band b: drop the crossover between it and its neighbour.
            const int drop = b == nb - 1 ? b - 1 : b;
            for (int i = drop; i < nb - 2; ++i) panel.setp (mb.pXover[i], xoverHz (i + 1));
            panel.setp (mb.pNumBands, (float) (nb - 2));
            if (solo.load() >= nb - 1) solo.store (-1);
            selectedBand = jmin (selectedBand, nb - 2);
            if (onSelect) onSelect (selectedBand);
            return;
        }
    }
    dragging = handleAt (p);
    if (dragging >= 0) return;
    if (hovering && nb < MultibandParams::kMaxBands && addRect().contains (p))
    {
        // Insert a crossover at the hover frequency.
        const float hz = hzForX (p.x);
        float f[3] { 20.0f, 20.0f, 20.0f };
        for (int i = 0; i < nb - 1; ++i) f[i] = xoverHz (i);
        int at = 0;
        while (at < nb - 1 && f[at] < hz) ++at;
        for (int i = nb - 1; i > at; --i) f[i] = f[i - 1];
        f[at] = hz;
        for (int i = 0; i < nb && i < 2; ++i) panel.setp (mb.pXover[i], f[i]);
        panel.setp (mb.pNumBands, (float) nb);
        panel.setp (mb.pBandOn[nb], 1.0f);
        return;
    }
    selectedBand = bandAt (p.x);
    if (onSelect) onSelect (selectedBand);
    repaint();
}

void CrossoverView::mouseDrag (const MouseEvent& e)
{
    if (dragging >= 0)
        setXover (dragging, hzForX (e.position.x));
}

void CrossoverView::mouseUp (const MouseEvent&) { dragging = -1; repaint(); }
void CrossoverView::mouseMove (const MouseEvent& e) { hover = e.position; hovering = true; repaint(); }
void CrossoverView::mouseExit (const MouseEvent&) { hovering = false; repaint(); }

void CrossoverView::mouseDoubleClick (const MouseEvent& e)
{
    const int h = handleAt (e.position);
    if (h < 0) return;
    auto* w = new AlertWindow ("Crossover " + String (h + 1), "Frequency in Hz.", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&getLookAndFeel());
    w->addTextEditor ("hz", String (roundToInt (xoverHz (h))));
    w->addButton ("SET", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("CANCEL", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w, h] (int result)
    {
        if (result == 1) setXover (h, w->getTextEditorContents ("hz").getFloatValue());
    }), true);
}

// ===========================================================================
// EqCurveView
// ===========================================================================
EqCurveView::EqCurveView (ModulePanel& p) : panel (p)
{
    showGainScale = true;
    setWantsKeyboardFocus (true);
}

Point<float> EqCurveView::nodePos (const Band& b) const
{
    return { xForHz (b.hz), b.hasGain ? yForGain (b.gainDb) : yForGain (0.0f) };
}

Rectangle<float> EqCurveView::arrowRect (const Band& b, bool up) const
{
    const auto p = nodePos (b);
    return Rectangle<float> (14.0f, 11.0f).withCentre ({ p.x, p.y + (up ? -16.0f : 16.0f) });
}

int EqCurveView::nodeAt (Point<float> p) const
{
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < numBands; ++i)
    {
        const Band b = getBand (i);
        if (! b.on) continue;
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < 12.0f && d < bd) { bd = d; best = i; }
    }
    return best;
}

void EqCurveView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();

    // Masking: brighter where more loudness is lost; the histogram hangs from the top.
    if (masking != nullptr)
        for (int b = 0; b < Masking::kBands; ++b)
        {
            const float x0 = xForHz (Masking::bandLowHz (b)), x1 = xForHz (Masking::bandHighHz (b));
            const float m = jlimit (0.0f, 1.0f, masking[b]);
            if (m > 0.01f)
            {
                g.setColour (colours::ledYellow.withAlpha (0.08f + 0.45f * m));
                g.fillRect (Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight()));
            }
            if (histogram != nullptr && histogram[b] > 0.001f)
            {
                g.setColour (colours::ledRed.withAlpha (0.75f));
                g.fillRect (Rectangle<float> (x0 + 1.0f, r.getY(), x1 - x0 - 2.0f, jlimit (0.0f, 1.0f, histogram[b]) * r.getHeight() * 0.35f));
            }
        }

    if (otherCompositeDb)
        drawCurve (g, otherCompositeDb, colours::sidechain.withAlpha (0.8f), 1.6f, false);
    if (selected >= 0 && bandDb)
    {
        const Band b = getBand (selected);
        if (b.on)
            drawCurve (g, [this] (float hz) { return bandDb (selected, hz); }, b.colour.withAlpha (0.9f), 1.5f, true);
    }
    if (compositeDb)
        drawCurve (g, compositeDb, colours::ink, 2.2f, false);
    if (dynamicDb)
        drawCurve (g, dynamicDb, colours::lcd, 1.6f, false);

    for (int i = 0; i < numBands; ++i)
    {
        const Band b = getBand (i);
        if (! b.on) continue;
        const auto p = nodePos (b);
        const bool sel = i == selected || std::find (multiSelected.begin(), multiSelected.end(), i) != multiSelected.end();
        const float d = sel ? 15.0f : 11.0f;
        g.setColour (b.colour);
        g.fillEllipse (Rectangle<float> (d, d).withCentre (p));
        g.setColour (sel ? colours::ink : colours::black);
        g.drawEllipse (Rectangle<float> (d, d).withCentre (p), 1.5f);
        drawText (g, String (i + 1), Rectangle<float> (16.0f, 12.0f).withCentre (p), 9.0f, colours::black, true);
        if (b.dynamic)
        {
            // Where the dynamic gain is now, and the UP/DOWN arrows.
            const float yd = yForGain (b.dynGainDb);
            g.setColour (b.colour.withAlpha (0.8f));
            g.drawLine (p.x, p.y, p.x, yd, 2.0f);
            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre ({ p.x, yd }));
            for (int k = 0; k < 2; ++k)
            {
                const bool upArrow = k == 0;
                const auto a = arrowRect (b, upArrow);
                Path tri;
                if (upArrow) tri.addTriangle (a.getX(), a.getBottom(), a.getRight(), a.getBottom(), a.getCentreX(), a.getY());
                else tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
                const bool active = upArrow == b.up;
                g.setColour (active ? colours::lcd : colours::inkDim.withAlpha (0.6f));
                g.fillPath (tri);
            }
        }
        if (sel && b.hasQ && i == selected)
        {
            // Q handles left and right of the node.
            const float w = jlimit (14.0f, 120.0f, 60.0f / jmax (0.2f, b.q));
            g.setColour (b.colour.withAlpha (0.8f));
            g.drawLine (p.x - w, p.y, p.x - 10.0f, p.y, 1.0f);
            g.drawLine (p.x + 10.0f, p.y, p.x + w, p.y, 1.0f);
            g.fillRect (Rectangle<float> (4.0f, 12.0f).withCentre ({ p.x - w, p.y }));
            g.fillRect (Rectangle<float> (4.0f, 12.0f).withCentre ({ p.x + w, p.y }));
        }
        if (i == selected)
            drawText (g, ids::hertz (b.hz) + "  " + noteName (b.hz) + (b.hasGain ? "  " + ids::decibels (b.gainDb) : String()),
                      { p.x - 90.0f, p.y - 34.0f, 180.0f, 14.0f }, 10.0f, colours::ink, true);
    }
    if (boxSelecting)
    {
        g.setColour (colours::ink.withAlpha (0.15f));
        g.fillRect (box);
        g.setColour (colours::ink.withAlpha (0.6f));
        g.drawRect (box, 1.0f);
    }
    if (hovering && nodeAt (hover) < 0 && dragging < 0 && onAdd && ! boxSelecting)
    {
        bool free = false;
        for (int i = 0; i < numBands; ++i) if (! getBand (i).on) free = true;
        if (free)
        {
            const float y = compositeDb ? yForGain (compositeDb (hzForX (hover.x))) : yForGain (0.0f);
            if (std::abs (hover.y - y) < 24.0f)
            {
                const auto a = Rectangle<float> (18.0f, 18.0f).withCentre ({ hover.x, y - 16.0f });
                g.setColour (colours::cream);
                g.fillEllipse (a);
                drawText (g, "+", a, 13.0f, colours::black, true);
            }
        }
    }
}

void EqCurveView::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    if (histogram != nullptr && p.y < plotArea().getY() + plotArea().getHeight() * 0.35f && onHistogramClick && nodeAt (p) < 0)
    {
        bool any = false;
        for (int b = 0; b < Masking::kBands; ++b) if (histogram[b] > 0.001f && xForHz (Masking::bandLowHz (b)) <= p.x && xForHz (Masking::bandHighHz (b)) >= p.x) any = true;
        if (any) { onHistogramClick(); return; }
    }
    // Dynamic arrows.
    if (selected >= 0)
    {
        const Band b = getBand (selected);
        if (b.on && b.dynamic && onDirection)
            for (int k = 0; k < 2; ++k)
                if (arrowRect (b, k == 0).expanded (3.0f).contains (p)) { onDirection (selected, k == 0); return; }
    }
    if (selected >= 0 && getBand (selected).on && getBand (selected).hasQ)
    {
        const Band b = getBand (selected);
        const auto np = nodePos (b);
        const float w = jlimit (14.0f, 120.0f, 60.0f / jmax (0.2f, b.q));
        if (std::abs (p.y - np.y) < 10.0f)
        {
            if (std::abs (p.x - (np.x - w)) < 8.0f) { dragging = selected; dragQ = -1; dragStart = p; startQ = b.q; return; }
            if (std::abs (p.x - (np.x + w)) < 8.0f) { dragging = selected; dragQ = 1; dragStart = p; startQ = b.q; return; }
        }
    }
    const int n = nodeAt (p);
    if (n >= 0)
    {
        if (e.mods.isShiftDown())
        {
            auto it = std::find (multiSelected.begin(), multiSelected.end(), n);
            if (it == multiSelected.end()) multiSelected.push_back (n); else multiSelected.erase (it);
        }
        else multiSelected.clear();
        selected = n;
        if (onSelect) onSelect (n);
        if (e.mods.isAltDown() && onAltSolo) { altSoloing = true; onAltSolo (getBand (n).hz, true); }
        dragging = n; dragQ = 0; dragStart = p;
        repaint();
        return;
    }
    // The + over the curve: add a band.
    if (hovering && onAdd)
    {
        const float y = compositeDb ? yForGain (compositeDb (hzForX (hover.x))) : yForGain (0.0f);
        const auto a = Rectangle<float> (18.0f, 18.0f).withCentre ({ hover.x, y - 16.0f });
        if (a.contains (p))
        {
            for (int i = 0; i < numBands; ++i)
                if (! getBand (i).on) { onAdd (i, hzForX (p.x)); selected = i; if (onSelect) onSelect (i); break; }
            return;
        }
    }
    if (e.mods.isAltDown() && onAltSolo) { altSoloing = true; onAltSolo (hzForX (p.x), true); return; }
    // Drag a box to select several nodes.
    boxSelecting = true;
    dragStart = p;
    box = {};
    multiSelected.clear();
}

void EqCurveView::mouseDrag (const MouseEvent& e)
{
    if (altSoloing && dragging < 0 && onAltSolo) { onAltSolo (hzForX (e.position.x), true); return; }
    if (boxSelecting)
    {
        box = Rectangle<float> (dragStart, e.position);
        multiSelected.clear();
        for (int i = 0; i < numBands; ++i)
        {
            const Band b = getBand (i);
            if (b.on && box.contains (nodePos (b))) multiSelected.push_back (i);
        }
        repaint();
        return;
    }
    if (dragging < 0) return;
    const Band b = getBand (dragging);
    if (dragQ != 0)
    {
        const float dx = (e.position.x - dragStart.x) * (float) dragQ;
        const float q = jlimit (0.1f, 40.0f, startQ * std::pow (2.0f, -dx / 40.0f));
        if (setBandQ) setBandQ (dragging, q);
        return;
    }
    float hz = hzForX (e.position.x), gain = gainForY (e.position.y);
    if (e.mods.isShiftDown())
    {
        if (std::abs (e.getDistanceFromDragStartX()) > std::abs (e.getDistanceFromDragStartY())) gain = b.gainDb; else hz = b.hz;
    }
    if (! b.hasGain) gain = b.gainDb;
    if (setBandFreqGain) setBandFreqGain (dragging, jlimit (20.0f, 20000.0f, hz), jlimit (gainBottom, gainTop, gain));
    if (altSoloing && onAltSolo) onAltSolo (hz, true);
}

void EqCurveView::mouseUp (const MouseEvent&)
{
    dragging = -1; dragQ = 0;
    boxSelecting = false;
    if (altSoloing && onAltSolo) { altSoloing = false; onAltSolo (-1.0f, false); }
    repaint();
}

void EqCurveView::mouseMove (const MouseEvent& e) { hover = e.position; hovering = true; repaint(); }
void EqCurveView::mouseExit (const MouseEvent&) { hovering = false; repaint(); }

void EqCurveView::mouseDoubleClick (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n >= 0) { if (onReset) onReset (n); return; }
    if (onAdd)
        for (int i = 0; i < numBands; ++i)
            if (! getBand (i).on) { onAdd (i, hzForX (e.position.x)); selected = i; if (onSelect) onSelect (i); break; }
}

bool EqCurveView::keyPressed (const KeyPress& k)
{
    if (k.getKeyCode() == KeyPress::returnKey && k.getModifiers().isCommandDown() && onAdd)
    {
        for (int i = 0; i < numBands; ++i)
            if (! getBand (i).on) { onAdd (i, 1000.0f); selected = i; if (onSelect) onSelect (i); return true; }
        return true;
    }
    if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && onRemove)
    {
        if (! multiSelected.empty()) { for (int i : multiSelected) onRemove (i); multiSelected.clear(); return true; }
        if (selected >= 0 && getBand (selected).on) { onRemove (selected); return true; }
        return false;
    }
    if (selected < 0 || ! getBand (selected).on) return false;
    const Band b = getBand (selected);
    const float coarse = k.getModifiers().isShiftDown() ? 4.0f : k.getModifiers().isCommandDown() ? 0.25f : 1.0f;
    if (k.getModifiers().isAltDown() && setBandQ)
    {
        if (k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::rightKey)) { setBandQ (selected, jlimit (0.1f, 40.0f, b.q * std::pow (2.0f, 0.1f * coarse))); return true; }
        if (k.isKeyCode (KeyPress::downKey) || k.isKeyCode (KeyPress::leftKey)) { setBandQ (selected, jlimit (0.1f, 40.0f, b.q / std::pow (2.0f, 0.1f * coarse))); return true; }
    }
    if (! setBandFreqGain) return false;
    if (k.isKeyCode (KeyPress::upKey))    { setBandFreqGain (selected, b.hz, jlimit (gainBottom, gainTop, b.gainDb + 0.5f * coarse)); return true; }
    if (k.isKeyCode (KeyPress::downKey))  { setBandFreqGain (selected, b.hz, jlimit (gainBottom, gainTop, b.gainDb - 0.5f * coarse)); return true; }
    if (k.isKeyCode (KeyPress::leftKey))  { setBandFreqGain (selected, jlimit (20.0f, 20000.0f, b.hz / std::pow (2.0f, coarse / 24.0f)), b.gainDb); return true; }
    if (k.isKeyCode (KeyPress::rightKey)) { setBandFreqGain (selected, jlimit (20.0f, 20000.0f, b.hz * std::pow (2.0f, coarse / 24.0f)), b.gainDb); return true; }
    return false;
}

// ===========================================================================
// DetectionFilterView
// ===========================================================================
DetectionFilterView::DetectionFilterView (ModulePanel& p, const DetectionParams& d) : panel (p), det (d)
{
    showGainScale = true;
    gainTop = 18.0f; gainBottom = -36.0f;
}

Point<float> DetectionFilterView::node (int which) const
{
    const float hz = panel.pv (which == 0 ? det.pHpFreq : det.pLpFreq);
    const float db = panel.pv (which == 0 ? det.pHpGain : det.pLpGain);
    return { xForHz (hz), yForGain (db) };
}

void DetectionFilterView::paintOverlay (Graphics& g)
{
    const bool on = panel.pv (det.pOn) > 0.5f;
    DetectionFilter f;
    const double sr = panel.processor().getSampleRate() > 0 ? panel.processor().getSampleRate() : 48000.0;
    f.design (panel.pv (det.pHpFreq), panel.pv (det.pHpGain), panel.pv (det.pLpFreq), panel.pv (det.pLpGain), sr);
    drawCurve (g, [&] (float hz) { return (float) f.responseDb (hz, sr); }, on ? colours::lcd : colours::inkDim, 2.0f, true);
    for (int i = 0; i < 2; ++i)
    {
        const auto p = node (i);
        g.setColour (on ? colours::lcd : colours::inkDim);
        g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (p));
        g.setColour (colours::black);
        g.drawEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (p), 1.5f);
        drawText (g, i == 0 ? "HP" : "LP", { p.x - 20.0f, p.y - 26.0f, 40.0f, 12.0f }, 10.0f, colours::ink, true);
        drawText (g, ids::hertz (panel.pv (i == 0 ? det.pHpFreq : det.pLpFreq)), { p.x - 40.0f, p.y + 10.0f, 80.0f, 12.0f }, 9.0f, colours::inkDim, true);
    }
    if (! on)
        drawText (g, "DETECTION FILTER OFF: switch it on with the button above", plotArea().reduced (20.0f).removeFromTop (20.0f), 12.0f, colours::inkDim, true);
}

void DetectionFilterView::mouseDown (const MouseEvent& e)
{
    dragging = -1;
    for (int i = 0; i < 2; ++i)
        if (node (i).getDistanceFrom (e.position) < 12.0f) dragging = i;
}

void DetectionFilterView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const float hz = jlimit (20.0f, 20000.0f, hzForX (e.position.x));
    const float db = jlimit (-12.0f, 18.0f, gainForY (e.position.y));
    if (dragging == 0) { panel.setp (det.pHpFreq, jmin (hz, panel.pv (det.pLpFreq) / 1.2f)); panel.setp (det.pHpGain, db); }
    else { panel.setp (det.pLpFreq, jmax (hz, panel.pv (det.pHpFreq) * 1.2f)); panel.setp (det.pLpGain, db); }
}

// ===========================================================================
// TraceView
// ===========================================================================
void TraceView::reset()
{
    inWave.fill (0.0f); outWave.fill (0.0f); scWave.fill (0.0f);
    for (auto& l : lanes) l.values.fill (0.0f);
}

void TraceView::push (float inPeak, float outPeak, const float* laneValues, float scPeak)
{
    inWave[(size_t) writePos] = inPeak;
    outWave[(size_t) writePos] = outPeak;
    scWave[(size_t) writePos] = scPeak;
    for (size_t i = 0; i < lanes.size(); ++i)
        lanes[i].values[(size_t) writePos] = laneValues[i];
    writePos = (writePos + 1) % kHistory;
    repaint();
}

float TraceView::yForLevel (float db) const
{
    const auto plot = getLocalBounds().toFloat().reduced (1.0f);
    const float lin = jlimit (0.0f, 1.0f, dbToGain (db));
    return plot.getCentreY() - lin * plot.getHeight() * 0.48f;
}

float TraceView::levelForY (float y) const
{
    const auto plot = getLocalBounds().toFloat().reduced (1.0f);
    const float lin = jlimit (0.0001f, 1.0f, (plot.getCentreY() - y) / (plot.getHeight() * 0.48f));
    return gainToDb (lin);
}

void TraceView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (1.0f);
    const float colW = plot.getWidth() / (float) kHistory;
    auto drawWave = [&] (const std::array<float, kHistory>& w, Colour c)
    {
        g.setColour (c);
        for (int i = 0; i < kHistory; ++i)
        {
            const float v = w[(size_t) ((writePos + i) % kHistory)];
            const float h = jlimit (0.0f, 1.0f, v) * plot.getHeight() * 0.48f;
            const float x = plot.getX() + (float) i * colW;
            g.fillRect (Rectangle<float> (x, plot.getCentreY() - h, jmax (1.0f, colW), 2.0f * h));
        }
    };
    drawWave (inWave, colours::spectrum.withAlpha (0.45f));
    if (showSidechain) drawWave (scWave, colours::sidechain.withAlpha (0.35f));
    drawWave (outWave, colours::ink.withAlpha (0.55f));

    // Threshold lines, mirrored.
    for (int t = 0; t < 2; ++t)
    {
        if (thresholdDb[(size_t) t] < -500.0f) continue;
        const float y = yForLevel (thresholdDb[(size_t) t]), y2 = plot.getCentreY() * 2.0f - y;
        g.setColour (t == 0 ? colours::ink : colours::ink.withAlpha (0.7f));
        if (t == 0) { g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight()); g.drawHorizontalLine ((int) y2, plot.getX(), plot.getRight()); }
        else
        {
            const float dash[] { 5.0f, 4.0f };
            g.drawDashedLine (Line<float> (plot.getX(), y, plot.getRight(), y), dash, 2, 1.0f);
            g.drawDashedLine (Line<float> (plot.getX(), y2, plot.getRight(), y2), dash, 2, 1.0f);
        }
        drawText (g, String (thresholdDb[(size_t) t], 1) + " dB", { plot.getX() + 4.0f, y - 14.0f, 80.0f, 12.0f }, 9.0f, colours::ink, true, Justification::centredLeft);
    }

    // Gain traces: from the top, down for reduction (or up and down from the middle).
    for (size_t li = 0; li < lanes.size(); ++li)
    {
        const auto& lane = lanes[li];
        Path p;
        for (int i = 0; i < kHistory; ++i)
        {
            const float v = lane.values[(size_t) ((writePos + i) % kHistory)];
            const float x = plot.getX() + (float) i * colW;
            float y;
            if (bipolar) y = plot.getCentreY() - jlimit (-rangeDb, rangeDb, v) / rangeDb * plot.getHeight() * 0.48f;
            else y = plot.getY() + jlimit (0.0f, rangeDb, -v) / rangeDb * plot.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const bool dim = highlight >= 0 && (int) li != highlight;
        g.setColour (lane.colour.withAlpha (dim ? 0.3f : 1.0f));
        g.strokePath (p, PathStrokeType (dim ? 1.0f : 1.8f));
    }
    // Scale.
    if (! bipolar)
        for (float db = 0.0f; db <= rangeDb; db += rangeDb / 4.0f)
        {
            const float y = plot.getY() + db / rangeDb * plot.getHeight() * 0.5f;
            g.setColour (colours::grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            drawText (g, String ((int) -db), { plot.getRight() - 30.0f, y - 6.0f, 28.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
        }
    else
        for (float db = -rangeDb; db <= rangeDb; db += rangeDb / 2.0f)
        {
            const float y = plot.getCentreY() - db / rangeDb * plot.getHeight() * 0.48f;
            g.setColour (db == 0.0f ? colours::grid.brighter (0.5f) : colours::grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            drawText (g, (db > 0 ? "+" : "") + String ((int) db), { plot.getRight() - 30.0f, y - 6.0f, 28.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredRight);
        }
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

void TraceView::mouseDown (const MouseEvent& e)
{
    dragging = -1;
    float best = 10.0f;
    for (int t = 0; t < 2; ++t)
    {
        if (thresholdDb[(size_t) t] < -500.0f) continue;
        const float y = yForLevel (thresholdDb[(size_t) t]), y2 = getLocalBounds().toFloat().reduced (1.0f).getCentreY() * 2.0f - y;
        const float d = jmin (std::abs (e.position.y - y), std::abs (e.position.y - y2));
        if (d < best) { best = d; dragging = t; }
    }
}

void TraceView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0 || ! onDragThreshold) return;
    const float cy = getLocalBounds().toFloat().reduced (1.0f).getCentreY();
    const float y = e.position.y < cy ? e.position.y : cy * 2.0f - e.position.y;
    onDragThreshold (dragging, jlimit (-80.0f, 0.0f, levelForY (y)));
}

// ===========================================================================
// Oscilloscope
// ===========================================================================
void Oscilloscope::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (2.0f);
    g.setColour (colours::grid);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    // Beat lines.
    const int beats = barFraction >= 1.0 ? 4 : barFraction >= 0.5 ? 2 : 1;
    for (int b = 1; b < beats * 4; ++b)
    {
        const float x = plot.getX() + plot.getWidth() * (float) b / (float) (beats * 4);
        g.setColour (b % 4 == 0 ? colours::grid.brighter (0.4f) : colours::grid.withAlpha (0.5f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }
    if (recorder == nullptr) return;
    const double window = 4.0 * barFraction;              // quarter notes on screen
    const double now = ppqNow ? ppqNow() : 0.0;
    const double start = std::floor (now / window) * window;
    const int end = recorder->position();
    const int count = jmin (ScopeRecorder::kSize - 1, end);
    // Columns hold min/max of what was recorded at their bar position, newest wins.
    const int w = jmax (1, (int) plot.getWidth());
    std::vector<float> lo ((size_t) w, 0.0f), hi ((size_t) w, 0.0f), slo ((size_t) w, 0.0f), shi ((size_t) w, 0.0f);
    std::vector<char> has ((size_t) w, 0);
    for (int i = 0; i < count; ++i)
    {
        const auto& e = recorder->at (end - 1 - i);
        double pos = e.ppq - start;
        if (pos < 0.0) pos += window;          // the previous pass, still on screen to the right of the cursor
        if (pos < 0.0 || pos >= window) { if (e.ppq < start - window) break; else continue; }
        const int x = jlimit (0, w - 1, (int) (pos / window * w));
        if (has[(size_t) x]) continue;
        has[(size_t) x] = 1;
        lo[(size_t) x] = e.lo; hi[(size_t) x] = e.hi; slo[(size_t) x] = e.scLo; shi[(size_t) x] = e.scHi;
    }
    auto yOf = [&] (float v) { return plot.getCentreY() - jlimit (-1.0f, 1.0f, v) * plot.getHeight() * 0.48f; };
    for (int x = 0; x < w; ++x)
    {
        if (! has[(size_t) x]) continue;
        const float px = plot.getX() + (float) x;
        if (type == Main || type == MainAndSidechain)
        {
            g.setColour (colours::ink.withAlpha (0.75f));
            g.fillRect (Rectangle<float> (px, yOf (hi[(size_t) x]), 1.0f, jmax (1.0f, yOf (lo[(size_t) x]) - yOf (hi[(size_t) x]))));
        }
        if (type == Sidechain || type == MainAndSidechain)
        {
            g.setColour (colours::sidechain.withAlpha (type == Sidechain ? 0.9f : 0.6f));
            g.fillRect (Rectangle<float> (px, yOf (shi[(size_t) x]), 1.0f, jmax (1.0f, yOf (slo[(size_t) x]) - yOf (shi[(size_t) x]))));
        }
        if (type == Combined)
        {
            g.setColour (colours::sidechain.brighter (0.3f));
            const float a = hi[(size_t) x] + shi[(size_t) x], b = lo[(size_t) x] + slo[(size_t) x];
            g.fillRect (Rectangle<float> (px, yOf (a), 1.0f, jmax (1.0f, yOf (b) - yOf (a))));
        }
    }
    // Play cursor.
    const float cx = plot.getX() + (float) ((now - start) / window) * plot.getWidth();
    g.setColour (colours::lcd.withAlpha (0.7f));
    g.drawVerticalLine ((int) cx, plot.getY(), plot.getBottom());
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// DifferenceView
// ===========================================================================
DifferenceView::DifferenceView (ModulePanel& p) : panel (p)
{
    showGainScale = true;
    gainTop = 12.0f; gainBottom = -18.0f;
}

void DifferenceView::paintOverlay (Graphics& g)
{
    const auto r = plotArea();
    auto bandPath = [&] (const float* v, float offset, float scale)
    {
        Path p;
        for (int b = 0; b < SpectralBands::kCount; ++b)
        {
            if (v[b] <= -100.0f) continue;   // nothing in this band yet
            const float x = xForHz (SpectralBands::centreHz (b));
            const float y = jlimit (r.getY(), r.getBottom(), yForGain ((v[b] - offset) * scale));
            if (p.isEmpty()) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };
    auto meanOf = [] (const float* v)
    {
        float mean = 0.0f; int cnt = 0;
        for (int b = 0; b < SpectralBands::kCount; ++b) if (v[b] > -100.0f) { mean += v[b]; ++cnt; }
        return cnt > 0 ? mean / (float) cnt : 0.0f;
    };
    const float specMean = specDb != nullptr ? meanOf (specDb) : 0.0f;
    if (targetDb)
    {
        // The target, fitted to the spectrum's level, as a band either side.
        float tMean = 0.0f;
        for (int b = 0; b < SpectralBands::kCount; ++b) tMean += targetDb (SpectralBands::centreHz (b));
        tMean /= (float) SpectralBands::kCount;
        Path band;
        for (int i = 0; i <= 80; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / 80.0f);
            const float y = yForGain ((targetDb (hz) - tMean) * 0.5f + 2.0f);
            if (i == 0) band.startNewSubPath (xForHz (hz), y); else band.lineTo (xForHz (hz), y);
        }
        for (int i = 80; i >= 0; --i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / 80.0f);
            band.lineTo (xForHz (hz), yForGain ((targetDb (hz) - tMean) * 0.5f - 2.0f));
        }
        band.closeSubPath();
        g.setColour (colours::cream.withAlpha (0.12f));
        g.fillPath (band);
    }
    if (scDb != nullptr)
    {
        g.setColour (colours::sidechain.withAlpha (0.8f));
        g.strokePath (bandPath (scDb, meanOf (scDb), 0.5f), PathStrokeType (1.4f));
    }
    // (The module's own spectrum is the analyser behind; specDb only sets the level.)
    juce::ignoreUnused (specMean);
    if (gainDb != nullptr)
    {
        Path p, f;
        for (int b = 0; b < SpectralBands::kCount; ++b)
        {
            const float x = xForHz (SpectralBands::centreHz (b));
            const float y = jlimit (r.getY(), r.getBottom(), yForGain (gainDb[b]));
            if (b == 0) { p.startNewSubPath (x, y); f.startNewSubPath (x, yForGain (0.0f)); f.lineTo (x, y); }
            else { p.lineTo (x, y); f.lineTo (x, y); }
        }
        f.lineTo (xForHz (SpectralBands::centreHz (SpectralBands::kCount - 1)), yForGain (0.0f));
        f.closeSubPath();
        g.setColour (colour.withAlpha (0.22f));
        g.fillPath (f);
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved));
    }
    if (lowParam >= 0 && highParam >= 0)
    {
        const float lo = panel.pv (lowParam), hi = panel.pv (highParam);
        const float x0 = xForHz (lo), x1 = xForHz (hi);
        g.setColour (colours::black.withAlpha (0.55f));
        g.fillRect (Rectangle<float> (r.getX(), r.getY(), x0 - r.getX(), r.getHeight()));
        g.fillRect (Rectangle<float> (x1, r.getY(), r.getRight() - x1, r.getHeight()));
        g.setColour (colours::ink);
        g.drawLine (x0, r.getY(), x0, r.getBottom(), 1.5f);
        g.drawLine (x1, r.getY(), x1, r.getBottom(), 1.5f);
        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? x0 : x1;
            const auto h = Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getBottom() - 30.0f });
            g.setColour (colours::cream);
            g.fillEllipse (h);
            drawText (g, ids::hertz (i == 0 ? lo : hi), { jlimit (r.getX() + 2.0f, r.getRight() - 82.0f, x - 40.0f), r.getBottom() - 54.0f, 80.0f, 14.0f }, 10.0f, colours::ink, true);
        }
    }
}

void DifferenceView::mouseDown (const MouseEvent& e)
{
    if (lowParam < 0) return;
    const float x0 = xForHz (panel.pv (lowParam)), x1 = xForHz (panel.pv (highParam));
    if (std::abs (e.position.x - x0) < 8.0f) dragging = 0;
    else if (std::abs (e.position.x - x1) < 8.0f) dragging = 1;
    else if (e.position.x > x0 && e.position.x < x1) { dragging = 2; dragOffsetOct = std::log2 (hzForX (e.position.x) / panel.pv (lowParam)); }
    else dragging = -1;
}

void DifferenceView::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const float hz = jlimit (20.0f, 20000.0f, hzForX (e.position.x));
    const float lo = panel.pv (lowParam), hi = panel.pv (highParam);
    if (dragging == 0) panel.setp (lowParam, jmin (hz, hi / 1.1f));
    else if (dragging == 1) panel.setp (highParam, jmax (hz, lo * 1.1f));
    else
    {
        const float span = std::log2 (hi / lo);
        float newLo = hz / std::pow (2.0f, dragOffsetOct);
        newLo = jlimit (20.0f, 20000.0f / std::pow (2.0f, span), newLo);
        panel.setp (lowParam, newLo);
        panel.setp (highParam, newLo * std::pow (2.0f, span));
    }
}

// ===========================================================================
// XYPad
// ===========================================================================
void XYPad::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    drawLcdBox (g, r);
    const auto inner = r.reduced (10.0f);
    g.setColour (colours::grid);
    g.drawVerticalLine ((int) inner.getCentreX(), inner.getY(), inner.getBottom());
    g.drawHorizontalLine ((int) inner.getCentreY(), inner.getX(), inner.getRight());
    drawText (g, corners[0], { r.getX() + 4.0f, r.getY() + 2.0f, 90.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredLeft);
    drawText (g, corners[1], { r.getRight() - 94.0f, r.getY() + 2.0f, 90.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredRight);
    drawText (g, corners[2], { r.getX() + 4.0f, r.getBottom() - 14.0f, 90.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredLeft);
    drawText (g, corners[3], { r.getRight() - 94.0f, r.getBottom() - 14.0f, 90.0f, 12.0f }, 9.0f, colours::inkDim, true, Justification::centredRight);
    if (! get) return;
    const auto [x, y] = get();
    const Point<float> p (inner.getX() + x * inner.getWidth(), inner.getBottom() - y * inner.getHeight());
    g.setColour (colour.withAlpha (0.25f));
    g.fillEllipse (Rectangle<float> (34.0f, 34.0f).withCentre (p));
    g.setColour (colour);
    g.fillEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (p));
    g.setColour (colours::black);
    g.drawEllipse (Rectangle<float> (14.0f, 14.0f).withCentre (p), 1.5f);
}

void XYPad::mouseDrag (const MouseEvent& e)
{
    if (! set) return;
    const auto inner = getLocalBounds().toFloat().reduced (10.0f);
    set (jlimit (0.0f, 1.0f, (e.position.x - inner.getX()) / inner.getWidth()),
         jlimit (0.0f, 1.0f, (inner.getBottom() - e.position.y) / inner.getHeight()));
    repaint();
}

// ===========================================================================
// DynamicsCurveView
// ===========================================================================
void DynamicsCurveView::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::black);
    g.fillRect (r);
    const auto plot = r.reduced (4.0f);
    auto xFor = [&] (float db) { return plot.getX() + (db + rangeDb) / rangeDb * plot.getWidth(); };
    auto yFor = [&] (float db) { return plot.getBottom() - (db + rangeDb) / rangeDb * plot.getHeight(); };
    g.setColour (colours::grid);
    for (float db = -rangeDb; db <= 0.0f; db += rangeDb / 6.0f)
    {
        g.drawVerticalLine ((int) xFor (db), plot.getY(), plot.getBottom());
        g.drawHorizontalLine ((int) yFor (db), plot.getX(), plot.getRight());
    }
    g.setColour (colours::grid.brighter (0.5f));
    g.drawLine (xFor (-rangeDb), yFor (-rangeDb), xFor (0.0f), yFor (0.0f), 1.0f);
    if (curve)
    {
        Path p;
        const int steps = 80;
        for (int i = 0; i <= steps; ++i)
        {
            const float in = -rangeDb + (float) i / (float) steps * rangeDb;
            const float out = jlimit (-rangeDb - 20.0f, 12.0f, in + curve (in));
            const float x = xFor (in), y = jlimit (plot.getY(), plot.getBottom(), yFor (out));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (2.0f));
        if (inputDb > -rangeDb)
        {
            const float x = xFor (jmin (0.0f, inputDb)), y = jlimit (plot.getY(), plot.getBottom(), yFor (jmin (0.0f, inputDb) + gainDb));
            g.setColour (colours::ink);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));
        }
    }
    drawText (g, "IN", { plot.getRight() - 24.0f, plot.getBottom() - 14.0f, 22.0f, 12.0f }, 9.0f, colours::inkDim, false);
    drawText (g, "OUT", { plot.getX() + 2.0f, plot.getY(), 30.0f, 12.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}

// ===========================================================================
// ThresholdMeter
// ===========================================================================
void ThresholdMeter::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto yFor = [&] (float db) { return r.getBottom() - jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * h; };
    g.setColour (colours::black);
    g.fillRect (r);
    g.setColour (colours::inkDim.withAlpha (0.5f));
    g.fillRect (Rectangle<float> (r.getX(), yFor (inputDb), 4.0f, r.getBottom() - yFor (inputDb)));
    g.fillRect (Rectangle<float> (r.getRight() - 4.0f, yFor (inputDb), 4.0f, r.getBottom() - yFor (inputDb)));
    g.setColour (colour);
    if (gainDb < 0.0f)
        g.fillRect (Rectangle<float> (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, jlimit (0.0f, h, -gainDb / -floorDb * h)));
    else if (gainDb > 0.0f)
        g.fillRect (Rectangle<float> (r.getX() + 6.0f, r.getBottom() - jlimit (0.0f, h, gainDb / -floorDb * h), r.getWidth() - 12.0f, jlimit (0.0f, h, gainDb / -floorDb * h)));
    g.setColour (colours::inkDim);
    g.drawRect (r, 1.0f);
}
} // namespace planish::ui
