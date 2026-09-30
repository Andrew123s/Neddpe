#include "TestHelpers.h"
#include "Sequencer/ClipTools.h"

namespace nedd::test
{
namespace
{
    TransportInfo transportAt (double ppq, int numSamples, double bpm = 120.0, double sampleRate = 48000.0)
    {
        TransportInfo t;
        t.sampleRate = sampleRate;
        t.bpm = bpm;
        t.beatsPerSample = bpm / 60.0 / sampleRate;
        t.ppqAtBlockStart = ppq;
        t.numSamples = numSamples;
        t.hostPlaying = true;
        t.hostTransportAvailable = true;
        return t;
    }

    struct Collected
    {
        std::vector<NoteEvent> events;
        int count (NoteEvent::Type type) const
        {
            return (int) std::count_if (events.begin(), events.end(), [type] (const NoteEvent& e) { return e.type == type; });
        }
    };

    /** Runs a generator over `beats` beats in 256-sample blocks. */
    template <typename Fn>
    Collected runBeats (double beats, Fn&& processBlock)
    {
        Collected c;
        const auto first = transportAt (0.0, 256);
        const int blocks = (int) std::ceil (beats / (first.beatsPerSample * 256));
        auto list = std::make_unique<NoteEventList>();
        for (int b = 0; b < blocks; ++b)
        {
            list->clear();
            const auto t = transportAt (first.beatsPerSample * 256 * b, 256);
            processBlock (t, *list);
            for (const auto& e : *list)
            {
                auto copy = e;
                copy.sampleOffset += b * 256;   // absolute sample time
                c.events.push_back (copy);
            }
        }
        return c;
    }
} // namespace

class SequencerTests : public juce::UnitTest
{
public:
    SequencerTests() : juce::UnitTest ("Sequencer, arpeggiator and clip", "NeddPE") {}

