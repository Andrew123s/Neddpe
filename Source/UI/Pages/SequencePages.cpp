#include "Pages.h"

namespace nedd::ui
{
namespace
{
    enum class Lane { Note = 0, Velocity, Gate, Slide, Pressure, Pitch, Probability, Ratchet, Accent, Count };

    struct LaneInfo { const char* name; juce::Colour colour; };
    const LaneInfo laneInfos[] = {
        { "NOTE", colours::accent }, { "VELOCITY", colours::velocity }, { "GATE", colours::text }, { "SLIDE", colours::slide },
        { "PRESSURE", colours::pressure }, { "PITCH", colours::pitch }, { "PROBABILITY", colours::textDim },
        { "RATCHET", colours::amber }, { "ACCENT", colours::amber },
    };

    constexpr int kLowNote = 36, kHighNote = 84;
} // namespace

// =============================================================================================
/**
    32-step editor. The top row toggles steps; the lane below edits one dimension per step by
    painting across the bars. Every step carries MPE pressure, slide and a pitch glide.
*/
class StepGrid : public juce::Component, private EditorContext::Listener
{
public:
    explicit StepGrid (EditorContext& c) : ctx (c) { ctx.addListener (this); }
    ~StepGrid() override { ctx.removeListener (this); }

    void setLane (Lane l) { lane = l; repaint(); }
    Lane getLane() const { return lane; }

    void paint (juce::Graphics& g) override
    {
        const auto& pattern = editing ? working : ctx.processor.getPattern();
        const int length = ctx.params().getInt (pid::seq (SeqField::Length));
        const int playing = ctx.telemetry.seqStep.load();
        const auto info = laneInfos[(int) lane];

        for (int i = 0; i < SequencerPattern::kMaxSteps; ++i)
        {
            const auto& s = pattern.steps[(size_t) i];
            const bool inRange = i < length;
            auto toggle = toggleArea (i);
            auto bar = laneArea (i);

            g.setColour (i == playing ? colours::accent.withAlpha (0.25f) : (i % 4 == 0 ? colours::raised : colours::panel));
            g.fillRoundedRectangle (toggle.toFloat(), 3.0f);
            if (s.on)
            {
                g.setColour ((inRange ? colours::accent : colours::textFaint).withAlpha (s.accent ? 1.0f : 0.7f));
                g.fillRoundedRectangle (toggle.reduced (4).toFloat(), 2.0f);
            }

            g.setColour (inRange ? colours::control.withAlpha (0.6f) : colours::panel);
            g.fillRoundedRectangle (bar.toFloat(), 3.0f);
            const float value = normalisedValue (s);
            const auto colour = info.colour.withAlpha (inRange && s.on ? 0.9f : 0.3f);
            g.setColour (colour);

            if (lane == Lane::Pitch)
            {
                const float mid = (float) bar.getCentreY();
                const float y = (float) bar.getY() + (1.0f - value) * (float) bar.getHeight();
                g.fillRect (juce::Rectangle<float> ((float) bar.getX() + 2.0f, std::min (mid, y), (float) bar.getWidth() - 4.0f, std::max (1.0f, std::abs (y - mid))));
            }
            else
            {
                const float h = std::max (2.0f, value * (float) bar.getHeight());
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) bar.getX() + 2.0f, (float) bar.getBottom() - h, (float) bar.getWidth() - 4.0f, h), 2.0f);
            }

            g.setColour (inRange ? colours::text : colours::textFaint);
            g.setFont (font (10.0f));
            g.drawText (valueText (s), bar.withTrimmedBottom (2).removeFromBottom (14), juce::Justification::centred);

            if (i == playing)
            {
                g.setColour (colours::accent);
                g.drawRoundedRectangle (bar.toFloat().expanded (1.0f), 3.0f, 1.0f);
            }
        }

        g.setColour (colours::textFaint);
        g.setFont (monoFont (10.0f));
        for (int i = 0; i < SequencerPattern::kMaxSteps; i += 4)
            g.drawText (juce::String (i + 1), toggleArea (i).translated (0, -14).withHeight (12), juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        before = ctx.processor.getPattern();
        working = before;
        editing = true;
        const int step = stepAt (e.x);
        if (step < 0)
            return;

        if (toggleArea (step).contains (e.getPosition()))
        {
            toggleMode = true;
            paintOn = ! working.steps[(size_t) step].on;
            working.steps[(size_t) step].on = paintOn;
        }
        else
        {
            toggleMode = false;
            applyValue (step, e.y);
        }
        push();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int step = stepAt (e.x);
        if (step < 0)
            return;
        if (toggleMode)
            working.steps[(size_t) step].on = paintOn;
        else
            applyValue (step, e.y);
        push();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        ctx.processor.setPattern (before);
        ctx.processor.setPattern (working, "Edit sequencer");
        editing = false;
        repaint();
    }

    void resized() override {}

