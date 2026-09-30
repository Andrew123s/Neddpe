#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor/PluginProcessor.h"
#include "UI/EditorShell.h"

namespace nedd
{
/**
    The NeddPE window. Everything is laid out at a fixed design size (1280 x 820) inside a
    root component that is scaled as a whole, so the interface resizes cleanly between 75% and
    150% without per-widget layout changes.
*/
class NeddPEEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NeddPEEditor (NeddPEAudioProcessor& p);
    ~NeddPEEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void showPage (int pageId);
    std::vector<int> getPageIds() const;
    void setUiScale (float scale);

    /** Advances the frame-rate UI updates without the timer (used by the snapshot renderer). */
    void tickForTesting (int frames);

private:
    void timerCallback() override;
    std::unique_ptr<ui::Page> createPage (int pageId);

    NeddPEAudioProcessor& processor;
    ui::NeddLookAndFeel lookAndFeel;
    ui::EditorContext ctx;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Component root;
    ui::HeaderBar header { ctx };
    ui::NavRail nav;
    ui::PerformanceStrip strip { ctx };
    std::unique_ptr<juce::Component> presetTools, morphTools;
    std::map<int, std::unique_ptr<ui::Page>> pages;
    std::vector<std::pair<int, juce::String>> pageList;
    int currentPage = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeddPEEditor)
};

} // namespace nedd
