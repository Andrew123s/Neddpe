#pragma once

#include "DSP/Lfo.h"
#include "DSP/Wavetable.h"
#include "Modulation/ModMatrix.h"
#include "Parameters/ParamSnapshot.h"
#include "Synth/OscillatorAssets.h"
#include "Synth/Tuning.h"

namespace nedd
{
/** A/B morph data prepared once per block by the engine. */
struct MorphContext
{
    bool enabled = false;
    float position = 0.0f;             // base morph position (parameter)
    NormalisedPatch liveNormalised {}; // A = the live patch
    NormalisedPatch targetNormalised {};
    ParamSnapshot targetPlain;         // B
};

/** Everything a voice needs for one block. Built by the engine; read-only for voices. */
struct VoiceContext
{
    const ParamSnapshot* params = nullptr;
    const ModRouting* routing = nullptr;
    const MorphContext* morph = nullptr;
    const TuningData* tuning = nullptr;
    const dsp::LfoCustomShapes* lfoShapes = nullptr;
    const WavetableBank* wavetables = nullptr;
    const OscillatorAssets* assets = nullptr;   // imported wavetables and samples (may be null)

    /** Values of the global (not per-note) modulation sources: wheel, bend, aftertouch, macros. */
    std::array<float, (size_t) kNumModSources> globalSources {};

    float sampleRate = 44100.0f;
    int controlBlockSize = 32;

    // Pre-computed per block
    std::array<float, (size_t) kNumLfos> lfoRateHz {};
    std::array<float, (size_t) kNumLfos> globalLfoPhase {};   // phase of the free-running / host-synced LFO
    float sampleHoldRateHz = 8.0f;
    std::array<bool, 12> scaleMask {};
    int scaleRoot = 0;
    bool scaleActive = false;
    bool customTuning = false;
};

/** Output buses a voice renders into. */
struct VoiceBuses
{
    float* mainL = nullptr;
    float* mainR = nullptr;
    float* delayL = nullptr;
    float* delayR = nullptr;
    float* reverbL = nullptr;
    float* reverbR = nullptr;
};

} // namespace nedd
