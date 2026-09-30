#pragma once

#include "DSP/DspMath.h"
#include "DSP/Wavetable.h"
#include "Parameters/ParameterDefs.h"

namespace nedd
{
/** Final, fully modulated oscillator settings for one control block of one voice. */
struct OscillatorBlockParams
{
    OscEngine engine = OscEngine::Analog;
    AnalogWave wave = AnalogWave::Saw;
    NoiseType noise = NoiseType::White;
    FmAlgorithm algorithm = FmAlgorithm::Stack;
    const Wavetable* table = nullptr;

    float frequency = 440.0f;
    float pulseWidth = 0.5f;
    float wtPosition = 0.0f;
    int unison = 1;
    float detune = 0.0f;       // 0..1 -> up to +-50 cents across the stack
    float spread = 0.0f;       // stereo spread of the unison stack
    float pan = 0.0f;

    float op1Ratio = 2.0f, op2Ratio = 1.0f;
    float op1Index = 0.0f, op2Index = 0.0f;   // phase-modulation depth in cycles
    float feedback = 0.0f;
};

/**
    One oscillator slot of a voice: analog (PolyBLEP), wavetable, 3-operator FM or noise,
    with up to 8 unison sub-voices spread in pitch and stereo.
*/
class Oscillator
{
public:
    static constexpr int kMaxUnison = 8;

    struct Output
    {
        float left = 0.0f, right = 0.0f;
        float mono = 0.0f;            // normalised mono signal, used as FM / ring / sync source
        bool wrapped = false;         // first unison voice completed a cycle this sample
        float wrapFraction = 0.0f;    // how far past the wrap it is, in samples (0..1)
    };

    void prepare (float newSampleRate) noexcept { sampleRate = newSampleRate; }

    void noteOn (float startPhase, float phaseRandom, dsp::Random32& rng) noexcept;

    void setBlock (const OscillatorBlockParams& p) noexcept;

    /** phaseMod: external phase modulation in cycles. syncFraction >= 0 triggers a hard-sync reset. */
    Output tick (float phaseMod, float syncFraction) noexcept;

private:
    struct NoiseState
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        float brown = 0, crackle = 0, held = 0;
        dsp::Random32 rng;
    };

    float renderUnison (int index, float t, float dt) noexcept;
    float renderNoise (NoiseState& n, float dt, float& digitalPhase) noexcept;

    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    OscillatorBlockParams params;
    float sampleRate = 44100.0f;
    float startPhase = 0.0f;

    std::array<float, kMaxUnison> phase {};
    std::array<float, kMaxUnison> increment {};
    std::array<float, kMaxUnison> gainL {}, gainR {};
    std::array<int, kMaxUnison> mip {};
    std::array<float, kMaxUnison> op1Phase {}, op2Phase {};
    std::array<float, kMaxUnison> fbHistory {}, fbLast {};
    float unisonNorm = 1.0f;

    NoiseState noiseL, noiseR;
    float digitalPhaseL = 0.0f, digitalPhaseR = 0.0f;
};

} // namespace nedd
