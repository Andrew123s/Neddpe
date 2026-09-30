#include "TestHelpers.h"
#include "Effects/EffectsChain.h"

namespace nedd::test
{
class EffectsTests : public juce::UnitTest
{
public:
    EffectsTests() : juce::UnitTest ("Effects", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Every effect at extreme settings stays finite and bounded");
        {
            fx::EffectsChain chain;
            chain.prepare (48000.0f, 256);
            ParamSnapshot p;
            p.setToDefaults();
            for (const auto& d : getParamDefs())
                if (d.group == ParamGroup::Effects)
                    p[d.index] = d.type == ParamType::Bool ? 1.0f : d.range.end;   // everything on, everything maxed

            std::array<float, (size_t) kNumModDests> mod {};
            TransportInfo t;
            dsp::Random32 rng;
            std::vector<float> l (256), r (256), sl (256), sr (256);
            bool finite = true;
            float peak = 0.0f;

            for (int quality = 0; quality < 4; ++quality)
                for (int block = 0; block < 200; ++block)
                {
                    for (int i = 0; i < 256; ++i)
                    {
                        l[(size_t) i] = r[(size_t) i] = 0.5f * rng.nextBipolar();
                        sl[(size_t) i] = sr[(size_t) i] = l[(size_t) i];
                    }
                    chain.process (l.data(), r.data(), sl.data(), sr.data(), sl.data(), sr.data(), 256, p, mod, t, (Quality) quality);
                    for (int i = 0; i < 256; ++i)
                    {
                        finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                        peak = std::max (peak, std::abs (l[(size_t) i]));
                    }
                }
            expect (finite, "finite");
            // Maxed EQ (+18 dB) and makeup (+24 dB) are deliberate gain; what matters is no runaway.
            expect (peak < 1000.0f, "no runaway, peak " + juce::String (peak));
        }

        beginTest ("Full engine with every effect maxed never exceeds full scale");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            for (const auto& d : getParamDefs())
                if (d.group == ParamGroup::Effects && d.type != ParamType::Choice)
                    h.set (d.index, d.type == ParamType::Bool ? 1.0f : d.range.end);
            for (int n = 0; n < 6; ++n)
                h.noteOn (2 + n, 40 + n * 5, 1.0f);
            const float peak = h.renderSeconds (1.0);
            expect (! h.sawNonFinite, "finite");
            expect (peak <= 1.0f, "limited output peak " + juce::String (peak));
        }

        beginTest ("Delay repeats the send signal at the set time");
        {
            fx::StereoDelay delay;
            delay.prepare (48000.0f);
            std::vector<float> in (48000, 0.0f), out (48000, 0.0f), outR (48000, 0.0f);
            in[0] = 1.0f;
            const float delaySamples = 12000.0f;
            for (int start = 0; start < 48000; start += 256)
                delay.process (in.data() + start, in.data() + start, out.data() + start, outR.data() + start, std::min (256, 48000 - start),
                               delaySamples, 0.5f, 0.0f, false, 1.0f);
            int peakIndex = 0;
            for (int i = 1; i < 20000; ++i)
                if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) peakIndex])) peakIndex = i;
            expect (std::abs (peakIndex - 12000) <= 2, "echo at " + juce::String (peakIndex));
            expect (std::abs (out[24000]) > 0.05f || std::abs (out[24001]) > 0.05f, "feedback repeat");
        }

        beginTest ("Delay line reads exact integer delays without wrapping errors");
        {
            fx::DelayLine line;
            line.prepare (64);
            bool ok = true;
            for (int t = 0; t < 1000; ++t)
            {
                line.push ((float) t);
                if (t >= 40)
                    ok = ok && std::abs (line.read (10.0f) - (float) (t - 9)) < 1.0e-3f
                            && std::abs (line.read (10.5f) - ((float) (t - 9) - 0.5f)) < 1.0e-3f;
            }
            expect (ok, "integer and fractional delays across the wrap point");
        }

        beginTest ("Reverb produces a decaying tail");
        {
            fx::FdnReverb reverb;
            reverb.prepare (48000.0f);
            std::vector<float> in (96000, 0.0f), l (96000, 0.0f), r (96000, 0.0f);
            in[0] = 1.0f;
            for (int start = 0; start < 96000; start += 256)
                reverb.process (in.data() + start, in.data() + start, l.data() + start, r.data() + start, 256, 0.6f, 0.4f, 10.0f, 1.0f, 1.0f);
            auto energy = [&l] (int from, int to) { float e = 0; for (int i = from; i < to; ++i) e += l[(size_t) i] * l[(size_t) i]; return e; };
            expect (energy (4800, 14400) > 1.0e-4f, "tail present");
            expect (energy (72000, 96000) < energy (4800, 28800), "tail decays");
        }

        beginTest ("Per-note delay send: pressure decides which note echoes");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::fx (FxField::DelayOn), 1.0f);
            h.set (pid::fx (FxField::DelaySync), 0.0f);
            h.set (pid::fx (FxField::DelayTime), 300.0f);
            h.set (pid::fx (FxField::DelayFeedback), 0.0f);
            h.set (pid::fx (FxField::DelaySend), 0.0f);
            h.set (pid::fx (FxField::DelayReturn), 1.0f);
            h.set (pid::env (pid::ampEnv, EnvField::Release), 0.01f);
            h.set (pid::mod (4, ModSlotField::Source), (float) ModSource::MpePressure);
            h.set (pid::mod (4, ModSlotField::Dest), (float) ModDest::DelaySend);
            h.set (pid::mod (4, ModSlotField::Amount), 1.0f);

            // Unpressed note: no echo after it stops.
            h.noteOn (2, 60);
            h.renderSeconds (0.2);
            h.noteOff (2, 60);
            h.renderSeconds (0.1);
            const float quietTail = h.renderSeconds (0.25);

            // Pressed note: the echo arrives after the note has stopped.
            h.pressure (3, 1.0f);
            h.noteOn (3, 60);
            h.renderSeconds (0.2);
            h.noteOff (3, 60);
            h.renderSeconds (0.1);
            const float echoTail = h.renderSeconds (0.25);

            expect (quietTail < 1.0e-3f, "no send, no echo: " + juce::String (quietTail));
            expect (echoTail > 0.02f, "pressure send echoes: " + juce::String (echoTail));
        }
    }
};

