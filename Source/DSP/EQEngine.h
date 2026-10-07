#pragma once
#include "Biquad.h"
#include <array>

namespace eqm
{
constexpr int kNumBands = 24;

enum class ChannelMode : int { Stereo = 0, Mid, Side, Left, Right, Count };

struct BandParams
{
    bool enabled = false;
    FilterType type = FilterType::Bell;
    ChannelMode mode = ChannelMode::Stereo;
    double freq = 1000.0, gain = 0.0, q = 1.0;
    // Dynamic EQ
    bool dynEnabled = false;
    double thresholdDb = -30.0, attackMs = 10.0, releaseMs = 120.0, rangeDb = 6.0, ratio = 2.0;
};

// Static (non-dynamic) magnitude response of one band, in dB.
inline double bandMagnitudeDb (const BandParams& p, double fs, double f)
{
    const auto st = makeStages (p.type, fs, p.freq, p.gain, p.q);
    double db = magDb (st.s1, fs, f);
    if (p.type == FilterType::Tilt) db += magDb (st.s2, fs, f);
    return db;
}

class EQBand
{
public:
    void prepare (double sampleRate)
    {
        fs = sanitize (sampleRate, 8000.0, 768000.0, 44100.0);
        resetAll();
        running = false; wasEnabled = false;
    }

    void setParams (const BandParams& p) { target = p; }

    void process (double* l, double* r, int n)
    {
        const bool en = target.enabled;
        if (en)
        {
            if (! wasEnabled || target.type != held.type || target.mode != held.mode)
            {
                resetAll();
                logF = std::log (sanitize (target.freq, 5.0, fs * 0.4999, 1000.0));
                qS = sanitize (target.q, 0.05, 50.0, 1.0);
                gS = typeUsesGain (target.type) ? 0.0 : target.gain; // gain bands fade in
                running = true;
            }
            held = target;
        }
        else if (running)
        {
            // Fade gain-type bands out, switch other types off at once
            if (! (typeUsesGain (held.type) && std::abs (gS) > 0.01)) { running = false; wasEnabled = false; return; }
            held.dynEnabled = false;
        }
        else { wasEnabled = false; return; }
        wasEnabled = en;

        const double tgtGain = en ? held.gain : 0.0;
        const bool dynOn = held.dynEnabled;
        const double aA = std::exp (-1.0 / (fs * sanitize (held.attackMs, 0.05, 1000.0, 10.0) * 0.001));
        const double aR = std::exp (-1.0 / (fs * sanitize (held.releaseMs, 1.0, 10000.0, 100.0) * 0.001));
        const double thr = sanitize (held.thresholdDb, -120.0, 6.0, -30.0);
        const double ratio = sanitize (held.ratio, 1.0, 100.0, 2.0);
        const double range = sanitize (held.rangeDb, -48.0, 48.0, 0.0);
        const double fTarget = std::log (sanitize (held.freq, 5.0, fs * 0.4999, 1000.0));
        const double qTarget = sanitize (held.q, 0.05, 50.0, 1.0);

        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int c = std::min (kChunk, n - pos);

            logF += (fTarget - logF) * kSmooth;
            qS   += (qTarget - qS) * kSmooth;
            gS   += (tgtGain + (dynOn ? dynOffsetDb : 0.0) - gS) * kSmooth;

            const double fHz = std::exp (logF);
            const auto st = makeStages (held.type, fs, fHz, gS, qS);
            for (int ch = 0; ch < 2; ++ch) { f[ch][0].c = st.s1; f[ch][1].c = st.s2; }
            if (dynOn) det.c = makeCoeffs (FilterType::BandPass, fs, fHz, 0.0, std::max (qS, 0.5));

            double* pl = l + pos; double* pr = r + pos;
            for (int i = 0; i < c; ++i)
            {
                double L = pl[i], R = pr[i], d = 0;
                switch (held.mode)
                {
                    case ChannelMode::Left:  d = L; L = run (0, L); break;
                    case ChannelMode::Right: d = R; R = run (1, R); break;
                    case ChannelMode::Mid:
                    {
                        const double m = 0.5 * (L + R), s = 0.5 * (L - R);
                        d = m; const double m2 = run (0, m); L = m2 + s; R = m2 - s; break;
                    }
                    case ChannelMode::Side:
                    {
                        const double m = 0.5 * (L + R), s = 0.5 * (L - R);
                        d = s; const double s2 = run (1, s); L = m + s2; R = m - s2; break;
                    }
                    case ChannelMode::Stereo:
                    default: d = 0.5 * (L + R); L = run (0, L); R = run (1, R); break;
                }
                if (dynOn)
                {
                    const double a = std::abs (det.process (d));
                    env = a > env ? aA * env + (1 - aA) * a : aR * env + (1 - aR) * a;
                }
                pl[i] = L; pr[i] = R;
            }

            if (dynOn)
            {
                const double lvl = 20.0 * std::log10 (env + 1e-12);
                const double over = lvl - thr;
                const double red = over > 0 ? over * (1.0 - 1.0 / ratio) : 0.0;
                dynOffsetDb = range >= 0 ? -std::min (range, red) : std::min (-range, red);
            }
            else dynOffsetDb = 0.0;
        }

