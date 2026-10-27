#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Settings.h"

using namespace planish;

namespace
{
constexpr const char* kPresetExtension = ".planish";
constexpr const char* kModulePresetExtension = ".planishmod";
constexpr const char* kOrderProperty = "order";

juce::String orderToString (const std::vector<int>& order)
{
    juce::StringArray a;
    for (int id : order) a.add (juce::String (id));
    return a.joinIntoString (",");
}

std::vector<int> orderFromString (const juce::String& s)
{
    std::vector<int> v;
    juce::StringArray a;
    a.addTokens (s, ",", "");
    for (const auto& t : a)
        if (t.trim().isNotEmpty()) v.push_back (t.getIntValue());
    return v;
}
} // namespace

PlanishProcessor::PlanishProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      assistant (chain),
      apvts (*this, nullptr, "PLANISH", createParameterLayout (chain))
{
    cacheParams();
    for (int m = 0; m < NumModules; ++m)
        chain.module (m).setDefaults();
    apvts.state.setProperty (kOrderProperty, orderToString (chain.getOrder()), nullptr);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.addParameterListener (rp->getParameterID(), this);

    assistant.setParam = [this] (const juce::String& id, float v) { setParam (id, v); };
    assistant.getParam = [this] (const juce::String& id) { return getParam (id); };
    assistant.setOrder = [this] (const std::vector<int>& o) { setOrder (o); };
    assistant.getOrder = [this] { return getOrder(); };

    linkId = LinkRegistry::get().add (this);
    name = "Planish " + juce::String (linkId);
    maskFft.prepare (2048);
    maskL.assign (2048, 0.0f); maskR.assign (2048, 0.0f); maskWin.assign (2048, 0.0f);
    maskBins.assign (1025, Fft::cpx (0.0f, 0.0f));
    for (int i = 0; i < 2048; ++i) maskWin[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * kPif * (float) i / 2048.0f));
    for (auto& v : maskBands) v = -120.0f;

    currentName = factoryPresets().front().name;
    loadedValues = snapshot();
    loadedOrder = getOrder();
    startTimerHz (20);
}

PlanishProcessor::~PlanishProcessor()
{
    stopTimer();
    LinkRegistry::get().remove (this);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (rp->getParameterID(), this);
}

bool PlanishProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    if (! ok (in) || ! ok (out)) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && ! ok (sc)) return false;
    }
    return true;
}

void PlanishProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (64, samplesPerBlock);
    chain.prepare (sampleRate, maxBlock);
    monoScratch.setSize (4, maxBlock);
    readSettings();
    lastLatency = chain.latencyFor (settings);
    setLatencySamples (lastLatency);
}

// ---------------------------------------------------------------------------
void PlanishProcessor::cacheParams()
{
    auto ptr = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    fast.in = ptr (ids::inGain); fast.out = ptr (ids::outGain);
    fast.bypass = ptr (ids::bypass); fast.mono = ptr (ids::mono);
    fast.pan = ptr (ids::pan); fast.invert = ptr (ids::invert); fast.width = ptr (ids::width); fast.swap = ptr (ids::swap);
    fast.limiter = ptr (ids::limiter); fast.ceiling = ptr (ids::ceiling); fast.limStyle = ptr (ids::limStyle); fast.limMode = ptr (ids::limMode);
    fast.zeroLatency = ptr (ids::zeroLatency); fast.gainMatch = ptr (ids::gainMatch); fast.meterType = ptr (ids::meterType);

    moduleParams.resize (NumModules);
    for (int m = 0; m < NumModules; ++m)
    {
        auto& v = moduleParams[(size_t) m];
        v.clear();
        for (const auto& d : chain.module (m).params())
            v.push_back (apvts.getRawParameterValue (d.id));
    }
}

void PlanishProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

