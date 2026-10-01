#pragma once

#include "Generators.h"
#include <map>

namespace nedd
{
/**
    Builds clip notes from live events recorded on the audio thread (message thread only).
    Expression streams become per-note curves, thinned to the points that matter.
*/
class PerformanceRecorder
{
public:
    /** keepExisting = overdub: new notes are added to the clip's notes. */
    void begin (const NoteClip& base, bool keepExisting, bool looping);
    void consume (const RecordedEvent& e);
    NoteClip finish (double endBeat);

    bool isActive() const noexcept { return active; }

    /** What has been recorded so far, with held notes drawn up to `nowBeat` (for live display). */
    NoteClip preview (double nowBeat) const;
    int getOpenNoteCount() const noexcept { return (int) open.size(); }

private:
    double timeSince (double start, double beat) const noexcept;
    void close (ClipNote note, double endBeat, float releaseVelocity);

    NoteClip clip;
    bool active = false;
    bool looping = false;
    bool lengthFixed = false;
    std::map<uint32_t, ClipNote> open;
};

namespace ClipTools
{
    /** Moves note starts (and optionally ends) towards the grid by `strength` (0..1). */
    void quantize (NoteClip& clip, const std::vector<int>& uids, double grid, float strength, bool quantizeLength);

    /** Random timing (+- beats) and velocity (+- amount) variation. */
    void humanize (NoteClip& clip, const std::vector<int>& uids, double timing, float velocity, uint32_t seed);

    /** Copies notes, shifted by `offset` beats; returns the new uids. */
    std::vector<int> duplicate (NoteClip& clip, const std::vector<int>& uids, double offset);

    /** Extent of a set of notes in beats. */
    juce::Range<double> span (const NoteClip& clip, const std::vector<int>& uids);
} // namespace ClipTools

} // namespace nedd
