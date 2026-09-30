#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Modulation/ModTypes.h"

namespace nedd
{
/*
    Central parameter definition system.

    Every host-visible parameter is declared exactly once in ParameterDefs.cpp. Parameters are
    addressed on the audio thread by a dense integer index computed from the field enums below,
    and by a stable string ID in the host and in saved state.

    STABILITY RULES
      - Never rename a string ID once released: DAW projects store automation against it.
      - The integer index layout may change between versions (it is never persisted).
      - Choice lists may only be appended to; reordering breaks saved projects.
*/

enum class ParamType { Float, Int, Bool, Choice };

enum class OscField : int
{
    On, Engine, Wave, PulseWidth, Table, WtPos, Octave, Semi, Fine, Phase, PhaseRandom,
    Level, Pan, Unison, Detune, Spread, FmSource, FmAmount, Sync, Ring, Route, NoiseType,
    FmAlgorithm, Op1Ratio, Op2Ratio, OpFine, Op1Amount, Op2Amount, FmFeedback, FmEnvAmount, FmKeyTrack,
    SampleRoot, SampleLoop, GrainSize, GrainDensity, GrainSpray, GrainPitchSpray,
    Count
};

enum class FilterField : int { On, Type, Cutoff, Resonance, Drive, KeyTrack, EnvAmount, Velocity, Mix, Count };

enum class AmpField : int { Level, Velocity, Pressure, Pan, Count };

enum class EnvField : int { Delay, Attack, Hold, Decay, Sustain, Release, AttackCurve, DecayCurve, ReleaseCurve, Count };

enum class LfoField : int { Shape, Rate, Sync, Division, Phase, Fade, Retrigger, Amount, Count };

enum class ModSlotField : int { Source, Dest, Amount, Curve, Polarity, Count };

enum class GlobalField : int
{
    MasterVolume, Quality,
    VoiceMode, Polyphony, Glide, GlideMode,
    MpeMode, MpeBendRange, MasterBendRange, PitchSensitivity,
    VelocityCurve, PressureCurve, SlideCurve, ExpressionSmoothing,
    ScaleType, ScaleRoot, BendQuantize, CustomTuning,
    SampleHoldDivision,
    MorphOn, MorphPosition,
    MidiOut,
    OscOversampling,
    Count
};

enum class FxField : int
{
    DistOn, DistType, DistDrive, DistTone, DistMix,
    SatOn, SatDrive, SatWarmth, SatMix,
    CrushOn, CrushBits, CrushDownsample, CrushMix,
    ChorusOn, ChorusRate, ChorusDepth, ChorusMix,
    PhaserOn, PhaserRate, PhaserDepth, PhaserFeedback, PhaserMix,
    FlangerOn, FlangerRate, FlangerDepth, FlangerFeedback, FlangerMix,
    DelayOn, DelaySync, DelayTime, DelayDivision, DelayFeedback, DelayDamping, DelayPingPong, DelaySend, DelayReturn,
    ReverbOn, ReverbSize, ReverbDamping, ReverbPredelay, ReverbWidth, ReverbSend, ReverbReturn,
    EqOn, EqLowGain, EqMidFreq, EqMidGain, EqHighGain,
    CompOn, CompThreshold, CompRatio, CompAttack, CompRelease, CompMakeup,
    LimiterOn,
    Count
};

enum class ArpField : int
{
    On, Mode, Division, Gate, Swing, Octaves, Length, VelocityMode, Velocity, Probability, Ratchet, Repeat, Accent, AccentEvery,
    Count
};

enum class SeqField : int { On, Division, Length, Swing, Transpose, Clock, Count };

namespace pid
{
    constexpr int oscCount = (int) OscField::Count;
    constexpr int oscBase = 0;
    constexpr int filterBase = oscBase + kNumOscillators * oscCount;
    constexpr int ampBase = filterBase + (int) FilterField::Count;
    constexpr int envBase = ampBase + (int) AmpField::Count;
    constexpr int lfoBase = envBase + kNumEnvelopes * (int) EnvField::Count;
    constexpr int modBase = lfoBase + kNumLfos * (int) LfoField::Count;
    constexpr int macroBase = modBase + kNumModSlots * (int) ModSlotField::Count;
    constexpr int globalBase = macroBase + kNumMacros;
    constexpr int fxBase = globalBase + (int) GlobalField::Count;
    constexpr int arpBase = fxBase + (int) FxField::Count;
    constexpr int seqBase = arpBase + (int) ArpField::Count;
    constexpr int count = seqBase + (int) SeqField::Count;

