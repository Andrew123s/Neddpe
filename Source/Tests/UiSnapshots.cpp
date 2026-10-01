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

    // A short expressive clip so the note editor has something to show.
    NoteClip clip;
    clip.lengthBeats = 16.0;
    struct Demo { int note; double start, length; float velocity; ExprCurve pitch; };
    const Demo demo[] = {
        { 48, 0.0, 3.0, 0.9f, { { 0.0f, 0.0f }, { 1.5f, 0.0f }, { 2.2f, 7.0f }, { 3.0f, 7.0f } } },
        { 60, 0.5, 2.5, 0.6f, {} },
        { 63, 1.0, 2.0, 0.55f, { { 0.0f, 0.0f }, { 1.4f, 0.0f }, { 2.0f, -1.0f } } },
        { 67, 4.0, 3.5, 0.8f, { { 0.0f, 0.0f }, { 0.6f, 5.0f }, { 2.5f, 5.0f }, { 3.5f, 0.0f } } },
        { 55, 4.5, 3.0, 0.7f, {} },
        { 70, 8.0, 1.0, 0.65f, {} },
        { 72, 9.0, 1.0, 0.75f, {} },
        { 74, 10.0, 2.0, 0.85f, { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, -12.0f } } },
        { 50, 8.0, 4.0, 0.6f, {} },
        { 62, 12.5, 3.0, 0.7f, { { 0.0f, -3.0f }, { 0.8f, 0.0f } } },
    };
    for (const auto& d : demo)
    {
        ClipNote n;
        n.noteNumber = d.note;
        n.start = d.start;
        n.length = d.length;
        n.velocity = d.velocity;
        n.releaseVelocity = 0.4f;
        n.pitch = d.pitch;
        const float len = (float) d.length;
        n.pressure = { { 0.0f, 0.15f }, { len * 0.35f, 0.85f }, { len, 0.35f } };
        n.slide = { { 0.0f, 0.1f }, { len, 0.6f } };
        clip.addNote (n);
    }
    processor->setClip (clip);

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
