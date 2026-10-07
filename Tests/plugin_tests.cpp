// Plugin-level tests: instantiate the real processor (no GUI, no host) and run audio through it.
#include "../Source/PluginProcessor.h"
#include "../Source/DSP/Presets.h"
#include <cstdio>

static int failures = 0, checks = 0;
#define CHECK(c, m) do { ++checks; if (! (c)) { ++failures; std::printf ("  FAIL: %s [line %d]\n", m, __LINE__); } } while (0)
#define NEAR(a, b, t, m) do { ++checks; const double _a = (a), _b = (b); if (! (std::abs (_a - _b) <= (t))) { ++failures; std::printf ("  FAIL: %s got %.3f expected %.3f [line %d]\n", m, _a, _b, __LINE__); } } while (0)

static double rmsDb (const juce::AudioBuffer<float>& b, int from)
{
    double s = 0; int n = 0;
    for (int ch = 0; ch < 2; ++ch) for (int i = from; i < b.getNumSamples(); ++i) { const double v = b.getSample (ch, i); s += v * v; ++n; }
    return 10.0 * std::log10 (s / n + 1e-30);
}

static juce::AudioBuffer<float> makeSine (double fs, double f, double db, int n)
{
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i) { const float v = (float) (std::pow (10.0, db / 20.0) * std::sin (2 * juce::MathConstants<double>::pi * f * i / fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}

static void run (EQMasterProAudioProcessor& p, juce::AudioBuffer<float>& b, int block)
{
    juce::MidiBuffer midi;
    for (int pos = 0; pos < b.getNumSamples(); pos += block)
    {
        const int n = std::min (block, b.getNumSamples() - pos);
        juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, pos, n);
        p.processBlock (view, midi);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000;

    {   std::printf ("[metering through the real processor]\n");
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        auto b = makeSine (fs, 1000, -20.0, (int) fs * 12);
        run (p, b, 512);
        NEAR (p.meters.integrated.load(), -20.0, 0.15, "LUFS-I of -20 dBFS stereo sine");
        NEAR (p.meters.momentary.load(), -20.0, 0.15, "LUFS-M");
        NEAR (p.meters.tpMax.load(), -20.0, 0.2, "true peak");
        NEAR (p.meters.peak.load(), -20.0, 0.1, "sample peak");
        NEAR (p.meters.rms.load(), -23.0, 0.15, "RMS (-20 dBFS sine = -23 dBFS RMS)");
        NEAR (rmsDb (b, 48000), -23.01, 0.05, "flat EQ leaves the signal unchanged");
        CHECK (p.meters.clipCount.load() == 0, "no clips");
        p.meters.resetRequest.store (true);
        auto z = makeSine (fs, 1000, -30.0, 4096); run (p, z, 512);
        CHECK (p.meters.integrated.load() < -100.0f, "reset clears integrated loudness");
    }
    {   std::printf ("[EQ band, bypass, block sizes]\n");
        for (int block : { 1, 7, 64, 513, 4096 })
        {
            EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
            p.setParam ("b1_freq", 1000.f); p.setParam ("b1_gain", 6.f); p.setParam ("b1_q", 1.f); p.setParam ("b1_en", 1.f);
            auto b = makeSine (fs, 1000, -20.0, (int) fs * 3);
            if (block == 1) b.setSize (2, (int) fs, true);
            run (p, b, block);
            NEAR (rmsDb (b, b.getNumSamples() / 2), -23.01 + 6.0, 0.15, "band +6 dB at 1 kHz (varied block size)");
        }
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        p.setParam ("b1_gain", 9.f); p.setParam ("b1_en", 1.f); p.setParam ("bypass", 1.f);
        auto orig = makeSine (fs, 440, -12.0, (int) fs * 2); auto b = orig;
        run (p, b, 256);
        double maxDiff = 0; for (int i = 24000; i < b.getNumSamples(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (b.getSample (0, i) - orig.getSample (0, i)));
        CHECK (maxDiff == 0.0, "bypass is bit-exact after the crossfade");
    }
    {   std::printf ("[stability]\n");
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        p.applyPreset (1);
        juce::AudioBuffer<float> b (2, 4096); b.clear();
        for (int i = 0; i < 4096; ++i) { b.setSample (0, i, 0.3f); b.setSample (1, i, -0.3f); }
        b.setSample (0, 10, std::numeric_limits<float>::quiet_NaN()); b.setSample (1, 20, std::numeric_limits<float>::infinity());
        run (p, b, 512);
        bool fin = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 4096; ++i) fin = fin && std::isfinite (b.getSample (ch, i));
        CHECK (fin, "NaN/Inf input -> finite output");
        CHECK (std::isfinite (p.meters.integrated.load()) && std::isfinite (p.meters.tpMax.load()), "meters finite");
        for (double badFs : { 0.0, -1.0, 1e12 }) { EQMasterProAudioProcessor q; q.prepareToPlay (badFs, 0); auto s = makeSine (44100, 1000, -20, 2048); run (q, s, 256); CHECK (std::isfinite (s.getSample (0, 100)), "invalid sample rate survives"); }
    }
    {   std::printf ("[presets]\n");
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        const int count = (int) eqm::factoryPresets().size();
        CHECK (count == 19, "19 presets");
        for (int i = 0; i < count; ++i)
        {
            p.applyPreset (i);
            int enabled = 0; for (int k = 0; k < eqm::kNumBands; ++k) enabled += p.readBandParams (k).enabled ? 1 : 0;
            CHECK (enabled == (int) eqm::factoryPresets()[(size_t) i].bands.size(), eqm::factoryPresets()[(size_t) i].name);
            auto b = makeSine (fs, 1000, -18.0, 24000); run (p, b, 512);
            bool fin = true; for (int j = 0; j < b.getNumSamples(); ++j) fin = fin && std::isfinite (b.getSample (0, j)) && std::abs (b.getSample (0, j)) < 4.0f;
            CHECK (fin, "preset output finite and bounded");
        }
    }
    {   std::printf ("[state + A/B]\n");
        EQMasterProAudioProcessor a; a.prepareToPlay (fs, 512);
        a.setParam ("b3_gain", -7.5f); a.setParam ("b3_en", 1.f); a.setParam ("out_gain", 2.f);
        a.switchAB();
        CHECK (a.getABSlot() == 1, "switched to B");
        a.setParam ("b3_gain", 5.f);
        a.switchAB();
        NEAR (a.getRaw ("b3_gain"), -7.5, 0.01, "A restored after switching back");
        a.switchAB();
        NEAR (a.getRaw ("b3_gain"), 5.0, 0.01, "B kept its own value");
        juce::MemoryBlock mb; a.getStateInformation (mb);
        EQMasterProAudioProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize());
        NEAR (c.getRaw ("b3_gain"), 5.0, 0.01, "state round trip");
        NEAR (c.getRaw ("out_gain"), 2.0, 0.01, "state round trip (global)");
        CHECK (c.getABSlot() == 1, "A/B slot restored");
        c.switchAB();
        NEAR (c.getRaw ("b3_gain"), -7.5, 0.01, "other A/B slot restored from saved state");
        c.setStateInformation ("garbage", 7);
        CHECK (true, "garbage state does not crash");
    }
    {   std::printf ("[auto gain never clips]\n");
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        p.setParam ("b1_freq", 1000.f); p.setParam ("b1_gain", -8.f); p.setParam ("b1_q", 0.3f); p.setParam ("b1_en", 1.f);
        p.setParam ("autogain", 1.f);
        auto b = makeSine (fs, 3000, -0.5, (int) fs * 4);
        run (p, b, 512);
        float mx = 0; for (int i = 0; i < b.getNumSamples(); ++i) mx = std::max (mx, std::abs (b.getSample (0, i)));
        CHECK (mx <= 1.0f, "auto gain does not push the signal over 0 dBFS");
        auto q = makeSine (fs, 1000, -30.0, (int) fs * 4); run (p, q, 512);
        CHECK (rmsDb (q, (int) fs * 3) > -33.0 - 8.0, "auto gain is active on quiet signals");
        p.setParam ("autogain", 0.f);
    }
    {   std::printf ("[clip counter]\n");
        EQMasterProAudioProcessor p; p.prepareToPlay (fs, 512);
        p.setParam ("out_gain", 12.f);
        auto b = makeSine (fs, 100, -3.0, (int) fs); run (p, b, 512);
        CHECK (p.meters.clipCount.load() > 50, "clipping detected and counted");
    }
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
