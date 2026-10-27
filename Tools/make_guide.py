"""make_guide.py - builds docs/Planish-Guide.pdf.

    python3 Tools/make_guide.py docs/Planish-Guide.pdf <screenshot-folder>

The screenshot folder holds PNGs made with PlanishSnapshot (build with
-DPLANISH_BUILD_TOOLS=ON), run from the build folder:

    S=./PlanishSnapshot_artefacts/Release/PlanishSnapshot
    $S shots/overview.png 1                 # Lead Vocal, the Equalizer showing
    for id in $(seq 0 10); do $S shots/mod$id.png 0 $id; done
    $S shots/assistrun.png assistrun        # the assistant's result page
    $S shots/mixer.png mixer                # the Visual Mixer, five tracks
    $S shots/mixassist.png mixassist        # the mix assistant's result

Needs reportlab and Pillow. Same page style as the Burnish guide.
"""
import os
import sys

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import (BaseDocTemplate, Flowable, Frame, Image, KeepTogether, ListFlowable,
                                ListItem, PageBreak, PageTemplate, Paragraph, Spacer, Table, TableStyle)
from reportlab.platypus.doctemplate import NextPageTemplate

OUT, SHOTS = sys.argv[1], sys.argv[2]
TMP = os.path.join(SHOTS, "_cropped")
os.makedirs(TMP, exist_ok=True)

PANEL = colors.HexColor("#2a2a2c")
BRAND = colors.HexColor("#6f9fc8")
BRAND_DARK = colors.HexColor("#3f6f99")
CREAM = colors.HexColor("#f3f0e6")
INK = colors.HexColor("#1c1c1e")
DIM = colors.HexColor("#6b6b6b")
RULE = colors.HexColor("#d6d6d6")
TINT = colors.HexColor("#e9f1f8")

W, H = letter
M = 0.9 * inch
FW = W - 2 * M

body = ParagraphStyle("body", fontName="Helvetica", fontSize=9.8, leading=13.6, textColor=INK, spaceAfter=6)
lead = ParagraphStyle("lead", parent=body, fontSize=11, leading=15.5, spaceAfter=8)
h1 = ParagraphStyle("h1", parent=body, fontName="Helvetica-Bold", fontSize=19, leading=23, spaceBefore=4, spaceAfter=10)
h2 = ParagraphStyle("h2", parent=body, fontName="Helvetica-Bold", fontSize=13, leading=16, spaceBefore=12, spaceAfter=5)
h3 = ParagraphStyle("h3", parent=body, fontName="Helvetica-Bold", fontSize=10.5, leading=13.5, spaceBefore=8, spaceAfter=3)
small = ParagraphStyle("small", parent=body, fontSize=8.3, leading=11, textColor=DIM)
cell = ParagraphStyle("cell", parent=body, fontSize=8.8, leading=11.6, spaceAfter=0)
cellb = ParagraphStyle("cellb", parent=cell, fontName="Helvetica-Bold")
callout = ParagraphStyle("callout", parent=body, fontSize=9.3, leading=12.8, backColor=TINT, borderPadding=(8, 10, 8, 12),
                         spaceBefore=10, spaceAfter=14, leftIndent=4, rightIndent=4)


def P(t, s=body):
    return Paragraph(t, s)


def bullets(items, style=body):
    return ListFlowable([ListItem(P(i, style), leftIndent=12, value="•") for i in items],
                        bulletType="bullet", start="•", leftIndent=12, bulletFontSize=8)


def table(rows, widths, header=True, bold_first=True):
    data = []
    for r, row in enumerate(rows):
        out = []
        for c, text in enumerate(row):
            st = cellb if (header and r == 0) or (bold_first and c == 0) else cell
            out.append(P(text, st))
        data.append(out)
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 4), ("LEFTPADDING", (0, 0), (-1, -1), 3)]
    if header:
        st.append(("LINEBELOW", (0, 0), (-1, 0), 0.9, INK))
    t.setStyle(TableStyle(st))
    return t


class Note(Flowable):
    """The tinted callout with a brand bar down its left edge, as in the Lacquer guide."""

    def __init__(self, html):
        super().__init__()
        self.p = P(html, ParagraphStyle("n", parent=body, fontSize=9.3, leading=12.8, spaceAfter=0))

    def wrap(self, aw, ah):
        self.w = aw
        _, self.ph = self.p.wrap(aw - 26, ah)
        self.h = self.ph + 20
        return aw, self.h + 10

    def draw(self):
        c = self.canv
        c.setFillColor(TINT)
        c.rect(0, 0, self.w, self.h, stroke=0, fill=1)
        c.setFillColor(BRAND)
        c.rect(0, 0, 3, self.h, stroke=0, fill=1)
        self.p.drawOn(c, 16, 10)


