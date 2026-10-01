#include "Theme.h"
#include "NeddPEBinaryData.h"

namespace nedd::ui
{
namespace
{
    /** The embedded Nunito faces (rounded, soft), with a system fallback if loading ever fails. */
    struct Typefaces
    {
        juce::Typeface::Ptr regular, semiBold, extraBold;
        juce::String mono;

        Typefaces()
        {
            regular = juce::Typeface::createSystemTypefaceFor (NeddPEBinary::NunitoRegular_ttf, (size_t) NeddPEBinary::NunitoRegular_ttfSize);
            semiBold = juce::Typeface::createSystemTypefaceFor (NeddPEBinary::NunitoSemiBold_ttf, (size_t) NeddPEBinary::NunitoSemiBold_ttfSize);
            extraBold = juce::Typeface::createSystemTypefaceFor (NeddPEBinary::NunitoExtraBold_ttf, (size_t) NeddPEBinary::NunitoExtraBold_ttfSize);

            const auto available = juce::Font::findAllTypefaceNames();
            mono = juce::Font::getDefaultMonospacedFontName();
            for (auto* name : { "Cascadia Mono", "Consolas", "Menlo" })
                if (available.contains (name))
                {
                    mono = name;
                    break;
                }
        }

        static const Typefaces& get()
        {
            static const Typefaces faces;
            return faces;
        }
    };

    juce::Font fontFrom (const juce::Typeface::Ptr& face, float height)
    {
        if (face != nullptr)
            return juce::Font (juce::FontOptions (face).withHeight (height));
        return juce::Font (juce::FontOptions (height));
    }

    constexpr float kControlRadius = 9.0f;
} // namespace

juce::Font font (float height, bool bold)
{
    const auto& f = Typefaces::get();
    return fontFrom (bold ? f.extraBold : f.semiBold, height);
}

juce::Font displayFont (float height)
{
    return fontFrom (Typefaces::get().extraBold, height);
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (Typefaces::get().mono, height, juce::Font::plain));
}

juce::Colour noteColour (uint32_t noteId, int noteNumber)
{
    // Golden-angle steps through a pastel arc from orchid to peach (no greens), so simultaneous
    // notes stay distinguishable while matching the palette.
    const float t = std::fmod (0.618034f * (float) (noteId % 97u) + (float) noteNumber * 0.071f, 1.0f);
    const float hue = std::fmod (0.70f + t * 0.42f, 1.0f);   // 0.70 (violet) .. 0.12 (peach), through pink
    return juce::Colour::fromHSV (hue, 0.55f, 0.97f, 1.0f);
}

juce::String noteName (int noteNumber)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    noteNumber = juce::jlimit (0, 127, noteNumber);
    return juce::String (names[noteNumber % 12]) + juce::String (noteNumber / 12 - 1);
}

juce::ColourGradient roseGradient (juce::Rectangle<float> area, float alpha)
{
    return juce::ColourGradient (colours::accentLight.withMultipliedAlpha (alpha), area.getTopLeft(),
                                 colours::accent.withMultipliedAlpha (alpha), area.getBottomRight(), false);
}

void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius, float strength)
{
    // Three widening, fading rounded outlines approximate a diffuse blur without an image pass.
    const juce::Colour tint (0xffd76a98);
    for (int i = 3; i >= 1; --i)
    {
        const float spread = (float) i * 2.5f;
        g.setColour (tint.withAlpha (0.035f * strength * (float) (4 - i)));
        g.fillRoundedRectangle (bounds.expanded (spread * 0.6f).translated (0.0f, spread * 0.55f), cornerRadius + spread * 0.6f);
    }
}

