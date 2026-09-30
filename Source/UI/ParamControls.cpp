#include "ParamControls.h"
#include "HelpText.h"
#include "Modulation/ParamModMapping.h"

namespace nedd::ui
{
namespace
{
    constexpr float kStartAngle = -2.35619449f;   // -135 degrees
    constexpr float kEndAngle = 2.35619449f;

    juce::Point<float> pointOnCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre.getPointOnCircumference (radius, angle);
    }

    float angleFor (float normalised) { return kStartAngle + juce::jlimit (0.0f, 1.0f, normalised) * (kEndAngle - kStartAngle); }
} // namespace

juce::PopupMenu createModSourceMenu (int idOffset)
{
    juce::PopupMenu m;
    auto add = [&m, idOffset] (ModSource s) { m.addItem (idOffset + (int) s, getModSourceInfo (s).name); };

    m.addSectionHeader ("MPE (per note)");
    for (auto s : { ModSource::MpePressure, ModSource::MpeSlide, ModSource::MpePitch, ModSource::Velocity, ModSource::ReleaseVelocity })
        add (s);
    m.addSectionHeader ("Modulators");
    for (auto s : { ModSource::Lfo1, ModSource::Lfo2, ModSource::Lfo3, ModSource::AmpEnv, ModSource::FilterEnv, ModSource::ModEnv,
                    ModSource::Random, ModSource::SampleHold })
        add (s);
    m.addSectionHeader ("Performance");
    for (auto s : { ModSource::ModWheel, ModSource::PitchBend, ModSource::Aftertouch, ModSource::KeyPosition, ModSource::NoteNumber, ModSource::Gate })
        add (s);
    m.addSectionHeader ("Macros");
    for (auto s : { ModSource::Macro1, ModSource::Macro2, ModSource::Macro3, ModSource::Macro4 })
        add (s);
    return m;
}

juce::PopupMenu createModDestMenu (int idOffset)
{
    juce::PopupMenu m;
    auto add = [&m, idOffset] (ModDest d) { m.addItem (idOffset + (int) d, getModDestInfo (d).name); };
    auto range = [&add] (ModDest first, ModDest last) { for (int d = (int) first; d <= (int) last; ++d) add ((ModDest) d); };

    m.addSectionHeader ("Pitch");
    range (ModDest::Pitch, ModDest::Osc3Pitch);
    m.addSectionHeader ("Oscillators");
    range (ModDest::Osc1WtPos, ModDest::StereoWidth);
    m.addSectionHeader ("Filter");
    range (ModDest::FilterCutoff, ModDest::FilterMix);
    m.addSectionHeader ("Amp");
    range (ModDest::AmpLevel, ModDest::AmpRelease);
    m.addSectionHeader ("LFO");
    range (ModDest::Lfo1Rate, ModDest::Lfo3Depth);
    m.addSectionHeader ("Per-note sends & morph");
    range (ModDest::DelaySend, ModDest::Morph);
    m.addSectionHeader ("Global effects & arp");
    range (ModDest::DistDrive, ModDest::ArpProbability);
    return m;
}

// ---------------------------------------------------------------------------------------------
ParamKnob::ParamKnob (EditorContext& c, int index, const juce::String& text, juce::Colour colour)
    : ctx (c), paramIndex (index), dest (modDestForParam (index)), label (text), accent (colour)
{
    const auto& def = getParamDef (paramIndex);
    if (label.isEmpty())
        label = def.name;

    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kStartAngle + juce::MathConstants<float>::twoPi, kEndAngle + juce::MathConstants<float>::twoPi, true);
    setMouseDragSensitivity (180);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    setScrollWheelEnabled (true);
    setTooltip (helpForParam (paramIndex));

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.state, def.id, *this);
    setDoubleClickReturnValue (true, def.defaultValue);

    onDragStart = [this] { ctx.processor.getUndoManager().beginNewTransaction (getParamDef (paramIndex).name); };

    ctx.addListener (this);
}

ParamKnob::~ParamKnob()
{
    ctx.removeListener (this);
}

