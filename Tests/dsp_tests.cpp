// Self-contained DSP tests (no JUCE, no test framework). Exit code != 0 on any failure.
#include "../Source/DSP/Biquad.h"
#include "../Source/DSP/EQEngine.h"
#include "../Source/DSP/Loudness.h"
#include "../Source/DSP/SpectrumTools.h"
#include "../Source/DSP/Presets.h"
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace eqm;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } } while (0)
#define CHECK_NEAR(a, b, tol, msg) do { ++checks; const double _a = (a), _b = (b); \
    if (! (std::abs (_a - _b) <= (tol))) { ++failures; std::printf ("  FAIL: %s  got %.4f expected %.4f +/- %.4f [%s:%d]\n", msg, _a, _b, (double) (tol), __FILE__, __LINE__); } } while (0)

static void section (const char* n) { std::printf ("[%s]\n", n); }

// ----- helpers --------------------------------------------------------------------------------------------
static std::vector<float> sine (double fs, double f, double ampDb, double seconds, double phase = 0.0)
{
    std::vector<float> v ((size_t) (fs * seconds));
    const double a = std::pow (10.0, ampDb / 20.0);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (a * std::sin (2 * kPi * f * (double) i / fs + phase));
    return v;
}

static double rmsDb (const std::vector<double>& v, size_t from)
{
    double s = 0; for (size_t i = from; i < v.size(); ++i) s += v[i] * v[i];
    return 10.0 * std::log10 (s / (double) (v.size() - from) + 1e-30);
}

// ----- tests ----------------------------------------------------------------------------------------------
static void testFilters()
{
    section ("Filters (9 types)");
    const double fs = 48000;
    CHECK_NEAR (magDb (makeCoeffs (FilterType::Bell, fs, 1000, 6, 1), fs, 1000), 6.0, 0.01, "bell +6 dB at centre");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::Bell, fs, 1000, -9, 2), fs, 1000), -9.0, 0.01, "bell -9 dB at centre");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::Bell, fs, 1000, 6, 1), fs, 30), 0.0, 0.1, "bell is flat far away");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::LowShelf, fs, 1000, 6, 0.707), fs, 20), 6.0, 0.1, "low shelf gain at LF");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::LowShelf, fs, 1000, 6, 0.707), fs, 20000), 0.0, 0.1, "low shelf flat at HF");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::HighShelf, fs, 1000, -6, 0.707), fs, 20000), -6.0, 0.3, "high shelf gain at HF");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::HighShelf, fs, 1000, -6, 0.707), fs, 20), 0.0, 0.1, "high shelf flat at LF");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::LowPass, fs, 1000, 0, 0.7071), fs, 1000), -3.01, 0.05, "LP -3 dB at fc");
    CHECK (magDb (makeCoeffs (FilterType::LowPass, fs, 1000, 0, 0.7071), fs, 10000) < -35, "LP attenuates 10x above fc");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::HighPass, fs, 1000, 0, 0.7071), fs, 1000), -3.01, 0.05, "HP -3 dB at fc");
    CHECK (magDb (makeCoeffs (FilterType::HighPass, fs, 1000, 0, 0.7071), fs, 100) < -35, "HP attenuates 10x below fc");
    CHECK (magDb (makeCoeffs (FilterType::Notch, fs, 1000, 0, 4), fs, 1000) < -80, "notch is deep at fc");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::Notch, fs, 1000, 0, 4), fs, 100), 0.0, 0.1, "notch flat far away");
    CHECK_NEAR (magDb (makeCoeffs (FilterType::BandPass, fs, 1000, 0, 2), fs, 1000), 0.0, 0.01, "band pass 0 dB peak");
    CHECK (magDb (makeCoeffs (FilterType::BandPass, fs, 1000, 0, 2), fs, 100) < -15, "band pass rejects LF");
    for (double f : { 50.0, 500.0, 5000.0, 15000.0 })
        CHECK_NEAR (magDb (makeCoeffs (FilterType::AllPass, fs, 2000, 0, 1), fs, f), 0.0, 1e-6, "all pass is flat");
    // Tilt: lows down, highs up, pivot around fc
    BandParams t; t.enabled = true; t.type = FilterType::Tilt; t.freq = 1000; t.gain = 6;
    CHECK (bandMagnitudeDb (t, fs, 30) < -2.5 && bandMagnitudeDb (t, fs, 18000) > 2.5, "tilt slopes upward");
    CHECK_NEAR (bandMagnitudeDb (t, fs, 1000), 0.0, 0.2, "tilt pivots at fc");

    // Invalid parameters never produce NaN/Inf/unstable filters
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    bool allFinite = true;
    for (int t2 = 0; t2 < (int) FilterType::Count; ++t2)
        for (double fsv : { nan, 0.0, -5.0, 44100.0, 192000.0 })
            for (double f : { nan, 0.0, -1.0, inf, 1e9, 1000.0 })
                for (double q : { nan, 0.0, -3.0, inf, 1e6 })
                {
                    const auto s = makeStages ((FilterType) t2, fsv, f, nan, q);
                    for (const Coeffs* c : { &s.s1, &s.s2 })
                        allFinite = allFinite && std::isfinite (c->b0) && std::isfinite (c->b1) && std::isfinite (c->b2)
                                    && std::isfinite (c->a1) && std::isfinite (c->a2)
                                    && std::abs (c->a2) < 1.0 + 1e-9;  // pole inside unit circle (necessary condition)
                }
    CHECK (allFinite, "coefficients stay finite and stable for invalid parameters");
}

