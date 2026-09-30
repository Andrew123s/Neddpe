#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace nedd
{
/** Tempo-synced note divisions shared by LFOs, delay, arpeggiator, sequencer and S&H. Append only. */
struct TempoDivision
{
    const char* name;
    double beats;   // length in quarter notes
};

inline const std::array<TempoDivision, 18>& getTempoDivisions()
{
    static const std::array<TempoDivision, 18> divisions { {
        { "8/1",    32.0 },
        { "4/1",    16.0 },
        { "2/1",    8.0 },
        { "1/1",    4.0 },
        { "1/2",    2.0 },
        { "1/2T",   4.0 / 3.0 },
        { "1/4.",   1.5 },
        { "1/4",    1.0 },
        { "1/4T",   2.0 / 3.0 },
        { "1/8.",   0.75 },
        { "1/8",    0.5 },
        { "1/8T",   1.0 / 3.0 },
        { "1/16.",  0.375 },
        { "1/16",   0.25 },
        { "1/16T",  1.0 / 6.0 },
        { "1/32",   0.125 },
        { "1/32T",  1.0 / 12.0 },
        { "1/64",   0.0625 },
    } };
    return divisions;
}

inline double divisionToBeats (int index)
{
    const auto& divisions = getTempoDivisions();
    return divisions[(size_t) juce::jlimit (0, (int) divisions.size() - 1, index)].beats;
}

inline juce::StringArray getTempoDivisionNames()
{
    juce::StringArray names;
    for (const auto& d : getTempoDivisions())
        names.add (d.name);
    return names;
}

/** Index of a division by name, used when defining defaults. */
inline int divisionIndex (const char* name)
{
    const auto& divisions = getTempoDivisions();
    for (size_t i = 0; i < divisions.size(); ++i)
        if (juce::String (divisions[i].name) == name)
            return (int) i;
    jassertfalse;
    return 7;
}

/** Musical timing information for one processing block. */
struct TransportInfo
{
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double ppqAtBlockStart = 0.0;     // host position when playing, internal clock otherwise
    double beatsPerSample = 120.0 / 60.0 / 44100.0;
    bool hostPlaying = false;
    bool hostTransportAvailable = false;
    int numSamples = 0;

    double ppqAt (int sampleOffset) const noexcept { return ppqAtBlockStart + beatsPerSample * sampleOffset; }
};

} // namespace nedd
