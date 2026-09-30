#include "EffectsChain.h"

namespace nedd::fx
{
void EffectsChain::prepare (float newSampleRate, int maxBlock)
{
    sampleRate = newSampleRate;
    distortion.prepare (sampleRate, maxBlock);
    saturation.prepare (sampleRate);
    chorus.prepare (sampleRate);
    phaser.prepare (sampleRate);
    flanger.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    eq.prepare (sampleRate);
    compressor.prepare (sampleRate);
    reset();
}

void EffectsChain::reset()
{
    distortion.reset();
    saturation.reset();
    crusher.reset();
    chorus.reset();
    phaser.reset();
    flanger.reset();
    delay.reset();
    reverb.reset();
    eq.reset();
    compressor.reset();
    wasOn.fill (false);
}

bool EffectsChain::justEnabled (int index, bool on) noexcept
{
    const bool result = on && ! wasOn[(size_t) index];
    wasOn[(size_t) index] = on;
    return result;
}

void EffectsChain::process (float* l, float* r, const float* delayL, const float* delayR, const float* reverbL, const float* reverbR, int n,
                            const ParamSnapshot& p, const std::array<float, (size_t) kNumModDests>& mod, const TransportInfo& transport,
                            Quality quality) noexcept
{
    auto m = [&mod] (ModDest d) { return mod[(size_t) d]; };
    auto on = [&p] (FxField f) { return p.getBool (pid::fx (f)); };
    auto v = [&p] (FxField f) { return p[pid::fx (f)]; };

    // Distortion (oversampled according to the CPU quality setting)
    const bool distOn = on (FxField::DistOn);
    if (justEnabled (0, distOn)) distortion.reset();
    if (distOn)
        distortion.process (l, r, n, p.getChoice<DistortionType> (pid::fx (FxField::DistType)),
                            dsp::clamp01 (v (FxField::DistDrive) + m (ModDest::DistDrive)), v (FxField::DistTone),
                            dsp::clamp01 (v (FxField::DistMix) + m (ModDest::DistMix)), (int) quality);

    const bool satOn = on (FxField::SatOn);
    if (justEnabled (1, satOn)) saturation.reset();
    if (satOn)
        saturation.process (l, r, n, dsp::clamp01 (v (FxField::SatDrive) + m (ModDest::SatDrive)), v (FxField::SatWarmth), v (FxField::SatMix));

    const bool crushOn = on (FxField::CrushOn);
    if (justEnabled (2, crushOn)) crusher.reset();
    if (crushOn)
        crusher.process (l, r, n, v (FxField::CrushBits) - m (ModDest::CrushAmount) * 14.0f, v (FxField::CrushDownsample), v (FxField::CrushMix));

    const bool chorusOn = on (FxField::ChorusOn);
    if (justEnabled (3, chorusOn)) chorus.reset();
    if (chorusOn)
        chorus.process (l, r, n, v (FxField::ChorusRate), v (FxField::ChorusDepth), 0.0f, dsp::clamp01 (v (FxField::ChorusMix) + m (ModDest::ChorusMix)));

    const bool phaserOn = on (FxField::PhaserOn);
    if (justEnabled (4, phaserOn)) phaser.reset();
    if (phaserOn)
        phaser.process (l, r, n, v (FxField::PhaserRate), v (FxField::PhaserDepth), v (FxField::PhaserFeedback),
                        dsp::clamp01 (v (FxField::PhaserMix) + m (ModDest::PhaserMix)));

    const bool flangerOn = on (FxField::FlangerOn);
    if (justEnabled (5, flangerOn)) flanger.reset();
    if (flangerOn)
        flanger.process (l, r, n, v (FxField::FlangerRate), v (FxField::FlangerDepth), v (FxField::FlangerFeedback),
                         dsp::clamp01 (v (FxField::FlangerMix) + m (ModDest::FlangerMix)));

    const bool delayOn = on (FxField::DelayOn);
    if (justEnabled (6, delayOn)) delay.reset();
    if (delayOn)
    {
        const float seconds = p.getBool (pid::fx (FxField::DelaySync))
                                  ? (float) (divisionToBeats (p.getInt (pid::fx (FxField::DelayDivision))) * 60.0 / transport.bpm)
                                  : v (FxField::DelayTime) * 0.001f;
        delay.process (delayL, delayR, l, r, n, seconds * sampleRate,
                       v (FxField::DelayFeedback) + m (ModDest::DelayFeedback), v (FxField::DelayDamping),
                       p.getBool (pid::fx (FxField::DelayPingPong)), v (FxField::DelayReturn) + m (ModDest::DelayMix));
    }

    const bool reverbOn = on (FxField::ReverbOn);
    if (justEnabled (7, reverbOn)) reverb.reset();
    if (reverbOn)
        reverb.process (reverbL, reverbR, l, r, n, v (FxField::ReverbSize) + m (ModDest::ReverbSize), v (FxField::ReverbDamping),
                        v (FxField::ReverbPredelay), v (FxField::ReverbWidth), v (FxField::ReverbReturn) + m (ModDest::ReverbMix));

    const bool eqOn = on (FxField::EqOn);
    if (justEnabled (8, eqOn)) eq.reset();
    if (eqOn)
        eq.process (l, r, n, v (FxField::EqLowGain), v (FxField::EqMidFreq), v (FxField::EqMidGain), v (FxField::EqHighGain));

    const bool compOn = on (FxField::CompOn);
    if (justEnabled (9, compOn)) compressor.reset();
    if (compOn)
        compressor.process (l, r, n, v (FxField::CompThreshold), v (FxField::CompRatio), v (FxField::CompAttack),
                            v (FxField::CompRelease), v (FxField::CompMakeup));
}

} // namespace nedd::fx
