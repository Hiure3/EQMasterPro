#pragma once
#include "EQEngine.h"
#include <vector>

// Spectra are kept on a fixed logarithmic grid (20 Hz - 20 kHz, 256 points, values in dB) so analysis is
// independent of the FFT size. Auto EQ and EQ Match are pure functions on that grid.
namespace eqm
{
constexpr int kGridPts = 256;
using Spectrum = std::array<float, kGridPts>;

inline double gridFreq (int i) { return 20.0 * std::pow (1000.0, i / double (kGridPts - 1)); }
inline double gridIndexOf (double f) { return std::log (f / 20.0) / std::log (1000.0) * (kGridPts - 1); }
constexpr double kPtsPerOctave = (kGridPts - 1) / 9.965784284662087; // log2(1000)

struct BandSuggestion { FilterType type = FilterType::Bell; double freq = 1000, gain = 0, q = 1; };

inline void smoothOctaves (const Spectrum& in, Spectrum& out, double widthOct)
{
    const int half = std::max (0, (int) std::lround (widthOct * kPtsPerOctave * 0.5));
    for (int i = 0; i < kGridPts; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (kGridPts - 1, i + half);
        double s = 0; for (int k = a; k <= b; ++k) s += in[(size_t) k];
        out[(size_t) i] = (float) (s / (b - a + 1));
    }
}

inline double averageRange (const Spectrum& s, double f0, double f1)
{
    const int a = std::max (0, (int) std::floor (gridIndexOf (f0)));
    const int b = std::min (kGridPts - 1, (int) std::ceil (gridIndexOf (f1)));
    double acc = 0; int n = 0;
    for (int i = a; i <= b; ++i) { acc += s[(size_t) i]; ++n; }
    return n ? acc / n : 0.0;
}

// Averages many spectra in the power domain (message thread only).
class SpectrumAccumulator
{
public:
    void clear() { sum.fill (0.0); count = 0; }
    int frames() const { return count; }
    void add (const Spectrum& dB)
    {
        double mean = 0; for (float v : dB) mean += v; mean /= kGridPts;
        if (! std::isfinite (mean) || mean < -95.0) return; // ignore silence
        for (int i = 0; i < kGridPts; ++i) sum[(size_t) i] += std::pow (10.0, dB[(size_t) i] / 10.0);
        ++count;
    }
    Spectrum average() const
    {
        Spectrum s; s.fill (-120.0f);
        if (count > 0) for (int i = 0; i < kGridPts; ++i)
            s[(size_t) i] = (float) (10.0 * std::log10 (std::max (sum[(size_t) i] / count, 1e-12)));
        return s;
    }
private:
    std::array<double, kGridPts> sum {};
    int count = 0;
};

// ---------------------------------------------------------------------------------------------------------
// AUTO EQ: finds resonances (narrow peaks above their surroundings) and broad tonal imbalance relative to a
// -4.5 dB/oct reference slope, and proposes at most 8 moderate corrections (cuts up to 6 dB, boosts up to 3 dB).
inline std::vector<BandSuggestion> suggestAutoEQ (const Spectrum& spec)
{
    std::vector<BandSuggestion> out;
    Spectrum s3, narrow, wide;
    smoothOctaves (spec, s3, 1.0 / 3.0);
    smoothOctaves (spec, narrow, 1.0 / 12.0);
    smoothOctaves (spec, wide, 1.0);

    // reference level: mean of s3 in the 60 Hz - 12 kHz range, expressed against a -4.5 dB/oct slope
    double xm = 0, ym = 0; int n = 0;
    for (int i = 0; i < kGridPts; ++i)
    {
        const double f = gridFreq (i);
        if (f < 60 || f > 12000) continue;
        xm += std::log2 (f); ym += s3[(size_t) i]; ++n;
    }
    if (n == 0) return out;
    xm /= n; ym /= n;

    Spectrum dev;
    for (int i = 0; i < kGridPts; ++i)
        dev[(size_t) i] = (float) (s3[(size_t) i] - (ym - 4.5 * (std::log2 (gridFreq (i)) - xm)));

    // 1) resonances
    std::vector<BandSuggestion> cand;
    const int nb = (int) std::lround (kPtsPerOctave * 0.5);
    for (int i = 0; i < kGridPts; ++i)
    {
        const double f = gridFreq (i);
        if (f < 80 || f > 16000) continue;
        const double ex = narrow[(size_t) i] - wide[(size_t) i];
        if (ex < 4.0) continue;
        bool isMax = true;
        for (int k = std::max (0, i - nb); k <= std::min (kGridPts - 1, i + nb); ++k)
            if (narrow[(size_t) k] - wide[(size_t) k] > ex) { isMax = false; break; }
        if (! isMax) continue;
        if (! cand.empty() && std::abs (std::log2 (f / cand.back().freq)) < 1.0 / 3.0) continue;
        cand.push_back ({ FilterType::Bell, f, -std::min (6.0, std::max (1.5, 0.6 * ex)), 5.0 });
    }

    // 2) broad tonal balance, one candidate per octave
    const double centres[] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800 };
    for (int c = 0; c < 8; ++c)
    {
        double lo = centres[c] / 1.4142, hi = centres[c] * 1.4142;
        FilterType t = FilterType::Bell; double fc = centres[c], q = 0.8;
        if (c == 0) { lo = 20; t = FilterType::LowShelf; fc = 120; q = 0.7; }
        if (c == 7) { hi = 20000; t = FilterType::HighShelf; fc = 9000; q = 0.7; lo = 7000; }
        const double d = averageRange (dev, lo, hi);
        if (std::abs (d) < 2.5) continue;
        const double g = d > 0 ? -std::min (0.5 * d, 4.5) : std::min (-0.5 * d, 3.0);
        cand.push_back ({ t, fc, g, q });
    }

