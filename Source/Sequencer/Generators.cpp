#include "Generators.h"

namespace nedd
{
namespace
{
    inline int offsetFor (double beat, double b0, const TransportInfo& t, int numSamples) noexcept
    {
        const double samples = (beat - b0) / std::max (1.0e-12, t.beatsPerSample);
        return juce::jlimit (0, std::max (0, numSamples - 1), (int) std::floor (samples));
    }
} // namespace

// =============================================================================================
// GeneratedNoteTracker
// =============================================================================================
void GeneratedNoteTracker::add (uint32_t id, int note, double startBeat, double endBeat, float pitchTarget, NoteOrigin origin) noexcept
{
    for (auto& n : notes)
    {
        if (! n.used)
        {
            n = { true, id, note, startBeat, endBeat, pitchTarget, origin };
            return;
        }
    }
}

void GeneratedNoteTracker::process (double b0, double b1, const TransportInfo& t, int numSamples, NoteEventList& out) noexcept
{
    for (auto& n : notes)
    {
        if (! n.used)
            continue;

        if (n.endBeat < b1)
        {
            out.add (NoteEvent::noteOff (offsetFor (n.endBeat, b0, t, numSamples), n.id, n.note, 1, 0.5f, n.origin));
            n.used = false;
            continue;
        }

        if (n.pitchTarget != 0.0f && b0 >= n.startBeat)
        {
            // Smooth (S-shaped) glide towards the step's pitch target across the gate.
            const double span = std::max (1.0e-9, n.endBeat - n.startBeat);
            const float x = (float) juce::jlimit (0.0, 1.0, (0.5 * (b0 + b1) - n.startBeat) / span);
            out.add (NoteEvent::expression (0, n.id, ExprDim::Pitch, n.pitchTarget * x * x * (3.0f - 2.0f * x), n.origin));
        }
    }
}

void GeneratedNoteTracker::releaseAll (int sampleOffset, NoteEventList& out) noexcept
{
    for (auto& n : notes)
    {
        if (n.used)
            out.add (NoteEvent::noteOff (sampleOffset, n.id, n.note, 1, 0.5f, n.origin));
        n.used = false;
    }
}

int GeneratedNoteTracker::count() const noexcept
{
    int c = 0;
    for (const auto& n : notes)
        c += n.used ? 1 : 0;
    return c;
}

// =============================================================================================
// StepSequencer
// =============================================================================================
void StepSequencer::reset() noexcept
{
    tracker = {};
    pendingCount = 0;
    wasRunning = false;
    lastBlockEnd = -1.0;
    currentStep = -1;
}

void StepSequencer::startStep (const SeqStep& s, double beat, double gateBeats, double b0, const TransportInfo& t, int transpose,
                               NoteIdAllocator& ids, NoteEventList& out) noexcept
{
    const uint32_t id = ids.next();
    const int note = juce::jlimit (0, 127, s.note + transpose);
    const float velocity = s.accent ? std::min (1.0f, s.velocity + 0.2f) : s.velocity;
    out.add (NoteEvent::noteOn (offsetFor (beat, b0, t, t.numSamples), id, note, 1, velocity, NoteOrigin::Sequencer,
                                0.0f, s.pressure, s.slide));
    tracker.add (id, note, beat, beat + gateBeats, s.pitch, NoteOrigin::Sequencer);
}

void StepSequencer::process (const SequencerPattern& pattern, const ParamSnapshot& p, const TransportInfo& t, bool running,
                             NoteIdAllocator& ids, dsp::Random32& rng, NoteEventList& out) noexcept
{
    const int n = t.numSamples;
    const double b0 = t.ppqAtBlockStart;
    const double b1 = b0 + t.beatsPerSample * n;

    if (! running)
    {
        if (wasRunning)
        {
            tracker.releaseAll (0, out);
            pendingCount = 0;
        }
        wasRunning = false;
        currentStep = -1;
        lastBlockEnd = b1;
        return;
    }

    // Transport jumped (loop, locate): end what is playing and restart cleanly.
    if (! wasRunning || std::abs (b0 - lastBlockEnd) > t.beatsPerSample * 4.0)
    {
        tracker.releaseAll (0, out);
        pendingCount = 0;
    }
    wasRunning = true;

    const double stepLength = divisionToBeats (p.getInt (pid::seq (SeqField::Division)));
    const int length = juce::jlimit (1, SequencerPattern::kMaxSteps, p.getInt (pid::seq (SeqField::Length)));
    const float swing = p[pid::seq (SeqField::Swing)];
    const int transpose = p.getInt (pid::seq (SeqField::Transpose));

    // Ratchet repeats that were scheduled into this block.
    for (int i = 0; i < pendingCount;)
    {
        if (pending[(size_t) i].beat < b1)
        {
            startStep (pattern.steps[(size_t) pending[(size_t) i].step], pending[(size_t) i].beat, pending[(size_t) i].gateBeats, b0, t, transpose, ids, out);
            pending[(size_t) i] = pending[(size_t) --pendingCount];
        }
        else
            ++i;
    }

    const long long first = (long long) std::floor (b0 / stepLength) - 1;
    const long long last = (long long) std::ceil (b1 / stepLength);
    for (long long k = std::max (0LL, first); k <= last; ++k)
    {
        const double start = (double) k * stepLength + ((k & 1) != 0 ? swing * stepLength * 0.5 : 0.0);
        if (start < b0 || start >= b1)
            continue;

        const int index = (int) (k % length);
        currentStep = index;
        const auto& step = pattern.steps[(size_t) index];
        if (! step.on || rng.nextFloat() >= step.probability)
            continue;

        const int ratchet = juce::jlimit (1, 4, step.ratchet);
        const double sub = stepLength / ratchet;
        const double gateBeats = step.gate * sub;
        for (int r = 0; r < ratchet; ++r)
        {
            const double beat = start + sub * r;
            if (beat < b1)
                startStep (step, beat, gateBeats, b0, t, transpose, ids, out);
            else if (pendingCount < (int) pending.size())
                pending[(size_t) pendingCount++] = { beat, index, gateBeats };
        }
    }

    tracker.process (b0, b1, t, n, out);
    lastBlockEnd = b1;
}

// =============================================================================================
// Arpeggiator
// =============================================================================================
void Arpeggiator::reset() noexcept
{
    heldCount = 0;
    pendingCount = 0;
    tracker = {};
    active = false;
    stepCounter = 0;
    currentStep = -1;
}

void Arpeggiator::releaseAll (int sampleOffset, NoteEventList& out) noexcept
{
    tracker.releaseAll (sampleOffset, out);
    heldCount = 0;
    pendingCount = 0;
    active = false;
    currentStep = -1;
}

double Arpeggiator::stepBeat (long long k, double stepLength, float swing) const noexcept
{
    return origin + (double) k * stepLength + ((k & 1) != 0 ? swing * stepLength * 0.5 : 0.0);
}

int Arpeggiator::buildSequence (ArpMode mode, int octaves, std::array<Entry, 128>& seq) const noexcept
{
    std::array<int, 32> order {};
    for (int i = 0; i < heldCount; ++i)
        order[(size_t) i] = i;

    if (mode == ArpMode::Order)
        std::sort (order.begin(), order.begin() + heldCount, [this] (int a, int b) { return held[(size_t) a].order < held[(size_t) b].order; });
    else
        std::sort (order.begin(), order.begin() + heldCount, [this] (int a, int b) { return held[(size_t) a].note < held[(size_t) b].note; });

    int size = 0;
    for (int o = 0; o < octaves; ++o)
        for (int i = 0; i < heldCount && size < (int) seq.size(); ++i)
            seq[(size_t) size++] = { order[(size_t) i], o };

    if (mode == ArpMode::Down)
        std::reverse (seq.begin(), seq.begin() + size);
    else if (mode == ArpMode::UpDown && size > 2)
    {
        const int up = size;
        for (int i = up - 2; i >= 1 && size < (int) seq.size(); --i)
            seq[(size_t) size++] = seq[(size_t) i];
    }
    return size;
}

void Arpeggiator::startNote (const Held& h, int octave, float velocity, double beat, double lengthBeats, double b0, const TransportInfo& t,
                             NoteIdAllocator& ids, NoteEventList& out) noexcept
{
    const uint32_t id = ids.next();
    const int note = juce::jlimit (0, 127, h.note + 12 * octave);
    // exprId = the held finger: live expression of that key keeps shaping this generated note.
    out.add (NoteEvent::noteOn (offsetFor (beat, b0, t, t.numSamples), id, note, 1, velocity, NoteOrigin::Arp,
                                h.pitch, h.pressure, h.slide, h.id));
    tracker.add (id, note, beat, beat + lengthBeats, 0.0f, NoteOrigin::Arp);
}

void Arpeggiator::fireStep (double beat, double b0, const ParamSnapshot& p, const ArpPattern& custom, float gateMod, float probabilityMod,
                            const TransportInfo& t, NoteIdAllocator& ids, dsp::Random32& rng, NoteEventList& out) noexcept
{
    const auto mode = p.getChoice<ArpMode> (pid::arp (ArpField::Mode));
    const int octaves = juce::jlimit (1, 4, p.getInt (pid::arp (ArpField::Octaves)));
    const int length = juce::jlimit (1, 16, p.getInt (pid::arp (ArpField::Length)));
    const int repeat = juce::jlimit (1, 4, p.getInt (pid::arp (ArpField::Repeat)));
    const int ratchet = juce::jlimit (1, 4, p.getInt (pid::arp (ArpField::Ratchet)));
    const int accentEvery = juce::jlimit (2, 8, p.getInt (pid::arp (ArpField::AccentEvery)));
    const float accent = p[pid::arp (ArpField::Accent)];
    const double stepLength = divisionToBeats (p.getInt (pid::arp (ArpField::Division)));
    const double gate = juce::jlimit (0.05, 1.0, (double) (p[pid::arp (ArpField::Gate)] + gateMod)) * stepLength;
    const float probability = dsp::clamp01 (p[pid::arp (ArpField::Probability)] + probabilityMod);

    const long long logical = stepCounter / repeat;

    std::array<Entry, 128> seq {};
    const int size = buildSequence (mode, octaves, seq);
    if (size == 0)
        return;

    const int cycle = mode == ArpMode::Custom ? length : std::min (length, size);
    currentStep = (int) (logical % cycle);

    if (rng.nextFloat() >= probability)
        return;

    const bool accented = accent > 0.0f && logical % accentEvery == 0;
    auto velocityFor = [&] (const Held& h)
    {
        float v = p.getChoice<ArpVelocityMode> (pid::arp (ArpField::VelocityMode)) == ArpVelocityMode::Fixed ? p[pid::arp (ArpField::Velocity)] : h.velocity;
        v = accented ? v + accent * (1.0f - v) : v * (1.0f - 0.35f * accent);
        return dsp::clamp01 (v);
    };

    auto play = [&] (const Held& h, int octave)
    {
        const double sub = stepLength / ratchet;
        for (int r = 0; r < ratchet; ++r)
        {
            const double at = beat + sub * r;
            const double b1 = t.ppqAtBlockStart + t.beatsPerSample * t.numSamples;
            if (at < b1)
                startNote (h, octave, velocityFor (h), at, gate / ratchet, b0, t, ids, out);
            else if (pendingCount < (int) pending.size())
                pending[(size_t) pendingCount++] = { at, gate / ratchet, h, octave, velocityFor (h) };
        }
    };

    switch (mode)
    {
        case ArpMode::Chord:
        {
            const int octave = (int) (logical % octaves);
            for (int i = 0; i < heldCount; ++i)
                play (held[(size_t) i], octave);
            break;
        }
        case ArpMode::Custom:
        {
            const int value = custom.steps[(size_t) (logical % std::min (length, ArpPattern::kSteps))];
            if (value < 0)
                break;   // rest
            const int base = size / octaves;   // held notes per octave, sorted ascending
            const int index = value % std::max (1, base);
            const int octave = std::min (value / std::max (1, base), octaves - 1);
            play (held[(size_t) seq[(size_t) index].held], octave);
            break;
        }
        case ArpMode::Random:
        {
            const auto& e = seq[(size_t) (rng.next() % (uint32_t) size)];
            play (held[(size_t) e.held], e.octave);
            break;
        }
        case ArpMode::Up:
        case ArpMode::Down:
        case ArpMode::UpDown:
        case ArpMode::Order:
        {
            const auto& e = seq[(size_t) (logical % cycle)];
            play (held[(size_t) e.held], e.octave);
            break;
        }
    }
}

void Arpeggiator::process (const NoteEventList& in, NoteEventList& out, const ParamSnapshot& p, const ArpPattern& custom,
                           float gateMod, float probabilityMod, const TransportInfo& t, NoteIdAllocator& ids, dsp::Random32& rng) noexcept
{
    const int n = t.numSamples;
    const double b0 = t.ppqAtBlockStart;
    const double b1 = b0 + t.beatsPerSample * n;
    const double stepLength = divisionToBeats (p.getInt (pid::arp (ArpField::Division)));
    const float swing = p[pid::arp (ArpField::Swing)];

    for (int i = 0; i < pendingCount;)
    {
        auto& pn = pending[(size_t) i];
        if (pn.beat < b1)
        {
            startNote (pn.source, pn.octave, pn.velocity, pn.beat, pn.length, b0, t, ids, out);
            pending[(size_t) i] = pending[(size_t) --pendingCount];
        }
        else
            ++i;
    }

    auto fireUntil = [&] (double limit)
    {
        if (! active || heldCount == 0)
            return;
        // Far behind (transport jump): resynchronise instead of firing a burst of steps.
        if (stepBeat (stepCounter, stepLength, swing) < b0 - stepLength * 2.0)
            stepCounter = (long long) std::ceil ((b0 - origin) / stepLength);
        while (stepBeat (stepCounter, stepLength, swing) < limit)
        {
            fireStep (stepBeat (stepCounter, stepLength, swing), b0, p, custom, gateMod, probabilityMod, t, ids, rng, out);
            ++stepCounter;
        }
    };

    for (const auto& e : in)
    {
        const double beat = b0 + t.beatsPerSample * e.sampleOffset;
        fireUntil (beat);

        switch (e.type)
        {
            case NoteEvent::Type::NoteOn:
            {
                if (heldCount < (int) held.size())
                    held[(size_t) heldCount++] = { e.noteId, e.noteNumber, e.value, e.pitch, e.pressure, e.slide, ++orderCounter };
                if (! active)
                {
                    active = true;
                    stepCounter = 0;
                    // In sync with a playing host the arp lands on the grid; otherwise it starts at once.
                    origin = t.hostPlaying ? std::ceil (beat / stepLength - 1.0e-9) * stepLength : beat;
                }
                break;
            }
            case NoteEvent::Type::NoteOff:
            {
                for (int i = 0; i < heldCount; ++i)
                {
                    if (held[(size_t) i].id == e.noteId)
                    {
                        std::move (held.begin() + i + 1, held.begin() + heldCount, held.begin() + i);
                        --heldCount;
                        break;
                    }
                }
                if (heldCount == 0)
                {
                    active = false;
                    currentStep = -1;
                }
                break;
            }
            case NoteEvent::Type::Expression:
            {
                for (int i = 0; i < heldCount; ++i)
                {
                    auto& h = held[(size_t) i];
                    if (h.id != e.noteId) continue;
                    if (e.dim == ExprDim::Pitch) h.pitch = e.value;
                    else if (e.dim == ExprDim::Pressure) h.pressure = e.value;
                    else h.slide = e.value;
                }
                out.add (e);   // voices following this finger update live
                break;
            }
            case NoteEvent::Type::AllNotesOff:
                releaseAll (e.sampleOffset, out);
                out.add (e);
                break;
            case NoteEvent::Type::Global:
                out.add (e);
                break;
        }
    }

    fireUntil (b1);
    tracker.process (b0, b1, t, n, out);
}

// =============================================================================================
// ClipPlayer
// =============================================================================================
void ClipPlayer::reset() noexcept
{
    active = {};
    lastClip = nullptr;
}

void ClipPlayer::releaseAll (int sampleOffset, NoteEventList& out) noexcept
{
    for (auto& a : active)
    {
        if (a.used)
            out.add (NoteEvent::noteOff (sampleOffset, a.id, 60, 1, 0.5f, NoteOrigin::Clip));
        a.used = false;
    }
}

void ClipPlayer::playSegment (const NoteClip& clip, double p0, double p1, int offset0, const TransportInfo& t, int numSamples,
                              NoteIdAllocator& ids, NoteEventList& out) noexcept
{
    const double bps = std::max (1.0e-12, t.beatsPerSample);
    auto offsetOf = [&] (double position) { return juce::jlimit (0, numSamples - 1, offset0 + (int) std::floor ((position - p0) / bps)); };

    // Note ends
    for (auto& a : active)
    {
        if (a.used && a.endPosition < p1)
        {
            const auto* note = clip.findNote (a.uid);
            out.add (NoteEvent::noteOff (offsetOf (std::max (p0, a.endPosition)), a.id, note != nullptr ? note->noteNumber : 60, 1,
                                         note != nullptr ? note->releaseVelocity : 0.5f, NoteOrigin::Clip));
            a.used = false;
        }
    }

    // Expression of sounding notes, every 64 samples
    const int segmentSamples = std::max (1, (int) std::ceil ((p1 - p0) / bps));
    for (auto& a : active)
    {
        if (! a.used)
            continue;
        const auto* note = clip.findNote (a.uid);
        if (note == nullptr)
            continue;

        for (int s = 0; s < segmentSamples; s += 64)
        {
            const float time = (float) (p0 + s * bps - a.startPosition);
            const float pitch = evaluateCurve (note->pitch, time, 0.0f);
            const float pressure = evaluateCurve (note->pressure, time, 0.0f);
            const float slide = evaluateCurve (note->slide, time, 0.0f);
            const int offset = juce::jlimit (0, numSamples - 1, offset0 + s);
            if (std::abs (pitch - a.lastPitch) > 1.0e-4f)       { out.add (NoteEvent::expression (offset, a.id, ExprDim::Pitch, pitch, NoteOrigin::Clip)); a.lastPitch = pitch; }
            if (std::abs (pressure - a.lastPressure) > 1.0e-4f) { out.add (NoteEvent::expression (offset, a.id, ExprDim::Pressure, pressure, NoteOrigin::Clip)); a.lastPressure = pressure; }
            if (std::abs (slide - a.lastSlide) > 1.0e-4f)       { out.add (NoteEvent::expression (offset, a.id, ExprDim::Slide, slide, NoteOrigin::Clip)); a.lastSlide = slide; }
        }
    }

    // Note starts (notes are sorted by start)
    const auto begin = std::lower_bound (clip.notes.begin(), clip.notes.end(), p0, [] (const ClipNote& n, double v) { return n.start < v; });
    for (auto it = begin; it != clip.notes.end() && it->start < p1; ++it)
    {
        Active* slot = nullptr;
        for (auto& a : active)
            if (! a.used) { slot = &a; break; }
        if (slot == nullptr)
            break;

        const float pitch = evaluateCurve (it->pitch, 0.0f, 0.0f);
        const float pressure = evaluateCurve (it->pressure, 0.0f, 0.0f);
        const float slide = evaluateCurve (it->slide, 0.0f, 0.0f);
        const uint32_t id = ids.next();
        out.add (NoteEvent::noteOn (offsetOf (it->start), id, it->noteNumber, 1, it->velocity, NoteOrigin::Clip, pitch, pressure, slide));
        *slot = { true, id, it->uid, it->start, it->end(), pitch, pressure, slide };
    }
}

void ClipPlayer::process (const NoteClip* clip, ClipTransport& transport, const TransportInfo& t, NoteIdAllocator& ids, NoteEventList& out) noexcept
{
    const int n = t.numSamples;

    if (clip != lastClip)
    {
        // The clip was edited: stop notes that no longer exist, keep the rest sounding.
        for (auto& a : active)
        {
            if (a.used && (clip == nullptr || clip->findNote (a.uid) == nullptr))
            {
                out.add (NoteEvent::noteOff (0, a.id, 60, 1, 0.5f, NoteOrigin::Clip));
                a.used = false;
            }
        }
        lastClip = clip;
    }

    if (transport.syncToHost && t.hostTransportAvailable)
        transport.playing = t.hostPlaying || transport.recording;

    if (! transport.playing)
    {
        releaseAll (0, out);
        return;
    }

    const double length = clip != nullptr ? std::max (1.0, clip->lengthBeats) : 16.0;
    const bool looping = transport.loop && clip != nullptr && ! clip->notes.empty();

    if (transport.syncToHost && t.hostPlaying)
        transport.position = looping ? std::fmod (t.ppqAtBlockStart, length) : t.ppqAtBlockStart;

    const double p0 = transport.position;
    const double p1 = p0 + t.beatsPerSample * n;

    if (clip == nullptr)
    {
        transport.position = p1;
        return;
    }

    if (looping && p1 >= length)
    {
        const int wrap = juce::jlimit (0, n - 1, (int) std::floor ((length - p0) / std::max (1.0e-12, t.beatsPerSample)));
        playSegment (*clip, p0, length, 0, t, n, ids, out);
        releaseAll (wrap, out);
        playSegment (*clip, 0.0, p1 - length, wrap, t, n, ids, out);
        transport.position = p1 - length;
        return;
    }

    playSegment (*clip, p0, p1, 0, t, n, ids, out);
    transport.position = p1;

    if (! looping && ! transport.recording && p0 >= length)
    {
        transport.playing = false;
        releaseAll (0, out);
    }
}

// =============================================================================================
// MPE MIDI output
// =============================================================================================
void MpeMidiOutput::reset() noexcept
{
    channelNote.fill (0);
    channelExpr.fill (0);
    channelKey.fill (0);
    channelAge.fill (0);
}

int MpeMidiOutput::channelFor (uint32_t id) const noexcept
{
    for (int ch = 2; ch <= 16; ++ch)
        if (channelNote[(size_t) ch] == id)
            return ch;
    return -1;
}

int MpeMidiOutput::allocate (uint32_t id, uint32_t exprId, juce::MidiBuffer& out, int offset) noexcept
{
    int best = -1;
    uint32_t oldest = std::numeric_limits<uint32_t>::max();
    for (int ch = 2; ch <= 16; ++ch)
    {
        if (channelNote[(size_t) ch] == 0 && channelAge[(size_t) ch] < oldest)
        {
            oldest = channelAge[(size_t) ch];
            best = ch;
        }
    }

    if (best < 0)
    {
        // All 15 member channels busy: steal the oldest note.
        for (int ch = 2; ch <= 16; ++ch)
            if (channelAge[(size_t) ch] < oldest) { oldest = channelAge[(size_t) ch]; best = ch; }
        out.addEvent (juce::MidiMessage::noteOff (best, channelKey[(size_t) best]), offset);
    }

    channelNote[(size_t) best] = id;
    channelExpr[(size_t) best] = exprId;
    channelAge[(size_t) best] = ++age;
    return best;
}

void MpeMidiOutput::sendConfiguration (juce::MidiBuffer& out, int offset, float bendRange) noexcept
{
    // MPE Configuration Message: lower zone with 15 member channels, then the member bend range.
    out.addEvent (juce::MidiMessage::controllerEvent (1, 101, 0), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 100, 6), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (1, 6, 15), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (2, 101, 0), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (2, 100, 0), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (2, 6, juce::jlimit (1, 96, juce::roundToInt (bendRange))), offset);
    out.addEvent (juce::MidiMessage::controllerEvent (2, 38, 0), offset);
}

