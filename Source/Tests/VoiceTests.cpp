#include "TestHelpers.h"

namespace nedd::test
{
class VoiceTests : public juce::UnitTest
{
public:
    VoiceTests() : juce::UnitTest ("Voice allocation", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Notes produce sound and release to silence");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            expect (h.renderSeconds (0.2) > 0.01f, "note is audible");
            h.noteOff (2, 60);
            h.renderSeconds (1.5);
            expectEquals (h.engine.getVoiceManager().getNumActiveVoices(), 0);
            expect (h.render() < 1.0e-6f, "silent after release");
            expect (! h.sawNonFinite);
        }

        beginTest ("Polyphony limit and voice stealing");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::Polyphony), 4.0f);
            for (int i = 0; i < 6; ++i)
            {
                h.noteOn (2 + i, 60 + i);
                h.render();
            }
            expect (h.soundingVoices() <= 4);
            expect (h.voiceForNote (60) == nullptr, "oldest note was stolen");
            expect (h.voiceForNote (65) != nullptr, "newest note plays");
        }

        beginTest ("Stolen voice stops following its note's expression");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::Polyphony), 2.0f);
            h.noteOn (2, 60);
            h.render();
            h.noteOn (3, 62);
            h.render();
            h.noteOn (4, 64);                // steals note 60 (ch 2)
            h.render();
            h.pressure (2, 1.0f);            // expression for the stolen note
            h.renderSeconds (0.1);
            expectWithinAbsoluteError (h.voiceForNote (62)->getState().smoothedPressure, 0.0f, 1.0e-4f);
            expectWithinAbsoluteError (h.voiceForNote (64)->getState().smoothedPressure, 0.0f, 1.0e-4f);
        }

        beginTest ("Voice steal does not click");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::Polyphony), 1.0f);
            h.set (pid::env (pid::ampEnv, EnvField::Attack), 0.0f);
            h.noteOn (2, 48);
            h.renderSeconds (0.2);
            h.noteOn (3, 55);
            h.render();
            expect (! h.sawNonFinite);
            expect (h.lastPeak < 1.0f);
        }

        beginTest ("Legato mode: one voice, falls back to the held note");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::VoiceMode), (float) VoiceMode::Legato);
            h.set (pid::global (GlobalField::GlideMode), (float) GlideMode::Off);
            h.render();
            h.noteOn (2, 60);
            h.render();
            h.noteOn (3, 67);
            h.render();
            expectEquals (h.soundingVoices(), 1);
            expect (h.voiceForNote (67) != nullptr);
            h.noteOff (3, 67);
            h.render();
            const auto* v = h.voiceForNote (60);
            expect (v != nullptr && v->isGated(), "returned to the held note");
        }

        beginTest ("Mono voice follows the expression of the note on top");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::VoiceMode), (float) VoiceMode::Mono);
            h.render();
            h.noteOn (2, 60);
            h.noteOn (3, 64);
            h.pressure (2, 0.9f);
            h.pressure (3, 0.3f);
            h.renderSeconds (0.2);
            expectWithinAbsoluteError (h.voiceForNote (64)->getState().smoothedPressure, 0.3f, 0.01f);
        }

        beginTest ("Glide moves pitch gradually");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::VoiceMode), (float) VoiceMode::Legato);
            h.set (pid::global (GlobalField::GlideMode), (float) GlideMode::Always);
            h.set (pid::global (GlobalField::Glide), 0.3f);
            h.render();
            h.noteOn (2, 48);
            h.render (4);
            h.noteOn (3, 60);
            h.render();
            const float mid = h.voiceForNote (60)->getBasePitch();
            expect (mid > 48.5f && mid < 59.5f, "mid-glide pitch " + juce::String (mid));
            h.renderSeconds (1.0);
            expectWithinAbsoluteError (h.voiceForNote (60)->getBasePitch(), 60.0f, 0.01f);
        }
    }
};

static VoiceTests voiceTests;

} // namespace nedd::test
