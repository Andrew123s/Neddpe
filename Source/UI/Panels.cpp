#include "Panels.h"

namespace nedd::ui
{
namespace
{
    void place (juce::Component& c, juce::Rectangle<int> r)
    {
        c.setBounds (r);
        c.setVisible (true);
    }
}

// =============================================================================================
// OscillatorPanel
// =============================================================================================
OscillatorPanel::OscillatorPanel (EditorContext& c, int index, bool isCompact)
    : Card (c, "OSC " + juce::String (index + 1)),
      oscIndex (index),
      compact (isCompact),
      onToggle (c, pid::osc (index, OscField::On)),
      engineChoice (c, pid::osc (index, OscField::Engine)),
      waveChoice (c, pid::osc (index, OscField::Wave)),
      tableChoice (c, pid::osc (index, OscField::Table)),
      noiseChoice (c, pid::osc (index, OscField::NoiseType)),
      algoChoice (c, pid::osc (index, OscField::FmAlgorithm)),
      routeChoice (c, pid::osc (index, OscField::Route)),
      fmSourceChoice (c, pid::osc (index, OscField::FmSource)),
      op1RatioChoice (c, pid::osc (index, OscField::Op1Ratio)),
      op2RatioChoice (c, pid::osc (index, OscField::Op2Ratio)),
      syncToggle (c, pid::osc (index, OscField::Sync), "Sync"),
      display (c, index)
{
    titleColour = colours::text;
    for (auto* comp : std::initializer_list<juce::Component*> { &onToggle, &engineChoice, &waveChoice, &tableChoice, &noiseChoice, &algoChoice,
                                                                &routeChoice, &fmSourceChoice, &op1RatioChoice, &op2RatioChoice, &syncToggle,
                                                                &display, &fmCaption, &unisonCaption, &operatorCaption })
        addChildComponent (comp);

    onToggle.setVisible (true);
    engineChoice.setVisible (true);
    display.setVisible (true);

    knob (OscField::Octave, "OCT");
    knob (OscField::Semi, "SEMI");
    knob (OscField::Fine, "FINE");
    knob (OscField::Level, "LEVEL");
    knob (OscField::Pan, "PAN");
    knob (OscField::PulseWidth, "PW");
    knob (OscField::WtPos, "WT POS");
    knob (OscField::Unison, "VOICES");
    knob (OscField::Detune, "DETUNE");
    knob (OscField::Spread, "SPREAD");
    knob (OscField::FmAmount, "FM");
    knob (OscField::Ring, "RING");
    knob (OscField::Phase, "PHASE");
    knob (OscField::PhaseRandom, "RAND");
    knob (OscField::Op1Amount, "MOD 1");
    knob (OscField::Op2Amount, "MOD 2");
    knob (OscField::OpFine, "FINE R");
    knob (OscField::FmFeedback, "FEEDBK");
    knob (OscField::FmEnvAmount, "ENV");
    knob (OscField::FmKeyTrack, "KEY");

    ctx.addListener (this);
}

OscillatorPanel::~OscillatorPanel() { ctx.removeListener (this); }

ParamKnob& OscillatorPanel::knob (OscField f, const juce::String& label)
{
    auto k = std::make_unique<ParamKnob> (ctx, p (f), label);
    auto& ref = *k;
    addChildComponent (ref);
    knobs[f] = &ref;
    owned.push_back (std::move (k));
    return ref;
}

void OscillatorPanel::editorTick()
{
    const auto engine = ctx.params().getChoice<OscEngine> (p (OscField::Engine));
    const auto wave = ctx.params().getChoice<AnalogWave> (p (OscField::Wave));
    if (engine != shownEngine || wave != shownWave)
    {
        shownEngine = engine;
        shownWave = wave;
        resized();
    }
}

void OscillatorPanel::resized()
{
    for (auto& [field, k] : knobs)
        k->setVisible (false);
    for (auto* comp : std::initializer_list<juce::Component*> { &waveChoice, &tableChoice, &noiseChoice, &algoChoice, &routeChoice, &fmSourceChoice,
                                                                &op1RatioChoice, &op2RatioChoice, &syncToggle, &fmCaption, &unisonCaption, &operatorCaption })
        comp->setVisible (false);

    auto bounds = getLocalBounds();
    auto strip = bounds.removeFromTop (panelTitleHeight).reduced (8, 3);
    strip.removeFromLeft (46);
    onToggle.setBounds (strip.removeFromLeft (30));
    engineChoice.setBounds (strip.removeFromRight (compact ? 98 : 108));
    if (! compact)
    {
        strip.removeFromRight (6);
        place (routeChoice, strip.removeFromRight (74));
    }

    auto r = bounds.reduced (8, 4);
    display.setBounds (r.removeFromTop (compact ? 76 : 104));
    r.removeFromTop (5);

    auto selector = r.removeFromTop (22);
    switch (shownEngine)
    {
        case OscEngine::Analog:    place (waveChoice, selector); break;
        case OscEngine::Wavetable: place (tableChoice, selector); break;
        case OscEngine::Noise:     place (noiseChoice, selector); break;
        case OscEngine::FM:        place (algoChoice, selector); break;
    }
    r.removeFromTop (6);

    const int knobHeight = compact ? 56 : 60;
    auto row = [&r, knobHeight] { auto rr = r.removeFromTop (knobHeight); r.removeFromTop (2); return rr; };
    auto show = [this] (std::initializer_list<OscField> fields, juce::Rectangle<int> area, int maxWidth = 60)
    {
        std::vector<juce::Component*> comps;
        for (auto f : fields)
        {
            knobs[f]->setVisible (true);
            comps.push_back (knobs[f]);
        }
        const int n = (int) comps.size();
        const int w = std::min (maxWidth, area.getWidth() / std::max (1, n));
        int x = area.getX();
        for (auto* c : comps)
        {
            c->setBounds (x, area.getY(), w, area.getHeight());
            x += w;
        }
    };

    const OscField shape = shownEngine == OscEngine::Wavetable ? OscField::WtPos
                         : shownEngine == OscEngine::FM ? OscField::Op1Amount
                         : OscField::PulseWidth;

    if (compact)
    {
        show ({ OscField::Level, shape, OscField::Semi, OscField::Detune }, row());
        show ({ OscField::Unison, OscField::Pan, OscField::FmAmount, OscField::Fine }, row());
        if (shownEngine == OscEngine::Analog && shownWave != AnalogWave::Pulse)
            knobs[OscField::PulseWidth]->setEnabled (false);
        else
            knobs[OscField::PulseWidth]->setEnabled (true);
        return;
    }

    show ({ OscField::Octave, OscField::Semi, OscField::Fine, OscField::Level, OscField::Pan }, row());

    if (shownEngine == OscEngine::Wavetable)
        show ({ OscField::WtPos }, row());
    else if (shownEngine == OscEngine::Analog)
    {
        show ({ OscField::PulseWidth }, row());
        knobs[OscField::PulseWidth]->setEnabled (shownWave == AnalogWave::Pulse);
    }
    else if (shownEngine == OscEngine::FM)
    {
        auto captionRow = r.removeFromTop (16);
        place (operatorCaption, captionRow);
        auto ratios = r.removeFromTop (22);
        place (op1RatioChoice, ratios.removeFromLeft (ratios.getWidth() / 2 - 3));
        ratios.removeFromLeft (6);
        place (op2RatioChoice, ratios);
        r.removeFromTop (4);
        show ({ OscField::Op1Amount, OscField::Op2Amount, OscField::OpFine, OscField::FmFeedback, OscField::FmEnvAmount, OscField::FmKeyTrack }, row(), 56);
    }

    if (shownEngine != OscEngine::Noise)
    {
        place (unisonCaption, r.removeFromTop (16));
        show ({ OscField::Unison, OscField::Detune, OscField::Spread, OscField::Phase, OscField::PhaseRandom }, row());
    }
    else
    {
        place (unisonCaption, r.removeFromTop (16));
        show ({ OscField::Spread }, row());
    }

    place (fmCaption, r.removeFromTop (16));
    auto modRow = row();
    auto left = modRow.removeFromLeft (100);
    place (fmSourceChoice, left.removeFromTop (22).reduced (0, 0));
    left.removeFromTop (8);
    place (syncToggle, left.removeFromTop (20));
    show ({ OscField::FmAmount, OscField::Ring }, modRow);
}

// =============================================================================================
// FilterPanel
// =============================================================================================
FilterPanel::FilterPanel (EditorContext& c, bool isLarge)
    : Card (c, "Filter"),
      large (isLarge),
      onToggle (c, pid::filter (FilterField::On)),
      typeChoice (c, pid::filter (FilterField::Type)),
      display (c),
      cutoff (c, pid::filter (FilterField::Cutoff), "CUTOFF"),
      resonance (c, pid::filter (FilterField::Resonance), "RESO"),
      drive (c, pid::filter (FilterField::Drive), "DRIVE"),
      envAmount (c, pid::filter (FilterField::EnvAmount), "ENV"),
      keyTrack (c, pid::filter (FilterField::KeyTrack), "KEY TRK"),
      velocity (c, pid::filter (FilterField::Velocity), "VEL"),
      mix (c, pid::filter (FilterField::Mix), "MIX")
{
    titleColour = colours::text;
    for (auto* comp : std::initializer_list<juce::Component*> { &onToggle, &typeChoice, &display, &cutoff, &resonance, &drive,
                                                                &envAmount, &keyTrack, &velocity, &mix })
        addAndMakeVisible (comp);
}

void FilterPanel::resized()
{
    auto bounds = getLocalBounds();
    auto strip = bounds.removeFromTop (panelTitleHeight).reduced (8, 3);
    strip.removeFromLeft (52);
    onToggle.setBounds (strip.removeFromLeft (30));
    typeChoice.setBounds (strip.removeFromRight (large ? 140 : 110));

    auto r = bounds.reduced (8, 4);
    display.setBounds (r.removeFromTop (large ? 230 : (r.getHeight() - 132)));
    r.removeFromTop (6);

    if (large)
    {
        layoutRow (r.removeFromTop (74), { &cutoff, &resonance, &drive, &envAmount, &keyTrack, &velocity, &mix }, 84);
    }
    else
    {
        layoutRow (r.removeFromTop (62), { &cutoff, &resonance, &drive, &envAmount }, 70);
        r.removeFromTop (2);
        layoutRow (r.removeFromTop (60), { &keyTrack, &velocity, &mix }, 70);
    }
}

// =============================================================================================
// EnvelopePanel
// =============================================================================================
EnvelopePanel::EnvelopePanel (EditorContext& c, int index, const juce::String& t, juce::Colour colour, bool curves)
    : Card (c, t), envIndex (index), showCurves (curves), display (c, index, colour)
{
    titleColour = colour;
    addAndMakeVisible (display);

    const std::pair<EnvField, const char*> fields[] = {
        { EnvField::Delay, "DELAY" }, { EnvField::Attack, "ATTACK" }, { EnvField::Hold, "HOLD" }, { EnvField::Decay, "DECAY" },
        { EnvField::Sustain, "SUSTAIN" }, { EnvField::Release, "RELEASE" },
        { EnvField::AttackCurve, "A CURVE" }, { EnvField::DecayCurve, "D CURVE" }, { EnvField::ReleaseCurve, "R CURVE" },
    };

    for (const auto& [field, label] : fields)
    {
        if (! showCurves && (field == EnvField::AttackCurve || field == EnvField::DecayCurve || field == EnvField::ReleaseCurve
                             || field == EnvField::Delay || field == EnvField::Hold))
            continue;
        knobs.push_back (std::make_unique<ParamKnob> (c, pid::env (index, field), label, colour));
        addAndMakeVisible (*knobs.back());
    }
}

void EnvelopePanel::resized()
{
    auto r = content();
    const int rows = showCurves ? 2 : 1;
    display.setBounds (r.removeFromTop (r.getHeight() - rows * 62 - 4));
    r.removeFromTop (4);

    if (! showCurves)
    {
        std::vector<juce::Component*> comps;
        for (auto& k : knobs) comps.push_back (k.get());
        const int w = std::min (70, r.getWidth() / (int) comps.size());
        int x = r.getX() + (r.getWidth() - w * (int) comps.size()) / 2;
        for (auto* c : comps) { c->setBounds (x, r.getY(), w, 60); x += w; }
        return;
    }

    auto row1 = r.removeFromTop (60);
    r.removeFromTop (2);
    auto row2 = r.removeFromTop (60);
    layoutRow (row1, { knobs[0].get(), knobs[1].get(), knobs[2].get(), knobs[3].get(), knobs[4].get(), knobs[5].get() }, 66);
    layoutRow (row2, { knobs[6].get(), knobs[7].get(), knobs[8].get() }, 66);
}

} // namespace nedd::ui