void ParamKnob::editorTick()
{
    const bool learning = ctx.processor.getMidiLearnTarget() == paramIndex;
    bool changed = learning;

    if (dest != ModDest::None)
    {
        const auto range = ctx.modulationRange (dest);
        const bool global = getModDestInfo (dest).scope == ModScope::Global;
        const bool nowLive = (global || ctx.hasActiveVoice()) && (range.getStart() != 0.0f || range.getEnd() != 0.0f);
        const float nowMod = nowLive ? ctx.liveModulation (dest) : 0.0f;

        changed = changed || range != modRange || nowLive != live || std::abs (nowMod - liveMod) > 1.0e-3f;
        modRange = range;
        live = nowLive;
        liveMod = nowMod;
    }

    if (ctx.getFrameCounter() % 15 == 0)
    {
        const int cc = ctx.processor.getMidiMappingFor (paramIndex);
        changed = changed || cc != midiCc;
        midiCc = cc;
    }

    if (changed)
        repaint();
}

void ParamKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto labelArea = bounds.removeFromBottom (15.0f);
    const float size = std::min (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto knob = bounds.withSizeKeepingCentre (size, size);
    const auto centre = knob.getCentre();
    const float radius = size * 0.5f;
    const float arcRadius = radius - 3.0f;

    const auto& def = getParamDef (paramIndex);
    const float norm = (float) valueToProportionOfLength (getValue());
    const bool bipolar = def.range.start < 0.0f && def.range.end > 0.0f;
    const float zeroNorm = bipolar ? def.range.convertTo0to1 (0.0f) : 0.0f;
    const bool enabled = isEnabled();

    auto arc = [&] (float from, float to, float r, float thickness, juce::Colour c)
    {
        juce::Path p;
        p.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (from), angleFor (to), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // Track
    arc (0.0f, 1.0f, arcRadius, 3.0f, colours::control);

    // Modulation reach
    const float basePlain = (float) getValue();
    if (dest != ModDest::None && (modRange.getStart() != 0.0f || modRange.getEnd() != 0.0f))
    {
        const float lo = modulatedNormalised (paramIndex, basePlain, modRange.getStart());
        const float hi = modulatedNormalised (paramIndex, basePlain, modRange.getEnd());
        arc (std::min (lo, hi), std::max (lo, hi), radius - 0.5f, 2.0f, colours::modulation.withAlpha (0.55f));
    }

    // Value
    if (std::abs (norm - zeroNorm) > 1.0e-4f)
        arc (std::min (norm, zeroNorm), std::max (norm, zeroNorm), arcRadius, 3.0f, enabled ? accent : colours::textFaint);

    // Body
    const float bodyRadius = radius * 0.62f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (colours::raised.brighter (0.12f), body.getTopLeft(), colours::panel, body.getBottomRight(), false));
    g.fillEllipse (body);
    g.setColour (colours::outlineStrong);
    g.drawEllipse (body, 1.0f);

    const float angle = angleFor (norm);
    g.setColour (enabled ? colours::text : colours::textFaint);
    g.drawLine (juce::Line<float> (pointOnCircle (centre, bodyRadius * 0.3f, angle), pointOnCircle (centre, bodyRadius * 0.92f, angle)), 2.0f);

    // Live modulated position of the focus note
    if (live && dest != ModDest::None)
    {
        const float modNorm = modulatedNormalised (paramIndex, basePlain, liveMod);
        g.setColour (colours::modulation);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pointOnCircle (centre, arcRadius, angleFor (modNorm))));
    }

    if (ctx.processor.getMidiLearnTarget() == paramIndex)
    {
        const float pulse = 0.5f + 0.5f * std::sin ((float) ctx.getFrameCounter() * 0.35f);
        g.setColour (colours::amber.withAlpha (0.4f + 0.5f * pulse));
        g.drawEllipse (knob.reduced (0.5f), 1.5f);
    }
    else if (midiCc >= 0)
    {
        g.setColour (colours::amber);
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ knob.getRight() - 3.0f, knob.getY() + 3.0f }));
    }

    const bool showValue = isMouseOverOrDragging();
    g.setColour (showValue ? colours::text : colours::textDim);
    g.setFont (showValue ? font (11.5f) : displayFont (11.5f));
    g.drawText (showValue ? getTextFromValue (getValue()) : label, labelArea.expanded (6.0f, 0.0f), juce::Justification::centred, true);
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }
    Slider::mouseDown (e);
}

