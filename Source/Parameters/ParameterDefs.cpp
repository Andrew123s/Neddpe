#include "ParameterDefs.h"
#include "DSP/Wavetable.h"
#include "Synth/Tempo.h"

namespace nedd
{
// ---------------------------------------------------------------------------------------------
// Choice lists (append only)
// ---------------------------------------------------------------------------------------------
juce::StringArray getOscEngineNames() { return { "Analog", "Wavetable", "FM", "Noise" }; }
juce::StringArray getAnalogWaveNames() { return { "Sine", "Triangle", "Saw", "Square", "Pulse" }; }
juce::StringArray getNoiseTypeNames() { return { "White", "Pink", "Brown", "Crackle", "Digital" }; }
juce::StringArray getFmAlgorithmNames() { return { "Stack 2>1>C", "Parallel 1+2>C", "Branch 2>1+C" }; }
juce::StringArray getFilterTypeNames() { return { "LP 12", "LP 24", "HP 12", "HP 24", "Band Pass", "Notch", "Ladder LP" }; }
juce::StringArray getLfoShapeNames() { return { "Sine", "Triangle", "Saw", "Reverse Saw", "Square", "Sample & Hold", "Smooth Random", "Custom" }; }

juce::StringArray getScaleNames()
{
    return { "Chromatic (12-TET)", "Major", "Natural Minor", "Harmonic Minor", "Dorian", "Phrygian", "Lydian",
             "Mixolydian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone", "Octave", "User" };
}

namespace
{
    const std::array<float, 18> fmRatios { 0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
                                           9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f };
}

juce::StringArray getFmRatioNames()
{
    juce::StringArray names;
    for (auto r : fmRatios)
        names.add ("x" + juce::String (r, r < 1.0f ? 2 : 0));
    return names;
}

float fmRatioValue (int index) { return fmRatios[(size_t) juce::jlimit (0, (int) fmRatios.size() - 1, index)]; }

// ---------------------------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------------------------
namespace fmt
{
    juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

    juce::String bipolarPercent (float v)
    {
        const int p = juce::roundToInt (v * 100.0f);
        return (p > 0 ? "+" : "") + juce::String (p) + "%";
    }

    juce::String hz (float v)
    {
        if (v >= 1000.0f)
            return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        return juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
    }

    juce::String seconds (float v)
    {
        if (v < 1.0f)
            return juce::String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms";
        return juce::String (v, 2) + " s";
    }

    juce::String ms (float v) { return juce::String (v, v < 10.0f ? 1 : 0) + " ms"; }
    juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String semis (float v) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; }
    juce::String cents (float v) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct"; }

    juce::String pan (float v)
    {
        const int p = juce::roundToInt (v * 100.0f);
        if (p == 0) return "C";
        return p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p);
    }

    juce::String gain (float v) { return v <= 0.0001f ? "-inf dB" : db (juce::Decibels::gainToDecibels (v)); }
} // namespace fmt

// ---------------------------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------------------------
namespace
{
    class Builder
    {
    public:
        std::vector<ParamDef> defs;

        ParamDef& add (int index, juce::String id, juce::String name, ParamGroup group, ParamType type)
        {
            // The table must be declared in exactly the order of the pid:: layout.
            jassert (index == (int) defs.size());
            juce::ignoreUnused (index);

            ParamDef d;
            d.index = (int) defs.size();
            d.id = std::move (id);
            d.name = std::move (name);
            d.group = group;
            d.type = type;
            defs.push_back (std::move (d));
            return defs.back();
        }

        ParamDef& real (int index, juce::String id, juce::String name, ParamGroup group,
                        float min, float max, float def, std::function<juce::String (float)> formatter,
                        float skewCentre = 0.0f, float interval = 0.0f)
        {
            auto& d = add (index, std::move (id), std::move (name), group, ParamType::Float);
            d.range = juce::NormalisableRange<float> (min, max, interval);
            if (skewCentre > 0.0f)
                d.range.setSkewForCentre (skewCentre);
            d.defaultValue = def;
            d.formatter = std::move (formatter);
            return d;
        }

        ParamDef& integer (int index, juce::String id, juce::String name, ParamGroup group, int min, int max, int def,
                           std::function<juce::String (float)> formatter = {})
        {
            auto& d = add (index, std::move (id), std::move (name), group, ParamType::Int);
            d.range = juce::NormalisableRange<float> ((float) min, (float) max, 1.0f);
            d.defaultValue = (float) def;
            d.formatter = formatter ? std::move (formatter) : [] (float v) { return juce::String (juce::roundToInt (v)); };
            return d;
        }

