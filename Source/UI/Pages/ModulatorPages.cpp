#include "Pages.h"

namespace nedd::ui
{
// =============================================================================================
/**
    Shows an LFO's waveform and its live value. In Custom mode the curve is editable:
    click to add a point, drag to move it, right-click or double-click a point to delete it.
*/
class LfoDisplay : public juce::Component, public juce::SettableTooltipClient, private EditorContext::Listener
{
public:
    LfoDisplay (EditorContext& c, int index) : ctx (c), lfoIndex (index)
    {
        ctx.addListener (this);
        points = ctx.processor.getLfoShapes().points[(size_t) lfoIndex];
    }

    ~LfoDisplay() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (colours::background.withAlpha (0.6f));
        g.fillRoundedRectangle (bounds, 4.0f);
        const auto area = bounds.reduced (10.0f, 10.0f);
        g.setColour (colours::outline);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        const auto shape = ctx.params().getChoice<LfoShape> (pid::lfo (lfoIndex, LfoField::Shape));
        const float phaseOffset = ctx.param (pid::lfo (lfoIndex, LfoField::Phase));
        const auto& shapes = ctx.processor.getLfoShapes();

        dsp::Lfo lfo;
        lfo.reset (0.0f, 1234u);
        dsp::Lfo::Settings s;
        s.shape = shape;
        s.phaseOffset = phaseOffset;
        s.customTable = shapes.tables[(size_t) lfoIndex].data();

        juce::Path path;
        constexpr int steps = 200;
        for (int i = 0; i <= steps; ++i)
        {
            lfo.setPhase ((float) i / (float) steps * 0.9999f);
            const float v = shape == LfoShape::SampleHold || shape == LfoShape::SmoothRandom ? sampleRandomShape (shape, i, steps) : lfo.evaluate (s);
            const juce::Point<float> p (area.getX() + area.getWidth() * (float) i / (float) steps, area.getCentreY() - v * area.getHeight() * 0.46f);
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        g.setColour (colours::modulation);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        // Live output of this LFO for the most recent note
        const float live = ctx.telemetry.sourceValue[(size_t) ModSource::Lfo1 + (size_t) lfoIndex].load (std::memory_order_relaxed);
        g.setColour (colours::text.withAlpha (0.8f));
        const float y = area.getCentreY() - live * area.getHeight() * 0.46f;
        g.fillRoundedRectangle (juce::Rectangle<float> (area.getRight() - 4.0f, y - 2.0f, 8.0f, 4.0f), 2.0f);

        if (shape == LfoShape::Custom)
        {
            for (size_t i = 0; i < this->points.size(); ++i)
            {
                const auto p = toScreen (this->points[i]);
                g.setColour ((int) i == dragging ? colours::text : colours::modulation);
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
            }
            g.setColour (colours::textFaint);
            g.setFont (font (10.5f));
            g.drawText ("click: add  |  drag: move  |  double-click: delete", bounds.reduced (6.0f).toNearestInt(), juce::Justification::bottomRight);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isCustom())
            return;
        dragging = hitPoint (e.position);
        if (dragging < 0 && ! e.mods.isPopupMenu() && (int) points.size() < dsp::LfoCustomShapes::kMaxPoints)
        {
            points.push_back (fromScreen (e.position));
            sortPoints();
            dragging = hitPoint (e.position);
            commit();
        }
        else if (dragging >= 0 && e.mods.isPopupMenu())
        {
            deletePoint (dragging);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isCustom())
            if (const int hit = hitPoint (e.position); hit >= 0)
                deletePoint (hit);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! isCustom() || dragging < 0)
            return;
        auto p = fromScreen (e.position);
        // End points stay at the edges so the cycle wraps cleanly.
        if (dragging == 0) p.x = 0.0f;
        if (dragging == (int) points.size() - 1) p.x = 1.0f;
        points[(size_t) dragging] = p;
        commit();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        sortPoints();
        dragging = -1;
        commit();
    }

private:
    bool isCustom() const { return ctx.params().getChoice<LfoShape> (pid::lfo (lfoIndex, LfoField::Shape)) == LfoShape::Custom; }