private:
    juce::Rectangle<int> toggleArea (int step) const
    {
        const float w = (float) getWidth() / (float) SequencerPattern::kMaxSteps;
        return juce::Rectangle<int> ((int) (w * (float) step) + 1, 16, (int) w - 2, 22);
    }

    juce::Rectangle<int> laneArea (int step) const
    {
        const float w = (float) getWidth() / (float) SequencerPattern::kMaxSteps;
        return juce::Rectangle<int> ((int) (w * (float) step) + 1, 44, (int) w - 2, getHeight() - 46);
    }

    int stepAt (int x) const
    {
        const int step = (int) ((float) x / ((float) getWidth() / (float) SequencerPattern::kMaxSteps));
        return juce::isPositiveAndBelow (step, SequencerPattern::kMaxSteps) ? step : -1;
    }

    float normalisedValue (const SeqStep& s) const
    {
        switch (lane)
        {
            case Lane::Note:        return (float) (s.note - kLowNote) / (float) (kHighNote - kLowNote);
            case Lane::Velocity:    return s.velocity;
            case Lane::Gate:        return s.gate / 1.5f;
            case Lane::Slide:       return s.slide;
            case Lane::Pressure:    return s.pressure;
            case Lane::Pitch:       return 0.5f + s.pitch / 24.0f;
            case Lane::Probability: return s.probability;
            case Lane::Ratchet:     return (float) s.ratchet / 4.0f;
            case Lane::Accent:      return s.accent ? 1.0f : 0.0f;
            case Lane::Count:       break;
        }
        return 0.0f;
    }

    juce::String valueText (const SeqStep& s) const
    {
        switch (lane)
        {
            case Lane::Note:        return noteName (s.note);
            case Lane::Velocity:    return juce::String (juce::roundToInt (s.velocity * 100.0f));
            case Lane::Gate:        return juce::String (juce::roundToInt (s.gate * 100.0f));
            case Lane::Slide:       return juce::String (juce::roundToInt (s.slide * 100.0f));
            case Lane::Pressure:    return juce::String (juce::roundToInt (s.pressure * 100.0f));
            case Lane::Pitch:       return (s.pitch > 0 ? "+" : "") + juce::String (s.pitch, 1);
            case Lane::Probability: return juce::String (juce::roundToInt (s.probability * 100.0f));
            case Lane::Ratchet:     return "x" + juce::String (s.ratchet);
            case Lane::Accent:      return s.accent ? "ACC" : "";
            case Lane::Count:       break;
        }
        return {};
    }

    void applyValue (int step, int y)
    {
        const auto area = laneArea (step);
        const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (float) (y - area.getY()) / (float) area.getHeight());
        auto& s = working.steps[(size_t) step];
        switch (lane)
        {
            case Lane::Note:        s.note = kLowNote + juce::roundToInt (v * (float) (kHighNote - kLowNote)); break;
            case Lane::Velocity:    s.velocity = std::max (0.05f, v); break;
            case Lane::Gate:        s.gate = juce::jlimit (0.05f, 1.5f, v * 1.5f); break;
            case Lane::Slide:       s.slide = v; break;
            case Lane::Pressure:    s.pressure = v; break;
            case Lane::Pitch:       s.pitch = std::round ((v - 0.5f) * 24.0f * 2.0f) / 2.0f; break;
            case Lane::Probability: s.probability = v; break;
            case Lane::Ratchet:     s.ratchet = juce::jlimit (1, 4, 1 + (int) (v * 3.999f)); break;
            case Lane::Accent:      s.accent = v > 0.5f; break;
            case Lane::Count:       break;
        }
    }

    void push()
    {
        ctx.processor.setPattern (working);
        repaint();
    }

    void editorTick() override
    {
        const int step = ctx.telemetry.seqStep.load();
        const int version = ctx.processor.getPatternVersion();
        if (step != lastStep || version != lastVersion)
        {
            lastStep = step;
            lastVersion = version;
            repaint();
        }
    }

    EditorContext& ctx;
    Lane lane = Lane::Note;
    SequencerPattern working, before;
    bool editing = false, toggleMode = false, paintOn = true;
    int lastStep = -2, lastVersion = -1;
};