class ModulationTests : public juce::UnitTest
{
public:
    ModulationTests() : juce::UnitTest ("Modulation matrix", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Curve and polarity shaping");
        {
            expectWithinAbsoluteError (shapeModValue (0.5f, false, ModCurve::Linear, ModPolarity::Unipolar), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (shapeModValue (0.5f, false, ModCurve::Exponential, ModPolarity::Unipolar), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (shapeModValue (0.25f, false, ModCurve::Logarithmic, ModPolarity::Unipolar), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (shapeModValue (0.0f, false, ModCurve::Linear, ModPolarity::Bipolar), -1.0f, 1.0e-6f);
            expectWithinAbsoluteError (shapeModValue (-1.0f, true, ModCurve::Linear, ModPolarity::Unipolar), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (shapeModValue (-0.5f, true, ModCurve::Exponential, ModPolarity::Bipolar), -0.25f, 1.0e-6f);
        }

        beginTest ("Routing sums amounts per destination");
        {
            ParamSnapshot p;
            p.setToDefaults();
            for (int s = 0; s < kNumModSlots; ++s)
                p[pid::mod (s, ModSlotField::Source)] = 0.0f;
            p[pid::mod (0, ModSlotField::Source)] = (float) ModSource::Macro1;
            p[pid::mod (0, ModSlotField::Dest)] = (float) ModDest::FilterCutoff;
            p[pid::mod (0, ModSlotField::Amount)] = 0.5f;
            p[pid::mod (1, ModSlotField::Source)] = (float) ModSource::Macro2;
            p[pid::mod (1, ModSlotField::Dest)] = (float) ModDest::FilterCutoff;
            p[pid::mod (1, ModSlotField::Amount)] = -0.25f;
            p[pid::mod (1, ModSlotField::Polarity)] = 0.0f;

            ModRouting routing;
            routing.build (p);
            expectEquals (routing.numRoutes, 2);

            std::array<float, (size_t) kNumModSources> sources {};
            sources[(size_t) ModSource::Macro1] = 1.0f;
            sources[(size_t) ModSource::Macro2] = 1.0f;
            std::array<float, (size_t) kNumModDests> dest {};
            evaluateModMatrix (routing, sources.data(), dest.data(), ModScope::Voice);
            expectWithinAbsoluteError (dest[(size_t) ModDest::FilterCutoff], 0.25f, 1.0e-6f);
        }

        beginTest ("Pressure > cutoff is evaluated per note");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::filter (FilterField::Cutoff), 500.0f);
            h.set (pid::filter (FilterField::KeyTrack), 0.0f);
            h.set (pid::filter (FilterField::EnvAmount), 0.0f);
            h.set (pid::mod (2, ModSlotField::Source), 0.0f);   // remove the default slide route
            h.noteOn (2, 60);
            h.noteOn (3, 60 + 12);
            h.pressure (2, 1.0f);
            h.pressure (3, 0.0f);
            h.renderSeconds (0.2);

            const float pressed = h.voiceForNote (60)->getCutoffHz();
            const float unpressed = h.voiceForNote (72)->getCutoffHz();
            // Default route: pressure > cutoff +35% of 96 st = +33.6 st.
            expectWithinAbsoluteError (dsp::hzToMidiNote (pressed) - dsp::hzToMidiNote (500.0f), 33.6f, 0.3f);
            expectWithinAbsoluteError (unpressed, 500.0f, 1.0f);
        }

        beginTest ("MPE slide drives wavetable position of one note only");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::osc (0, OscField::Engine), (float) OscEngine::Wavetable);
            h.set (pid::mod (2, ModSlotField::Dest), (float) ModDest::Osc1WtPos);
            h.set (pid::mod (2, ModSlotField::Amount), 1.0f);
            h.noteOn (2, 60);
            h.noteOn (3, 67);
            h.slide (2, 1.0f);
            h.renderSeconds (0.2);
            expectWithinAbsoluteError (h.voiceForNote (60)->getDestMods()[(size_t) ModDest::Osc1WtPos], 1.0f, 0.02f);
            expectWithinAbsoluteError (h.voiceForNote (67)->getDestMods()[(size_t) ModDest::Osc1WtPos], 0.0f, 1.0e-4f);
        }

        beginTest ("LFO rate and tempo sync");
        {
            dsp::Lfo lfo;
            lfo.reset (0.0f, 1u);
            dsp::Lfo::Settings s;
            s.shape = LfoShape::Saw;
            for (int i = 0; i < 100; ++i)
                lfo.advance (s, 2.0f, 240, 48000.0f);   // 0.5 s at 2 Hz = one full cycle
            expect (lfo.getPhase() < 0.01f || lfo.getPhase() > 0.99f, "one cycle after 0.5 s, phase " + juce::String (lfo.getPhase()));

            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::lfo (0, LfoField::Sync), 1.0f);
            h.set (pid::lfo (0, LfoField::Division), (float) divisionIndex ("1/4"));
            h.transport.hostPlaying = true;
            h.transport.ppqAtBlockStart = 10.25;
            h.render();
            expect (h.shared.telemetry.sourceValue[(size_t) ModSource::Lfo1].load() > 0.9f, "synced LFO sine at quarter phase is at its peak");
        }