static void testEngineEQ()
{
    section ("EQ engine (static)");
    const double fs = 48000;
    EQEngine eq; eq.prepare (fs);
    BandParams p; p.enabled = true; p.type = FilterType::Bell; p.freq = 1000; p.gain = 6; p.q = 1;
    eq.setBand (0, p);
    auto x = sine (fs, 1000, -20, 2.0);
    std::vector<double> l (x.begin(), x.end()), r (x.begin(), x.end());
    for (size_t pos = 0; pos < l.size(); pos += 500) { const int n = (int) std::min<size_t> (500, l.size() - pos); eq.process (l.data() + pos, r.data() + pos, n); }
    CHECK_NEAR (rmsDb (l, 48000) - (-20.0 - 3.0103), 6.0, 0.1, "1 kHz sine gets +6 dB through the engine");
    CHECK_NEAR (rmsDb (r, 48000) - (-20.0 - 3.0103), 6.0, 0.1, "right channel too");

    // 24 bands at once, white noise, finite & not exploding
    EQEngine big; big.prepare (fs);
    for (int i = 0; i < kNumBands; ++i)
    {
        BandParams b; b.enabled = true; b.type = (FilterType) (i % (int) FilterType::Count);
        b.freq = 40.0 * std::pow (400.0, i / 23.0); b.gain = (i % 2 ? 4 : -4); b.q = 1.0 + (i % 5);
        b.mode = (ChannelMode) (i % (int) ChannelMode::Count);
        big.setBand (i, b);
    }
    std::mt19937 rng (1); std::uniform_real_distribution<double> d (-0.5, 0.5);
    std::vector<double> nl (48000), nr (48000);
    for (auto& v : nl) v = d (rng); for (auto& v : nr) v = d (rng);
    big.process (nl.data(), nr.data(), (int) nl.size());
    bool fin = true; double mx = 0; for (size_t i = 0; i < nl.size(); ++i) { fin = fin && std::isfinite (nl[i]) && std::isfinite (nr[i]); mx = std::max (mx, std::abs (nl[i])); }
    CHECK (fin, "24 bands: output finite");
    CHECK (mx < 20.0, "24 bands: output bounded");

    // Mid/Side: a band in Side mode must not change a mono (mid-only) signal; a Mid band must not change a pure side signal
    EQEngine ms; ms.prepare (fs);
    BandParams sb; sb.enabled = true; sb.mode = ChannelMode::Side; sb.gain = 12; sb.freq = 1000;
    ms.setBand (0, sb);
    std::vector<double> ml (x.begin(), x.end()), mr (x.begin(), x.end());
    ms.process (ml.data(), mr.data(), (int) ml.size());
    CHECK_NEAR (rmsDb (ml, 48000) - rmsDb (std::vector<double> (x.begin(), x.end()), 48000), 0.0, 0.01, "Side band leaves mono signal untouched");
    EQEngine ms2; ms2.prepare (fs);
    BandParams mb = sb; mb.mode = ChannelMode::Mid; ms2.setBand (0, mb);
    std::vector<double> sl (x.begin(), x.end()), sr (x.size());
    for (size_t i = 0; i < x.size(); ++i) sr[i] = -sl[i];
    ms2.process (sl.data(), sr.data(), (int) sl.size());
    CHECK_NEAR (rmsDb (sl, 48000) - rmsDb (std::vector<double> (x.begin(), x.end()), 48000), 0.0, 0.01, "Mid band leaves pure-side signal untouched");

    // Left / Right modes
    EQEngine lr; lr.prepare (fs);
    BandParams lb; lb.enabled = true; lb.mode = ChannelMode::Left; lb.gain = 6; lb.freq = 1000; lr.setBand (0, lb);
    std::vector<double> ll (x.begin(), x.end()), rr (x.begin(), x.end());
    lr.process (ll.data(), rr.data(), (int) ll.size());
    CHECK_NEAR (rmsDb (ll, 48000) - rmsDb (rr, 48000), 6.0, 0.15, "Left-only band boosts only the left channel");
}

