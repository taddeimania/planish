// Snapshot.cpp — dev tool: run Planish without a host and render its panel
// to a PNG, after playing a few seconds of a built-in phrase through it so
// the meters and spectra have something to show.
//   PlanishSnapshot out.png [preset-index [module-id]]   a preset, showing a module
//   PlanishSnapshot out.png assist                         the assistant start page
//   PlanishSnapshot out.png assistrun                      a full assistant pass, then its result
//   PlanishSnapshot out.png mixer                          the Visual Mixer with five tracks
//   PlanishSnapshot out.png mixassist                      the mix assistant's result with five tracks
//   PlanishSnapshot out.png stress [rounds]                open, use and close the editor while audio runs
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <thread>

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

enum Part { Mix = 0, Drums, Bass, Vocal, Pad };

// A few seconds of one part (or all of them) of a simple song.
juce::AudioBuffer<float> testPhrase (double rate, double seconds, int part = Mix)
{
    const int n = (int) (rate * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rng (7);
    const float chords[4][3] { { 220.0f, 261.6f, 329.6f }, { 174.6f, 220.0f, 261.6f }, { 196.0f, 246.9f, 293.7f }, { 164.8f, 196.0f, 246.9f } };
    const float roots[4] { 55.0f, 43.65f, 49.0f, 41.2f };
    const float melody[8] { 440.0f, 523.3f, 493.9f, 440.0f, 392.0f, 440.0f, 329.6f, 392.0f };
    const int beat = (int) (rate * 0.5);
    float vocalPhase = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const int bar = (i / (beat * 4)) % 4;
        const int inBar = i % (beat * 4);
        float l = 0.0f, r = 0.0f;
        if (part == Mix || part == Pad)
        {
            const float t = (float) (inBar % (beat * 2)) / (float) rate;
            const float env = std::exp (-t * 2.5f);
            float x = 0.0f;
            for (float f : chords[bar])
            {
                const float ph = (float) i * f / (float) rate;
                x += env * (std::sin (kTwoPi * ph) + 0.3f * std::sin (kTwoPi * 2.0f * ph));
            }
            l += x * 0.12f; r += x * 0.1f;
        }
        if (part == Mix || part == Drums)
        {
            const float tb = (float) (i % beat) / (float) rate;
            const float kick = 0.4f * std::exp (-tb * 18.0f) * std::sin (kTwoPi * 55.0f * tb * (1.0f + 2.0f * std::exp (-tb * 30.0f)));
            const float th = (float) ((i + beat / 2) % beat) / (float) rate;
            const float hat = 0.06f * std::exp (-th * 60.0f) * (rng.nextFloat() * 2.0f - 1.0f);
            const float ts = (float) ((i + beat) % (beat * 2)) / (float) rate;
            const float snare = 0.18f * std::exp (-ts * 22.0f) * ((rng.nextFloat() * 2.0f - 1.0f) + 0.6f * std::sin (kTwoPi * 190.0f * ts));
            l += kick + hat + snare; r += kick - hat * 0.5f + snare;
        }
        if (part == Mix || part == Bass)
        {
            const float t = (float) (i % beat) / (float) rate;
            const float ph = (float) i * roots[bar] / (float) rate;
            const float x = 0.3f * std::exp (-t * 3.0f) * std::tanh (2.0f * std::sin (kTwoPi * ph));
            l += x; r += x;
        }
        if (part == Mix || part == Vocal)
        {
            // A sung line: a buzzy source through two formants, with vibrato.
            const int note = (i / beat) % 8;
            const float t = (float) (i % beat) / (float) rate;
            const float f = melody[note] * 0.5f * (1.0f + 0.006f * std::sin (kTwoPi * 5.5f * (float) i / (float) rate));
            vocalPhase += f / (float) rate;
            vocalPhase -= std::floor (vocalPhase);
            float x = 0.0f;
            for (int h = 1; h <= 16; ++h)
            {
                const float hf = f * (float) h;
                const float formant = std::exp (-std::pow ((hf - 700.0f) / 300.0f, 2.0f)) + 0.6f * std::exp (-std::pow ((hf - 1200.0f) / 400.0f, 2.0f)) + 0.05f;
                x += formant * std::sin (kTwoPi * vocalPhase * (float) h) / (float) h;
            }
            x *= 0.25f * juce::jmin (1.0f, t * 30.0f) * (t < 0.45f ? 1.0f : 0.2f);
            l += x; r += x;
        }
        b.setSample (0, i, l);
        b.setSample (1, i, r);
    }
    return b;
}

