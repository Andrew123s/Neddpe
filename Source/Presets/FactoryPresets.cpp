#include "FactoryPresets.h"
#include "Synth/Tempo.h"

namespace nedd
{
namespace
{
    /** Small fluent helper so every recipe reads like a patch sheet. */
    class Build
    {
    public:
        explicit Build (PresetState& st) : s (st)
        {
            // Every factory sound starts from the init patch with a clean matrix; slot 1 keeps
            // MPE Pitch > Pitch at 100% so per-note bends always work.
            for (int slot = 1; slot < kNumModSlots; ++slot)
            {
                s.set (pid::mod (slot, ModSlotField::Source), 0.0f);
                s.set (pid::mod (slot, ModSlotField::Dest), 0.0f);
                s.set (pid::mod (slot, ModSlotField::Amount), 0.0f);
            }
            s.set (pid::osc (0, OscField::On), 0.0f);
            s.set (pid::fx (FxField::ReverbSend), 0.2f);
            s.set (pid::fx (FxField::DelaySend), 0.2f);
        }

        Build& analog (int o, AnalogWave w, float level, int octave = 0, int semi = 0, float fine = 0.0f)
        {
            osc (o, OscField::On, 1.0f).osc (o, OscField::Engine, (float) OscEngine::Analog).osc (o, OscField::Wave, (float) w);
            return tune (o, level, octave, semi, fine);
        }

        Build& wavetable (int o, int table, float position, float level, int octave = 0, int semi = 0, float fine = 0.0f)
        {
            osc (o, OscField::On, 1.0f).osc (o, OscField::Engine, (float) OscEngine::Wavetable).osc (o, OscField::Table, (float) table)
                .osc (o, OscField::WtPos, position);
            return tune (o, level, octave, semi, fine);
        }

        /** ratio indices: 0 = x0.25, 1 = x0.5, 2 = x1, 3 = x2, 4 = x3 ... */
        Build& fm (int o, FmAlgorithm algo, int ratio1, int ratio2, float amount1, float amount2, float feedback, float envAmount,
                   float level, int octave = 0)
        {
            osc (o, OscField::On, 1.0f).osc (o, OscField::Engine, (float) OscEngine::FM).osc (o, OscField::FmAlgorithm, (float) algo)
                .osc (o, OscField::Op1Ratio, (float) ratio1).osc (o, OscField::Op2Ratio, (float) ratio2)
                .osc (o, OscField::Op1Amount, amount1).osc (o, OscField::Op2Amount, amount2)
                .osc (o, OscField::FmFeedback, feedback).osc (o, OscField::FmEnvAmount, envAmount);
            return tune (o, level, octave, 0, 0.0f);
        }

        /** Granular engine on the oscillator's sample (the built-in source unless one is imported). */
        Build& granular (int o, float position, float size, float density, float spray, float pitchSpray, float level, int octave = 0)
        {
            osc (o, OscField::On, 1.0f).osc (o, OscField::Engine, (float) OscEngine::Granular).osc (o, OscField::WtPos, position)
                .osc (o, OscField::GrainSize, size).osc (o, OscField::GrainDensity, density)
                .osc (o, OscField::GrainSpray, spray).osc (o, OscField::GrainPitchSpray, pitchSpray);
            return tune (o, level, octave, 0, 0.0f);
        }

        Build& noise (int o, NoiseType type, float level, float spread = 0.6f)
        {
            osc (o, OscField::On, 1.0f).osc (o, OscField::Engine, (float) OscEngine::Noise).osc (o, OscField::NoiseType, (float) type)
                .osc (o, OscField::Spread, spread);
            return osc (o, OscField::Level, level);
        }

        Build& unison (int o, int voices, float detune, float spread)
        {
            return osc (o, OscField::Unison, (float) voices).osc (o, OscField::Detune, detune).osc (o, OscField::Spread, spread);
        }

        Build& osc (int o, OscField f, float v) { s.set (pid::osc (o, f), v); return *this; }

        Build& filter (FilterType type, float cutoff, float resonance, float envAmount, float keyTrack = 0.3f, float drive = 0.0f)
        {
            s.set (pid::filter (FilterField::On), 1.0f);
            s.set (pid::filter (FilterField::Type), (float) type);
            s.set (pid::filter (FilterField::Cutoff), cutoff);
            s.set (pid::filter (FilterField::Resonance), resonance);
            s.set (pid::filter (FilterField::EnvAmount), envAmount);
            s.set (pid::filter (FilterField::KeyTrack), keyTrack);
            s.set (pid::filter (FilterField::Drive), drive);
            return *this;
        }

        Build& flt (FilterField f, float v) { s.set (pid::filter (f), v); return *this; }

        Build& env (int e, float a, float d, float sustain, float r, float attackCurve = 0.2f, float decayCurve = 0.5f)
        {
            s.set (pid::env (e, EnvField::Attack), a);
            s.set (pid::env (e, EnvField::Decay), d);
            s.set (pid::env (e, EnvField::Sustain), sustain);
            s.set (pid::env (e, EnvField::Release), r);
            s.set (pid::env (e, EnvField::AttackCurve), attackCurve);
            s.set (pid::env (e, EnvField::DecayCurve), decayCurve);
            return *this;
        }

        Build& lfo (int l, LfoShape shape, float rate, float amount = 1.0f, bool retrigger = true)
        {
            s.set (pid::lfo (l, LfoField::Shape), (float) shape);
            s.set (pid::lfo (l, LfoField::Rate), rate);
            s.set (pid::lfo (l, LfoField::Amount), amount);
            s.set (pid::lfo (l, LfoField::Retrigger), retrigger ? 1.0f : 0.0f);
            s.set (pid::lfo (l, LfoField::Sync), 0.0f);
            return *this;
        }

        Build& lfoSync (int l, LfoShape shape, const char* division, bool retrigger = false)
        {
            lfo (l, shape, 1.0f, 1.0f, retrigger);
            s.set (pid::lfo (l, LfoField::Sync), 1.0f);
            s.set (pid::lfo (l, LfoField::Division), (float) divisionIndex (division));
            return *this;
        }

