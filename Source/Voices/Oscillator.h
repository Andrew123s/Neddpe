#pragma once

#include "DSP/DspMath.h"
#include "DSP/Simd.h"
#include "DSP/Wavetable.h"
#include "Parameters/ParameterDefs.h"
#include "Synth/OscillatorAssets.h"

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
    const SampleData* sample = nullptr;        // Sample / Granular source (never null for those engines)

    float frequency = 440.0f;
    float pulseWidth = 0.5f;
    float wtPosition = 0.0f;   // wavetable position; sample start; grain position
    int unison = 1;
    float detune = 0.0f;       // 0..1 -> up to +-50 cents across the stack
    float spread = 0.0f;       // stereo spread of the unison stack / grain scatter
    float pan = 0.0f;

    float op1Ratio = 2.0f, op2Ratio = 1.0f;
    float op1Index = 0.0f, op2Index = 0.0f;   // phase-modulation depth in cycles
    float feedback = 0.0f;

    float rootHz = 261.6256f;  // pitch at which the sample plays at its recorded speed
    bool loop = true;
    float grainSeconds = 0.08f;
    float grainDensity = 30.0f;               // grains per second
    float grainSpray = 0.0f;                  // 0..1 position scatter
    float grainPitchSpray = 0.0f;             // semitones
};

/**
    One oscillator slot of a voice: analog (PolyBLEP), wavetable, 3-operator FM, noise,
    granular or sample playback, with up to 8 unison sub-voices spread in pitch and stereo.

    - Unison sub-voices of the analog and FM engines are rendered four at a time with SIMD.
    - Hard sync is band-limited: each reset adds a PolyBLEP correction for the jump it causes,
      spread over the sample before and the sample after the reset. That needs a one-sample
      look-behind, so every engine's output is delayed by exactly one sample.
    - The oscillator can run at 2x or 4x the voice's sample rate (setOversampling); the voice
      decimates the result.
*/
class Oscillator
{
public:
    static constexpr int kMaxUnison = 8;
    static constexpr int kMaxGrains = 32;

    struct Output
    {
        float left = 0.0f, right = 0.0f;
        float mono = 0.0f;            // normalised mono signal, used as FM / ring / sync source
        bool wrapped = false;         // first unison voice completed a cycle this sample
        float wrapFraction = 0.0f;    // how far past the wrap it is, in samples (0..1)
    };

    void prepare (float newSampleRate) noexcept
    {
        baseRate = newSampleRate;
        rate = baseRate * (float) oversampling;
    }

    /** 1, 2 or 4. Call before noteOn. */
    void setOversampling (int factor) noexcept
    {
        oversampling = factor == 4 ? 4 : (factor == 2 ? 2 : 1);
        rate = baseRate * (float) oversampling;
    }

    void noteOn (float startPhase, float phaseRandom, dsp::Random32& rng) noexcept;

    void setBlock (const OscillatorBlockParams& p) noexcept;

    /** phaseMod: external phase modulation in cycles. syncFraction >= 0 triggers a hard-sync reset. */
    Output tick (float phaseMod, float syncFraction) noexcept;

private:
    struct Frame { float left = 0.0f, right = 0.0f, mono = 0.0f; };

    struct NoiseState
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        float brown = 0, crackle = 0, held = 0;
        dsp::Random32 rng;
    };

    struct Grain
    {
        double position = 0.0;        // source samples
        float increment = 1.0f;       // source samples per output sample
        float window = 0.0f;          // 0..1 through the grain
        float windowIncrement = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
        bool active = false;
    };

    void renderAnalog (float phaseMod, Frame& f) noexcept;
    void renderFm (Frame& f, float phaseMod) noexcept;
    void renderWavetable (float phaseMod, Frame& f) noexcept;
    void renderSynced (float phaseMod, float syncFraction, Frame& f) noexcept;
    void renderSample (float syncFraction, Frame& f, Output& out) noexcept;
    void renderGranular (Frame& f) noexcept;
    void renderNoiseFrame (Frame& f) noexcept;
    void spawnGrain() noexcept;
    void advanceLanePhases (Output& out) noexcept;

    float naiveValue (int lane, float t, float p1, float p2) const noexcept;
    float renderNoise (NoiseState& n, float dt, float& digitalPhase) noexcept;
    float sampleStartPosition() const noexcept;

    OscillatorBlockParams params;
    float baseRate = 44100.0f;
    float rate = 44100.0f;          // baseRate * oversampling
    int oversampling = 1;
    int numLanes = 1;
    float startPhase = 0.0f;
    float lastPhaseMod = 0.0f;

    // Unison lanes (padded lanes have zero gain and a safe increment).
    alignas (16) std::array<float, kMaxUnison> phase {};
    alignas (16) std::array<float, kMaxUnison> increment {};
    alignas (16) std::array<float, kMaxUnison> gainL {}, gainR {};
    alignas (16) std::array<float, kMaxUnison> laneWeight {};
    alignas (16) std::array<float, kMaxUnison> op1Phase {}, op2Phase {};
    alignas (16) std::array<float, kMaxUnison> fbHistory {}, fbLast {};
    std::array<int, kMaxUnison> mip {};

    // Sample engine
    std::array<double, kMaxUnison> samplePos {};
    std::array<float, kMaxUnison> sampleIncrement {};
    bool sampleNeedsStart = true;

    // Granular engine
    std::array<Grain, (size_t) kMaxGrains> grains {};
    float grainClock = 1.0f;
    float grainNorm = 1.0f;
    float grainBaseIncrement = 1.0f;
    dsp::Random32 grainRng;

    // Noise (rendered at the base rate and held, so oversampling does not change its spectrum)
    NoiseState noiseL, noiseR;
    float digitalPhaseL = 0.0f, digitalPhaseR = 0.0f;
    Frame noiseHeld;
    int noiseCounter = 0;

    // One-sample delay that lets a sync reset correct the previous sample.
    Frame pending;
};

} // namespace nedd
