#pragma once

#include "EffectUnits.h"
#include "Modulation/ModTypes.h"
#include "Parameters/ParamSnapshot.h"
#include "Synth/Tempo.h"

namespace nedd::fx
{
/**
    Global effect chain (after all voices are summed):

        Distortion > Saturation > Bitcrush > Chorus > Phaser > Flanger
        + Delay return (fed by per-voice delay sends)
        + Reverb return (fed by per-voice reverb sends)
        > EQ > Compressor

    The delay and reverb inputs are the per-voice send buses, so how much of each note reaches
    them is a per-note decision (e.g. pressure or velocity to send), while the effects
    themselves run once.
*/
class EffectsChain
{
public:
    void prepare (float sampleRate, int maxBlock);
    void reset();

    void process (float* l, float* r, const float* delayL, const float* delayR, const float* reverbL, const float* reverbR, int n,
                  const ParamSnapshot& p, const std::array<float, (size_t) kNumModDests>& mod, const TransportInfo& transport,
                  Quality quality) noexcept;

    float getCompressorReductionDb() const noexcept { return compressor.getGainReductionDb(); }

private:
    bool justEnabled (int index, bool on) noexcept;

    float sampleRate = 44100.0f;
    Distortion distortion;
    Saturation saturation;
    Bitcrusher crusher;
    ModulatedDelay chorus { ModulatedDelay::Mode::Chorus };
    Phaser phaser;
    ModulatedDelay flanger { ModulatedDelay::Mode::Flanger };
    StereoDelay delay;
    FdnReverb reverb;
    Equalizer eq;
    Compressor compressor;
    std::array<bool, 10> wasOn {};
};

} // namespace nedd::fx
