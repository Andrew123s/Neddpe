#pragma once

#include "DspMath.h"

namespace nedd::dsp
{
/**
    Half-band FIR decimator: takes two samples at twice the output rate and returns one.

    47-tap Kaiser-windowed (beta 8) half-band, designed once at start-up. Every other
    coefficient is zero and the rest are symmetric, so each output costs 12 multiplies plus
    the centre tap. Passband to ~0.39 fs (output rate), stopband below -80 dB from ~0.61 fs:
    anything the oscillators produce between the output Nyquist frequency and the
    oversampled Nyquist frequency is removed instead of folding back as aliasing.
    Latency: 11.5 output samples.
*/
class HalfbandDecimator
{
public:
    static constexpr int kTaps = 47;
    static constexpr int kCentre = kTaps / 2;           // 23
    static constexpr int kPairs = (kCentre + 1) / 2;    // 12 non-zero taps each side

    void reset() noexcept
    {
        history.fill (0.0f);
        writePos = 0;
    }

    float process (float first, float second) noexcept
    {
        push (first);
        push (second);

        // history[writePos .. writePos + kTaps - 1] is the newest kTaps samples, oldest first.
        const float* x = history.data() + writePos;
        const auto& c = coefficients();
        float acc = c[0] * x[kCentre];
        for (int k = 0; k < kPairs; ++k)
        {
            const int offset = 2 * k + 1;
            acc += c[(size_t) k + 1] * (x[kCentre - offset] + x[kCentre + offset]);
        }
        return acc;
    }

private:
    void push (float v) noexcept
    {
        // Doubled buffer: every sample is written twice so the read window is always contiguous.
        history[(size_t) writePos] = v;
        history[(size_t) (writePos + kTaps)] = v;
        writePos = writePos + 1 == kTaps ? 0 : writePos + 1;
    }

    /** [0] = centre tap, [k + 1] = the pair at distance 2k + 1. */
    static const std::array<float, (size_t) kPairs + 1>& coefficients() noexcept
    {
        static const std::array<float, (size_t) kPairs + 1> table = []
        {
            std::array<double, (size_t) kPairs + 1> c {};
            c[0] = 0.5;
            constexpr double beta = 8.0;
            auto besselI0 = [] (double x)
            {
                double sum = 1.0, term = 1.0;
                for (int k = 1; k < 40; ++k)
                {
                    term *= (x / (2.0 * k)) * (x / (2.0 * k));
                    sum += term;
                }
                return sum;
            };

            double total = 0.5;
            for (int k = 0; k < kPairs; ++k)
            {
                const int n = 2 * k + 1;                                     // distance from the centre
                const double sinc = std::sin (juce::MathConstants<double>::pi * 0.5 * n) / (juce::MathConstants<double>::pi * n);
                const double r = (double) n / (double) kCentre;
                const double window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
                c[(size_t) k + 1] = sinc * window;
                total += 2.0 * sinc * window;
            }

            // Unity DC gain.
            std::array<float, (size_t) kPairs + 1> result {};
            for (size_t i = 0; i < c.size(); ++i)
                result[i] = (float) (c[i] / total);
            return result;
        }();
        return table;
    }

    std::array<float, (size_t) kTaps * 2> history {};
    int writePos = 0;
};

/** 1x / 2x / 4x decimation for one channel (4x = two cascaded half-band stages). */
class OversamplingDecimator
{
public:
    void reset() noexcept
    {
        stage1.reset();
        stage2.reset();
    }

    /** Takes `factor` input samples, returns one output sample. */
    float process (const float* in, int factor) noexcept
    {
        if (factor == 2)
            return stage1.process (in[0], in[1]);
        if (factor == 4)
        {
            const float a = stage2.process (in[0], in[1]);
            const float b = stage2.process (in[2], in[3]);
            return stage1.process (a, b);
        }
        return in[0];
    }

private:
    HalfbandDecimator stage1, stage2;
};

} // namespace nedd::dsp
