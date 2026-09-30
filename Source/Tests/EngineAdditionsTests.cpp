#include "TestHelpers.h"
#include "DSP/Decimator.h"
#include "DSP/Simd.h"
#include "Presets/PresetState.h"
#include "Voices/Oscillator.h"
#include <complex>

namespace nedd::test
{
/** SIMD rendering, band-limited sync, oversampling, granular / sample engines and imported content (v0.2). */
class EngineAdditionsTests : public juce::UnitTest
{
public:
    EngineAdditionsTests() : juce::UnitTest ("Engine additions (SIMD, sync, oversampling, granular, import)", "NeddPE") {}

    /** Magnitude of one frequency in a signal (single-bin DFT with a Hann window). */
    static float magnitudeAt (const std::vector<float>& x, float freq, float sampleRate)
    {
        std::complex<double> acc;
        const auto n = x.size();
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) (n - 1));
            const double phase = 2.0 * juce::MathConstants<double>::pi * freq * (double) i / sampleRate;
            acc += std::polar ((double) x[i] * w, -phase);
        }
        return (float) (std::abs (acc) / ((double) n * 0.25));
    }

    void runTest() override
    {
        beginTest ("Polynomial sine matches std::sin (scalar and SIMD)");
        {
            float worst = 0.0f;
            for (int i = -4000; i <= 4000; ++i)
            {
                const float x = (float) i * 0.00137f;
                const float ref = (float) std::sin (2.0 * juce::MathConstants<double>::pi * (double) x);
                worst = std::max (worst, std::abs (simd::sinCycles (x) - ref));

                alignas (16) float lanes[4];
                simd::sinCycles (simd::f4 (x)).store (lanes);
                worst = std::max (worst, std::abs (lanes[3] - ref));
            }
            expect (worst < 2.0e-6f, "max error " + juce::String (worst));
        }

        beginTest ("Half-band decimator: unity DC gain, strong rejection above the output band");
        {
            dsp::OversamplingDecimator dec;
            dec.reset();
            float out = 0.0f;
            for (int i = 0; i < 200; ++i)
            {
                const float in[2] = { 1.0f, 1.0f };
                out = dec.process (in, 2);
            }
            expectWithinAbsoluteError (out, 1.0f, 1.0e-4f);

            // 0.8 x the oversampled Nyquist frequency: must not come through (it would alias to 0.4 fs).
            const float inputRate = 96000.0f, freq = 38400.0f;
            dec.reset();
            std::vector<float> y;
            for (int i = 0; i < 8192; ++i)
            {
                const float in[2] = { std::sin (2.0f * dsp::kPi * freq * (float) (2 * i) / inputRate),
                                      std::sin (2.0f * dsp::kPi * freq * (float) (2 * i + 1) / inputRate) };
                const float v = dec.process (in, 2);
                if (i >= 64) y.push_back (v);
            }
            float peak = 0.0f;
            for (auto v : y) peak = std::max (peak, std::abs (v));
            expect (peak < 1.0e-3f, "stopband leakage " + juce::String (peak));
        }

        beginTest ("Every engine is finite and bounded, with and without oversampling and sync");
        {
            for (int factor : { 1, 2, 4 })
                for (int engineIndex = 0; engineIndex < 6; ++engineIndex)
                    for (float freq : { 55.0f, 880.0f, 7000.0f })
                    {
                        Oscillator osc;
                        osc.prepare (48000.0f);
                        osc.setOversampling (factor);
                        dsp::Random32 rng;
                        osc.noteOn (0.0f, 0.0f, rng);

                        OscillatorBlockParams p;
                        p.engine = (OscEngine) engineIndex;
                        p.table = &WavetableBank::getInstance().get (5);
                        p.sample = &assets::getBuiltInSample();
                        p.wtPosition = 0.4f;
                        p.frequency = freq;
                        p.unison = 5;
                        p.detune = 0.4f;
                        p.spread = 1.0f;
                        p.op1Index = 1.5f;
                        p.feedback = 0.5f;
                        p.grainDensity = 120.0f;
                        p.grainSeconds = 0.2f;
                        p.grainPitchSpray = 3.0f;
                        osc.setBlock (p);

                        float peak = 0.0f;
                        bool finite = true;
                        for (int i = 0; i < 9600; ++i)
                        {
                            const float sync = (i % 37) == 0 ? 0.37f : -1.0f;
                            const auto out = osc.tick (0.4f * std::sin ((float) i * 0.013f), sync);
                            finite = finite && std::isfinite (out.left) && std::isfinite (out.right) && std::isfinite (out.mono);
                            peak = std::max ({ peak, std::abs (out.left), std::abs (out.right) });
                        }
                        expect (finite, "finite, engine " + juce::String (engineIndex));
                        expect (peak < 6.0f, "bounded, engine " + juce::String (engineIndex) + " x" + juce::String (factor)
                                                 + " peak " + juce::String (peak));
                    }
        }

        beginTest ("Band-limited hard sync lowers alias energy");
        {
            // Slave saw at 5.1 kHz synced to a 1 kHz master (48 kHz). Every sync harmonic is a multiple of 1 kHz;
            // energy at 1.5 kHz, 2.5 kHz, ... is aliasing. Compare against the same reset without the BLEP correction
            // by measuring the corrected oscillator and a naive reference rendered here.
            constexpr float rate = 48000.0f, master = 1000.0f, slave = 5100.0f;
            Oscillator osc;
            osc.prepare (rate);
            dsp::Random32 rng;
            osc.noteOn (0.0f, 0.0f, rng);
            OscillatorBlockParams p;
            p.engine = OscEngine::Analog;
            p.wave = AnalogWave::Saw;
            p.frequency = slave;
            osc.setBlock (p);

            std::vector<float> blep, naive;
            float masterPhase = 0.0f, naivePhase = 0.0f;
            for (int i = 0; i < 16384; ++i)
            {
                masterPhase += master / rate;
                float sync = -1.0f;
                if (masterPhase >= 1.0f)
                {
                    masterPhase -= 1.0f;
                    sync = masterPhase / (master / rate);
                }
                blep.push_back (osc.tick (0.0f, sync).mono);

                naivePhase = sync >= 0.0f ? sync * slave / rate : naivePhase + slave / rate;
                naivePhase -= std::floor (naivePhase);
                naive.push_back (2.0f * naivePhase - 1.0f);
            }

            float aliasBlep = 0.0f, aliasNaive = 0.0f;
            for (float f = 1500.0f; f < 20000.0f; f += 1000.0f)
            {
                aliasBlep += magnitudeAt (blep, f, rate);
                aliasNaive += magnitudeAt (naive, f, rate);
            }
            expect (aliasBlep < aliasNaive * 0.5f, "alias energy " + juce::String (aliasBlep) + " vs naive " + juce::String (aliasNaive));
        }

        beginTest ("Granular and Sample engines play the built-in source through the engine");
        {
            for (auto engine : { OscEngine::Granular, OscEngine::Sample })
            {
                auto harness = std::make_unique<EngineHarness>();
                auto& h = *harness;
                h.set (pid::osc (0, OscField::Engine), (float) engine);
                h.noteOn (2, 60);
                const float peak = h.renderSeconds (0.3);
                expect (peak > 0.01f && ! h.sawNonFinite, "engine " + juce::String ((int) engine) + " peak " + juce::String (peak));
            }
        }

        beginTest ("Auto oversampling applies to cross-FM notes only");
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.noteOn (2, 60);
            h.render();
            expectEquals (h.voiceForNote (60)->getOversampling(), 1);

            h.set (pid::osc (0, OscField::FmAmount), 0.3f);
            h.noteOn (3, 64);
            h.render();
            expectEquals (h.voiceForNote (64)->getOversampling(), 2);

            h.set (pid::global (GlobalField::OscOversampling), (float) OscOversampling::X4);
            h.noteOn (4, 67);
            h.render();
            expectEquals (h.voiceForNote (67)->getOversampling(), 4);
            expect (h.renderSeconds (0.2) > 0.01f && ! h.sawNonFinite);
        }

        beginTest ("Imported wavetable and sample survive a save / load round trip");
        {
            std::vector<float> frames ((size_t) 4 * Wavetable::kFrameSize);
            for (int f = 0; f < 4; ++f)
                for (int i = 0; i < Wavetable::kFrameSize; ++i)
                    frames[(size_t) (f * Wavetable::kFrameSize + i)] = std::sin (2.0f * dsp::kPi * (float) ((f + 1) * i) / (float) Wavetable::kFrameSize);

            std::vector<float> left (4000), right (4000);
            for (int i = 0; i < 4000; ++i)
            {
                left[(size_t) i] = 0.5f * std::sin ((float) i * 0.05f);
                right[(size_t) i] = 0.25f * std::cos ((float) i * 0.03f);
            }

            OscillatorAssets a;
            a.slots[1].wavetable = UserWavetable::create ("Test Table", frames);
            a.slots[2].sample = SampleData::create ("Test Sample", 44100.0, left.data(), right.data(), 4000);

            PresetState st;
            st.assets = a;
            const auto restored = PresetState::fromValueTree (PresetState::fromValueTree (st.toValueTree()).toValueTree());

            const auto& t = restored.assets.slots[1].wavetable;
            const auto& s = restored.assets.slots[2].sample;
            expect (restored.assets.slots[0].wavetable == nullptr && restored.assets.slots[0].sample == nullptr);
            expect (t != nullptr && s != nullptr);
            if (t != nullptr && s != nullptr)
            {
                expectEquals (t->numFrames, 4);
                expectWithinAbsoluteError (t->table.sample (0.25f, 0.0f, 0), 1.0f, 0.01f);
                expectEquals (s->length, 4000);
                expect (s->stereo);
                expectWithinAbsoluteError ((float) s->sampleRate, 44100.0f, 0.01f);
                float worst = 0.0f;
                for (int i = 0; i < 4000; ++i)
                    worst = std::max ({ worst, std::abs (s->channel (0)[i] - left[(size_t) i]), std::abs (s->channel (1)[i] - right[(size_t) i]) });
                expect (worst < 1.0e-5f, "24-bit round trip error " + juce::String (worst));
            }
        }
    }
};

static EngineAdditionsTests engineAdditionsTests;

} // namespace nedd::test
