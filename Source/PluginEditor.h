#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include "PluginProcessor.h"

namespace ui
{
class SpectrumView;
class BandPanel;
class LoudnessPanel;
class DarkLookAndFeel;
struct Shared;
}

class EQMasterProAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EQMasterProAudioProcessorEditor (EQMasterProAudioProcessor&);
    ~EQMasterProAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void startLearning (int mode);
    void stopCaptures();
    void setStatus (const juce::String& text);

    EQMasterProAudioProcessor& proc;
    std::unique_ptr<ui::DarkLookAndFeel> lnf;
    std::unique_ptr<ui::Shared> shared;
    std::unique_ptr<ui::SpectrumView> spectrum;
    std::unique_ptr<ui::BandPanel> bandPanel;
    std::unique_ptr<ui::LoudnessPanel> loudPanel;

    // header
    juce::TextButton abA { "A" }, abB { "B" }, bypassBtn { "BYPASS" };
    juce::ComboBox fftBox, sourceBox;
    juce::Slider avgSlider, decaySlider;
    juce::Label avgLabel, decayLabel;

    // tools
    juce::TextButton autoEqBtn { "AUTO EQ" }, acceptBtn { "ACCEPT" }, rejectBtn { "REJECT" };
    juce::TextButton matchCurBtn { "CAPTURE CURRENT" }, matchRefBtn { "CAPTURE REFERENCE" }, matchBtn { "MATCH" };
    juce::TextButton autoGainBtn { "AUTO GAIN" };
    juce::ComboBox presetBox;
    juce::Slider outGainSlider;
    juce::Label outLabel, statusLabel, eqMatchLabel;

    using BtnAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CmbAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SldAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<BtnAtt> bypassAtt, autoGainAtt;
    std::unique_ptr<CmbAtt> fftAtt, sourceAtt;
    std::unique_ptr<SldAtt> avgAtt, decayAtt, outAtt;

    enum class Capture { Idle, AutoLearning, Current, Reference };
    Capture capture = Capture::Idle;
    juce::uint32 captureStart = 0;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQMasterProAudioProcessorEditor)
};