void fillWindowBackground (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::ColourGradient gradient (colours::background, bounds.getTopLeft(), colours::backgroundDeep, bounds.getBottomRight(), false);
    gradient.addColour (0.55, colours::background.interpolatedWith (colours::backgroundDeep, 0.35f));
    g.setGradientFill (gradient);
    g.fillRect (bounds);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour titleColour)
{
    drawSoftShadow (g, bounds, metrics::corner);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::corner, 1.0f);

    if (title.isNotEmpty())
    {
        auto strip = bounds.removeFromTop ((float) panelTitleHeight).reduced (12.0f, 0.0f).translated (0.0f, 1.0f);
        // A small rose dot leads every card title.
        g.setGradientFill (roseGradient (strip.withWidth (7.0f).withSizeKeepingCentre (7.0f, 7.0f)));
        g.fillEllipse (strip.getX(), strip.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (titleColour == colours::textDim ? colours::text : titleColour);
        g.setFont (displayFont (12.5f));
        g.drawText (title.toUpperCase(), strip.withTrimmedLeft (13.0f), juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------------------------
NeddLookAndFeel::NeddLookAndFeel()
{
    // Base scheme for every JUCE widget we do not draw ourselves (device settings, file choosers...).
    setColourScheme ({ colours::background, colours::panel, colours::panel, colours::outlineStrong, colours::text,
                       colours::accent, colours::accent, colours::accentSoft, colours::text });

    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::textColourId, colours::text);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::arrowColourId, colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, colours::raised);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentSoft);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::accent);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::panel);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.22f));
    setColour (juce::TextEditor::highlightedTextColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, colours::raised);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::ListBox::backgroundColourId, colours::panel);
    setColour (juce::ListBox::textColourId, colours::text);
    setColour (juce::ScrollBar::thumbColourId, colours::outlineStrong);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, colours::panel);
    setColour (juce::Slider::textBoxOutlineColourId, colours::outline);
    setColour (juce::Slider::thumbColourId, colours::accent);
    setColour (juce::Slider::trackColourId, colours::accent);
    setColour (juce::Slider::backgroundColourId, colours::control);
    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::control);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::AlertWindow::backgroundColourId, colours::raised);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::outline);
    setColour (juce::GroupComponent::textColourId, colours::text);
    setColour (juce::GroupComponent::outlineColourId, colours::outline);
    setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, colours::panel);
    setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId, colours::panel);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, colours::accentSoft);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, colours::text);

    if (auto face = Typefaces::get().semiBold)
        setDefaultSansSerifTypeface (face);
}

void NeddLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const bool hover = box.isMouseOver (true);
    const float radius = std::min (kControlRadius, bounds.getHeight() * 0.5f);

    g.setColour (hover ? colours::control : colours::panel);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (box.hasKeyboardFocus (true) || hover ? colours::outlineStrong : colours::outline);
    g.drawRoundedRectangle (bounds, radius, 1.0f);

    // Chevron
    const float cx = (float) width - 13.0f;
    const float cy = (float) height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
    chevron.lineTo (cx, cy + 2.0f);
    chevron.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (box.isEnabled() ? colours::accent : colours::textFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NeddLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (std::min (13.5f, (float) box.getHeight() * 0.56f));
}

void NeddLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void NeddLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (colours::raised);
    g.setColour (colours::outlineStrong);
    g.drawRect (0, 0, width, height, 1);
}

void NeddLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                         const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (colours::outline);
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (colours::accentSoft);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }

    juce::Colour c = textColour != nullptr ? *textColour : colours::text;
    if (! isActive) c = colours::textFaint;
    else if (isHighlighted) c = colours::accent;

    if (isTicked)
    {
        const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) r.getX() + 11.0f, (float) r.getCentreY() });
        g.setGradientFill (roseGradient (dot));
        g.fillEllipse (dot);
    }

    g.setColour (c);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (24).withTrimmedRight (hasSubMenu ? 20 : 8), juce::Justification::centredLeft);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (colours::textFaint);
        g.drawText (shortcutKeyText, r.withTrimmedRight (8), juce::Justification::centredRight);
    }

    if (hasSubMenu)
    {
        const float x = (float) r.getRight() - 12.0f, y = (float) r.getCentreY();
        juce::Path p;
        p.startNewSubPath (x, y - 4.0f);
        p.lineTo (x + 4.0f, y);
        p.lineTo (x, y + 4.0f);
        g.setColour (colours::accent);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void NeddLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (colours::accent);
    g.setFont (displayFont (11.0f));
    g.drawText (sectionName.toUpperCase(), area.reduced (12, 0), juce::Justification::bottomLeft);
}

juce::Font NeddLookAndFeel::getPopupMenuFont() { return font (13.5f); }

void NeddLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const float radius = std::min (kControlRadius, bounds.getHeight() * 0.5f);
    const bool on = button.getToggleState();

    if (on)
    {
        drawSoftShadow (g, bounds, radius, 0.7f);
        g.setGradientFill (roseGradient (bounds, down ? 0.85f : 1.0f));
        g.fillRoundedRectangle (bounds, radius);
        if (over)
        {
            g.setColour (colours::panel.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds, radius);
        }
        return;
    }

    g.setColour (down ? colours::accentSoft : (over ? colours::control : colours::panel));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (over || down ? colours::outlineStrong : colours::outline);
    g.drawRoundedRectangle (bounds, radius, 1.0f);
}

void NeddLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    juce::Colour c = button.getToggleState() ? colours::panel : button.findColour (juce::TextButton::textColourOffId);
    if (! button.isEnabled())
        c = colours::textFaint;
    g.setColour (c);
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred);
}

juce::Font NeddLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return displayFont (std::min (13.0f, (float) buttonHeight * 0.5f));
}

void NeddLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool)
{
    // Pill switch + label.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto pill = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - 8.0f, 30.0f, 16.0f);
    const bool on = button.getToggleState();

    if (on)
    {
        g.setGradientFill (roseGradient (pill));
        g.fillRoundedRectangle (pill, 8.0f);
    }
    else
    {
        g.setColour (over ? colours::controlHover : colours::control);
        g.fillRoundedRectangle (pill, 8.0f);
        g.setColour (colours::outlineStrong);
        g.drawRoundedRectangle (pill.reduced (0.5f), 8.0f, 1.0f);
    }

    const auto knob = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ on ? pill.getRight() - 8.0f : pill.getX() + 8.0f, pill.getCentreY() });
    g.setColour (juce::Colour (0xffd76a98).withAlpha (0.25f));
    g.fillEllipse (knob.translated (0.0f, 1.0f));
    g.setColour (colours::panel);
    g.fillEllipse (knob);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.isEnabled() ? (on ? colours::text : colours::textDim) : colours::textFaint);
        g.setFont (font (13.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (38.0f), juce::Justification::centredLeft);
    }
}

void NeddLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (colours::raised);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (colours::outlineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text, font (13.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayout (s, (float) width - 18.0f);
    layout.draw (g, bounds.reduced (9.0f, 7.0f));
}

juce::Rectangle<int> NeddLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (tipText, font (13.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayout (s, 300.0f);

    const int w = (int) std::ceil (layout.getWidth()) + 20;
    const int h = (int) std::ceil (layout.getHeight()) + 16;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 12, w, h)
        .constrainedWithin (parentArea);
}

void NeddLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                     int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (down ? colours::accent.withAlpha (0.6f) : (over ? colours::accentLight : colours::outlineStrong));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
}

void NeddLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto track = horizontal ? bounds.withSizeKeepingCentre (bounds.getWidth(), 5.0f) : bounds.withSizeKeepingCentre (5.0f, bounds.getHeight());

    g.setColour (colours::control);
    g.fillRoundedRectangle (track, 2.5f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (track, 2.5f, 1.0f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zero = (float) slider.getPositionOfValue (bipolar ? 0.0 : slider.getMinimum());

    const auto custom = slider.findColour (juce::Slider::trackColourId);
    const auto filled = horizontal ? juce::Rectangle<float> (std::min (zero, sliderPos), track.getY(), std::abs (sliderPos - zero), track.getHeight())
                                   : juce::Rectangle<float> (track.getX(), std::min (zero, sliderPos), track.getWidth(), std::abs (sliderPos - zero));
    if (custom != colours::accent)
        g.setColour (custom);
    else
        g.setGradientFill (roseGradient (filled));
    g.fillRoundedRectangle (filled, 2.5f);

    const auto thumbCentre = horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY()) : juce::Point<float> (bounds.getCentreX(), sliderPos);
    const auto thumb = juce::Rectangle<float> (13.0f, 13.0f).withCentre (thumbCentre);
    g.setColour (juce::Colour (0xffd76a98).withAlpha (0.25f));
    g.fillEllipse (thumb.translated (0.0f, 1.0f).expanded (1.0f));
    g.setColour (colours::panel);
    g.fillEllipse (thumb);
    g.setColour (custom != colours::accent ? custom : colours::accent);
    g.drawEllipse (thumb.reduced (1.0f), 2.0f);
}

void NeddLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    const juce::PathStrokeType stroke (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colours::control);
    g.strokePath (track, stroke);
    g.setGradientFill (roseGradient (bounds));
    g.strokePath (value, stroke);

    const auto body = juce::Rectangle<float> (radius * 1.2f, radius * 1.2f).withCentre (centre);
    drawSoftShadow (g, body, body.getWidth() * 0.5f, 0.8f);
    g.setColour (colours::panel);
    g.fillEllipse (body);
    g.setColour (colours::accent);
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (centre.getPointOnCircumference (radius * 0.42f, angle)));
}

void NeddLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 7.0f);
}

void NeddLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? colours::accent : colours::outline);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f), 7.0f, 1.0f);
}

void NeddLookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX,
                                                  int titleSpaceW, const juce::Image*, bool)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setGradientFill (juce::ColourGradient (colours::panel, 0.0f, 0.0f, colours::background, 0.0f, (float) h, false));
    g.fillRect (bounds);
    g.setColour (colours::outline);
    g.drawHorizontalLine (h - 1, 0.0f, (float) w);

    g.setColour (window.isActiveWindow() ? colours::text : colours::textDim);
    g.setFont (displayFont ((float) h * 0.5f));
    g.drawText (window.getName(), titleSpaceX + 4, 0, titleSpaceW, h, juce::Justification::centredLeft, true);
}

void NeddLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto bounds = alert.getLocalBounds().toFloat();
    g.setColour (colours::raised);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (colours::outlineStrong);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);
    layout.draw (g, textArea.toFloat());
}

void NeddLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));
    if (label.isBeingEdited())
        return;

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (label));
    g.drawFittedText (label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()), label.getJustificationType(),
                      juce::jmax (1, (int) ((float) label.getHeight() / label.getFont().getHeight())), label.getMinimumHorizontalScale());
}

} // namespace nedd::ui
