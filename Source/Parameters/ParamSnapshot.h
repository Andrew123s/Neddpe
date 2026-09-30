#pragma once

#include "ParameterDefs.h"
#include <array>
#include <atomic>

namespace nedd
{
/**
    Plain (denormalised) values of every parameter for one processing block.
    Choice and bool parameters hold their index / 0-1 value as a float.
*/
struct ParamSnapshot
{
    std::array<float, (size_t) pid::count> values {};

    float operator[] (int index) const noexcept { return values[(size_t) index]; }
    float& operator[] (int index) noexcept { return values[(size_t) index]; }

    int getInt (int index) const noexcept { return (int) std::lround (values[(size_t) index]); }
    bool getBool (int index) const noexcept { return values[(size_t) index] >= 0.5f; }

    template <typename Enum>
    Enum getChoice (int index) const noexcept { return (Enum) getInt (index); }

    void setToDefaults()
    {
        for (const auto& d : getParamDefs())
            values[(size_t) d.index] = d.defaultValue;
    }
};

/** Normalised (0..1) values of every parameter, used for morph targets and patch mutation. */
using NormalisedPatch = std::array<float, (size_t) pid::count>;

/** Caches the APVTS raw-value atomics so the audio thread can build a snapshot without lookups. */
class ParamReader
{
public:
    void attach (juce::AudioProcessorValueTreeState& state)
    {
        for (const auto& d : getParamDefs())
        {
            sources[(size_t) d.index] = state.getRawParameterValue (d.id);
            jassert (sources[(size_t) d.index] != nullptr);
        }
    }

    void read (ParamSnapshot& snapshot) const noexcept
    {
        for (size_t i = 0; i < sources.size(); ++i)
            snapshot.values[i] = sources[i] != nullptr ? sources[i]->load (std::memory_order_relaxed) : 0.0f;
    }

private:
    std::array<std::atomic<float>*, (size_t) pid::count> sources {};
};

} // namespace nedd
