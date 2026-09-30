#include "ModTypes.h"
#include "DSP/DspMath.h"

namespace nedd
{
namespace
{
    const std::array<ModSourceInfo, kNumModSources> sourceInfos { {
        { "None",             "--",    false, false },
        { "MPE Pitch",        "PITCH", true,  true },
        { "MPE Pressure",     "PRESS", false, true },
        { "MPE Slide",        "SLIDE", false, true },
        { "Velocity",         "VEL",   false, true },
        { "Release Velocity", "RVEL",  false, true },
        { "Mod Wheel",        "MW",    false, false },
        { "Pitch Bend",       "PB",    true,  false },
        { "Aftertouch",       "AT",    false, false },
        { "LFO 1",            "LFO1",  true,  true },
        { "LFO 2",            "LFO2",  true,  true },
        { "LFO 3",            "LFO3",  true,  true },
        { "Amp Env",          "AENV",  false, true },
        { "Filter Env",       "FENV",  false, true },
        { "Mod Env",          "MENV",  false, true },
        { "Random",           "RND",   true,  true },
        { "Sample & Hold",    "S&H",   true,  true },
        { "Key Position",     "KEY",   true,  true },
        { "Note Number",      "NOTE",  false, true },
        { "Gate",             "GATE",  false, true },
        { "Macro 1",          "M1",    false, false },
        { "Macro 2",          "M2",    false, false },
        { "Macro 3",          "M3",    false, false },
        { "Macro 4",          "M4",    false, false },
    } };