        Build& lfoField (int l, LfoField f, float v) { s.set (pid::lfo (l, f), v); return *this; }

        Build& route (ModSource source, ModDest dest, float amount, ModCurve curve = ModCurve::Linear)
        {
            if (nextSlot >= kNumModSlots)
            {
                jassertfalse;
                return *this;
            }
            const bool bipolar = getModSourceInfo (source).bipolar;
            s.set (pid::mod (nextSlot, ModSlotField::Source), (float) source);
            s.set (pid::mod (nextSlot, ModSlotField::Dest), (float) dest);
            s.set (pid::mod (nextSlot, ModSlotField::Amount), amount);
            s.set (pid::mod (nextSlot, ModSlotField::Curve), (float) curve);
            s.set (pid::mod (nextSlot, ModSlotField::Polarity), (float) (bipolar ? ModPolarity::Bipolar : ModPolarity::Unipolar));
            ++nextSlot;
            return *this;
        }

        /** Standard performance macros: MOVEMENT (preset specific), TONE, SPACE, DRIVE. */
        Build& macros (ModDest movement, float movementAmount, const char* movementName = "MOVEMENT")
        {
            s.macroNames = { movementName, "TONE", "SPACE", "DRIVE" };
            route (ModSource::Macro1, movement, movementAmount);
            route (ModSource::Macro2, ModDest::FilterCutoff, 0.45f);
            route (ModSource::Macro3, ModDest::ReverbSend, 0.7f);
            route (ModSource::Macro4, ModDest::FilterDrive, 0.8f);
            return *this;
        }

        Build& fx (FxField f, float v) { s.set (pid::fx (f), v); return *this; }
        Build& reverb (float size, float send, float ret = 0.5f) { return fx (FxField::ReverbOn, 1.0f).fx (FxField::ReverbSize, size).fx (FxField::ReverbSend, send).fx (FxField::ReverbReturn, ret); }
        Build& delay (const char* division, float feedback, float send, bool pingPong = true)
        {
            return fx (FxField::DelayOn, 1.0f).fx (FxField::DelaySync, 1.0f).fx (FxField::DelayDivision, (float) divisionIndex (division))
                .fx (FxField::DelayFeedback, feedback).fx (FxField::DelaySend, send).fx (FxField::DelayPingPong, pingPong ? 1.0f : 0.0f);
        }
        Build& chorus (float mix, float depth = 0.5f, float rate = 0.6f) { return fx (FxField::ChorusOn, 1.0f).fx (FxField::ChorusMix, mix).fx (FxField::ChorusDepth, depth).fx (FxField::ChorusRate, rate); }

        Build& g (GlobalField f, float v) { s.set (pid::global (f), v); return *this; }
        Build& amp (float level, float velocity = 0.6f, float pressure = 0.0f)
        {
            s.set (pid::amp (AmpField::Level), level);
            s.set (pid::amp (AmpField::Velocity), velocity);
            s.set (pid::amp (AmpField::Pressure), pressure);
            return *this;
        }

        Build& arp (ArpField f, float v) { s.set (pid::arp (f), v); return *this; }
        Build& seq (SeqField f, float v) { s.set (pid::seq (f), v); return *this; }

    private:
        Build& tune (int o, float level, int octave, int semi, float fine)
        {
            return osc (o, OscField::Level, level).osc (o, OscField::Octave, (float) octave).osc (o, OscField::Semi, (float) semi)
                .osc (o, OscField::Fine, fine);
        }

        PresetState& s;
        int nextSlot = 1;
    };

    using W = AnalogWave;
    using F = FilterType;
    using S = ModSource;
    using D = ModDest;

    std::vector<FactoryPreset> buildLibrary()
    {
        std::vector<FactoryPreset> p;

        // ------------------------------------------------------------------ LEADS
        p.push_back ({ "Nedd Lead", "Leads", "Warm unison lead. Pressure opens the filter, slide adds bite, glide on legato.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.7f).unison (0, 5, 0.22f, 0.5f)
                .analog (1, W::Square, 0.45f, 0, 0, 7.0f)
                .filter (F::LowPass24, 1800.0f, 0.3f, 0.35f, 0.5f)
                .env (pid::ampEnv, 0.006f, 0.4f, 0.85f, 0.25f).env (pid::filterEnv, 0.002f, 0.5f, 0.3f, 0.3f)
                .g (GlobalField::GlideMode, (float) GlideMode::Legato).g (GlobalField::Glide, 0.07f)
                .route (S::MpePressure, D::FilterCutoff, 0.4f).route (S::MpeSlide, D::FilterResonance, 0.35f)
                .route (S::ModWheel, D::Lfo1Depth, 0.6f).route (S::Lfo1, D::Pitch, 0.006f)
                .lfo (0, LfoShape::Sine, 5.5f, 0.0f).macros (D::UnisonDetune, 0.5f)
                .delay ("1/8.", 0.35f, 0.25f).reverb (0.45f, 0.18f).amp (0.62f); } });

        p.push_back ({ "Glass Whistle", "Leads", "Pure FM whistle. Pressure swells level and brightness; mod wheel adds vibrato.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Stack, 3, 2, 0.28f, 0.0f, 0.1f, 0.4f, 0.8f)
                .filter (F::LowPass12, 9000.0f, 0.05f, 0.0f, 0.2f)
                .env (pid::ampEnv, 0.03f, 0.5f, 0.9f, 0.4f).env (pid::modEnv, 0.001f, 0.8f, 0.4f, 0.5f)
                .route (S::MpePressure, D::AmpLevel, 0.35f).route (S::MpePressure, D::Osc1Fm, 0.35f)
                .route (S::ModWheel, D::Lfo1Depth, 1.0f).route (S::Lfo1, D::Pitch, 0.008f)
                .lfo (0, LfoShape::Sine, 5.2f, 0.0f).macros (D::Osc1Fm, 0.6f, "SHIMMER")
                .reverb (0.6f, 0.35f).amp (0.55f, 0.4f, 0.4f); } });

