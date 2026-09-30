#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

namespace nedd
{
/** Per-note expression dimensions. */
enum class ExprDim : uint8_t { Pitch = 0, Pressure, Slide };

/** Where a note came from. Generated notes may be sent to the MIDI output. */
enum class NoteOrigin : uint8_t { Live = 0, Arp, Sequencer, Clip };

enum class GlobalControl : uint8_t { ModWheel = 0, PitchBend, Aftertouch, ControlChange };

/**
    The engine's internal note/expression event.

    Everything downstream of MIDI parsing addresses notes by noteId, never by MIDI channel.
    This is what keeps MPE expression strictly per note: an expression event only reaches the
    voice(s) whose expression source is that note id.
*/
struct NoteEvent
{
    enum class Type : uint8_t { NoteOn = 0, NoteOff, Expression, Global, AllNotesOff };

    Type type = Type::NoteOn;
    NoteOrigin origin = NoteOrigin::Live;
    ExprDim dim = ExprDim::Pitch;
    GlobalControl control = GlobalControl::ModWheel;
    int sampleOffset = 0;

    uint32_t noteId = 0;      // identity of this note
    uint32_t exprId = 0;      // id whose expression this note follows (== noteId unless generated)
    int noteNumber = 60;
    int channel = 1;
    int ccNumber = 0;

    float value = 0.0f;       // velocity (on), release velocity (off), expression or controller value
    float pitch = 0.0f;       // initial per-note pitch offset in semitones (note on)
    float pressure = 0.0f;    // initial pressure (note on)
    float slide = 0.0f;       // initial slide (note on)

    static NoteEvent noteOn (int offset, uint32_t id, int note, int ch, float velocity, NoteOrigin origin,
                             float pitch = 0.0f, float pressure = 0.0f, float slide = 0.0f, uint32_t exprId = 0) noexcept
    {
        NoteEvent e;
        e.type = Type::NoteOn;
        e.sampleOffset = offset;
        e.noteId = id;
        e.exprId = exprId != 0 ? exprId : id;
        e.noteNumber = note;
        e.channel = ch;
        e.value = velocity;
        e.origin = origin;
        e.pitch = pitch;
        e.pressure = pressure;
        e.slide = slide;
        return e;
    }

    static NoteEvent noteOff (int offset, uint32_t id, int note, int ch, float releaseVelocity, NoteOrigin origin) noexcept
    {
        NoteEvent e;
        e.type = Type::NoteOff;
        e.sampleOffset = offset;
        e.noteId = e.exprId = id;
        e.noteNumber = note;
        e.channel = ch;
        e.value = releaseVelocity;
        e.origin = origin;
        return e;
    }

    static NoteEvent expression (int offset, uint32_t id, ExprDim dim, float value, NoteOrigin origin, int ch = 1) noexcept
    {
        NoteEvent e;
        e.type = Type::Expression;
        e.sampleOffset = offset;
        e.noteId = e.exprId = id;
        e.dim = dim;
        e.value = value;
        e.origin = origin;
        e.channel = ch;
        return e;
    }

    static NoteEvent global (int offset, GlobalControl control, float value, int cc = 0, int ch = 1) noexcept
    {
        NoteEvent e;
        e.type = Type::Global;
        e.sampleOffset = offset;
        e.control = control;
        e.value = value;
        e.ccNumber = cc;
        e.channel = ch;
        return e;
    }

    static NoteEvent allNotesOff (int offset) noexcept
    {
        NoteEvent e;
        e.type = Type::AllNotesOff;
        e.sampleOffset = offset;
        return e;
    }
};

/** Fixed-capacity event list for one block. Never allocates after construction. */
class NoteEventList
{
public:
    static constexpr int kCapacity = 4096;

    void clear() noexcept { count = 0; }
    int size() const noexcept { return count; }
    bool isEmpty() const noexcept { return count == 0; }

    bool add (const NoteEvent& e) noexcept
    {
        if (count >= kCapacity)
        {
            ++dropped;
            return false;
        }
        events[(size_t) count++] = e;
        return true;
    }

    const NoteEvent& operator[] (int i) const noexcept { return events[(size_t) i]; }
    NoteEvent& operator[] (int i) noexcept { return events[(size_t) i]; }

    const NoteEvent* begin() const noexcept { return events.data(); }
    const NoteEvent* end() const noexcept { return events.data() + count; }

    /** Stable insertion sort by sample offset; lists are nearly sorted so this is ~linear. */
    void sortByTime() noexcept
    {
        for (int i = 1; i < count; ++i)
        {
            const NoteEvent e = events[(size_t) i];
            int j = i - 1;
            while (j >= 0 && events[(size_t) j].sampleOffset > e.sampleOffset)
            {
                events[(size_t) j + 1] = events[(size_t) j];
                --j;
            }
            events[(size_t) j + 1] = e;
        }
    }

    int getDroppedCount() const noexcept { return dropped; }

private:
    std::array<NoteEvent, (size_t) kCapacity> events {};
    int count = 0;
    int dropped = 0;
};

/** Hands out unique note ids for live and generated notes. */
class NoteIdAllocator
{
public:
    uint32_t next() noexcept
    {
        if (++counter == 0)
            counter = 1;
        return counter;
    }

private:
    uint32_t counter = 0;
};

} // namespace nedd
