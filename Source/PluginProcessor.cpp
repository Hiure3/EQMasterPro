#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/Presets.h"

using namespace eqm;

EQMasterProAudioProcessor::EQMasterProAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "EQMasterPro", params::createLayout())
{
    for (int i = 0; i < kNumBands; ++i)
    {
        auto raw = [&] (const char* s) { return apvts.getRawParameterValue (params::bandId (i, s)); };
        bandPtrs[(size_t) i] = { raw ("en"), raw ("type"), raw ("freq"), raw ("gain"), raw ("q"), raw ("mode"),
                                 raw ("dyn"), raw ("thr"), raw ("atk"), raw ("rel"), raw ("range"), raw ("ratio") };
    }
    bypassP = apvts.getRawParameterValue ("bypass");
    autoGainP = apvts.getRawParameterValue ("autogain");
    outGainP = apvts.getRawParameterValue ("out_gain");

    for (int i = 0; i < Meters::kHistory; ++i)
    {
        meters.histM[(size_t) i].store (-120.f); meters.histS[(size_t) i].store (-120.f);
        meters.histI[(size_t) i].store (-120.f); meters.histTP[(size_t) i].store (-120.f);
    }
}

bool EQMasterProAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

juce::AudioProcessorEditor* EQMasterProAudioProcessor::createEditor() { return new EQMasterProAudioProcessorEditor (*this); }

void EQMasterProAudioProcessor::prepareToPlay (double sampleRate, int)
{
    fs = (std::isfinite (sampleRate) && sampleRate >= 8000.0 && sampleRate <= 768000.0) ? sampleRate : 44100.0;
    currentSampleRate.store (fs);
    engine.prepare (fs);
    loudness.prepare (fs);
    bypassMix = bypassP->load() > 0.5f ? 1.0 : 0.0;
    gainCurrent = autoGainSmoothed = 1.0;
    rmsL = rmsR = 0; peakHold = 0; clipping[0] = clipping[1] = false; clipEvents = 0; historyCounter = 0;
    meters.clipCount.store (0);
}

BandParams EQMasterProAudioProcessor::readBandParams (int i) const
{
    const auto& b = bandPtrs[(size_t) i];
    BandParams p;
    p.enabled = b.en->load() > 0.5f;
    p.type = (FilterType) juce::jlimit (0, (int) FilterType::Count - 1, (int) std::lround (b.type->load()));
    p.mode = (ChannelMode) juce::jlimit (0, (int) ChannelMode::Count - 1, (int) std::lround (b.mode->load()));
    p.freq = b.freq->load(); p.gain = b.gain->load(); p.q = b.q->load();
    p.dynEnabled = b.dyn->load() > 0.5f;
    p.thresholdDb = b.thr->load(); p.attackMs = b.atk->load(); p.releaseMs = b.rel->load();
    p.rangeDb = b.range->load(); p.ratio = b.ratio->load();
    return p;
}

float EQMasterProAudioProcessor::getRaw (const juce::String& id) const
{
    if (auto* p = apvts.getRawParameterValue (id)) return p->load();
    return 0.0f;
}

