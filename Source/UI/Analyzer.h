// Analyzer.h — the panel's spectrum analyser: reads a Tap, runs a 4096-
// point FFT on the message thread, averages and holds peaks, and draws as
// a filled curve (or as 1/3-octave, critical-band or octave bars) on the
// chosen frequency scale. Every spectrum view in the module panels is
// built on SpectrumView.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../Engine/Chain.h"
#include "../Engine/Fft.h"
#include "Widgets.h"

namespace planish::ui
{
class SpectrumSource
{
public:
    static constexpr int kFrame = 4096;
    static constexpr int kColumns = 256;   // the curve is kept per log-spaced column

    SpectrumSource();
    void setSampleRate (double sr) { sampleRate = sr; }
    void setAverageSeconds (float s) { averageSeconds = s; }
    void setTilt (float dbPerOct) { tilt = dbPerOct; }
    void setPeakHold (bool on) { peakHold = on; if (! on) resetPeaks(); }
    void setPeakHoldSeconds (float s) { holdSeconds = s; }   // < 0: infinite
    void setMidSide (bool ms) { midSide = ms; }
    void resetPeaks() { for (auto& v : peak) v = -200.0f; }

    // Pull the latest frame from the tap and update the curves.
    void update (const Tap& tap, int channel = -1);   // -1: both summed; 0 left/mid; 1 right/side

    // Curves in dB (0 dBFS sine = 0), per column (column 0 = 20 Hz).
    const float* curve() const { return avg; }
    const float* peaks() const { return peak; }
    static float columnHz (int col) { return normToFreq ((float) col / (float) (kColumns - 1)); }
    float dbAtHz (float hz) const;

private:
    double sampleRate = 48000.0;
    float averageSeconds = 1.0f, tilt = 3.0f, holdSeconds = 1.0f;
    float peakAge[kColumns] {};
    bool peakHold = true, midSide = false;
    Fft fft;
    std::vector<float> window, frame, left, right, mono;
    std::vector<Fft::cpx> bins;
    float avg[kColumns] {}, peak[kColumns] {};
    bool primed = false;
    int lastPos = -1;
};

// How spectra are drawn, for every view (OPTIONS).
enum SpectrumType { SpectrumLine = 0, SpectrumThird, SpectrumCritical, SpectrumOctave, NumSpectrumTypes };
void setSpectrumType (int);
int spectrumType();
const char* spectrumTypeName (int);

class SpectrumView : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpectrumView();
    bool pianoRoll = true;               // draw keys under the axis on the PIANO ROLL scale
    // What to show.
    SpectrumSource* source = nullptr;
    SpectrumSource* secondary = nullptr;    // e.g. the input, or a reference
    juce::Colour secondaryColour = juce::Colour (0xff6fb2d6);
    float dbTop = 6.0f, dbBottom = -84.0f;   // spectrum scale
    bool showGrid = true, showLabels = true, fill = true, showPeaks = true;

    // Gain scale for curves drawn on top (right-hand ruler).
    float gainTop = 15.0f, gainBottom = -30.0f;
    bool showGainScale = false;

    float xForHz (float hz) const;
    float hzForX (float x) const;
    float yForDb (float db) const;        // spectrum scale
    float yForGain (float db) const;      // curve scale
    float gainForY (float y) const;
    juce::Rectangle<float> plotArea() const;

    void paint (juce::Graphics&) override;
    // Subclasses draw their curves here, after the spectrum.
    virtual void paintOverlay (juce::Graphics&) {}
    void drawCurve (juce::Graphics&, const std::function<float (float hz)>& gainDb, juce::Colour, float thickness = 2.0f, bool fillUnder = false);

protected:
    void drawSpectrum (juce::Graphics&, const SpectrumSource&, juce::Colour line, juce::Colour fillColour, bool withPeaks);
};
} // namespace planish::ui
