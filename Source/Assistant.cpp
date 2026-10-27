#include "Assistant.h"
#include "Settings.h"

namespace planish
{
namespace
{
juce::File targetFile (const juce::String& name)
{
    return Assistant::targetsFolder().getChildFile (juce::File::createLegalFileName (name) + ".target");
}

// What each kind of track tends to measure: log2 of the spectral centroid,
// share of energy under 120 Hz and over 5 kHz, onsets a second, crest
// factor (dB) and width (side/mid, dB). The nearest prototype wins.
struct Prototype { float centroidOct, low, high, onsets, crest, width; };
const Prototype kProto[InstrumentTargets::NumTargets] {
    { 10.6f, 0.04f, 0.04f, 2.5f, 13.0f, -30.0f },   // Vocal (~1.5 kHz)
    { 10.8f, 0.02f, 0.06f, 2.0f, 11.0f, -12.0f },   // Backing vocals
    { 10.3f, 0.04f, 0.02f, 4.0f, 15.0f, -30.0f },   // Dialogue
    {  6.8f, 0.70f, 0.01f, 2.0f, 15.0f, -30.0f },   // Kick (~110 Hz)
    { 10.0f, 0.10f, 0.08f, 2.0f, 17.0f, -28.0f },   // Snare
    {  9.0f, 0.30f, 0.12f, 4.0f, 15.0f,  -8.0f },   // Drum bus
    { 12.0f, 0.05f, 0.35f, 4.0f, 16.0f,  -6.0f },   // Overheads
    {  7.0f, 0.55f, 0.01f, 2.0f,  9.0f, -30.0f },   // Bass
    { 10.4f, 0.04f, 0.04f, 1.5f,  8.0f, -14.0f },   // Electric guitar
    { 10.9f, 0.05f, 0.12f, 2.5f, 14.0f, -12.0f },   // Acoustic guitar
    {  9.8f, 0.10f, 0.04f, 2.0f, 15.0f,  -8.0f },   // Piano
    { 10.2f, 0.12f, 0.08f, 0.6f,  8.0f,  -8.0f },   // Synth
    { 10.0f, 0.04f, 0.04f, 0.4f, 10.0f,  -8.0f },   // Strings
    { 10.4f, 0.02f, 0.05f, 1.0f, 11.0f, -20.0f },   // Brass
    { 11.8f, 0.03f, 0.30f, 5.0f, 17.0f, -10.0f },   // Percussion
    {  9.4f, 0.25f, 0.08f, 3.0f, 11.0f,  -9.0f },   // Full mix
};

// Per target: high-pass, compressor depth (dB under the loud level),
// ratio, attack, release, exciter drive and corner (x, y), density range.
struct Recipe { float hpHz; float depth, ratio, attack, release; float drive, x, y; float density; bool gate, transient; };
const Recipe kRecipe[InstrumentTargets::NumTargets] {
    {  80.0f, 7.0f, 3.0f,  8.0f, 120.0f, 15.0f, 0.0f, 0.0f, 4.0f, false, false },   // Vocal: tube
    { 120.0f, 8.0f, 4.0f, 10.0f, 150.0f, 10.0f, 0.0f, 0.0f, 3.0f, false, false },
    {  90.0f, 7.0f, 3.0f, 10.0f, 150.0f,  6.0f, 0.0f, 0.0f, 4.0f, true,  false },   // Dialogue
    {   0.0f, 4.0f, 4.0f, 25.0f,  90.0f, 12.0f, 1.0f, 1.0f, 0.0f, true,  true  },   // Kick: tape
    {  60.0f, 5.0f, 4.0f, 12.0f,  90.0f, 18.0f, 1.0f, 1.0f, 0.0f, true,  true  },   // Snare
    {   0.0f, 4.0f, 3.0f, 30.0f, 150.0f, 12.0f, 1.0f, 1.0f, 2.0f, false, true  },   // Drum bus
    { 120.0f, 3.0f, 2.0f, 20.0f, 150.0f,  6.0f, 1.0f, 1.0f, 0.0f, false, true  },   // Overheads
    {  30.0f, 6.0f, 4.0f, 20.0f, 160.0f, 20.0f, 1.0f, 0.0f, 4.0f, false, false },   // Bass: warm
    {  80.0f, 4.0f, 2.5f, 15.0f, 120.0f,  8.0f, 1.0f, 1.0f, 2.0f, false, false },   // Electric guitar
    {  80.0f, 6.0f, 2.5f, 12.0f, 120.0f,  8.0f, 1.0f, 1.0f, 3.0f, false, false },   // Acoustic guitar
    {  30.0f, 5.0f, 2.0f, 25.0f, 250.0f,  6.0f, 1.0f, 0.0f, 3.0f, false, false },   // Piano
    {  30.0f, 4.0f, 2.0f, 20.0f, 200.0f, 12.0f, 0.0f, 1.0f, 2.0f, false, false },   // Synth: retro
    {  50.0f, 4.0f, 1.8f, 30.0f, 300.0f,  6.0f, 1.0f, 1.0f, 3.0f, false, false },   // Strings
    {  70.0f, 5.0f, 2.5f, 15.0f, 150.0f, 10.0f, 0.0f, 0.0f, 2.0f, false, false },   // Brass
    { 100.0f, 4.0f, 3.0f, 10.0f, 100.0f,  8.0f, 1.0f, 1.0f, 0.0f, false, true  },   // Percussion
    {   0.0f, 3.0f, 2.0f, 30.0f, 250.0f,  6.0f, 1.0f, 1.0f, 2.0f, false, false },   // Full mix
};
} // namespace

juce::File Assistant::targetsFolder()
{
    return Settings::file().getParentDirectory().getChildFile ("Targets");
}

Assistant::Assistant (Chain& c) : chain (c)
{
    loadCustomTargets();
}

juce::String Assistant::targetName (int index) const
{
    if (index < InstrumentTargets::NumTargets) return InstrumentTargets::name (index);
    const int c = index - InstrumentTargets::NumTargets;
    return c < (int) customs.size() ? juce::String (customs[(size_t) c].name) : juce::String ("?");
}

float Assistant::targetDb (int index, float hz) const
{
    if (index < InstrumentTargets::NumTargets) return InstrumentTargets::curveDb (index, hz);
    const int c = index - InstrumentTargets::NumTargets;
    return c < (int) customs.size() ? customs[(size_t) c].curveDb (hz) : 0.0f;
}

// ---------------------------------------------------------------------------
void Assistant::loadCustomTargets()
{
    customs.clear();
    auto files = targetsFolder().findChildFiles (juce::File::findFiles, false, "*.target");
    files.sort();
    for (const auto& f : files)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("TARGET")) continue;
        CustomTarget t;
        t.name = f.getFileNameWithoutExtension().toStdString();
        juce::StringArray sp;
        sp.addTokens (xml->getStringAttribute ("points"), ",", "");
        for (int p = 0; p < InstrumentTargets::kPoints && p < sp.size(); ++p) t.points[(size_t) p] = sp[p].getFloatValue();
        customs.push_back (t);
    }
}

