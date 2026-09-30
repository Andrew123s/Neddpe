#include "PluginEditor/PluginEditor.h"
#include <iostream>

namespace nedd::test
{
namespace
{
    /** Plays a small expressive chord so the visualisers have something to show. */
    void playChord (NeddPEAudioProcessor& processor, bool start)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        const int notes[] = { 48, 55, 60, 64 };
        const float pressures[] = { 0.25f, 0.55f, 0.8f, 0.4f };
        const float slides[] = { 0.2f, 0.45f, 0.7f, 0.9f };

        for (int block = 0; block < 60; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0 && start)
                for (int i = 0; i < 4; ++i)
                    midi.addEvent (juce::MidiMessage::noteOn (i + 2, notes[i], 0.5f + 0.12f * (float) i), 0);

            for (int i = 0; i < 4; ++i)
            {
                const float t = (float) block / 60.0f;
                midi.addEvent (juce::MidiMessage::channelPressureChange (i + 2, juce::roundToInt (127.0f * pressures[i] * (0.6f + 0.4f * t))), 0);
                midi.addEvent (juce::MidiMessage::controllerEvent (i + 2, 74, juce::roundToInt (127.0f * slides[i] * t)), 0);
                if (i == 2)
                    midi.addEvent (juce::MidiMessage::pitchWheel (i + 2, 8192 + juce::roundToInt (t * 420.0f)), 0);
            }

            processor.processBlock (buffer, midi);
        }
    }
} // namespace

/** Renders every editor page to PNG files so the interface can be inspected without a host. */
int renderUiSnapshots (const juce::File& directory)
{
    directory.createDirectory();
    std::cout << "creating processor" << std::endl;
    auto processor = std::make_unique<NeddPEAudioProcessor>();
    processor->prepareToPlay (48000.0, 256);
    std::cout << "creating editor" << std::endl;

    std::unique_ptr<juce::AudioProcessorEditor> base (processor->createEditor());
    auto* editor = dynamic_cast<NeddPEEditor*> (base.get());
    if (editor == nullptr)
        return 1;

    // A few routes so modulation rings and the matrix are populated.
    auto set = [&processor] (int index, float value) { processor->setParameterPlain (index, value); };
    set (pid::mod (3, ModSlotField::Source), (float) ModSource::Lfo1);
    set (pid::mod (3, ModSlotField::Dest), (float) ModDest::Osc1WtPos);
    set (pid::mod (3, ModSlotField::Amount), 0.4f);
    set (pid::mod (3, ModSlotField::Polarity), (float) ModPolarity::Bipolar);
    set (pid::osc (1, OscField::On), 1.0f);
    set (pid::osc (1, OscField::Engine), (float) OscEngine::Wavetable);
    set (pid::osc (2, OscField::On), 1.0f);
    set (pid::osc (2, OscField::Engine), (float) OscEngine::FM);

    editor->setVisible (true);

    bool first = true;
    for (int id : editor->getPageIds())
    {
        std::cout << "page " << id << std::endl;
        editor->showPage (id);
        for (int frame = 0; frame < 20; ++frame)
        {
            playChord (*processor, first);
            first = false;
            editor->tickForTesting (1);
        }

        std::cout << "  snapshot" << std::endl;
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = directory.getChildFile ("page-" + juce::String (id).paddedLeft ('0', 2) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << "wrote " << file.getFullPathName() << "\n";
    }
    return 0;
}

} // namespace nedd::test
