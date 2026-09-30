#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "MPE/NoteEvents.h"

namespace nedd::ui
{
/** NeddPE colour system: charcoal surfaces, one mint signal colour, and one colour per MPE dimension. */
namespace colours
{
    const juce::Colour background   { 0xff0a0b0e };
    const juce::Colour panel        { 0xff111318 };
    const juce::Colour raised       { 0xff181b22 };
    const juce::Colour control      { 0xff222631 };
    const juce::Colour outline      { 0xff262b36 };
    const juce::Colour outlineStrong{ 0xff363c4a };

    const juce::Colour text         { 0xffe8eaf0 };
    const juce::Colour textDim      { 0xff979eaf };
    const juce::Colour textFaint    { 0xff5a6172 };

    const juce::Colour accent       { 0xff36e2b4 };   // NeddPE signal mint
    const juce::Colour amber        { 0xffffb547 };
    const juce::Colour modulation   { 0xffb18cff };   // anything that is being modulated
    const juce::Colour danger       { 0xffff5d73 };

    // MPE dimensions (used consistently everywhere)
    const juce::Colour pitch        { 0xff6fa8ff };
    const juce::Colour pressure     { 0xffff6b7f };
    const juce::Colour slide        { 0xff36e2b4 };
    const juce::Colour velocity     { 0xffffc857 };
    const juce::Colour releaseVel   { 0xffb18cff };
} // namespace colours

namespace metrics
{
    constexpr int designWidth = 1280;
    constexpr int designHeight = 820;
    constexpr int headerHeight = 60;
    constexpr int navWidth = 132;
    constexpr int bottomHeight = 112;
    constexpr float corner = 6.0f;
    constexpr int gap = 8;
} // namespace metrics

juce::Font font (float height, bool bold = false);
juce::Font displayFont (float height);   // condensed display face for headings and values
juce::Font monoFont (float height);

/** Stable identity colour for a note, so a note keeps its colour across every view. */
juce::Colour noteColour (uint32_t noteId, int noteNumber);

juce::String noteName (int noteNumber);

/** Draws a panel: rounded surface, hairline outline, optional title in the top-left. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title = {},
                juce::Colour titleColour = colours::textDim);

/** Title strip height used by drawPanel. */
constexpr int panelTitleHeight = 24;

class NeddLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeddLookAndFeel();

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

} // namespace nedd::ui
