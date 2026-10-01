#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "MPE/NoteEvents.h"

namespace nedd::ui
{
/** NeddPE colour system: porcelain-white cards on a blush ground, one rose signal colour, soft plum ink
    (never black), and one pastel colour per MPE dimension. */
namespace colours
{
    const juce::Colour background   { 0xfffcf1f6 };   // blush ground behind cards, display wells
    const juce::Colour backgroundDeep { 0xfff8e3ed }; // lower end of the window gradient
    const juce::Colour panel        { 0xffffffff };   // cards
    const juce::Colour raised       { 0xffffffff };   // popups, tooltips
    const juce::Colour control      { 0xfffdf0f5 };   // buttons, boxes, knob tracks
    const juce::Colour controlHover { 0xfffbe2ec };
    const juce::Colour outline      { 0xfff4d9e5 };
    const juce::Colour outlineStrong{ 0xffeab9cf };

    const juce::Colour text         { 0xff4b2840 };   // deep plum ink
    const juce::Colour textDim      { 0xff9b7189 };
    const juce::Colour textFaint    { 0xffcfa9bc };

    const juce::Colour accent       { 0xffef4f91 };   // NeddPE rose
    const juce::Colour accentLight  { 0xffff9cc4 };   // start of the rose gradient
    const juce::Colour accentSoft   { 0xffffe1ee };   // selected / active surfaces
    const juce::Colour amber        { 0xffff9a76 };   // peach (macros, clip end, MIDI learn)
    const juce::Colour modulation   { 0xffa77cf2 };   // lilac: anything that is being modulated
    const juce::Colour danger       { 0xffe5406b };   // raspberry

    // MPE dimensions (used consistently everywhere)
    const juce::Colour pitch        { 0xff8b7cf6 };   // periwinkle
    const juce::Colour pressure     { 0xffef4f91 };   // rose
    const juce::Colour slide        { 0xffff9a76 };   // peach
    const juce::Colour velocity     { 0xfff2b25c };   // honey
    const juce::Colour releaseVel   { 0xffc08cf0 };   // orchid

    // Keyboard keys: no black anywhere, the "black" keys are mauve.
    const juce::Colour keyWhite     { 0xffffffff };
    const juce::Colour keyBlack     { 0xffc9759c };
} // namespace colours

namespace metrics
{
    constexpr int designWidth = 1280;
    constexpr int designHeight = 820;
    constexpr int headerHeight = 60;
    constexpr int navWidth = 132;
    constexpr int bottomHeight = 112;
    constexpr float corner = 12.0f;
    constexpr int gap = 8;
} // namespace metrics

juce::Font font (float height, bool bold = false);
juce::Font displayFont (float height);   // condensed display face for headings and values
juce::Font monoFont (float height);

/** Stable identity colour for a note, so a note keeps its colour across every view. */
juce::Colour noteColour (uint32_t noteId, int noteNumber);

juce::String noteName (int noteNumber);

/** Draws a panel: soft rose shadow, white rounded card, hairline outline, optional title in the top-left. */
void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title = {},
                juce::Colour titleColour = colours::textDim);

/** The rose gradient used for active surfaces, value arcs and highlights (light at the top-left). */
juce::ColourGradient roseGradient (juce::Rectangle<float> area, float alpha = 1.0f);

/** Soft diffuse shadow under a rounded rectangle (tinted rose, never grey). */
void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius, float strength = 1.0f);

/** Window background: a gentle blush gradient. */
void fillWindowBackground (juce::Graphics& g, juce::Rectangle<float> bounds);

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
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

} // namespace nedd::ui