float PlanishProcessor::getParam (const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

// Audio thread.
void PlanishProcessor::readSettings()
{
    auto get = [] (const std::atomic<float>* p) { return p->load (std::memory_order_relaxed); };
    settings.inGainDb = get (fast.in);
    settings.outGainDb = get (fast.out);
    settings.bypass = get (fast.bypass) > 0.5f;
    settings.mono = get (fast.mono) > 0.5f;
    settings.pan = isStereo() ? get (fast.pan) : 0.0f;
    settings.invert = get (fast.invert) > 0.5f;
    settings.width = isStereo() ? get (fast.width) : 0.0f;
    settings.swap = get (fast.swap) > 0.5f;
    settings.limiterOn = get (fast.limiter) > 0.5f;
    settings.ceilingDb = get (fast.ceiling);
    settings.limiterStyle = juce::roundToInt (get (fast.limStyle));
    settings.limiterMode = juce::roundToInt (get (fast.limMode));
    settings.zeroLatency = get (fast.zeroLatency) > 0.5f;
    settings.gainMatch = get (fast.gainMatch) > 0.5f;
    settings.meterType = juce::roundToInt (get (fast.meterType));
    settings.soloModule = soloModule.load (std::memory_order_relaxed);
    settings.deltaModule = deltaModule.load (std::memory_order_relaxed);
    settings.analysing = analysing.load (std::memory_order_relaxed);

    for (int m = 0; m < NumModules; ++m)
    {
        auto& mod = chain.module (m);
        const auto& src = moduleParams[(size_t) m];
        float* dst = mod.values();
        for (size_t i = 0; i < src.size(); ++i)
            dst[i] = src[i]->load (std::memory_order_relaxed);
    }
}

void PlanishProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainIn = getBusBuffer (buffer, true, 0);
    auto mainOut = getBusBuffer (buffer, false, 0);
    const int numIn = mainIn.getNumChannels();
    const int numOut = mainOut.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numOut == 0 || n == 0)
        return;
    if (numIn == 1 && numOut >= 2)
        mainOut.copyFrom (1, 0, mainOut, 0, 0, n);

    // The sidechain, if the host is feeding it.
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                scL = sc.getReadPointer (0);
                scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
            }
        }

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm.store (*b);
            const bool isPlaying = pos->getIsPlaying();
            if (isPlaying && ! wasPlaying) meterResets.fetch_add (1);
            wasPlaying = isPlaying;
            playing.store (isPlaying);
            chain.setTransport (bpm.load(), pos->getPpqPosition().orFallback (0.0), isPlaying);
        }

    const int mr = meterResets.load (std::memory_order_relaxed);
    if (mr != seenMeterResets) { seenMeterResets = mr; chain.resetMeters(); }

    readSettings();

    float* left = mainOut.getWritePointer (0);
    float* right = numOut >= 2 ? mainOut.getWritePointer (1) : monoScratch.getWritePointer (0);
    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - start);
        if (numOut < 2) std::copy (left + start, left + start + len, right);
        chain.process (left + start, (numOut >= 2 ? right + start : right), len,
                       scL != nullptr ? scL + start : nullptr, scR != nullptr ? scR + start : nullptr, settings);
        if (numOut < 2)
            for (int i = 0; i < len; ++i) left[start + i] = 0.5f * (left[start + i] + right[i]);
    }
    float pk = activity.load (std::memory_order_relaxed);
    for (int c = 0; c < juce::jmin (2, numOut); ++c)
        pk = juce::jmax (pk, mainOut.getMagnitude (c, 0, n));
    activity.store (pk, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
void PlanishProcessor::updateMaskingSpectrum()
{
    chain.outputTap.latest (maskL.data(), maskR.data(), 2048);
    std::vector<float> frame (2048);
    for (int i = 0; i < 2048; ++i) frame[(size_t) i] = 0.5f * (maskL[(size_t) i] + maskR[(size_t) i]) * maskWin[(size_t) i];
    maskFft.forwardReal (frame.data(), maskBins.data());
    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    for (int b = 0; b < Masking::kBands; ++b)
    {
        const int k0 = juce::jlimit (1, 1024, (int) (Masking::bandLowHz (b) * 2048 / sr));
        const int k1 = juce::jlimit (k0, 1024, (int) (Masking::bandHighHz (b) * 2048 / sr));
        double e = 0;
        for (int k = k0; k <= k1; ++k) e += std::norm (maskBins[(size_t) k]);
        // Band energy, scaled so a full-scale sine reads about 0 dB.
        const float db = 10.0f * std::log10 ((float) e * 4.0f / (1024.0f * 1024.0f) + 1e-14f) + maskingGainOffsetDb;
        maskBands[b] += 0.5f * (db - maskBands[b]);
    }
}

void PlanishProcessor::timerCallback()
{
    // Latency follows the chain (and ZERO LATENCY).
    const int lat = chain.latency();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        setLatencySamples (lat);
    }

    // A finished Phase LEARN.
    if (chain.phase.learnTick())
    {
        setParam (paramId (chain.phase, chain.phase.pRotation), chain.phase.learnedRotation);
        if (chain.phase.learnedDelayValid)
            setParam (paramId (chain.phase, chain.phase.pDelay), chain.phase.learnedDelayMs);
    }

    // The assistant.
    if (assistant.analysing())
    {
        analysing.store (true);
        assistant.tick();
        if (! assistant.analysing())
            analysing.store (false);
    }
    else
        analysing.store (false);

    updateMaskingSpectrum();

    // Undo: snapshot the state a little after the last change.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (dirty.load() && now - lastPushMs > 600.0)
    {
        dirty.store (false);
        juce::String label;
        {
            const juce::SpinLock::ScopedLockType sl (labelLock);
            label = pendingLabel;
            pendingLabel.clear();
        }
        if (label.isEmpty())
            if (auto* p = lastChanged.load())
                label = p->getName (40) + "  " + p->getCurrentValueAsText();
        pushUndo (label);
        lastPushMs = now;
    }
}