    void runTest() override
    {
        ParamSnapshot p;
        p.setToDefaults();

        beginTest ("Step sequencer plays every 1/16 step with its note and expression");
        {
            SequencerPattern pattern;
            for (auto& s : pattern.steps) { s.probability = 1.0f; s.ratchet = 1; s.on = true; s.pitch = 0.0f; }
            pattern.steps[2].pitch = 5.0f;

            StepSequencer seq;
            NoteIdAllocator ids;
            dsp::Random32 rng;
            auto c = runBeats (4.0, [&] (const TransportInfo& t, NoteEventList& out) { seq.process (pattern, p, t, true, ids, rng, out); });

            expectEquals (c.count (NoteEvent::Type::NoteOn), 16, "16 sixteenths in a bar");
            std::vector<int> notes;
            for (const auto& e : c.events)
                if (e.type == NoteEvent::Type::NoteOn) notes.push_back (e.noteNumber);
            expectEquals (notes[0], pattern.steps[0].note);
            expectEquals (notes[5], pattern.steps[5].note);

            const auto firstOn = std::find_if (c.events.begin(), c.events.end(), [] (const NoteEvent& e) { return e.type == NoteEvent::Type::NoteOn; });
            expectWithinAbsoluteError (firstOn->pressure, pattern.steps[0].pressure, 1.0e-6f);
            expectWithinAbsoluteError (firstOn->slide, pattern.steps[0].slide, 1.0e-6f);

            const bool glided = std::any_of (c.events.begin(), c.events.end(), [] (const NoteEvent& e)
                                             { return e.type == NoteEvent::Type::Expression && e.dim == ExprDim::Pitch && e.value > 1.0f; });
            expect (glided, "step 3 glides its pitch towards +5 st");
            expectEquals (c.count (NoteEvent::Type::NoteOff), c.count (NoteEvent::Type::NoteOn) - (int) 0, "every note is released");
        }

        beginTest ("Sequencer ratchet, probability and swing");
        {
            SequencerPattern pattern;
            for (auto& s : pattern.steps) { s.probability = 0.0f; s.ratchet = 1; }
            pattern.steps[0].probability = 1.0f;
            pattern.steps[0].ratchet = 4;
            pattern.steps[1].probability = 1.0f;

            auto params = p;
            params[pid::seq (SeqField::Swing)] = 0.5f;
            StepSequencer seq;
            NoteIdAllocator ids;
            dsp::Random32 rng;
            auto c = runBeats (0.5, [&] (const TransportInfo& t, NoteEventList& out) { seq.process (pattern, params, t, true, ids, rng, out); });

            std::vector<int> onTimes;
            for (const auto& e : c.events)
                if (e.type == NoteEvent::Type::NoteOn) onTimes.push_back (e.sampleOffset);
            expectEquals ((int) onTimes.size(), 5, "4 ratchets on step 1, one note on step 2");
            const double samplesPerSixteenth = 0.25 / (120.0 / 60.0 / 48000.0);
            expectWithinAbsoluteError ((double) onTimes[4], samplesPerSixteenth * 1.25, 2.0, "swung second step");
        }

        beginTest ("Arpeggiator: order, octaves and per-finger expression");
        {
            auto params = p;
            params[pid::arp (ArpField::On)] = 1.0f;
            params[pid::arp (ArpField::Mode)] = (float) ArpMode::Up;
            params[pid::arp (ArpField::Octaves)] = 2.0f;
            params[pid::arp (ArpField::Gate)] = 0.5f;

            Arpeggiator arp;
            NoteIdAllocator ids;
            dsp::Random32 rng;
            ArpPattern custom;
            bool first = true;
            auto c = runBeats (2.0, [&] (const TransportInfo& t, NoteEventList& out)
            {
                NoteEventList in;
                if (first)
                {
                    in.add (NoteEvent::noteOn (0, 1001, 64, 3, 0.7f, NoteOrigin::Live));
                    in.add (NoteEvent::noteOn (0, 1000, 60, 2, 0.7f, NoteOrigin::Live));
                    in.add (NoteEvent::noteOn (0, 1002, 67, 4, 0.7f, NoteOrigin::Live));
                    in.add (NoteEvent::expression (10, 1000, ExprDim::Pressure, 0.9f, NoteOrigin::Live));
                    first = false;
                }
                arp.process (in, out, params, custom, 0.0f, 0.0f, t, ids, rng);
            });

            std::vector<int> notes;
            std::vector<uint32_t> exprIds;
            for (const auto& e : c.events)
                if (e.type == NoteEvent::Type::NoteOn) { notes.push_back (e.noteNumber); exprIds.push_back (e.exprId); }

            const std::vector<int> expected { 60, 64, 67, 72, 76, 79, 60, 64 };
            expect (notes.size() >= expected.size());
            for (size_t i = 0; i < expected.size() && i < notes.size(); ++i)
                expectEquals (notes[i], expected[i]);
            expectEquals ((int) exprIds[0], 1000, "C arp note follows the C finger");
            expectEquals ((int) exprIds[1], 1001, "E arp note follows the E finger");

            const bool passedThrough = std::any_of (c.events.begin(), c.events.end(), [] (const NoteEvent& e)
                                                    { return e.type == NoteEvent::Type::Expression && e.noteId == 1000 && e.value > 0.8f; });
            expect (passedThrough, "held-key pressure is forwarded to the notes it spawned");
        }

        beginTest ("Arp in the engine: pressure on a held key reaches its arpeggiated voices only");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::arp (ArpField::On), 1.0f);
            h.set (pid::arp (ArpField::Mode), (float) ArpMode::Chord);
            h.set (pid::arp (ArpField::Gate), 1.0f);
            h.render();
            h.noteOn (2, 60);
            h.noteOn (3, 67);
            h.render (4);
            h.pressure (2, 1.0f);
            h.renderSeconds (0.1);
            const auto* c = h.voiceForNote (60);
            const auto* g = h.voiceForNote (67);
            expect (c != nullptr && g != nullptr, "chord arp is sounding");
            if (c != nullptr && g != nullptr)
            {
                expect (c->getState().smoothedPressure > 0.9f, "C voice follows the C key");
                expectWithinAbsoluteError (g->getState().smoothedPressure, 0.0f, 1.0e-4f, "G voice unaffected");
            }
        }

