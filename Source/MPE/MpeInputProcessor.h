#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "NoteEvents.h"
#include "Parameters/ParameterDefs.h"

namespace nedd
{
/**
    Converts raw MIDI into per-note events.

    Handles MPE lower/upper zones (including the MPE Configuration Message, RPN 6), per-zone
    member and master pitch-bend ranges (RPN 0), channel pressure, polyphonic aftertouch,
    CC74 slide, release velocity and the sustain pedal.

    In Legacy mode every channel behaves like a member channel using the master bend range,
    so an ordinary keyboard's wheel and aftertouch still reach its notes, while multi-channel
    controllers that don't send an MCM still get per-note expression.

    Real-time safe: fixed-size state, no allocation.
*/
class MpeInputProcessor
{
public:
    static constexpr int kMaxNotes = 128;

    struct ActiveNote
    {
        bool active = false;
        bool keyDown = false;
        uint32_t id = 0;
        int channel = 1;
        int noteNumber = 60;
        float polyPressure = -1.0f;   // >= 0 when polyphonic aftertouch overrides channel pressure
        float releaseVelocity = 0.5f;
    };

    explicit MpeInputProcessor (NoteIdAllocator& ids) : idAllocator (ids) {}

    /** Called once per block with the current parameter values. */
    void setConfig (MpeMode mode, float memberBendRange, float masterBendRange) noexcept;

    void reset() noexcept;

    void process (const juce::MidiMessage& message, int sampleOffset, NoteEventList& out) noexcept;

    /** Emits note-offs for every sounding note (mode change, transport reset, panic). */
    void releaseAll (int sampleOffset, NoteEventList& out) noexcept;

    // ---- inspection (tests and UI) ----
    int getLowerZoneMembers() const noexcept { return lowerMembers; }
    int getUpperZoneMembers() const noexcept { return upperMembers; }
    bool isLegacy() const noexcept { return lowerMembers == 0 && upperMembers == 0; }
    bool isMasterChannel (int channel) const noexcept;
    float getMemberBendRange() const noexcept { return effectiveMemberRange(); }
    bool isSustainDown() const noexcept { return sustain; }
    const std::array<ActiveNote, kMaxNotes>& getActiveNotes() const noexcept { return notes; }

private:
    enum class Zone { None, Lower, Upper };

    struct ChannelState
    {
        float bend = 0.0f;       // -1..1
        float pressure = 0.0f;
        float timbre = 0.0f;
        int rpnMsb = 127, rpnLsb = 127;
        int dataMsb = 0;
    };

    Zone zoneOf (int channel) const noexcept;
    float effectiveMemberRange() const noexcept;
    float effectiveMasterRange() const noexcept;
    float masterBendSemitones (Zone zone) const noexcept;
    float notePitch (int channel) const noexcept;
    float notePressure (const ActiveNote& n) const noexcept;
    float noteTimbre (int channel) const noexcept;

    void handleNoteOn (int channel, int note, float velocity, int offset, NoteEventList& out) noexcept;
    void handleNoteOff (int channel, int note, float releaseVelocity, int offset, NoteEventList& out) noexcept;
    void handlePitchBend (int channel, float normalised, int offset, NoteEventList& out) noexcept;
    void handleChannelPressure (int channel, float value, int offset, NoteEventList& out) noexcept;
    void handlePolyPressure (int channel, int note, float value, int offset, NoteEventList& out) noexcept;
    void handleController (int channel, int cc, int value, int offset, NoteEventList& out) noexcept;
    void handleRpnData (int channel, int offset, NoteEventList& out) noexcept;
    void applyMcm (int channel, int members, int offset, NoteEventList& out) noexcept;
    void refreshPitchForChannel (int channel, int offset, NoteEventList& out) noexcept;
    void refreshPitchForZone (Zone zone, int offset, NoteEventList& out) noexcept;
    void releaseSustained (int offset, NoteEventList& out) noexcept;
    void emitNoteOff (ActiveNote& n, int offset, NoteEventList& out) noexcept;
    void layoutFromMode (MpeMode mode) noexcept;

    NoteIdAllocator& idAllocator;
    std::array<ChannelState, 17> channels {};    // 1-based
    std::array<ActiveNote, kMaxNotes> notes {};

    MpeMode mode = MpeMode::LowerZone;
    int lowerMembers = 15, upperMembers = 0;
    bool layoutFromMcm = false;

    float paramMemberRange = 48.0f, paramMasterRange = 2.0f;
    float rpnMemberRange = -1.0f, rpnMasterRange = -1.0f;   // < 0 when no RPN override
    bool sustain = false;
};

} // namespace nedd
