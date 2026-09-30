#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/DspMath.h"
#include "MPE/NoteEvents.h"
#include "Parameters/ParamSnapshot.h"
#include "SequencerData.h"
#include "Synth/Tempo.h"

namespace nedd
{
/** Tracks notes a generator has started so it can end them (and ramp their pitch) on time. */
class GeneratedNoteTracker
{
public:
    static constexpr int kCapacity = 128;

    struct Active
    {
        bool used = false;
        uint32_t id = 0;
        int note = 60;
        double startBeat = 0.0, endBeat = 0.0;
        float pitchTarget = 0.0f;   // semitones reached at endBeat (0 = no ramp)
        NoteOrigin origin = NoteOrigin::Sequencer;
    };

    void add (uint32_t id, int note, double startBeat, double endBeat, float pitchTarget, NoteOrigin origin) noexcept;

    /** Emits note-offs that fall inside [b0, b1) (or are overdue) and pitch-ramp updates. */
    void process (double b0, double b1, const TransportInfo& t, int numSamples, NoteEventList& out) noexcept;

    void releaseAll (int sampleOffset, NoteEventList& out) noexcept;
    int count() const noexcept;

private:
    std::array<Active, (size_t) kCapacity> notes {};
};

// =============================================================================================
/** 32-step expressive step sequencer: every step carries MPE pressure, slide and a pitch glide. */
class StepSequencer
{
public:
    void reset() noexcept;

    void process (const SequencerPattern& pattern, const ParamSnapshot& p, const TransportInfo& t, bool running,
                  NoteIdAllocator& ids, dsp::Random32& rng, NoteEventList& out) noexcept;

    int getCurrentStep() const noexcept { return currentStep; }

private:
    void startStep (const SeqStep& step, double beat, double gateBeats, double b0, const TransportInfo& t, int transpose,
                    NoteIdAllocator& ids, NoteEventList& out) noexcept;

    struct Pending { double beat; int step; double gateBeats; };
    GeneratedNoteTracker tracker;
    std::array<Pending, 64> pending {};
    int pendingCount = 0;
    bool wasRunning = false;
    double lastBlockEnd = -1.0;
    int currentStep = -1;
};

// =============================================================================================
/**
    Arpeggiator. Live notes become the held set; the arp generates new notes whose expression
    source is the finger that produced them, so sliding or pressing a held key still shapes
    every arpeggiated note it spawns.
*/
class Arpeggiator
{
public:
    void reset() noexcept;

    /** Consumes live events from `in`, writes generated notes and pass-through expression to `out`. */
    void process (const NoteEventList& in, NoteEventList& out, const ParamSnapshot& p, const ArpPattern& custom,
                  float gateMod, float probabilityMod, const TransportInfo& t, NoteIdAllocator& ids, dsp::Random32& rng) noexcept;

    /** Ends every generated note and forgets held keys (arp switched off, panic). */
    void releaseAll (int sampleOffset, NoteEventList& out) noexcept;

    int getCurrentStep() const noexcept { return currentStep; }
    int getHeldCount() const noexcept { return heldCount; }

private:
    struct Held
    {
        uint32_t id = 0;
        int note = 60;
        float velocity = 0.8f;
        float pitch = 0.0f, pressure = 0.0f, slide = 0.0f;
        uint32_t order = 0;
    };

    struct Entry { int held; int octave; };
    struct PendingNote { double beat; double length; Held source; int octave; float velocity; };

    void fireStep (double beat, double b0, const ParamSnapshot& p, const ArpPattern& custom, float gateMod, float probabilityMod,
                   const TransportInfo& t, NoteIdAllocator& ids, dsp::Random32& rng, NoteEventList& out) noexcept;
    int buildSequence (ArpMode mode, int octaves, std::array<Entry, 128>& seq) const noexcept;
    double stepBeat (long long k, double stepLength, float swing) const noexcept;
    void startNote (const Held& h, int octave, float velocity, double beat, double lengthBeats, double b0, const TransportInfo& t,
                    NoteIdAllocator& ids, NoteEventList& out) noexcept;

    std::array<Held, 32> held {};
    int heldCount = 0;
    std::array<PendingNote, 32> pending {};
    int pendingCount = 0;
    uint32_t orderCounter = 0;
    GeneratedNoteTracker tracker;
    double origin = 0.0;
    long long stepCounter = 0;
    int currentStep = -1;
    bool active = false;
};

// =============================================================================================
/** Transport of the performance clip (note editor / recorder). Lives on the audio thread. */
struct ClipTransport
{
    bool playing = false;
    bool recording = false;
    bool loop = true;
    bool syncToHost = false;
    double position = 0.0;   // beats
};

/** A recorded live event, sent from the audio thread to the message thread. */
struct RecordedEvent
{
    enum Type : uint8_t { NoteOn = 0, NoteOff, Expression };
    uint8_t type = NoteOn;
    uint8_t dim = 0;
    int noteNumber = 60;
    uint32_t noteId = 0;
    float value = 0.0f;                                  // velocity, release velocity or expression value
    float pitch = 0.0f, pressure = 0.0f, slide = 0.0f;   // initial expression (note on)
    double beat = 0.0;
};

/** Plays a NoteClip with its per-note expression curves. */
class ClipPlayer
{
public:
    void reset() noexcept;

    /** Advances the transport and emits notes/expression for this block. */
    void process (const NoteClip* clip, ClipTransport& transport, const TransportInfo& t, NoteIdAllocator& ids, NoteEventList& out) noexcept;

    void releaseAll (int sampleOffset, NoteEventList& out) noexcept;

private:
    struct Active
    {
        bool used = false;
        uint32_t id = 0;
        int uid = 0;
        double startPosition = 0.0, endPosition = 0.0;
        float lastPitch = 0.0f, lastPressure = 0.0f, lastSlide = 0.0f;
    };

    void playSegment (const NoteClip& clip, double p0, double p1, int offset0, const TransportInfo& t, int numSamples,
                      NoteIdAllocator& ids, NoteEventList& out) noexcept;

    std::array<Active, 128> active {};
    const NoteClip* lastClip = nullptr;
};

// =============================================================================================
/** Converts generated notes (arp / sequencer / clip) to MPE MIDI: one member channel per note. */
class MpeMidiOutput
{
public:
    void reset() noexcept;
    void process (const NoteEventList& events, juce::MidiBuffer& out, int offsetBase, float bendRange) noexcept;
    void sendConfiguration (juce::MidiBuffer& out, int offset, float bendRange) noexcept;
    void allNotesOff (juce::MidiBuffer& out, int offset) noexcept;

private:
    int channelFor (uint32_t id) const noexcept;
    int allocate (uint32_t id, uint32_t exprId, juce::MidiBuffer& out, int offset) noexcept;

    std::array<uint32_t, 17> channelNote {};   // note id per member channel (2..16)
    std::array<uint32_t, 17> channelExpr {};   // id whose expression that channel follows
    std::array<int, 17> channelKey {};
    std::array<uint32_t, 17> channelAge {};
    uint32_t age = 0;
};

} // namespace nedd