class SequencerPage : public Page
{
public:
    explicit SequencerPage (EditorContext& c)
        : Page (c), grid (c),
          on (c, pid::seq (SeqField::On), "Sequencer on"),
          rate (c, pid::seq (SeqField::Division)),
          clock (c, pid::seq (SeqField::Clock)),
          length (c, pid::seq (SeqField::Length), "LENGTH"),
          swing (c, pid::seq (SeqField::Swing), "SWING"),
          transpose (c, pid::seq (SeqField::Transpose), "TRANSPOSE")
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &grid, &on, &rate, &clock, &length, &swing, &transpose, &randomise, &clear, &shiftLeft, &shiftRight })
            addAndMakeVisible (comp);

        for (int l = 0; l < (int) Lane::Count; ++l)
        {
            auto b = std::make_unique<juce::TextButton> (laneInfos[l].name);
            b->setClickingTogglesState (false);
            b->onClick = [this, l] { setLane ((Lane) l); };
            addAndMakeVisible (*b);
            laneButtons.push_back (std::move (b));
        }
        setLane (Lane::Note);

        randomise.setTooltip ("Musical random pattern: notes from the current scale area, expressive pressure/slide shapes.");
        randomise.onClick = [this] { randomisePattern(); };
        clear.onClick = [this]
        {
            SequencerPattern p = ctx.processor.getPattern();
            for (auto& s : p.steps) { s.on = false; }
            ctx.processor.setPattern (p, "Clear sequencer");
        };
        shiftLeft.onClick = [this] { shift (-1); };
        shiftRight.onClick = [this] { shift (1); };
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, controlsArea.toFloat(), "Step sequencer", colours::accent);
        drawPanel (g, gridArea.toFloat(), {}, colours::textDim);
        g.setColour (colours::textFaint);
        g.setFont (font (12.0f));
        g.drawText ("Each step sends a note with its own pressure, slide and pitch glide (MPE). Paint values across the bars.",
                    hintArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        controlsArea = r.removeFromTop (110);
        r.removeFromTop (metrics::gap);
        gridArea = r;

        auto c = controlsArea.withTrimmedTop (panelTitleHeight).reduced (10, 4);
        auto left = c.removeFromLeft (200);
        on.setBounds (left.removeFromTop (24));
        left.removeFromTop (4);
        rate.setBounds (left.removeFromTop (22));
        left.removeFromTop (4);
        clock.setBounds (left.removeFromTop (22));
        c.removeFromLeft (16);
        layoutRow (c.removeFromLeft (240), { &length, &swing, &transpose }, 76);
        c.removeFromLeft (16);
        auto buttons = c.removeFromLeft (220);
        auto row1 = buttons.removeFromTop (28);
        randomise.setBounds (row1.removeFromLeft (row1.getWidth() / 2 - 3));
        row1.removeFromLeft (6);
        clear.setBounds (row1);
        buttons.removeFromTop (6);
        auto row2 = buttons.removeFromTop (28);
        shiftLeft.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
        row2.removeFromLeft (6);
        shiftRight.setBounds (row2);
        hintArea = c.reduced (12, 0);

        auto g = gridArea.reduced (10, 8);
        auto tabs = g.removeFromTop (26);
        const int w = tabs.getWidth() / (int) laneButtons.size();
        for (auto& b : laneButtons)
            b->setBounds (tabs.removeFromLeft (w).reduced (2, 0));
        g.removeFromTop (6);
        grid.setBounds (g);
    }

