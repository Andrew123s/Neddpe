#include "EditorShell.h"

namespace nedd::ui
{
namespace
{
    /** The NeddPE mark: three expression strokes in the pitch / pressure / slide colours. */
    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto mark = area.removeFromLeft (area.getHeight()).reduced (4.0f);
        const juce::Colour strokeColours[] = { colours::pitch, colours::pressure, colours::slide };
        for (int i = 0; i < 3; ++i)
        {
            juce::Path p;
            const float y = mark.getY() + mark.getHeight() * (0.28f + 0.22f * (float) i);
            p.startNewSubPath (mark.getX(), y);
            p.cubicTo (mark.getX() + mark.getWidth() * 0.35f, y - mark.getHeight() * (0.28f - 0.08f * (float) i),
                       mark.getX() + mark.getWidth() * 0.65f, y + mark.getHeight() * 0.18f,
                       mark.getRight(), y - mark.getHeight() * 0.1f);
            g.setColour (strokeColours[i]);
            g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        area.removeFromLeft (8.0f);
        g.setFont (displayFont (25.0f));
        g.setColour (colours::text);
        const auto nedd = juce::String ("NEDD");
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), nedd);
        g.drawText (nedd, area, juce::Justification::centredLeft);
        g.setColour (colours::accent);
        g.drawText ("PE", area.withTrimmedLeft (w + 1.0f), juce::Justification::centredLeft);
    }
} // namespace

// =============================================================================================
HeaderBar::HeaderBar (EditorContext& c) : ctx (c), meter (c)
{
    auto& um = ctx.processor.getUndoManager();
    undo.onClick = [&um] { um.undo(); };
    redo.onClick = [&um] { um.redo(); };
    undo.setTooltip ("Undo (parameter, preset and modulation changes)");
    redo.setTooltip ("Redo");
    addAndMakeVisible (undo);
    addAndMakeVisible (redo);
    addAndMakeVisible (meter);
    ctx.addListener (this);
}

HeaderBar::~HeaderBar() { ctx.removeListener (this); }

void HeaderBar::setTools (juce::Component* presets, juce::Component* morph)
{
    presetTools = presets;
    morphTools = morph;
    if (presetTools != nullptr) addAndMakeVisible (presetTools);
    if (morphTools != nullptr) addAndMakeVisible (morphTools);
    resized();
}

void HeaderBar::editorTick()
{
    const auto name = ctx.processor.getCurrentPresetName();
    const float load = ctx.telemetry.cpuLoad.load (std::memory_order_relaxed);
    auto& um = ctx.processor.getUndoManager();
    undo.setEnabled (um.canUndo());
    redo.setEnabled (um.canRedo());

    if (name != patchName || std::abs (load - cpu) > 0.005f)
    {
        patchName = name;
        cpu = load;
        repaint();
    }
}

void HeaderBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::panel.brighter (0.03f), 0.0f, 0.0f, colours::background, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (colours::outline);
    g.drawHorizontalLine ((int) bounds.getBottom() - 1, 0.0f, bounds.getRight());

    drawLogo (g, bounds.withWidth (190.0f).reduced (14.0f, 14.0f));

    if (presetTools == nullptr)
    {
        g.setColour (colours::text);
        g.setFont (font (15.0f, true));
        g.drawText (patchName, juce::Rectangle<float> (230.0f, 0.0f, 360.0f, bounds.getHeight()), juce::Justification::centredLeft);
    }

    g.setColour (cpu > 0.7f ? colours::danger : colours::textFaint);
    g.setFont (monoFont (11.0f));
    g.drawText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0f)) + "%",
                juce::Rectangle<float> (bounds.getRight() - 140.0f, 0.0f, 90.0f, bounds.getHeight()), juce::Justification::centredRight);
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (10, 13);
    r.removeFromLeft (200);
    meter.setBounds (r.removeFromRight (22).expanded (0, 4));
    r.removeFromRight (100);

    redo.setBounds (r.removeFromRight (54));
    r.removeFromRight (4);
    undo.setBounds (r.removeFromRight (54));
    r.removeFromRight (12);

    if (morphTools != nullptr)
    {
        morphTools->setBounds (r.removeFromRight (250));
        r.removeFromRight (12);
    }
    if (presetTools != nullptr)
        presetTools->setBounds (r);
}

