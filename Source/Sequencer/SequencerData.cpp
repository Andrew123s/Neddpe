#include "SequencerData.h"
#include <algorithm>

namespace nedd
{
namespace
{
    juce::String curveToString (const ExprCurve& curve)
    {
        juce::String s;
        for (const auto& p : curve)
            s << juce::String (p.time, 4) << ":" << juce::String (p.value, 4) << ";";
        return s;
    }

    ExprCurve curveFromString (const juce::String& s)
    {
        ExprCurve curve;
        for (const auto& token : juce::StringArray::fromTokens (s, ";", ""))
        {
            if (token.isEmpty())
                continue;
            curve.push_back ({ token.upToFirstOccurrenceOf (":", false, false).getFloatValue(),
                               token.fromFirstOccurrenceOf (":", false, false).getFloatValue() });
        }
        std::sort (curve.begin(), curve.end(), [] (const ExprPoint& a, const ExprPoint& b) { return a.time < b.time; });
        return curve;
    }
} // namespace

// ---------------------------------------------------------------------------------------------
SequencerPattern::SequencerPattern()
{
    // A playable default: a minor figure with some expression so the sequencer demonstrates MPE.
    const int notes[] = { 48, 55, 60, 63, 48, 58, 60, 67, 46, 53, 58, 62, 46, 55, 58, 65 };
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& s = steps[(size_t) i];
        s.note = notes[i % 16];
        s.velocity = i % 4 == 0 ? 0.9f : 0.7f;
        s.gate = i % 8 == 7 ? 0.9f : 0.45f;
        s.slide = 0.2f + 0.6f * (float) (i % 8) / 7.0f;
        s.pressure = i % 4 == 0 ? 0.6f : 0.25f;
        s.pitch = i % 8 == 7 ? 2.0f : 0.0f;
        s.accent = i % 4 == 0;
    }
}

juce::ValueTree SequencerPattern::toValueTree() const
{
    juce::ValueTree tree ("Sequencer");
    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto& s = steps[(size_t) i];
        juce::ValueTree step ("Step");
        step.setProperty ("i", i, nullptr);
        step.setProperty ("on", s.on, nullptr);
        step.setProperty ("note", s.note, nullptr);
        step.setProperty ("vel", s.velocity, nullptr);
        step.setProperty ("gate", s.gate, nullptr);
        step.setProperty ("slide", s.slide, nullptr);
        step.setProperty ("press", s.pressure, nullptr);
        step.setProperty ("pitch", s.pitch, nullptr);
        step.setProperty ("prob", s.probability, nullptr);
        step.setProperty ("ratchet", s.ratchet, nullptr);
        step.setProperty ("accent", s.accent, nullptr);
        tree.appendChild (step, nullptr);
    }
    return tree;
}

void SequencerPattern::fromValueTree (const juce::ValueTree& tree)
{
    *this = SequencerPattern();
    if (! tree.isValid())
        return;

    for (const auto& step : tree)
    {
        const int i = step.getProperty ("i", -1);
        if (i < 0 || i >= kMaxSteps)
            continue;
        auto& s = steps[(size_t) i];
        s.on = step.getProperty ("on", s.on);
        s.note = juce::jlimit (0, 127, (int) step.getProperty ("note", s.note));
        s.velocity = juce::jlimit (0.0f, 1.0f, (float) step.getProperty ("vel", s.velocity));
        s.gate = juce::jlimit (0.05f, 1.5f, (float) step.getProperty ("gate", s.gate));
        s.slide = juce::jlimit (0.0f, 1.0f, (float) step.getProperty ("slide", s.slide));
        s.pressure = juce::jlimit (0.0f, 1.0f, (float) step.getProperty ("press", s.pressure));
        s.pitch = juce::jlimit (-24.0f, 24.0f, (float) step.getProperty ("pitch", s.pitch));
        s.probability = juce::jlimit (0.0f, 1.0f, (float) step.getProperty ("prob", s.probability));
        s.ratchet = juce::jlimit (1, 4, (int) step.getProperty ("ratchet", s.ratchet));
        s.accent = step.getProperty ("accent", s.accent);
    }
}

// ---------------------------------------------------------------------------------------------
juce::ValueTree ArpPattern::toValueTree() const
{
    juce::StringArray values;
    for (auto v : steps)
        values.add (juce::String (v));
    juce::ValueTree tree ("ArpPattern");
    tree.setProperty ("steps", values.joinIntoString (" "), nullptr);
    return tree;
}