void PlanishProcessor::parameterChanged (const juce::String& id, float)
{
    // May come from the audio thread (automation): no allocation here, the
    // label is made on the timer.
    if (applying) return;
    dirty.store (true);
    lastChanged.store (apvts.getParameter (id));
}

// ---------------------------------------------------------------------------
// Order
// ---------------------------------------------------------------------------
void PlanishProcessor::setOrder (const std::vector<int>& order)
{
    chain.setOrder (order);
    apvts.state.setProperty (kOrderProperty, orderToString (chain.getOrder()), nullptr);
    if (! applying)
    {
        dirty.store (true);
        const juce::SpinLock::ScopedLockType sl (labelLock);
        pendingLabel = "Signal chain";
    }
}

void PlanishProcessor::syncOrderFromState()
{
    chain.setOrder (orderFromString (apvts.state.getProperty (kOrderProperty).toString()));
}

void PlanishProcessor::addModule (int id, int position)
{
    auto order = getOrder();
    for (int i : order) if (i == id) return;
    if (position < 0 || position > (int) order.size()) order.push_back (id);
    else order.insert (order.begin() + position, id);
    setOrder (order);
}

void PlanishProcessor::removeModule (int id)
{
    auto order = getOrder();
    order.erase (std::remove (order.begin(), order.end(), id), order.end());
    setOrder (order);
    if (soloModule.load() == id) soloModule.store (-1);
    if (deltaModule.load() == id) deltaModule.store (-1);
}

void PlanishProcessor::moveModule (int id, int newPosition)
{
    auto order = getOrder();
    order.erase (std::remove (order.begin(), order.end(), id), order.end());
    newPosition = juce::jlimit (0, (int) order.size(), newPosition);
    order.insert (order.begin() + newPosition, id);
    setOrder (order);
}

void PlanishProcessor::resetModule (int id)
{
    for (const auto& d : chain.module (id).params())
        if (d.id != chain.module (id).params()[(size_t) chain.module (id).pOn].id)
            setParam (d.id, d.def);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
int PlanishProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String PlanishProcessor::getProgramName (int index)
{
    const auto& f = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) f.size()) ? juce::String (f[(size_t) index].name) : juce::String();
}

