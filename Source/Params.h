#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/EQEngine.h"

namespace params
{
inline juce::String bandId (int band, const char* suffix) { return "b" + juce::String (band + 1) + "_" + suffix; }

inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray n { "Bell", "Low Shelf", "High Shelf", "Low Pass", "High Pass", "Notch", "Band Pass", "Tilt", "All Pass" };
    return n;
}
inline const juce::StringArray& modeNames()
{
    static const juce::StringArray n { "Stereo", "Mid", "Side", "Left", "Right" };
    return n;
}
inline const juce::StringArray& fftSizeNames()
{
    static const juce::StringArray n { "1024", "2048", "4096", "8192", "16384" };
    return n;
}
inline const juce::StringArray& targetNames()
{
    static const juce::StringArray n { "-24 LUFS", "-23 LUFS", "-16 LUFS", "-14 LUFS", "-12 LUFS", "-10 LUFS", "CUSTOM" };
    return n;
}
inline double targetLufs (int index, double custom)
{
    static const double v[] = { -24, -23, -16, -14, -12, -10 };
    return (index >= 0 && index < 6) ? v[index] : custom;
}
inline double defaultBandFreq (int band) { return 40.0 * std::pow (400.0, band / 23.0); }

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const String& s) { return ParameterID (s, 1); };

    auto floatParam = [&] (const String& id, const String& name, float lo, float hi, float def, float centre, const String& label, float step = 0.01f)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (centre > 0) r.setSkewForCentre (centre);
        return std::make_unique<AudioParameterFloat> (pid (id), name, r, def, AudioParameterFloatAttributes().withLabel (label));
    };

    layout.add (std::make_unique<AudioParameterBool> (pid ("bypass"), "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (pid ("autogain"), "Auto Gain", false));
    layout.add (floatParam ("out_gain", "Output Gain", -24.f, 24.f, 0.f, 0.f, "dB"));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("fft_size"), "FFT Size", fftSizeNames(), 2));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("an_source"), "Analyzer Source", StringArray { "Post EQ", "Pre EQ" }, 0));
    layout.add (floatParam ("an_decay", "Analyzer Decay", 3.f, 60.f, 20.f, 0.f, "dB/s", 0.1f));
    layout.add (floatParam ("an_avg", "Analyzer Average", 20.f, 2000.f, 300.f, 300.f, "ms", 1.f));
    layout.add (std::make_unique<AudioParameterChoice> (pid ("target"), "Loudness Target", targetNames(), 3));
    layout.add (floatParam ("target_custom", "Custom Target", -40.f, 0.f, -14.f, 0.f, "LUFS", 0.1f));

    for (int i = 0; i < eqm::kNumBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("band" + String (i + 1), "Band " + String (i + 1), "|");
        grp->addChild (std::make_unique<AudioParameterBool> (pid (bandId (i, "en")), n + "Enable", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (pid (bandId (i, "type")), n + "Type", filterTypeNames(), 0));
        grp->addChild (floatParam (bandId (i, "freq"), n + "Frequency", 20.f, 20000.f, (float) defaultBandFreq (i), 1000.f, "Hz", 0.01f));
        grp->addChild (floatParam (bandId (i, "gain"), n + "Gain", -24.f, 24.f, 0.f, 0.f, "dB"));
        grp->addChild (floatParam (bandId (i, "q"), n + "Q", 0.1f, 18.f, 1.f, 1.f, ""));
        grp->addChild (std::make_unique<AudioParameterChoice> (pid (bandId (i, "mode")), n + "Channel", modeNames(), 0));
        grp->addChild (std::make_unique<AudioParameterBool> (pid (bandId (i, "dyn")), n + "Dynamic", false));
        grp->addChild (floatParam (bandId (i, "thr"), n + "Threshold", -80.f, 0.f, -30.f, 0.f, "dB", 0.1f));
        grp->addChild (floatParam (bandId (i, "atk"), n + "Attack", 0.1f, 300.f, 10.f, 20.f, "ms", 0.1f));
        grp->addChild (floatParam (bandId (i, "rel"), n + "Release", 5.f, 3000.f, 120.f, 150.f, "ms", 1.f));
        grp->addChild (floatParam (bandId (i, "range"), n + "Range", -24.f, 24.f, 6.f, 0.f, "dB", 0.1f));
        grp->addChild (floatParam (bandId (i, "ratio"), n + "Ratio", 1.f, 20.f, 2.f, 3.f, ":1", 0.1f));
        layout.add (std::move (grp));
    }
    return layout;
}
} // namespace params