static void testDynamicEQ()
{
    section ("Dynamic EQ");
    const double fs = 48000;
    auto run = [&] (double levelDb)
    {
        EQEngine eq; eq.prepare (fs);
        BandParams p; p.enabled = true; p.type = FilterType::Bell; p.freq = 1000; p.gain = 6; p.q = 1;
        p.dynEnabled = true; p.thresholdDb = -30; p.ratio = 4; p.rangeDb = 12; p.attackMs = 5; p.releaseMs = 100;
        eq.setBand (0, p);
        auto x = sine (fs, 1000, levelDb, 3.0);
        std::vector<double> l (x.begin(), x.end()), r (x.begin(), x.end());
        for (size_t pos = 0; pos < l.size(); pos += 512) { const int n = (int) std::min<size_t> (512, l.size() - pos); eq.process (l.data() + pos, r.data() + pos, n); }
        return rmsDb (l, 96000) - rmsDb (std::vector<double> (x.begin(), x.end()), 96000);
    };
    const double quiet = run (-60.0), loud = run (-6.0);
    CHECK_NEAR (quiet, 6.0, 0.3, "below threshold: full static boost");
    CHECK (loud < quiet - 8.0, "above threshold: gain is reduced by the dynamics");
    CHECK (loud > -7.5, "reduction limited by Range (6 dB boost - 12 dB range = -6 dB)");

    // Upward (negative range) expansion boosts when loud
    EQEngine eq; eq.prepare (fs);
    BandParams p; p.enabled = true; p.freq = 1000; p.gain = 0; p.q = 1; p.dynEnabled = true; p.thresholdDb = -40; p.ratio = 4; p.rangeDb = -6;
    eq.setBand (0, p);
    auto x = sine (fs, 1000, -10, 3.0);
    std::vector<double> l (x.begin(), x.end()), r (x.begin(), x.end());
    eq.process (l.data(), r.data(), (int) l.size());
    CHECK (rmsDb (l, 96000) - rmsDb (std::vector<double> (x.begin(), x.end()), 96000) > 3.0, "negative range expands (boosts)");
}