        beginTest ("Macros feed the matrix");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::macro (0), 1.0f);
            h.set (pid::mod (5, ModSlotField::Source), (float) ModSource::Macro1);
            h.set (pid::mod (5, ModSlotField::Dest), (float) ModDest::FilterResonance);
            h.set (pid::mod (5, ModSlotField::Amount), 0.5f);
            h.noteOn (2, 60);
            h.renderSeconds (0.05);
            expectWithinAbsoluteError (h.voiceForNote (60)->getDestMods()[(size_t) ModDest::FilterResonance], 0.5f, 1.0e-4f);
        }

        beginTest ("Per-note morph: slide morphs one note towards B");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            auto target = std::make_unique<MorphTarget>();
            target->plain = h.params;
            target->plain[pid::filter (FilterField::Cutoff)] = 20000.0f;
            for (const auto& d : getParamDefs())
                target->normalised[(size_t) d.index] = d.range.convertTo0to1 (target->plain[d.index]);
            h.shared.morphTarget.publish (std::move (target));

            h.set (pid::global (GlobalField::MorphOn), 1.0f);
            h.set (pid::filter (FilterField::Cutoff), 200.0f);
            h.set (pid::filter (FilterField::KeyTrack), 0.0f);
            h.set (pid::filter (FilterField::EnvAmount), 0.0f);
            for (int s = 0; s < 3; ++s)
                h.set (pid::mod (s, ModSlotField::Source), 0.0f);
            h.set (pid::mod (6, ModSlotField::Source), (float) ModSource::MpeSlide);
            h.set (pid::mod (6, ModSlotField::Dest), (float) ModDest::Morph);
            h.set (pid::mod (6, ModSlotField::Amount), 1.0f);

            h.noteOn (2, 60);
            h.noteOn (3, 64);
            h.slide (2, 1.0f);
            h.renderSeconds (0.3);
            expect (h.voiceForNote (60)->getCutoffHz() > 15000.0f, "slid note reached B: " + juce::String (h.voiceForNote (60)->getCutoffHz()));
            expectWithinAbsoluteError (h.voiceForNote (64)->getCutoffHz(), 200.0f, 1.0f);
        }
    }
};

static EffectsTests effectsTests;
static ModulationTests modulationTests;

} // namespace nedd::test
