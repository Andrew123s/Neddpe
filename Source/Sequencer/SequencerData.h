#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

namespace nedd
{
// =============================================================================================
// Step sequencer pattern
// =============================================================================================
struct SeqStep
{
    bool on = true;
    int note = 60;            // absolute MIDI note (transposed by the sequencer transpose parameter)
    float velocity = 0.8f;
    float gate = 0.5f;        // fraction of the step (0.05 .. 1.5, > 1 overlaps the next step)
    float slide = 0.0f;       // MPE slide (CC74) for the note, 0..1
    float pressure = 0.0f;    // MPE pressure for the note, 0..1
    float pitch = 0.0f;       // per-note bend target in semitones, reached at the end of the gate
    float probability = 1.0f;
    int ratchet = 1;          // 1..4 repeats inside the step
    bool accent = false;

    bool operator== (const SeqStep&) const = default;
};

struct SequencerPattern
{
    static constexpr int kMaxSteps = 32;
    std::array<SeqStep, (size_t) kMaxSteps> steps {};

    SequencerPattern();
    bool operator== (const SequencerPattern&) const = default;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);
};

// =============================================================================================
// Arpeggiator custom pattern
// =============================================================================================
struct ArpPattern
{
    static constexpr int kSteps = 16;
    /** -1 = rest, otherwise index into the sorted held notes (wraps into higher octaves). */
    std::array<int, (size_t) kSteps> steps { 0, 1, 2, 1, 0, 2, 3, -1, 0, 1, 2, 3, 4, 3, 2, 1 };

    bool operator== (const ArpPattern&) const = default;
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);
};

// =============================================================================================
// Expressive note clip (note editor + performance recorder)
// =============================================================================================
struct ExprPoint
{
    float time = 0.0f;    // beats since the note started
    float value = 0.0f;
};

using ExprCurve = std::vector<ExprPoint>;

/** Linear interpolation on a curve sorted by time; `fallback` when empty. */
float evaluateCurve (const ExprCurve& curve, float time, float fallback) noexcept;

struct ClipNote
{
    int uid = 0;                 // stable identity for editing and selection
    int noteNumber = 60;
    double start = 0.0;          // beats from the clip start
    double length = 1.0;         // beats
    float velocity = 0.8f;
    float releaseVelocity = 0.5f;
    ExprCurve pitch;             // semitones of per-note bend
    ExprCurve pressure;          // 0..1
    ExprCurve slide;             // 0..1

    double end() const noexcept { return start + length; }
};

struct NoteClip
{
    double lengthBeats = 16.0;
    std::vector<ClipNote> notes;
    int nextUid = 1;

    int addNote (ClipNote note);   // assigns a uid, returns it
    ClipNote* findNote (int uid);
    const ClipNote* findNote (int uid) const;
    void removeNote (int uid);
    void sortByStart();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);
};

/** Removes redundant points from a recorded curve (keeps shape within `tolerance`). */
void simplifyCurve (ExprCurve& curve, float tolerance);

} // namespace nedd
