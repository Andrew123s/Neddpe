#pragma once

#include "EditorContext.h"

namespace nedd::ui
{
/** Popup menu of modulation sources grouped by kind. Item IDs are (int) ModSource + idOffset. */
juce::PopupMenu createModSourceMenu (int idOffset);
/** Popup menu of modulation destinations grouped by section. Item IDs are (int) ModDest + idOffset. */
juce::PopupMenu createModDestMenu (int idOffset);

/**
    Rotary control bound to a parameter.

    Draws the base value, the full range its modulation routes can reach (violet arc) and the
    live modulated position of the most recent note (violet dot), so it is always visible
    why a value is moving. Right-click: add/remove modulation, MIDI learn, reset.
*/
class ParamKnob : public juce::Slider, private EditorContext::Listener
{
public:
    ParamKnob (EditorContext& ctx, int paramIndex, const juce::String& label = {}, juce::Colour accent = colours::accent);
    ~ParamKnob() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    int getParamIndex() const noexcept { return paramIndex; }
    void setLabel (const juce::String& newLabel) { label = newLabel; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }

private:
    void editorTick() override;
    void showContextMenu();

    EditorContext& ctx;
    const int paramIndex;
    const ModDest dest;
    juce::String label;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    juce::Range<float> modRange;
    float liveMod = 0.0f;
    bool live = false;
    int midiCc = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamKnob)
};

/** Compact horizontal slider bound to a parameter (used in dense rows such as the matrix). */
class ParamSlider : public juce::Slider
{
public:
    ParamSlider (EditorContext& ctx, int paramIndex, juce::Colour colour = colours::accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Drop-down bound to a choice / int parameter. */
class ParamChoice : public juce::ComboBox
{
public:
    ParamChoice (EditorContext& ctx, int paramIndex);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Pill switch bound to a bool parameter. */
class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle (EditorContext& ctx, int paramIndex, const juce::String& text = {});

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Small caption label in the NeddPE style. */
class Caption : public juce::Component
{
public:
    explicit Caption (const juce::String& t = {}, juce::Justification j = juce::Justification::centredLeft)
        : text (t), justification (j) { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void setColour (juce::Colour c) { colour = c; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour);
        g.setFont (displayFont (12.0f));
        g.drawText (text, getLocalBounds(), justification, true);
    }

private:
    juce::String text;
    juce::Justification justification;
    juce::Colour colour = colours::textDim;
};

/** Lays out knobs in a row, evenly spaced, each at most maxWidth wide. */
void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> components, int maxWidth = 64);

} // namespace nedd::ui