    float sampleRandomShape (LfoShape shape, int i, int total) const
    {
        // Deterministic preview of random shapes.
        const int steps = 8;
        const int step = i * steps / (total + 1);
        auto rnd = [] (int k) { return std::sin ((float) k * 12.9898f) * 43758.5453f; };
        auto value = [&rnd] (int k) { const float r = rnd (k); return (r - std::floor (r)) * 2.0f - 1.0f; };
        if (shape == LfoShape::SampleHold)
            return value (step);
        const float t = (float) (i * steps) / (float) (total + 1) - (float) step;
        return dsp::lerp (value (step), value (step + 1), 0.5f - 0.5f * std::cos (dsp::kPi * t));
    }

    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10.0f, 10.0f); }
    juce::Point<float> toScreen (dsp::LfoCustomShapes::Point p) const
    {
        const auto a = area();
        return { a.getX() + p.x * a.getWidth(), a.getCentreY() - p.y * a.getHeight() * 0.46f };
    }
    dsp::LfoCustomShapes::Point fromScreen (juce::Point<float> s) const
    {
        const auto a = area();
        return { juce::jlimit (0.0f, 1.0f, (s.x - a.getX()) / a.getWidth()),
                 juce::jlimit (-1.0f, 1.0f, (a.getCentreY() - s.y) / (a.getHeight() * 0.46f)) };
    }

    int hitPoint (juce::Point<float> s) const
    {
        for (size_t i = 0; i < points.size(); ++i)
            if (toScreen (points[i]).getDistanceFrom (s) < 8.0f)
                return (int) i;
        return -1;
    }

    void deletePoint (int index)
    {
        if (points.size() <= 2 || index == 0 || index == (int) points.size() - 1)
            return;
        points.erase (points.begin() + index);
        dragging = -1;
        commit();
    }

    void sortPoints()
    {
        std::sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.x < b.x; });
    }

    void commit()
    {
        auto shapes = ctx.processor.getLfoShapes();
        shapes.points[(size_t) lfoIndex] = points;
        ctx.processor.setLfoShapes (shapes);
        repaint();
    }

    void editorTick() override
    {
        const auto& current = ctx.processor.getLfoShapes().points[(size_t) lfoIndex];
        if (dragging < 0 && current.size() != points.size())
            points = current;
        repaint();
    }

    EditorContext& ctx;
    const int lfoIndex;
    std::vector<dsp::LfoCustomShapes::Point> points;
    int dragging = -1;
};

class LfoPanel : public Card, private EditorContext::Listener
{
public:
    LfoPanel (EditorContext& c, int index)
        : Card (c, "LFO " + juce::String (index + 1)),
          lfoIndex (index),
          display (c, index),
          shape (c, pid::lfo (index, LfoField::Shape)),
          division (c, pid::lfo (index, LfoField::Division)),
          sync (c, pid::lfo (index, LfoField::Sync), "Sync"),
          retrig (c, pid::lfo (index, LfoField::Retrigger), "Retrigger"),
          rate (c, pid::lfo (index, LfoField::Rate), "RATE", colours::modulation),
          phase (c, pid::lfo (index, LfoField::Phase), "PHASE", colours::modulation),
          fade (c, pid::lfo (index, LfoField::Fade), "FADE IN", colours::modulation),
          amount (c, pid::lfo (index, LfoField::Amount), "DEPTH", colours::modulation)
    {
        titleColour = colours::modulation;
        for (auto* comp : std::initializer_list<juce::Component*> { &display, &shape, &division, &sync, &retrig, &rate, &phase, &fade, &amount, &assign })
            addAndMakeVisible (comp);
        assign.setTooltip ("Route this LFO to a destination");
        assign.onClick = [this]
        {
            createModDestMenu (1).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&assign), [this] (int result)
            {
                if (result > 0)
                    ctx.addModulation ((ModSource) ((int) ModSource::Lfo1 + lfoIndex), (ModDest) (result - 1), 0.3f);
            });
        };
        ctx.addListener (this);
    }

    ~LfoPanel() override { ctx.removeListener (this); }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (panelTitleHeight).reduced (8, 3);
        assign.setBounds (strip.removeFromRight (80));

        auto r = content();
        display.setBounds (r.removeFromTop (r.getHeight() - 150));
        r.removeFromTop (6);
        auto row = r.removeFromTop (24);
        shape.setBounds (row.removeFromLeft (row.getWidth() / 2 - 4));
        row.removeFromLeft (8);
        division.setBounds (row);
        r.removeFromTop (4);
        auto toggles = r.removeFromTop (22);
        sync.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
        retrig.setBounds (toggles);
        r.removeFromTop (4);
        layoutRow (r.removeFromTop (70), { &rate, &phase, &fade, &amount }, 80);
    }

