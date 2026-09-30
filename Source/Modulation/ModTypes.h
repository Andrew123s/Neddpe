#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace nedd
{
constexpr int kNumOscillators = 3;
constexpr int kNumEnvelopes = 3;
constexpr int kNumLfos = 3;
constexpr int kNumModSlots = 16;
constexpr int kNumMacros = 4;

/** Modulation sources. The numeric order is stored in presets: append only, never reorder. */
enum class ModSource : int
{
    None = 0,
    MpePitch,        // per-note pitch offset (semitones / 48), bipolar
    MpePressure,     // per-note pressure, unipolar
    MpeSlide,        // per-note CC74 / timbre, unipolar
    Velocity,
    ReleaseVelocity,
    ModWheel,
    PitchBend,       // master / legacy pitch wheel, bipolar
    Aftertouch,      // master / legacy channel pressure
    Lfo1, Lfo2, Lfo3,
    AmpEnv, FilterEnv, ModEnv,
    Random,          // fixed random value per note, bipolar
    SampleHold,      // stepped random clocked by the S&H division, bipolar
    KeyPosition,     // (note - 60) / 60, bipolar
    NoteNumber,      // note / 127
    Gate,
    Macro1, Macro2, Macro3, Macro4,
    Count
};

/** Scope of a destination: per-voice destinations are evaluated for every note independently. */
enum class ModScope { Voice, Global };

/** Modulation destinations. Stored in presets by index: append only. */
enum class ModDest : int
{
    None = 0,
    Pitch,
    Osc1Pitch, Osc2Pitch, Osc3Pitch,
    Osc1WtPos, Osc2WtPos, Osc3WtPos,
    Osc1Fm, Osc2Fm, Osc3Fm,
    Osc1Pw, Osc2Pw, Osc3Pw,
    Osc1Level, Osc2Level, Osc3Level,
    UnisonDetune,
    StereoWidth,
    FilterCutoff,
    FilterResonance,
    FilterDrive,
    FilterEnvAmount,
    FilterMix,
    AmpLevel,
    Pan,
    AmpAttack, AmpDecay, AmpRelease,
    Lfo1Rate, Lfo2Rate, Lfo3Rate,
    Lfo1Depth, Lfo2Depth, Lfo3Depth,
    DelaySend,
    ReverbSend,
    Morph,
    // ---- global destinations (evaluated once per block) ----
    DistDrive,
    DistMix,
    SatDrive,
    CrushAmount,
    ChorusMix,
    PhaserMix,
    FlangerMix,
    DelayFeedback,
    DelayMix,
    ReverbSize,
    ReverbMix,
    ArpGate,
    ArpProbability,
    Count
};

constexpr int kNumModSources = (int) ModSource::Count;
constexpr int kNumModDests = (int) ModDest::Count;

enum class ModCurve : int { Linear = 0, Exponential, Logarithmic, SCurve, Stepped, Count };
enum class ModPolarity : int { Unipolar = 0, Bipolar, Count };

struct ModSourceInfo
{
    const char* name;
    const char* shortName;
    bool bipolar;      // natural range is -1..1
    bool perVoice;     // differs between notes
};

struct ModDestInfo
{
    const char* name;
    ModScope scope;
    float range;       // amount +100% adds this much (in the destination's own units)
    const char* unit;
};

const ModSourceInfo& getModSourceInfo (ModSource source);
const ModDestInfo& getModDestInfo (ModDest dest);

juce::StringArray getModSourceNames();
juce::StringArray getModDestNames();
juce::StringArray getModCurveNames();
juce::StringArray getModPolarityNames();

/** Applies a route's curve and polarity conversion to a source value. */
float shapeModValue (float sourceValue, bool sourceIsBipolar, ModCurve curve, ModPolarity polarity) noexcept;

inline ModDest oscDest (ModDest first, int oscIndex) noexcept { return (ModDest) ((int) first + oscIndex); }

} // namespace nedd