def crop(name, box, out):
    path = os.path.join(TMP, out)
    PILImage.open(os.path.join(SHOTS, name)).convert("RGB").crop(box).save(path, quality=90)
    return path


def image(path, width):
    w, h = PILImage.open(path).size
    return Image(path, width=width, height=width * h / w)


class Annotated(Flowable):
    """A screenshot with numbered markers on it."""

    def __init__(self, path, width, marks, src_size):
        super().__init__()
        self.path, self.width_, self.marks, self.src = path, width, marks, src_size
        self.height_ = width * src_size[1] / src_size[0]

    def wrap(self, aw, ah):
        return self.width_, self.height_ + 4

    def draw(self):
        c = self.canv
        c.drawImage(self.path, 0, 4, width=self.width_, height=self.height_)
        k = self.width_ / self.src[0]
        for n, (x, y) in enumerate(self.marks, start=1):
            px, py = x * k, 4 + self.height_ - y * k
            c.setFillColor(BRAND)
            c.setStrokeColor(colors.white)
            c.setLineWidth(1)
            c.circle(px, py, 7.5, stroke=1, fill=1)
            c.setFillColor(INK)
            c.setFont("Helvetica-Bold", 8)
            c.drawCentredString(px, py - 2.8, str(n))


# ---------------------------------------------------------------------------
# Page furniture
# ---------------------------------------------------------------------------
def footer(c, doc):
    c.saveState()
    c.setFont("Helvetica", 7.6)
    c.setFillColor(DIM)
    c.drawString(M, 0.55 * inch, "Planish · Mixing suite for Ableton Live")
    c.drawRightString(W - M, 0.55 * inch, str(doc.page))
    c.restoreState()


