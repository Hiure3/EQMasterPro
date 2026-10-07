#pragma once
#include <algorithm>
#include <cmath>

namespace eqm
{
constexpr double kPi = 3.14159265358979323846;

enum class FilterType : int
{
    Bell = 0, LowShelf, HighShelf, LowPass, HighPass, Notch, BandPass, Tilt, AllPass, Count
};

inline bool typeUsesGain (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf
        || t == FilterType::HighShelf || t == FilterType::Tilt;
}

inline double sanitize (double v, double lo, double hi, double fallback)
{
    if (! std::isfinite (v)) return fallback;
    return std::min (hi, std::max (lo, v));
}

struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// RBJ audio-EQ-cookbook biquads. All inputs are sanitised, so the result is always finite and stable.
inline Coeffs makeCoeffs (FilterType type, double fs, double freq, double gainDb, double q)
{
    fs     = sanitize (fs, 8000.0, 768000.0, 44100.0);
    freq   = sanitize (freq, 5.0, fs * 0.4999, 1000.0);
    gainDb = sanitize (gainDb, -48.0, 48.0, 0.0);
    q      = sanitize (q, 0.05, 50.0, 0.7071);

    const double w0 = 2.0 * kPi * freq / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double sqA = std::sqrt (A);

    double b0, b1, b2, a0, a1, a2;
    switch (type)
    {
        case FilterType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + 2 * sqA * alpha);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - 2 * sqA * alpha);
            a0 = (A + 1) + (A - 1) * cw + 2 * sqA * alpha;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - 2 * sqA * alpha;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + 2 * sqA * alpha);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - 2 * sqA * alpha);
            a0 = (A + 1) - (A - 1) * cw + 2 * sqA * alpha;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - 2 * sqA * alpha;
            break;
        case FilterType::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case FilterType::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case FilterType::Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case FilterType::BandPass: // constant 0 dB peak gain
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case FilterType::AllPass:
            b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case FilterType::Bell:
        case FilterType::Tilt: // Tilt is built from two shelves, see makeStages()
        default:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
            break;
    }

    Coeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
    c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

struct Stages { Coeffs s1, s2; };

// A band is one or two cascaded biquads (Tilt = low shelf -g/2 + high shelf +g/2 around the frequency).
inline Stages makeStages (FilterType t, double fs, double f, double gainDb, double q)
{
    if (t == FilterType::Tilt)
        return { makeCoeffs (FilterType::LowShelf,  fs, f, -gainDb * 0.5, 0.7071),
                 makeCoeffs (FilterType::HighShelf, fs, f,  gainDb * 0.5, 0.7071) };
    return { makeCoeffs (t, fs, f, gainDb, q), Coeffs {} };
}

inline double magSq (const Coeffs& c, double w)
{
    const double cw = std::cos (w), sw = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
    const double nr = c.b0 + c.b1 * cw + c.b2 * c2, ni = -(c.b1 * sw + c.b2 * s2);
    const double dr = 1 + c.a1 * cw + c.a2 * c2,     di = -(c.a1 * sw + c.a2 * s2);
    return (nr * nr + ni * ni) / std::max (dr * dr + di * di, 1e-30);
}

inline double magDb (const Coeffs& c, double fs, double f)
{
    return 10.0 * std::log10 (std::max (magSq (c, 2.0 * kPi * f / fs), 1e-30));
}

// Transposed direct form II, double precision
struct Biquad
{
    Coeffs c;
    double z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};
} // namespace eqm
