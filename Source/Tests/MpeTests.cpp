#include "TestHelpers.h"

namespace nedd::test
{
class MpeTests : public juce::UnitTest
{
public:
    MpeTests() : juce::UnitTest ("MPE handling", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Per-note pitch bend only moves its own note");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.noteOn (3, 64);
            h.render (2);
            h.pitchBend (2, 0.5f);           // +24 st with the default 48 st range
            h.renderSeconds (0.1);

            const auto* a = h.voiceForNote (60);
            const auto* b = h.voiceForNote (64);
            expect (a != nullptr && b != nullptr);
            expectWithinAbsoluteError (a->getState().smoothedPitch, 24.0f, 0.05f);
            expectWithinAbsoluteError (b->getState().smoothedPitch, 0.0f, 1.0e-4f);
        }

        beginTest ("Pressure: no cross-talk between voices");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.noteOn (3, 67);
            h.pressure (2, 0.8f);
            h.pressure (3, 0.2f);
            h.renderSeconds (0.2);

            const auto* a = h.voiceForNote (60);
            const auto* b = h.voiceForNote (67);
            expectWithinAbsoluteError (a->getState().smoothedPressure, 0.8f, 0.01f);
            expectWithinAbsoluteError (b->getState().smoothedPressure, 0.2f, 0.01f);

            h.pressure (2, 0.4f);            // change A only
            h.renderSeconds (0.2);
            expectWithinAbsoluteError (a->getState().smoothedPressure, 0.4f, 0.01f);
            expectWithinAbsoluteError (b->getState().smoothedPressure, 0.2f, 0.01f, "B must not follow A");
        }

        beginTest ("Slide (CC74) is per note");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.noteOn (3, 64);
            h.slide (3, 1.0f);
            h.renderSeconds (0.2);
            expectWithinAbsoluteError (h.voiceForNote (60)->getState().smoothedTimbre, 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (h.voiceForNote (64)->getState().smoothedTimbre, 1.0f, 0.01f);
        }

        beginTest ("Expression sent before note-on is used as the initial value");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.pressure (4, 0.5f);
            h.slide (4, 0.75f);
            h.render();
            h.noteOn (4, 72);
            h.render();
            const auto* v = h.voiceForNote (72);
            expect (v != nullptr);
            expectWithinAbsoluteError (v->getState().pressure, 0.5f, 0.01f);
            expectWithinAbsoluteError (v->getState().timbre, 0.75f, 0.01f);
        }

        beginTest ("Master channel bend applies to every note in the zone");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.noteOn (3, 64);
            h.pitchBend (2, 0.25f);          // +12 on note A
            h.pitchBend (1, 1.0f);           // master +2 st on both
            h.renderSeconds (0.1);
            expectWithinAbsoluteError (h.voiceForNote (60)->getState().smoothedPitch, 14.0f, 0.1f);
            expectWithinAbsoluteError (h.voiceForNote (64)->getState().smoothedPitch, 2.0f, 0.1f);
        }

        beginTest ("Legacy mode: channel bend and aftertouch reach all notes on that channel");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::MpeMode), (float) MpeMode::Legacy);
            h.render();
            h.noteOn (1, 60);
            h.noteOn (1, 64);
            h.pitchBend (1, 1.0f);           // master range: 2 st
            h.pressure (1, 0.6f);
            h.renderSeconds (0.1);
            for (int note : { 60, 64 })
            {
                expectWithinAbsoluteError (h.voiceForNote (note)->getState().smoothedPitch, 2.0f, 0.05f);
                expectWithinAbsoluteError (h.voiceForNote (note)->getState().smoothedPressure, 0.6f, 0.02f);
            }
        }

        beginTest ("Pitch-bend range: parameter and RPN 0");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::MpeBendRange), 12.0f);
            h.noteOn (2, 60);
            h.pitchBend (2, 1.0f);
            h.renderSeconds (0.1);
            expectWithinAbsoluteError (h.voiceForNote (60)->getState().smoothedPitch, 12.0f, 0.05f);

            // Member-channel RPN 0 = 24 st overrides the parameter.
            h.midi (juce::MidiMessage::controllerEvent (2, 101, 0));
            h.midi (juce::MidiMessage::controllerEvent (2, 100, 0));
            h.midi (juce::MidiMessage::controllerEvent (2, 6, 24));
            h.pitchBend (2, 0.5f);
            h.renderSeconds (0.1);
            expectWithinAbsoluteError (h.voiceForNote (60)->getState().smoothedPitch, 12.0f, 0.05f);
            expectWithinAbsoluteError (h.engine.getMpeInput().getMemberBendRange(), 24.0f, 1.0e-4f);
        }

        beginTest ("MPE Configuration Message sets the zone layout");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.midi (juce::MidiMessage::controllerEvent (1, 101, 0));
            h.midi (juce::MidiMessage::controllerEvent (1, 100, 6));
            h.midi (juce::MidiMessage::controllerEvent (1, 6, 7));
            h.render();
            expectEquals (h.engine.getMpeInput().getLowerZoneMembers(), 7);
            expect (h.engine.getMpeInput().isMasterChannel (1));
            expect (! h.engine.getMpeInput().isMasterChannel (8));
        }

        beginTest ("Sustain pedal holds released notes; release velocity is captured");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.midi (juce::MidiMessage::controllerEvent (1, 64, 127));
            h.noteOn (2, 60);
            h.render();
            h.noteOff (2, 60, 0.9f);
            h.render();
            expect (h.voiceForNote (60)->isGated(), "sustained note keeps its gate");
            h.midi (juce::MidiMessage::controllerEvent (1, 64, 0));
            h.render();
            const auto* v = h.voiceForNote (60);
            expect (v != nullptr && ! v->isGated());
            expectWithinAbsoluteError (v->getState().releaseVelocity, 0.9f, 0.01f);
        }

        beginTest ("Re-used MIDI channel starts a fresh note with its own id");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.pressure (2, 0.9f);
            h.render();
            h.noteOff (2, 60);
            h.pressure (2, 0.0f);
            h.render();
            h.noteOn (2, 62);
            h.renderSeconds (0.1);
            const auto* v = h.voiceForNote (62);
            expect (v != nullptr);
            expectWithinAbsoluteError (v->getState().smoothedPressure, 0.0f, 0.01f);
        }
    }
};

static MpeTests mpeTests;

} // namespace nedd::test