void ParamKnob::showContextMenu()
{
    constexpr int kAddSource = 1000, kRemoveSlot = 2000, kLearn = 3000, kClearLearn = 3001, kReset = 3002;
    const auto& def = getParamDef (paramIndex);

    juce::PopupMenu menu;
    menu.addSectionHeader (def.name);

    if (dest != ModDest::None)
    {
        menu.addSubMenu ("Modulate with", createModSourceMenu (kAddSource));
        const auto& routing = ctx.routing();
        for (int r = 0; r < routing.numRoutes; ++r)
        {
            const auto& route = routing.routes[(size_t) r];
            if (route.dest == dest)
                menu.addItem (kRemoveSlot + route.slot, "Remove  " + juce::String (getModSourceInfo (route.source).name) + "  ("
                                                            + juce::String (juce::roundToInt (route.amount * 100.0f)) + "%)");
        }
        menu.addSeparator();
    }

    const int cc = ctx.processor.getMidiMappingFor (paramIndex);
    menu.addItem (kLearn, cc >= 0 ? "MIDI Learn (currently CC " + juce::String (cc) + ")" : "MIDI Learn");
    if (cc >= 0)
        menu.addItem (kClearLearn, "Clear MIDI mapping");
    menu.addItem (kReset, "Reset to default");

    juce::Component::SafePointer<ParamKnob> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, kAddSource, kRemoveSlot, kLearn, kClearLearn, kReset] (int result)
    {
        if (safe == nullptr || result == 0)
            return;
        auto& self = *safe;

        if (result >= kAddSource && result < kAddSource + kNumModSources)
        {
            const auto source = (ModSource) (result - kAddSource);
            const float amount = self.dest == ModDest::Pitch ? 0.25f : 0.5f;
            if (self.ctx.addModulation (source, self.dest, amount) < 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Modulation matrix full",
                                                        "All 16 matrix slots are in use. Remove a route on the MATRIX page first.");
        }
        else if (result >= kRemoveSlot && result < kRemoveSlot + kNumModSlots)
            self.ctx.removeModulationSlot (result - kRemoveSlot);
        else if (result == kLearn)
            self.ctx.processor.armMidiLearn (self.paramIndex);
        else if (result == kClearLearn)
            self.ctx.processor.clearMidiMapping (self.paramIndex);
        else if (result == kReset)
            self.ctx.setParam (self.paramIndex, getParamDef (self.paramIndex).defaultValue, "Reset " + getParamDef (self.paramIndex).name);

        self.repaint();
    });
}

// ---------------------------------------------------------------------------------------------
ParamSlider::ParamSlider (EditorContext& ctx, int paramIndex, juce::Colour colour)
{
    const auto& def = getParamDef (paramIndex);
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 18);
    setColour (juce::Slider::trackColourId, colour);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setTooltip (helpForParam (paramIndex));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.state, def.id, *this);
    setDoubleClickReturnValue (true, def.defaultValue);
    onDragStart = [&ctx, paramIndex] { ctx.processor.getUndoManager().beginNewTransaction (getParamDef (paramIndex).name); };
}

ParamChoice::ParamChoice (EditorContext& ctx, int paramIndex)
{
    const auto& def = getParamDef (paramIndex);
    if (def.type == ParamType::Choice)
    {
        addItemList (def.choices, 1);
    }
    else
    {
        int id = 1;
        for (int v = (int) def.range.start; v <= (int) def.range.end; ++v)
            addItem (def.formatter ? def.formatter ((float) v) : juce::String (v), id++);
    }

    setTooltip (helpForParam (paramIndex));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ctx.state, def.id, *this);
}

ParamToggle::ParamToggle (EditorContext& ctx, int paramIndex, const juce::String& text)
{
    setButtonText (text);
    setTooltip (helpForParam (paramIndex));
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.state, getParamDef (paramIndex).id, *this);
}

void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> components, int maxWidth)
{
    const int n = (int) components.size();
    if (n == 0)
        return;
    const int w = std::min (maxWidth, area.getWidth() / n);
    const int spare = area.getWidth() - w * n;
    const int gap = n > 1 ? spare / n : spare;
    int x = area.getX() + gap / 2;

    for (auto* c : components)
    {
        if (c != nullptr)
            c->setBounds (x, area.getY(), w, area.getHeight());
        x += w + gap;
    }
}

} // namespace nedd::ui
