#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParameterDefs.h"
#include <iostream>

namespace nedd::test
{
namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::cout << (condition ? "  [ ok ] " : "  [FAIL] ") << what << "\n";
        if (! condition)
            ++failures;
    }

    float renderPeak (juce::AudioPluginInstance& plugin, int blocks, bool withNotes)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        float peak = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (withNotes && b == 0)
            {
                for (int n = 0; n < 3; ++n)
                {
                    midi.addEvent (juce::MidiMessage::channelPressureChange (2 + n, 80), 0);
                    midi.addEvent (juce::MidiMessage::controllerEvent (2 + n, 74, 64), 0);
                    midi.addEvent (juce::MidiMessage::noteOn (2 + n, 48 + n * 7, 0.8f), 0);
                }
            }
            if (withNotes && b == 20)
                midi.addEvent (juce::MidiMessage::pitchWheel (2, 8192 + 2000), 0);   // per-note bend on one channel

            buffer.clear();
            plugin.processBlock (buffer, midi);
            peak = std::max (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
        }
        return peak;
    }
} // namespace

/** Loads the built VST3 through JUCE's VST3 host, like a DAW would, and exercises it. */
int validateVst3 (const juce::File& file)
{
    std::cout << "Validating " << file.getFullPathName() << "\n";
    failures = 0;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, file.getFullPathName());
    check (types.size() == 1, "module exposes one plugin (" + juce::String (types.size()) + ")");
    if (types.isEmpty())
        return 1;

    const auto& desc = *types[0];
    std::cout << "  name: " << desc.name << " | vendor: " << desc.manufacturerName << " | version: " << desc.version
              << " | category: " << desc.category << "\n";
    check (desc.name == "NeddPE", "plugin name");
    check (desc.isInstrument, "registered as an instrument");

    juce::String error;
    auto plugin = format.createInstanceFromDescription (desc, 48000.0, 256, error);
    check (plugin != nullptr, "instantiates" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));
    if (plugin == nullptr)
        return 1;

    check (plugin->acceptsMidi(), "accepts MIDI");
    check (plugin->getTotalNumOutputChannels() == 2, "stereo output");
    check (plugin->getParameters().size() >= pid::count,
           "exposes all parameters (" + juce::String (plugin->getParameters().size()) + " >= " + juce::String (pid::count) + ")");

    plugin->setPlayConfigDetails (0, 2, 48000.0, 256);
    plugin->prepareToPlay (48000.0, 256);

    check (renderPeak (*plugin, 20, false) < 1.0e-6f, "silent without notes");
    const float peak = renderPeak (*plugin, 200, true);
    check (peak > 0.01f && peak <= 1.0f, "MPE notes produce audio within full scale (peak " + juce::String (peak, 3) + ")");

    // Real-time speed through the VST3 wrapper.
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        renderPeak (*plugin, 1875, true);   // 10 s of audio
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        std::cout << "  10 s of 3-note MPE audio rendered in " << juce::String (ms, 1) << " ms (" << juce::String (ms / 100.0, 2) << "% of real time)\n";
        check (ms < 10000.0, "faster than real time");
    }

    // State round trip through the host interface.
    auto* cutoff = [&]() -> juce::AudioProcessorParameter*
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (64) == "Filter Cutoff")
                return p;
        return nullptr;
    }();
    check (cutoff != nullptr, "parameter lookup by name");
    if (cutoff != nullptr)
    {
        cutoff->setValueNotifyingHost (0.321f);
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        check (state.getSize() > 1000, "state saved (" + juce::String ((int) state.getSize()) + " bytes)");

        auto second = format.createInstanceFromDescription (desc, 48000.0, 256, error);
        if (second != nullptr)
        {
            second->setStateInformation (state.getData(), (int) state.getSize());
            float restored = -1.0f;
            for (auto* p : second->getParameters())
                if (p->getName (64) == "Filter Cutoff")
                    restored = p->getValue();
            check (std::abs (restored - 0.321f) < 1.0e-3f, "state restored in a new instance (" + juce::String (restored, 4) + ")");
        }
    }

    // Editor
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr, "editor opens");
        if (editor != nullptr)
        {
            std::cout << "  editor size " << editor->getWidth() << " x " << editor->getHeight() << "\n";
            check (editor->getWidth() >= 900 && editor->getHeight() >= 600, "editor has a usable size");
        }
    }

    plugin->releaseResources();
    std::cout << (failures == 0 ? "VST3 validation passed\n" : "VST3 validation FAILED\n");
    return failures == 0 ? 0 : 1;
}

} // namespace nedd::test
