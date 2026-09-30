#include "PatchRandomizer.h"
#include "DSP/Wavetable.h"
#include "Synth/Tempo.h"

namespace nedd
{
namespace
{
    class Dice
    {
    public:
        explicit Dice (uint32_t seed) : random ((juce::int64) seed * 2654435761LL + 17) {}

        float uniform (float lo, float hi) { return lo + (hi - lo) * random.nextFloat(); }
        float logUniform (float lo, float hi) { return lo * std::pow (hi / lo, random.nextFloat()); }
        bool chance (float p) { return random.nextFloat() < p; }
        int integer (int lo, int hi) { return lo + random.nextInt (hi - lo + 1); }
        float gaussian() { return (float) random.nextDouble() * 2.0f - 1.0f + (float) random.nextDouble() * 2.0f - 1.0f + (float) random.nextDouble() * 2.0f - 1.0f; }

        /** Index chosen by weight. */
        int weighted (std::initializer_list<float> weights)
        {
            float total = 0.0f;
            for (float w : weights) total += w;
            float r = random.nextFloat() * total;
            int i = 0;
            for (float w : weights)
            {
                if (r < w) return i;
                r -= w;
                ++i;
            }
            return i - 1;
        }

        template <typename T>
        T pick (std::initializer_list<T> values)
        {
            const int n = (int) values.size();
            return *(values.begin() + random.nextInt (n));
        }

    private:
        juce::Random random;
    };

    void set (PresetState& s, int index, float value)
    {
        const auto& d = getParamDef (index);
        s.params[index] = juce::jlimit (d.range.start, d.range.end, value);
    }

    void randomiseOscillators (PresetState& s, Dice& dice)
    {
        for (int o = 0; o < kNumOscillators; ++o)
        {
            auto p = [o] (OscField f) { return pid::osc (o, f); };
            const bool on = o == 0 || dice.chance (o == 1 ? 0.7f : 0.45f);
            set (s, p (OscField::On), on ? 1.0f : 0.0f);
            if (! on)
                continue;

            const int engine = dice.weighted ({ 0.35f, 0.35f, 0.2f, o > 0 ? 0.1f : 0.0f });
            set (s, p (OscField::Engine), (float) engine);
            set (s, p (OscField::Wave), (float) dice.weighted ({ 0.15f, 0.15f, 0.35f, 0.2f, 0.15f }));
            set (s, p (OscField::PulseWidth), dice.uniform (0.15f, 0.6f));
            set (s, p (OscField::Table), (float) dice.integer (0, getWavetableNames().size() - 1));
            set (s, p (OscField::WtPos), dice.uniform (0.0f, 1.0f));

            const int octave = o == 0 ? dice.pick ({ 0, 0, 0, -1, -1 })
                             : o == 1 ? dice.pick ({ 0, 0, -1, 1 })
                                      : dice.pick ({ -1, -1, 0, 1, -2 });
            set (s, p (OscField::Octave), (float) octave);
            set (s, p (OscField::Semi), (float) dice.pick ({ 0, 0, 0, 0, 0, 7, 12, 5, -5, 3 }));
            set (s, p (OscField::Fine), dice.gaussian() * 5.0f);
            set (s, p (OscField::Level), o == 0 ? dice.uniform (0.6f, 0.8f) : dice.uniform (0.35f, 0.7f));
            set (s, p (OscField::Pan), dice.chance (0.7f) ? 0.0f : dice.uniform (-0.4f, 0.4f));

            const int unison = dice.pick ({ 1, 1, 1, 2, 2, 3, 3, 5, 5, 7 });
            set (s, p (OscField::Unison), (float) unison);
            set (s, p (OscField::Detune), dice.uniform (0.05f, 0.35f));
            set (s, p (OscField::Spread), dice.uniform (0.3f, 1.0f));
            set (s, p (OscField::PhaseRandom), dice.chance (0.3f) ? 1.0f : 0.0f);

            set (s, p (OscField::FmAmount), dice.chance (0.75f) ? 0.0f : dice.uniform (0.05f, 0.35f));
            set (s, p (OscField::Ring), dice.chance (0.9f) ? 0.0f : dice.uniform (0.2f, 0.6f));
            set (s, p (OscField::Sync), o > 0 && dice.chance (0.1f) ? 1.0f : 0.0f);
            set (s, p (OscField::Route), dice.chance (0.9f) ? 0.0f : 1.0f);

            set (s, p (OscField::FmAlgorithm), (float) dice.integer (0, 2));
            set (s, p (OscField::Op1Ratio), (float) dice.pick ({ 2, 3, 3, 4, 5, 6, 1 }));
            set (s, p (OscField::Op2Ratio), (float) dice.pick ({ 2, 3, 4, 8, 1 }));
            set (s, p (OscField::Op1Amount), dice.uniform (0.1f, 0.5f));
            set (s, p (OscField::Op2Amount), dice.chance (0.5f) ? 0.0f : dice.uniform (0.05f, 0.35f));
            set (s, p (OscField::FmFeedback), dice.chance (0.6f) ? 0.0f : dice.uniform (0.05f, 0.35f));
            set (s, p (OscField::FmEnvAmount), dice.uniform (0.0f, 0.8f));
            set (s, p (OscField::NoiseType), (float) dice.integer (0, 4));
            if (engine == (int) OscEngine::Noise)
                set (s, p (OscField::Level), dice.uniform (0.15f, 0.4f));
        }
    }

