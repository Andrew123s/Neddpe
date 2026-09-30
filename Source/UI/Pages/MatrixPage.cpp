#include "Pages.h"
#include "Modulation/ParamModMapping.h"

namespace nedd::ui
{
namespace
{
    void drawBipolarBar (juce::Graphics& g, juce::Rectangle<float> bar, float value, juce::Colour colour)
    {
        g.setColour (colours::control);
        g.fillRoundedRectangle (bar, 2.0f);
        const float v = juce::jlimit (-1.0f, 1.0f, value);
        const float mid = bar.getCentreX();
        const float w = std::abs (v) * bar.getWidth() * 0.5f;
        g.setColour (colour);
        g.fillRoundedRectangle (v >= 0.0f ? bar.withX (mid).withWidth (w) : bar.withX (mid - w).withWidth (w), 2.0f);
        g.setColour (colours::outlineStrong);
        g.drawVerticalLine ((int) mid, bar.getY(), bar.getBottom());
    }
} // namespace

// =============================================================================================
class MatrixRow : public juce::Component, private EditorContext::Listener
{
public:
    MatrixRow (EditorContext& c, int s, std::function<void (ModDest)> onSelect)
        : ctx (c), slot (s), select (std::move (onSelect)),
          source (c, pid::mod (s, ModSlotField::Source)),
          amount (c, pid::mod (s, ModSlotField::Amount), colours::modulation),
          dest (c, pid::mod (s, ModSlotField::Dest)),
          curve (c, pid::mod (s, ModSlotField::Curve)),
          polarity (c, pid::mod (s, ModSlotField::Polarity))
    {
        amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
        clear.setTooltip ("Clear this slot");
        clear.onClick = [this] { juce::MessageManager::callAsync ([this] { ctx.removeModulationSlot (slot); }); };
        for (auto* comp : std::initializer_list<juce::Component*> { &source, &amount, &dest, &curve, &polarity, &clear })
            addAndMakeVisible (comp);
        ctx.addListener (this);
    }

    ~MatrixRow() override { ctx.removeListener (this); }

    void mouseDown (const juce::MouseEvent&) override { select (currentDest()); }

    void paint (juce::Graphics& g) override
    {
        const bool active = isActive();
        g.setColour (active ? colours::raised : colours::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.0f, 1.0f), 4.0f);
        g.setColour (active ? colours::modulation : colours::textFaint);
        g.setFont (monoFont (12.0f));
        g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), getLocalBounds().removeFromLeft (30), juce::Justification::centred);
        drawBipolarBar (g, meterArea.toFloat().reduced (0.0f, 9.0f), live, colours::modulation);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        r.removeFromLeft (32);
        clear.setBounds (r.removeFromRight (24));
        r.removeFromRight (6);
        meterArea = r.removeFromRight (70);
        r.removeFromRight (8);
        source.setBounds (r.removeFromLeft (140));
        r.removeFromLeft (6);
        amount.setBounds (r.removeFromLeft (150));
        r.removeFromLeft (6);
        dest.setBounds (r.removeFromLeft (156));
        r.removeFromLeft (6);
        curve.setBounds (r.removeFromLeft (92));
        r.removeFromLeft (6);
        polarity.setBounds (r);
    }

    ModDest currentDest() const { return (ModDest) ctx.params().getInt (pid::mod (slot, ModSlotField::Dest)); }

private:
    bool isActive() const
    {
        return ctx.params().getInt (pid::mod (slot, ModSlotField::Source)) != 0 && ctx.params().getInt (pid::mod (slot, ModSlotField::Dest)) != 0;
    }

    void editorTick() override
    {
        const float now = ctx.telemetry.slotContribution[(size_t) slot].load (std::memory_order_relaxed);
        const bool act = isActive();
        if (std::abs (now - live) > 1.0e-3f || act != wasActive)
        {
            live = now;
            wasActive = act;
            repaint();
        }
    }

    EditorContext& ctx;
    const int slot;
    std::function<void (ModDest)> select;
    ParamChoice source;
    ParamSlider amount;
    ParamChoice dest, curve, polarity;
    juce::TextButton clear { "x" };
    juce::Rectangle<int> meterArea;
    float live = 0.0f;
    bool wasActive = false;
};

