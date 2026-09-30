#include "PluginEditor.h"

namespace nedd
{
NeddPEEditor::NeddPEEditor (NeddPEAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    addAndMakeVisible (generic);
    setSize (900, 600);
}

NeddPEEditor::~NeddPEEditor() = default;

void NeddPEEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }
void NeddPEEditor::resized() { generic.setBounds (getLocalBounds()); }

} // namespace nedd