    void randomiseFilter (PresetState& s, Dice& dice)
    {
        const int type = dice.weighted ({ 0.2f, 0.4f, 0.05f, 0.04f, 0.07f, 0.04f, 0.2f });
        set (s, pid::filter (FilterField::On), 1.0f);
        set (s, pid::filter (FilterField::Type), (float) type);
        set (s, pid::filter (FilterField::Cutoff), dice.logUniform (250.0f, 9000.0f));
        set (s, pid::filter (FilterField::Resonance), dice.uniform (0.0f, type == (int) FilterType::Ladder ? 0.75f : 0.55f));
        set (s, pid::filter (FilterField::Drive), dice.chance (0.5f) ? 0.0f : dice.uniform (0.05f, 0.35f));
        set (s, pid::filter (FilterField::EnvAmount), dice.uniform (-0.1f, 0.6f));
        set (s, pid::filter (FilterField::KeyTrack), dice.uniform (0.2f, 0.7f));
        set (s, pid::filter (FilterField::Velocity), dice.uniform (0.2f, 0.8f));
        set (s, pid::filter (FilterField::Mix), 1.0f);
    }

    void randomiseEnvelopes (PresetState& s, Dice& dice)
    {
        auto envelope = [&] (int e, int style)
        {
            float a, d, su, r;
            switch (style)
            {
                case 0:  a = dice.logUniform (0.001f, 0.01f); d = dice.logUniform (0.15f, 0.8f); su = dice.uniform (0.0f, 0.2f); r = dice.logUniform (0.1f, 0.5f); break;
                case 1:  a = dice.logUniform (0.002f, 0.05f); d = dice.logUniform (0.2f, 1.2f); su = dice.uniform (0.5f, 0.95f); r = dice.logUniform (0.1f, 0.8f); break;
                default: a = dice.logUniform (0.3f, 2.0f); d = dice.logUniform (0.5f, 2.5f); su = dice.uniform (0.6f, 1.0f); r = dice.logUniform (0.8f, 3.0f); break;
            }
            set (s, pid::env (e, EnvField::Attack), a);
            set (s, pid::env (e, EnvField::Decay), d);
            set (s, pid::env (e, EnvField::Sustain), su);
            set (s, pid::env (e, EnvField::Release), r);
            set (s, pid::env (e, EnvField::AttackCurve), dice.uniform (-0.2f, 0.4f));
            set (s, pid::env (e, EnvField::DecayCurve), dice.uniform (0.2f, 0.7f));
            set (s, pid::env (e, EnvField::ReleaseCurve), dice.uniform (0.2f, 0.7f));
        };

        const int style = dice.weighted ({ 0.3f, 0.4f, 0.3f });
        envelope (pid::ampEnv, style);
        envelope (pid::filterEnv, dice.chance (0.6f) ? style : dice.integer (0, 2));
        envelope (pid::modEnv, dice.integer (0, 2));
    }