private:
    void setLane (Lane l)
    {
        grid.setLane (l);
        for (size_t i = 0; i < laneButtons.size(); ++i)
            laneButtons[i]->setToggleState ((int) i == (int) l, juce::dontSendNotification);
    }

    void shift (int direction)
    {
        auto p = ctx.processor.getPattern();
        const int steps = ctx.params().getInt (pid::seq (SeqField::Length));
        auto copy = p;
        for (int i = 0; i < steps; ++i)
            p.steps[(size_t) ((i + direction + steps) % steps)] = copy.steps[(size_t) i];
        ctx.processor.setPattern (p, "Shift sequencer");
    }

    void randomisePattern()
    {
        auto p = ctx.processor.getPattern();
        juce::Random r;
        const int scale[] = { 0, 3, 5, 7, 10, 12, 15 };
        const int root = 48 + r.nextInt (5);
        for (auto& s : p.steps)
        {
            s.on = r.nextFloat() < 0.72f;
            s.note = root + scale[r.nextInt (7)];
            s.velocity = 0.55f + 0.4f * r.nextFloat();
            s.gate = r.nextFloat() < 0.2f ? 1.0f : 0.3f + 0.4f * r.nextFloat();
            s.slide = r.nextFloat();
            s.pressure = r.nextFloat() * 0.8f;
            s.pitch = r.nextFloat() < 0.15f ? (r.nextBool() ? 2.0f : -2.0f) : 0.0f;
            s.probability = r.nextFloat() < 0.2f ? 0.6f : 1.0f;
            s.ratchet = r.nextFloat() < 0.1f ? 2 : 1;
            s.accent = r.nextFloat() < 0.2f;
        }
        ctx.processor.setPattern (p, "Randomise sequencer");
    }

    StepGrid grid;
    ParamToggle on;
    ParamChoice rate, clock;
    ParamKnob length, swing, transpose;
    juce::TextButton randomise { "RANDOMISE" }, clear { "CLEAR" }, shiftLeft { "< SHIFT" }, shiftRight { "SHIFT >" };
    std::vector<std::unique_ptr<juce::TextButton>> laneButtons;
    juce::Rectangle<int> controlsArea, gridArea, hintArea;
};

std::unique_ptr<Page> createSequencerPage (EditorContext& ctx) { return std::make_unique<SequencerPage> (ctx); }

// =============================================================================================
/** Custom arp pattern: each of 16 steps is a rest or an index into the held notes. */
class ArpPatternEditor : public juce::Component, private EditorContext::Listener
{
public:
    explicit ArpPatternEditor (EditorContext& c) : ctx (c) { ctx.addListener (this); }
    ~ArpPatternEditor() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto& pattern = editing ? working : ctx.processor.getArpPattern();
        const int current = ctx.telemetry.arpStep.load();
        const float w = (float) getWidth() / (float) ArpPattern::kSteps;
        constexpr int maxValue = 8;