void ArpPattern::fromValueTree (const juce::ValueTree& tree)
{
    *this = ArpPattern();
    const auto values = juce::StringArray::fromTokens (tree.getProperty ("steps").toString(), " ", "");
    for (int i = 0; i < kSteps && i < values.size(); ++i)
        steps[(size_t) i] = juce::jlimit (-1, 15, values[i].getIntValue());
}

// ---------------------------------------------------------------------------------------------
float evaluateCurve (const ExprCurve& curve, float time, float fallback) noexcept
{
    if (curve.empty())
        return fallback;
    if (time <= curve.front().time)
        return curve.front().value;
    if (time >= curve.back().time)
        return curve.back().value;

    const auto it = std::upper_bound (curve.begin(), curve.end(), time, [] (float t, const ExprPoint& p) { return t < p.time; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    const float span = std::max (1.0e-6f, b.time - a.time);
    return a.value + (b.value - a.value) * (time - a.time) / span;
}

void simplifyCurve (ExprCurve& curve, float tolerance)
{
    if (curve.size() < 3)
        return;

    // Drop points that lie within `tolerance` of the line between their neighbours.
    ExprCurve result;
    result.push_back (curve.front());
    for (size_t i = 1; i + 1 < curve.size(); ++i)
    {
        const auto& a = result.back();
        const auto& p = curve[i];
        const auto& b = curve[i + 1];
        const float span = std::max (1.0e-6f, b.time - a.time);
        const float predicted = a.value + (b.value - a.value) * (p.time - a.time) / span;
        if (std::abs (predicted - p.value) > tolerance)
            result.push_back (p);
    }
    result.push_back (curve.back());
    curve = std::move (result);
}

int NoteClip::addNote (ClipNote note)
{
    note.uid = nextUid++;
    notes.push_back (std::move (note));
    return notes.back().uid;
}

ClipNote* NoteClip::findNote (int uid)
{
    for (auto& n : notes)
        if (n.uid == uid)
            return &n;
    return nullptr;
}

const ClipNote* NoteClip::findNote (int uid) const
{
    for (const auto& n : notes)
        if (n.uid == uid)
            return &n;
    return nullptr;
}

void NoteClip::removeNote (int uid)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(), [uid] (const ClipNote& n) { return n.uid == uid; }), notes.end());
}

void NoteClip::sortByStart()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const ClipNote& a, const ClipNote& b) { return a.start < b.start; });
}

juce::ValueTree NoteClip::toValueTree() const
{
    juce::ValueTree tree ("Clip");
    tree.setProperty ("length", lengthBeats, nullptr);
    for (const auto& n : notes)
    {
        juce::ValueTree node ("Note");
        node.setProperty ("note", n.noteNumber, nullptr);
        node.setProperty ("start", n.start, nullptr);
        node.setProperty ("len", n.length, nullptr);
        node.setProperty ("vel", n.velocity, nullptr);
        node.setProperty ("rvel", n.releaseVelocity, nullptr);
        node.setProperty ("pitch", curveToString (n.pitch), nullptr);
        node.setProperty ("press", curveToString (n.pressure), nullptr);
        node.setProperty ("slide", curveToString (n.slide), nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void NoteClip::fromValueTree (const juce::ValueTree& tree)
{
    *this = NoteClip();
    if (! tree.isValid())
        return;

    lengthBeats = juce::jlimit (1.0, 1024.0, (double) tree.getProperty ("length", 16.0));
    for (const auto& node : tree)
    {
        ClipNote n;
        n.noteNumber = juce::jlimit (0, 127, (int) node.getProperty ("note", 60));
        n.start = std::max (0.0, (double) node.getProperty ("start", 0.0));
        n.length = std::max (1.0 / 64.0, (double) node.getProperty ("len", 1.0));
        n.velocity = juce::jlimit (0.0f, 1.0f, (float) node.getProperty ("vel", 0.8f));
        n.releaseVelocity = juce::jlimit (0.0f, 1.0f, (float) node.getProperty ("rvel", 0.5f));
        n.pitch = curveFromString (node.getProperty ("pitch").toString());
        n.pressure = curveFromString (node.getProperty ("press").toString());
        n.slide = curveFromString (node.getProperty ("slide").toString());
        addNote (std::move (n));
    }
    sortByStart();
}

} // namespace nedd