def cover(c, doc):
    c.saveState()
    band = 3.55 * inch
    c.setFillColor(PANEL)
    c.rect(0, H - band, W, band, stroke=0, fill=1)
    c.setFillColor(BRAND)
    c.setFont("Helvetica-Bold", 54)
    c.drawString(M, H - 1.25 * inch, "PLANISH")
    c.setFillColor(CREAM)
    c.setFont("Helvetica-Bold", 17)
    c.drawString(M, H - 1.68 * inch, "A mixing suite, track by track:")
    c.drawString(M, H - 1.95 * inch, "from raw parts to a balanced mix")
    labels = [("CLEAN", "GATE"), ("TONE", "EQ"), ("DYNAMICS", "COMP"), ("COLOUR", "EXCITE"), ("SPACE", "UNMASK"), ("BALANCE", "MIXER")]
    bw, gap = 0.86 * inch, 0.2 * inch
    y = H - 2.95 * inch
    for i, (top, big) in enumerate(labels):
        x = M + i * (bw + gap)
        c.setFillColor(colors.HexColor("#111112"))
        c.setStrokeColor(CREAM)
        c.setLineWidth(1)
        c.rect(x, y, bw, 0.62 * inch, stroke=1, fill=1)
        c.setFillColor(BRAND)
        c.setFont("Helvetica-Bold", 6.6)
        c.drawString(x + 7, y + 0.46 * inch, top)
        c.setFont("Helvetica-Bold", 10)
        c.drawCentredString(x + bw / 2, y + 0.15 * inch, big)
        if i < len(labels) - 1:
            c.setStrokeColor(CREAM)
            ax = x + bw + 3
            c.line(ax, y + 0.31 * inch, ax + gap - 7, y + 0.31 * inch)
            p = c.beginPath()
            p.moveTo(ax + gap - 6, y + 0.31 * inch)
            p.lineTo(ax + gap - 11, y + 0.31 * inch + 3)
            p.lineTo(ax + gap - 11, y + 0.31 * inch - 3)
            p.close()
            c.setFillColor(CREAM)
            c.drawPath(p, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#9a978e"))
    c.setFont("Helvetica", 7.6)
    c.drawRightString(W - M, H - 3.3 * inch, "Eleven modules, every track, one session")
    c.restoreState()
    footer(c, doc)


def later(c, doc):
    c.saveState()
    c.setFont("Helvetica-Bold", 8.2)
    c.setFillColor(BRAND_DARK)
    c.drawString(M, H - 0.6 * inch, "PLANISH")
    c.setFont("Helvetica", 8.2)
    c.setFillColor(INK)
    c.drawRightString(W - M, H - 0.6 * inch, "Mixing suite guide")
    c.setStrokeColor(RULE)
    c.setLineWidth(0.6)
    c.line(M, H - 0.68 * inch, W - M, H - 0.68 * inch)
    c.restoreState()
    footer(c, doc)


doc = BaseDocTemplate(OUT, pagesize=letter, leftMargin=M, rightMargin=M, topMargin=0.9 * inch, bottomMargin=0.85 * inch,
                      title="Planish guide", author="Taddei", subject="Planish: a mixing suite for Ableton Live")
doc.addPageTemplates([
    PageTemplate(id="cover", frames=[Frame(M, 0.85 * inch, FW, H - 3.55 * inch - 1.15 * inch, id="c", leftPadding=0, rightPadding=0)], onPage=cover),
    PageTemplate(id="later", frames=[Frame(M, 0.85 * inch, FW, H - 0.95 * inch - 0.85 * inch, id="l", leftPadding=0, rightPadding=0)], onPage=later)])

s = [NextPageTemplate("later")]
COLS = [1.45 * inch, FW - 1.45 * inch]

# ---------------------------------------------------------------------------
# Cover page
# ---------------------------------------------------------------------------
s.append(P("Planish is a channel strip for every track of a mix. You build a <b>signal chain</b> out of modules in whatever order you like: a "
           "gate, an equaliser whose bands can move with the music, two compressors, a transient shaper, saturation, two spectral tools, "
           "slow levelling, phase alignment and a clipper. Each module has its own <b>MIX</b>, so anything can be parallel. Around the chain sit "
           "meters, width and pan, an output limiter, a gain-matched bypass, presets, a named history and four sets.", lead))
s.append(P("Put Planish on more than one track and the copies find each other. The Equalizer's <b>masking meter</b> shows where this track and "
           "another are fighting for the same frequencies, <b>Unmask</b> ducks just those frequencies when the other track plays, and the "
           "<b>Visual Mixer</b> lays every track out on one pad by level, pan and width. Two assistants do the first pass for you: <b>ASSIST</b> "
           "listens to one track and builds a chain for what it is; the <b>MIX ASSISTANT</b> listens to the whole song and sets a balance."))
s.append(P("Planish does for a mix what Burnish does for a master, and it looks and works the same way. If you know Burnish you know the "
           "panel already; this guide starts with the panel and a first mix, then takes the modules one at a time."))
s.append(P("You'll need", h3))
s.append(bullets(["Ableton Live 11 or 12 (any edition that loads VST3 plug-ins).",
                  "Planish built and installed: <b>Planish.vst3</b> in ~/Library/Audio/Plug-Ins/VST3 (a release download goes in the same folder).",
                  "A song with its parts on separate tracks, and headphones or speakers you know well."]))
s.append(Spacer(1, 6))
s.append(Note("<b>The honest bit.</b> A plugin on every track makes it easy to process everything because you can. Most tracks in a good mix "
              "need very little. Use <b>DELTA</b> to hear what a module is actually doing, keep <b>AUTO GAIN</b> on while you compare, and take "
              "out anything whose DELTA you can't hear."))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Find your way around
# ---------------------------------------------------------------------------
s.append(P("Find your way around", h1))
marks = [(384, 39), (520, 39), (730, 39), (1010, 39), (1145, 39), (1253, 39), (1340, 39),
         (66, 150), (300, 228), (560, 470), (650, 760), (1258, 240), (1258, 420), (1258, 610), (1258, 740), (1258, 830)]
s.append(Annotated(os.path.join(SHOTS, "overview.png"), FW * 0.9, marks, (1400, 900)))
s.append(Spacer(1, 8))
s.append(table([
    ["1", "Track", "This instance's name, taken from Live's track name. The other Planish show it in their menus and in the Visual Mixer. Double-click to rename."],
    ["2", "Assist", "Listens to the track and builds a chain for it. Page 5."],
    ["3", "Presets", "Whole chains, factory and yours: which modules, in what order, with their settings. Click for the list, &lt; &gt; to step, * means changed. SAVE keeps your own."],
    ["4", "History, undo", "HISTORY lists every change by name (\"Equalizer band 3 gain\", \"Loaded Kick\"): pick one to go back to just before it. UNDO (cmd-Z) steps back one."],
    ["5", "Sets", "A B C D hold four complete states of this Planish: shift-click to store, click to recall."],
    ["6", "Mixer", "The Visual Mixer and the Mix Assistant, for every Planish in the set. Page 12."],
    ["7", "Options, tips", "Spectrum display, averaging, peak hold, tilt; the frequency scale; alt-solo width; the masking histogram; meters. TIPS turns the hover help on and off."],
    ["8", "Signal chain", "One tile per module, in signal order. Click to show it, drag to reorder. LED: power. S: solo. x: remove. The bar: how much it changes the level. MIX: its wet/dry. +: add."],
    ["9", "Module header", "DELTA (hear only what this module changes), the channel mode, view buttons, the module's own PRESETS, RESET."],
    ["10", "Module display", "The spectrum, the curves, the traces: whatever the module needs you to see, with its handles on it."],
    ["11", "Module controls", "Everything else for the selected module. A line at the very bottom says in words what it is doing now."],
    ["12", "Meters", "Input and output: peak, with RMS or short-term loudness under it. A readout turns red on a clip; click it to clear."],
    ["13", "Output stage", "INPUT and OUTPUT gain, WIDTH (mono to twice as wide), PAN, INVERT, SWAP."],
    ["14", "Output limiter", "Catches peaks after the output gain: CEILING, STYLE (CLEAR, SMOOTH, THICK), MODE (IRC II, IRC LL, HARD, ZERO-L) and its gain reduction."],
    ["15", "Listening", "BYPASS (the input, delayed to line up), AUTO GAIN (output matched to input loudness, so bypass is fair), MONO, ZERO LATENCY."],
    ["16", "Loudness", "Integrated and short-term loudness, true peak, and the latency Planish reports to Live."]],
    [0.3 * inch, 1.15 * inch, FW - 1.45 * inch], header=False))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# A first mix
# ---------------------------------------------------------------------------
s.append(P("A first mix", h1))
s.append(P("A worked example: a song with a lead vocal, drums, bass, keys and a pad. The order matters less than the habit of listening "
           "after every step."))
s.append(P("1. One Planish on every track", h2))
s.append(bullets(["Drag Planish onto each track you mean to work on, as the first device (before any sends). Each opens with an Equalizer in its chain.",
                  "Leave Live's faders at 0 dB and its pans in the middle for now: the Visual Mixer and the Mix Assistant set level and pan through each Planish's output stage, where every track can see them.",
                  "If Live passes on the track names and colours, Planish takes them; otherwise rename each one in TRACK."]))
s.append(P("2. A chain for each track", h2))
s.append(P("Solo the vocal, press <b>ASSIST</b>, then <b>GO</b>, and play a typical part of the track (eight seconds is the default; LISTEN sets "
           "it). Planish works out what the track is, picks that instrument's target and builds a chain toward it: an Equalizer for the tonal "
           "balance, a Compressor, the Exciter, Density, Sculptor, and for drums a Gate and the Transient Shaper. Do the same on each track, or "
           "start from a factory preset (Lead Vocal, Kick, Bass, Drum Bus...) and adjust."))
s.append(P("3. Find the collisions", h2))
s.append(P("On the vocal's Equalizer press <b>MASKING</b> and pick the keys as the other track. While the song plays, the bands where the keys "
           "cover the vocal light up along the bottom of the display, and stay lit for the histogram's hold time. Cut those frequencies a "
           "little on the keys (OTHER EQ edits the keys' Equalizer from here), or let <b>Unmask</b> on the keys do it only when the vocal is "
           "singing: put Unmask on the keys, open Live's sidechain menu on the keys' Planish and choose the vocal track."))
s.append(P("4. Balance", h2))
s.append(P("Press <b>MIXER</b> on any of them. Every Planish is a node: drag a node up and down for its level, left and right for its pan, "
           "and pull its handles for width. Or let the <b>MIX ASSISTANT</b> listen to the song and set the levels by group, then adjust. Store "
           "the balance in a mix snapshot (SET then A) before you try another."))
s.append(P("5. Check", h2))
s.append(bullets(["BYPASS each track against the processed version with AUTO GAIN on: it should sound better, not just different.",
                  "MONO on a few tracks: anything that disappears is too wide or out of phase (try the Phase module).",
                  "Listen on a second pair of speakers or headphones before you trust the balance."]))
s.append(Note("<b>Latency.</b> Spectral modules (Sculptor, Unmask) add about 43 ms each and Phase about 21 ms; Live lines everything up on "
              "playback. If you are recording through a track, press <b>ZERO LATENCY</b>: the lookahead and oversampling drop out and the "
              "modules that need latency are bypassed until you switch it off."))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Assist
# ---------------------------------------------------------------------------
s.append(P("ASSIST", h1))
assist = crop("assistrun.png", (10, 100, 1118, 890), "assistrun.jpg")
s.append(image(assist, FW * 0.72))
s.append(Spacer(1, 6))
s.append(P("ASSIST listens to one track and builds a starting point. It measures the track's spectrum, loudness, crest factor, onsets and "
           "width, compares them with sixteen instrument profiles (vocal, backing vocals, dialogue, kick, snare, drum bus, overheads, bass, "
           "electric and acoustic guitar, piano, synth, strings, brass, percussion and a full mix) and picks the nearest. Then it builds that "
           "instrument's recipe toward its tonal target. The text under the meter lists every decision it made."))
s.append(table([["Control", "What it does"],
                ["EQUALIZER", "How much of the corrective EQ it built (its MIX)."],
                ["SCULPTOR", "How hard the Sculptor pulls toward the target."],
                ["DENSITY, COMPRESSOR", "How much levelling, and the Compressor's MIX."],
                ["DRIVE, TONE, XY", "The Exciter on every band: how much, how bright, and the blend of shapes. CLASSIC and TRASH pick the two families of shapes."],
                ["WIDTH", "The output stage's width."],
                ["TARGET LIBRARY", "Pick another target if it guessed wrong: the chain is rebuilt for it. SUGGESTED marks the one it detected. + REFERENCE learns a target from an audio file of your own (the loudest eight seconds); REMOVE deletes one."],
                ["TONAL BALANCE", "Your output against the target: the band is where the target says the track should sit, the line is where it does."],
                ["RESTORE, RELEARN", "Put back the chain you had before; listen again."],
                ["DETAILED VIEW", "Back to the modules it built, to fine-tune them."]], COLS))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# The modules
# ---------------------------------------------------------------------------
s.append(P("The modules", h1))
s.append(P("Every control also has a hover tip on the panel. Three things are shared by many modules:"))
s.append(bullets([
    "<b>Channel modes.</b> STEREO uses one set of controls for both channels. MID/SIDE gives the middle and the sides their own set, A and B. "
    "TRANSIENT/SUSTAIN splits the signal into its attacks and what rings on after them (the two always add back up to the input) and gives "
    "each its own set: compress only the sustain, saturate only the attacks.",
    "<b>Bands.</b> Gate, Compressor, Transient Shaper, Exciter, Density and Clipper split into up to three bands. In CROSSOVER view drag a "
    "handle to move a crossover, hover and click + to add one; LEARN puts the crossovers at dips in what is playing.",
    "<b>Sidechain.</b> The Gate, the Compressors and the Equalizer's dynamic bands choose per band what they listen to: SELF (the band "
    "itself), the module's full input or one of its bands, or the external sidechain (full, or one band of it). In Live, open the device's "
    "sidechain menu and pick the track to listen to. The sidechain is drawn in purple everywhere."]))


def module(mid, name, intro, rows, extra=None):
    shot = crop("mod%d.png" % mid, (10, 180, 1118, 890), "mod%d.jpg" % mid)
    img_w = 2.6 * inch
    head = Table([[image(shot, img_w), P(intro)]], colWidths=[img_w + 0.15 * inch, FW - img_w - 0.15 * inch])
    head.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0),
                              ("RIGHTPADDING", (0, 0), (0, 0), 10), ("TOPPADDING", (0, 0), (-1, -1), 0)]))
    block = [P(name, h2), head, Spacer(1, 6), table([["Control", "What it does"]] + rows, COLS)]
    if extra:
        block.append(P(extra, small))
    s.append(KeepTogether(block[:3]))
    s.extend(block[3:])
    s.append(Spacer(1, 4))


