#include "Pages.h"
#include "DSP/DspMath.h"

namespace nedd::ui
{
namespace
{
    struct Dimension
    {
        ModSource source;
        const char* name;
        juce::Colour colour;
    };

    const Dimension dimensions[] = {
        { ModSource::MpePitch, "PITCH", colours::pitch },
        { ModSource::MpePressure, "PRESSURE", colours::pressure },
        { ModSource::MpeSlide, "SLIDE", colours::slide },
        { ModSource::Velocity, "VELOCITY", colours::velocity },
        { ModSource::ReleaseVelocity, "RELEASE VEL", colours::releaseVel },
    };
    constexpr int kNumDimensions = 5;
} // namespace

// =============================================================================================
/** "What does each finger dimension do?" — lists and edits the matrix routes of each MPE source. */
class ExpressionRoutingPanel : public Card, private EditorContext::Listener
{
public:
    explicit ExpressionRoutingPanel (EditorContext& c) : Card (c, "Expression routing")
    {
        titleColour = colours::modulation;
        for (int d = 0; d < kNumDimensions; ++d)
        {
            assign[(size_t) d] = std::make_unique<juce::TextButton> ("+ ASSIGN");
            assign[(size_t) d]->setTooltip ("Route " + juce::String (dimensions[d].name).toLowerCase() + " to a destination (per note).");
            const auto source = dimensions[d].source;
            assign[(size_t) d]->onClick = [this, source, d]
            {
                createModDestMenu (1).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (assign[(size_t) d].get()),
                                                     [this, source] (int result)
                                                     {
                                                         if (result > 0 && ctx.addModulation (source, (ModDest) (result - 1), 0.5f) < 0)
                                                             juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Matrix full",
                                                                                                     "All 16 modulation slots are in use.");
                                                     });
            };
            addAndMakeVisible (*assign[(size_t) d]);
        }
        ctx.addListener (this);
        rebuild();
    }

    ~ExpressionRoutingPanel() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        Card::paint (g);
        for (int d = 0; d < kNumDimensions; ++d)
        {
            g.setColour (dimensions[d].colour);
            g.setFont (displayFont (12.5f));
            g.drawText (dimensions[d].name, headerAreas[(size_t) d], juce::Justification::centredLeft);
            if (lines[(size_t) d].empty())
            {
                g.setColour (colours::textFaint);
                g.setFont (font (11.5f));
                g.drawText ("not routed", headerAreas[(size_t) d].withTrimmedLeft (96), juce::Justification::centredLeft);
            }
            for (const auto& line : lines[(size_t) d])
            {
                g.setColour (colours::text);
                g.setFont (font (12.0f));
                g.drawText (line.destName, line.labelArea, juce::Justification::centredLeft, true);
            }
        }
    }

    void resized() override
    {
        auto r = content();
        for (int d = 0; d < kNumDimensions; ++d)
        {
            auto header = r.removeFromTop (22);
            assign[(size_t) d]->setBounds (header.removeFromRight (74).reduced (0, 1));
            headerAreas[(size_t) d] = header;
            for (auto& line : lines[(size_t) d])
            {
                auto row = r.removeFromTop (20);
                row.removeFromLeft (12);
                line.remove->setBounds (row.removeFromRight (20).reduced (1));
                line.labelArea = row.removeFromLeft (row.getWidth() / 2);
                line.amount->setBounds (row);
            }
            r.removeFromTop (4);
        }
    }