    /** Clears slots 2..16 (slot 1 keeps MPE Pitch > Pitch). */
    void clearRoutes (PresetState& s)
    {
        for (int slot = 1; slot < kNumModSlots; ++slot)
        {
            set (s, pid::mod (slot, ModSlotField::Source), 0.0f);
            set (s, pid::mod (slot, ModSlotField::Dest), 0.0f);
            set (s, pid::mod (slot, ModSlotField::Amount), 0.0f);
            set (s, pid::mod (slot, ModSlotField::Curve), 0.0f);
        }
        set (s, pid::mod (0, ModSlotField::Source), (float) ModSource::MpePitch);
        set (s, pid::mod (0, ModSlotField::Dest), (float) ModDest::Pitch);
        set (s, pid::mod (0, ModSlotField::Amount), 1.0f);
        set (s, pid::mod (0, ModSlotField::Polarity), (float) ModPolarity::Bipolar);
    }

    int freeSlot (const PresetState& s)
    {
        for (int slot = 1; slot < kNumModSlots; ++slot)
            if (s.params.getInt (pid::mod (slot, ModSlotField::Source)) == 0)
                return slot;
        return -1;
    }

    void addRoute (PresetState& s, ModSource source, ModDest dest, float amount, ModCurve curve = ModCurve::Linear)
    {
        const int slot = freeSlot (s);
        if (slot < 0)
            return;
        set (s, pid::mod (slot, ModSlotField::Source), (float) source);
        set (s, pid::mod (slot, ModSlotField::Dest), (float) dest);
        set (s, pid::mod (slot, ModSlotField::Amount), amount);
        set (s, pid::mod (slot, ModSlotField::Curve), (float) curve);
        set (s, pid::mod (slot, ModSlotField::Polarity), (float) (getModSourceInfo (source).bipolar ? ModPolarity::Bipolar : ModPolarity::Unipolar));
    }

    /** Destinations that actually do something for this patch's oscillator setup. */
    std::vector<ModDest> usefulDestinations (const PresetState& s)
    {
        std::vector<ModDest> d { ModDest::FilterCutoff, ModDest::FilterCutoff, ModDest::FilterResonance, ModDest::Pan,
                                 ModDest::StereoWidth, ModDest::FilterDrive, ModDest::DelaySend, ModDest::ReverbSend };
        for (int o = 0; o < kNumOscillators; ++o)
        {
            if (! s.params.getBool (pid::osc (o, OscField::On)))
                continue;
            const auto engine = s.params.getChoice<OscEngine> (pid::osc (o, OscField::Engine));
            if (engine == OscEngine::Wavetable) { d.push_back (oscDest (ModDest::Osc1WtPos, o)); d.push_back (oscDest (ModDest::Osc1WtPos, o)); }
            if (engine == OscEngine::FM || s.params[pid::osc (o, OscField::FmAmount)] > 0.0f) d.push_back (oscDest (ModDest::Osc1Fm, o));
            if (engine == OscEngine::Analog && s.params.getChoice<AnalogWave> (pid::osc (o, OscField::Wave)) == AnalogWave::Pulse)
                d.push_back (oscDest (ModDest::Osc1Pw, o));
            if (o > 0) d.push_back (oscDest (ModDest::Osc1Level, o));
        }
        if (s.params.getInt (pid::osc (0, OscField::Unison)) > 1) d.push_back (ModDest::UnisonDetune);
        return d;
    }