module(1, "Equalizer",
       "Twelve bands over the spectrum of the track. Double-click the display to add a band, drag a node for frequency and gain, drag its "
       "side handles (or scroll) for Q. Box-select several nodes to move them together. Alt-click and hold anywhere to hear just that region. "
       "Any band can turn <b>dynamic</b>: an arrow on its node shows which way it is moving now.",
       [["SHAPE", "Proportional Q (narrower as it gets bigger), bell, band shelf; analog, vintage and Baxandall low and high shelves; flat and resonant high and low passes."],
        ["FREQ, GAIN, Q, SLOPE", "The band. SLOPE is for the passes: 6 to 48 dB/oct."],
        ["ON, SOLO, X", "Switch the band off, hear only its region, remove it."],
        ["DYNAMIC", "The band moves with the level in its region: UP or DOWN past the THRESHOLD, at its ATTACK and RELEASE. A DOWN band with a cut is a de-esser; an UP band with a boost lifts a part only when it is quiet. SIDECHAIN picks what it listens to."],
        ["SOFT SAT", "A gentle saturation on the EQ's output, to round the peaks of big boosts."],
        ["LEARN", "Listens and places nodes on the resonances that stick out."],
        ["Scale", "LOGARITHMIC, MEL, LINEAR or PIANO ROLL (keys under the axis, note names on the nodes)."],
        ["MASKING", "The masking meter: pick another Planish, and the bands where it covers this track light up below the display, with a histogram that holds them. SENSITIVITY sets how much counts; GAIN OFFSET matches Live's faders. THIS EQ and OTHER EQ switch which track's Equalizer you are editing; INVERSE LINK makes a boost on one a cut on the other; BYPASS EQs compares with both off."]])

