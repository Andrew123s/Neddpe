#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>

namespace nedd
{
/**
    Key-to-pitch mapping and scale data. Published to the audio thread as an immutable object.

    pitch[key] is the pitch of MIDI key `key`, expressed in fractional 12-TET semitones
    (so 69.0 = 440 Hz). The default table is plain 12-TET; a Scala (.scl) file replaces it.
*/
struct TuningData
{
    std::array<float, 128> pitch {};
    juce::String tuningName { "12-TET" };
    std::array<bool, 12> userScale { true, false, true, false, true, true, false, true, false, true, false, true };

    TuningData() { resetTo12Tet(); }

    void resetTo12Tet()
    {
        for (int i = 0; i < 128; ++i)
            pitch[(size_t) i] = (float) i;
        tuningName = "12-TET";
    }

    /**
        Loads a Scala scale. The scale's 1/1 is mapped to rootKey at rootKey's 12-TET pitch.
        Returns an error message, or an empty string on success.
    */
    juce::String loadScala (const juce::String& fileContents, int rootKey = 60);

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);
};

/** Scale masks and quantisation helpers. */
struct Scales
{
    /** Degree mask for a scale index (matching getScaleNames()); 'User' uses userScale. */
    static std::array<bool, 12> mask (int scaleIndex, const std::array<bool, 12>& userScale) noexcept;

    /** Snaps a MIDI note to the nearest scale degree (ties go down). Chromatic returns the note. */
    static int snapNote (int note, const std::array<bool, 12>& mask, int root) noexcept;

    /** Nearest in-scale pitch (in semitones) to a continuous pitch. */
    static float nearestScalePitch (float pitch, const std::array<bool, 12>& mask, int root) noexcept;
};

} // namespace nedd