    void randomiseLfos (PresetState& s, Dice& dice)
    {
        for (int l = 0; l < kNumLfos; ++l)
        {
            set (s, pid::lfo (l, LfoField::Shape), (float) dice.weighted ({ 0.3f, 0.2f, 0.1f, 0.1f, 0.05f, 0.1f, 0.15f, 0.0f }));
            set (s, pid::lfo (l, LfoField::Rate), dice.logUniform (0.08f, 7.0f));
            set (s, pid::lfo (l, LfoField::Sync), dice.chance (0.3f) ? 1.0f : 0.0f);
            set (s, pid::lfo (l, LfoField::Division), (float) dice.pick ({ divisionIndex ("1/4"), divisionIndex ("1/8"), divisionIndex ("1/2"),
                                                                          divisionIndex ("1/1"), divisionIndex ("1/16") }));
            set (s, pid::lfo (l, LfoField::Retrigger), dice.chance (0.6f) ? 1.0f : 0.0f);
            set (s, pid::lfo (l, LfoField::Fade), dice.chance (0.7f) ? 0.0f : dice.uniform (0.1f, 2.0f));
            set (s, pid::lfo (l, LfoField::Amount), 1.0f);
        }
    }

    void randomiseModulation (PresetState& s, Dice& dice, bool includeMpe)
    {
        clearRoutes (s);
        auto dests = usefulDestinations (s);
        auto pickDest = [&] { return dests[(size_t) dice.integer (0, (int) dests.size() - 1)]; };

        if (includeMpe)
        {
            // MPE-first: pressure and slide always get a meaningful per-note job.
            addRoute (s, ModSource::MpePressure, dice.chance (0.6f) ? ModDest::FilterCutoff : pickDest(), dice.uniform (0.2f, 0.5f));
            addRoute (s, ModSource::MpeSlide, pickDest(), dice.uniform (0.2f, 0.7f));
            if (dice.chance (0.5f))
                addRoute (s, ModSource::Velocity, pickDest(), dice.uniform (0.15f, 0.4f));
        }

        const int extra = dice.integer (1, 3);
        for (int i = 0; i < extra; ++i)
        {
            const auto source = dice.pick ({ ModSource::Lfo1, ModSource::Lfo2, ModSource::ModEnv, ModSource::ModWheel, ModSource::Random,
                                             ModSource::KeyPosition, ModSource::Lfo3 });
            const bool subtle = source == ModSource::Lfo1 || source == ModSource::Lfo2 || source == ModSource::Lfo3;
            addRoute (s, source, pickDest(), (dice.chance (0.25f) ? -1.0f : 1.0f) * dice.uniform (0.08f, subtle ? 0.25f : 0.45f));
        }

        // Macros: always routed so they are useful straight away.
        addRoute (s, ModSource::Macro1, pickDest(), 0.5f);
        addRoute (s, ModSource::Macro2, ModDest::FilterCutoff, 0.4f);
        addRoute (s, ModSource::Macro3, ModDest::ReverbSend, 0.6f);
        addRoute (s, ModSource::Macro4, ModDest::FilterDrive, 0.7f);
        s.macroNames = { "MOVEMENT", "TONE", "SPACE", "DRIVE" };
    }

    void randomiseMpeResponse (PresetState& s, Dice& dice)
    {
        set (s, pid::global (GlobalField::PressureCurve), dice.uniform (-0.3f, 0.4f));
        set (s, pid::global (GlobalField::SlideCurve), dice.uniform (-0.3f, 0.3f));
        set (s, pid::amp (AmpField::Pressure), dice.chance (0.6f) ? 0.0f : dice.uniform (0.2f, 0.6f));
        set (s, pid::amp (AmpField::Velocity), dice.uniform (0.3f, 0.8f));
    }