module(0, "Gate",
       "Takes down what plays between the notes: spill on a tom, room on a snare, noise under a vocal. The trace scrolls the level with the "
       "two thresholds across it; drag them there. WAVEFORM, CROSSOVER and DETECTION switch the display.",
       [["OPEN, CLOSE", "The gate opens above OPEN and closes below CLOSE. Set CLOSE a few dB under OPEN so it doesn't chatter on a decaying note."],
        ["RATIO", "How hard it turns down below the threshold: low ratios are gentle expansion, 20:1 is a gate."],
        ["ATTACK, HOLD, RELEASE", "How fast it opens, how long it stays open after the signal drops, and how fast it closes."],
        ["SIDECHAIN", "What opens it: a kick mic opening the bottom snare mic, say."],
        ["DETECTION", "A filter on what the gate listens to: high-pass the kick out of a snare's detector."]])

module(2, "Compressor 1 and 2",
       "Two independent compressors: one to level, one to colour, or one before the EQ and one after. The trace shows input, output and "
       "sidechain with the threshold across them; the curve shows the transfer.",
       [["MODE", "PUNCH keeps the attack of every hit (its own ATTACK and SUSTAIN amounts); MODERN is clean and fast; VINTAGE is a slower feedback compressor."],
        ["Detection", "RMS (smooth, for levelling), PEAK (catches every transient) or TRUE (inter-sample peaks)."],
        ["THRESHOLD, RATIO, KNEE", "Ratios from 0.5:1 (upward: quiet parts come up) to 20:1. KNEE softens the turn."],
        ["ATTACK, RELEASE, AUTO", "AUTO is an adaptive release: fast on transients, slow on sustained compression."],
        ["MAKEUP, MIX", "Gain after compression, and a parallel blend per band. AUTO GAIN sets the makeup for you."],
        ["SIDECHAIN, DETECTION", "What it listens to (the kick, for a bass that ducks under it), and a filter on that."],
        ["SCOPE", "The trace synced to Live's tempo, to see the pumping against the beat."]])

