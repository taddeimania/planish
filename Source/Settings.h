// Settings.h — per-user preferences shared by every Planish on the machine:
// tooltips and the OPTIONS (analyser, EQ scale, masking histogram). User
// presets and custom targets live in folders beside this file.
//
//   macOS:   ~/Library/Application Support/Planish/Planish.settings
//   Windows: %APPDATA%\Planish\Planish.settings
//   Linux:   ~/.config/Planish/Planish.settings
//
// Message thread only.
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace planish
{
struct SettingsStore
{
    SettingsStore();
    juce::PropertiesFile props;
};

class Settings
{
public:
    bool tooltipsEnabled() const;            // default: on
    void setTooltipsEnabled (bool);

    // Spectrum: averaging (0 real time, 1 1 s, 2 3 s, 3 5 s, 4 10 s, 5 infinite),
    // peak hold on/off and time (0 5 ms .. 4 5 s, 5 infinite), display type,
    // tilt (dB/oct, 3 makes pink read flat).
    int  spectrumAverage() const;  void setSpectrumAverage (int);
    bool spectrumPeakHold() const; void setSpectrumPeakHold (bool);
    int  peakHoldTime() const;     void setPeakHoldTime (int);
    int  spectrumType() const;     void setSpectrumType (int);
    float spectrumTilt() const;    void setSpectrumTilt (float);

    // EQ: frequency scale, alt-solo Q, show the spectrum under the curve.
    int  frequencyScale() const;   void setFrequencyScale (int);
    float altSoloQ() const;        void setAltSoloQ (float);
    bool eqSpectrum() const;       void setEqSpectrum (bool);

    // Masking meter: collision histogram window (0 400 ms, 1 3 s, 2 infinite).
    int  histogramHold() const;    void setHistogramHold (int);

    // Assistant: listening time, seconds.
    float assistantSeconds() const; void setAssistantSeconds (float);

    static juce::File file();
    static float averageSeconds (int index);
    static float holdSeconds (int index);    // < 0: infinite

private:
    juce::SharedResourcePointer<SettingsStore> store;
};
} // namespace planish