static void testStability()
{
    section ("Stability");
    const double fs = 48000;
    EQEngine eq; eq.prepare (fs);
    for (int i = 0; i < kNumBands; ++i)
    {
        BandParams b; b.enabled = true; b.type = (FilterType) (i % 9); b.freq = 30.0 * (i + 1); b.gain = 20; b.q = 18; b.dynEnabled = (i % 2 == 0);
        eq.setBand (i, b);
    }
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    std::vector<double> l (4096, 0.3), r (4096, -0.3);
    l[10] = nan; r[20] = inf; l[30] = -inf; r[500] = nan;
    eq.process (l.data(), r.data(), 4096);
    bool fin = true; for (size_t i = 0; i < l.size(); ++i) fin = fin && std::isfinite (l[i]) && std::isfinite (r[i]);
    CHECK (fin, "NaN/Inf input never leaks to the output");

    // rapid parameter changes every 16 samples
    EQEngine fast; fast.prepare (fs);
    std::mt19937 rng (7); std::uniform_real_distribution<double> d (-1, 1);
    bool ok = true;
    for (int blk = 0; blk < 3000; ++blk)
    {
        for (int i = 0; i < 8; ++i)
        {
            BandParams b; b.enabled = (rng() % 4) != 0; b.type = (FilterType) (rng() % 9);
            b.freq = 20.0 * std::pow (1000.0, (d (rng) + 1) / 2); b.gain = d (rng) * 24; b.q = 0.1 + 17.9 * (d (rng) + 1) / 2;
            b.mode = (ChannelMode) (rng() % 5); b.dynEnabled = (rng() % 2); fast.setBand (i, b);
        }
        double a[16], c[16]; for (int i = 0; i < 16; ++i) { a[i] = 0.5 * d (rng); c[i] = 0.5 * d (rng); }
        fast.process (a, c, 16);
        for (int i = 0; i < 16; ++i) ok = ok && std::isfinite (a[i]) && std::isfinite (c[i]) && std::abs (a[i]) < 1e3;
    }
    CHECK (ok, "rapid random parameter changes stay finite and bounded");

    // invalid sample rates
    for (double bad : { 0.0, -1.0, 1e12, std::numeric_limits<double>::quiet_NaN() })
    {
        EQEngine e; e.prepare (bad);
        BandParams b; b.enabled = true; b.gain = 6; e.setBand (0, b);
        double a[64] = {}, c[64] = {}; a[3] = 1; c[3] = 1;
        e.process (a, c, 64);
        bool f2 = true; for (int i = 0; i < 64; ++i) f2 = f2 && std::isfinite (a[i]);
        CHECK (f2, "invalid sample rate handled");
    }
    EQEngine e0; e0.prepare (48000); double z[1] = { 0 }; e0.process (z, z, 0); e0.process (z, z, -5);
    CHECK (true, "empty / negative buffer sizes are ignored");
}

