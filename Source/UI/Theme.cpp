#include "Theme.h"

namespace nedd::ui
{
namespace
{
    struct FontNames
    {
        juce::String ui, display, mono;

        FontNames()
        {
            const auto available = juce::Font::findAllTypefaceNames();
            auto pick = [&available] (std::initializer_list<const char*> candidates, const juce::String& fallback)
            {
                for (auto* name : candidates)
                    if (available.contains (name))
                        return juce::String (name);
                return fallback;
            };

            ui = pick ({ "Segoe UI", "Helvetica Neue", "Inter", "Arial" }, juce::Font::getDefaultSansSerifFontName());
            display = pick ({ "Bahnschrift", "DIN Alternate", "Avenir Next Condensed", "Segoe UI" }, ui);
            mono = pick ({ "Cascadia Mono", "Consolas", "Menlo", "Courier New" }, juce::Font::getDefaultMonospacedFontName());
        }

        static const FontNames& get()
        {
            static const FontNames names;
            return names;
        }
    };
} // namespace

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (FontNames::get().ui, height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font displayFont (float height)
{
    return juce::Font (juce::FontOptions (FontNames::get().display, height, juce::Font::plain));
}

juce::Font monoFont (float height)
{
    return juce::Font (juce::FontOptions (FontNames::get().mono, height, juce::Font::plain));
}

juce::Colour noteColour (uint32_t noteId, int noteNumber)
{
    // Golden-angle hue spacing keeps simultaneous notes well separated.
    const float hue = std::fmod (0.52f + 0.618034f * (float) (noteId % 97u) + (float) noteNumber * 0.013f, 1.0f);
    return juce::Colour::fromHSV (hue, 0.55f, 1.0f, 1.0f);
}

juce::String noteName (int noteNumber)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    noteNumber = juce::jlimit (0, 127, noteNumber);
    return juce::String (names[noteNumber % 12]) + juce::String (noteNumber / 12 - 1);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour titleColour)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, metrics::corner);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), metrics::corner, 1.0f);

    if (title.isNotEmpty())
    {
        auto strip = bounds.removeFromTop ((float) panelTitleHeight).reduced (10.0f, 0.0f);
        g.setColour (titleColour);
        g.setFont (displayFont (12.5f));
        g.drawText (title.toUpperCase(), strip, juce::Justification::centredLeft);
    }
}

// ---------------------------------------------------------------------------------------------
NeddLookAndFeel::NeddLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::ComboBox::backgroundColourId, colours::control);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ComboBox::arrowColourId, colours::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colours::raised);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::control);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::control);
    setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.2f));
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, colours::accent);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::TextEditor::backgroundColourId, colours::control);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, colours::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, colours::raised);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::ListBox::backgroundColourId, colours::panel);
    setColour (juce::ScrollBar::thumbColourId, colours::outlineStrong);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::AlertWindow::backgroundColourId, colours::raised);
    setColour (juce::AlertWindow::textColourId, colours::text);

    setDefaultSansSerifTypefaceName (font (14.0f).getTypefaceName());
}

void NeddLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const bool hover = box.isMouseOver (true);

    g.setColour (colours::control.brighter (hover ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent.withAlpha (0.6f) : colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    const float arrowX = (float) width - 14.0f;
    const float cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowX - 4.0f, cy - 2.0f, arrowX + 4.0f, cy - 2.0f, arrowX, cy + 3.0f);
    g.setColour (box.isEnabled() ? colours::textDim : colours::textFaint);
    g.fillPath (arrow);
}

juce::Font NeddLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (std::min (14.0f, (float) box.getHeight() * 0.55f));
}

void NeddLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 24, box.getHeight() - 2);
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
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (colours::control);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
    }

    juce::Colour c = textColour != nullptr ? *textColour : colours::text;
    if (! isActive) c = colours::textFaint;
    else if (isHighlighted) c = colours::accent;

    if (isTicked)
    {
        g.setColour (colours::accent);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 10.0f, (float) r.getCentreY() }));
    }

    g.setColour (c);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (22).withTrimmedRight (hasSubMenu ? 20 : 8), juce::Justification::centredLeft);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (colours::textFaint);
        g.drawText (shortcutKeyText, r.withTrimmedRight (8), juce::Justification::centredRight);
    }

    if (hasSubMenu)
    {
        const float x = (float) r.getRight() - 12.0f, y = (float) r.getCentreY();
        juce::Path p;
        p.addTriangle (x, y - 4.0f, x, y + 4.0f, x + 5.0f, y);
        g.fillPath (p);
    }
}

void NeddLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setColour (colours::textFaint);
    g.setFont (displayFont (11.5f));
    g.drawText (sectionName.toUpperCase(), area.reduced (10, 0), juce::Justification::bottomLeft);
}

juce::Font NeddLookAndFeel::getPopupMenuFont() { return font (14.0f); }

void NeddLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    auto fill = on ? colours::accent.withAlpha (0.16f) : colours::control;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.brighter (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (on ? colours::accent.withAlpha (0.7f) : colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void NeddLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (! button.isEnabled() ? colours::textFaint : (button.getToggleState() ? colours::accent : colours::text));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred);
}

juce::Font NeddLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return displayFont (std::min (14.0f, (float) buttonHeight * 0.55f));
}

void NeddLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool)
{
    // Pill switch + label.
    const auto bounds = button.getLocalBounds().toFloat();
    const auto pill = bounds.withWidth (26.0f).withSizeKeepingCentre (26.0f, 14.0f).withX (bounds.getX() + 1.0f);
    const bool on = button.getToggleState();

    g.setColour (on ? colours::accent.withAlpha (0.85f) : colours::control.brighter (over ? 0.1f : 0.0f));
    g.fillRoundedRectangle (pill, 7.0f);
    g.setColour (on ? colours::accent : colours::outlineStrong);
    g.drawRoundedRectangle (pill, 7.0f, 1.0f);

    const float knobX = on ? pill.getRight() - 12.0f : pill.getX() + 2.0f;
    g.setColour (on ? colours::background : colours::textDim);
    g.fillEllipse (knobX, pill.getY() + 2.0f, 10.0f, 10.0f);

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.isEnabled() ? (on ? colours::text : colours::textDim) : colours::textFaint);
        g.setFont (font (13.0f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (34.0f), juce::Justification::centredLeft);
    }
}

void NeddLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (colours::raised);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (colours::accent.withAlpha (0.45f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text, font (13.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayout (s, (float) width - 16.0f);
    layout.draw (g, bounds.reduced (8.0f, 6.0f));
}

juce::Rectangle<int> NeddLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (tipText, font (13.0f), colours::text);
    juce::TextLayout layout;
    layout.createLayout (s, 300.0f);

    const int w = (int) std::ceil (layout.getWidth()) + 18;
    const int h = (int) std::ceil (layout.getHeight()) + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 12, w, h)
        .constrainedWithin (parentArea);
}

void NeddLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                     int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    g.setColour (colours::outlineStrong.brighter (down ? 0.3f : (over ? 0.15f : 0.0f)));
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

void NeddLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto track = horizontal ? bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f) : bounds.withSizeKeepingCentre (4.0f, bounds.getHeight());

    g.setColour (colours::control);
    g.fillRoundedRectangle (track, 2.0f);

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zero = horizontal ? (float) slider.getPositionOfValue (bipolar ? 0.0 : slider.getMinimum())
                                  : (float) slider.getPositionOfValue (bipolar ? 0.0 : slider.getMinimum());

    g.setColour (slider.findColour (juce::Slider::trackColourId, true).getAlpha() > 0 ? slider.findColour (juce::Slider::trackColourId) : colours::accent);
    if (horizontal)
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (zero, sliderPos), track.getY(), std::abs (sliderPos - zero), track.getHeight()), 2.0f);
    else
        g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), std::min (zero, sliderPos), track.getWidth(), std::abs (sliderPos - zero)), 2.0f);

    g.setColour (colours::text);
    const auto thumb = horizontal ? juce::Point<float> (sliderPos, bounds.getCentreY()) : juce::Point<float> (bounds.getCentreX(), sliderPos);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (thumb));
}

void NeddLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (colours::control);
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 4.0f);
}

void NeddLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? colours::accent.withAlpha (0.7f) : colours::outline);
    g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f), 4.0f, 1.0f);
}

} // namespace nedd::ui