module(4, "Transient Shaper",
       "Changes the attack and sustain of every note, without a threshold: it follows two envelopes, one fast and one slow, and turns up or "
       "down whatever is between them. The trace shows the gain it is applying.",
       [["ATTACK", "Positive makes hits snap; negative softens them."],
        ["SUSTAIN", "Positive brings up the ring and the room; negative tightens them."],
        ["SHARP, MEDIUM, SMOOTH", "The contour: how short a moment counts as an attack."],
        ["Envelope", "PRECISE, BALANCED or LOOSE: how closely the envelopes follow the signal."]])

module(5, "Exciter",
       "Adds harmonics, band by band. The display shows where the added harmonics go; the lit node on it is a post filter on what is added, "
       "to keep the top from getting harsh. Only the harmonics pass through the oversampler, and they are added back to the dry signal in time.",
       [["DRIVE", "How much. The added harmonics are level matched, so the track should not get much louder."],
        ["CLASSIC, TRASH", "The two families of shapes on the pad's corners: RETRO, TAPE, TUBE, WARM, or OVERDRIVE, SCREAM, CLIPPER, SCRATCH."],
        ["XY pad", "Blends the four shapes. The dot is where this band sits."],
        ["MIX", "Parallel blend for the band."],
        ["TAME, Pre-emphasis", "TAME softens the harshest harmonics. Pre-emphasis (FULL, DEFINED, CLEAR, FLAT) tilts what goes into the saturation."],
        ["TONE", "Darker or brighter on the added harmonics."],
        ["4X OVERSAMPLE", "Less aliasing, at a little latency and CPU."]])