// =============================================================================================
/** Explains a destination: base value + every route's live contribution = final value. */
class ModInspector : public Card, private EditorContext::Listener
{
public:
    explicit ModInspector (EditorContext& c) : Card (c, "Why is it moving?")
    {
        titleColour = colours::modulation;
        for (int d = 1; d < kNumModDests; ++d)
            destination.addItem (getModDestInfo ((ModDest) d).name, d);
        destination.setSelectedId ((int) ModDest::FilterCutoff, juce::dontSendNotification);
        destination.onChange = [this] { repaint(); };
        addAndMakeVisible (destination);
        ctx.addListener (this);
    }

    ~ModInspector() override { ctx.removeListener (this); }

    void show (ModDest d)
    {
        if (d != ModDest::None)
            destination.setSelectedId ((int) d, juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        auto r = content();
        destination.setBounds (r.removeFromTop (24));
    }

    void paint (juce::Graphics& g) override
    {
        Card::paint (g);
        auto r = content().withTrimmedTop (32);
        const auto dest = (ModDest) destination.getSelectedId();
        if (dest == ModDest::None)
            return;

        const auto& info = getModDestInfo (dest);
        g.setFont (font (12.0f));
        g.setColour (colours::textDim);
        g.drawText (info.scope == ModScope::Voice ? "Per-note destination: values shown for the most recent note."
                                                  : "Global destination: evaluated once per block.",
                    r.removeFromTop (18), juce::Justification::centredLeft);
        r.removeFromTop (6);

        const int param = paramForModDest (dest);
        if (param >= 0)
        {
            const auto& def = getParamDef (param);
            auto line = r.removeFromTop (22);
            g.setColour (colours::text);
            g.setFont (font (13.0f, true));
            g.drawText ("Base", line.removeFromLeft (120), juce::Justification::centredLeft);
            g.setFont (monoFont (12.5f));
            g.drawText (def.formatter ? def.formatter (ctx.param (param)) : juce::String (ctx.param (param)), line, juce::Justification::centredRight);
        }

        float total = 0.0f;
        const auto& routing = ctx.routing();
        int count = 0;
        for (int i = 0; i < routing.numRoutes; ++i)
        {
            const auto& route = routing.routes[(size_t) i];
            if (route.dest != dest)
                continue;
            ++count;
            const float contribution = ctx.telemetry.slotContribution[(size_t) route.slot].load (std::memory_order_relaxed);
            total += contribution;

            auto line = r.removeFromTop (24);
            g.setColour (colours::text);
            g.setFont (font (12.5f));
            g.drawText ("+ " + juce::String (getModSourceInfo (route.source).name), line.removeFromLeft (130), juce::Justification::centredLeft);
            g.setColour (colours::textDim);
            g.setFont (monoFont (11.5f));
            g.drawText (juce::String (juce::roundToInt (route.amount * 100.0f)) + "%", line.removeFromLeft (48), juce::Justification::centredRight);
            line.removeFromLeft (8);
            drawBipolarBar (g, line.reduced (0, 7).toFloat(), contribution, colours::modulation);
        }

        if (count == 0)
        {
            g.setColour (colours::textFaint);
            g.setFont (font (12.5f));
            g.drawText ("Nothing modulates this destination.", r.removeFromTop (22), juce::Justification::centredLeft);
            return;
        }

        r.removeFromTop (6);
        g.setColour (colours::outline);
        g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
        r.removeFromTop (6);
        auto line = r.removeFromTop (24);
        g.setColour (colours::modulation);
        g.setFont (font (13.0f, true));
        g.drawText ("= Offset", line.removeFromLeft (130), juce::Justification::centredLeft);
        g.setFont (monoFont (12.5f));
        const float units = total * info.range;
        g.drawText ((units >= 0 ? "+" : "") + juce::String (units, 2) + " " + info.unit, line, juce::Justification::centredRight);

        if (param >= 0)
        {
            const auto& def = getParamDef (param);
            const float norm = modulatedNormalised (param, ctx.param (param), total);
            const float plain = def.range.convertFrom0to1 (norm);
            auto result = r.removeFromTop (24);
            g.setColour (colours::text);
            g.setFont (font (13.0f, true));
            g.drawText ("= Result", result.removeFromLeft (130), juce::Justification::centredLeft);
            g.setFont (monoFont (12.5f));
            g.drawText (def.formatter ? def.formatter (plain) : juce::String (plain), result, juce::Justification::centredRight);
        }

        // Live source values, for reference
        r.removeFromTop (14);
        g.setColour (colours::textFaint);
        g.setFont (displayFont (11.5f));
        g.drawText ("LIVE SOURCES", r.removeFromTop (16), juce::Justification::centredLeft);
        const int columns = 2;
        const int colWidth = r.getWidth() / columns;
        int index = 0;
        for (int s = 1; s < kNumModSources && r.getHeight() > 16; ++s)
        {
            const auto cell = juce::Rectangle<int> (r.getX() + (index % columns) * colWidth, r.getY() + (index / columns) * 17, colWidth - 8, 16);
            if (cell.getBottom() > r.getBottom())
                break;
            const auto& sInfo = getModSourceInfo ((ModSource) s);
            g.setColour (colours::textDim);
            g.setFont (font (11.0f));
            g.drawText (sInfo.shortName, cell.withWidth (48), juce::Justification::centredLeft);
            const float v = ctx.telemetry.sourceValue[(size_t) s].load (std::memory_order_relaxed);
            const auto bar = cell.withTrimmedLeft (50).reduced (0, 5).toFloat();
            if (sInfo.bipolar)
            {
                drawBipolarBar (g, bar, v, colours::accent.withAlpha (0.8f));
            }
            else
            {
                g.setColour (colours::control);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (colours::accent.withAlpha (0.8f));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, v)), 2.0f);
            }
            ++index;
        }
    }