private:
    struct Line
    {
        int slot = 0;
        juce::String destName;
        std::unique_ptr<ParamSlider> amount;
        std::unique_ptr<juce::TextButton> remove;
        juce::Rectangle<int> labelArea;
    };

    void editorTick() override
    {
        juce::String signature;
        const auto& routing = ctx.routing();
        for (int i = 0; i < routing.numRoutes; ++i)
            signature << routing.routes[(size_t) i].slot << ":" << (int) routing.routes[(size_t) i].source << ">" << (int) routing.routes[(size_t) i].dest << ";";
        if (signature != lastSignature)
            rebuild();
    }

    void rebuild()
    {
        const auto& routing = ctx.routing();
        lastSignature.clear();
        for (int i = 0; i < routing.numRoutes; ++i)
            lastSignature << routing.routes[(size_t) i].slot << ":" << (int) routing.routes[(size_t) i].source << ">" << (int) routing.routes[(size_t) i].dest << ";";

        for (int d = 0; d < kNumDimensions; ++d)
        {
            lines[(size_t) d].clear();
            for (int i = 0; i < routing.numRoutes; ++i)
            {
                const auto& route = routing.routes[(size_t) i];
                if (route.source != dimensions[d].source)
                    continue;
                Line line;
                line.slot = route.slot;
                line.destName = getModDestInfo (route.dest).name;
                line.amount = std::make_unique<ParamSlider> (ctx, pid::mod (route.slot, ModSlotField::Amount), dimensions[d].colour);
                line.remove = std::make_unique<juce::TextButton> ("x");
                line.remove->setTooltip ("Remove this route");
                const int slot = route.slot;
                line.remove->onClick = [this, slot] { juce::MessageManager::callAsync ([this, slot] { ctx.removeModulationSlot (slot); }); };
                addAndMakeVisible (*line.amount);
                addAndMakeVisible (*line.remove);
                lines[(size_t) d].push_back (std::move (line));
            }
        }
        resized();
        repaint();
    }

    std::array<std::unique_ptr<juce::TextButton>, kNumDimensions> assign;
    std::array<std::vector<Line>, kNumDimensions> lines;
    std::array<juce::Rectangle<int>, kNumDimensions> headerAreas;
    juce::String lastSignature;
};

// =============================================================================================
/** Response curve of one expression dimension with the live input plotted on it. */
class CurveGraph : public juce::Component, private EditorContext::Listener
{
public:
    CurveGraph (EditorContext& c, int curveParam, std::function<float()> liveInput, juce::Colour col, const juce::String& label)
        : ctx (c), param (curveParam), input (std::move (liveInput)), colour (col), name (label)
    {
        ctx.addListener (this);
    }
    ~CurveGraph() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (bounds, 4.0f);
        auto area = bounds.reduced (10.0f);
        g.setColour (colours::outline);
        g.drawRect (area);
        g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

        const float curve = ctx.param (param);
        juce::Path path;
        for (int i = 0; i <= 64; ++i)
        {
            const float x = (float) i / 64.0f;
            const float y = dsp::responseCurve (x, curve);
            const juce::Point<float> p (area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight());
            if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        const float in = juce::jlimit (0.0f, 1.0f, value);
        const float out = dsp::responseCurve (in, curve);
        const juce::Point<float> dot (area.getX() + in * area.getWidth(), area.getBottom() - out * area.getHeight());
        g.setColour (colour.withAlpha (0.35f));
        g.drawLine (dot.x, area.getBottom(), dot.x, dot.y, 1.0f);
        g.drawLine (area.getX(), dot.y, dot.x, dot.y, 1.0f);
        g.setColour (colours::text);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));

        g.setColour (colour);
        g.setFont (displayFont (12.0f));
        g.drawText (name, area.reduced (6.0f).toNearestInt(), juce::Justification::topLeft);
        g.setColour (colours::textDim);
        g.setFont (monoFont (11.0f));
        g.drawText ("in " + juce::String (in, 2) + "  out " + juce::String (out, 2), area.reduced (6.0f).toNearestInt(), juce::Justification::bottomRight);
    }

private:
    void editorTick() override
    {
        const float v = input();
        if (std::abs (v - value) > 1.0e-3f || ctx.param (param) != lastCurve)
        {
            value = v;
            lastCurve = ctx.param (param);
            repaint();
        }
    }

    EditorContext& ctx;
    int param;
    std::function<float()> input;
    juce::Colour colour;
    juce::String name;
    float value = 0.0f, lastCurve = -9.0f;
};