juce::File PlanishProcessor::userPresetFolder() { return Settings::file().getParentDirectory().getChildFile ("Presets"); }
juce::File PlanishProcessor::userModulePresetFolder (int module)
{
    // Both compressors share one folder.
    const int m = module == ModCompressor2 ? ModCompressor1 : module;
    const juce::String folder = m == ModCompressor1 ? juce::String ("COMPRESSOR") : juce::String (Chain::moduleShortName (m)).replaceCharacter (' ', '_');
    return Settings::file().getParentDirectory().getChildFile ("Module Presets").getChildFile (folder);
}

juce::Array<juce::File> PlanishProcessor::userPresets()
{
    auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + kPresetExtension);
    files.sort();
    return files;
}

juce::Array<juce::File> PlanishProcessor::userModulePresets (int module)
{
    auto files = userModulePresetFolder (module).findChildFiles (juce::File::findFiles, false, juce::String ("*") + kModulePresetExtension);
    files.sort();
    return files;
}

std::vector<float> PlanishProcessor::snapshot() const
{
    std::vector<float> v;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! ids::notInPreset (rp->getParameterID()))
                v.push_back (rp->getValue());
    return v;
}

bool PlanishProcessor::presetEdited() const
{
    const auto now = snapshot();
    if (now.size() != loadedValues.size()) return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (std::abs (now[i] - loadedValues[i]) > 0.002f) return true;
    return getOrder() != loadedOrder;
}

void PlanishProcessor::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& preset = list[(size_t) index];
    pushUndo ("Before preset " + juce::String (preset.name));
    applying = true;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (ids::notInPreset (rp->getParameterID())) continue;
            float v = rp->getDefaultValue();
            for (const auto& [id, plain] : preset.values)
                if (rp->getParameterID() == id)
                    v = rp->convertTo0to1 (plain);
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
    setOrder (preset.order);
    applying = false;
    factoryIndex = index;
    userFile = juce::File();
    currentName = preset.name;
    loadedValues = snapshot();
    loadedOrder = getOrder();
    pushUndo ("Preset " + juce::String (preset.name));
}

std::unique_ptr<juce::XmlElement> PlanishProcessor::stateXml()
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return nullptr;
    xml->setAttribute ("presetName", currentName);
    xml->setAttribute ("presetFactory", userFile == juce::File() ? factoryIndex : -1);
    xml->setAttribute ("presetFile", userFile.getFullPathName());
    xml->setAttribute (kOrderProperty, orderToString (getOrder()));
    if (chain.sculptor.hasCustom.load())
    {
        juce::StringArray sp;
        for (float v : chain.sculptor.custom.points) sp.add (juce::String (v, 2));
        xml->setAttribute ("sculptorCustom", sp.joinIntoString (","));
        xml->setAttribute ("sculptorCustomName", juce::String (chain.sculptor.custom.name));
    }
    return xml;
}

void PlanishProcessor::applyStateXml (const juce::XmlElement& xml, bool includeOrder)
{
    applying = true;
    for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id");
        if (ids::notInPreset (id)) continue;
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
            p->endChangeGesture();
        }
    }
    if (includeOrder)
        setOrder (orderFromString (xml.getStringAttribute (kOrderProperty)));
    if (xml.hasAttribute ("sculptorCustom"))
    {
        juce::StringArray sp;
        sp.addTokens (xml.getStringAttribute ("sculptorCustom"), ",", "");
        for (int p = 0; p < InstrumentTargets::kPoints && p < sp.size(); ++p) chain.sculptor.custom.points[(size_t) p] = sp[p].getFloatValue();
        chain.sculptor.custom.name = xml.getStringAttribute ("sculptorCustomName").toStdString();
        chain.sculptor.hasCustom.store (true);
    }
    applying = false;
}

bool PlanishProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return false;
    pushUndo ("Before preset " + file.getFileNameWithoutExtension());
    applyStateXml (*xml, true);
    userFile = file;
    factoryIndex = -1;
    currentName = file.getFileNameWithoutExtension();
    loadedValues = snapshot();
    loadedOrder = getOrder();
    pushUndo ("Preset " + currentName);
    return true;
}