module(6, "Sculptor",
       "Moves the track's spectrum toward a target for its instrument, in 32 bands, smoothly and continuously, so the tone holds as the part "
       "changes. The pale band is the target; the coloured curve is what Sculptor is doing now. Drag the handles at the bottom to limit the "
       "range it works in.",
       [["TARGET", "One of the sixteen instruments, or a reference you've learnt (ASSIST, + REFERENCE)."],
        ["AMOUNT", "How far toward the target."],
        ["TONE", "Tilts the target darker or brighter."],
        ["SPEED", "How fast it follows: slow for a steady colour, fast to chase resonances."],
        ["SOLO RANGE", "Hear only the range it works in."]])

module(7, "Unmask",
       "Makes room for another track. It compares this track with the sidechain in 32 bands and turns this one down only in the bands where "
       "the other is present, only while it plays. On the keys, with the vocal on the sidechain: the keys stay full until the vocal comes "
       "in, then step aside only where the vocal is.",
       [["AMOUNT", "The most it will take away."],
        ["SENSITIVITY", "How readily it reacts to the sidechain."],
        ["ATTACK, RELEASE", "How fast it ducks and recovers."],
        ["SOLO RANGE", "Hear only the range it works in (drag the handles in the display)."]],
       "Unmask needs a sidechain: in Live, open the device's sidechain menu on this track and pick the track to make room for.")

module(8, "Density",
       "Slow levelling in both directions: quiet passages come up, loud ones go down, toward a steady level, without the sound of a "
       "compressor working. A 3 ms lookahead lets it get out of the way before a loud part arrives.",
       [["THRESHOLD", "The level it levels toward."],
        ["RANGE", "The most it will move, up or down."],
        ["RATIO", "How firmly it holds the level."],
        ["SPEED", "How fast it follows: slow for a vocal performance, faster for a bass."]])

module(9, "Phase",
       "Lines a track up with another recording of the same thing: a bass DI against its amp, a kick inside and outside, a snare top and "
       "bottom. Put the other track on the sidechain; the scope shows the two waveforms.",
       [["LEARN", "Listens to both and sets the rotation and the delay that line them up best."],
        ["ROTATION", "Turns the phase of every frequency by the same angle (0 to 360 degrees): a fix for two mics that are not quite opposite or in phase."],
        ["DELAY", "Moves the track in time, in fractions of a sample."],
        ["INVERT", "Flips the polarity (the rotation to 180 degrees)."],
        ["Scope", "What it shows (MAIN, SC, MAIN &amp; SC or COMBINED) and over how long, from a bar to a sixteenth of one, synced to Live."]])

module(10, "Clipper",
       "Shaves peaks off, oversampled so it stays clean: drums hit as hard at a lower peak level, leaving headroom for the mix bus. The curve "
       "shows the shape.",
       [["SOFT CLIP BOOST", "Drives into the clipper and turns the result back down, so the level stays put and only the peaks change."],
        ["THRESHOLD", "Where clipping begins."],
        ["4X OVERSAMPLE", "Less aliasing on hard clipping."]])
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Visual mixer
# ---------------------------------------------------------------------------
s.append(P("The Visual Mixer", h1))
mixer = crop("mixer.png", (10, 100, 1118, 890), "mixer.jpg")
s.append(image(mixer, FW * 0.86))
s.append(Spacer(1, 6))
s.append(P("MIXER shows every Planish in the set as a node. Left and right is pan; up and down is the track's output level; the bar through "
           "the node is its width, and its handles change it. The node glows with the track's level, and its ring turns red when the track "
           "clips. Moving a node moves the OUTPUT, PAN and WIDTH of that track's Planish, so the change is in the track, saved with the set, "
           "and automatable."))