    void randomiseEffects (PresetState& s, Dice& dice)
    {
        for (auto f : { FxField::DistOn, FxField::SatOn, FxField::CrushOn, FxField::ChorusOn, FxField::PhaserOn, FxField::FlangerOn,
                        FxField::DelayOn, FxField::ReverbOn, FxField::EqOn, FxField::CompOn })
            set (s, pid::fx (f), 0.0f);

        const int count = dice.integer (1, 3);
        for (int i = 0; i < count; ++i)
        {
            switch (dice.weighted ({ 0.3f, 0.25f, 0.12f, 0.08f, 0.08f, 0.08f, 0.05f, 0.04f }))
            {
                case 0:
                    set (s, pid::fx (FxField::ReverbOn), 1.0f);
                    set (s, pid::fx (FxField::ReverbSize), dice.uniform (0.3f, 0.9f));
                    set (s, pid::fx (FxField::ReverbSend), dice.uniform (0.15f, 0.45f));
                    set (s, pid::fx (FxField::ReverbDamping), dice.uniform (0.2f, 0.7f));
                    break;
                case 1:
                    set (s, pid::fx (FxField::DelayOn), 1.0f);
                    set (s, pid::fx (FxField::DelaySync), 1.0f);
                    set (s, pid::fx (FxField::DelayDivision), (float) dice.pick ({ divisionIndex ("1/8."), divisionIndex ("1/4"), divisionIndex ("1/8"), divisionIndex ("1/4.") }));
                    set (s, pid::fx (FxField::DelayFeedback), dice.uniform (0.2f, 0.5f));
                    set (s, pid::fx (FxField::DelaySend), dice.uniform (0.1f, 0.35f));
                    break;
                case 2: set (s, pid::fx (FxField::ChorusOn), 1.0f); set (s, pid::fx (FxField::ChorusMix), dice.uniform (0.2f, 0.5f)); break;
                case 3: set (s, pid::fx (FxField::PhaserOn), 1.0f); set (s, pid::fx (FxField::PhaserMix), dice.uniform (0.2f, 0.5f)); break;
                case 4: set (s, pid::fx (FxField::DistOn), 1.0f); set (s, pid::fx (FxField::DistDrive), dice.uniform (0.1f, 0.4f)); set (s, pid::fx (FxField::DistMix), dice.uniform (0.3f, 0.8f)); break;
                case 5: set (s, pid::fx (FxField::SatOn), 1.0f); set (s, pid::fx (FxField::SatDrive), dice.uniform (0.1f, 0.5f)); break;
                case 6: set (s, pid::fx (FxField::CrushOn), 1.0f); set (s, pid::fx (FxField::CrushBits), dice.uniform (7.0f, 13.0f)); set (s, pid::fx (FxField::CrushMix), 0.5f); break;
                default: set (s, pid::fx (FxField::FlangerOn), 1.0f); set (s, pid::fx (FxField::FlangerMix), dice.uniform (0.2f, 0.5f)); break;
            }
        }
    }

    void randomiseTexture (PresetState& s, Dice& dice)
    {
        for (int o = 0; o < kNumOscillators; ++o)
        {
            set (s, pid::osc (o, OscField::WtPos), dice.uniform (0.0f, 1.0f));
            set (s, pid::osc (o, OscField::Detune), dice.uniform (0.05f, 0.4f));
            set (s, pid::osc (o, OscField::Spread), dice.uniform (0.3f, 1.0f));
        }
        if (dice.chance (0.4f) && ! s.params.getBool (pid::osc (2, OscField::On)))
        {
            set (s, pid::osc (2, OscField::On), 1.0f);
            set (s, pid::osc (2, OscField::Engine), (float) OscEngine::Noise);
            set (s, pid::osc (2, OscField::NoiseType), (float) dice.integer (0, 4));
            set (s, pid::osc (2, OscField::Level), dice.uniform (0.1f, 0.3f));
        }
        randomiseLfos (s, dice);
        set (s, pid::fx (FxField::ChorusOn), dice.chance (0.5f) ? 1.0f : 0.0f);
        set (s, pid::fx (FxField::PhaserOn), dice.chance (0.3f) ? 1.0f : 0.0f);
    }