        p.push_back ({ "Sync Scream", "Leads", "Hard-sync lead: slide sweeps the synced oscillator, pressure drives the distortion.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.0f)
                .analog (1, W::Saw, 0.75f).osc (1, OscField::Sync, 1.0f).osc (1, OscField::Semi, 7.0f)
                .filter (F::LowPass24, 5000.0f, 0.2f, 0.2f, 0.4f)
                .env (pid::ampEnv, 0.003f, 0.3f, 0.9f, 0.2f)
                .route (S::MpeSlide, D::Osc2Pitch, 0.4f).route (S::MpePressure, D::DistDrive, 0.5f)
                .route (S::MpePressure, D::FilterCutoff, 0.25f).route (S::ModEnv, D::Osc2Pitch, 0.25f)
                .env (pid::modEnv, 0.001f, 0.35f, 0.0f, 0.2f).macros (D::Osc2Pitch, 0.3f, "SWEEP")
                .fx (FxField::DistOn, 1.0f).fx (FxField::DistType, (float) DistortionType::Tube).fx (FxField::DistDrive, 0.3f).fx (FxField::DistMix, 0.7f)
                .delay ("1/8", 0.3f, 0.2f).amp (0.55f); } });

        p.push_back ({ "Expressive Brass", "Leads", "Brassy saws: velocity sets the filter blat, pressure crescendos, slide darkens.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.7f).unison (0, 3, 0.12f, 0.4f)
                .analog (1, W::Saw, 0.5f, 0, 0, -8.0f)
                .filter (F::LowPass24, 700.0f, 0.15f, 0.45f, 0.6f).flt (FilterField::Velocity, 0.8f)
                .env (pid::ampEnv, 0.04f, 0.6f, 0.8f, 0.3f).env (pid::filterEnv, 0.06f, 0.5f, 0.35f, 0.4f)
                .route (S::MpePressure, D::FilterCutoff, 0.35f).route (S::MpeSlide, D::FilterCutoff, -0.2f)
                .macros (D::FilterEnvAmount, 0.4f, "BLAT").chorus (0.25f).reverb (0.5f, 0.25f).amp (0.55f, 0.7f, 0.5f); } });

        // ------------------------------------------------------------------ BASS
        p.push_back ({ "Sub Pressure", "Bass", "Mono sub with a square edge. Pressure opens the low-pass per note.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Sine, 0.85f, -1).analog (1, W::Square, 0.3f, -1)
                .filter (F::LowPass24, 280.0f, 0.2f, 0.25f, 0.2f)
                .env (pid::ampEnv, 0.002f, 0.3f, 0.9f, 0.12f)
                .g (GlobalField::VoiceMode, (float) VoiceMode::Legato).g (GlobalField::GlideMode, (float) GlideMode::Legato).g (GlobalField::Glide, 0.05f)
                .route (S::MpePressure, D::FilterCutoff, 0.45f).route (S::MpeSlide, D::Osc2Level, 0.4f)
                .macros (D::Osc2Level, 0.5f, "EDGE").amp (0.72f, 0.3f); } });

        p.push_back ({ "Growl Bass", "Bass", "Wavetable growl: slide scans the table, pressure adds drive, all per note.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 4, 0.2f, 0.8f, -1).unison (0, 2, 0.08f, 0.2f)
                .analog (1, W::Sine, 0.5f, -2)
                .filter (F::Ladder, 900.0f, 0.35f, 0.3f, 0.3f, 0.3f)
                .env (pid::ampEnv, 0.002f, 0.4f, 0.8f, 0.15f).env (pid::filterEnv, 0.001f, 0.25f, 0.2f, 0.2f)
                .route (S::MpeSlide, D::Osc1WtPos, 0.8f).route (S::MpePressure, D::FilterDrive, 0.6f)
                .route (S::MpePressure, D::FilterCutoff, 0.3f).route (S::Lfo1, D::Osc1WtPos, 0.15f)
                .lfoSync (0, LfoShape::Triangle, "1/8", true).macros (D::Osc1WtPos, 0.6f, "GROWL")
                .fx (FxField::DistOn, 1.0f).fx (FxField::DistDrive, 0.25f).fx (FxField::DistMix, 0.5f).amp (0.62f, 0.4f); } });

        p.push_back ({ "808 Glide", "Bass", "Punchy 808: pitch drop from the mod envelope, saturated, long decay, mono glide.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Sine, 0.95f, -1)
                .filter (F::LowPass12, 2500.0f, 0.0f, 0.0f, 0.0f).flt (FilterField::On, 0.0f)
                .env (pid::ampEnv, 0.001f, 1.8f, 0.0f, 0.5f, 0.0f, 0.3f).env (pid::modEnv, 0.0f, 0.08f, 0.0f, 0.05f, 0.0f, 0.8f)
                .route (S::ModEnv, D::Pitch, 0.25f)
                .g (GlobalField::VoiceMode, (float) VoiceMode::Mono).g (GlobalField::GlideMode, (float) GlideMode::Legato).g (GlobalField::Glide, 0.12f)
                .fx (FxField::SatOn, 1.0f).fx (FxField::SatDrive, 0.45f).fx (FxField::SatWarmth, 0.5f)
                .macros (D::AmpDecay, 0.5f, "LENGTH").amp (0.7f, 0.4f); } });

        p.push_back ({ "Acid Line", "Bass", "Resonant ladder acid with an expressive 16-step pattern (runs with the host transport).",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.8f, -1)
                .filter (F::Ladder, 420.0f, 0.78f, 0.55f, 0.4f, 0.2f).flt (FilterField::Velocity, 0.7f)
                .env (pid::ampEnv, 0.001f, 0.3f, 0.7f, 0.08f).env (pid::filterEnv, 0.001f, 0.22f, 0.0f, 0.1f)
                .route (S::MpePressure, D::FilterResonance, 0.2f).route (S::MpeSlide, D::FilterCutoff, 0.35f)
                .seq (SeqField::On, 1.0f).seq (SeqField::Clock, (float) SeqClock::FollowHost).seq (SeqField::Transpose, -12.0f)
                .macros (D::FilterEnvAmount, 0.4f, "SQUELCH").delay ("1/8.", 0.35f, 0.15f)
                .fx (FxField::DistOn, 1.0f).fx (FxField::DistDrive, 0.2f).fx (FxField::DistMix, 0.6f).amp (0.55f, 0.5f); } });

        // ------------------------------------------------------------------ PADS
        p.push_back ({ "Slide Morph Pad", "Pads", "Two wavetables that each note scans with slide. Pressure breathes the level.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 3, 0.1f, 0.6f).unison (0, 3, 0.15f, 0.8f)
                .wavetable (1, 2, 0.3f, 0.45f, 0, 12).unison (1, 2, 0.2f, 0.9f)
                .filter (F::LowPass12, 3500.0f, 0.1f, 0.1f, 0.3f)
                .env (pid::ampEnv, 0.9f, 1.5f, 0.85f, 1.8f).env (pid::filterEnv, 0.8f, 2.0f, 0.5f, 1.5f)
                .route (S::MpeSlide, D::Osc1WtPos, 0.85f).route (S::MpeSlide, D::Osc2WtPos, 0.6f)
                .route (S::MpePressure, D::FilterCutoff, 0.3f).route (S::Lfo1, D::Osc2WtPos, 0.1f)
                .lfo (0, LfoShape::SmoothRandom, 0.2f, 1.0f, false).macros (D::Osc1WtPos, 0.5f)
                .chorus (0.35f).reverb (0.75f, 0.4f, 0.6f).amp (0.5f, 0.3f, 0.45f); } });

        p.push_back ({ "Warm Analog Pad", "Pads", "Detuned saws through a slow-moving low-pass; long, wide and soft.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.55f).unison (0, 6, 0.3f, 0.9f)
                .analog (1, W::Saw, 0.4f, -1).unison (1, 2, 0.1f, 0.5f)
                .filter (F::LowPass12, 1200.0f, 0.15f, 0.2f, 0.3f)
                .env (pid::ampEnv, 0.7f, 1.0f, 0.9f, 1.6f).env (pid::filterEnv, 1.0f, 2.0f, 0.6f, 1.5f)
                .route (S::Lfo1, D::FilterCutoff, 0.1f).route (S::MpePressure, D::FilterCutoff, 0.25f)
                .route (S::MpeSlide, D::StereoWidth, 0.3f)
                .lfo (0, LfoShape::Sine, 0.15f, 1.0f, false).macros (D::FilterCutoff, 0.3f, "OPEN")
                .reverb (0.8f, 0.45f, 0.6f).amp (0.45f, 0.3f); } });

        p.push_back ({ "Aurora", "Pads", "Glassy FM pad with slowly shimmering modulation depth and a phaser halo.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Parallel, 4, 3, 0.3f, 0.15f, 0.05f, 0.0f, 0.55f).unison (0, 3, 0.1f, 0.8f)
                .analog (1, W::Triangle, 0.4f, -1)
                .filter (F::LowPass12, 6000.0f, 0.0f, 0.0f, 0.2f)
                .env (pid::ampEnv, 1.2f, 1.5f, 0.9f, 2.5f)
                .route (S::Lfo1, D::Osc1Fm, 0.25f).route (S::MpePressure, D::Osc1Fm, 0.4f).route (S::MpeSlide, D::FilterCutoff, 0.2f)
                .lfo (0, LfoShape::Triangle, 0.12f, 1.0f, false).macros (D::Osc1Fm, 0.5f, "SHIMMER")
                .fx (FxField::PhaserOn, 1.0f).fx (FxField::PhaserMix, 0.35f).fx (FxField::PhaserRate, 0.15f)
                .reverb (0.85f, 0.5f, 0.65f).amp (0.5f, 0.3f, 0.2f); } });

        p.push_back ({ "Pressure Strings", "Pads", "Synth strings that only swell as you press: each note's level follows its pressure.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.6f).unison (0, 7, 0.35f, 1.0f)
                .analog (1, W::Saw, 0.35f, 1).unison (1, 3, 0.2f, 0.8f)
                .filter (F::LowPass24, 2600.0f, 0.1f, 0.0f, 0.5f)
                .env (pid::ampEnv, 0.35f, 0.8f, 1.0f, 0.9f)
                .route (S::MpePressure, D::FilterCutoff, 0.3f).route (S::MpeSlide, D::Lfo1Depth, 0.8f).route (S::Lfo1, D::Pitch, 0.004f)
                .lfo (0, LfoShape::Sine, 5.0f, 0.0f).macros (D::UnisonDetune, 0.4f, "ENSEMBLE")
                .chorus (0.4f, 0.6f).reverb (0.75f, 0.4f).amp (0.55f, 0.2f, 0.85f); } });

        // ------------------------------------------------------------------ PLUCKS
        p.push_back ({ "Nedd Pluck", "Plucks", "Snappy saw/square pluck with ping-pong delay; pressure after the attack re-opens it.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.65f).analog (1, W::Square, 0.4f, 1)
                .filter (F::LowPass24, 350.0f, 0.25f, 0.62f, 0.5f)
                .env (pid::ampEnv, 0.001f, 0.6f, 0.0f, 0.35f).env (pid::filterEnv, 0.0f, 0.22f, 0.0f, 0.2f, 0.0f, 0.7f)
                .route (S::MpePressure, D::FilterCutoff, 0.4f).route (S::Velocity, D::FilterEnvAmount, 0.3f)
                .macros (D::AmpDecay, 0.5f, "LENGTH").delay ("1/8.", 0.4f, 0.3f).reverb (0.5f, 0.2f).amp (0.65f, 0.7f); } });

        p.push_back ({ "Glass Pluck", "Plucks", "Bell-like FM pluck; velocity sets how glassy the attack is.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Stack, 5, 2, 0.3f, 0.1f, 0.0f, 1.0f, 0.8f)
                .env (pid::ampEnv, 0.001f, 1.2f, 0.0f, 0.6f, 0.0f, 0.6f).env (pid::modEnv, 0.0f, 0.35f, 0.0f, 0.2f, 0.0f, 0.7f)
                .flt (FilterField::On, 0.0f)
                .route (S::Velocity, D::Osc1Fm, 0.6f).route (S::MpeSlide, D::Osc1Fm, 0.3f)
                .macros (D::Osc1Fm, 0.5f, "GLASS").reverb (0.6f, 0.3f).delay ("1/4", 0.25f, 0.15f).amp (0.55f, 0.8f); } });

        p.push_back ({ "Digital Pluck", "Plucks", "Stepped wavetable swept by an envelope, lightly crushed.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 6, 0.0f, 0.8f)
                .filter (F::LowPass12, 8000.0f, 0.1f, 0.0f, 0.3f)
                .env (pid::ampEnv, 0.001f, 0.45f, 0.0f, 0.25f).env (pid::modEnv, 0.0f, 0.25f, 0.0f, 0.2f)
                .route (S::ModEnv, D::Osc1WtPos, 0.9f).route (S::MpeSlide, D::Osc1WtPos, 0.5f).route (S::MpePressure, D::CrushAmount, 0.4f)
                .fx (FxField::CrushOn, 1.0f).fx (FxField::CrushBits, 12.0f).fx (FxField::CrushDownsample, 2.0f).fx (FxField::CrushMix, 0.5f)
                .macros (D::Osc1WtPos, 0.5f).delay ("1/16", 0.3f, 0.2f).amp (0.55f, 0.7f); } });

        // ------------------------------------------------------------------ KEYS
        p.push_back ({ "Tine Keys", "Keys", "Electric-piano FM: velocity and pressure bring out the bark, key tracking keeps it sweet.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Branch, 2, 11, 0.32f, 0.12f, 0.0f, 0.85f, 0.75f).osc (0, OscField::FmKeyTrack, 0.5f)
                .env (pid::ampEnv, 0.001f, 2.5f, 0.0f, 0.4f, 0.0f, 0.6f).env (pid::modEnv, 0.0f, 0.9f, 0.15f, 0.4f)
                .flt (FilterField::On, 0.0f)
                .route (S::Velocity, D::Osc1Fm, 0.5f).route (S::MpePressure, D::Osc1Fm, 0.3f).route (S::Lfo1, D::Pan, 0.25f)
                .lfo (0, LfoShape::Sine, 3.5f, 1.0f, false).macros (D::Osc1Fm, 0.4f, "BARK")
                .chorus (0.3f, 0.4f).reverb (0.45f, 0.15f).amp (0.6f, 0.8f); } });

        p.push_back ({ "Drawbar Organ", "Keys", "Organ registrations in a wavetable; slide changes the drawbars, pressure the Leslie speed.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 8, 0.33f, 0.75f)
                .flt (FilterField::On, 0.0f)
                .env (pid::ampEnv, 0.004f, 0.1f, 1.0f, 0.06f)
                .route (S::MpeSlide, D::Osc1WtPos, 0.66f).route (S::Lfo1, D::Pitch, 0.003f).route (S::Lfo1, D::Pan, 0.3f)
                .route (S::MpePressure, D::Lfo1Rate, 0.4f)
                .lfo (0, LfoShape::Sine, 1.2f, 1.0f, false).macros (D::Osc1WtPos, 0.4f, "DRAWBARS")
                .chorus (0.5f, 0.3f, 1.5f).fx (FxField::SatOn, 1.0f).fx (FxField::SatDrive, 0.25f).amp (0.55f, 0.0f); } });

        p.push_back ({ "Soft Keys", "Keys", "Rounded triangle/sine keys with velocity-sensitive tone and a small room.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Triangle, 0.7f).analog (1, W::Sine, 0.45f, 1)
                .filter (F::LowPass12, 1400.0f, 0.05f, 0.4f, 0.6f).flt (FilterField::Velocity, 0.8f)
                .env (pid::ampEnv, 0.002f, 1.8f, 0.2f, 0.5f).env (pid::filterEnv, 0.001f, 0.8f, 0.1f, 0.4f)
                .route (S::MpePressure, D::FilterCutoff, 0.25f).route (S::ReleaseVelocity, D::AmpRelease, -0.5f)
                .macros (D::FilterCutoff, 0.3f, "BRIGHT").reverb (0.35f, 0.2f).amp (0.65f, 0.75f); } });

        // ------------------------------------------------------------------ FM
        p.push_back ({ "FM Bell Choir", "FM", "Inharmonic bells with a long decay; slide detunes the modulator for evolving partials.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Stack, 5, 8, 0.35f, 0.2f, 0.0f, 0.7f, 0.7f).osc (0, OscField::OpFine, 0.07f).unison (0, 2, 0.05f, 0.6f)
                .flt (FilterField::On, 0.0f)
                .env (pid::ampEnv, 0.001f, 4.0f, 0.0f, 2.0f, 0.0f, 0.6f).env (pid::modEnv, 0.0f, 2.5f, 0.2f, 1.5f)
                .route (S::MpeSlide, D::Osc1Fm, 0.4f).route (S::Velocity, D::Osc1Fm, 0.3f)
                .macros (D::Osc1Fm, 0.4f, "SPARKLE").reverb (0.8f, 0.45f, 0.6f).amp (0.5f, 0.6f); } });

        p.push_back ({ "Feedback Grit", "FM", "Operator feedback growls under pressure, then the distortion takes over.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Parallel, 2, 3, 0.2f, 0.0f, 0.35f, 0.0f, 0.75f, -1)
                .filter (F::LowPass24, 2200.0f, 0.2f, 0.2f, 0.4f)
                .env (pid::ampEnv, 0.003f, 0.5f, 0.8f, 0.2f)
                .route (S::MpePressure, D::Osc1Fm, 0.6f).route (S::MpeSlide, D::FilterCutoff, 0.3f).route (S::MpePressure, D::DistDrive, 0.4f)
                .fx (FxField::DistOn, 1.0f).fx (FxField::DistType, (float) DistortionType::Asymmetric).fx (FxField::DistDrive, 0.25f)
                .macros (D::Osc1Fm, 0.5f, "GRIT").amp (0.55f, 0.5f); } });

        p.push_back ({ "Ratio Slide", "FM", "Slide moves the modulator fine ratio for per-note beating and clangour.",
            [] (PresetState& s) { Build (s)
                .fm (0, FmAlgorithm::Stack, 3, 2, 0.4f, 0.0f, 0.1f, 0.4f, 0.7f)
                .filter (F::LowPass12, 5000.0f, 0.1f, 0.1f, 0.4f)
                .env (pid::ampEnv, 0.005f, 0.8f, 0.7f, 0.4f).env (pid::modEnv, 0.0f, 0.6f, 0.3f, 0.4f)
                .route (S::MpeSlide, D::Osc1Fm, 0.5f).route (S::MpePressure, D::FilterCutoff, 0.35f).route (S::Lfo1, D::Osc1Fm, 0.1f)
                .lfo (0, LfoShape::Sine, 0.4f).macros (D::Osc1Fm, 0.5f, "INDEX").delay ("1/4.", 0.3f, 0.2f).amp (0.55f); } });

        // ------------------------------------------------------------------ WAVETABLE
        p.push_back ({ "Vowel Sweep", "Wavetable", "Formant table: slide speaks A-E-I-O-U on each note separately.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 3, 0.0f, 0.75f).unison (0, 2, 0.08f, 0.4f)
                .filter (F::LowPass12, 7000.0f, 0.1f, 0.0f, 0.3f)
                .env (pid::ampEnv, 0.02f, 0.5f, 0.9f, 0.4f)
                .route (S::MpeSlide, D::Osc1WtPos, 1.0f).route (S::MpePressure, D::FilterCutoff, 0.2f).route (S::MpePressure, D::AmpLevel, 0.3f)
                .macros (D::Osc1WtPos, 0.8f, "VOWEL").reverb (0.5f, 0.25f).amp (0.55f, 0.5f, 0.3f); } });

        p.push_back ({ "Fold Scanner", "Wavetable", "Sine-fold table scanned by an LFO; pressure deepens the scan.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 9, 0.1f, 0.7f).unison (0, 3, 0.12f, 0.7f)
                .filter (F::LowPass24, 6000.0f, 0.15f, 0.0f, 0.3f)
                .env (pid::ampEnv, 0.01f, 0.6f, 0.85f, 0.5f)
                .route (S::Lfo1, D::Osc1WtPos, 0.2f).route (S::MpePressure, D::Lfo1Depth, 0.8f).route (S::MpeSlide, D::Osc1WtPos, 0.5f)
                .lfo (0, LfoShape::Triangle, 0.8f, 0.2f).macros (D::Osc1WtPos, 0.6f, "FOLD")
                .reverb (0.55f, 0.25f).amp (0.5f); } });

        p.push_back ({ "Harmonic Rise", "Wavetable", "Harmonics bloom with the mod envelope; slide holds them open.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 2, 0.0f, 0.7f).unison (0, 4, 0.15f, 0.8f)
                .filter (F::LowPass12, 9000.0f, 0.05f, 0.0f, 0.3f)
                .env (pid::ampEnv, 0.15f, 1.0f, 0.8f, 1.0f).env (pid::modEnv, 0.8f, 2.0f, 0.4f, 1.0f)
                .route (S::ModEnv, D::Osc1WtPos, 0.8f).route (S::MpeSlide, D::Osc1WtPos, 0.5f).route (S::MpePressure, D::AmpLevel, 0.3f)
                .macros (D::Osc1WtPos, 0.5f).chorus (0.25f).reverb (0.7f, 0.35f).amp (0.5f, 0.4f, 0.3f); } });

        // ------------------------------------------------------------------ ATMOSPHERIC
        p.push_back ({ "Night Drift", "Atmospheric", "Pink noise and a drifting wavetable under long reverb; slide moves the air.",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 7, 0.4f, 0.45f).unison (0, 4, 0.2f, 1.0f)
                .noise (2, NoiseType::Pink, 0.25f, 1.0f)
                .filter (F::LowPass12, 1800.0f, 0.3f, 0.0f, 0.2f)
                .env (pid::ampEnv, 2.0f, 2.0f, 0.9f, 4.0f)
                .route (S::Lfo1, D::FilterCutoff, 0.15f).route (S::Lfo2, D::Osc1WtPos, 0.3f).route (S::MpeSlide, D::FilterCutoff, 0.3f)
                .route (S::MpePressure, D::Osc3Level, 0.4f)
                .lfo (0, LfoShape::SmoothRandom, 0.1f, 1.0f, false).lfo (1, LfoShape::Sine, 0.05f, 1.0f, false)
                .macros (D::Osc1WtPos, 0.4f, "DRIFT").delay ("1/2", 0.55f, 0.3f).reverb (0.95f, 0.65f, 0.7f).amp (0.5f, 0.2f); } });

        p.push_back ({ "Wind Glass", "Atmospheric", "Filtered noise: each finger's slide tunes its band, pressure is its gust.",
            [] (PresetState& s) { Build (s)
                .noise (0, NoiseType::White, 0.8f, 0.8f)
                .filter (F::BandPass, 1200.0f, 0.75f, 0.0f, 1.0f)
                .env (pid::ampEnv, 0.4f, 1.0f, 1.0f, 1.5f)
                .route (S::MpeSlide, D::FilterCutoff, 0.35f).route (S::Lfo1, D::FilterCutoff, 0.05f)
                .lfo (0, LfoShape::SmoothRandom, 0.6f)
                .macros (D::FilterResonance, 0.3f, "WHISTLE").reverb (0.9f, 0.6f, 0.6f).amp (0.6f, 0.0f, 1.0f); } });

        p.push_back ({ "Cinematic Swell", "Atmospheric", "Slow orchestral swell; pressure pushes each note into the reverb.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.55f).unison (0, 5, 0.25f, 1.0f)
                .analog (1, W::Square, 0.35f, -1).unison (1, 3, 0.15f, 0.6f)
                .filter (F::LowPass24, 500.0f, 0.2f, 0.5f, 0.4f)
                .env (pid::ampEnv, 1.5f, 2.0f, 0.9f, 3.0f).env (pid::filterEnv, 3.0f, 3.0f, 0.8f, 2.5f, 0.0f)
                .route (S::MpePressure, D::ReverbSend, 0.6f).route (S::MpePressure, D::FilterCutoff, 0.25f).route (S::MpeSlide, D::StereoWidth, 0.4f)
                .macros (D::FilterEnvAmount, 0.4f, "SWELL").reverb (0.9f, 0.3f, 0.7f).amp (0.5f, 0.3f); } });

        p.push_back ({ "Grain Choir", "Atmospheric", "Granular vowels: slide moves each note through the source, pressure thickens the cloud.",
            [] (PresetState& s) { Build (s)
                .granular (0, 0.35f, 0.12f, 24.0f, 0.2f, 0.05f, 0.8f).osc (0, OscField::Spread, 0.8f)
                .filter (F::LowPass12, 7000.0f, 0.1f, 0.0f, 0.3f)
                .env (pid::ampEnv, 0.6f, 1.5f, 0.9f, 2.5f)
                .route (S::MpeSlide, D::Osc1WtPos, 0.5f).route (S::MpePressure, D::GrainDensity, 0.5f)
                .route (S::Lfo1, D::Osc1WtPos, 0.08f).lfo (0, LfoShape::SmoothRandom, 0.3f, 1.0f, false)
                .macros (D::GrainSize, 0.6f, "GRAIN").reverb (0.85f, 0.5f, 0.6f).amp (0.55f, 0.3f); } });

        p.push_back ({ "Frozen Shimmer", "Experimental", "Long, pitch-scattered grains an octave up; pressure sprays them further.",
            [] (PresetState& s) { Build (s)
                .granular (0, 0.6f, 0.4f, 40.0f, 0.1f, 0.3f, 0.7f, 1).osc (0, OscField::Spread, 1.0f)
                .granular (1, 0.2f, 0.25f, 12.0f, 0.4f, 0.0f, 0.4f, -1)
                .filter (F::HighPass12, 180.0f, 0.1f, 0.0f, 0.0f)
                .env (pid::ampEnv, 1.2f, 2.0f, 1.0f, 3.5f)
                .route (S::MpePressure, D::GrainSize, -0.4f).route (S::MpeSlide, D::Osc1WtPos, 0.4f).route (S::MpeSlide, D::Osc2WtPos, -0.3f)
                .macros (D::GrainDensity, 0.5f, "DENSITY").delay ("1/4.", 0.5f, 0.35f).reverb (0.95f, 0.6f, 0.7f).amp (0.5f, 0.2f); } });

        // ------------------------------------------------------------------ EXPERIMENTAL
        p.push_back ({ "Glitch Ratchet", "Experimental", "Random ratcheting arp through a crusher; S&H jumps the filter.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Pulse, 0.7f).osc (0, OscField::PulseWidth, 0.25f)
                .filter (F::LowPass24, 1500.0f, 0.4f, 0.3f, 0.4f)
                .env (pid::ampEnv, 0.001f, 0.15f, 0.0f, 0.08f).env (pid::filterEnv, 0.0f, 0.1f, 0.0f, 0.08f)
                .arp (ArpField::On, 1.0f).arp (ArpField::Mode, (float) ArpMode::Random).arp (ArpField::Ratchet, 2.0f)
                .arp (ArpField::Probability, 0.75f).arp (ArpField::Octaves, 2.0f).arp (ArpField::Gate, 0.4f)
                .route (S::SampleHold, D::FilterCutoff, 0.3f).route (S::MpePressure, D::CrushAmount, 0.5f).route (S::Lfo1, D::Osc1Pw, 0.3f)
                .lfo (0, LfoShape::Triangle, 3.0f).g (GlobalField::SampleHoldDivision, (float) divisionIndex ("1/16"))
                .fx (FxField::CrushOn, 1.0f).fx (FxField::CrushBits, 9.0f).fx (FxField::CrushDownsample, 3.0f).fx (FxField::CrushMix, 0.6f)
                .macros (D::ArpProbability, 0.25f, "CHAOS").delay ("1/16.", 0.4f, 0.25f).amp (0.55f, 0.5f); } });

        p.push_back ({ "Crackle Engine", "Experimental", "Pitched crackle ring-modulated by a saw and wavefolded.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.0f, -1)
                .noise (1, NoiseType::Crackle, 0.9f, 0.5f).osc (1, OscField::Ring, 0.8f)
                .analog (2, W::Sine, 0.4f)
                .filter (F::LowPass12, 4000.0f, 0.3f, 0.0f, 0.5f)
                .env (pid::ampEnv, 0.01f, 0.8f, 0.7f, 0.5f)
                .route (S::MpePressure, D::DistDrive, 0.5f).route (S::MpeSlide, D::FilterCutoff, 0.4f)
                .fx (FxField::DistOn, 1.0f).fx (FxField::DistType, (float) DistortionType::Fold).fx (FxField::DistDrive, 0.3f).fx (FxField::DistMix, 0.6f)
                .macros (D::DistDrive, 0.5f, "FOLD").reverb (0.5f, 0.25f).amp (0.5f); } });

        p.push_back ({ "Random Mutant", "Experimental", "Digital noise pitched to the keys; sample & hold rearranges the wavetable.",
            [] (PresetState& s) { Build (s)
                .noise (0, NoiseType::Digital, 0.5f, 0.4f)
                .wavetable (1, 6, 0.5f, 0.55f).unison (1, 2, 0.1f, 0.5f)
                .filter (F::Notch, 1500.0f, 0.4f, 0.0f, 0.5f)
                .env (pid::ampEnv, 0.005f, 0.7f, 0.6f, 0.4f)
                .route (S::SampleHold, D::Osc2WtPos, 0.8f).route (S::Lfo1, D::FilterCutoff, 0.3f).route (S::MpeSlide, D::FlangerMix, 0.6f)
                .lfo (0, LfoShape::Saw, 0.7f).g (GlobalField::SampleHoldDivision, (float) divisionIndex ("1/8"))
                .fx (FxField::FlangerOn, 1.0f).fx (FxField::FlangerMix, 0.3f)
                .macros (D::Osc2WtPos, 0.5f).amp (0.5f); } });

        // ------------------------------------------------------------------ MPE PERFORMANCE
        p.push_back ({ "MPE Morph Voice", "MPE Performance", "Each note morphs from a soft pad (A) to a bright, driven lead (B) with its slide.",
            [] (PresetState& s)
            {
                Build (s)
                    .wavetable (0, 0, 0.1f, 0.65f).unison (0, 3, 0.12f, 0.7f)
                    .analog (1, W::Triangle, 0.4f, -1)
                    .filter (F::LowPass24, 900.0f, 0.1f, 0.1f, 0.3f)
                    .env (pid::ampEnv, 0.08f, 0.8f, 0.85f, 0.8f)
                    .route (S::MpeSlide, D::Morph, 1.0f).route (S::MpePressure, D::FilterCutoff, 0.25f)
                    .g (GlobalField::MorphOn, 1.0f).macros (D::Morph, 1.0f, "MORPH").reverb (0.6f, 0.3f).amp (0.55f, 0.3f, 0.3f);

                // B: bright sync-free saw lead with an open, resonant filter.
                s.hasMorphTarget = true;
                s.morphTarget = s.params;
                auto& b = s.morphTarget;
                b[pid::osc (0, OscField::WtPos)] = 0.9f;
                b[pid::osc (0, OscField::Detune)] = 0.35f;
                b[pid::osc (1, OscField::Wave)] = (float) AnalogWave::Saw;
                b[pid::osc (1, OscField::Octave)] = 0.0f;
                b[pid::filter (FilterField::Cutoff)] = 6500.0f;
                b[pid::filter (FilterField::Resonance)] = 0.45f;
                b[pid::filter (FilterField::Drive)] = 0.5f;
                b[pid::env (pid::ampEnv, EnvField::Attack)] = 0.005f;
            } });

        p.push_back ({ "Five Dimensions", "MPE Performance", "Every MPE dimension has a job: bend, pressure (filter), slide (table), velocity (FM), release velocity (release).",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 5, 0.2f, 0.6f)
                .fm (1, FmAlgorithm::Stack, 3, 2, 0.1f, 0.0f, 0.0f, 0.0f, 0.4f)
                .filter (F::LowPass24, 1200.0f, 0.3f, 0.2f, 0.5f)
                .env (pid::ampEnv, 0.01f, 0.8f, 0.8f, 0.5f)
                .route (S::MpePressure, D::FilterCutoff, 0.45f).route (S::MpeSlide, D::Osc1WtPos, 0.8f)
                .route (S::Velocity, D::Osc2Fm, 0.6f).route (S::ReleaseVelocity, D::AmpRelease, -0.6f)
                .macros (D::Osc1WtPos, 0.4f).reverb (0.5f, 0.25f).amp (0.55f, 0.5f); } });

        p.push_back ({ "Pressure Echoes", "MPE Performance", "Only the notes you lean into reach the delay and reverb: per-note sends from pressure.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.55f).unison (0, 3, 0.15f, 0.6f).analog (1, W::Pulse, 0.35f, 1).osc (1, OscField::PulseWidth, 0.3f)
                .filter (F::LowPass24, 1600.0f, 0.2f, 0.3f, 0.4f)
                .env (pid::ampEnv, 0.005f, 0.6f, 0.7f, 0.35f)
                .fx (FxField::DelaySend, 0.0f).fx (FxField::ReverbSend, 0.0f)
                .route (S::MpePressure, D::DelaySend, 0.9f).route (S::MpePressure, D::ReverbSend, 0.7f)
                .route (S::MpeSlide, D::FilterCutoff, 0.35f)
                .delay ("1/8.", 0.5f, 0.0f).reverb (0.7f, 0.0f, 0.6f).macros (D::DelayFeedback, 0.3f, "REPEATS").amp (0.55f, 0.5f); } });

        p.push_back ({ "Expressive Arp", "MPE Performance", "Chord arp whose notes follow the finger that made them: lean or slide one key to shape its line.",
            [] (PresetState& s) { Build (s)
                .analog (0, W::Saw, 0.6f).analog (1, W::Square, 0.35f, 1)
                .filter (F::LowPass24, 700.0f, 0.35f, 0.4f, 0.5f)
                .env (pid::ampEnv, 0.001f, 0.25f, 0.2f, 0.15f).env (pid::filterEnv, 0.0f, 0.18f, 0.0f, 0.1f)
                .arp (ArpField::On, 1.0f).arp (ArpField::Mode, (float) ArpMode::UpDown).arp (ArpField::Octaves, 2.0f).arp (ArpField::Gate, 0.5f)
                .route (S::MpePressure, D::FilterCutoff, 0.45f).route (S::MpeSlide, D::FilterResonance, 0.4f)
                .macros (D::ArpGate, 0.4f, "GATE").delay ("1/8.", 0.35f, 0.25f).reverb (0.5f, 0.2f).amp (0.55f, 0.6f); } });

        p.push_back ({ "Sequenced Expression", "MPE Performance", "The step sequencer plays pressure, slide and pitch glides per step (runs with the host).",
            [] (PresetState& s) { Build (s)
                .wavetable (0, 3, 0.2f, 0.65f).analog (1, W::Saw, 0.35f, -1)
                .filter (F::LowPass24, 900.0f, 0.3f, 0.35f, 0.4f)
                .env (pid::ampEnv, 0.002f, 0.4f, 0.5f, 0.2f).env (pid::filterEnv, 0.0f, 0.3f, 0.1f, 0.2f)
                .seq (SeqField::On, 1.0f).seq (SeqField::Clock, (float) SeqClock::FollowHost)
                .route (S::MpeSlide, D::Osc1WtPos, 0.8f).route (S::MpePressure, D::FilterCutoff, 0.4f)
                .macros (D::Osc1WtPos, 0.4f).delay ("1/8.", 0.35f, 0.2f).reverb (0.5f, 0.2f).amp (0.55f, 0.6f); } });

        return p;
    }
} // namespace

const std::vector<FactoryPreset>& getFactoryPresets()
{
    static const std::vector<FactoryPreset> library = buildLibrary();
    return library;
}

PresetState makeFactoryPreset (int index)
{
    const auto& library = getFactoryPresets();
    const auto& recipe = library[(size_t) juce::jlimit (0, (int) library.size() - 1, index)];
    PresetState s;
    recipe.build (s);
    s.name = recipe.name;
    s.category = recipe.category;
    s.author = "NeddPE Factory";
    s.description = recipe.description;
    return s;
}

} // namespace nedd