static void testLoudness()
{
    section ("Loudness (BS.1770 / R128)");
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m; m.prepare (fs);
        auto x = sine (fs, 1000, -20.0, 30.0);
        for (size_t pos = 0; pos < x.size(); pos += 480)
        { const int n = (int) std::min<size_t> (480, x.size() - pos); m.process (x.data() + pos, x.data() + pos, n); }
        char msg[96];
        std::snprintf (msg, sizeof msg, "%.0f Hz: stereo 1 kHz -20 dBFS => I = -20 LUFS", fs);
        CHECK_NEAR (m.integrated(), -20.0, 0.1, msg);
        CHECK_NEAR (m.momentary(), -20.0, 0.1, "momentary");
        CHECK_NEAR (m.shortTerm(), -20.0, 0.1, "short-term");
        CHECK_NEAR (m.lra(), 0.0, 0.2, "LRA of a constant tone is ~0");
    }
    {   // EBU Tech 3341 style: -23 dBFS stereo 1 kHz => -23.0 LUFS
        LoudnessMeter m; m.prepare (48000);
        auto x = sine (48000, 1000, -23.0, 20.0);
        m.process (x.data(), x.data(), (int) x.size());
        CHECK_NEAR (m.integrated(), -23.0, 0.1, "-23 dBFS stereo sine = -23.0 LUFS");
    }
    {   // single channel: 3 dB lower
        LoudnessMeter m; m.prepare (48000);
        auto x = sine (48000, 1000, -20.0, 20.0); std::vector<float> z (x.size(), 0.0f);
        m.process (x.data(), z.data(), (int) x.size());
        CHECK_NEAR (m.integrated(), -23.01, 0.1, "one channel only = -3 LU");
    }
    {   // K-weighting: 100 Hz is ~ -0.5 dB lower than 1 kHz; 10 kHz ~ +3 dB higher? (head shelf)
        auto meas = [] (double f) { LoudnessMeter m; m.prepare (48000); auto x = sine (48000, f, -20.0, 10.0); m.process (x.data(), x.data(), (int) x.size()); return m.integrated(); };
        const double ref = meas (1000);
        CHECK (meas (50) < ref - 1.0, "RLB high-pass attenuates 50 Hz");
        CHECK (meas (10000) > ref + 2.0, "head-shelf boosts 10 kHz");
    }
    {   // absolute gate: 20 s tone then 20 s silence => silence must not drag I down
        LoudnessMeter m; m.prepare (48000);
        auto x = sine (48000, 1000, -20.0, 20.0); std::vector<float> z (48000 * 20, 0.0f);
        m.process (x.data(), x.data(), (int) x.size()); m.process (z.data(), z.data(), (int) z.size());
        CHECK_NEAR (m.integrated(), -20.0, 0.1, "absolute gate ignores silence");
    }
    {   // relative gate: 20 s at -20, 20 s at -45 => quiet part is gated out (-10 LU below the mean)
        LoudnessMeter m; m.prepare (48000);
        auto a = sine (48000, 1000, -20.0, 20.0), b = sine (48000, 1000, -45.0, 20.0);
        m.process (a.data(), a.data(), (int) a.size()); m.process (b.data(), b.data(), (int) b.size());
        CHECK_NEAR (m.integrated(), -20.0, 0.1, "relative gate removes the quiet section");
    }
    {   // LRA: 20 s at -20 LUFS then 20 s at -30 LUFS => ~10 LU
        LoudnessMeter m; m.prepare (48000);
        auto a = sine (48000, 1000, -20.0, 30.0), b = sine (48000, 1000, -30.0, 30.0);
        m.process (a.data(), a.data(), (int) a.size()); m.process (b.data(), b.data(), (int) b.size());
        CHECK_NEAR (m.lra(), 10.0, 0.6, "LRA of a 10 LU step");
    }
    {   // reset
        LoudnessMeter m; m.prepare (48000);
        auto a = sine (48000, 1000, -20.0, 5.0); m.process (a.data(), a.data(), (int) a.size());
        m.reset(); CHECK (m.integrated() < -100 && m.momentary() < -100, "reset clears the measurement");
    }
    {   // garbage input
        LoudnessMeter m; m.prepare (48000);
        std::vector<float> g (48000, std::numeric_limits<float>::quiet_NaN());
        m.process (g.data(), g.data(), (int) g.size());
        CHECK (std::isfinite (m.momentary()) && std::isfinite (m.integrated()) && std::isfinite (m.truePeakDb (0)), "NaN input does not poison the meter");
    }
}

