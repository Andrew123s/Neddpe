#pragma once

#include "DspMath.h"
#include "Parameters/ParameterDefs.h"

namespace nedd::dsp
{
/**
    Stereo per-voice filter.

    - SVF modes use the topology-preserving-transform state variable filter (Zavalishin /
      Simper). It stays stable under audio-rate cutoff modulation, which matters because every
      note modulates its own cutoff from MPE pressure and slide.
    - 24 dB modes cascade two SVF stages tuned to a Butterworth pair, with resonance applied to
      the second stage.
    - Ladder mode is a zero-delay-feedback 4-pole ladder: the feedback equation is solved
      linearly, then the loop input is saturated with tanh for the classic behaviour.

    Coefficients are interpolated per sample across each control block.
*/
class VoiceFilter
{
public:
    void prepare (float newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        for (auto& stage : svf)
            for (auto& ch : stage)
                ch = {};
        for (auto& ch : ladder)
            for (auto& s : ch)
                s = 0.0f;
        initialised = false;
    }

    void setType (FilterType newType) noexcept
    {
        if (newType != type)
        {
            type = newType;
            reset();
        }
    }

    /** Sets targets reached at the end of the next numSamples samples. */
    void setBlockTargets (float cutoffHz, float resonance, float drive, int numSamples) noexcept
    {
        const float fc = std::clamp (cutoffHz, 16.0f, sampleRate * 0.45f);
        const float newG = std::tan (kPi * fc / sampleRate);
        const float res = clamp01 (resonance);
        const float newK = type == FilterType::Ladder ? res * 3.95f : 2.0f - 1.96f * res;

        driveGain = 1.0f + drive * drive * 14.0f;
        driveMakeup = 1.0f / std::sqrt (driveGain);

        if (! initialised)
        {
            g = newG;
            k = newK;
            initialised = true;
        }

        const float inv = numSamples > 0 ? 1.0f / (float) numSamples : 1.0f;
        gStep = (newG - g) * inv;
        kStep = (newK - k) * inv;
    }

    void process (float& left, float& right) noexcept
    {
        g += gStep;
        k += kStep;

        if (driveGain > 1.001f && type != FilterType::Ladder)
        {
            left = fastTanh (left * driveGain) * driveMakeup;
            right = fastTanh (right * driveGain) * driveMakeup;
        }

        switch (type)
        {
            case FilterType::LowPass12:  processSvf1 (left, right, 0); break;
            case FilterType::HighPass12: processSvf1 (left, right, 2); break;
            case FilterType::BandPass:   processSvf1 (left, right, 1); break;
            case FilterType::Notch:      processSvf1 (left, right, 3); break;
            case FilterType::LowPass24:  processSvf2 (left, right, true); break;
            case FilterType::HighPass24: processSvf2 (left, right, false); break;
            case FilterType::Ladder:     processLadder (left, right); break;
        }
    }

private:
    struct SvfState { float ic1 = 0.0f, ic2 = 0.0f; };

    struct SvfCoeffs
    {
        float a1, a2, a3, k;
        SvfCoeffs (float gIn, float kIn) noexcept : k (kIn)
        {
            a1 = 1.0f / (1.0f + gIn * (gIn + kIn));
            a2 = gIn * a1;
            a3 = gIn * a2;
        }
    };

    /** mode: 0 low, 1 band (peak-normalised), 2 high, 3 notch */
    static float tick (SvfState& s, float v0, const SvfCoeffs& c, int mode) noexcept
    {
        const float v3 = v0 - s.ic2;
        const float v1 = c.a1 * s.ic1 + c.a2 * v3;
        const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;

        switch (mode)
        {
            case 0:  return v2;
            case 1:  return v1 * c.k;
            case 2:  return v0 - c.k * v1 - v2;
            default: return v0 - c.k * v1;
        }
    }

    void processSvf1 (float& l, float& r, int mode) noexcept
    {
        const SvfCoeffs c (g, k);
        l = tick (svf[0][0], l, c, mode);
        r = tick (svf[0][1], r, c, mode);
    }

    void processSvf2 (float& l, float& r, bool lowPass) noexcept
    {
        // Butterworth pair (Q 0.54 / 1.31); resonance sharpens the second stage.
        constexpr float kFirst = 1.8478f;
        const float kSecond = std::min (0.7654f, k);
        const SvfCoeffs c1 (g, kFirst);
        const SvfCoeffs c2 (g, kSecond);
        const int mode = lowPass ? 0 : 2;
        l = tick (svf[1][0], tick (svf[0][0], l, c1, mode), c2, mode);
        r = tick (svf[1][1], tick (svf[0][1], r, c1, mode), c2, mode);
    }

    void processLadder (float& l, float& r) noexcept
    {
        const float G = g / (1.0f + g);
        const float invOnePlusG = 1.0f / (1.0f + g);
        const float G2 = G * G;
        const float G3 = G2 * G;
        const float G4 = G3 * G;
        const float compensation = 1.0f + k * 0.55f;

        auto run = [&] (float x, float* s) noexcept
        {
            const float sigma = (G3 * s[0] + G2 * s[1] + G * s[2] + s[3]) * invOnePlusG;
            float u = (x * driveGain * compensation - k * sigma) / (1.0f + k * G4);
            u = fastTanh (u);

            float in = u;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (in - s[i]) * G;
                const float y = v + s[i];
                s[i] = y + v;
                in = y;
            }
            return in * driveMakeup;
        };

        l = run (l, ladder[0]);
        r = run (r, ladder[1]);
    }

    FilterType type = FilterType::LowPass24;
    float sampleRate = 44100.0f;
    float g = 0.1f, gStep = 0.0f, k = 1.0f, kStep = 0.0f;
    float driveGain = 1.0f, driveMakeup = 1.0f;
    bool initialised = false;

    SvfState svf[2][2];
    float ladder[2][4] {};
};

} // namespace nedd::dsp