bool PlanishProcessor::saveUserPreset (const juce::String& rawName, juce::String& error)
{
    const auto fileName = juce::File::createLegalFileName (rawName.trim());
    if (fileName.isEmpty()) { error = "Give the preset a name."; return false; }
    const auto folder = userPresetFolder();
    if (! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }
    const auto file = folder.getChildFile (fileName + kPresetExtension);
    currentName = fileName;
    userFile = file;
    factoryIndex = -1;
    auto xml = stateXml();
    if (xml != nullptr)
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            if (ids::notInPreset (e->getStringAttribute ("id")))
                e->setAttribute ("value", 0.0);
    if (xml == nullptr || ! xml->writeTo (file)) { error = "Couldn't write " + file.getFullPathName(); return false; }
    loadedValues = snapshot();
    loadedOrder = getOrder();
    return true;
}

void PlanishProcessor::stepPreset (int delta)
{
    const int numFactory = (int) factoryPresets().size();
    const auto users = userPresets();
    const int total = numFactory + users.size();
    int cur = presetNumber() - 1;
    if (cur < 0) cur = 0;
    const int next = (cur + delta + total) % total;
    if (next < numFactory) loadFactory (next);
    else loadUserPreset (users[next - numFactory]);
}

int PlanishProcessor::presetNumber() const
{
    if (userFile == juce::File())
        return factoryIndex >= 0 ? factoryIndex + 1 : 0;
    const auto users = userPresets();
    const int i = users.indexOf (userFile);
    return i >= 0 ? (int) factoryPresets().size() + i + 1 : 0;
}

void PlanishProcessor::loadModulePreset (int module, const ModulePreset& preset)
{
    pushUndo ("Before " + juce::String (Chain::moduleShortName (module)) + " preset");
    applying = true;
    // Compressor presets are written for Compressor 1; they apply to either.
    const juce::String from = chain.module (preset.module).info().id + ".";
    const juce::String to = chain.module (module).info().id + ".";
    for (const auto& d : chain.module (module).params())
    {
        if (d.id == chain.module (module).params()[(size_t) chain.module (module).pOn].id) continue;
        float v = d.def;
        for (const auto& [id, plain] : preset.values)
            if (juce::String (id).replace (from, to) == juce::String (d.id)) v = plain;
        setParam (d.id, v);
    }
    applying = false;
    pushUndo (juce::String (Chain::moduleShortName (module)) + " preset " + preset.name);
}

bool PlanishProcessor::loadUserModulePreset (int module, const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MODULE")) return false;
    pushUndo ("Before " + juce::String (Chain::moduleShortName (module)) + " preset");
    applying = true;
    const juce::String to = juce::String (chain.module (module).info().id) + ".";
    const juce::String from = xml->getStringAttribute ("prefix", to);
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = e->getStringAttribute ("id").replace (from, to);
        if (! id.startsWith (to)) continue;
        setParam (id, (float) e->getDoubleAttribute ("value"));
    }
    applying = false;
    pushUndo (juce::String (Chain::moduleShortName (module)) + " preset " + file.getFileNameWithoutExtension());
    return true;
}

bool PlanishProcessor::saveUserModulePreset (int module, const juce::String& rawName, juce::String& error)
{
    const auto fileName = juce::File::createLegalFileName (rawName.trim());
    if (fileName.isEmpty()) { error = "Give the preset a name."; return false; }
    const auto folder = userModulePresetFolder (module);
    if (! folder.createDirectory()) { error = "Couldn't create " + folder.getFullPathName(); return false; }
    juce::XmlElement xml ("MODULE");
    xml.setAttribute ("module", Chain::moduleName (module));
    xml.setAttribute ("prefix", juce::String (chain.module (module).info().id) + ".");
    for (const auto& d : chain.module (module).params())
    {
        auto* e = xml.createNewChildElement ("PARAM");
        e->setAttribute ("id", d.id);
        e->setAttribute ("value", getParam (d.id));
    }
    if (! xml.writeTo (folder.getChildFile (fileName + kModulePresetExtension))) { error = "Couldn't write the preset."; return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Undo history and sets
// ---------------------------------------------------------------------------
void PlanishProcessor::pushUndo (const juce::String& label)
{
    if (auto xml = stateXml())
    {
        // Skip if nothing changed since the last entry.
        if (! history.empty() && history.back().state->isEquivalentTo (xml.get(), false))
            return;
        history.push_back ({ label.isEmpty() ? juce::String ("Change") : label, std::move (xml) });
        if (history.size() > 80)
            history.erase (history.begin());
    }
}

void PlanishProcessor::undo()
{
    if (history.empty()) return;
    // The top entry is the state after the last change; restore the one before.
    auto current = stateXml();
    if (history.size() >= 2 && current != nullptr && history.back().state->isEquivalentTo (current.get(), false))
        history.pop_back();
    auto item = std::move (history.back());
    history.pop_back();
    applyStateXml (*item.state, true);
    dirty.store (false);
}

void PlanishProcessor::revertTo (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) history.size())) return;
    // Keep the list: carrying on from an earlier point builds on from there.
    applyStateXml (*history[(size_t) index].state, true);
    dirty.store (false);
}

