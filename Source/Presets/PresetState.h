#pragma once

#include "DSP/Lfo.h"
#include "Parameters/ParamSnapshot.h"
#include "Sequencer/SequencerData.h"
#include "Synth/OscillatorAssets.h"

namespace nedd
{
/**
    Everything that defines a sound: all parameters plus the structured sound data
    (macro names, LFO curves, sequencer and arp patterns, the morph B target, and the
    wavetables and samples imported into the oscillators).

    This is the single serialisation path: presets, DAW project state and undo all go
    through toValueTree() / fromValueTree(). Missing values fall back to defaults, so older
    presets keep loading as parameters are added.
*/
struct PresetState
{
    juce::String name { "Init" };
    juce::String category { "User" };
    juce::String author;
    juce::String description;

    ParamSnapshot params;                 // plain values
    std::array<juce::String, (size_t) kNumMacros> macroNames { "MOVEMENT", "TONE", "SPACE", "DRIVE" };
    dsp::LfoCustomShapes lfoShapes;
    SequencerPattern pattern;
    ArpPattern arpPattern;
    bool hasMorphTarget = false;
    ParamSnapshot morphTarget;
    OscillatorAssets assets;

    PresetState()
    {
        params.setToDefaults();
        morphTarget.setToDefaults();
    }

    void set (int index, float plainValue) { params[index] = plainValue; }

    static constexpr int kFormatVersion = 1;

    juce::ValueTree toValueTree() const;
    static PresetState fromValueTree (const juce::ValueTree& tree);
};

/** Preset categories shown in the browser (order = display order). */
juce::StringArray getPresetCategories();

} // namespace nedd
