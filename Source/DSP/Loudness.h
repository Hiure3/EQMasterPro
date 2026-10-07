#pragma once
#include "Biquad.h"
#include <array>
#include <cstdint>

// Loudness measurement following ITU-R BS.1770-4 / EBU R128 / EBU Tech 3341 & 3342.
// No allocation anywhere: safe to use on the audio thread.
namespace eqm
{
// K-weighting: stage 1 = high-shelf (head model), stage 2 = RLB high-pass. Coefficients are derived for any
// sample rate from the analogue prototype (same formulation as libebur128).
class KWeighting
{
public:
    void prepare (double fs)
    {
        fs = sanitize (fs, 8000.0, 768000.0, 48000.0);
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1.c.b0 = (Vh + Vb * K / Q + K * K) / a0;
            s1.c.b1 = 2.0 * (K * K - Vh) / a0;
            s1.c.b2 = (Vh - Vb * K / Q + K * K) / a0;
            s1.c.a1 = 2.0 * (K * K - 1.0) / a0;
            s1.c.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            s2.c.b0 = 1.0; s2.c.b1 = -2.0; s2.c.b2 = 1.0;
            s2.c.a1 = 2.0 * (K * K - 1.0) / a0;
            s2.c.a2 = (1.0 - K / Q + K * K) / a0;
        }
        reset();
    }
    void reset() { s1.reset(); s2.reset(); }
    inline double process (double x) noexcept { return s2.process (s1.process (x)); }
private:
    Biquad s1, s2;
};

// 4x oversampled true-peak detector (BS.1770 Annex 2 style polyphase FIR, 64 taps = 16 per phase).
class TruePeakMeter
{
public:
    TruePeakMeter() { buildFilter(); }
    void reset() { hist.fill (0.0f); pos = 0; peak = 0.0f; }

    inline void process (float x) noexcept
    {
        pos = (pos + 1) & (kHist - 1);
        hist[(size_t) pos] = x;
        hist[(size_t) pos + kHist] = x; // mirrored so the taps are contiguous
        const float* h = hist.data() + pos + kHist - (kTapsPerPhase - 1);
        for (int ph = 0; ph < kPhases; ++ph)
        {
            float acc = 0.0f;
            const float* c = phases[(size_t) ph].data();
            for (int k = 0; k < kTapsPerPhase; ++k) acc += c[k] * h[k];
            peak = std::max (peak, std::abs (acc));
        }
    }
    float getPeakLinear() const { return peak; }
    void resetPeak() { peak = 0.0f; }

private:
    static constexpr int kPhases = 4, kTapsPerPhase = 16, kHist = 16;
    void buildFilter()
    {
        const int N = kPhases * kTapsPerPhase;
        const double centre = (N - 1) / 2.0;
        std::array<double, 64> h {};
        for (int n = 0; n < N; ++n)
        {
            const double t = (n - centre) / kPhases;
            const double sinc = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double x = (double) n / (N - 1);                   // Blackman-Harris window
            const double w = 0.35875 - 0.48829 * std::cos (2 * kPi * x) + 0.14128 * std::cos (4 * kPi * x)
                             - 0.01168 * std::cos (6 * kPi * x);
            h[(size_t) n] = sinc * w;
        }
        for (int ph = 0; ph < kPhases; ++ph)
        {
            double sum = 0;
            for (int k = 0; k < kTapsPerPhase; ++k) sum += h[(size_t) (ph + k * kPhases)];
            for (int k = 0; k < kTapsPerPhase; ++k) // taps stored oldest -> newest
                phases[(size_t) ph][(size_t) (kTapsPerPhase - 1 - k)] = (float) (h[(size_t) (ph + k * kPhases)] / sum);
        }
        reset();
    }
    std::array<std::array<float, kTapsPerPhase>, kPhases> phases {};
    std::array<float, kHist * 2> hist {};
    int pos = 0;
    float peak = 0.0f;
};

inline double energyToLufs (double e) { return -0.691 + 10.0 * std::log10 (std::max (e, 1e-30)); }

// Stereo loudness meter: Momentary (400 ms), Short-term (3 s), Integrated (gated), LRA, True Peak.
class LoudnessMeter
{
public:
    static constexpr double kFloor = -120.0;

    void prepare (double sampleRate)
    {
        fs = sanitize (sampleRate, 8000.0, 768000.0, 48000.0);
        subLen = std::max (1, (int) std::lround (fs * 0.1));
        kw[0].prepare (fs); kw[1].prepare (fs);
        reset();
    }

    void reset()
    {
        kw[0].reset(); kw[1].reset();
        tp[0].reset(); tp[1].reset();
        sub.fill (0.0); subPos = 0; subCount = 0; accL = accR = 0.0; subFill = 0;
        histCountI.fill (0); histEnergyI.fill (0.0);
        histCountS.fill (0); histEnergyS.fill (0.0);
        M = S = I = kFloor; LRAv = 0.0;
    }

