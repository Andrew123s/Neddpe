#pragma once

#include "Displays.h"
#include "ParamControls.h"

namespace nedd::ui
{
/** Base for panels that paint a titled card and own parameter controls. */
class Card : public juce::Component
{
public:
    Card (EditorContext& c, const juce::String& t) : ctx (c), title (t) {}
    void paint (juce::Graphics& g) override { drawPanel (g, getLocalBounds().toFloat(), title, titleColour); }

protected:
    juce::Rectangle<int> content() const { return getLocalBounds().withTrimmedTop (panelTitleHeight).reduced (8, 4); }

    template <typename T, typename... Args>
    T& add (std::vector<std::unique_ptr<juce::Component>>& owner, Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        owner.push_back (std::move (c));
        return ref;
    }

    EditorContext& ctx;
    juce::String title;
    juce::Colour titleColour = colours::textDim;
    std::vector<std::unique_ptr<juce::Component>> owned;
};

/**
    Full controls for one oscillator. Only the controls relevant to the selected engine are
    shown (progressive disclosure); switching engine re-lays the panel.
*/
class OscillatorPanel : public Card, private EditorContext::Listener
{
public:
    OscillatorPanel (EditorContext& ctx, int oscIndex, bool compact);
    ~OscillatorPanel() override;
    void resized() override;

private:
    void editorTick() override;
    ParamKnob& knob (OscField f, const juce::String& label);
    int p (OscField f) const { return pid::osc (oscIndex, f); }
    void chooseFile();
    void updateSourceCaption();

    const int oscIndex;
    const bool compact;
    OscEngine shownEngine = OscEngine::Analog;
    AnalogWave shownWave = AnalogWave::Saw;
    bool shownTableImported = false;
    int shownAssetsVersion = -1;

    ParamToggle onToggle;
    ParamChoice engineChoice, waveChoice, tableChoice, noiseChoice, algoChoice, routeChoice, fmSourceChoice, op1RatioChoice, op2RatioChoice;
    ParamToggle syncToggle, loopToggle;
    juce::TextButton importButton { "Import" }, clearButton { "Clear" };
    Caption sourceCaption;
    WaveDisplay display;
    std::map<OscField, ParamKnob*> knobs;
    Caption fmCaption { "CROSS-MOD" }, unisonCaption { "UNISON" }, operatorCaption { "OPERATORS" }, grainCaption { "GRAINS" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDirectory { juce::File::getSpecialLocation (juce::File::userMusicDirectory) };
};

class FilterPanel : public Card
{
public:
    FilterPanel (EditorContext& ctx, bool large);
    void resized() override;

private:
    const bool large;
    ParamToggle onToggle;
    ParamChoice typeChoice;
    FilterDisplay display;
    ParamKnob cutoff, resonance, drive, envAmount, keyTrack, velocity, mix;
};

class EnvelopePanel : public Card
{
public:
    EnvelopePanel (EditorContext& ctx, int envIndex, const juce::String& title, juce::Colour colour, bool showCurves);
    void resized() override;

private:
    const int envIndex;
    const bool showCurves;
    EnvelopeDisplay display;
    std::vector<std::unique_ptr<ParamKnob>> knobs;
};

} // namespace nedd::ui