bool Assistant::addReferenceTarget (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Couldn't read " + file.getFileName(); return false; }
    const double sr = reader->sampleRate;
    const juce::int64 total = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (sr * 600.0));
    if (total < (juce::int64) (sr * 2.0)) { error = "The file is too short to learn from."; return false; }

    // Pass 1: the loudest stretch by short-term loudness.
    LoudnessMeter meter;
    meter.prepare (sr);
    meter.setTruePeak (false);
    const int block = 4096;
    juce::AudioBuffer<float> buf (2, block);
    float bestLufs = -200.0f;
    juce::int64 bestEnd = 0;
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int len = (int) std::min<juce::int64> (block, total - pos);
        reader->read (&buf, 0, len, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = reader->numChannels > 1 ? buf.getReadPointer (1) : l;
        meter.process (l, r, len);
        const float st = meter.shortTerm();
        if (st > bestLufs) { bestLufs = st; bestEnd = pos + len; }
    }
    const juce::int64 win = (juce::int64) (sr * 8.0);
    juce::int64 start = std::max<juce::int64> (0, bestEnd - (juce::int64) (sr * 1.5) - win / 2);
    if (start + win > total) start = std::max<juce::int64> (0, total - win);

    // Pass 2: analyse it.
    Analyzer an;
    an.prepare (sr);
    for (juce::int64 pos = start; pos < start + win; pos += block)
    {
        const int len = (int) std::min<juce::int64> (block, start + win - pos);
        reader->read (&buf, 0, len, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = reader->numChannels > 1 ? buf.getReadPointer (1) : l;
        an.push (l, r, len);
    }
    const auto res = an.result();
    if (! res.valid) { error = "Couldn't analyse the file."; return false; }
    const auto t = res.asTarget (file.getFileNameWithoutExtension().toStdString());

    targetsFolder().createDirectory();
    juce::XmlElement xml ("TARGET");
    juce::StringArray sp;
    for (float v : t.points) sp.add (juce::String (v, 2));
    xml.setAttribute ("points", sp.joinIntoString (","));
    if (! xml.writeTo (targetFile (t.name))) { error = "Couldn't save the target."; return false; }
    loadCustomTargets();
    for (int i = 0; i < (int) customs.size(); ++i)
        if (customs[(size_t) i].name == t.name) currentTarget = InstrumentTargets::NumTargets + i;
    if (hasResult()) build();
    return true;
}