    constexpr int osc (int index, OscField f) { return oscBase + index * oscCount + (int) f; }
    constexpr int filter (FilterField f) { return filterBase + (int) f; }
    constexpr int amp (AmpField f) { return ampBase + (int) f; }
    constexpr int env (int index, EnvField f) { return envBase + index * (int) EnvField::Count + (int) f; }
    constexpr int lfo (int index, LfoField f) { return lfoBase + index * (int) LfoField::Count + (int) f; }
    constexpr int mod (int slot, ModSlotField f) { return modBase + slot * (int) ModSlotField::Count + (int) f; }
    constexpr int macro (int index) { return macroBase + index; }
    constexpr int global (GlobalField f) { return globalBase + (int) f; }
    constexpr int fx (FxField f) { return fxBase + (int) f; }
    constexpr int arp (ArpField f) { return arpBase + (int) f; }
    constexpr int seq (SeqField f) { return seqBase + (int) f; }

    constexpr int ampEnv = 0;
    constexpr int filterEnv = 1;
    constexpr int modEnv = 2;
} // namespace pid

enum class ParamGroup
{
    Oscillator, Filter, Amp, Envelope, Lfo, Matrix, Macro, Voice, Mpe, Tuning, Morph, Master, Effects, Arp, Sequencer
};

struct ParamDef
{
    int index = 0;
    juce::String id;
    juce::String name;
    ParamGroup group = ParamGroup::Master;
    ParamType type = ParamType::Float;
    juce::NormalisableRange<float> range { 0.0f, 1.0f };
    float defaultValue = 0.0f;
    juce::String unit;
    juce::StringArray choices;
    bool automatable = true;
    /** Included in A/B morphing and patch mutation (continuous sound-shaping parameters). */
    bool morphable = false;
    /** Evaluated per voice (true) or globally (false). Used by per-note morphing. */
    bool perVoice = false;
    std::function<juce::String (float)> formatter;

    float getDefaultNormalised() const { return range.convertTo0to1 (defaultValue); }
};

/** All parameter definitions, ordered by index. */
const std::vector<ParamDef>& getParamDefs();

const ParamDef& getParamDef (int index);

/** Index lookup by string ID; returns -1 if unknown. */
int findParamIndex (const juce::String& id);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Choice lists shared with the UI. */
juce::StringArray getOscEngineNames();
/** Wavetable choice list: the factory bank followed by "Imported" (the oscillator's own imported table). */
juce::StringArray getWavetableChoiceNames();
/** Index of "Imported" in the wavetable choice list = number of factory wavetables (checked by WavetableBank). */
constexpr int kImportedWavetableChoice = 10;
constexpr int importedWavetableChoice() noexcept { return kImportedWavetableChoice; }
juce::StringArray getAnalogWaveNames();
juce::StringArray getNoiseTypeNames();
juce::StringArray getFmAlgorithmNames();
juce::StringArray getFmRatioNames();
float fmRatioValue (int index);
juce::StringArray getFilterTypeNames();
juce::StringArray getLfoShapeNames();
juce::StringArray getScaleNames();

enum class OscEngine : int { Analog = 0, Wavetable, FM, Noise, Granular, Sample };
enum class OscOversampling : int { Off = 0, Auto, X2, X4 };
enum class AnalogWave : int { Sine = 0, Triangle, Saw, Square, Pulse };
enum class NoiseType : int { White = 0, Pink, Brown, Crackle, Digital };
enum class FmAlgorithm : int { Stack = 0, Parallel, Branch };
enum class FilterType : int { LowPass12 = 0, LowPass24, HighPass12, HighPass24, BandPass, Notch, Ladder };
enum class LfoShape : int { Sine = 0, Triangle, Saw, ReverseSaw, Square, SampleHold, SmoothRandom, Custom };
enum class VoiceMode : int { Poly = 0, Mono, Legato };
enum class GlideMode : int { Off = 0, Always, Legato };
enum class MpeMode : int { Legacy = 0, LowerZone, UpperZone, BothZones };
enum class Quality : int { Eco = 0, Normal, High, Ultra };
enum class DistortionType : int { Soft = 0, Hard, Fold, Asymmetric, Tube };
enum class ArpMode : int { Up = 0, Down, UpDown, Random, Order, Chord, Custom };
enum class ArpVelocityMode : int { AsPlayed = 0, Fixed };
enum class SeqClock : int { FollowHost = 0, FreeRun };

} // namespace nedd