        if (! (std::isfinite (l[n - 1]) && std::isfinite (r[n - 1])))
        {
            resetAll();
            for (int i = 0; i < n; ++i) { l[i] = 0; r[i] = 0; }
        }
    }

private:
    static constexpr int kChunk = 32;
    static constexpr double kSmooth = 0.25;

    inline double run (int ch, double x) noexcept { return f[ch][1].process (f[ch][0].process (x)); }

    void resetAll()
    {
        for (auto& ch : f) for (auto& s : ch) { s.reset(); s.c = Coeffs {}; }
        det.reset(); env = 0; dynOffsetDb = 0;
    }

    double fs = 44100.0;
    BandParams target, held;
    Biquad f[2][2];
    Biquad det;
    double logF = 0, qS = 1, gS = 0, env = 0, dynOffsetDb = 0;
    bool running = false, wasEnabled = false;
};

class EQEngine
{
public:
    static constexpr int kGridN = 64;

    void prepare (double sampleRate)
    {
        fs = sanitize (sampleRate, 8000.0, 768000.0, 44100.0);
        for (auto& b : bands) b.prepare (fs);
    }
    void reset() { prepare (fs); }

    void setBand (int i, const BandParams& p)
    {
        if (i < 0 || i >= kNumBands) return;
        bands[(size_t) i].setParams (p);
        auto& o = params[(size_t) i];
        if (o.enabled != p.enabled || o.type != p.type || o.mode != p.mode || o.freq != p.freq || o.gain != p.gain || o.q != p.q)
            gainDirty = true;
        o = p;
    }

    // l/r are processed in place. Non-finite input is replaced by silence.
    void process (double* l, double* r, int n)
    {
        if (n <= 0) return;
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (l[i])) l[i] = 0;
            if (! std::isfinite (r[i])) r[i] = 0;
        }
        for (auto& b : bands) b.process (l, r, n);
    }

    // Level change a pink-noise signal would see through the gain-type bands (dB). Used by Auto Gain.
    double pinkNoiseGainDb()
    {
        if (! gainDirty) return cachedGainDb;
        double acc = 0; int cnt = 0;
        for (int g = 0; g < kGridN; ++g)
        {
            const double f = 20.0 * std::pow (1000.0, g / double (kGridN - 1));
            double db = 0;
            for (const auto& p : params)
            {
                if (! p.enabled || ! typeUsesGain (p.type)) continue;
                const double w = p.mode == ChannelMode::Stereo ? 1.0 : 0.5;
                db += w * bandMagnitudeDb (p, fs, f);
            }
            acc += std::pow (10.0, db / 10.0); ++cnt;
        }
        cachedGainDb = 10.0 * std::log10 (std::max (acc / cnt, 1e-12));
        gainDirty = false;
        return cachedGainDb;
    }

    double sampleRate() const { return fs; }

private:
    double fs = 44100.0;
    std::array<EQBand, kNumBands> bands;
    std::array<BandParams, kNumBands> params;
    bool gainDirty = true;
    double cachedGainDb = 0.0;
};
} // namespace eqm
