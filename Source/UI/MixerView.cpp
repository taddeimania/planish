#include "MixerView.h"

namespace planish::ui
{
using namespace juce;

namespace
{
constexpr float kGainTop = 12.0f, kGainBottom = -36.0f;
constexpr float kRowH = 28.0f;
// Where each group sits, in LU, against the focus; the focus lands at -18 LUFS.
const float kGroupLevel[MixerView::NumGroups] { 0.0f, -3.0f, -5.0f, -4.0f, -8.0f };
constexpr float kFocusLufs = -18.0f;
}

const char* MixerView::groupName (int g)
{
    static const char* n[NumGroups] { "FOCUS", "VOICE", "BASS", "PERCUSSION", "MUSICAL" };
    return n[jlimit (0, NumGroups - 1, g)];
}

MixerView::MixerView (PlanishProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    setSize (1104, 796);
    auto button = [this] (PanelButton& b, Rectangle<int> r, float font, const String& tip, std::function<void (const ModifierKeys&)> click)
    {
        b.setFontHeight (font);
        b.setTooltip (tip);
        b.onClick = std::move (click);
        addAndMakeVisible (b);
        b.setBounds (r);
    };
    button (assistBtn, { 790, 690, 300, 40 }, 13.0f, "MIX ASSISTANT: listens to the song through every Planish and sets a starting balance of levels by group.",
            [this] (const ModifierKeys&) { setStage (stage == Off ? Setup : Off); if (stage == Off) restoreOriginals(); });
    button (closeBtn, { 954, 742, 136, 40 }, 11.0f, "Back to the modules.", [this] (const ModifierKeys&) { if (stage != Off) { restoreOriginals(); setStage (Off); } if (onClose) onClose(); });
    button (setBtn, { 790, 600, 70, 34 }, 11.0f, "SET, then A, B or C: store the mix (gains, pans, widths and what's hidden) there.", [this] (const ModifierKeys&) { armedSnapshot = armedSnapshot >= 0 ? -1 : 0; });
    for (int i = 0; i < 3; ++i)
        button (snapBtn[i], { 870 + i * 74, 600, 70, 34 }, 12.0f, "Mix snapshot " + String::charToString ((juce_wchar) ('A' + i)) + ": recall it (after SET, store it).",
                [this, i] (const ModifierKeys& m) { if (armedSnapshot >= 0 || m.isShiftDown()) { storeSnapshot (i); armedSnapshot = -1; } else recallSnapshot (i); });
    button (beginBtn, { 290, 690, 220, 40 }, 13.0f, "Start: then play the song from the beginning.", [this] (const ModifierKeys&)
    {
        bool anyFocus = false;
        for (auto& t : tracks) anyFocus = anyFocus || (t.include && t.focus);
        if (anyFocus) setStage (Waiting);
    });
    button (resultsBtn, { 290, 690, 220, 40 }, 13.0f, "Stop listening and see the balance.", [this] (const ModifierKeys&) { computeResults(); setStage (Results); applyResults (false); });
    button (bypassBtn, { 100, 690, 180, 40 }, 11.0f, "Hear the mix as it was (level matched), to compare.", [this] (const ModifierKeys&) { bypassed = ! bypassed; applyResults (bypassed); });
    button (acceptBtn, { 290, 690, 220, 40 }, 13.0f, "Keep these levels: they are written to each Planish's output gain.", [this] (const ModifierKeys&) { if (! bypassed) setStage (Off); });
    button (cancelBtn, { 520, 690, 160, 40 }, 11.0f, "Leave the levels as they were.", [this] (const ModifierKeys&) { restoreOriginals(); setStage (Off); });
    for (int g = 0; g < NumGroups; ++g)
    {
        auto& f = groupFader[g];
        f.caption = groupName (g);
        f.bipolar = true;
        f.setRange (-12.0, 12.0, 0.1);
        f.setValue (0.0, dontSendNotification);
        f.fill = colours::lcd;
        f.format = [] (double v) { return ids::decibels ((float) v); };
        f.onValueChange = [this] { if (stage == Results && ! bypassed) applyResults (false); };
        f.setTooltip ("Move the whole group against the others, up to 12 dB either way.");
        addAndMakeVisible (f);
        f.setBounds (60 + g * 86, 420, 60, 240);
    }
    setStage (Off);
}

MixerView::~MixerView()
{
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
float MixerView::yForGain (float g, Rectangle<float> pad)
{
    return pad.getY() + 20.0f + (kGainTop - jlimit (kGainBottom, kGainTop, g)) / (kGainTop - kGainBottom) * (pad.getHeight() - 40.0f);
}
float MixerView::gainForY (float y, Rectangle<float> pad)
{
    return jlimit (kGainBottom, kGainTop, kGainTop - (y - pad.getY() - 20.0f) / (pad.getHeight() - 40.0f) * (kGainTop - kGainBottom));
}

Point<float> MixerView::nodePos (PlanishProcessor& p) const
{
    const auto pad = padArea();
    const float pan = p.getParam (ids::pan);
    return { pad.getX() + 40.0f + (pan + 100.0f) / 200.0f * (pad.getWidth() - 80.0f), yForGain (p.getParam (ids::outGain), pad) };
}

float MixerView::widthHandle (PlanishProcessor& p) const
{
    return 16.0f + (p.getParam (ids::width) + 100.0f) / 200.0f * 60.0f;
}

PlanishProcessor* MixerView::nodeAt (Point<float> pt, int& part) const
{
    for (auto* p : LinkRegistry::get().all())
    {
        if (p->mixerHidden) continue;
        const auto c = nodePos (*p);
        const float w = widthHandle (*p);
        if (p->isStereo())
        {
            if (Point<float> (c.x - w, c.y).getDistanceFrom (pt) < 7.0f) { part = -1; return p; }
            if (Point<float> (c.x + w, c.y).getDistanceFrom (pt) < 7.0f) { part = 1; return p; }
        }
        if (c.getDistanceFrom (pt) < 14.0f) { part = 0; return p; }
    }
    return nullptr;
}

int MixerView::defaultGroup (PlanishProcessor& p)
{
    if (p.mixGroup >= 0) return p.mixGroup;
    int t = -1;
    if (p.assistant.hasResult()) t = p.assistant.detected();
    else if (p.chain.inChain (ModSculptor)) t = (int) p.getParam ("scu.target");
    if (t >= 0 && t < InstrumentTargets::NumTargets)
    {
        const String cat (InstrumentTargets::category (t));
        if (cat == "VOICE") return Voice;
        if (cat == "DRUMS") return Percussion;
        if (cat == "BASS") return Bass;
        return Musical;
    }
    const auto n = p.instanceName().toLowerCase();
    if (n.contains ("vox") || n.contains ("vocal") || n.contains ("voice")) return Voice;
    if (n.contains ("bass") || n.contains ("sub")) return Bass;
    for (auto* k : { "kick", "snare", "drum", "hat", "perc", "tom", "cymbal", "clap", "overhead" })
        if (n.contains (k)) return Percussion;
    return Musical;
}

Colour MixerView::nodeColour (PlanishProcessor& p) const
{
    const auto c = p.trackColour();
    if (c.getAlpha() > 0) return c.withAlpha (1.0f);
    return colours::node (p.instanceId() % 12);
}

// ---------------------------------------------------------------------------
void MixerView::setStage (Stage s)
{
    if (s == Setup) buildTracks();
    if (s == Waiting) bypassed = false;
    stage = s;
    const bool mix = s == Off;
    showAll (mix, setBtn, snapBtn[0], snapBtn[1], snapBtn[2]);
    assistBtn.setLit (! mix);
    beginBtn.setVisible (s == Setup);
    resultsBtn.setVisible (s == Listening);
    showAll (s == Results, bypassBtn, acceptBtn);
    cancelBtn.setVisible (s != Off);
    for (auto& f : groupFader) f.setVisible (s == Results);
    if (s == Results) for (auto& f : groupFader) f.setValue (0.0, dontSendNotification);
    repaint();
}

void MixerView::buildTracks()
{
    tracks.clear();
    for (auto* p : LinkRegistry::get().all())
    {
        Track t;
        t.id = p->instanceId();
        t.group = defaultGroup (*p);
        t.focus = t.group == Voice;
        t.originalGain = p->getParam (ids::outGain);
        t.newGain = t.originalGain;
        tracks.push_back (t);
    }
    // No voice: the first track is the focus.
    bool any = false;
    for (auto& t : tracks) any = any || t.focus;
    if (! any && ! tracks.empty()) tracks.front().focus = true;
}

void MixerView::computeResults()
{
    for (auto& t : tracks)
        if (auto* p = LinkRegistry::get().find (t.id))
            t.loudness = p->mixLoudness();
}

void MixerView::applyResults (bool bypass)
{
    if (stage != Results) return;
    // Each group's summed loudness lands at its level against the focus.
    double groupPower[NumGroups] {};
    for (auto& t : tracks)
    {
        if (! t.include || t.loudness < -69.0f) continue;
        const int g = t.focus ? Focus : t.group;
        groupPower[g] += std::pow (10.0, t.loudness / 10.0);
    }
    for (auto& t : tracks)
    {
        auto* p = LinkRegistry::get().find (t.id);
        if (p == nullptr) continue;
        if (bypass || ! t.include || t.loudness < -69.0f) { t.newGain = t.originalGain; p->setParam (ids::outGain, t.originalGain); continue; }
        const int g = t.focus ? Focus : t.group;
        const float groupLufs = (float) (10.0 * std::log10 (std::max (1e-12, groupPower[g])));
        const float target = kFocusLufs + kGroupLevel[g] + (float) groupFader[g].getValue();
        t.newGain = jlimit (-36.0f, 24.0f, t.originalGain + (target - groupLufs));
        p->setParam (ids::outGain, t.newGain);
    }
    repaint();
}

void MixerView::restoreOriginals()
{
    for (auto& t : tracks)
        if (auto* p = LinkRegistry::get().find (t.id))
            p->setParam (ids::outGain, t.originalGain);
}

void MixerView::storeSnapshot (int slot)
{
    auto& s = LinkRegistry::get().snapshots[slot];
    s.values.clear();
    for (auto* p : LinkRegistry::get().all())
        s.values[p->instanceId()] = { p->getParam (ids::outGain), p->getParam (ids::pan), p->getParam (ids::width), p->mixerHidden ? 1.0f : 0.0f };
    s.set = true;
}

void MixerView::recallSnapshot (int slot)
{
    const auto& s = LinkRegistry::get().snapshots[slot];
    if (! s.set) return;
    for (const auto& [id, v] : s.values)
        if (auto* p = LinkRegistry::get().find (id))
        {
            p->setParam (ids::outGain, v[0]); p->setParam (ids::pan, v[1]); p->setParam (ids::width, v[2]);
            p->mixerHidden = v[3] > 0.5f;
        }
}

// ---------------------------------------------------------------------------
void MixerView::refresh()
{
    for (auto* p : LinkRegistry::get().all())
    {
        const float a = p->takeActivity();
        auto& v = activity[p->instanceId()];
        v = jmax (a, v * 0.85f);
    }
    for (int i = 0; i < 3; ++i)
    {
        snapBtn[i].setDimmed (! LinkRegistry::get().snapshots[i].set);
        snapBtn[i].setLit (armedSnapshot >= 0);
    }
    setBtn.setLit (armedSnapshot >= 0);
    if (stage == Waiting && proc.hostPlaying())
    {
        for (auto& t : tracks)
            if (t.include)
                if (auto* p = LinkRegistry::get().find (t.id)) p->startMixListening();
        setStage (Listening);
    }
    bypassBtn.setLit (bypassed);
    acceptBtn.setEnabled (! bypassed);
    bool anyFocus = false;
    for (auto& t : tracks) anyFocus = anyFocus || (t.include && t.focus);
    beginBtn.setEnabled (anyFocus);
    repaint();
}

void MixerView::paint (Graphics& g)
{
    g.fillAll (colours::panel);
    const auto pad = padArea();
    const auto list = listArea();
    drawSection (g, pad.expanded (0.0f, 0.0f).withTrimmedTop (-14.0f).translated (0.0f, 0.0f), stage == Off ? "VISUAL MIXER" : "MIX ASSISTANT", stage == Off ? colours::ink : colours::lcd);
    drawSection (g, list.withTrimmedTop (-14.0f), "PLANISH IN THIS SESSION");
    drawSection (g, Rectangle<float> (790.0f, 586.0f, 300.0f, 60.0f), "MIX SNAPSHOTS");

    const auto all = LinkRegistry::get().all();
    // --- the list -------------------------------------------------------------------------
    {
        g.saveState();
        g.reduceClipRegion (list.toNearestInt());
        float y = list.getY() + 8.0f - listScroll;
        for (auto* p : all)
        {
            const auto row = Rectangle<float> (list.getX() + 6.0f, y, list.getWidth() - 12.0f, kRowH - 2.0f);
            g.setColour (p == &proc ? colours::socket : colours::black);
            g.fillRect (row);
            // Eye.
            const auto eye = row.withWidth (24.0f).reduced (4.0f, 7.0f);
            g.setColour (p->mixerHidden ? colours::inkFaint : colours::ink);
            g.drawEllipse (eye, 1.4f);
            if (! p->mixerHidden) g.fillEllipse (eye.withSizeKeepingCentre (5.0f, 5.0f));
            g.setColour (nodeColour (*p));
            g.fillEllipse (Rectangle<float> (8.0f, 8.0f).withCentre ({ row.getX() + 34.0f, row.getCentreY() }));
            drawText (g, p->instanceName() + (p == &proc ? "  (this one)" : ""), row.withTrimmedLeft (44.0f).withTrimmedRight (80.0f), 11.5f, p->mixerHidden ? colours::inkDim : colours::ink, true, Justification::centredLeft);
            drawText (g, ids::decibels (p->getParam (ids::outGain)), row.withLeft (row.getRight() - 76.0f), 10.5f, colours::lcd, true, Justification::centredRight);
            y += kRowH;
        }
        g.restoreState();
        drawText (g, "Double-click a name to rename it. The eye hides a node.", { list.getX(), list.getBottom() + 4.0f, list.getWidth(), 14.0f }, 9.5f, colours::inkDim, false, Justification::centredLeft);
    }

    // --- the pad ---------------------------------------------------------------------------
    if (stage == Off)
    {
        g.setColour (colours::black);
        g.fillRect (pad);
        g.setColour (colours::grid);
        g.drawVerticalLine ((int) pad.getCentreX(), pad.getY(), pad.getBottom());
        for (float db = kGainTop; db >= kGainBottom; db -= 6.0f)
        {
            const float y = yForGain (db, pad);
            g.setColour (db == 0.0f ? colours::grid.brighter (0.6f) : colours::grid);
            g.drawHorizontalLine ((int) y, pad.getX(), pad.getRight());
            drawText (g, (db > 0 ? "+" : "") + String ((int) db), { pad.getX() + 4.0f, y - 12.0f, 30.0f, 11.0f }, 9.0f, colours::inkDim, false, Justification::centredLeft);
        }
        drawText (g, "L", { pad.getX() + 4.0f, pad.getBottom() - 18.0f, 20.0f, 14.0f }, 12.0f, colours::inkDim, true);
        drawText (g, "R", { pad.getRight() - 24.0f, pad.getBottom() - 18.0f, 20.0f, 14.0f }, 12.0f, colours::inkDim, true);
        drawText (g, "PAN", { pad.getCentreX() - 30.0f, pad.getBottom() - 18.0f, 60.0f, 14.0f }, 10.0f, colours::inkDim, true);
        for (auto* p : all)
        {
            if (p->mixerHidden) continue;
            const auto c = nodePos (*p);
            const Colour col = nodeColour (*p);
            if (p->isStereo())
            {
                const float w = widthHandle (*p);
                g.setColour (col.withAlpha (0.6f));
                g.drawLine (c.x - w, c.y, c.x + w, c.y, 2.0f);
                g.setColour (colours::cream);
                g.fillRect (Rectangle<float> (5.0f, 14.0f).withCentre ({ c.x - w, c.y }));
                g.fillRect (Rectangle<float> (5.0f, 14.0f).withCentre ({ c.x + w, c.y }));
            }
            const float act = jlimit (0.0f, 1.0f, activity[p->instanceId()]);
            const bool clip = activity[p->instanceId()] > 1.0f;
            g.setColour (col.withAlpha (0.25f + 0.75f * std::sqrt (act)));
            g.fillEllipse (Rectangle<float> (24.0f, 24.0f).withCentre (c));
            g.setColour (clip ? colours::ledRed : (p == &proc ? colours::ink : colours::black));
            g.drawEllipse (Rectangle<float> (24.0f, 24.0f).withCentre (c), clip ? 3.0f : 1.5f);
            drawText (g, p->instanceName(), { c.x - 80.0f, c.y + 14.0f, 160.0f, 14.0f }, 10.5f, colours::ink, true);
        }
        if (all.size() < 2)
        {
            g.setColour (colours::inkDim);
            g.setFont (panelFont (14.0f, false));
            g.drawFittedText ("Put Planish on the other tracks of the set: every one shows up here. Drag a node left and right to pan it, "
                              "up and down for its output level; the handles set its width.",
                              pad.reduced (80.0f).toNearestInt(), Justification::centred, 4, 1.0f);
        }
        return;
    }

    // --- the mix assistant ------------------------------------------------------------------
    g.setColour (colours::black);
    g.fillRect (pad);
    auto text = [&] (const String& t, float y, float h = 40.0f)
    {
        g.setColour (colours::ink);
        g.setFont (panelFont (13.0f, false));
        g.drawFittedText (t, Rectangle<float> (pad.getX() + 20.0f, y, pad.getWidth() - 40.0f, h).toNearestInt(), Justification::topLeft, 3, 1.0f);
    };
    if (stage == Setup)
    {
        text ("Set your faders in Live to 0 dB and your pans to the centre. Pick the tracks to include and at least one FOCUS: the part that matters most. "
              "Click a group to change it. Then BEGIN LISTENING and play the song from the start. Don't play anything yet.", pad.getY() + 12.0f, 54.0f);
        float y = pad.getY() + 76.0f;
        drawText (g, "INCLUDE", { pad.getX() + 20.0f, y, 70.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, "TRACK", { pad.getX() + 100.0f, y, 200.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, "GROUP", { pad.getX() + 380.0f, y, 120.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        drawText (g, "FOCUS", { pad.getX() + 560.0f, y, 80.0f, 14.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
        y += 20.0f;
        for (auto& t : tracks)
        {
            auto* p = LinkRegistry::get().find (t.id);
            if (p == nullptr) continue;
            const auto row = Rectangle<float> (pad.getX() + 14.0f, y, pad.getWidth() - 28.0f, kRowH - 2.0f);
            g.setColour (colours::socket.withAlpha (0.5f));
            g.fillRect (row);
            auto box = Rectangle<float> (16.0f, 16.0f).withCentre ({ row.getX() + 30.0f, row.getCentreY() });
            g.setColour (t.include ? colours::cream : colours::black); g.fillRect (box);
            g.setColour (colours::ink); g.drawRect (box, 1.0f);
            drawText (g, p->instanceName(), { row.getX() + 86.0f, row.getY(), 280.0f, row.getHeight() }, 12.0f, t.include ? colours::ink : colours::inkDim, true, Justification::centredLeft);
            drawText (g, groupName (t.group), { row.getX() + 366.0f, row.getY(), 140.0f, row.getHeight() }, 11.0f, colours::lcd, true, Justification::centredLeft);
            box = Rectangle<float> (16.0f, 16.0f).withCentre ({ row.getX() + 570.0f, row.getCentreY() });
            g.setColour (t.focus ? colours::ledYellow : colours::black); g.fillRect (box);
            g.setColour (colours::ink); g.drawRect (box, 1.0f);
            y += kRowH;
        }
    }
    else if (stage == Waiting || stage == Listening)
    {
        text (stage == Waiting ? "Put the playhead at the start of the song and press play in Live. Listening starts with playback."
                               : "Listening. Let the song play to the end (or as far as you like), then GO TO RESULTS.", pad.getY() + 12.0f);
        float y = pad.getY() + 70.0f;
        for (auto& t : tracks)
        {
            auto* p = LinkRegistry::get().find (t.id);
            if (p == nullptr || ! t.include) continue;
            drawText (g, p->instanceName(), { pad.getX() + 20.0f, y, 260.0f, kRowH }, 12.0f, colours::ink, true, Justification::centredLeft);
            const float st = p->chain.outputLoudness().shortTerm();
            const auto bar = Rectangle<float> (pad.getX() + 290.0f, y + 6.0f, 360.0f, 14.0f);
            drawLcdBox (g, bar);
            g.setColour (colours::lcd);
            g.fillRect (bar.reduced (2.0f).withWidth (jlimit (0.0f, bar.getWidth() - 4.0f, (st + 60.0f) / 60.0f * (bar.getWidth() - 4.0f))));
            drawText (g, stage == Listening ? String (p->mixLoudness(), 1) + " LUFS" : String(), { pad.getX() + 660.0f, y, 90.0f, kRowH }, 10.0f, colours::lcd, true, Justification::centredLeft);
            y += kRowH;
        }
    }
    else if (stage == Results)
    {
        text ("The groups' levels are set against the focus. Move a group fader to taste; BYPASS ASSISTANT compares with how it was. ACCEPT keeps it.", pad.getY() + 12.0f);
        float y = pad.getY() + 64.0f;
        for (auto& t : tracks)
        {
            auto* p = LinkRegistry::get().find (t.id);
            if (p == nullptr || ! t.include) continue;
            drawText (g, p->instanceName(), { pad.getX() + 20.0f, y, 240.0f, 22.0f }, 11.5f, colours::ink, true, Justification::centredLeft);
            drawText (g, t.focus ? "FOCUS" : groupName (t.group), { pad.getX() + 270.0f, y, 110.0f, 22.0f }, 10.0f, colours::inkDim, true, Justification::centredLeft);
            drawText (g, t.loudness < -69.0f ? String ("silent: unchanged") : ids::decibels (t.originalGain) + "  ->  " + ids::decibels (t.newGain),
                      { pad.getX() + 390.0f, y, 300.0f, 22.0f }, 11.0f, colours::lcd, true, Justification::centredLeft);
            y += 22.0f;
            if (y > pad.getY() + 330.0f) break;
        }
    }
}

// ---------------------------------------------------------------------------
void MixerView::mouseDown (const MouseEvent& e)
{
    const auto pt = e.position;
    const auto list = listArea();
    if (list.contains (pt))
    {
        const int row = (int) ((pt.y - list.getY() - 8.0f + listScroll) / kRowH);
        const auto all = LinkRegistry::get().all();
        if (row >= 0 && row < (int) all.size() && pt.x < list.getX() + 30.0f)
            all[(size_t) row]->mixerHidden = ! all[(size_t) row]->mixerHidden;
        repaint();
        return;
    }
    if (stage == Setup)
    {
        float y = padArea().getY() + 96.0f;
        for (auto& t : tracks)
        {
            if (pt.y >= y && pt.y < y + kRowH)
            {
                const float x = pt.x - padArea().getX() - 14.0f;
                if (x < 60.0f) t.include = ! t.include;
                else if (x > 360.0f && x < 520.0f)
                {
                    t.group = (t.group + 1) % NumGroups;
                    if (t.group == Focus) t.group = Voice;
                    if (auto* p = LinkRegistry::get().find (t.id)) p->mixGroup = t.group;
                }
                else if (x > 540.0f && x < 600.0f) t.focus = ! t.focus;
                repaint();
                return;
            }
            y += kRowH;
        }
        return;
    }
    if (stage != Off) return;
    int part = 0;
    if (auto* p = nodeAt (pt, part)) { dragId = p->instanceId(); dragPart = part; }
}

void MixerView::mouseDrag (const MouseEvent& e)
{
    if (dragId < 0) return;
    auto* p = LinkRegistry::get().find (dragId);
    if (p == nullptr) return;
    const auto pad = padArea();
    if (dragPart == 0)
    {
        const float pan = jlimit (-100.0f, 100.0f, (e.position.x - pad.getX() - 40.0f) / (pad.getWidth() - 80.0f) * 200.0f - 100.0f);
        if (p->isStereo()) p->setParam (ids::pan, std::abs (pan) < 3.0f ? 0.0f : pan);
        const float gn = gainForY (e.position.y, pad);
        p->setParam (ids::outGain, std::abs (gn) < 0.4f ? 0.0f : gn);
    }
    else
    {
        const auto c = nodePos (*p);
        const float half = std::abs (e.position.x - c.x);
        p->setParam (ids::width, jlimit (-100.0f, 100.0f, (half - 16.0f) / 60.0f * 200.0f - 100.0f));
    }
    repaint();
}

void MixerView::mouseUp (const MouseEvent&) { dragId = -1; }

void MixerView::mouseDoubleClick (const MouseEvent& e)
{
    const auto list = listArea();
    if (! list.contains (e.position)) return;
    const int row = (int) ((e.position.y - list.getY() - 8.0f + listScroll) / kRowH);
    const auto all = LinkRegistry::get().all();
    if (row < 0 || row >= (int) all.size()) return;
    const int id = all[(size_t) row]->instanceId();
    auto* w = new AlertWindow ("Rename", "The name every Planish shows for this track.", MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", all[(size_t) row]->instanceName());
    w->addButton ("RENAME", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("CANCEL", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([w, id] (int result)
    {
        if (result == 1)
            if (auto* p = LinkRegistry::get().find (id))
            {
                const auto n = w->getTextEditorContents ("name").trim();
                if (n.isNotEmpty()) p->setInstanceName (n);
            }
    }), true);
}

void MixerView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (! listArea().contains (e.position)) return;
    const float maxScroll = jmax (0.0f, (float) LinkRegistry::get().all().size() * kRowH - listArea().getHeight() + 16.0f);
    listScroll = jlimit (0.0f, maxScroll, listScroll - w.deltaY * 120.0f);
    repaint();
}
} // namespace planish::ui