void Assistant::removeCustomTarget (int index)
{
    if (! isCustom (index)) return;
    const int c = index - InstrumentTargets::NumTargets;
    if (c >= (int) customs.size()) return;
    targetFile (customs[(size_t) c].name).deleteFile();
    loadCustomTargets();
    if (currentTarget >= numTargets()) currentTarget = detectedTarget;
}

// ---------------------------------------------------------------------------
void Assistant::start (float s)
{
    seconds = juce::jlimit (3.0f, 60.0f, s);
    chain.analyser.reset();
    running = true;
}

void Assistant::cancel() { running = false; }

float Assistant::progress() const
{
    return running ? juce::jlimit (0.0f, 1.0f, chain.analyser.seconds() / seconds) : (hasResult() ? 1.0f : 0.0f);
}

void Assistant::tick()
{
    if (! running) return;
    if (chain.analyser.seconds() < seconds) return;
    running = false;
    lastResult = chain.analyser.result();
    if (! lastResult.valid) { summaryText = "Nothing to listen to: play the track and press RELEARN."; return; }
    detectedTarget = detectInstrument (lastResult);
    currentTarget = detectedTarget;
    build();
}

int Assistant::detectInstrument (const AnalysisResult& a)
{
    if (! a.valid) return InstrumentTargets::FullMix;
    const float c = std::log2 (std::max (40.0f, a.centroidHz));
    float best = 1e9f; int bestT = InstrumentTargets::FullMix;
    for (int t = 0; t < InstrumentTargets::NumTargets; ++t)
    {
        const auto& p = kProto[t];
        const float d = 1.2f * std::pow (c - p.centroidOct, 2.0f)
                      + 18.0f * std::pow (a.lowShare - p.low, 2.0f)
                      + 18.0f * std::pow (a.highShare - p.high, 2.0f)
                      + 0.4f * std::pow (std::log2 (1.0f + a.onsetsPerSecond) - std::log2 (1.0f + p.onsets), 2.0f)
                      + 0.02f * std::pow (a.crestDb - p.crest, 2.0f)
                      + 0.004f * std::pow (std::max (-30.0f, a.widthDb) - p.width, 2.0f);
        if (d < best) { best = d; bestT = t; }
    }
    return bestT;
}