    /** Keeps generated patches audible and sane. */
    void safetyPass (PresetState& s)
    {
        set (s, pid::osc (0, OscField::On), 1.0f);
        if (s.params[pid::osc (0, OscField::Level)] < 0.4f)
            set (s, pid::osc (0, OscField::Level), 0.6f);
        set (s, pid::amp (AmpField::Level), juce::jlimit (0.35f, 0.75f, s.params[pid::amp (AmpField::Level)]));
        // Very high resonance on a very low cutoff is rarely wanted from a random roll.
        if (s.params[pid::filter (FilterField::Cutoff)] < 300.0f)
            set (s, pid::filter (FilterField::Resonance), std::min (s.params[pid::filter (FilterField::Resonance)], 0.5f));
        set (s, pid::fx (FxField::LimiterOn), 1.0f);
    }
} // namespace

juce::StringArray PatchRandomizer::getModeNames()
{
    return { "Full", "Oscillators", "Filter", "Modulation", "MPE", "Effects", "Texture" };
}

PresetState PatchRandomizer::randomise (const PresetState& current, Mode mode, uint32_t seed)
{
    PresetState s = current;
    Dice dice (seed);

    switch (mode)
    {
        case Mode::Full:
            randomiseOscillators (s, dice);
            randomiseFilter (s, dice);
            randomiseEnvelopes (s, dice);
            randomiseLfos (s, dice);
            randomiseModulation (s, dice, true);
            randomiseMpeResponse (s, dice);
            randomiseEffects (s, dice);
            set (s, pid::amp (AmpField::Level), 0.55f);
            break;
        case Mode::Oscillators: randomiseOscillators (s, dice); break;
        case Mode::Filter:      randomiseFilter (s, dice); break;
        case Mode::Modulation:  randomiseLfos (s, dice); randomiseModulation (s, dice, true); break;
        case Mode::Mpe:
        {
            randomiseMpeResponse (s, dice);
            // Replace only the MPE routes, keep the rest of the matrix.
            for (int slot = 1; slot < kNumModSlots; ++slot)
            {
                const auto src = s.params.getChoice<ModSource> (pid::mod (slot, ModSlotField::Source));
                if (src == ModSource::MpePressure || src == ModSource::MpeSlide || src == ModSource::Velocity)
                    set (s, pid::mod (slot, ModSlotField::Source), 0.0f);
            }
            auto dests = usefulDestinations (s);
            addRoute (s, ModSource::MpePressure, dests[(size_t) dice.integer (0, (int) dests.size() - 1)], dice.uniform (0.2f, 0.5f));
            addRoute (s, ModSource::MpeSlide, dests[(size_t) dice.integer (0, (int) dests.size() - 1)], dice.uniform (0.2f, 0.7f));
            addRoute (s, ModSource::Velocity, dests[(size_t) dice.integer (0, (int) dests.size() - 1)], dice.uniform (0.1f, 0.4f));
            break;
        }
        case Mode::Effects: randomiseEffects (s, dice); break;
        case Mode::Texture: randomiseTexture (s, dice); break;
    }

    safetyPass (s);
    s.name = "Random " + getModeNames()[(int) mode];
    s.category = "User";
    s.hasMorphTarget = current.hasMorphTarget;
    return s;
}

PresetState PatchRandomizer::mutate (const PresetState& current, float amount, uint32_t seed)
{
    PresetState s = current;
    Dice dice (seed);
    amount = juce::jlimit (0.0f, 1.0f, amount);

    for (const auto& d : getParamDefs())
    {
        if (! d.morphable)
            continue;

        if (d.type == ParamType::Float)
        {
            const float n = d.range.convertTo0to1 (s.params[d.index]) + dice.gaussian() * 0.06f * amount;
            s.params[d.index] = d.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
        }
        else if (dice.chance (0.04f * amount) && d.id != "osc1_on" && d.id != "flt_on")
        {
            s.params[d.index] = (float) dice.integer ((int) d.range.start, (int) d.range.end);
        }
    }

    // Nudge the amounts of existing routes, never their existence.
    for (int slot = 1; slot < kNumModSlots; ++slot)
        if (s.params.getInt (pid::mod (slot, ModSlotField::Source)) != 0)
            set (s, pid::mod (slot, ModSlotField::Amount), s.params[pid::mod (slot, ModSlotField::Amount)] + dice.gaussian() * 0.08f * amount);

    safetyPass (s);
    return s;
}

} // namespace nedd
