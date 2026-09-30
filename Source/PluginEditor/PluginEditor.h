#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor/PluginProcessor.h"

namespace nedd
{
class NeddPEEditor : public juce::AudioProcessorEditor
{
public:
    explicit NeddPEEditor (NeddPEAudioProcessor& p);
    ~NeddPEEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    NeddPEAudioProcessor& processor;
    juce::GenericAudioProcessorEditor generic { processor };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeddPEEditor)
};

} // namespace nedd
