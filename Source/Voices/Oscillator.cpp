#include "Oscillator.h"

namespace nedd
{
namespace
{
    constexpr float kGoldenRatioFraction = 0.61803398875f;
    constexpr float kMaxUnisonCents = 50.0f;

    inline float wrap01 (float x) noexcept { return dsp::wrapPhase (x); }
}

void Oscillator::noteOn (float newStartPhase, float phaseRandom, dsp::Random32& rng) noexcept
{
    startPhase = newStartPhase;

    for (int i = 0; i < kMaxUnison; ++i)
    {
        // Unison voices start at spread-out phases so the stack does not phase-cancel on attack.
        const float spreadPhase = i > 0 ? kGoldenRatioFraction * (float) i : 0.0f;
        phase[(size_t) i] = wrap01 (startPhase + spreadPhase + rng.nextFloat() * phaseRandom);
        op1Phase[(size_t) i] = 0.0f;
        op2Phase[(size_t) i] = 0.0f;
        fbHistory[(size_t) i] = 0.0f;
        fbLast[(size_t) i] = 0.0f;
    }

    noiseL.rng.seed (rng.next());
    noiseR.rng.seed (rng.next());
    digitalPhaseL = digitalPhaseR = 0.0f;
}

void Oscillator::setBlock (const OscillatorBlockParams& p) noexcept
{
    params = p;
    params.unison = juce::jlimit (1, kMaxUnison, p.unison);

    const int n = params.engine == OscEngine::Noise ? 1 : params.unison;
    unisonNorm = 1.0f / std::sqrt ((float) n);

    for (int i = 0; i < n; ++i)
    {
        const float offset = n > 1 ? ((float) i / (float) (n - 1)) * 2.0f - 1.0f : 0.0f;
        const float cents = offset * dsp::clamp01 (params.detune) * kMaxUnisonCents;
        const float freq = params.frequency * std::exp2 (cents * (1.0f / 1200.0f));

        increment[(size_t) i] = std::min (freq / sampleRate, 0.49f);
        dsp::panGains (params.pan + offset * params.spread, gainL[(size_t) i], gainR[(size_t) i]);

        if (params.engine == OscEngine::Wavetable)
            mip[(size_t) i] = Wavetable::selectMip (increment[(size_t) i], sampleRate);
    }
}

float Oscillator::renderUnison (int i, float t, float dt) noexcept
{
    switch (params.engine)
    {
        case OscEngine::Analog:
        {
            switch (params.wave)
            {
                case AnalogWave::Sine:     return dsp::fastSin (t);
                case AnalogWave::Triangle: return 4.0f * std::abs (wrap01 (t + 0.75f) - 0.5f) - 1.0f;
                case AnalogWave::Saw:      return 2.0f * t - 1.0f - polyBlep (t, dt);
                case AnalogWave::Square:
                case AnalogWave::Pulse:
                {
                    const float pw = params.wave == AnalogWave::Square ? 0.5f : std::clamp (params.pulseWidth, 0.02f, 0.98f);
                    float v = t < pw ? 1.0f : -1.0f;
                    v += polyBlep (t, dt);
                    v -= polyBlep (wrap01 (t + 1.0f - pw), dt);
                    return v - (2.0f * pw - 1.0f);   // remove DC of asymmetric pulses
                }
            }
            return 0.0f;
        }

        case OscEngine::Wavetable:
            return params.table != nullptr ? params.table->sample (t, params.wtPosition, mip[(size_t) i]) : 0.0f;

        case OscEngine::FM:
        {
            auto& p1 = op1Phase[(size_t) i];
            auto& p2 = op2Phase[(size_t) i];
            const float fb = params.feedback * 0.3f * fbHistory[(size_t) i];
            float fbSource = 0.0f;
            float out = 0.0f;

            switch (params.algorithm)
            {
                case FmAlgorithm::Stack:
                {
                    const float m2 = dsp::fastSin (p2 + fb);
                    const float m1 = dsp::fastSin (p1 + params.op2Index * m2);
                    out = dsp::fastSin (t + params.op1Index * m1);
                    fbSource = m2;
                    break;
                }
                case FmAlgorithm::Parallel:
                {
                    const float m1 = dsp::fastSin (p1 + fb);
                    const float m2 = dsp::fastSin (p2);
                    out = dsp::fastSin (t + params.op1Index * m1 + params.op2Index * m2);
                    fbSource = m1;
                    break;
                }
                case FmAlgorithm::Branch:
                {
                    const float m2 = dsp::fastSin (p2 + fb);
                    const float m1 = dsp::fastSin (p1 + params.op2Index * m2);
                    out = dsp::fastSin (t + params.op1Index * m1 + params.op2Index * 0.5f * m2);
                    fbSource = m2;
                    break;
                }
            }

            // Averaging the last two feedback samples tames the classic FM feedback buzz.
            fbHistory[(size_t) i] = 0.5f * (fbSource + fbLast[(size_t) i]);
            fbLast[(size_t) i] = fbSource;

            p1 = wrap01 (p1 + dt * params.op1Ratio);
            p2 = wrap01 (p2 + dt * params.op2Ratio);
            return out;
        }

        case OscEngine::Noise:
            return 0.0f;
    }

    return 0.0f;
}

float Oscillator::renderNoise (NoiseState& n, float dt, float& digitalPhase) noexcept
{
    const float white = n.rng.nextBipolar();

    switch (params.noise)
    {
        case NoiseType::White:
            return white * 0.7f;

        case NoiseType::Pink:
        {
            // Paul Kellet's refined pink-noise filter.
            n.b0 = 0.99886f * n.b0 + white * 0.0555179f;
            n.b1 = 0.99332f * n.b1 + white * 0.0750759f;
            n.b2 = 0.96900f * n.b2 + white * 0.1538520f;
            n.b3 = 0.86650f * n.b3 + white * 0.3104856f;
            n.b4 = 0.55000f * n.b4 + white * 0.5329522f;
            n.b5 = -0.7616f * n.b5 - white * 0.0168980f;
            const float pink = n.b0 + n.b1 + n.b2 + n.b3 + n.b4 + n.b5 + n.b6 + white * 0.5362f;
            n.b6 = white * 0.115926f;
            return pink * 0.11f;
        }

        case NoiseType::Brown:
            n.brown = (n.brown + 0.02f * white) * (1.0f / 1.02f);
            return n.brown * 3.5f;

        case NoiseType::Crackle:
        {
            // Sparse random impulses; density follows the note pitch.
            const float density = std::min (0.25f, dt * 8.0f);
            if (n.rng.nextFloat() < density)
                n.crackle = n.rng.nextBipolar();
            n.crackle *= 0.9f;
            return n.crackle;
        }

        case NoiseType::Digital:
        {
            // Sample-and-hold noise clocked at the note frequency: pitched, gritty.
            digitalPhase += dt;
            if (digitalPhase >= 1.0f)
            {
                digitalPhase -= std::floor (digitalPhase);
                n.held = white;
            }
            return n.held * 0.7f;
        }
    }

    return 0.0f;
}

Oscillator::Output Oscillator::tick (float phaseMod, float syncFraction) noexcept
{
    Output out;

    if (params.engine == OscEngine::Noise)
    {
        const float dt = increment[0];
        const float l = renderNoise (noiseL, dt, digitalPhaseL);
        const float r = dsp::lerp (l, renderNoise (noiseR, dt, digitalPhaseR), dsp::clamp01 (params.spread));
        out.left = l * gainL[0] * 1.4142f;
        out.right = r * gainR[0] * 1.4142f;
        out.mono = l;
        return out;
    }

    const int n = params.unison;
    float mono = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        auto& ph = phase[(size_t) i];
        const float dt = increment[(size_t) i];

        if (syncFraction >= 0.0f)
        {
            ph = wrap01 (startPhase + syncFraction * dt);
            op1Phase[(size_t) i] = 0.0f;
            op2Phase[(size_t) i] = 0.0f;
        }

        const float t = wrap01 (ph + phaseMod);
        const float v = renderUnison (i, t, dt);

        out.left += v * gainL[(size_t) i];
        out.right += v * gainR[(size_t) i];
        mono += v;

        ph += dt;
        if (ph >= 1.0f)
        {
            ph -= 1.0f;
            if (i == 0)
            {
                out.wrapped = true;
                out.wrapFraction = dt > 0.0f ? ph / dt : 0.0f;
            }
        }
    }

    out.left *= unisonNorm;
    out.right *= unisonNorm;
    out.mono = mono / (float) n;
    return out;
}

} // namespace nedd