    void process (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];
            if (! std::isfinite (xl)) xl = 0.0f;
            if (! std::isfinite (xr)) xr = 0.0f;
            tp[0].process (xl); tp[1].process (xr);

            const double yl = kw[0].process (xl), yr = kw[1].process (xr);
            accL += yl * yl; accR += yr * yr;
            if (++subFill >= subLen)
            {
                finishSubBlock ((accL + accR) / subLen); // channel weights G = 1.0 for L and R
                accL = accR = 0.0; subFill = 0;
            }
        }
    }

    double momentary()  const { return M; }
    double shortTerm()  const { return S; }
    double integrated() const { return I; }
    double lra()        const { return LRAv; }
    double truePeakDb (int ch) const { return 20.0 * std::log10 (std::max (tp[ch & 1].getPeakLinear(), 1e-6f)); }
    void resetTruePeak() { tp[0].resetPeak(); tp[1].resetPeak(); }

private:
    static constexpr int kBins = 900;        // 0.1 LU bins from -70 LUFS to +20 LUFS
    static constexpr int kRing = 30;         // 30 x 100 ms = 3 s

    static int binOf (double lufs) { return (int) std::floor ((lufs + 70.0) * 10.0); }

    void finishSubBlock (double e)
    {
        sub[(size_t) subPos] = e;
        subPos = (subPos + 1) % kRing;
        if (subCount < kRing) ++subCount;

        double e400 = 0, e3s = 0;
        for (int k = 0; k < 4; ++k)  e400 += sub[(size_t) ((subPos - 1 - k + kRing * 2) % kRing)];
        for (int k = 0; k < kRing; ++k) e3s += sub[(size_t) k];
        e400 /= 4.0; e3s /= kRing;                     // not-yet-filled slots count as silence

        M = subCount >= 4 ? energyToLufs (e400) : kFloor;
        S = subCount >= kRing ? energyToLufs (e3s) : kFloor;

        if (subCount >= 4)      addToHist (histCountI, histEnergyI, M, e400);
        if (subCount >= kRing)  addToHist (histCountS, histEnergyS, S, e3s);
        I = computeIntegrated();
        LRAv = computeLRA();
    }

    template <typename C, typename E>
    static void addToHist (C& counts, E& energy, double lufs, double e)
    {
        if (lufs <= -70.0) return;                    // absolute gate
        const int b = std::min (kBins - 1, std::max (0, binOf (lufs)));
        counts[(size_t) b]++; energy[(size_t) b] += e;
    }

    double computeIntegrated() const
    {
        double sum = 0; uint64_t cnt = 0;
        for (int b = 0; b < kBins; ++b) { sum += histEnergyI[(size_t) b]; cnt += histCountI[(size_t) b]; }
        if (cnt == 0) return kFloor;
        const double relGate = energyToLufs (sum / (double) cnt) - 10.0;
        const int start = std::max (0, (int) std::ceil ((relGate + 70.0) * 10.0));
        double s2 = 0; uint64_t c2 = 0;
        for (int b = start; b < kBins; ++b) { s2 += histEnergyI[(size_t) b]; c2 += histCountI[(size_t) b]; }
        return c2 ? energyToLufs (s2 / (double) c2) : kFloor;
    }

    double computeLRA() const
    {
        double sum = 0; uint64_t cnt = 0;
        for (int b = 0; b < kBins; ++b) { sum += histEnergyS[(size_t) b]; cnt += histCountS[(size_t) b]; }
        if (cnt == 0) return 0.0;
        const double relGate = energyToLufs (sum / (double) cnt) - 20.0;
        const int start = std::max (0, (int) std::ceil ((relGate + 70.0) * 10.0));
        uint64_t total = 0;
        for (int b = start; b < kBins; ++b) total += histCountS[(size_t) b];
        if (total < 2) return 0.0;
        auto percentile = [&] (double p)
        {
            const double target = p * (double) (total - 1);
            uint64_t cum = 0;
            for (int b = start; b < kBins; ++b)
            {
                cum += histCountS[(size_t) b];
                if ((double) cum > target && histCountS[(size_t) b] > 0)
                    return energyToLufs (histEnergyS[(size_t) b] / (double) histCountS[(size_t) b]);
            }
            return kFloor;
        };
        return std::max (0.0, percentile (0.95) - percentile (0.10));
    }

    double fs = 48000.0;
    int subLen = 4800, subFill = 0, subPos = 0, subCount = 0;
    double accL = 0, accR = 0;
    std::array<double, kRing> sub {};
    std::array<uint32_t, kBins> histCountI {}, histCountS {};
    std::array<double, kBins> histEnergyI {}, histEnergyS {};
    KWeighting kw[2];
    TruePeakMeter tp[2];
    double M = kFloor, S = kFloor, I = kFloor, LRAv = 0;
};
} // namespace eqm