        ParamDef& toggle (int index, juce::String id, juce::String name, ParamGroup group, bool def)
        {
            auto& d = add (index, std::move (id), std::move (name), group, ParamType::Bool);
            d.range = juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
            d.defaultValue = def ? 1.0f : 0.0f;
            d.formatter = [] (float v) { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); };
            return d;
        }

        ParamDef& choice (int index, juce::String id, juce::String name, ParamGroup group, juce::StringArray choices, int def)
        {
            auto& d = add (index, std::move (id), std::move (name), group, ParamType::Choice);
            d.range = juce::NormalisableRange<float> (0.0f, (float) (choices.size() - 1), 1.0f);
            d.defaultValue = (float) def;
            d.formatter = [choices] (float v) { return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (v))]; };
            d.choices = std::move (choices);
            return d;
        }
    };

    ParamDef& sound (ParamDef& d, bool perVoice)
    {
        d.morphable = true;
        d.perVoice = perVoice;
        return d;
    }

    void addOscillators (Builder& b)
    {
        for (int o = 0; o < kNumOscillators; ++o)
        {
            const auto p = "osc" + juce::String (o + 1) + "_";
            const auto n = "OSC " + juce::String (o + 1) + " ";
            constexpr auto G = ParamGroup::Oscillator;
            auto i = [o] (OscField f) { return pid::osc (o, f); };

            sound (b.toggle (i (OscField::On), p + "on", n + "On", G, o == 0), true);
            sound (b.choice (i (OscField::Engine), p + "engine", n + "Engine", G, getOscEngineNames(), (int) OscEngine::Analog), true);
            sound (b.choice (i (OscField::Wave), p + "wave", n + "Waveform", G, getAnalogWaveNames(), (int) AnalogWave::Saw), true);
            sound (b.real (i (OscField::PulseWidth), p + "pw", n + "Pulse Width", G, 0.02f, 0.98f, 0.5f, fmt::percent), true);
            sound (b.choice (i (OscField::Table), p + "table", n + "Wavetable", G, getWavetableNames(), 0), true);
            sound (b.real (i (OscField::WtPos), p + "wtpos", n + "WT Position", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.integer (i (OscField::Octave), p + "octave", n + "Octave", G, -4, 4, 0,
                              [] (float v) { const int x = juce::roundToInt (v); return (x > 0 ? "+" : "") + juce::String (x) + " oct"; }), true);
            sound (b.integer (i (OscField::Semi), p + "semi", n + "Semitone", G, -12, 12, o == 1 ? 7 : 0, fmt::semis), true);
            sound (b.real (i (OscField::Fine), p + "fine", n + "Fine", G, -100.0f, 100.0f, 0.0f, fmt::cents), true);
            sound (b.real (i (OscField::Phase), p + "phase", n + "Phase", G, 0.0f, 1.0f, 0.0f,
                           [] (float v) { return juce::String (juce::roundToInt (v * 360.0f)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }), true);
            sound (b.real (i (OscField::PhaseRandom), p + "phrand", n + "Phase Random", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.real (i (OscField::Level), p + "level", n + "Level", G, 0.0f, 1.0f, 0.75f, fmt::percent), true);
            sound (b.real (i (OscField::Pan), p + "pan", n + "Pan", G, -1.0f, 1.0f, 0.0f, fmt::pan), true);
            sound (b.integer (i (OscField::Unison), p + "unison", n + "Unison", G, 1, 8, 1), true);
            sound (b.real (i (OscField::Detune), p + "detune", n + "Detune", G, 0.0f, 1.0f, 0.2f, fmt::percent), true);
            sound (b.real (i (OscField::Spread), p + "spread", n + "Stereo Spread", G, 0.0f, 1.0f, 0.6f, fmt::percent), true);
            sound (b.choice (i (OscField::FmSource), p + "fmsrc", n + "FM Source", G, { "OSC 1", "OSC 2", "OSC 3" }, o == 0 ? 2 : o - 1), true);
            sound (b.real (i (OscField::FmAmount), p + "fm", n + "FM Amount", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.toggle (i (OscField::Sync), p + "sync", n + "Hard Sync", G, false), true);
            sound (b.real (i (OscField::Ring), p + "ring", n + "Ring Mod", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.choice (i (OscField::Route), p + "route", n + "Route", G, { "Filter", "Direct" }, 0), true);
            sound (b.choice (i (OscField::NoiseType), p + "noise", n + "Noise Type", G, getNoiseTypeNames(), 0), true);
            sound (b.choice (i (OscField::FmAlgorithm), p + "fmalgo", n + "FM Algorithm", G, getFmAlgorithmNames(), 0), true);
            sound (b.choice (i (OscField::Op1Ratio), p + "op1ratio", n + "Mod 1 Ratio", G, getFmRatioNames(), 3), true);
            sound (b.choice (i (OscField::Op2Ratio), p + "op2ratio", n + "Mod 2 Ratio", G, getFmRatioNames(), 2), true);
            sound (b.real (i (OscField::OpFine), p + "opfine", n + "Mod Fine Ratio", G, -0.5f, 0.5f, 0.0f,
                           [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 3); }), true);
            sound (b.real (i (OscField::Op1Amount), p + "op1amt", n + "Mod 1 Amount", G, 0.0f, 1.0f, 0.35f, fmt::percent), true);
            sound (b.real (i (OscField::Op2Amount), p + "op2amt", n + "Mod 2 Amount", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.real (i (OscField::FmFeedback), p + "fmfb", n + "FM Feedback", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.real (i (OscField::FmEnvAmount), p + "fmenv", n + "FM Env Amount", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.real (i (OscField::FmKeyTrack), p + "fmkt", n + "FM Key Track", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
        }
    }

    void addFilterAndAmp (Builder& b)
    {
        constexpr auto F = ParamGroup::Filter;
        sound (b.toggle (pid::filter (FilterField::On), "flt_on", "Filter On", F, true), true);
        sound (b.choice (pid::filter (FilterField::Type), "flt_type", "Filter Type", F, getFilterTypeNames(), (int) FilterType::LowPass24), true);
        sound (b.real (pid::filter (FilterField::Cutoff), "flt_cutoff", "Filter Cutoff", F, 20.0f, 20000.0f, 6000.0f, fmt::hz, 1000.0f), true);
        sound (b.real (pid::filter (FilterField::Resonance), "flt_reso", "Filter Resonance", F, 0.0f, 1.0f, 0.15f, fmt::percent), true);
        sound (b.real (pid::filter (FilterField::Drive), "flt_drive", "Filter Drive", F, 0.0f, 1.0f, 0.0f, fmt::percent), true);
        sound (b.real (pid::filter (FilterField::KeyTrack), "flt_keytrack", "Filter Key Track", F, 0.0f, 1.0f, 0.3f, fmt::percent), true);
        sound (b.real (pid::filter (FilterField::EnvAmount), "flt_env", "Filter Env Amount", F, -1.0f, 1.0f, 0.2f, fmt::bipolarPercent), true);
        sound (b.real (pid::filter (FilterField::Velocity), "flt_vel", "Filter Env Velocity", F, 0.0f, 1.0f, 0.3f, fmt::percent), true);
        sound (b.real (pid::filter (FilterField::Mix), "flt_mix", "Filter Mix", F, 0.0f, 1.0f, 1.0f, fmt::percent), true);

        constexpr auto A = ParamGroup::Amp;
        sound (b.real (pid::amp (AmpField::Level), "amp_level", "Amp Level", A, 0.0f, 1.0f, 0.7f, fmt::gain), true);
        sound (b.real (pid::amp (AmpField::Velocity), "amp_vel", "Amp Velocity Sens", A, 0.0f, 1.0f, 0.6f, fmt::percent), true);
        sound (b.real (pid::amp (AmpField::Pressure), "amp_pressure", "Amp Pressure Sens", A, 0.0f, 1.0f, 0.0f, fmt::percent), true);
        sound (b.real (pid::amp (AmpField::Pan), "amp_pan", "Amp Pan", A, -1.0f, 1.0f, 0.0f, fmt::pan), true);
    }

    void addEnvelopes (Builder& b)
    {
        const char* prefixes[] = { "aenv_", "fenv_", "menv_" };
        const char* names[] = { "Amp Env ", "Filter Env ", "Mod Env " };
        // Defaults: delay, attack, hold, decay, sustain, release, curves
        const float defaults[3][9] = {
            { 0.0f, 0.004f, 0.0f, 0.35f, 0.8f, 0.25f, 0.2f, 0.5f, 0.5f },
            { 0.0f, 0.002f, 0.0f, 0.45f, 0.25f, 0.35f, 0.2f, 0.5f, 0.5f },
            { 0.0f, 0.002f, 0.0f, 0.6f, 0.0f, 0.4f, 0.2f, 0.5f, 0.5f },
        };

        for (int e = 0; e < kNumEnvelopes; ++e)
        {
            const juce::String p = prefixes[e];
            const juce::String n = names[e];
            const auto& d = defaults[e];
            constexpr auto G = ParamGroup::Envelope;
            auto i = [e] (EnvField f) { return pid::env (e, f); };

            sound (b.real (i (EnvField::Delay), p + "delay", n + "Delay", G, 0.0f, 5.0f, d[0], fmt::seconds, 0.5f), true);
            sound (b.real (i (EnvField::Attack), p + "attack", n + "Attack", G, 0.0f, 20.0f, d[1], fmt::seconds, 1.0f), true);
            sound (b.real (i (EnvField::Hold), p + "hold", n + "Hold", G, 0.0f, 5.0f, d[2], fmt::seconds, 0.5f), true);
            sound (b.real (i (EnvField::Decay), p + "decay", n + "Decay", G, 0.001f, 20.0f, d[3], fmt::seconds, 1.0f), true);
            sound (b.real (i (EnvField::Sustain), p + "sustain", n + "Sustain", G, 0.0f, 1.0f, d[4], fmt::percent), true);
            sound (b.real (i (EnvField::Release), p + "release", n + "Release", G, 0.001f, 20.0f, d[5], fmt::seconds, 1.0f), true);
            sound (b.real (i (EnvField::AttackCurve), p + "acurve", n + "Attack Curve", G, -1.0f, 1.0f, d[6], fmt::bipolarPercent), true);
            sound (b.real (i (EnvField::DecayCurve), p + "dcurve", n + "Decay Curve", G, -1.0f, 1.0f, d[7], fmt::bipolarPercent), true);
            sound (b.real (i (EnvField::ReleaseCurve), p + "rcurve", n + "Release Curve", G, -1.0f, 1.0f, d[8], fmt::bipolarPercent), true);
        }
    }

    void addLfos (Builder& b)
    {
        for (int l = 0; l < kNumLfos; ++l)
        {
            const auto p = "lfo" + juce::String (l + 1) + "_";
            const auto n = "LFO " + juce::String (l + 1) + " ";
            constexpr auto G = ParamGroup::Lfo;
            auto i = [l] (LfoField f) { return pid::lfo (l, f); };

            sound (b.choice (i (LfoField::Shape), p + "shape", n + "Shape", G, getLfoShapeNames(), 0), true);
            sound (b.real (i (LfoField::Rate), p + "rate", n + "Rate", G, 0.01f, 100.0f, 2.0f + (float) l, fmt::hz, 2.0f), true);
            sound (b.toggle (i (LfoField::Sync), p + "sync", n + "Tempo Sync", G, false), true);
            sound (b.choice (i (LfoField::Division), p + "div", n + "Division", G, getTempoDivisionNames(), divisionIndex ("1/4")), true);
            sound (b.real (i (LfoField::Phase), p + "phase", n + "Phase", G, 0.0f, 1.0f, 0.0f, fmt::percent), true);
            sound (b.real (i (LfoField::Fade), p + "fade", n + "Fade In", G, 0.0f, 10.0f, 0.0f, fmt::seconds, 1.0f), true);
            sound (b.toggle (i (LfoField::Retrigger), p + "retrig", n + "Retrigger", G, true), true);
            sound (b.real (i (LfoField::Amount), p + "amount", n + "Amount", G, 0.0f, 1.0f, 1.0f, fmt::percent), true);
        }
    }

    void addMatrix (Builder& b)
    {
        // Init patch routes: MPE pitch drives oscillator pitch 1:1, pressure and slide open the filter.
        struct InitRoute { ModSource src; ModDest dst; float amount; ModPolarity polarity; };
        const InitRoute initRoutes[] = {
            { ModSource::MpePitch,    ModDest::Pitch,        1.0f,  ModPolarity::Bipolar },
            { ModSource::MpePressure, ModDest::FilterCutoff, 0.35f, ModPolarity::Unipolar },
            { ModSource::MpeSlide,    ModDest::FilterCutoff, 0.2f,  ModPolarity::Unipolar },
        };

        for (int s = 0; s < kNumModSlots; ++s)
        {
            const auto p = "mod" + juce::String (s + 1) + "_";
            const auto n = "Mod " + juce::String (s + 1) + " ";
            constexpr auto G = ParamGroup::Matrix;
            auto i = [s] (ModSlotField f) { return pid::mod (s, f); };

            const bool hasInit = s < (int) std::size (initRoutes);
            const InitRoute r = hasInit ? initRoutes[s] : InitRoute { ModSource::None, ModDest::None, 0.0f, ModPolarity::Unipolar };

            b.choice (i (ModSlotField::Source), p + "src", n + "Source", G, getModSourceNames(), (int) r.src).automatable = false;
            b.choice (i (ModSlotField::Dest), p + "dst", n + "Destination", G, getModDestNames(), (int) r.dst).automatable = false;
            b.real (i (ModSlotField::Amount), p + "amt", n + "Amount", G, -1.0f, 1.0f, r.amount, fmt::bipolarPercent);
            b.choice (i (ModSlotField::Curve), p + "curve", n + "Curve", G, getModCurveNames(), 0).automatable = false;
            b.choice (i (ModSlotField::Polarity), p + "pol", n + "Polarity", G, getModPolarityNames(), (int) r.polarity).automatable = false;
        }

        for (int m = 0; m < kNumMacros; ++m)
            b.real (pid::macro (m), "macro" + juce::String (m + 1), "Macro " + juce::String (m + 1), ParamGroup::Macro, 0.0f, 1.0f, 0.0f, fmt::percent);
    }

    void addGlobal (Builder& b)
    {
        auto i = [] (GlobalField f) { return pid::global (f); };

        b.real (i (GlobalField::MasterVolume), "master_vol", "Master Volume", ParamGroup::Master, -60.0f, 6.0f, -6.0f, fmt::db, -12.0f);
        b.choice (i (GlobalField::Quality), "quality", "CPU Quality", ParamGroup::Master, { "Eco", "Normal", "High", "Ultra" }, (int) Quality::Normal);

        b.choice (i (GlobalField::VoiceMode), "voice_mode", "Voice Mode", ParamGroup::Voice, { "Poly", "Mono", "Legato" }, 0);
        b.integer (i (GlobalField::Polyphony), "voice_poly", "Polyphony", ParamGroup::Voice, 1, 32, 16);
        sound (b.real (i (GlobalField::Glide), "voice_glide", "Glide Time", ParamGroup::Voice, 0.0f, 5.0f, 0.08f, fmt::seconds, 0.4f), true);
        b.choice (i (GlobalField::GlideMode), "voice_glidemode", "Glide Mode", ParamGroup::Voice, { "Off", "Always", "Legato" }, 0);

        b.choice (i (GlobalField::MpeMode), "mpe_mode", "MPE Mode", ParamGroup::Mpe, { "Off (Legacy)", "Lower Zone", "Upper Zone", "Both Zones" }, (int) MpeMode::LowerZone);
        b.integer (i (GlobalField::MpeBendRange), "mpe_bendrange", "MPE Pitch Bend Range", ParamGroup::Mpe, 1, 96, 48, fmt::semis);
        b.integer (i (GlobalField::MasterBendRange), "mpe_masterbend", "Master Pitch Bend Range", ParamGroup::Mpe, 0, 48, 2, fmt::semis);
        b.real (i (GlobalField::PitchSensitivity), "mpe_pitchsens", "MPE Pitch Sensitivity", ParamGroup::Mpe, 0.0f, 2.0f, 1.0f, fmt::percent);
        b.real (i (GlobalField::VelocityCurve), "mpe_velcurve", "Velocity Curve", ParamGroup::Mpe, -1.0f, 1.0f, 0.0f, fmt::bipolarPercent);
        b.real (i (GlobalField::PressureCurve), "mpe_prescurve", "Pressure Curve", ParamGroup::Mpe, -1.0f, 1.0f, 0.0f, fmt::bipolarPercent);
        b.real (i (GlobalField::SlideCurve), "mpe_slidecurve", "Slide Curve", ParamGroup::Mpe, -1.0f, 1.0f, 0.0f, fmt::bipolarPercent);
        b.real (i (GlobalField::ExpressionSmoothing), "mpe_smooth", "Expression Smoothing", ParamGroup::Mpe, 0.0f, 100.0f, 8.0f, fmt::ms, 15.0f);

        b.choice (i (GlobalField::ScaleType), "scale_type", "Scale", ParamGroup::Tuning, getScaleNames(), 0);
        b.choice (i (GlobalField::ScaleRoot), "scale_root", "Scale Root", ParamGroup::Tuning,
                  { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
        b.real (i (GlobalField::BendQuantize), "scale_bendq", "Pitch Quantize", ParamGroup::Tuning, 0.0f, 1.0f, 0.0f, fmt::percent);
        b.toggle (i (GlobalField::CustomTuning), "tune_custom", "Custom Tuning", ParamGroup::Tuning, false);

        b.choice (i (GlobalField::SampleHoldDivision), "sh_div", "S&H Rate", ParamGroup::Lfo, getTempoDivisionNames(), divisionIndex ("1/16"));

        b.toggle (i (GlobalField::MorphOn), "morph_on", "Morph Enabled", ParamGroup::Morph, false);
        b.real (i (GlobalField::MorphPosition), "morph_pos", "Morph A/B", ParamGroup::Morph, 0.0f, 1.0f, 0.0f, fmt::percent);

        b.toggle (i (GlobalField::MidiOut), "midi_out", "MPE MIDI Out", ParamGroup::Master, false);
    }

    void addEffects (Builder& b)
    {
        constexpr auto G = ParamGroup::Effects;
        auto i = [] (FxField f) { return pid::fx (f); };
        auto fx = [&b] (ParamDef& d) -> ParamDef& { return sound (d, false); };

        fx (b.toggle (i (FxField::DistOn), "dist_on", "Distortion On", G, false));
        fx (b.choice (i (FxField::DistType), "dist_type", "Distortion Type", G, { "Soft Clip", "Hard Clip", "Wavefold", "Asymmetric", "Tube" }, 0));
        fx (b.real (i (FxField::DistDrive), "dist_drive", "Distortion Drive", G, 0.0f, 1.0f, 0.3f, fmt::percent));
        fx (b.real (i (FxField::DistTone), "dist_tone", "Distortion Tone", G, 0.0f, 1.0f, 0.7f, fmt::percent));
        fx (b.real (i (FxField::DistMix), "dist_mix", "Distortion Mix", G, 0.0f, 1.0f, 1.0f, fmt::percent));

        fx (b.toggle (i (FxField::SatOn), "sat_on", "Saturation On", G, false));
        fx (b.real (i (FxField::SatDrive), "sat_drive", "Saturation Drive", G, 0.0f, 1.0f, 0.3f, fmt::percent));
        fx (b.real (i (FxField::SatWarmth), "sat_warmth", "Saturation Warmth", G, 0.0f, 1.0f, 0.5f, fmt::percent));
        fx (b.real (i (FxField::SatMix), "sat_mix", "Saturation Mix", G, 0.0f, 1.0f, 1.0f, fmt::percent));

        fx (b.toggle (i (FxField::CrushOn), "crush_on", "Bitcrush On", G, false));
        fx (b.real (i (FxField::CrushBits), "crush_bits", "Bitcrush Bits", G, 2.0f, 16.0f, 10.0f,
                    [] (float v) { return juce::String (v, 1) + " bit"; }));
        fx (b.real (i (FxField::CrushDownsample), "crush_rate", "Bitcrush Downsample", G, 1.0f, 32.0f, 1.0f,
                    [] (float v) { return "/" + juce::String (v, 1); }, 4.0f));
        fx (b.real (i (FxField::CrushMix), "crush_mix", "Bitcrush Mix", G, 0.0f, 1.0f, 1.0f, fmt::percent));

        fx (b.toggle (i (FxField::ChorusOn), "chorus_on", "Chorus On", G, false));
        fx (b.real (i (FxField::ChorusRate), "chorus_rate", "Chorus Rate", G, 0.05f, 5.0f, 0.6f, fmt::hz, 0.8f));
        fx (b.real (i (FxField::ChorusDepth), "chorus_depth", "Chorus Depth", G, 0.0f, 1.0f, 0.5f, fmt::percent));
        fx (b.real (i (FxField::ChorusMix), "chorus_mix", "Chorus Mix", G, 0.0f, 1.0f, 0.4f, fmt::percent));

        fx (b.toggle (i (FxField::PhaserOn), "phaser_on", "Phaser On", G, false));
        fx (b.real (i (FxField::PhaserRate), "phaser_rate", "Phaser Rate", G, 0.02f, 8.0f, 0.3f, fmt::hz, 0.8f));
        fx (b.real (i (FxField::PhaserDepth), "phaser_depth", "Phaser Depth", G, 0.0f, 1.0f, 0.7f, fmt::percent));
        fx (b.real (i (FxField::PhaserFeedback), "phaser_fb", "Phaser Feedback", G, 0.0f, 0.95f, 0.5f, fmt::percent));
        fx (b.real (i (FxField::PhaserMix), "phaser_mix", "Phaser Mix", G, 0.0f, 1.0f, 0.5f, fmt::percent));

        fx (b.toggle (i (FxField::FlangerOn), "flanger_on", "Flanger On", G, false));
        fx (b.real (i (FxField::FlangerRate), "flanger_rate", "Flanger Rate", G, 0.02f, 5.0f, 0.2f, fmt::hz, 0.6f));
        fx (b.real (i (FxField::FlangerDepth), "flanger_depth", "Flanger Depth", G, 0.0f, 1.0f, 0.7f, fmt::percent));
        fx (b.real (i (FxField::FlangerFeedback), "flanger_fb", "Flanger Feedback", G, -0.95f, 0.95f, 0.6f, fmt::bipolarPercent));
        fx (b.real (i (FxField::FlangerMix), "flanger_mix", "Flanger Mix", G, 0.0f, 1.0f, 0.5f, fmt::percent));

        fx (b.toggle (i (FxField::DelayOn), "delay_on", "Delay On", G, false));
        fx (b.toggle (i (FxField::DelaySync), "delay_sync", "Delay Sync", G, true));
        fx (b.real (i (FxField::DelayTime), "delay_time", "Delay Time", G, 1.0f, 2000.0f, 375.0f, fmt::ms, 300.0f));
        fx (b.choice (i (FxField::DelayDivision), "delay_div", "Delay Division", G, getTempoDivisionNames(), divisionIndex ("1/8.")));
        fx (b.real (i (FxField::DelayFeedback), "delay_fb", "Delay Feedback", G, 0.0f, 0.98f, 0.4f, fmt::percent));
        fx (b.real (i (FxField::DelayDamping), "delay_damp", "Delay Damping", G, 0.0f, 1.0f, 0.35f, fmt::percent));
        fx (b.toggle (i (FxField::DelayPingPong), "delay_pingpong", "Delay Ping Pong", G, true));
        sound (b.real (i (FxField::DelaySend), "delay_send", "Delay Send", G, 0.0f, 1.0f, 0.35f, fmt::percent), true);
        fx (b.real (i (FxField::DelayReturn), "delay_return", "Delay Return", G, 0.0f, 1.0f, 0.6f, fmt::percent));

        fx (b.toggle (i (FxField::ReverbOn), "reverb_on", "Reverb On", G, false));
        fx (b.real (i (FxField::ReverbSize), "reverb_size", "Reverb Size", G, 0.0f, 1.0f, 0.6f, fmt::percent));
        fx (b.real (i (FxField::ReverbDamping), "reverb_damp", "Reverb Damping", G, 0.0f, 1.0f, 0.4f, fmt::percent));
        fx (b.real (i (FxField::ReverbPredelay), "reverb_predelay", "Reverb Pre-Delay", G, 0.0f, 200.0f, 12.0f, fmt::ms, 40.0f));
        fx (b.real (i (FxField::ReverbWidth), "reverb_width", "Reverb Width", G, 0.0f, 1.0f, 1.0f, fmt::percent));
        sound (b.real (i (FxField::ReverbSend), "reverb_send", "Reverb Send", G, 0.0f, 1.0f, 0.3f, fmt::percent), true);
        fx (b.real (i (FxField::ReverbReturn), "reverb_return", "Reverb Return", G, 0.0f, 1.0f, 0.5f, fmt::percent));

        fx (b.toggle (i (FxField::EqOn), "eq_on", "EQ On", G, false));
        fx (b.real (i (FxField::EqLowGain), "eq_low", "EQ Low Gain", G, -18.0f, 18.0f, 0.0f, fmt::db));
        fx (b.real (i (FxField::EqMidFreq), "eq_midfreq", "EQ Mid Frequency", G, 150.0f, 10000.0f, 1200.0f, fmt::hz, 1200.0f));
        fx (b.real (i (FxField::EqMidGain), "eq_mid", "EQ Mid Gain", G, -18.0f, 18.0f, 0.0f, fmt::db));
        fx (b.real (i (FxField::EqHighGain), "eq_high", "EQ High Gain", G, -18.0f, 18.0f, 0.0f, fmt::db));

        fx (b.toggle (i (FxField::CompOn), "comp_on", "Compressor On", G, false));
        fx (b.real (i (FxField::CompThreshold), "comp_thresh", "Comp Threshold", G, -48.0f, 0.0f, -18.0f, fmt::db));
        fx (b.real (i (FxField::CompRatio), "comp_ratio", "Comp Ratio", G, 1.0f, 20.0f, 3.0f,
                    [] (float v) { return juce::String (v, 1) + ":1"; }, 4.0f));
        fx (b.real (i (FxField::CompAttack), "comp_attack", "Comp Attack", G, 0.1f, 100.0f, 10.0f, fmt::ms, 10.0f));
        fx (b.real (i (FxField::CompRelease), "comp_release", "Comp Release", G, 10.0f, 1000.0f, 120.0f, fmt::ms, 120.0f));
        fx (b.real (i (FxField::CompMakeup), "comp_makeup", "Comp Makeup", G, 0.0f, 24.0f, 0.0f, fmt::db));

        b.toggle (i (FxField::LimiterOn), "limiter_on", "Output Limiter", G, true);
    }

    void addArpAndSeq (Builder& b)
    {
        constexpr auto A = ParamGroup::Arp;
        auto a = [] (ArpField f) { return pid::arp (f); };
        b.toggle (a (ArpField::On), "arp_on", "Arp On", A, false);
        b.choice (a (ArpField::Mode), "arp_mode", "Arp Mode", A, { "Up", "Down", "Up/Down", "Random", "Order", "Chord", "Custom" }, 0);
        b.choice (a (ArpField::Division), "arp_rate", "Arp Rate", A, getTempoDivisionNames(), divisionIndex ("1/16"));
        b.real (a (ArpField::Gate), "arp_gate", "Arp Gate", A, 0.05f, 1.0f, 0.6f, fmt::percent);
        b.real (a (ArpField::Swing), "arp_swing", "Arp Swing", A, 0.0f, 0.75f, 0.0f, fmt::percent);
        b.integer (a (ArpField::Octaves), "arp_oct", "Arp Octaves", A, 1, 4, 1);
        b.integer (a (ArpField::Length), "arp_length", "Arp Length", A, 1, 16, 8);
        b.choice (a (ArpField::VelocityMode), "arp_velmode", "Arp Velocity Mode", A, { "As Played", "Fixed" }, 0);
        b.real (a (ArpField::Velocity), "arp_vel", "Arp Velocity", A, 0.0f, 1.0f, 0.8f, fmt::percent);
        b.real (a (ArpField::Probability), "arp_prob", "Arp Probability", A, 0.0f, 1.0f, 1.0f, fmt::percent);
        b.integer (a (ArpField::Ratchet), "arp_ratchet", "Arp Ratchet", A, 1, 4, 1);
        b.integer (a (ArpField::Repeat), "arp_repeat", "Arp Step Repeat", A, 1, 4, 1);
        b.real (a (ArpField::Accent), "arp_accent", "Arp Accent", A, 0.0f, 1.0f, 0.0f, fmt::percent);
        b.integer (a (ArpField::AccentEvery), "arp_accevery", "Arp Accent Every", A, 2, 8, 4);

        constexpr auto S = ParamGroup::Sequencer;
        auto s = [] (SeqField f) { return pid::seq (f); };
        b.toggle (s (SeqField::On), "seq_on", "Sequencer On", S, false);
        b.choice (s (SeqField::Division), "seq_rate", "Sequencer Rate", S, getTempoDivisionNames(), divisionIndex ("1/16"));
        b.integer (s (SeqField::Length), "seq_length", "Sequencer Length", S, 1, 32, 16);
        b.real (s (SeqField::Swing), "seq_swing", "Sequencer Swing", S, 0.0f, 0.75f, 0.0f, fmt::percent);
        b.integer (s (SeqField::Transpose), "seq_transpose", "Sequencer Transpose", S, -24, 24, 0, fmt::semis);
        b.choice (s (SeqField::Clock), "seq_clock", "Sequencer Clock", S, { "Follow Host", "Free Run" }, 1);
    }

    std::vector<ParamDef> buildDefs()
    {
        Builder b;
        b.defs.reserve ((size_t) pid::count);
        addOscillators (b);
        addFilterAndAmp (b);
        addEnvelopes (b);
        addLfos (b);
        addMatrix (b);
        addGlobal (b);
        addEffects (b);
        addArpAndSeq (b);
        jassert ((int) b.defs.size() == pid::count);
        return std::move (b.defs);
    }
} // namespace

const std::vector<ParamDef>& getParamDefs()
{
    static const std::vector<ParamDef> defs = buildDefs();
    return defs;
}

const ParamDef& getParamDef (int index)
{
    return getParamDefs()[(size_t) juce::jlimit (0, pid::count - 1, index)];
}

int findParamIndex (const juce::String& id)
{
    static const std::unordered_map<juce::String, int> lookup = []
    {
        std::unordered_map<juce::String, int> map;
        for (const auto& d : getParamDefs())
            map[d.id] = d.index;
        return map;
    }();

    const auto it = lookup.find (id);
    return it != lookup.end() ? it->second : -1;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    constexpr int kParameterVersion = 1;

    for (const auto& d : getParamDefs())
    {
        const juce::ParameterID pid { d.id, kParameterVersion };
        auto formatter = d.formatter;

        switch (d.type)
        {
            case ParamType::Float:
            {
                auto attributes = juce::AudioParameterFloatAttributes()
                                      .withLabel (d.unit)
                                      .withAutomatable (d.automatable)
                                      .withStringFromValueFunction ([formatter] (float v, int) { return formatter ? formatter (v) : juce::String (v, 3); });
                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, d.range, d.defaultValue, attributes));
                break;
            }
            case ParamType::Int:
            {
                auto attributes = juce::AudioParameterIntAttributes()
                                      .withAutomatable (d.automatable)
                                      .withStringFromValueFunction ([formatter] (int v, int) { return formatter ? formatter ((float) v) : juce::String (v); });
                layout.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.range.start, (int) d.range.end,
                                                                        (int) d.defaultValue, attributes));
                break;
            }
            case ParamType::Bool:
            {
                auto attributes = juce::AudioParameterBoolAttributes().withAutomatable (d.automatable);
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.defaultValue >= 0.5f, attributes));
                break;
            }
            case ParamType::Choice:
            {
                auto attributes = juce::AudioParameterChoiceAttributes().withAutomatable (d.automatable);
                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, d.choices, (int) d.defaultValue, attributes));
                break;
            }
        }
    }

    return layout;
}

} // namespace nedd