void MpeMidiOutput::allNotesOff (juce::MidiBuffer& out, int offset) noexcept
{
    for (int ch = 2; ch <= 16; ++ch)
        if (channelNote[(size_t) ch] != 0)
            out.addEvent (juce::MidiMessage::noteOff (ch, channelKey[(size_t) ch]), offset);
    reset();
}

void MpeMidiOutput::process (const NoteEventList& events, juce::MidiBuffer& out, int offsetBase, float bendRange) noexcept
{
    auto bendValue = [bendRange] (float semitones)
    {
        return juce::jlimit (0, 16383, juce::roundToInt (8192.0f + semitones / std::max (1.0f, bendRange) * 8191.0f));
    };
    auto cc = [] (float v) { return juce::jlimit (0, 127, juce::roundToInt (v * 127.0f)); };

    for (const auto& e : events)
    {
        const int offset = offsetBase + e.sampleOffset;

        switch (e.type)
        {
            case NoteEvent::Type::NoteOn:
            {
                if (e.origin == NoteOrigin::Live)
                    break;
                const int ch = allocate (e.noteId, e.exprId, out, offset);
                channelKey[(size_t) ch] = e.noteNumber;
                out.addEvent (juce::MidiMessage::pitchWheel (ch, bendValue (e.pitch)), offset);
                out.addEvent (juce::MidiMessage::controllerEvent (ch, 74, cc (e.slide)), offset);
                out.addEvent (juce::MidiMessage::channelPressureChange (ch, cc (e.pressure)), offset);
                out.addEvent (juce::MidiMessage::noteOn (ch, e.noteNumber, juce::jlimit (0.01f, 1.0f, e.value)), offset);
                break;
            }
            case NoteEvent::Type::NoteOff:
            {
                const int ch = channelFor (e.noteId);
                if (ch < 0)
                    break;
                out.addEvent (juce::MidiMessage::noteOff (ch, channelKey[(size_t) ch], juce::jlimit (0.0f, 1.0f, e.value)), offset);
                channelNote[(size_t) ch] = 0;
                channelExpr[(size_t) ch] = 0;
                break;
            }
            case NoteEvent::Type::Expression:
            {
                // Generated notes follow either their own id or the finger they came from (arp).
                for (int ch = 2; ch <= 16; ++ch)
                {
                    if (channelNote[(size_t) ch] == 0 || (channelNote[(size_t) ch] != e.noteId && channelExpr[(size_t) ch] != e.noteId))
                        continue;
                    if (e.dim == ExprDim::Pitch)         out.addEvent (juce::MidiMessage::pitchWheel (ch, bendValue (e.value)), offset);
                    else if (e.dim == ExprDim::Pressure) out.addEvent (juce::MidiMessage::channelPressureChange (ch, cc (e.value)), offset);
                    else                                 out.addEvent (juce::MidiMessage::controllerEvent (ch, 74, cc (e.value)), offset);
                }
                break;
            }
            case NoteEvent::Type::AllNotesOff:
                allNotesOff (out, offset);
                break;
            case NoteEvent::Type::Global:
                break;
        }
    }
}

} // namespace nedd