/** Live meters of the raw incoming expression. */
class InputMeters : public juce::Component, private EditorContext::Listener
{
public:
    explicit InputMeters (EditorContext& c) : ctx (c) { ctx.addListener (this); }
    ~InputMeters() override { ctx.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto& t = ctx.telemetry;
        struct Meter { const char* name; float value; bool bipolar; juce::Colour colour; };
        const Meter meters[] = {
            { "PITCH BEND", t.lastPitchBend.load(), true, colours::pitch },
            { "PRESSURE", t.lastPressure.load(), false, colours::pressure },
            { "SLIDE (CC74)", t.lastSlide.load(), false, colours::slide },
            { "VELOCITY", t.lastVelocity.load(), false, colours::velocity },
        };

        for (const auto& m : meters)
        {
            auto row = r.removeFromTop (46.0f);
            g.setColour (m.colour);
            g.setFont (displayFont (12.0f));
            g.drawText (m.name, row.removeFromTop (18.0f), juce::Justification::centredLeft);
            g.setColour (colours::textDim);
            g.setFont (monoFont (11.0f));
            g.drawText (juce::String (m.value, 3), row.withHeight (0.0f).withY (row.getY() - 18.0f).withHeight (18.0f), juce::Justification::centredRight);

            auto bar = row.removeFromTop (14.0f);
            g.setColour (colours::control);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (m.colour);
            if (m.bipolar)
            {
                const float mid = bar.getCentreX();
                const float w = std::abs (m.value) * bar.getWidth() * 0.5f;
                g.fillRoundedRectangle (m.value >= 0.0f ? bar.withX (mid).withWidth (std::max (1.0f, w)) : bar.withX (mid - w).withWidth (w), 3.0f);
            }
            else
            {
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, m.value)), 3.0f);
            }
            r.removeFromTop (4.0f);
        }

        const bool activity = t.midiActivity.load() != lastActivity;
        g.setColour (activity ? colours::accent : colours::control);
        g.fillEllipse (r.removeFromTop (14.0f).withWidth (10.0f).withHeight (10.0f).translated (0.0f, 2.0f));
        g.setColour (colours::textDim);
        g.setFont (font (11.5f));
        g.drawText ("MIDI in", r.withHeight (14.0f).translated (16.0f, -14.0f), juce::Justification::centredLeft);
    }

private:
    void editorTick() override
    {
        repaint();
        lastActivity = pendingActivity;
        pendingActivity = ctx.telemetry.midiActivity.load();
    }

    EditorContext& ctx;
    int lastActivity = 0, pendingActivity = 0;
};

class CalibrationPanel : public Card
{
public:
    explicit CalibrationPanel (EditorContext& c)
        : Card (c, "MPE calibration"),
          meters (c),
          velocity (c, pid::global (GlobalField::VelocityCurve), [&c] { return c.telemetry.lastVelocity.load(); }, colours::velocity, "VELOCITY"),
          pressure (c, pid::global (GlobalField::PressureCurve), [&c] { return c.telemetry.lastPressure.load(); }, colours::pressure, "PRESSURE"),
          slide (c, pid::global (GlobalField::SlideCurve), [&c] { return c.telemetry.lastSlide.load(); }, colours::slide, "SLIDE"),
          velocityCurve (c, pid::global (GlobalField::VelocityCurve), "CURVE", colours::velocity),
          pressureCurve (c, pid::global (GlobalField::PressureCurve), "CURVE", colours::pressure),
          slideCurve (c, pid::global (GlobalField::SlideCurve), "CURVE", colours::slide),
          bendRange (c, pid::global (GlobalField::MpeBendRange), "BEND RANGE", colours::pitch),
          masterBend (c, pid::global (GlobalField::MasterBendRange), "MASTER BEND", colours::pitch),
          pitchSens (c, pid::global (GlobalField::PitchSensitivity), "PITCH SENS", colours::pitch),
          smoothing (c, pid::global (GlobalField::ExpressionSmoothing), "SMOOTHING"),
          quantize (c, pid::global (GlobalField::BendQuantize), "PITCH QNT", colours::pitch),
          mode (c, pid::global (GlobalField::MpeMode))
    {
        titleColour = colours::text;
        for (auto* comp : std::initializer_list<juce::Component*> { &meters, &velocity, &pressure, &slide, &velocityCurve, &pressureCurve,
                                                                    &slideCurve, &bendRange, &masterBend, &pitchSens, &smoothing, &quantize, &mode })
            addAndMakeVisible (comp);
    }