private:
    void editorTick() override { repaint(); }
    juce::ComboBox destination;
};

// =============================================================================================
MatrixPage::MatrixPage (EditorContext& c) : Page (c)
{
    inspector = std::make_unique<ModInspector> (c);
    addAndMakeVisible (*inspector);
    for (int s = 0; s < kNumModSlots; ++s)
    {
        rows.push_back (std::make_unique<MatrixRow> (c, s, [this] (ModDest d) { inspector->show (d); }));
        addAndMakeVisible (*rows.back());
    }
}

MatrixPage::~MatrixPage() = default;

void MatrixPage::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromTop (22).withTrimmedRight (inspector->getWidth() + metrics::gap);
    g.setColour (colours::textFaint);
    g.setFont (displayFont (11.5f));
    header.removeFromLeft (32);
    const std::pair<const char*, int> cols[] = { { "SOURCE", 146 }, { "AMOUNT", 156 }, { "DESTINATION", 162 }, { "CURVE", 98 }, { "POLARITY", 100 } };
    for (const auto& [name, width] : cols)
        g.drawText (name, header.removeFromLeft (width), juce::Justification::centredLeft);
    g.drawText ("LIVE", header.withTrimmedRight (30).removeFromRight (70), juce::Justification::centred);
}

void MatrixPage::resized()
{
    auto r = getLocalBounds();
    inspector->setBounds (r.removeFromRight (330));
    r.removeFromRight (metrics::gap);
    r.removeFromTop (22);
    const int rowHeight = std::min (38, r.getHeight() / kNumModSlots);
    for (auto& row : rows)
        row->setBounds (r.removeFromTop (rowHeight));
}

} // namespace nedd::ui