        beginTest ("Clip player: notes, timing and expression curves");
        {
            NoteClip clip;
            clip.lengthBeats = 4.0;
            ClipNote a;
            a.noteNumber = 62;
            a.start = 1.0;
            a.length = 1.0;
            a.pressure = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
            a.pitch = { { 0.0f, 0.0f }, { 1.0f, 12.0f } };
            clip.addNote (a);

            ClipPlayer player;
            ClipTransport transport;
            transport.playing = true;
            NoteIdAllocator ids;
            auto c = runBeats (3.0, [&] (const TransportInfo& t, NoteEventList& out)
            {
                auto local = t;
                local.hostPlaying = false;
                player.process (&clip, transport, local, ids, out);
            });

            const auto on = std::find_if (c.events.begin(), c.events.end(), [] (const NoteEvent& e) { return e.type == NoteEvent::Type::NoteOn; });
            expect (on != c.events.end());
            const double samplesPerBeat = 48000.0 / 2.0;
            expectWithinAbsoluteError ((double) on->sampleOffset, samplesPerBeat, 1.0, "note starts at beat 1");

            float maxPressure = 0.0f, maxPitch = 0.0f;
            for (const auto& e : c.events)
                if (e.type == NoteEvent::Type::Expression)
                {
                    if (e.dim == ExprDim::Pressure) maxPressure = std::max (maxPressure, e.value);
                    if (e.dim == ExprDim::Pitch) maxPitch = std::max (maxPitch, e.value);
                }
            expect (maxPressure > 0.95f, "pressure curve played");
            expect (maxPitch > 11.5f, "pitch curve played");
            expectEquals (c.count (NoteEvent::Type::NoteOff), 1);
        }

        beginTest ("Recorder turns live expression into an editable clip note");
        {
            PerformanceRecorder recorder;
            recorder.begin (NoteClip(), false, false);
            RecordedEvent on;
            on.type = RecordedEvent::NoteOn; on.noteId = 7; on.noteNumber = 65; on.value = 0.9f; on.pressure = 0.1f; on.beat = 0.5;
            recorder.consume (on);
            for (int i = 1; i <= 40; ++i)
            {
                RecordedEvent ex;
                ex.type = RecordedEvent::Expression; ex.noteId = 7; ex.dim = (uint8_t) ExprDim::Pressure;
                ex.value = 0.1f + 0.02f * (float) i; ex.beat = 0.5 + 0.025 * i;
                recorder.consume (ex);
            }
            RecordedEvent off;
            off.type = RecordedEvent::NoteOff; off.noteId = 7; off.value = 0.3f; off.beat = 1.5;
            recorder.consume (off);
            const auto clip = recorder.finish (2.0);

            expectEquals ((int) clip.notes.size(), 1);
            const auto& n = clip.notes.front();
            expectEquals (n.noteNumber, 65);
            expectWithinAbsoluteError (n.start, 0.5, 1.0e-9);
            expectWithinAbsoluteError (n.length, 1.0, 1.0e-9);
            expectWithinAbsoluteError (n.releaseVelocity, 0.3f, 1.0e-6f);
            expect (n.pressure.size() < 10, "linear ramp simplified to few points: " + juce::String ((int) n.pressure.size()));
            expectWithinAbsoluteError (evaluateCurve (n.pressure, 0.5f, 0.0f), 0.5f, 0.02f);
            expectWithinAbsoluteError (clip.lengthBeats, 4.0, 1.0e-9, "rounded up to one bar");
        }

        beginTest ("Engine records a live MPE performance");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            PerformanceRecorder recorder;
            recorder.begin (NoteClip(), false, false);
            h.shared.clipCommand.store ((int) ClipCommand::Record);
            h.render();
            h.noteOn (2, 60);
            h.render (8);
            h.pressure (2, 0.7f);
            h.slide (2, 0.4f);
            h.render (8);
            h.noteOff (2, 60);
            h.render (2);
            h.shared.clipCommand.store ((int) ClipCommand::Stop);
            h.render();

