#include "TestHelpers.h"
#include <iostream>

namespace nedd::test
{
/** Worst-case CPU benchmark: 16 notes, three 8-voice unison oscillators, every effect on. */
int runBenchmark()
{
    const char* qualityNames[] = { "Eco", "Normal", "High", "Ultra" };
    struct Case { const char* name; int engine; int unison; bool effects; };
    const Case cases[] = {
        { "Analog, 1 voice unison, no FX", (int) OscEngine::Analog, 1, false },
        { "Wavetable, 8-voice unison x3 osc, all FX", (int) OscEngine::Wavetable, 8, true },
        { "FM, 8-voice unison x3 osc, all FX", (int) OscEngine::FM, 8, true },
    };

    std::cout << "NeddPE CPU benchmark (48 kHz, 256-sample blocks, 16 simultaneous MPE notes, 10 s)\n";
    for (const auto& c : cases)
    {
        std::cout << "\n" << c.name << "\n";
        for (int q = 0; q < 4; ++q)
        {
            auto harness = std::make_unique<EngineHarness>();
            auto& h = *harness;
            h.set (pid::global (GlobalField::Quality), (float) q);
            h.set (pid::global (GlobalField::Polyphony), 16.0f);
            for (int o = 0; o < kNumOscillators; ++o)
            {
                h.set (pid::osc (o, OscField::On), 1.0f);
                h.set (pid::osc (o, OscField::Engine), (float) c.engine);
                h.set (pid::osc (o, OscField::Unison), (float) c.unison);
            }
            h.set (pid::filter (FilterField::Type), (float) FilterType::Ladder);
            if (c.effects)
                for (const auto& d : getParamDefs())
                    if (d.group == ParamGroup::Effects && d.type == ParamType::Bool)
                        h.set (d.index, 1.0f);

            for (int n = 0; n < 16; ++n)
            {
                h.pressure (2 + (n % 15), 0.5f);
                h.noteOn (2 + (n % 15), 36 + n * 3);
            }
            h.render (4);

            constexpr double seconds = 10.0;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int b = 0; b < (int) (seconds * EngineHarness::kSampleRate / EngineHarness::kBlock); ++b)
            {
                if (b % 8 == 0)
                    h.slide (2 + (b / 8) % 15, (float) ((b / 8) % 128) / 127.0f);   // keep expression moving
                h.render (1);
            }
            const double ms = juce::Time::getMillisecondCounterHiRes() - start;
            std::cout << "  " << qualityNames[q] << ": " << juce::String (ms / (seconds * 10.0), 1) << "% of one core ("
                      << h.engine.getVoiceManager().getNumActiveVoices() << " voices)\n";
        }
    }
    return 0;
}

} // namespace nedd::test