private:
    void editorTick() override
    {
        const bool synced = ctx.params().getBool (pid::lfo (lfoIndex, LfoField::Sync));
        division.setEnabled (synced);
        rate.setEnabled (! synced);
    }

    const int lfoIndex;
    LfoDisplay display;
    ParamChoice shape, division;
    ParamToggle sync, retrig;
    ParamKnob rate, phase, fade, amount;
    juce::TextButton assign { "+ ASSIGN" };
};

class LfoPage : public Page
{
public:
    explicit LfoPage (EditorContext& c) : Page (c), sampleHold (c, pid::global (GlobalField::SampleHoldDivision))
    {
        for (int l = 0; l < kNumLfos; ++l)
        {
            panels[(size_t) l] = std::make_unique<LfoPanel> (c, l);
            addAndMakeVisible (*panels[(size_t) l]);
        }
        addAndMakeVisible (sampleHold);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::textDim);
        g.setFont (displayFont (12.5f));
        g.drawText ("SAMPLE & HOLD SOURCE RATE", getLocalBounds().removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (colours::textFaint);
        g.setFont (font (12.0f));
        g.drawText ("Retriggered LFOs restart with every note (one LFO per note). Free LFOs share one phase; synced ones lock to the host.",
                    getLocalBounds().removeFromTop (24).withTrimmedLeft (380), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (24);
        sampleHold.setBounds (top.withTrimmedLeft (200).withWidth (100));
        r.removeFromTop (metrics::gap);
        const int w = (r.getWidth() - 2 * metrics::gap) / 3;
        for (auto& p : panels)
        {
            p->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (metrics::gap);
        }
    }

private:
    std::array<std::unique_ptr<LfoPanel>, (size_t) kNumLfos> panels;
    ParamChoice sampleHold;
};

std::unique_ptr<Page> createLfoPage (EditorContext& ctx) { return std::make_unique<LfoPage> (ctx); }

// =============================================================================================
// Effects
// =============================================================================================
class EffectCard : public Card
{
public:
    struct Control { int param; const char* label; };

    EffectCard (EditorContext& c, const juce::String& t, int onParam, std::vector<Control> knobSpecs,
                std::vector<int> choiceParams = {}, std::vector<std::pair<int, const char*>> toggleParams = {})
        : Card (c, t), onToggle (c, onParam)
    {
        titleColour = colours::text;
        addAndMakeVisible (onToggle);
        for (const auto& k : knobSpecs)
        {
            knobs.push_back (std::make_unique<ParamKnob> (c, k.param, k.label));
            addAndMakeVisible (*knobs.back());
        }
        for (int p : choiceParams)
        {
            choices.push_back (std::make_unique<ParamChoice> (c, p));
            addAndMakeVisible (*choices.back());
        }
        for (const auto& [p, label] : toggleParams)
        {
            toggles.push_back (std::make_unique<ParamToggle> (c, p, label));
            addAndMakeVisible (*toggles.back());
        }
    }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (panelTitleHeight).reduced (8, 3);
        onToggle.setBounds (strip.removeFromRight (30));

