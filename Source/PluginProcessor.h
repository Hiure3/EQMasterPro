#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include <vector>
#include "Params.h"
#include "DSP/EQEngine.h"
#include "DSP/Loudness.h"
#include "DSP/SpectrumTools.h"

// Lock-free audio -> GUI sample transport (single producer / single consumer)
class AudioTap
{
public:
    void push (const float* data, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        if (z1 + z2 < n) return; // GUI is not keeping up (editor closed): drop
        std::copy (data, data + z1, buffer.begin() + s1);
        std::copy (data + z1, data + z1 + z2, buffer.begin() + s2);
        fifo.finishedWrite (z1 + z2);
    }
    int pull (float* dst, int maxN) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (std::min (maxN, fifo.getNumReady()), s1, z1, s2, z2);
        std::copy (buffer.begin() + s1, buffer.begin() + s1 + z1, dst);
        std::copy (buffer.begin() + s2, buffer.begin() + s2 + z2, dst + z1);
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }
private:
    static constexpr int kSize = 32768;
    juce::AbstractFifo fifo { kSize };
    std::array<float, kSize> buffer {};
};

struct Meters
{
    static constexpr int kHistory = 600; // 2 points per second = 5 minutes
    std::atomic<float> momentary { -120.f }, shortTerm { -120.f }, integrated { -120.f }, lra { 0.f };
    std::atomic<float> tpL { -120.f }, tpR { -120.f }, tpMax { -120.f }, rms { -120.f }, peak { -120.f };
    std::atomic<int> clipCount { 0 };
    std::atomic<bool> resetRequest { false };
    std::atomic<int> historyWrite { 0 };
    std::array<std::atomic<float>, kHistory> histM, histS, histI, histTP;
};

class EQMasterProAudioProcessor : public juce::AudioProcessor
{
public:
    EQMasterProAudioProcessor();
    ~EQMasterProAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "EQ Master Pro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- shared with the editor ----
    juce::AudioProcessorValueTreeState apvts;
    Meters meters;
    AudioTap preTap, postTap;
    std::atomic<double> currentSampleRate { 44100.0 };

    // GUI-thread-only state
    eqm::SpectrumAccumulator autoAcc, matchCurrent, matchReference;
    std::vector<eqm::BandSuggestion> suggestion;

    eqm::BandParams readBandParams (int band) const;
    float getRaw (const juce::String& id) const;
    void setParam (const juce::String& id, float plainValue);   // with its own change gesture
    void resetBand (int band);
    void applyPreset (int index);
    int  applySuggestion();                                      // returns number of bands created
    int  findFreeBand() const;
    void switchAB();
    int  getABSlot() const { return abSlot; }

private:
    struct BandPtrs { std::atomic<float> *en, *type, *freq, *gain, *q, *mode, *dyn, *thr, *atk, *rel, *range, *ratio; };
    std::array<BandPtrs, eqm::kNumBands> bandPtrs {};
    std::atomic<float> *bypassP = nullptr, *autoGainP = nullptr, *outGainP = nullptr;

    eqm::EQEngine engine;
    eqm::LoudnessMeter loudness;

    double fs = 44100.0;
    double bypassMix = 0.0, gainCurrent = 1.0, autoGainSmoothed = 1.0;
    double rmsL = 0, rmsR = 0;
    float peakHold = 0.0f;
    bool clipping[2] = { false, false };
    int clipEvents = 0;
    int historyCounter = 0;

    juce::ValueTree abSlots[2];
    int abSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQMasterProAudioProcessor)
};
