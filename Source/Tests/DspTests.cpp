#include "TestHelpers.h"
#include "DSP/Envelope.h"
#include "DSP/VoiceFilter.h"
#include "Voices/Oscillator.h"

namespace nedd::test
{
class DspTests : public juce::UnitTest
{
public:
    DspTests() : juce::UnitTest ("DSP stability", "NeddPE") {}

    void runTest() override
    {
        beginTest ("Envelope stages and timing");
        {
            dsp::Envelope env;
            env.setSampleRate (1000.0f);
            dsp::Envelope::Settings s;
            s.attack = 0.1f; s.decay = 0.1f; s.sustain = 0.5f; s.release = 0.1f;
            s.attackCurve = s.decayCurve = s.releaseCurve = 0.0f;
            env.setSettings (s);
            env.noteOn (true);

            for (int i = 0; i < 50; ++i) env.process();
            expectWithinAbsoluteError (env.getValue(), 0.5f, 0.02f, "linear attack halfway");
            for (int i = 0; i < 60; ++i) env.process();
            expect (env.getStage() == dsp::Envelope::Stage::Decay);
            for (int i = 0; i < 200; ++i) env.process();
            expectWithinAbsoluteError (env.getValue(), 0.5f, 0.01f, "sustain level");
            env.noteOff();
            for (int i = 0; i < 150; ++i) env.process();
            expect (! env.isActive(), "idle after release");
        }

        beginTest ("Envelope retrigger continues from the current level");
        {
            dsp::Envelope env;
            env.setSampleRate (1000.0f);
            dsp::Envelope::Settings s;
            s.attack = 0.1f; s.decay = 0.2f; s.sustain = 0.6f; s.release = 0.5f;
            env.setSettings (s);
            env.noteOn (true);
            for (int i = 0; i < 400; ++i) env.process();
            env.noteOff();
            for (int i = 0; i < 50; ++i) env.process();
            const float before = env.getValue();
            env.noteOn (false);
            const float after = env.process();
            expect (std::abs (after - before) < 0.05f, "no jump on retrigger");
        }

        beginTest ("Oscillators are finite and bounded for every engine");
        {
            for (int engineIndex = 0; engineIndex < 4; ++engineIndex)
                for (int wave = 0; wave < 5; ++wave)
                    for (float freq : { 20.0f, 440.0f, 5000.0f, 18000.0f })
                    {
                        Oscillator osc;
                        osc.prepare (48000.0f);
                        dsp::Random32 rng;
                        osc.noteOn (0.0f, 0.0f, rng);

                        OscillatorBlockParams p;
                        p.engine = (OscEngine) engineIndex;
                        p.wave = (AnalogWave) wave;
                        p.noise = (NoiseType) wave;
                        p.table = &WavetableBank::getInstance().get (wave);
                        p.wtPosition = 0.7f;
                        p.frequency = freq;
                        p.unison = 7;
                        p.detune = 0.5f;
                        p.spread = 1.0f;
                        p.op1Index = 2.0f;
                        p.op2Index = 1.0f;
                        p.feedback = 1.0f;
                        osc.setBlock (p);

                        float peak = 0.0f;
                        bool finite = true;
                        for (int i = 0; i < 4800; ++i)
                        {
                            const auto out = osc.tick (0.3f * std::sin ((float) i * 0.01f), -1.0f);
                            finite = finite && std::isfinite (out.left) && std::isfinite (out.right);
                            peak = std::max ({ peak, std::abs (out.left), std::abs (out.right) });
                        }
                        expect (finite, "finite output");
                        expect (peak < 4.0f, "bounded output engine " + juce::String (engineIndex) + " wave " + juce::String (wave)
                                                 + " freq " + juce::String (freq) + " peak " + juce::String (peak));
                    }
        }

        beginTest ("Wavetables are normalised and band-limited");
        {
            const auto& bank = WavetableBank::getInstance();
            expectEquals (bank.size(), getWavetableNames().size());
            for (int t = 0; t < bank.size(); ++t)
            {
                const auto& table = bank.get (t);
                float peak = 0.0f;
                for (int i = 0; i < Wavetable::kFrameSize; ++i)
                    peak = std::max (peak, std::abs (table.getFrame (table.numFrames / 2, 0)[i]));
                expect (peak > 0.9f && peak < 1.01f, table.name + " peak " + juce::String (peak));
            }
            expectEquals (Wavetable::selectMip (440.0f / 48000.0f, 48000.0f), 4);
            expectEquals (Wavetable::selectMip (20.0f / 48000.0f, 48000.0f), 0);
        }

        beginTest ("Filters stay stable at extreme settings");
        {
            dsp::Random32 rng;
            for (int type = 0; type <= (int) FilterType::Ladder; ++type)
            {
                dsp::VoiceFilter filter;
                filter.prepare (48000.0f);
                filter.setType ((FilterType) type);
                bool finite = true;
                float peak = 0.0f;

                for (int block = 0; block < 400; ++block)
                {
                    // Sweep cutoff across the whole range at full resonance and drive.
                    const float cutoff = 20.0f * std::pow (1000.0f, (float) (block % 100) / 99.0f);
                    filter.setBlockTargets (cutoff, 1.0f, block % 2 == 0 ? 1.0f : 0.0f, 32);
                    for (int i = 0; i < 32; ++i)
                    {
                        float l = rng.nextBipolar(), r = rng.nextBipolar();
                        filter.process (l, r);
                        finite = finite && std::isfinite (l) && std::isfinite (r);
                        peak = std::max (peak, std::abs (l));
                    }
                }
                expect (finite, "filter " + juce::String (type) + " finite");
                expect (peak < 60.0f, "filter " + juce::String (type) + " bounded, peak " + juce::String (peak));
            }
        }

        beginTest ("Low-pass attenuates above cutoff");
        {
            dsp::VoiceFilter filter;
            filter.prepare (48000.0f);
            filter.setType (FilterType::LowPass24);
            filter.setBlockTargets (500.0f, 0.0f, 0.0f, 1);
            float energyIn = 0.0f, energyOut = 0.0f;
            for (int i = 0; i < 48000; ++i)
            {
                float l = std::sin (2.0f * dsp::kPi * 8000.0f * (float) i / 48000.0f);
                float r = l;
                energyIn += l * l;
                filter.process (l, r);
                if (i > 1000) energyOut += l * l;
            }
            expect (energyOut < energyIn * 1.0e-4f, "8 kHz strongly attenuated by a 500 Hz 24 dB low-pass");
        }

        beginTest ("Full engine renders every oscillator engine without NaN");
        {
            for (int engineIndex = 0; engineIndex < 4; ++engineIndex)
            {
                auto harness = std::make_unique<EngineHarness>();
                auto& h = *harness;
                for (int o = 0; o < kNumOscillators; ++o)
                {
                    h.set (pid::osc (o, OscField::On), 1.0f);
                    h.set (pid::osc (o, OscField::Engine), (float) engineIndex);
                    h.set (pid::osc (o, OscField::Unison), 4.0f);
                    h.set (pid::osc (o, OscField::FmAmount), 0.5f);
                    h.set (pid::osc (o, OscField::Ring), 0.3f);
                    h.set (pid::osc (o, OscField::Sync), o == 1 ? 1.0f : 0.0f);
                }
                for (int n = 0; n < 8; ++n)
                    h.noteOn (2 + n, 36 + n * 7);
                h.renderSeconds (0.5);
                expect (! h.sawNonFinite, "engine " + juce::String (engineIndex) + " finite");
                expect (h.lastPeak > 0.001f && h.lastPeak <= 1.0f, "engine " + juce::String (engineIndex) + " level " + juce::String (h.lastPeak));
            }
        }
    }
};

static DspTests dspTests;

} // namespace nedd::test