void play (PlanishProcessor& proc, juce::AudioBuffer<float>& phrase, int blocks, int& pos, int block)
{
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i)
    {
        if (pos + block > phrase.getNumSamples()) pos = 0;
        juce::AudioBuffer<float> view (phrase.getArrayOfWritePointers(), 2, pos, block);
        juce::AudioBuffer<float> copy;
        copy.makeCopyOf (view);
        proc.processBlock (copy, midi);
        pos += block;
    }
}

void press (juce::Component& c, const juce::String& label)
{
    if (auto* b = dynamic_cast<planish::ui::PanelButton*> (&c))
        if (b->getLabel() == label && b->isVisible() && b->onClick) { b->onClick ({}); return; }
    for (auto* ch : c.getChildren()) press (*ch, label);
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
    {
        std::printf ("usage: PlanishSnapshot out.png [preset-index [module-id]] | assist | assistrun | mixer | mixassist | stress [rounds]\n");
        return 1;
    }
    const double rate = 48000.0;
    const int block = 512;
    auto proc = std::make_unique<PlanishProcessor>();
    proc->setPlayConfigDetails (2, 2, rate, block);
    proc->prepareToPlay (rate, block);

    const juce::String mode = argc > 2 ? argv[2] : "1";
    if (mode == "stress")
    {
        // What a host does: audio on its own thread the whole time, while the
        // editor is opened, used and closed again and again.
        proc->loadFactory (5);   // the drum bus: several modules
        auto phraseS = testPhrase (rate, 6.0);
        std::atomic<bool> running { true };
        std::thread audio ([&]
        {
            int pos = 0;
            while (running.load())
            {
                play (*proc, phraseS, 1, pos, block);
                std::this_thread::sleep_for (std::chrono::microseconds (500));
            }
        });
        const int rounds = argc > 3 ? juce::String (argv[3]).getIntValue() : 6;
        for (int round = 0; round < rounds; ++round)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
            ed->setSize (PlanishPanel::designW, PlanishPanel::designH);
            pump (150);
            // Visit every module panel, the assistant and the mixer.
            for (int id = 0; id < planish::NumModules; ++id)
            {
                proc->addModule (id);
                std::function<void (juce::Component&)> pressTile = [&] (juce::Component& c)
                {
                    if (auto* strip = dynamic_cast<ChainStrip*> (&c))
                        if (strip->onSelect) strip->onSelect (id);
                    for (auto* ch : c.getChildren()) pressTile (*ch);
                };
                pressTile (*ed);
                pump (60);
            }
            press (*ed, "ASSIST");
            pump (100);
            press (*ed, "ASSIST");
            press (*ed, "MIXER");
            pump (100);
            press (*ed, "MIXER");
            if (round % 2 == 1)
                proc->loadFactory (round % (int) planish::factoryPresets().size());
            if (round % 3 == 2)
                proc->removeModule (proc->getOrder().empty() ? 0 : proc->getOrder().front());
            ed.reset();   // closing the plugin window
            pump (50);
            std::printf ("round %d ok, %d modules, latency %d\n", round + 1, (int) proc->getOrder().size(), proc->getLatencySamples());
        }
        running.store (false);
        audio.join();
        std::printf ("stress done\n");
        return 0;
    }

    // The other tracks of a session, for the mixer pages.
    std::vector<std::unique_ptr<PlanishProcessor>> others;
    std::vector<juce::AudioBuffer<float>> otherPhrases;
    const bool mixerMode = mode == "mixer" || mode == "mixassist";
    if (mixerMode)
    {
        struct T { const char* name; juce::uint32 colour; int part; float gain, pan, width; };
        const T tracks[] { { "Drums", 0xffff6f3c, Drums, 2.0f, 0.0f, 40.0f }, { "Bass", 0xff3cc8ff, Bass, -4.0f, 0.0f, -100.0f },
                           { "Keys", 0xff9ad75c, Pad, -1.5f, 35.0f, 20.0f }, { "Synth Pad", 0xffa98dff, Pad, -9.0f, -45.0f, 80.0f } };
        proc->updateTrackProperties ({ juce::String ("Lead Vox"), juce::Colour (0xffffd21f) });
        proc->loadFactory (1);
        for (const auto& t : tracks)
        {
            auto p = std::make_unique<PlanishProcessor>();
            p->setPlayConfigDetails (2, 2, rate, block);
            p->prepareToPlay (rate, block);
            p->updateTrackProperties ({ juce::String (t.name), juce::Colour (t.colour) });
            p->setParam (planish::ids::outGain, t.gain);
            p->setParam (planish::ids::pan, t.pan);
            p->setParam (planish::ids::width, t.width);
            others.push_back (std::move (p));
            otherPhrases.push_back (testPhrase (rate, 6.0, t.part));
        }
        pump (50);
    }
    else if (mode != "assist" && mode != "assistrun")
    {
        proc->loadFactory (mode.getIntValue());
        if (argc > 3)
            proc->addModule (juce::String (argv[3]).getIntValue(), 0);
    }
    auto phrase = testPhrase (rate, 6.0, mixerMode ? Vocal : Mix);
    int pos = 0;
    std::vector<int> otherPos (others.size(), 0);
    auto playAll = [&] (int blocks)
    {
        play (*proc, phrase, blocks, pos, block);
        for (size_t k = 0; k < others.size(); ++k) play (*others[k], otherPhrases[k], blocks, otherPos[k], block);
    };
    playAll (phrase.getNumSamples() / block);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
    editor->setSize (PlanishPanel::designW, PlanishPanel::designH);
    // Let the timers run a few frames so meters and spectra fill.
    const int frames = juce::SystemStats::getEnvironmentVariable ("PLANISH_FRAMES", "160").getIntValue();
    for (int i = 0; i < frames; ++i) { playAll (2); pump (33); }

    if (mode == "assist")
    {
        press (*editor, "ASSIST");
        pump (100);
    }
    if (mode == "assistrun")
    {
        // A full assistant pass on the phrase, then the result page.
        proc->startAssistant (6.0f);
        for (int i = 0; i < 2000 && proc->assistant.analysing(); ++i) { playAll (1); pump (2); }
        pump (200);
        std::printf ("assistant built: %d, detected %s, order size %d\n", (int) proc->assistant.built(),
                     proc->assistant.targetName (proc->assistant.detected()).toRawUTF8(), (int) proc->getOrder().size());
        for (int i = 0; i < 30; ++i) { playAll (2); pump (40); }
        // Save and reload the state as a host would, then check the order survived.
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        proc->setStateInformation (state.getData(), (int) state.getSize());
        std::printf ("after state round trip: order size %d, latency %d\n", (int) proc->getOrder().size(), proc->getLatencySamples());
        for (int i = 0; i < 10; ++i) { playAll (2); pump (40); }
    }
    if (mixerMode)
    {
        press (*editor, "MIXER");
        pump (100);
        if (mode == "mixassist")
        {
            press (*editor, "MIX ASSISTANT");
            pump (60);
            press (*editor, "BEGIN LISTENING");
            pump (60);
            // The mix assistant starts listening when the host plays.
            for (auto* p : planish::LinkRegistry::get().all())
            {
                struct Head final : juce::AudioPlayHead
                {
                    juce::Optional<PositionInfo> getPosition() const override
                    {
                        PositionInfo i;
                        i.setIsPlaying (true);
                        i.setBpm (120.0);
                        i.setPpqPosition (0.0);
                        return i;
                    }
                };
                static Head head;
                p->setPlayHead (&head);
            }
            playAll (4);
            pump (100);
            for (int i = 0; i < 40; ++i) { playAll (24); pump (20); }
            press (*editor, "GO TO RESULTS");
            pump (100);
            for (auto* p : planish::LinkRegistry::get().all())
                std::printf ("  %-10s out %+.1f dB, loudness %.1f LUFS\n", p->instanceName().toRawUTF8(), p->getParam (planish::ids::outGain), p->mixLoudness());
        }
        for (int i = 0; i < 10; ++i) { playAll (2); pump (40); }
    }
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);
    juce::PNGImageFormat png;
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (! png.writeImageToStream (image, os))
    {
        std::printf ("couldn't write %s\n", argv[1]);
        return 1;
    }
    std::printf ("wrote %s (%d x %d), latency %d samples, chain:", argv[1], image.getWidth(), image.getHeight(), proc->getLatencySamples());
    for (int id : proc->getOrder()) std::printf (" %s", planish::Chain::moduleShortName (id));
    std::printf ("\n");
    editor.reset();
    pump (50);
    others.clear();
    return 0;
}
