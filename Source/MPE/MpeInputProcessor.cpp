#include "MpeInputProcessor.h"

namespace nedd
{
namespace
{
    constexpr int kSustainCC = 64;
    constexpr int kSlideCC = 74;
    constexpr int kModWheelCC = 1;
}

void MpeInputProcessor::layoutFromMode (MpeMode newMode) noexcept
{
    switch (newMode)
    {
        case MpeMode::Legacy:    lowerMembers = 0;  upperMembers = 0;  break;
        case MpeMode::LowerZone: lowerMembers = 15; upperMembers = 0;  break;
        case MpeMode::UpperZone: lowerMembers = 0;  upperMembers = 15; break;
        case MpeMode::BothZones: lowerMembers = 7;  upperMembers = 7;  break;
    }
}

void MpeInputProcessor::setConfig (MpeMode newMode, float memberBendRange, float masterBendRange) noexcept
{
    if (newMode != mode)
    {
        mode = newMode;
        layoutFromMcm = false;
        layoutFromMode (mode);
    }
    else if (! layoutFromMcm)
    {
        layoutFromMode (mode);
    }

    // A change of the range parameters wins over any RPN received earlier.
    if (memberBendRange != paramMemberRange) rpnMemberRange = -1.0f;
    if (masterBendRange != paramMasterRange) rpnMasterRange = -1.0f;
    paramMemberRange = memberBendRange;
    paramMasterRange = masterBendRange;
}

void MpeInputProcessor::reset() noexcept
{
    channels = {};
    notes = {};
    sustain = false;
    layoutFromMcm = false;
    layoutFromMode (mode);
    rpnMemberRange = rpnMasterRange = -1.0f;
}

bool MpeInputProcessor::isMasterChannel (int channel) const noexcept
{
    return (lowerMembers > 0 && channel == 1) || (upperMembers > 0 && channel == 16);
}

MpeInputProcessor::Zone MpeInputProcessor::zoneOf (int channel) const noexcept
{
    if (lowerMembers > 0 && channel >= 1 && channel <= 1 + lowerMembers)
        return Zone::Lower;
    if (upperMembers > 0 && channel <= 16 && channel >= 16 - upperMembers)
        return Zone::Upper;
    return Zone::None;
}

float MpeInputProcessor::effectiveMemberRange() const noexcept { return rpnMemberRange >= 0.0f ? rpnMemberRange : paramMemberRange; }
float MpeInputProcessor::effectiveMasterRange() const noexcept { return rpnMasterRange >= 0.0f ? rpnMasterRange : paramMasterRange; }

float MpeInputProcessor::masterBendSemitones (Zone zone) const noexcept
{
    if (zone == Zone::Lower) return channels[1].bend * effectiveMasterRange();
    if (zone == Zone::Upper) return channels[16].bend * effectiveMasterRange();
    return 0.0f;
}

float MpeInputProcessor::notePitch (int channel) const noexcept
{
    const Zone zone = zoneOf (channel);

    if (zone == Zone::None)   // legacy or outside any zone: the channel's own wheel
        return channels[(size_t) channel].bend * effectiveMasterRange();

    if (isMasterChannel (channel))
        return masterBendSemitones (zone);

    return channels[(size_t) channel].bend * effectiveMemberRange() + masterBendSemitones (zone);
}

float MpeInputProcessor::notePressure (const ActiveNote& n) const noexcept
{
    return n.polyPressure >= 0.0f ? n.polyPressure : channels[(size_t) n.channel].pressure;
}

float MpeInputProcessor::noteTimbre (int channel) const noexcept { return channels[(size_t) channel].timbre; }

void MpeInputProcessor::process (const juce::MidiMessage& m, int offset, NoteEventList& out) noexcept
{
    const int ch = m.getChannel();
    if (ch < 1 || ch > 16)
        return;

    if (m.isNoteOn())
        handleNoteOn (ch, m.getNoteNumber(), m.getFloatVelocity(), offset, out);
    else if (m.isNoteOff())
        handleNoteOff (ch, m.getNoteNumber(), m.getFloatVelocity(), offset, out);
    else if (m.isPitchWheel())
        handlePitchBend (ch, (float) (m.getPitchWheelValue() - 8192) / 8192.0f, offset, out);
    else if (m.isChannelPressure())
        handleChannelPressure (ch, (float) m.getChannelPressureValue() / 127.0f, offset, out);
    else if (m.isAftertouch())
        handlePolyPressure (ch, m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f, offset, out);
    else if (m.isController())
        handleController (ch, m.getControllerNumber(), m.getControllerValue(), offset, out);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        releaseAll (offset, out);
}

void MpeInputProcessor::handleNoteOn (int ch, int note, float velocity, int offset, NoteEventList& out) noexcept
{
    // Re-striking the same key on the same channel ends the previous instance first.
    for (auto& n : notes)
        if (n.active && n.channel == ch && n.noteNumber == note)
            emitNoteOff (n, offset, out);

    ActiveNote* slot = nullptr;
    for (auto& n : notes)
        if (! n.active) { slot = &n; break; }

    if (slot == nullptr)
    {
        // Table full: recycle the oldest held note (lowest id).
        slot = &notes[0];
        for (auto& n : notes)
            if (n.id < slot->id) slot = &n;
        emitNoteOff (*slot, offset, out);
    }

    slot->active = true;
    slot->keyDown = true;
    slot->id = idAllocator.next();
    slot->channel = ch;
    slot->noteNumber = note;
    slot->polyPressure = -1.0f;

    const bool masterNote = isMasterChannel (ch);
    const float pressure = masterNote ? 0.0f : notePressure (*slot);
    const float timbre = masterNote ? 0.0f : noteTimbre (ch);

    out.add (NoteEvent::noteOn (offset, slot->id, note, ch, velocity, NoteOrigin::Live, notePitch (ch), pressure, timbre));
}

void MpeInputProcessor::handleNoteOff (int ch, int note, float releaseVelocity, int offset, NoteEventList& out) noexcept
{
    for (auto& n : notes)
    {
        if (n.active && n.keyDown && n.channel == ch && n.noteNumber == note)
        {
            n.keyDown = false;
            n.releaseVelocity = releaseVelocity;
            if (! sustain)
                emitNoteOff (n, offset, out);
            return;
        }
    }
}

void MpeInputProcessor::emitNoteOff (ActiveNote& n, int offset, NoteEventList& out) noexcept
{
    out.add (NoteEvent::noteOff (offset, n.id, n.noteNumber, n.channel, n.releaseVelocity, NoteOrigin::Live));
    n.active = false;
    n.keyDown = false;
}

void MpeInputProcessor::releaseSustained (int offset, NoteEventList& out) noexcept
{
    for (auto& n : notes)
        if (n.active && ! n.keyDown)
            emitNoteOff (n, offset, out);
}

void MpeInputProcessor::releaseAll (int offset, NoteEventList& out) noexcept
{
    for (auto& n : notes)
        if (n.active)
            emitNoteOff (n, offset, out);
    sustain = false;
}

void MpeInputProcessor::refreshPitchForChannel (int ch, int offset, NoteEventList& out) noexcept
{
    const float pitch = notePitch (ch);
    for (const auto& n : notes)
        if (n.active && n.channel == ch)
            out.add (NoteEvent::expression (offset, n.id, ExprDim::Pitch, pitch, NoteOrigin::Live, ch));
}

void MpeInputProcessor::refreshPitchForZone (Zone zone, int offset, NoteEventList& out) noexcept
{
    for (const auto& n : notes)
        if (n.active && zoneOf (n.channel) == zone)
            out.add (NoteEvent::expression (offset, n.id, ExprDim::Pitch, notePitch (n.channel), NoteOrigin::Live, n.channel));
}

void MpeInputProcessor::handlePitchBend (int ch, float normalised, int offset, NoteEventList& out) noexcept
{
    channels[(size_t) ch].bend = juce::jlimit (-1.0f, 1.0f, normalised);
    const Zone zone = zoneOf (ch);

    if (zone != Zone::None && isMasterChannel (ch))
    {
        // Master bend: applies to every note of the zone on top of its own bend.
        refreshPitchForZone (zone, offset, out);
        out.add (NoteEvent::global (offset, GlobalControl::PitchBend, normalised, 0, ch));
        return;
    }

    refreshPitchForChannel (ch, offset, out);

    if (zone == Zone::None)   // legacy wheel also drives the global Pitch Bend source
        out.add (NoteEvent::global (offset, GlobalControl::PitchBend, normalised, 0, ch));
}

void MpeInputProcessor::handleChannelPressure (int ch, float value, int offset, NoteEventList& out) noexcept
{
    channels[(size_t) ch].pressure = value;
    const Zone zone = zoneOf (ch);

    if (zone != Zone::None && isMasterChannel (ch))
    {
        out.add (NoteEvent::global (offset, GlobalControl::Aftertouch, value, 0, ch));
        return;
    }

    for (auto& n : notes)
        if (n.active && n.channel == ch && n.polyPressure < 0.0f)
            out.add (NoteEvent::expression (offset, n.id, ExprDim::Pressure, value, NoteOrigin::Live, ch));

    if (zone == Zone::None)
        out.add (NoteEvent::global (offset, GlobalControl::Aftertouch, value, 0, ch));
}

void MpeInputProcessor::handlePolyPressure (int ch, int note, float value, int offset, NoteEventList& out) noexcept
{
    for (auto& n : notes)
    {
        if (n.active && n.channel == ch && n.noteNumber == note)
        {
            n.polyPressure = value;
            out.add (NoteEvent::expression (offset, n.id, ExprDim::Pressure, value, NoteOrigin::Live, ch));
        }
    }
}

void MpeInputProcessor::handleController (int ch, int cc, int value, int offset, NoteEventList& out) noexcept
{
    auto& state = channels[(size_t) ch];
    const float normalised = (float) value / 127.0f;
    const bool memberChannel = zoneOf (ch) != Zone::None && ! isMasterChannel (ch);

    switch (cc)
    {
        case 101: state.rpnMsb = value; return;
        case 100: state.rpnLsb = value; return;
        case 99:
        case 98:  state.rpnMsb = state.rpnLsb = 127; return;   // NRPN: ignore data entry
        case 6:
            state.dataMsb = value;
            handleRpnData (ch, offset, out);
            return;
        case 38:
            // Fine bend range in cents for RPN 0.
            if (state.rpnMsb == 0 && state.rpnLsb == 0)
            {
                const float range = (float) state.dataMsb + (float) value / 100.0f;
                if (memberChannel) rpnMemberRange = range; else rpnMasterRange = range;
            }
            return;
        default:
            break;
    }

    if (cc == kSustainCC)
    {
        const bool down = value >= 64;
        if (sustain && ! down)
            releaseSustained (offset, out);
        sustain = down;
        return;
    }

    if (cc == kSlideCC && (memberChannel || zoneOf (ch) == Zone::None))
    {
        state.timbre = normalised;
        for (const auto& n : notes)
            if (n.active && n.channel == ch)
                out.add (NoteEvent::expression (offset, n.id, ExprDim::Slide, normalised, NoteOrigin::Live, ch));
        if (memberChannel)
            return;
    }

    if (cc == kModWheelCC)
        out.add (NoteEvent::global (offset, GlobalControl::ModWheel, normalised, cc, ch));

    out.add (NoteEvent::global (offset, GlobalControl::ControlChange, normalised, cc, ch));
}

void MpeInputProcessor::handleRpnData (int ch, int offset, NoteEventList& out) noexcept
{
    const auto& state = channels[(size_t) ch];

    if (state.rpnMsb == 0 && state.rpnLsb == 0)
    {
        // RPN 0: pitch-bend sensitivity. Member channels set the zone's per-note range.
        const bool memberChannel = zoneOf (ch) != Zone::None && ! isMasterChannel (ch);
        if (memberChannel) rpnMemberRange = (float) state.dataMsb;
        else               rpnMasterRange = (float) state.dataMsb;
        return;
    }

    if (state.rpnMsb == 0 && state.rpnLsb == 6 && (ch == 1 || ch == 16))
        applyMcm (ch, state.dataMsb, offset, out);
}

void MpeInputProcessor::applyMcm (int ch, int members, int offset, NoteEventList& out) noexcept
{
    if (mode == MpeMode::Legacy)
        return;   // the user explicitly chose non-MPE operation

    members = juce::jlimit (0, 15, members);
    releaseAll (offset, out);

    if (ch == 1)
    {
        lowerMembers = members;
        if (lowerMembers + upperMembers > 14 && upperMembers > 0)
            upperMembers = std::max (0, 14 - lowerMembers);
    }
    else
    {
        upperMembers = members;
        if (lowerMembers + upperMembers > 14 && lowerMembers > 0)
            lowerMembers = std::max (0, 14 - upperMembers);
    }

    layoutFromMcm = true;
    // MPE spec: an MCM resets the zone's per-note bend range to the 48 semitone default.
    rpnMemberRange = 48.0f;
}

} // namespace nedd