        auto r = content();
        if (! choices.empty())
        {
            auto row = r.removeFromTop (22);
            const int w = row.getWidth() / (int) choices.size();
            for (auto& c : choices) c->setBounds (row.removeFromLeft (w).reduced (2, 0));
            r.removeFromTop (4);
        }
        if (! toggles.empty())
        {
            auto row = r.removeFromTop (22);
            const int w = row.getWidth() / (int) toggles.size();
            for (auto& t : toggles) t->setBounds (row.removeFromLeft (w).reduced (2, 0));
            r.removeFromTop (4);
        }

        // As many knobs per row as fit at a readable width.
        const int perRow = juce::jlimit (1, std::max (1, (int) knobs.size()), r.getWidth() / 64);
        const int rows = ((int) knobs.size() + perRow - 1) / std::max (1, perRow);
        const int rowHeight = std::min (68, r.getHeight() / std::max (1, rows));
        size_t index = 0;
        for (int row = 0; row < rows; ++row)
        {
            auto line = r.removeFromTop (rowHeight);
            std::vector<juce::Component*> comps;
            for (int k = 0; k < perRow && index < knobs.size(); ++k)
                comps.push_back (knobs[index++].get());
            const int w = std::min (70, line.getWidth() / std::max (1, (int) comps.size()));
            int x = line.getX() + (line.getWidth() - w * (int) comps.size()) / 2;
            for (auto* c : comps) { c->setBounds (x, line.getY(), w, rowHeight); x += w; }
        }
    }

private:
    ParamToggle onToggle;
    std::vector<std::unique_ptr<ParamKnob>> knobs;
    std::vector<std::unique_ptr<ParamChoice>> choices;
    std::vector<std::unique_ptr<ParamToggle>> toggles;
};

/** Signal flow strip: voices > each effect (lit when on) > output. */
class SignalFlow : public juce::Component, private EditorContext::Listener
{
public:
    explicit SignalFlow (EditorContext& c) : ctx (c) { ctx.addListener (this); }
    ~SignalFlow() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const std::pair<const char*, FxField> stages[] = {
            { "DIST", FxField::DistOn }, { "SAT", FxField::SatOn }, { "CRUSH", FxField::CrushOn }, { "CHORUS", FxField::ChorusOn },
            { "PHASER", FxField::PhaserOn }, { "FLANGER", FxField::FlangerOn }, { "+DELAY", FxField::DelayOn }, { "+REVERB", FxField::ReverbOn },
            { "EQ", FxField::EqOn }, { "COMP", FxField::CompOn }, { "LIMIT", FxField::LimiterOn },
        };
        auto r = getLocalBounds().toFloat();
        const float w = r.getWidth() / 13.0f;
        auto box = [&g] (juce::Rectangle<float> b, const juce::String& text, bool on, juce::Colour c)
        {
            g.setColour (on ? c.withAlpha (0.18f) : colours::panel);
            g.fillRoundedRectangle (b.reduced (3.0f, 2.0f), 4.0f);
            g.setColour (on ? c : colours::outlineStrong);
            g.drawRoundedRectangle (b.reduced (3.0f, 2.0f), 4.0f, 1.0f);
            g.setColour (on ? colours::text : colours::textFaint);
            g.setFont (displayFont (11.5f));
            g.drawText (text, b, juce::Justification::centred);
        };
        box (r.removeFromLeft (w), "VOICES", true, colours::accent);
        for (const auto& [name, field] : stages)
            box (r.removeFromLeft (w), name, ctx.params().getBool (pid::fx (field)), colours::accent);
        box (r.removeFromLeft (w), "OUT", true, colours::accent);
    }

private:
    void editorTick() override
    {
        int signature = 0;
        for (int f = 0; f < (int) FxField::Count; ++f)
            if (getParamDef (pid::fx ((FxField) f)).type == ParamType::Bool)
                signature = signature * 2 + (ctx.params().getBool (pid::fx ((FxField) f)) ? 1 : 0);
        if (signature != last)
        {
            last = signature;
            repaint();
        }
    }

    EditorContext& ctx;
    int last = -1;
};