    void paint (juce::Graphics& g) override
    {
        Card::paint (g);
        g.setColour (colours::textFaint);
        g.setFont (font (12.0f));
        g.drawFittedText ("Press lightly and firmly, slide your finger and bend a note: set each curve so your full physical range "
                          "maps to the full 0..1 range. Match BEND RANGE to your controller (Seaboard 48, LinnStrument often 24).",
                          hintArea, juce::Justification::topLeft, 3);
        g.setFont (displayFont (12.0f));
        g.setColour (colours::textDim);
        g.drawText ("ZONE LAYOUT", modeLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = content();
        hintArea = r.removeFromBottom (44);
        meters.setBounds (r.removeFromLeft (230).reduced (4));
        r.removeFromLeft (metrics::gap);

        auto settings = r.removeFromRight (260);
        modeLabel = settings.removeFromTop (18);
        mode.setBounds (settings.removeFromTop (24));
        settings.removeFromTop (10);
        auto row1 = settings.removeFromTop (74);
        bendRange.setBounds (row1.removeFromLeft (row1.getWidth() / 3));
        masterBend.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
        pitchSens.setBounds (row1);
        auto row2 = settings.removeFromTop (74);
        smoothing.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
        quantize.setBounds (row2);

        r.removeFromRight (metrics::gap);
        const int w = (r.getWidth() - 2 * metrics::gap) / 3;
        CurveGraph* graphs[] = { &velocity, &pressure, &slide };
        ParamKnob* knobs[] = { &velocityCurve, &pressureCurve, &slideCurve };
        for (int i = 0; i < 3; ++i)
        {
            auto col = r.removeFromLeft (w);
            r.removeFromLeft (metrics::gap);
            knobs[i]->setBounds (col.removeFromBottom (66).withSizeKeepingCentre (70, 66));
            graphs[i]->setBounds (col.withSizeKeepingCentre (std::min (col.getWidth(), col.getHeight()), std::min (col.getWidth(), col.getHeight())));
        }
    }

private:
    InputMeters meters;
    CurveGraph velocity, pressure, slide;
    ParamKnob velocityCurve, pressureCurve, slideCurve, bendRange, masterBend, pitchSens, smoothing, quantize;
    ParamChoice mode;
    juce::Rectangle<int> hintArea, modeLabel;
};

// =============================================================================================
MpePage::MpePage (EditorContext& c) : Page (c), history (c), field (c, history), lanes (c), timeline (c, history)
{
    routing = std::make_unique<ExpressionRoutingPanel> (c);
    calibration = std::make_unique<CalibrationPanel> (c);

    for (auto* comp : std::initializer_list<juce::Component*> { &field, &lanes, &timeline, routing.get(), &performanceButton, &calibrationButton })
        addAndMakeVisible (comp);
    addChildComponent (*calibration);

    performanceButton.setClickingTogglesState (false);
    performanceButton.onClick = [this] { setView (false); };
    calibrationButton.onClick = [this] { setView (true); };
    setView (false);
}

MpePage::~MpePage() = default;

void MpePage::setView (bool showCalibration)
{
    showingCalibration = showCalibration;
    performanceButton.setToggleState (! showCalibration, juce::dontSendNotification);
    calibrationButton.setToggleState (showCalibration, juce::dontSendNotification);
    for (auto* comp : std::initializer_list<juce::Component*> { &field, &lanes, &timeline, routing.get() })
        comp->setVisible (! showCalibration);
    calibration->setVisible (showCalibration);
    repaint();
}

void MpePage::paint (juce::Graphics&) {}

void MpePage::resized()
{
    auto r = getLocalBounds();
    auto tabs = r.removeFromTop (26);
    performanceButton.setBounds (tabs.removeFromLeft (118).withTrimmedBottom (4));
    tabs.removeFromLeft (6);
    calibrationButton.setBounds (tabs.removeFromLeft (118).withTrimmedBottom (4));
    r.removeFromTop (4);

    calibration->setBounds (r);

    auto right = r.removeFromRight (440);
    r.removeFromRight (metrics::gap);
    field.setBounds (r.removeFromTop (r.getHeight() * 55 / 100));
    r.removeFromTop (metrics::gap);
    timeline.setBounds (r);

    lanes.setBounds (right.removeFromTop (right.getHeight() * 45 / 100));
    right.removeFromTop (metrics::gap);
    routing->setBounds (right);
}

} // namespace nedd::ui
