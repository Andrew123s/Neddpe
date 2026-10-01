#include "ClipTools.h"

namespace nedd
{
void PerformanceRecorder::begin (const NoteClip& base, bool keepExisting, bool isLooping)
{
    clip = keepExisting ? base : NoteClip();
    clip.lengthBeats = base.lengthBeats;
    looping = isLooping && keepExisting && ! base.notes.empty();
    lengthFixed = looping;
    open.clear();
    active = true;
}

double PerformanceRecorder::timeSince (double start, double beat) const noexcept
{
    double t = beat - start;
    if (t < 0.0 && looping)
        t += clip.lengthBeats;   // the note crossed the loop point
    return std::max (0.0, t);
}

void PerformanceRecorder::consume (const RecordedEvent& e)
{
    if (! active)
        return;

    switch (e.type)
    {
        case RecordedEvent::NoteOn:
        {
            ClipNote note;
            note.noteNumber = e.noteNumber;
            note.start = std::max (0.0, e.beat);
            note.velocity = e.value;
            note.pitch.push_back ({ 0.0f, e.pitch });
            note.pressure.push_back ({ 0.0f, e.pressure });
            note.slide.push_back ({ 0.0f, e.slide });
            open[e.noteId] = std::move (note);
            break;
        }
        case RecordedEvent::Expression:
        {
            auto it = open.find (e.noteId);
            if (it == open.end())
                break;
            auto& note = it->second;
            auto& curve = e.dim == (uint8_t) ExprDim::Pitch ? note.pitch : (e.dim == (uint8_t) ExprDim::Pressure ? note.pressure : note.slide);
            const float t = (float) timeSince (note.start, e.beat);
            if (! curve.empty() && t <= curve.back().time)
                curve.back().value = e.value;    // same instant: keep the latest value
            else
                curve.push_back ({ t, e.value });
            break;
        }
        case RecordedEvent::NoteOff:
        {
            auto it = open.find (e.noteId);
            if (it == open.end())
                break;
            close (std::move (it->second), e.beat, e.value);
            open.erase (it);
            break;
        }
        default:
            break;
    }
}

void PerformanceRecorder::close (ClipNote note, double endBeat, float releaseVelocity)
{
    note.length = std::max (1.0 / 64.0, timeSince (note.start, endBeat));
    note.releaseVelocity = releaseVelocity;
    simplifyCurve (note.pitch, 0.02f);
    simplifyCurve (note.pressure, 0.004f);
    simplifyCurve (note.slide, 0.004f);
    clip.addNote (std::move (note));
}

NoteClip PerformanceRecorder::preview (double nowBeat) const
{
    NoteClip result = clip;
    for (const auto& [id, note] : open)
    {
        auto n = note;
        n.length = std::max (1.0 / 64.0, timeSince (n.start, nowBeat));
        result.addNote (std::move (n));
    }
    return result;
}

NoteClip PerformanceRecorder::finish (double endBeat)
{
    for (auto& [id, note] : open)
        close (std::move (note), endBeat, 0.5f);
    open.clear();

    if (! lengthFixed)
    {
        // A fresh recording: the clip becomes as long as the performance, rounded up to the bar.
        double last = endBeat;
        for (const auto& n : clip.notes)
            last = std::max (last, n.end());
        clip.lengthBeats = std::max (4.0, std::ceil (last / 4.0 - 1.0e-6) * 4.0);
    }

    clip.sortByStart();
    active = false;
    return clip;
}

// =============================================================================================
namespace ClipTools
{
    namespace
    {
        bool contains (const std::vector<int>& uids, int uid)
        {
            return uids.empty() || std::find (uids.begin(), uids.end(), uid) != uids.end();
        }
    } // namespace

    void quantize (NoteClip& clip, const std::vector<int>& uids, double grid, float strength, bool quantizeLength)
    {
        if (grid <= 0.0)
            return;
        for (auto& n : clip.notes)
        {
            if (! contains (uids, n.uid))
                continue;
            const double target = std::round (n.start / grid) * grid;
            const double end = n.end();
            n.start += (target - n.start) * strength;
            if (quantizeLength)
            {
                const double targetEnd = std::max (n.start + grid, std::round (end / grid) * grid);
                n.length = std::max (1.0 / 64.0, (end + (targetEnd - end) * strength) - n.start);
            }
            n.start = std::max (0.0, n.start);
        }
        clip.sortByStart();
    }

    void humanize (NoteClip& clip, const std::vector<int>& uids, double timing, float velocity, uint32_t seed)
    {
        dsp::Random32 rng;
        rng.seed (seed);
        for (auto& n : clip.notes)
        {
            if (! contains (uids, n.uid))
                continue;
            n.start = std::max (0.0, n.start + timing * rng.nextBipolar());
            n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity + velocity * rng.nextBipolar());
        }
        clip.sortByStart();
    }

    std::vector<int> duplicate (NoteClip& clip, const std::vector<int>& uids, double offset)
    {
        std::vector<ClipNote> copies;
        for (const auto& n : clip.notes)
            if (contains (uids, n.uid))
                copies.push_back (n);

        std::vector<int> created;
        for (auto& n : copies)
        {
            n.start += offset;
            created.push_back (clip.addNote (n));
        }
        clip.sortByStart();
        return created;
    }

    juce::Range<double> span (const NoteClip& clip, const std::vector<int>& uids)
    {
        double lo = std::numeric_limits<double>::max(), hi = 0.0;
        for (const auto& n : clip.notes)
        {
            if (! contains (uids, n.uid))
                continue;
            lo = std::min (lo, n.start);
            hi = std::max (hi, n.end());
        }
        return lo > hi ? juce::Range<double>() : juce::Range<double> (lo, hi);
    }
} // namespace ClipTools

} // namespace nedd