static void testTruePeak()
{
    section ("True Peak");
    {   // fs/4 sine at 45 degrees: all samples = +-0.7071, true peak = 1.0
        const double fs = 48000;
        auto x = sine (fs, fs / 4, 0.0, 1.0, kPi / 4);
        float sp = 0; for (float v : x) sp = std::max (sp, std::abs (v));
        TruePeakMeter tp; for (float v : x) tp.process (v);
        CHECK_NEAR (20 * std::log10 (sp), -3.01, 0.02, "sample peak is -3.01 dBFS");
        CHECK_NEAR (20 * std::log10 (tp.getPeakLinear()), 0.0, 0.15, "true peak is ~0 dBTP");
    }
    {   // 1 kHz -6 dBFS: true peak == sample peak ~ -6 dB
        auto x = sine (48000, 1000, -6.0, 1.0);
        TruePeakMeter tp; for (float v : x) tp.process (v);
        CHECK_NEAR (20 * std::log10 (tp.getPeakLinear()), -6.0, 0.1, "true peak of a low-frequency tone");
    }
    {   // meter object: L/R separate
        LoudnessMeter m; m.prepare (48000);
        auto a = sine (48000, 1000, -6.0, 2.0), b = sine (48000, 1000, -12.0, 2.0);
        m.process (a.data(), b.data(), (int) a.size());
        CHECK_NEAR (m.truePeakDb (0), -6.0, 0.15, "L true peak");
        CHECK_NEAR (m.truePeakDb (1), -12.0, 0.15, "R true peak");
    }
}

static Spectrum slope (double dbPerOct, double lvl = -40)
{
    Spectrum s; for (int i = 0; i < kGridPts; ++i) s[(size_t) i] = (float) (lvl + dbPerOct * std::log2 (gridFreq (i) / 1000.0));
    return s;
}

static void testAutoEQ()
{
    section ("Auto EQ");
    auto flat = suggestAutoEQ (slope (-4.5));
    CHECK (flat.empty(), "a spectrum on the reference slope needs no correction");

    auto s = slope (-4.5);
    for (int i = 0; i < kGridPts; ++i) s[(size_t) i] += (float) (8.0 * std::exp (-0.5 * std::pow (std::log2 (gridFreq (i) / 3000.0) / 0.07, 2)));
    auto sug = suggestAutoEQ (s);
    bool found = false; for (const auto& b : sug) if (std::abs (std::log2 (b.freq / 3000.0)) < 0.2 && b.gain < -1.5 && b.q >= 3) found = true;
    CHECK (found, "narrow 8 dB resonance at 3 kHz is detected and cut");
    CHECK (sug.size() <= 8, "never more than 8 bands");
    for (const auto& b : sug) CHECK (b.gain >= -6.01 && b.gain <= 3.01, "moderate gains only");

    auto dull = slope (-9.0); // far too dark: expect boosts in the highs
    auto sd = suggestAutoEQ (dull); bool boostHF = false; for (const auto& b : sd) if (b.freq > 4000 && b.gain > 0) boostHF = true;
    CHECK (boostHF, "deficient high end gets a boost");
    auto bright = slope (-1.0);
    auto sb = suggestAutoEQ (bright); bool cutHF = false; for (const auto& b : sb) if (b.freq > 4000 && b.gain < 0) cutHF = true;
    CHECK (cutHF, "excess high end gets a cut");
}

static void testEQMatch()
{
    section ("EQ Match");
    Spectrum cur = slope (-4.5), ref = slope (-4.5);
    for (int i = 0; i < kGridPts; ++i) ref[(size_t) i] += (float) (3.0 * std::log2 (gridFreq (i) / 1000.0) / 2.0 + 6.0); // brighter + louder
    auto sug = suggestMatchEQ (ref, cur);
    CHECK (! sug.empty(), "match produces a suggestion");
    double e0 = 0, e1 = 0; int n = 0;
    double off = 0; { Spectrum d; for (int i = 0; i < kGridPts; ++i) d[(size_t) i] = ref[(size_t) i] - cur[(size_t) i]; off = averageRange (d, 100, 10000); }
    for (int i = 0; i < kGridPts; ++i)
    {
        const double f = gridFreq (i); if (f < 40 || f > 16000) continue;
        const double target = ref[(size_t) i] - cur[(size_t) i] - off;
        e0 += target * target; const double e = target - suggestionResponseDb (sug, f); e1 += e * e; ++n;
    }
    std::printf ("  match error: RMS %.2f dB -> %.2f dB\n", std::sqrt (e0 / n), std::sqrt (e1 / n));
    CHECK (std::sqrt (e1 / n) < 0.5 * std::sqrt (e0 / n), "match at least halves the spectral error");
    for (const auto& b : sug) CHECK (std::abs (b.gain) <= 12.01, "gains within +-12 dB");
    CHECK (suggestMatchEQ (cur, cur).empty(), "identical spectra need no EQ");

    SpectrumAccumulator acc; acc.add (cur); acc.add (cur);
    auto avg = acc.average(); CHECK_NEAR (avg[100], cur[100], 0.01, "accumulator averages identical frames");
    Spectrum silent; silent.fill (-120); SpectrumAccumulator a2; a2.add (silent); CHECK (a2.frames() == 0, "accumulator ignores silence");
}