void PlanishProcessor::setSnapshot (int slot)
{
    if (slot < 0 || slot > 3) return;
    snapshots[(size_t) slot] = stateXml();
    activeSnap = slot;
}

void PlanishProcessor::recallSnapshot (int slot)
{
    if (slot < 0 || slot > 3 || snapshots[(size_t) slot] == nullptr) return;
    pushUndo ("Before set " + juce::String::charToString ((juce::juce_wchar) ('A' + slot)));
    applyStateXml (*snapshots[(size_t) slot], true);
    activeSnap = slot;
}

// ---------------------------------------------------------------------------
void PlanishProcessor::startAssistant (float seconds)
{
    pushUndo ("Before the assistant");
    assistant.start (seconds);
    analysing.store (true);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
void PlanishProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateXml())
    {
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        {
            const auto id = e->getStringAttribute ("id");
            if (id == ids::bypass || id == ids::mono || id == ids::swap)
                e->setAttribute ("value", 0.0);
        }
        xml->setAttribute ("instanceName", name);
        xml->setAttribute ("nameEdited", nameEdited);
        xml->setAttribute ("maskingOffset", maskingGainOffsetDb);
        xml->setAttribute ("mixGroup", mixGroup);
        xml->setAttribute ("mixerHidden", mixerHidden);
        copyXmlToBinary (*xml, destData);
    }
}

void PlanishProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    applying = true;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    applying = false;
    if (xml->hasAttribute (kOrderProperty))
        apvts.state.setProperty (kOrderProperty, xml->getStringAttribute (kOrderProperty), nullptr);
    syncOrderFromState();
    if (xml->hasAttribute ("sculptorCustom"))
        applyStateXml (*xml, false);
    nameEdited = xml->getBoolAttribute ("nameEdited", false);
    if (nameEdited) name = xml->getStringAttribute ("instanceName", name);
    maskingGainOffsetDb = (float) xml->getDoubleAttribute ("maskingOffset", 0.0);
    mixGroup = xml->getIntAttribute ("mixGroup", -1);
    mixerHidden = xml->getBoolAttribute ("mixerHidden", false);
    currentName = xml->getStringAttribute ("presetName", currentName);
    factoryIndex = xml->getIntAttribute ("presetFactory", -1);
    userFile = juce::File (xml->getStringAttribute ("presetFile"));
    if (userFile != juce::File() && ! userFile.existsAsFile())
        userFile = juce::File();
    loadedValues = snapshot();
    loadedOrder = getOrder();
}

void PlanishProcessor::updateTrackProperties (const TrackProperties& props)
{
    // May arrive on any thread: hand it to the message thread.
    const auto trackName = props.name;
    const auto colour = props.colour;
    juce::MessageManager::callAsync ([this, trackName, colour, alive = std::weak_ptr<int> (aliveToken)]
    {
        if (alive.expired()) return;
        if (trackName.has_value() && ! nameEdited && trackName->isNotEmpty()) name = *trackName;
        if (colour.has_value()) trackCol = *colour;
    });
}

juce::AudioProcessorEditor* PlanishProcessor::createEditor()
{
    return new PlanishEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlanishProcessor();
}