// =============================================================================================
int NavRail::itemAt (juce::Point<int> p) const
{
    const int index = (p.y - 10) / itemHeight;
    return index >= 0 && index < (int) items.size() ? index : -1;
}

void NavRail::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto r = juce::Rectangle<int> (0, 10 + (int) i * itemHeight, getWidth() - 1, itemHeight);
        const bool selected = items[i].id == current;
        const bool over = (int) i == hover;

        if (selected || over)
        {
            g.setColour (selected ? colours::raised : colours::panel);
            g.fillRect (r.reduced (6, 2).toFloat());
        }
        if (selected)
        {
            g.setColour (colours::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, (float) r.getY() + 9.0f, 3.0f, (float) itemHeight - 18.0f), 1.5f);
        }

        g.setColour (selected ? colours::text : (over ? colours::text.withAlpha (0.8f) : colours::textDim));
        g.setFont (displayFont (14.0f));
        g.drawText (items[i].name, r.withTrimmedLeft (20), juce::Justification::centredLeft);
    }
}

void NavRail::mouseDown (const juce::MouseEvent& e)
{
    const int index = itemAt (e.getPosition());
    if (index >= 0 && onSelect)
        onSelect (items[(size_t) index].id);
}

void NavRail::mouseMove (const juce::MouseEvent& e)
{
    const int index = itemAt (e.getPosition());
    if (index != hover)
    {
        hover = index;
        repaint();
    }
}

// =============================================================================================
PerformanceStrip::PerformanceStrip (EditorContext& c) : ctx (c), keyboard (c)
{
    for (int m = 0; m < kNumMacros; ++m)
    {
        macros[(size_t) m] = std::make_unique<ParamKnob> (c, pid::macro (m), " ", colours::amber);
        addAndMakeVisible (*macros[(size_t) m]);

        auto& label = names[(size_t) m];
        label.setText (ctx.processor.getMacroName (m), juce::dontSendNotification);
        label.setEditable (false, true, false);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (displayFont (12.5f));
        label.setColour (juce::Label::textColourId, colours::amber);
        label.setTooltip ("Double-click to rename. Assign macros in the matrix; automate them from your DAW.");
        label.onTextChange = [this, m] { ctx.processor.setMacroName (m, names[(size_t) m].getText()); names[(size_t) m].setText (ctx.processor.getMacroName (m), juce::dontSendNotification); };
        addAndMakeVisible (label);
    }

    octaveDown.onClick = [this] { keyboard.setLowestNote (juce::jmax (12, keyboard.getLowestNote() - 12)); };
    octaveUp.onClick = [this] { keyboard.setLowestNote (juce::jmin (84, keyboard.getLowestNote() + 12)); };
    for (auto* comp : std::initializer_list<juce::Component*> { &keyboard, &octaveDown, &octaveUp })
        addAndMakeVisible (comp);
    ctx.addListener (this);
}

PerformanceStrip::~PerformanceStrip() { ctx.removeListener (this); }

void PerformanceStrip::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    g.setColour (colours::textFaint);
    g.setFont (displayFont (11.0f));
    g.drawText ("MACROS", juce::Rectangle<int> (14, 6, 80, 14), juce::Justification::centredLeft);
}

void PerformanceStrip::editorTick()
{
    // Keep macro names in sync with preset loads and host state restores.
    for (int m = 0; m < kNumMacros; ++m)
        if (! names[(size_t) m].isBeingEdited() && names[(size_t) m].getText() != ctx.processor.getMacroName (m))
            names[(size_t) m].setText (ctx.processor.getMacroName (m), juce::dontSendNotification);
}

void PerformanceStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    auto macroArea = r.removeFromLeft (440);
    macroArea.removeFromTop (12);
    const int w = macroArea.getWidth() / kNumMacros;
    for (int m = 0; m < kNumMacros; ++m)
    {
        auto col = macroArea.removeFromLeft (w).reduced (6, 0);
        names[(size_t) m].setBounds (col.removeFromBottom (20));
        macros[(size_t) m]->setBounds (col.withSizeKeepingCentre (70, col.getHeight()));
    }

    r.removeFromLeft (10);
    auto buttons = r.removeFromLeft (22);
    octaveUp.setBounds (buttons.removeFromTop (buttons.getHeight() / 2).reduced (0, 2));
    octaveDown.setBounds (buttons.reduced (0, 2));
    r.removeFromLeft (6);
    keyboard.setBounds (r);
}

} // namespace nedd::ui
