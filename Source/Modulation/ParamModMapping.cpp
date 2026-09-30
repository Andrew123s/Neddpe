#include "ParamModMapping.h"
#include "Parameters/ParameterDefs.h"
#include <unordered_map>

namespace nedd
{
namespace
{
    enum class Mapping { Linear, Cutoff, TimeOctaves, Cents, CrushBits };

    struct Entry { ModDest dest; Mapping mapping; };

    const std::unordered_map<int, Entry>& table()
    {
        static const std::unordered_map<int, Entry> map = []
        {
            std::unordered_map<int, Entry> m;
            auto add = [&m] (int index, ModDest d, Mapping mapping = Mapping::Linear) { m[index] = { d, mapping }; };

            for (int o = 0; o < kNumOscillators; ++o)
            {
                add (pid::osc (o, OscField::Level), oscDest (ModDest::Osc1Level, o));
                add (pid::osc (o, OscField::WtPos), oscDest (ModDest::Osc1WtPos, o));
                add (pid::osc (o, OscField::PulseWidth), oscDest (ModDest::Osc1Pw, o));
                add (pid::osc (o, OscField::FmAmount), oscDest (ModDest::Osc1Fm, o));
                add (pid::osc (o, OscField::Fine), oscDest (ModDest::Osc1Pitch, o), Mapping::Cents);
                add (pid::osc (o, OscField::Detune), ModDest::UnisonDetune);
                add (pid::osc (o, OscField::Spread), ModDest::StereoWidth);
                add (pid::osc (o, OscField::GrainSize), ModDest::GrainSize, Mapping::TimeOctaves);
                add (pid::osc (o, OscField::GrainDensity), ModDest::GrainDensity, Mapping::TimeOctaves);
            }

            add (pid::filter (FilterField::Cutoff), ModDest::FilterCutoff, Mapping::Cutoff);
            add (pid::filter (FilterField::Resonance), ModDest::FilterResonance);
            add (pid::filter (FilterField::Drive), ModDest::FilterDrive);
            add (pid::filter (FilterField::EnvAmount), ModDest::FilterEnvAmount);
            add (pid::filter (FilterField::Mix), ModDest::FilterMix);
            add (pid::amp (AmpField::Level), ModDest::AmpLevel);
            add (pid::amp (AmpField::Pan), ModDest::Pan);
            add (pid::env (pid::ampEnv, EnvField::Attack), ModDest::AmpAttack, Mapping::TimeOctaves);
            add (pid::env (pid::ampEnv, EnvField::Decay), ModDest::AmpDecay, Mapping::TimeOctaves);
            add (pid::env (pid::ampEnv, EnvField::Release), ModDest::AmpRelease, Mapping::TimeOctaves);

            for (int l = 0; l < kNumLfos; ++l)
            {
                add (pid::lfo (l, LfoField::Rate), (ModDest) ((int) ModDest::Lfo1Rate + l), Mapping::TimeOctaves);
                add (pid::lfo (l, LfoField::Amount), (ModDest) ((int) ModDest::Lfo1Depth + l));
            }

            add (pid::fx (FxField::DelaySend), ModDest::DelaySend);
            add (pid::fx (FxField::ReverbSend), ModDest::ReverbSend);
            add (pid::fx (FxField::DistDrive), ModDest::DistDrive);
            add (pid::fx (FxField::DistMix), ModDest::DistMix);
            add (pid::fx (FxField::SatDrive), ModDest::SatDrive);
            add (pid::fx (FxField::CrushBits), ModDest::CrushAmount, Mapping::CrushBits);
            add (pid::fx (FxField::ChorusMix), ModDest::ChorusMix);
            add (pid::fx (FxField::PhaserMix), ModDest::PhaserMix);
            add (pid::fx (FxField::FlangerMix), ModDest::FlangerMix);
            add (pid::fx (FxField::DelayFeedback), ModDest::DelayFeedback);
            add (pid::fx (FxField::DelayReturn), ModDest::DelayMix);
            add (pid::fx (FxField::ReverbSize), ModDest::ReverbSize);
            add (pid::fx (FxField::ReverbReturn), ModDest::ReverbMix);
            add (pid::arp (ArpField::Gate), ModDest::ArpGate);
            add (pid::arp (ArpField::Probability), ModDest::ArpProbability);
            add (pid::global (GlobalField::MorphPosition), ModDest::Morph);
            return m;
        }();
        return map;
    }
} // namespace

ModDest modDestForParam (int paramIndex)
{
    const auto it = table().find (paramIndex);
    return it != table().end() ? it->second.dest : ModDest::None;
}

int paramForModDest (ModDest dest)
{
    int best = -1;
    for (const auto& [index, entry] : table())
        if (entry.dest == dest && (best < 0 || index < best))
            best = index;
    return best;
}

float modulatedNormalised (int paramIndex, float basePlain, float destUnits)
{
    const auto& def = getParamDef (paramIndex);
    const auto it = table().find (paramIndex);
    if (it == table().end())
        return def.range.convertTo0to1 (basePlain);

    const float range = getModDestInfo (it->second.dest).range;
    float plain = basePlain;

    switch (it->second.mapping)
    {
        case Mapping::Linear:      plain = basePlain + destUnits * range; break;
        case Mapping::Cutoff:      plain = basePlain * std::exp2 (destUnits * range / 12.0f); break;
        case Mapping::TimeOctaves: plain = basePlain * std::exp2 (destUnits * range); break;
        case Mapping::Cents:       plain = basePlain + destUnits * range * 100.0f; break;
        case Mapping::CrushBits:   plain = basePlain - destUnits * 14.0f; break;
    }

    plain = juce::jlimit (def.range.start, def.range.end, plain);
    return def.range.convertTo0to1 (plain);
}

} // namespace nedd
