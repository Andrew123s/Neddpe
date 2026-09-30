#include "Tuning.h"
#include <cmath>

namespace nedd
{
juce::String TuningData::loadScala (const juce::String& fileContents, int rootKey)
{
    juce::StringArray lines;
    for (auto line : juce::StringArray::fromLines (fileContents))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('!'))
            continue;
        lines.add (line);
    }

    if (lines.size() < 2)
        return "Not a Scala file: missing description or note count";

    const juce::String description = lines[0];
    const int count = lines[1].getIntValue();
    if (count <= 0 || count > 1024 || lines.size() < count + 2)
        return "Scala file declares " + juce::String (count) + " notes but provides " + juce::String (lines.size() - 2);

    // Each degree in cents relative to 1/1. The last entry is the period (usually 2/1).
    std::vector<double> cents;
    for (int i = 0; i < count; ++i)
    {
        const auto token = lines[i + 2].upToFirstOccurrenceOf (" ", false, false).trim();
        double c = 0.0;

        if (token.containsChar ('.'))
        {
            c = token.getDoubleValue();
        }
        else if (token.containsChar ('/'))
        {
            const double num = token.upToFirstOccurrenceOf ("/", false, false).getDoubleValue();
            const double den = token.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            if (num <= 0.0 || den <= 0.0)
                return "Invalid ratio: " + token;
            c = 1200.0 * std::log2 (num / den);
        }
        else
        {
            const double ratio = token.getDoubleValue();
            if (ratio <= 0.0)
                return "Invalid degree: " + token;
            c = 1200.0 * std::log2 (ratio);
        }

        cents.push_back (c);
    }

    const double period = cents.back();
    if (period <= 0.0)
        return "Scale period must be positive";

    rootKey = juce::jlimit (0, 127, rootKey);
    for (int key = 0; key < 128; ++key)
    {
        const int steps = key - rootKey;
        const int size = (int) cents.size();
        const int octave = (int) std::floor ((double) steps / size);
        const int degree = steps - octave * size;
        const double degreeCents = degree == 0 ? 0.0 : cents[(size_t) degree - 1];
        pitch[(size_t) key] = (float) (rootKey + (octave * period + degreeCents) / 100.0);
    }

    tuningName = description.isNotEmpty() ? description : juce::String ("Scala scale");
    return {};
}

juce::ValueTree TuningData::toValueTree() const
{
    juce::ValueTree tree ("Tuning");
    tree.setProperty ("name", tuningName, nullptr);

    juce::StringArray pitches;
    for (auto p : pitch)
        pitches.add (juce::String (p, 5));
    tree.setProperty ("pitch", pitches.joinIntoString (" "), nullptr);

    juce::String mask;
    for (auto degree : userScale)
        mask << (degree ? "1" : "0");
    tree.setProperty ("userScale", mask, nullptr);
    return tree;
}

void TuningData::fromValueTree (const juce::ValueTree& tree)
{
    resetTo12Tet();
    if (! tree.isValid())
        return;

    tuningName = tree.getProperty ("name", "12-TET").toString();

    const auto pitches = juce::StringArray::fromTokens (tree.getProperty ("pitch").toString(), " ", "");
    if (pitches.size() == 128)
        for (int i = 0; i < 128; ++i)
            pitch[(size_t) i] = pitches[i].getFloatValue();

    const auto mask = tree.getProperty ("userScale").toString();
    if (mask.length() == 12)
        for (int i = 0; i < 12; ++i)
            userScale[(size_t) i] = mask[i] == '1';
}

std::array<bool, 12> Scales::mask (int scaleIndex, const std::array<bool, 12>& userScale) noexcept
{
    static const int intervals[][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 },   // Chromatic
        { 1,0,1,0,1,1,0,1,0,1,0,1 },   // Major
        { 1,0,1,1,0,1,0,1,1,0,1,0 },   // Natural minor
        { 1,0,1,1,0,1,0,1,1,0,0,1 },   // Harmonic minor
        { 1,0,1,1,0,1,0,1,0,1,1,0 },   // Dorian
        { 1,1,0,1,0,1,0,1,1,0,1,0 },   // Phrygian
        { 1,0,1,0,1,0,1,1,0,1,0,1 },   // Lydian
        { 1,0,1,0,1,1,0,1,0,1,1,0 },   // Mixolydian
        { 1,0,1,0,1,0,0,1,0,1,0,0 },   // Major pentatonic
        { 1,0,0,1,0,1,0,1,0,0,1,0 },   // Minor pentatonic
        { 1,0,0,1,0,1,1,1,0,0,1,0 },   // Blues
        { 1,0,1,0,1,0,1,0,1,0,1,0 },   // Whole tone
        { 1,0,0,0,0,0,0,0,0,0,0,0 },   // Octave
    };
    constexpr int numBuiltIn = (int) (sizeof (intervals) / sizeof (intervals[0]));

    if (scaleIndex >= numBuiltIn)
        return userScale;

    std::array<bool, 12> result {};
    for (int i = 0; i < 12; ++i)
        result[(size_t) i] = intervals[juce::jmax (0, scaleIndex)][i] != 0;
    return result;
}

int Scales::snapNote (int note, const std::array<bool, 12>& mask, int root) noexcept
{
    for (int distance = 0; distance < 12; ++distance)
    {
        for (int sign : { -1, 1 })
        {
            const int candidate = note + sign * distance;
            const int degree = ((candidate - root) % 12 + 12) % 12;
            if (mask[(size_t) degree])
                return juce::jlimit (0, 127, candidate);
        }
    }
    return note;
}

float Scales::nearestScalePitch (float pitch, const std::array<bool, 12>& mask, int root) noexcept
{
    const int below = (int) std::floor (pitch);
    float best = pitch;
    float bestDistance = 1.0e9f;

    for (int candidate = below - 6; candidate <= below + 7; ++candidate)
    {
        const int degree = ((candidate - root) % 12 + 12) % 12;
        if (! mask[(size_t) degree])
            continue;
        const float d = std::abs ((float) candidate - pitch);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = (float) candidate;
        }
    }
    return best;
}

} // namespace nedd