static void testPresets()
{
    section ("Presets");
    const auto& ps = factoryPresets();
    CHECK (ps.size() == 19, "19 factory presets");
    const char* expected[] = { "Default", "Vocal", "Voice", "Podcast", "Bass", "Kick", "Snare", "Drums", "Guitar", "Acoustic Guitar",
                               "Electric Guitar", "Piano", "Keys", "Master", "Streaming", "YouTube", "Radio", "Broadcast", "Live" };
    for (size_t i = 0; i < ps.size() && i < 19; ++i) CHECK (std::string (ps[i].name) == expected[i], expected[i]);
    for (const auto& p : ps)
    {
        CHECK ((int) p.bands.size() <= kNumBands, "band count fits");
        EQEngine eq; eq.prepare (48000); int i = 0;
        for (const auto& b : p.bands)
        {
            CHECK (b.freq >= 20 && b.freq <= 20000 && std::abs (b.gain) <= 24 && b.q >= 0.1 && b.q <= 18, "preset values within parameter ranges");
            BandParams bp; bp.enabled = true; bp.type = b.type; bp.freq = b.freq; bp.gain = b.gain; bp.q = b.q; bp.mode = b.mode;
            bp.dynEnabled = b.dyn; bp.thresholdDb = b.thr; bp.ratio = b.ratio; bp.rangeDb = b.range;
            eq.setBand (i++, bp);
        }
        std::mt19937 rng (3); std::uniform_real_distribution<double> d (-0.5, 0.5);
        std::vector<double> l (24000), r (24000); for (auto& v : l) v = d (rng); for (auto& v : r) v = d (rng);
        eq.process (l.data(), r.data(), (int) l.size());
        bool fin = true; double mx = 0; for (size_t k = 0; k < l.size(); ++k) { fin = fin && std::isfinite (l[k]) && std::isfinite (r[k]); mx = std::max (mx, std::abs (l[k])); }
        CHECK (fin && mx < 8.0, p.name);
    }
}

static void testAutoGain()
{
    section ("Auto Gain");
    EQEngine eq; eq.prepare (48000);
    CHECK_NEAR (eq.pinkNoiseGainDb(), 0.0, 1e-6, "flat EQ => 0 dB compensation");
    BandParams p; p.enabled = true; p.freq = 1000; p.gain = 6; p.q = 0.5; eq.setBand (0, p);
    const double g = eq.pinkNoiseGainDb();
    CHECK (g > 1.0 && g < 6.0, "boost raises the pink-noise level by a plausible amount");
    BandParams hp; hp.enabled = true; hp.type = FilterType::HighPass; hp.freq = 200; eq.setBand (1, hp);
    CHECK_NEAR (eq.pinkNoiseGainDb(), g, 1e-9, "filters without gain do not affect auto gain");
}

int main()
{
    testFilters();
    testEngineEQ();
    testDynamicEQ();
    testStability();
    testLoudness();
    testTruePeak();
    testAutoEQ();
    testEQMatch();
    testPresets();
    testAutoGain();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
