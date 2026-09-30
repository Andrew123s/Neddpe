#pragma once

#include "DSP/DspMath.h"

namespace nedd::fx
{
/**
    Stereo-linked output protection limiter (no look-ahead, so no added latency).
    A fast peak follower pulls the gain down, it recovers with a smooth release, and a final
    soft clip catches the few samples that slip through the attack.
*/
class Limiter
{
public:
    void prepare (float sampleRate) noexcept
    {
        attackCoeff = 1.0f - std::exp (-1.0f / (0.0005f * sampleRate));
        releaseCoeff = 1.0f - std::exp (-1.0f / (0.12f * sampleRate));
        reset();
    }

    void reset() noexcept { gain = 1.0f; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
            const float target = peak > ceiling ? ceiling / peak : 1.0f;
            gain += (target < gain ? attackCoeff : releaseCoeff) * (target - gain);

            left[i] = softClip (left[i] * gain);
            right[i] = softClip (right[i] * gain);
        }
    }

    float getGainReduction() const noexcept { return gain; }

private:
    float softClip (float x) const noexcept
    {
        constexpr float knee = 0.9f;
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float over = (a - knee) / (1.0f - knee);
        const float shaped = knee + (1.0f - knee) * dsp::fastTanh (over);
        return x < 0.0f ? -shaped : shaped;
    }

    float ceiling = 0.966f;   // -0.3 dBFS
    float gain = 1.0f;
    float attackCoeff = 0.1f, releaseCoeff = 0.001f;
};

} // namespace nedd::fx