    std::sort (cand.begin(), cand.end(), [] (const BandSuggestion& a, const BandSuggestion& b)
               { return std::abs (a.gain) > std::abs (b.gain); });
    if (cand.size() > 8) cand.resize (8);
    std::sort (cand.begin(), cand.end(), [] (const BandSuggestion& a, const BandSuggestion& b) { return a.freq < b.freq; });
    return cand;
}

// ---------------------------------------------------------------------------------------------------------
// EQ MATCH: fits up to 9 bands so that (current + EQ) approaches the reference spectrum. The overall level
// difference is ignored (only the tonal shape is matched) and the result is moderated (85 %, max +/-12 dB).
inline double suggestionResponseDb (const std::vector<BandSuggestion>& s, double f, double fs = 48000.0)
{
    double db = 0;
    for (const auto& b : s)
    {
        BandParams p; p.enabled = true; p.type = b.type; p.freq = b.freq; p.gain = b.gain; p.q = b.q;
        db += bandMagnitudeDb (p, fs, f);
    }
    return db;
}

inline std::vector<BandSuggestion> suggestMatchEQ (const Spectrum& reference, const Spectrum& current)
{
    Spectrum diff, sm;
    for (int i = 0; i < kGridPts; ++i) diff[(size_t) i] = reference[(size_t) i] - current[(size_t) i];
    const double offset = averageRange (diff, 100, 10000);
    for (auto& v : diff) v = (float) (v - offset);
    smoothOctaves (diff, sm, 1.0 / 3.0);

    Spectrum residual;
    for (int i = 0; i < kGridPts; ++i)
        residual[(size_t) i] = (float) std::max (-12.0, std::min (12.0, 0.85 * sm[(size_t) i]));

    struct Slot { FilterType t; double f; };
    const Slot slots[] = { { FilterType::LowShelf, 50 }, { FilterType::Bell, 100 }, { FilterType::Bell, 200 },
                           { FilterType::Bell, 400 },  { FilterType::Bell, 800 },  { FilterType::Bell, 1600 },
                           { FilterType::Bell, 3200 }, { FilterType::Bell, 6400 }, { FilterType::HighShelf, 12000 } };
    std::vector<BandSuggestion> bands;
    for (const auto& s : slots) bands.push_back ({ s.t, s.f, 0.0, s.t == FilterType::Bell ? 1.0 : 0.7 });

    for (int pass = 0; pass < 3; ++pass)
        for (auto& b : bands)
        {
            const bool low = b.type == FilterType::LowShelf, high = b.type == FilterType::HighShelf;
            const double g = averageRange (residual, low ? 20.0 : b.freq / 1.4142, high ? 20000.0 : b.freq * 1.4142);
            const double delta = 0.8 * g;
            if (std::abs (delta) < 0.01) continue;
            BandParams p; p.enabled = true; p.type = b.type; p.freq = b.freq; p.q = b.q; p.gain = delta;
            for (int i = 0; i < kGridPts; ++i)
                residual[(size_t) i] -= (float) bandMagnitudeDb (p, 48000.0, gridFreq (i));
            b.gain += delta;
        }

    std::vector<BandSuggestion> out;
    for (auto& b : bands)
    {
        b.gain = std::max (-12.0, std::min (12.0, b.gain));
        if (std::abs (b.gain) >= 0.7) out.push_back (b);
    }
    return out;
}
} // namespace eqm