s.append(table([["Control", "What it does"],
                ["List", "Every Planish in the set, with its level. The eye hides a node from the pad (it still plays); double-click a name to rename it."],
                ["SET, A, B, C", "SET then A, B or C stores the whole mix: every track's level, pan, width and visibility. A, B or C recalls it."],
                ["MIX ASSISTANT", "Below."],
                ["BACK", "Back to the modules."]], COLS))
mixa = crop("mixassist.png", (10, 100, 1118, 890), "mixassist.jpg")
s.append(KeepTogether([P("The Mix Assistant", h2), image(mixa, FW * 0.7)]))
s.append(Spacer(1, 6))
s.append(bullets([
    "Set Live's faders to 0 dB, then press MIX ASSISTANT.",
    "Tick the tracks to include, and at least one <b>FOCUS</b>: the part the song is about, usually the lead vocal. Click a track's group to "
    "change it: VOICE, BASS, PERCUSSION or MUSICAL. Planish guesses it from ASSIST, the Sculptor target or the track's name.",
    "BEGIN LISTENING, then play the song from the start. Listening begins with playback; let it run to the end, or as far as is representative.",
    "GO TO RESULTS. The focus is set to -18 LUFS, voices 3 LU under it, percussion 4, bass 5 and everything else 8: a starting point that "
    "keeps the focus on top. A fader per group moves that whole group up to 12 dB either way.",
    "BYPASS ASSISTANT compares with the levels you had; ACCEPT keeps the new ones; CANCEL puts the old ones back."]))
s.append(PageBreak())

# ---------------------------------------------------------------------------
# Reference
# ---------------------------------------------------------------------------
s.append(P("Reference", h1))
s.append(P("Factory presets", h2))
s.append(table([["Preset", "Chain"],
                ["Default", "An Equalizer."],
                ["Lead Vocal", "Equalizer, Compressor, Density, Exciter, Sculptor."],
                ["Backing Vocals", "Equalizer, Compressor, Sculptor: thinner, softer and held back."],
                ["Kick", "Gate, Equalizer, Compressor, Transient Shaper, Clipper."],
                ["Snare", "Gate, Equalizer, Compressor, Transient Shaper, Exciter."],
                ["Drum Bus", "Equalizer, Compressor, Exciter, Clipper."],
                ["Bass", "Equalizer, Compressor, Exciter (so it reads on small speakers), Density."],
                ["Electric Guitar", "Equalizer, Compressor, Sculptor."],
                ["Acoustic Guitar", "Equalizer, Compressor, Transient Shaper."],
                ["Piano", "Equalizer, Compressor."],
                ["Synth Pad", "Equalizer, Density, and the output wide."],
                ["Dialogue", "Gate, Equalizer, Compressor, Phase."],
                ["Bus Glue", "A slow vintage Compressor."],
                ["Duck Under Sidechain", "Compressor 1 keyed from the sidechain: route the kick (or a voice) there."],
                ["Make Room for Vocal", "Unmask on the music, keyed from the vocal on the sidechain."]], [2.0 * inch, FW - 2.0 * inch]))
s.append(P("Latency", h2))
s.append(table([["Module", "Latency at 48 kHz"],
                ["Sculptor, Unmask", "about 43 ms each"],
                ["Phase", "about 21 ms"],
                ["Exciter and Clipper with 4X OVERSAMPLE", "under 1 ms"],
                ["Density", "3 ms"],
                ["Output limiter (IRC II, IRC LL, HARD)", "about 3 ms; ZERO-L none"],
                ["Everything else", "none"]],
               [3.6 * inch, FW - 3.6 * inch], bold_first=False))
s.append(P("Planish tells Live its latency, and Live lines everything up on playback; the I/O column shows the total. ZERO LATENCY takes it "
           "to none for recording.", small))
s.append(P("Where things are kept", h2))
s.append(P("Your presets, module presets and reference targets are kept in ~/Library/Application Support/Planish (the Presets, Module "
           "Presets and Targets folders), with the settings file beside them. OPTIONS → Show settings folder opens it."))
s.append(KeepTogether([P("Keyboard and mouse", h2), bullets(["cmd-Z: undo. Shift-click A B C D: store a set; click: recall it.",
                  "EQ node: drag for frequency and gain, drag the side handles or scroll for Q, alt-click the display to solo a region, double-click a node to reset it; arrow keys nudge.",
                  "Any knob, fader or readout: double-click to reset; scroll to adjust.",
                  "Chain tile MIX: drag; double-click for 100%."])]))

doc.build(s)
print("wrote", OUT)
