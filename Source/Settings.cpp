#include "Settings.h"

namespace planish
{
namespace
{
juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName         = "Planish";
    o.filenameSuffix          = ".settings";
   #if JUCE_LINUX
    o.folderName              = ".config/Planish";
   #else
    o.folderName              = "Planish";
   #endif
    o.osxLibrarySubFolder     = "Application Support";
    o.storageFormat           = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1;   // every change is saved explicitly
    return o;
}
} // namespace

SettingsStore::SettingsStore() : props (options()) {}

juce::File Settings::file() { return options().getDefaultFile(); }

float Settings::averageSeconds (int i) { const float a[] { 0.0f, 1.0f, 3.0f, 5.0f, 10.0f, 60.0f }; return a[juce::jlimit (0, 5, i)]; }
float Settings::holdSeconds (int i) { const float h[] { 0.005f, 0.25f, 0.5f, 1.0f, 5.0f, -1.0f }; return h[juce::jlimit (0, 5, i)]; }

#define PLANISH_SETTING_BOOL(getter, setter, key, def) \
    bool Settings::getter() const { return store->props.getBoolValue (key, def); } \
    void Settings::setter (bool v) { store->props.setValue (key, v); store->props.saveIfNeeded(); }
#define PLANISH_SETTING_INT(getter, setter, key, def) \
    int Settings::getter() const { return store->props.getIntValue (key, def); } \
    void Settings::setter (int v) { store->props.setValue (key, v); store->props.saveIfNeeded(); }
#define PLANISH_SETTING_FLOAT(getter, setter, key, def) \
    float Settings::getter() const { return (float) store->props.getDoubleValue (key, def); } \
    void Settings::setter (float v) { store->props.setValue (key, v); store->props.saveIfNeeded(); }

PLANISH_SETTING_BOOL  (tooltipsEnabled,  setTooltipsEnabled,  "tooltips", true)
PLANISH_SETTING_INT   (spectrumAverage,  setSpectrumAverage,  "spectrumAverage", 1)
PLANISH_SETTING_BOOL  (spectrumPeakHold, setSpectrumPeakHold, "spectrumPeakHold", true)
PLANISH_SETTING_INT   (peakHoldTime,     setPeakHoldTime,     "peakHoldTime", 3)
PLANISH_SETTING_INT   (spectrumType,     setSpectrumType,     "spectrumType", 0)
PLANISH_SETTING_FLOAT (spectrumTilt,     setSpectrumTilt,     "spectrumTilt", 3.0)
PLANISH_SETTING_INT   (frequencyScale,   setFrequencyScale,   "frequencyScale", 0)
PLANISH_SETTING_FLOAT (altSoloQ,         setAltSoloQ,         "altSoloQ", 2.0)
PLANISH_SETTING_BOOL  (eqSpectrum,       setEqSpectrum,       "eqSpectrum", true)
PLANISH_SETTING_INT   (histogramHold,    setHistogramHold,    "histogramHold", 1)
PLANISH_SETTING_FLOAT (assistantSeconds, setAssistantSeconds, "assistantSeconds", 8.0)
} // namespace planish
