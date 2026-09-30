#pragma once

#include "PresetState.h"

namespace nedd
{
/**
    Musically constrained patch generation.

    Randomisation never draws parameters uniformly: every section uses weighted choices and
    ranges that produce playable sounds (sensible envelope styles, filter ranges, unison sizes),
    always gives MPE pressure and slide a job, and never touches the performance setup
    (MPE zone, bend ranges, polyphony, quality, master volume).

    Mutation nudges an existing patch: continuous parameters move by a Gaussian step in their
    normalised range, discrete ones flip only occasionally.
*/
class PatchRandomizer
{
public:
    enum class Mode : int { Full = 0, Oscillators, Filter, Modulation, Mpe, Effects, Texture };

    static juce::StringArray getModeNames();

    static PresetState randomise (const PresetState& current, Mode mode, uint32_t seed);
    static PresetState mutate (const PresetState& current, float amount, uint32_t seed);
};

/** A short history of generated patches so the user can step back through them. */
struct MutationHistory
{
    static constexpr int kMaxEntries = 24;
    std::vector<PresetState> entries;
    int current = -1;

    void push (const PresetState& state)
    {
        if (current >= 0 && current < (int) entries.size() - 1)
            entries.erase (entries.begin() + current + 1, entries.end());
        entries.push_back (state);
        if ((int) entries.size() > kMaxEntries)
            entries.erase (entries.begin());
        current = (int) entries.size() - 1;
    }

    void clear() { entries.clear(); current = -1; }
};

} // namespace nedd
