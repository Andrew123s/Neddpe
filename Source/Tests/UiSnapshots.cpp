#include "PluginEditor/PluginEditor.h"
#include <iostream>

namespace nedd::test
{
/** Renders the editor to PNG files so the interface can be inspected without a host. */
int renderUiSnapshots (const juce::File& directory)
{
    directory.createDirectory();
    NeddPEAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    const auto file = directory.getChildFile ("editor.png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::cout << "wrote " << file.getFullPathName() << "\n";
    return 0;
}

} // namespace nedd::test