            RecordedEvent e;
            while (h.shared.recorded.pop (e))
                recorder.consume (e);
            const auto clip = recorder.finish (h.engine.getClipTransport().position);
            expectEquals ((int) clip.notes.size(), 1);
            if (! clip.notes.empty())
            {
                expectEquals (clip.notes[0].noteNumber, 60);
                expect (evaluateCurve (clip.notes[0].pressure, 100.0f, 0.0f) > 0.65f, "pressure recorded");
                expect (evaluateCurve (clip.notes[0].slide, 100.0f, 0.0f) > 0.35f, "slide recorded");
            }
        }

        beginTest ("Quantize, humanize and duplicate");
        {
            NoteClip clip;
            ClipNote n;
            n.start = 1.1;
            n.length = 0.4;
            const int uid = clip.addNote (n);
            ClipTools::quantize (clip, { uid }, 0.25, 1.0f, false);
            expectWithinAbsoluteError (clip.findNote (uid)->start, 1.0, 1.0e-9);
            const auto copies = ClipTools::duplicate (clip, { uid }, 2.0);
            expectEquals ((int) clip.notes.size(), 2);
            expectWithinAbsoluteError (clip.findNote (copies[0])->start, 3.0, 1.0e-9);
            ClipTools::humanize (clip, {}, 0.05, 0.1f, 42u);
            expect (std::abs (clip.findNote (uid)->start - 1.0) <= 0.05 + 1.0e-9);
        }

        beginTest ("Clip serialisation round trip");
        {
            NoteClip clip;
            clip.lengthBeats = 8.0;
            ClipNote n;
            n.noteNumber = 70; n.start = 2.25; n.length = 1.5; n.velocity = 0.6f;
            n.slide = { { 0.0f, 0.2f }, { 1.0f, 0.9f } };
            clip.addNote (n);
            NoteClip restored;
            restored.fromValueTree (clip.toValueTree());
            expectEquals ((int) restored.notes.size(), 1);
            expectWithinAbsoluteError (restored.notes[0].start, 2.25, 1.0e-6);
            expectWithinAbsoluteError (evaluateCurve (restored.notes[0].slide, 0.5f, 0.0f), 0.55f, 1.0e-3f);
            expectWithinAbsoluteError (restored.lengthBeats, 8.0, 1.0e-9);
        }

        beginTest ("MPE MIDI out: each generated note on its own channel with expression");
        {
            MpeMidiOutput output;
            NoteEventList events;
            events.add (NoteEvent::noteOn (0, 1, 60, 1, 0.8f, NoteOrigin::Sequencer, 2.0f, 0.5f, 0.25f));
            events.add (NoteEvent::noteOn (0, 2, 64, 1, 0.8f, NoteOrigin::Sequencer));
            events.add (NoteEvent::expression (10, 1, ExprDim::Pressure, 1.0f, NoteOrigin::Sequencer));
            events.add (NoteEvent::noteOn (0, 3, 67, 1, 0.8f, NoteOrigin::Live));   // live notes are not echoed
            juce::MidiBuffer midi;
            output.process (events, midi, 0, 48.0f);

            std::set<int> noteChannels;
            int pressureOnFirst = 0, bends = 0;
            int firstChannel = -1;
            for (const auto m : midi)
            {
                const auto msg = m.getMessage();
                if (msg.isNoteOn()) { noteChannels.insert (msg.getChannel()); if (msg.getNoteNumber() == 60) firstChannel = msg.getChannel(); }
                if (msg.isPitchWheel()) ++bends;
            }
            for (const auto m : midi)
                if (m.getMessage().isChannelPressure() && m.getMessage().getChannel() == firstChannel && m.getMessage().getChannelPressureValue() == 127)
                    ++pressureOnFirst;
            expectEquals ((int) noteChannels.size(), 2, "two generated notes on two channels");
            expect (noteChannels.count (1) == 0, "member channels only");
            expectEquals (pressureOnFirst, 1, "pressure update on the first note's channel");
            expectEquals (bends, 2);
        }
    }
};

static SequencerTests sequencerTests;

} // namespace nedd::test