class EffectsPage : public Page
{
public:
    explicit EffectsPage (EditorContext& c) : Page (c), flow (c)
    {
        using F = FxField;
        auto f = [] (F field) { return pid::fx (field); };
        addAndMakeVisible (flow);

        cards.push_back (std::make_unique<EffectCard> (c, "Distortion", f (F::DistOn),
            std::vector<EffectCard::Control> { { f (F::DistDrive), "DRIVE" }, { f (F::DistTone), "TONE" }, { f (F::DistMix), "MIX" } },
            std::vector<int> { f (F::DistType) }));
        cards.push_back (std::make_unique<EffectCard> (c, "Saturation", f (F::SatOn),
            std::vector<EffectCard::Control> { { f (F::SatDrive), "DRIVE" }, { f (F::SatWarmth), "WARMTH" }, { f (F::SatMix), "MIX" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Bitcrush", f (F::CrushOn),
            std::vector<EffectCard::Control> { { f (F::CrushBits), "BITS" }, { f (F::CrushDownsample), "RATE" }, { f (F::CrushMix), "MIX" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Chorus", f (F::ChorusOn),
            std::vector<EffectCard::Control> { { f (F::ChorusRate), "RATE" }, { f (F::ChorusDepth), "DEPTH" }, { f (F::ChorusMix), "MIX" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Phaser", f (F::PhaserOn),
            std::vector<EffectCard::Control> { { f (F::PhaserRate), "RATE" }, { f (F::PhaserDepth), "DEPTH" }, { f (F::PhaserFeedback), "FEEDBK" }, { f (F::PhaserMix), "MIX" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Flanger", f (F::FlangerOn),
            std::vector<EffectCard::Control> { { f (F::FlangerRate), "RATE" }, { f (F::FlangerDepth), "DEPTH" }, { f (F::FlangerFeedback), "FEEDBK" }, { f (F::FlangerMix), "MIX" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Delay  (per-note send)", f (F::DelayOn),
            std::vector<EffectCard::Control> { { f (F::DelayTime), "TIME" }, { f (F::DelayFeedback), "FEEDBK" }, { f (F::DelayDamping), "DAMP" },
                                               { f (F::DelaySend), "SEND" }, { f (F::DelayReturn), "RETURN" } },
            std::vector<int> { f (F::DelayDivision) },
            std::vector<std::pair<int, const char*>> { { f (F::DelaySync), "Sync" }, { f (F::DelayPingPong), "Ping-pong" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Reverb  (per-note send)", f (F::ReverbOn),
            std::vector<EffectCard::Control> { { f (F::ReverbSize), "SIZE" }, { f (F::ReverbDamping), "DAMP" }, { f (F::ReverbPredelay), "PRE-DLY" },
                                               { f (F::ReverbWidth), "WIDTH" }, { f (F::ReverbSend), "SEND" }, { f (F::ReverbReturn), "RETURN" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "EQ", f (F::EqOn),
            std::vector<EffectCard::Control> { { f (F::EqLowGain), "LOW" }, { f (F::EqMidFreq), "MID FREQ" }, { f (F::EqMidGain), "MID" }, { f (F::EqHighGain), "HIGH" } }));
        cards.push_back (std::make_unique<EffectCard> (c, "Compressor", f (F::CompOn),
            std::vector<EffectCard::Control> { { f (F::CompThreshold), "THRESH" }, { f (F::CompRatio), "RATIO" }, { f (F::CompAttack), "ATTACK" },
                                               { f (F::CompRelease), "RELEASE" }, { f (F::CompMakeup), "MAKEUP" } }));

        for (auto& card : cards)
            addAndMakeVisible (*card);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        flow.setBounds (r.removeFromTop (30));
        r.removeFromTop (metrics::gap);
        const int rowHeight = (r.getHeight() - metrics::gap) / 2;
        auto row1 = r.removeFromTop (rowHeight);
        r.removeFromTop (metrics::gap);
        auto row2 = r;

        auto layout = [] (juce::Rectangle<int> row, std::vector<juce::Component*> comps, std::vector<int> weights)
        {
            int total = 0;
            for (int w : weights) total += w;
            const int available = row.getWidth() - metrics::gap * ((int) comps.size() - 1);
            for (size_t i = 0; i < comps.size(); ++i)
            {
                const int w = i + 1 == comps.size() ? row.getWidth() : available * weights[i] / total;
                comps[i]->setBounds (row.removeFromLeft (w));
                row.removeFromLeft (metrics::gap);
            }
        };

        layout (row1, { cards[0].get(), cards[1].get(), cards[2].get(), cards[3].get(), cards[4].get() }, { 5, 4, 4, 4, 5 });
        layout (row2, { cards[5].get(), cards[6].get(), cards[7].get(), cards[8].get(), cards[9].get() }, { 5, 5, 6, 5, 5 });
    }

private:
    SignalFlow flow;
    std::vector<std::unique_ptr<EffectCard>> cards;
};

std::unique_ptr<Page> createEffectsPage (EditorContext& ctx) { return std::make_unique<EffectsPage> (ctx); }

/** Compact effects overview for the MAIN page. */
class EffectsOverviewCard : public Card
{
public:
    explicit EffectsOverviewCard (EditorContext& c)
        : Card (c, "Effects"),
          delaySend (c, pid::fx (FxField::DelaySend), "DLY SEND"),
          reverbSend (c, pid::fx (FxField::ReverbSend), "REV SEND"),
          distDrive (c, pid::fx (FxField::DistDrive), "DRIVE"),
          reverbSize (c, pid::fx (FxField::ReverbSize), "SIZE")
    {
        titleColour = colours::text;
        const std::pair<FxField, const char*> toggles[] = {
            { FxField::DistOn, "Distortion" }, { FxField::SatOn, "Saturation" }, { FxField::CrushOn, "Bitcrush" }, { FxField::ChorusOn, "Chorus" },
            { FxField::PhaserOn, "Phaser" }, { FxField::FlangerOn, "Flanger" }, { FxField::DelayOn, "Delay" }, { FxField::ReverbOn, "Reverb" },
            { FxField::EqOn, "EQ" }, { FxField::CompOn, "Compressor" },
        };
        for (const auto& [field, name] : toggles)
        {
            switches.push_back (std::make_unique<ParamToggle> (c, pid::fx (field), name));
            addAndMakeVisible (*switches.back());
        }
        for (auto* comp : std::initializer_list<juce::Component*> { &delaySend, &reverbSend, &distDrive, &reverbSize, &open })
            addAndMakeVisible (comp);
        open.onClick = [this] { if (ctx.showPage) ctx.showPage ((int) PageId::Effects); };
    }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (panelTitleHeight).reduced (8, 3);
        open.setBounds (strip.removeFromRight (70));
        auto r = content();
        auto grid = r.removeFromTop (5 * 24);
        const int colWidth = grid.getWidth() / 2;
        for (size_t i = 0; i < switches.size(); ++i)
            switches[i]->setBounds (grid.getX() + (int) (i % 2) * colWidth, grid.getY() + (int) (i / 2) * 24, colWidth, 22);
        r.removeFromTop (8);
        layoutRow (r.removeFromTop (64), { &distDrive, &delaySend, &reverbSend, &reverbSize }, 70);
    }

private:
    std::vector<std::unique_ptr<ParamToggle>> switches;
    ParamKnob delaySend, reverbSend, distDrive, reverbSize;
    juce::TextButton open { "FX PAGE" };
};

std::unique_ptr<juce::Component> createEffectsOverview (EditorContext& ctx) { return std::make_unique<EffectsOverviewCard> (ctx); }

} // namespace nedd::ui