// ======================================================================================================
// AUDIO THREAD — no allocation, no locks, no I/O below this line (processBlock)
// ======================================================================================================
void EQMasterProAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples <= 0) return;

    if (meters.resetRequest.exchange (false))
    {
        loudness.reset();
        clipEvents = 0; clipping[0] = clipping[1] = false; peakHold = 0.0f;
        meters.clipCount.store (0);
    }

    for (int i = 0; i < kNumBands; ++i) engine.setBand (i, readBandParams (i));

    const bool bypass = bypassP->load() > 0.5f;
    const bool autoGainOn = autoGainP->load() > 0.5f;
    const double outGain = std::pow (10.0, juce::jlimit (-24.0f, 24.0f, outGainP->load()) / 20.0);
    const double autoGainTarget = autoGainOn ? std::pow (10.0, juce::jlimit (-24.0, 24.0, -engine.pinkNoiseGainDb()) / 20.0) : 1.0;

    constexpr int kSlice = 256;
    double dryL[kSlice], dryR[kSlice], wl[kSlice], wr[kSlice];
    float mono[kSlice];

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    const double bypassStep = 1.0 / std::max (1.0, 0.01 * fs);
    const double bypassTarget = bypass ? 1.0 : 0.0;
    const double gCoef = 1.0 - std::exp (-1.0 / (0.01 * fs));

    for (int pos = 0; pos < numSamples; pos += kSlice)
    {
        const int c = std::min (kSlice, numSamples - pos);

        for (int i = 0; i < c; ++i)
        {
            double xl = L[pos + i], xr = R[pos + i];
            if (! std::isfinite (xl)) xl = 0.0;
            if (! std::isfinite (xr)) xr = 0.0;
            dryL[i] = xl; dryR[i] = xr; wl[i] = xl; wr[i] = xr;
            mono[i] = (float) (0.5 * (xl + xr));
        }
        preTap.push (mono, c);

        engine.process (wl, wr, c);

        double pk = 0.0;
        for (int i = 0; i < c; ++i) pk = std::max (pk, std::max (std::abs (wl[i]), std::abs (wr[i])));

        // Auto Gain: slow smoothing, but never allowed to push the signal past 0 dBFS
        autoGainSmoothed += (autoGainTarget - autoGainSmoothed) * 0.05;
        double gAuto = autoGainSmoothed;
        bool protect = false;
        if (autoGainOn && gAuto > 1.0 && pk * gAuto * outGain > 0.9999)
        {
            gAuto = std::max (1.0, 0.9999 / std::max (pk * outGain, 1e-9));
            autoGainSmoothed = std::min (autoGainSmoothed, gAuto);
            protect = true;
        }
        const double gTarget = gAuto * outGain;

        for (int i = 0; i < c; ++i)
        {
            gainCurrent += (gTarget - gainCurrent) * gCoef;
            if (protect && gainCurrent > gTarget) gainCurrent = gTarget;

            if (bypassMix < bypassTarget) bypassMix = std::min (bypassTarget, bypassMix + bypassStep);
            else if (bypassMix > bypassTarget) bypassMix = std::max (bypassTarget, bypassMix - bypassStep);

            const double ol = wl[i] * gainCurrent * (1.0 - bypassMix) + dryL[i] * bypassMix;
            const double orr = wr[i] * gainCurrent * (1.0 - bypassMix) + dryR[i] * bypassMix;
            L[pos + i] = (float) ol; R[pos + i] = (float) orr;
            mono[i] = (float) (0.5 * (ol + orr));
        }
        postTap.push (mono, c);
    }

    // ---- metering (on the final output) ----
    loudness.process (L, R, numSamples);

    const double rmsCoef = 1.0 - std::exp (-1.0 / (0.3 * fs));
    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float a = std::abs (L[i]), b = std::abs (R[i]);
        blockPeak = std::max (blockPeak, std::max (a, b));
        rmsL += ((double) a * a - rmsL) * rmsCoef;
        rmsR += ((double) b * b - rmsR) * rmsCoef;
        const float ch[2] = { a, b };
        for (int k = 0; k < 2; ++k)
        {
            const bool over = ch[k] >= 1.0f;
            if (over && ! clipping[k]) ++clipEvents;
            clipping[k] = over;
        }
    }
    peakHold = std::max (peakHold, blockPeak);

    auto toDb = [] (double lin) { return (float) (20.0 * std::log10 (std::max (lin, 1e-6))); };
    const float tL = (float) loudness.truePeakDb (0), tR = (float) loudness.truePeakDb (1);
    meters.momentary.store ((float) loudness.momentary());
    meters.shortTerm.store ((float) loudness.shortTerm());
    meters.integrated.store ((float) loudness.integrated());
    meters.lra.store ((float) loudness.lra());
    meters.tpL.store (tL); meters.tpR.store (tR); meters.tpMax.store (std::max (tL, tR));
    meters.rms.store ((float) (10.0 * std::log10 (std::max (0.5 * (rmsL + rmsR), 1e-12))));
    meters.peak.store (toDb (peakHold));
    meters.clipCount.store (clipEvents);

    historyCounter += numSamples;
    if (historyCounter >= (int) (0.5 * fs))
    {
        historyCounter = 0;
        const int w = meters.historyWrite.load();
        meters.histM[(size_t) w].store ((float) loudness.momentary());
        meters.histS[(size_t) w].store ((float) loudness.shortTerm());
        meters.histI[(size_t) w].store ((float) loudness.integrated());
        meters.histTP[(size_t) w].store (std::max (tL, tR));
        meters.historyWrite.store ((w + 1) % Meters::kHistory);
    }
}