    const std::array<ModDestInfo, kNumModDests> destInfos { {
        { "None",               ModScope::Voice,  0.0f,  "" },
        { "Pitch",              ModScope::Voice,  48.0f, "st" },
        { "OSC 1 Pitch",        ModScope::Voice,  48.0f, "st" },
        { "OSC 2 Pitch",        ModScope::Voice,  48.0f, "st" },
        { "OSC 3 Pitch",        ModScope::Voice,  48.0f, "st" },
        { "OSC 1 WT Position",  ModScope::Voice,  1.0f,  "" },
        { "OSC 2 WT Position",  ModScope::Voice,  1.0f,  "" },
        { "OSC 3 WT Position",  ModScope::Voice,  1.0f,  "" },
        { "OSC 1 FM Amount",    ModScope::Voice,  1.0f,  "" },
        { "OSC 2 FM Amount",    ModScope::Voice,  1.0f,  "" },
        { "OSC 3 FM Amount",    ModScope::Voice,  1.0f,  "" },
        { "OSC 1 Pulse Width",  ModScope::Voice,  0.5f,  "" },
        { "OSC 2 Pulse Width",  ModScope::Voice,  0.5f,  "" },
        { "OSC 3 Pulse Width",  ModScope::Voice,  0.5f,  "" },
        { "OSC 1 Level",        ModScope::Voice,  1.0f,  "" },
        { "OSC 2 Level",        ModScope::Voice,  1.0f,  "" },
        { "OSC 3 Level",        ModScope::Voice,  1.0f,  "" },
        { "Unison Detune",      ModScope::Voice,  1.0f,  "" },
        { "Stereo Width",       ModScope::Voice,  1.0f,  "" },
        { "Filter Cutoff",      ModScope::Voice,  96.0f, "st" },
        { "Filter Resonance",   ModScope::Voice,  1.0f,  "" },
        { "Filter Drive",       ModScope::Voice,  1.0f,  "" },
        { "Filter Env Amount",  ModScope::Voice,  1.0f,  "" },
        { "Filter Mix",         ModScope::Voice,  1.0f,  "" },
        { "Amp Level",          ModScope::Voice,  1.0f,  "" },
        { "Pan",                ModScope::Voice,  1.0f,  "" },
        { "Amp Attack",         ModScope::Voice,  4.0f,  "oct" },
        { "Amp Decay",          ModScope::Voice,  4.0f,  "oct" },
        { "Amp Release",        ModScope::Voice,  4.0f,  "oct" },
        { "LFO 1 Rate",         ModScope::Voice,  4.0f,  "oct" },
        { "LFO 2 Rate",         ModScope::Voice,  4.0f,  "oct" },
        { "LFO 3 Rate",         ModScope::Voice,  4.0f,  "oct" },
        { "LFO 1 Depth",        ModScope::Voice,  1.0f,  "" },
        { "LFO 2 Depth",        ModScope::Voice,  1.0f,  "" },
        { "LFO 3 Depth",        ModScope::Voice,  1.0f,  "" },
        { "Delay Send",         ModScope::Voice,  1.0f,  "" },
        { "Reverb Send",        ModScope::Voice,  1.0f,  "" },
        { "Morph A/B",          ModScope::Voice,  1.0f,  "" },
        { "Distortion Drive",   ModScope::Global, 1.0f,  "" },
        { "Distortion Mix",     ModScope::Global, 1.0f,  "" },
        { "Saturation Drive",   ModScope::Global, 1.0f,  "" },
        { "Bitcrush Amount",    ModScope::Global, 1.0f,  "" },
        { "Chorus Mix",         ModScope::Global, 1.0f,  "" },
        { "Phaser Mix",         ModScope::Global, 1.0f,  "" },
        { "Flanger Mix",        ModScope::Global, 1.0f,  "" },
        { "Delay Feedback",     ModScope::Global, 1.0f,  "" },
        { "Delay Return",       ModScope::Global, 1.0f,  "" },
        { "Reverb Size",        ModScope::Global, 1.0f,  "" },
        { "Reverb Return",      ModScope::Global, 1.0f,  "" },
        { "Arp Gate",           ModScope::Global, 1.0f,  "" },
        { "Arp Probability",    ModScope::Global, 1.0f,  "" },
    } };
} // namespace

const ModSourceInfo& getModSourceInfo (ModSource source)
{
    return sourceInfos[(size_t) juce::jlimit (0, kNumModSources - 1, (int) source)];
}

const ModDestInfo& getModDestInfo (ModDest dest)
{
    return destInfos[(size_t) juce::jlimit (0, kNumModDests - 1, (int) dest)];
}

juce::StringArray getModSourceNames()
{
    juce::StringArray names;
    for (const auto& info : sourceInfos)
        names.add (info.name);
    return names;
}

juce::StringArray getModDestNames()
{
    juce::StringArray names;
    for (const auto& info : destInfos)
        names.add (info.name);
    return names;
}

juce::StringArray getModCurveNames() { return { "Linear", "Exponential", "Logarithmic", "S-Curve", "Stepped" }; }
juce::StringArray getModPolarityNames() { return { "Unipolar", "Bipolar" }; }

float shapeModValue (float value, bool sourceIsBipolar, ModCurve curve, ModPolarity polarity) noexcept
{
    // Work in the source's natural domain first, then convert polarity.
    float magnitude = std::abs (value);
    const float sign = value < 0.0f ? -1.0f : 1.0f;

    switch (curve)
    {
        case ModCurve::Exponential: magnitude = magnitude * magnitude; break;
        case ModCurve::Logarithmic: magnitude = std::sqrt (magnitude); break;
        case ModCurve::SCurve:      magnitude = magnitude * magnitude * (3.0f - 2.0f * magnitude); break;
        case ModCurve::Stepped:     magnitude = std::floor (magnitude * 8.0f + 1.0e-4f) / 8.0f; break;
        case ModCurve::Linear:
        case ModCurve::Count:       break;
    }

    float shaped = sign * magnitude;

    if (polarity == ModPolarity::Bipolar && ! sourceIsBipolar)
        shaped = shaped * 2.0f - 1.0f;
    else if (polarity == ModPolarity::Unipolar && sourceIsBipolar)
        shaped = shaped * 0.5f + 0.5f;

    return shaped;
}

} // namespace nedd