        for (int i = 0; i < ArpPattern::kSteps; ++i)
        {
            auto cell = juce::Rectangle<float> (w * (float) i + 1.0f, 0.0f, w - 2.0f, (float) getHeight());
            g.setColour (i == current ? colours::accent.withAlpha (0.2f) : colours::control.withAlpha (0.5f));
            g.fillRoundedRectangle (cell, 3.0f);
            const int v = pattern.steps[(size_t) i];
            g.setFont (font (11.0f));
            if (v < 0)
            {
                g.setColour (colours::textFaint);
                g.drawText ("REST", cell.removeFromBottom (16.0f), juce::Justification::centred);
                continue;
            }
            const float h = cell.getHeight() * (float) (v + 1) / (float) (maxValue + 1);
            g.setColour (colours::amber.withAlpha (0.85f));
            g.fillRoundedRectangle (cell.withTop (cell.getBottom() - h).reduced (3.0f, 0.0f), 2.0f);
            g.setColour (colours::background);
            g.drawText (juce::String (v + 1), cell.removeFromBottom (16.0f), juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        before = ctx.processor.getArpPattern();
        working = before;
        editing = true;
        apply (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override { apply (e); }

    void mouseUp (const juce::MouseEvent&) override
    {
        ctx.processor.setArpPattern (before);
        ctx.processor.setArpPattern (working, "Edit arp pattern");
        editing = false;
    }

private:
    void apply (const juce::MouseEvent& e)
    {
        const int step = (int) ((float) e.x / ((float) getWidth() / (float) ArpPattern::kSteps));
        if (! juce::isPositiveAndBelow (step, ArpPattern::kSteps))
            return;
        const float v = 1.0f - (float) e.y / (float) getHeight();
        working.steps[(size_t) step] = juce::jlimit (-1, 8, (int) std::floor (v * 10.0f) - 1);
        ctx.processor.setArpPattern (working);
        repaint();
    }

    void editorTick() override
    {
        const int step = ctx.telemetry.arpStep.load();
        if (step != lastStep) { lastStep = step; repaint(); }
    }

    EditorContext& ctx;
    ArpPattern working, before;
    bool editing = false;
    int lastStep = -2;
};

class ArpPage : public Page, private EditorContext::Listener
{
public:
    explicit ArpPage (EditorContext& c)
        : Page (c), patternEditor (c),
          on (c, pid::arp (ArpField::On), "Arpeggiator on"),
          mode (c, pid::arp (ArpField::Mode)),
          rate (c, pid::arp (ArpField::Division)),
          velocityMode (c, pid::arp (ArpField::VelocityMode)),
          gate (c, pid::arp (ArpField::Gate), "GATE", colours::amber),
          swing (c, pid::arp (ArpField::Swing), "SWING", colours::amber),
          octaves (c, pid::arp (ArpField::Octaves), "OCTAVES", colours::amber),
          length (c, pid::arp (ArpField::Length), "LENGTH", colours::amber),
          velocity (c, pid::arp (ArpField::Velocity), "VELOCITY", colours::amber),
          probability (c, pid::arp (ArpField::Probability), "PROB", colours::amber),
          ratchet (c, pid::arp (ArpField::Ratchet), "RATCHET", colours::amber),
          repeat (c, pid::arp (ArpField::Repeat), "REPEAT", colours::amber),
          accent (c, pid::arp (ArpField::Accent), "ACCENT", colours::amber),
          accentEvery (c, pid::arp (ArpField::AccentEvery), "ACC EVERY", colours::amber)
    {
        for (auto* comp : std::initializer_list<juce::Component*> { &patternEditor, &on, &mode, &rate, &velocityMode, &gate, &swing, &octaves, &length,
                                                                    &velocity, &probability, &ratchet, &repeat, &accent, &accentEvery })
            addAndMakeVisible (comp);
        ctx.addListener (this);
    }

    ~ArpPage() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, mainArea.toFloat(), "Arpeggiator", colours::amber);
        drawPanel (g, patternArea.toFloat(), "Custom pattern  (mode: Custom)  -  bar height = held note, bottom = rest", colours::amber);

        auto status = statusArea;
        g.setColour (colours::textDim);
        g.setFont (font (12.5f));
        g.drawText ("Held keys: " + juce::String (held), status.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.setFont (font (12.0f));
        g.drawFittedText ("MPE: every arpeggiated note keeps following the key that produced it. Press, slide or bend a held key and "
                          "all of its arp notes respond, while the other keys' notes stay independent. Arp gate and probability are "
                          "global matrix destinations.",
                          status, juce::Justification::topLeft, 6);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        mainArea = r.removeFromTop (300);
        r.removeFromTop (metrics::gap);
        patternArea = r;

        auto c = mainArea.withTrimmedTop (panelTitleHeight).reduced (12, 6);
        auto left = c.removeFromLeft (220);
        on.setBounds (left.removeFromTop (24));
        left.removeFromTop (8);
        mode.setBounds (left.removeFromTop (24));
        left.removeFromTop (6);
        rate.setBounds (left.removeFromTop (24));
        left.removeFromTop (6);
        velocityMode.setBounds (left.removeFromTop (24));
        left.removeFromTop (12);
        statusArea = left;
        c.removeFromLeft (20);
        layoutRow (c.removeFromTop (80), { &gate, &swing, &octaves, &length, &velocity }, 90);
        c.removeFromTop (10);
        layoutRow (c.removeFromTop (80), { &probability, &ratchet, &repeat, &accent, &accentEvery }, 90);

        patternEditor.setBounds (patternArea.withTrimmedTop (panelTitleHeight).reduced (12, 10));
    }

private:
    void editorTick() override
    {
        const int now = ctx.telemetry.arpHeld.load();
        if (now != held) { held = now; repaint(); }
        const bool custom = ctx.params().getChoice<ArpMode> (pid::arp (ArpField::Mode)) == ArpMode::Custom;
        patternEditor.setAlpha (custom ? 1.0f : 0.45f);
    }

    ArpPatternEditor patternEditor;
    ParamToggle on;
    ParamChoice mode, rate, velocityMode;
    ParamKnob gate, swing, octaves, length, velocity, probability, ratchet, repeat, accent, accentEvery;
    juce::Rectangle<int> mainArea, patternArea, statusArea;
    int held = -1;
};

std::unique_ptr<Page> createArpPage (EditorContext& ctx) { return std::make_unique<ArpPage> (ctx); }

} // namespace nedd::ui