void Assistant::setTarget (int index)
{
    currentTarget = juce::jlimit (0, numTargets() - 1, index);
    if (hasResult()) build();
}

void Assistant::apply (const juce::String& id, float v)
{
    if (setParam) setParam (id, v);
    builtValues.push_back ({ id, v });
}

bool Assistant::dirty() const
{
    if (! hasBuilt || ! getParam) return false;
    for (const auto& [id, v] : builtValues)
        if (std::abs (getParam (id) - v) > 0.01f * std::max (1.0f, std::abs (v))) return true;
    return getOrder && getOrder() != builtOrder;
}

void Assistant::restore()
{
    if (! hasBuilt) return;
    for (const auto& [id, v] : builtValues)
        if (setParam) setParam (id, v);
    if (setOrder) setOrder (builtOrder);
}

// ---------------------------------------------------------------------------
void Assistant::build()
{
    const auto& a = lastResult;
    if (! a.valid) return;
    const int recipeIndex = isCustom (currentTarget) ? detectedTarget : currentTarget;
    const auto& rc = kRecipe[recipeIndex];
    builtValues.clear();

    // Every module it touches starts from its defaults.
    auto resetModule = [this] (int id)
    {
        for (const auto& d : chain.module (id).params())
            if (setParam) setParam (d.id, d.def);
    };
    std::vector<int> order;
    if (rc.gate && a.bodyDb - a.floorDb > 20.0f) order.push_back (ModGate);
    order.push_back (ModEqualizer);
    order.push_back (ModCompressor1);
    if (rc.transient) order.push_back (ModTransientShaper);
    order.push_back (ModExciter);
    if (rc.density > 0.0f) order.push_back (ModDensity);
    order.push_back (ModSculptor);
    for (int id : order) resetModule (id);

    juce::StringArray lines;
    lines.add ("Sounds like: " + juce::String (InstrumentTargets::name (detectedTarget)) + ".  Target: " + targetName (currentTarget) + ".");

    // --- Gate -----------------------------------------------------------------------
    if (std::find (order.begin(), order.end(), (int) ModGate) != order.end())
    {
        const float open = a.floorDb + (a.bodyDb - a.floorDb) * 0.35f + 3.0f;   // peak-ish detector reads ~3 dB over RMS
        apply ("gate.band1.open", juce::jlimit (-80.0f, -10.0f, open));
        apply ("gate.band1.close", juce::jlimit (-80.0f, -16.0f, open - 6.0f));
        apply ("gate.band1.ratio", recipeIndex == InstrumentTargets::Dialogue ? 3.0f : 20.0f);
        lines.add ("Gate: opens at " + juce::String (open, 1) + " dB, under the playing and over the noise.");
    }

    // --- Equalizer: toward the target ------------------------------------------------
    auto diffAt = [&] (float hz)
    {
        // Half-octave smoothed difference, track minus target.
        float s = 0.0f; int n = 0;
        for (float k = -0.25f; k <= 0.251f; k += 0.125f)
        {
            const float f = hz * std::pow (2.0f, k);
            s += a.spectrumAt (f) - targetDb (currentTarget, f);
            ++n;
        }
        return s / (float) n;
    };
    float mean = 0.0f; int cnt = 0;
    for (float f = 100.0f; f <= 8000.0f; f *= 1.189f) { mean += diffAt (f); ++cnt; }
    mean /= (float) std::max (1, cnt);
    auto move = [&] (float hz) { return juce::jlimit (-6.0f, 6.0f, -(diffAt (hz) - mean) * 0.6f); };
    int nextBand = 1;
    auto band = [&] (const char* key) { return "eq.a.b" + juce::String (nextBand) + "." + key; };
    if (rc.hpHz > 0.0f)
    {
        apply (band ("on"), 1.0f); apply (band ("shape"), (float) HighPassFlat); apply (band ("freq"), rc.hpHz); apply (band ("slope"), 3.0f);
        ++nextBand;
    }
    const float lowG = move (90.0f), highG = move (9000.0f);
    apply (band ("on"), 1.0f); apply (band ("shape"), (float) LowShelfAnalog); apply (band ("freq"), std::max (100.0f, rc.hpHz * 1.6f)); apply (band ("gain"), lowG); ++nextBand;
    // Two broad moves where the track strays most from the target.
    std::vector<std::pair<float, float>> cands;   // |dev|, hz
    for (float f = 180.0f; f <= 5000.0f; f *= 1.122f) cands.push_back ({ std::abs (move (f)), f });
    std::sort (cands.begin(), cands.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    std::vector<float> placed;
    for (const auto& [mag, f] : cands)
    {
        if ((int) placed.size() >= 2 || mag < 0.75f) break;
        bool near = false;
        for (float p : placed) if (std::abs (std::log2 (f / p)) < 1.2f) near = true;
        if (near) continue;
        placed.push_back (f);
        apply (band ("on"), 1.0f); apply (band ("shape"), (float) ProportionalQ); apply (band ("freq"), f); apply (band ("gain"), move (f)); apply (band ("q"), 0.9f);
        lines.add ("EQ: " + juce::String (move (f) > 0 ? "lift " : "dip ") + juce::String (std::abs (move (f)), 1) + " dB near " + juce::String (juce::roundToInt (f)) + " Hz.");
        ++nextBand;
    }
    apply (band ("on"), 1.0f); apply (band ("shape"), (float) HighShelfAnalog); apply (band ("freq"), 8000.0f); apply (band ("gain"), highG); ++nextBand;
    // A resonance: a narrow peak standing 5 dB over its neighbours.
    float resHz = 0.0f, resDb = 0.0f;
    for (int b = 8; b < AnalysisResult::kBands - 8; ++b)
    {
        const float hz = AnalysisResult::bandHz (b);
        if (hz < 150.0f || hz > 8000.0f) continue;
        const float around = 0.5f * (a.spectrumDb[(size_t) b - 3] + a.spectrumDb[(size_t) b + 3]);
        const float over = a.spectrumDb[(size_t) b] - around;
        if (over > 5.0f && over > resDb) { resDb = over; resHz = hz; }
    }
    if (resHz > 0.0f)
    {
        apply (band ("on"), 1.0f); apply (band ("shape"), (float) ProportionalQ); apply (band ("freq"), resHz);
        apply (band ("gain"), -juce::jlimit (2.0f, 6.0f, resDb * 0.6f)); apply (band ("q"), 4.0f);
        lines.add ("EQ: a resonance at " + juce::String (juce::roundToInt (resHz)) + " Hz, cut.");
        ++nextBand;
    }
    // Sibilance on voices: a dynamic cut.
    if (recipeIndex <= InstrumentTargets::Dialogue)
    {
        apply (band ("on"), 1.0f); apply (band ("shape"), (float) Bell); apply (band ("freq"), 6500.0f); apply (band ("gain"), -5.0f); apply (band ("q"), 2.5f);
        apply (band ("dyn"), 1.0f); apply (band ("direction"), (float) Equalizer::Down); apply (band ("threshold"), juce::jlimit (-60.0f, -10.0f, a.loudDb - 14.0f));
        apply (band ("attack"), 1.0f); apply (band ("release"), 60.0f);
        lines.add ("EQ: a dynamic de-esser at 6.5 kHz.");
        ++nextBand;
    }
    for (int b = nextBand; b <= Equalizer::kBands; ++b) apply ("eq.a.b" + juce::String (b) + ".on", 0.0f);
    lines.add ("EQ: low shelf " + juce::String (lowG, 1) + " dB, high shelf " + juce::String (highG, 1) + " dB.");

    // --- Compressor 1 ---------------------------------------------------------------
    {
        const float thr = juce::jlimit (-60.0f, -3.0f, a.loudDb - rc.depth);
        apply ("cmp1.mode", (float) Compressor::Modern);
        apply ("cmp1.detect", (float) DetectRms);
        apply ("cmp1.band1.threshold", thr);
        apply ("cmp1.band1.ratio", rc.ratio);
        apply ("cmp1.band1.attack", rc.attack);
        apply ("cmp1.band1.release", rc.release);
        apply ("cmp1.band1.knee", 8.0f);
        apply ("cmp1.band1.autorelease", 1.0f);
        const float makeup = juce::jlimit (0.0f, 8.0f, rc.depth * (1.0f - 1.0f / rc.ratio) * 0.6f);
        apply ("cmp1.band1.makeup", makeup);
        lines.add ("Compressor: " + juce::String (rc.ratio, 1) + ":1 from " + juce::String (thr, 1) + " dB, about " + juce::String (rc.depth * (1.0f - 1.0f / rc.ratio), 1) + " dB on the loud parts.");
    }

    // --- Transient Shaper -------------------------------------------------------------
    if (rc.transient)
    {
        apply ("trs.a.band1.attack", recipeIndex == InstrumentTargets::Overheads ? 1.5f : 3.0f);
        apply ("trs.a.band1.sustain", recipeIndex == InstrumentTargets::Kick ? -2.0f : 0.0f);
        lines.add ("Transient Shaper: a little more attack.");
    }

    // --- Exciter ------------------------------------------------------------------------
    {
        const float top = diffAt (10000.0f) - mean;   // + means already bright
        const float tone = juce::jlimit (-60.0f, 60.0f, -top * 10.0f);
        if (recipeIndex == InstrumentTargets::Bass)
        {
            // Harmonics above the fundamentals so the bass reads small.
            apply ("exc.bands", 1.0f); apply ("exc.xover1", 200.0f);
            apply ("exc.band1.drive", 0.0f);
            apply ("exc.band2.drive", rc.drive); apply ("exc.band2.x", rc.x); apply ("exc.band2.y", rc.y);
        }
        else
        {
            apply ("exc.band1.drive", rc.drive); apply ("exc.band1.x", rc.x); apply ("exc.band1.y", rc.y);
        }
        apply ("exc.tone", tone);
        lines.add ("Exciter: drive " + juce::String (juce::roundToInt (rc.drive)) + "%, tone " + juce::String (juce::roundToInt (tone)) + ".");
    }

    // --- Density ------------------------------------------------------------------------
    if (rc.density > 0.0f)
    {
        const float range = juce::jlimit (0.0f, 8.0f, rc.density * juce::jlimit (0.5f, 1.5f, (a.crestDb - 8.0f) / 6.0f));
        apply ("den.a.band1.threshold", juce::jlimit (-60.0f, -6.0f, a.bodyDb - 4.0f));
        apply ("den.a.band1.range", range);
        apply ("den.a.band1.ratio", 2.0f);
        apply ("den.a.band1.speed", 60.0f);
        lines.add ("Density: up to " + juce::String (range, 1) + " dB on the quiet parts.");
    }

    // --- Sculptor -------------------------------------------------------------------------
    if (isCustom (currentTarget))
    {
        chain.sculptor.custom = customs[(size_t) (currentTarget - InstrumentTargets::NumTargets)];
        chain.sculptor.hasCustom.store (true);
        apply ("scu.target", (float) InstrumentTargets::NumTargets);   // "Custom"
    }
    else
        apply ("scu.target", (float) currentTarget);
    apply ("scu.a.amount", 30.0f);
    apply ("scu.a.speed", 50.0f);

    // The tiles' mixes, which the intent controls drive.
    apply ("eq.mix", 100.0f);
    apply ("cmp1.mix", 100.0f);
    if (setOrder) setOrder (order);
    builtOrder = order;
    hasBuilt = true;
    summaryText = lines.joinIntoString ("\n");
}
} // namespace planish