// ======================================================================================================
// GUI-thread helpers
// ======================================================================================================
void EQMasterProAudioProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void EQMasterProAudioProcessor::resetBand (int i)
{
    for (const char* s : { "en", "type", "freq", "gain", "q", "mode", "dyn", "thr", "atk", "rel", "range", "ratio" })
        if (auto* p = apvts.getParameter (params::bandId (i, s)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

void EQMasterProAudioProcessor::applyPreset (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];

    for (int i = 0; i < kNumBands; ++i) resetBand (i);
    int i = 0;
    for (const auto& b : pr.bands)
    {
        if (i >= kNumBands) break;
        setParam (params::bandId (i, "type"), (float) (int) b.type);
        setParam (params::bandId (i, "freq"), (float) b.freq);
        setParam (params::bandId (i, "gain"), (float) b.gain);
        setParam (params::bandId (i, "q"), (float) b.q);
        setParam (params::bandId (i, "mode"), (float) (int) b.mode);
        setParam (params::bandId (i, "dyn"), b.dyn ? 1.f : 0.f);
        if (b.dyn)
        {
            setParam (params::bandId (i, "thr"), (float) b.thr);
            setParam (params::bandId (i, "ratio"), (float) b.ratio);
            setParam (params::bandId (i, "range"), (float) b.range);
        }
        setParam (params::bandId (i, "en"), 1.f);
        ++i;
    }
    if (pr.target >= 0) setParam ("target", (float) pr.target);
}

int EQMasterProAudioProcessor::findFreeBand() const
{
    for (int i = 0; i < kNumBands; ++i)
        if (getRaw (params::bandId (i, "en")) < 0.5f) return i;
    return -1;
}

int EQMasterProAudioProcessor::applySuggestion()
{
    int applied = 0;
    for (const auto& s : suggestion)
    {
        const int i = findFreeBand();
        if (i < 0) break;
        resetBand (i);
        setParam (params::bandId (i, "type"), (float) (int) s.type);
        setParam (params::bandId (i, "freq"), (float) s.freq);
        setParam (params::bandId (i, "gain"), (float) s.gain);
        setParam (params::bandId (i, "q"), (float) s.q);
        setParam (params::bandId (i, "en"), 1.f);
        ++applied;
    }
    suggestion.clear();
    return applied;
}

void EQMasterProAudioProcessor::switchAB()
{
    abSlots[abSlot] = apvts.copyState();
    const int other = abSlot ^ 1;
    if (! abSlots[other].isValid()) abSlots[other] = abSlots[abSlot].createCopy();
    abSlot = other;
    apvts.replaceState (abSlots[abSlot].createCopy());
}

void EQMasterProAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
    {
        xml->setAttribute ("abSlot", abSlot);
        const int other = abSlot ^ 1;
        if (abSlots[other].isValid())
            if (auto* holder = xml->createNewChildElement ("ABOTHER"))
                if (auto otherXml = abSlots[other].createXml())
                    holder->addChildElement (otherXml.release());
        copyXmlToBinary (*xml, dest);
    }
}

void EQMasterProAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        const int slot = juce::jlimit (0, 1, xml->getIntAttribute ("abSlot", 0));
        juce::ValueTree otherTree;
        if (auto* holder = xml->getChildByName ("ABOTHER"))
        {
            if (auto* inner = holder->getFirstChildElement()) otherTree = juce::ValueTree::fromXml (*inner);
            xml->removeChildElement (holder, true);
        }
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            abSlot = slot;
            abSlots[abSlot] = juce::ValueTree();
            abSlots[abSlot ^ 1] = otherTree;
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EQMasterProAudioProcessor(); }
