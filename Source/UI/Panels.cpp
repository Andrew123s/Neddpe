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
      sourceChoice (c, pid::osc (index, OscField::SampleSource)),
      syncToggle (c, pid::osc (index, OscField::Sync), "Sync"),
      loopToggle (c, pid::osc (index, OscField::SampleLoop), "Loop"),
      display (c, index)
{
    titleColour = colours::text;
    for (auto* comp : std::initializer_list<juce::Component*> { &onToggle, &engineChoice, &waveChoice, &tableChoice, &noiseChoice, &algoChoice,
                                                                &routeChoice, &fmSourceChoice, &op1RatioChoice, &op2RatioChoice, &syncToggle,
                                                                &loopToggle, &importButton, &clearButton, &sourceCaption, &sourceChoice,
                                                                &display, &fmCaption, &unisonCaption, &operatorCaption, &grainCaption })
        addChildComponent (comp);
    sourceChoice.setTooltip ("Built-in source played when no sample is imported into this oscillator");

    onToggle.setVisible (true);
    engineChoice.setVisible (true);
    display.setVisible (true);

    importButton.setTooltip ("Load an audio file into this oscillator. Wavetable engine: a wavetable file (frames of 2048 samples, "
                             "Serum-style files included). Granular / Sample engines: any WAV, AIFF, FLAC or OGG up to 60 s. "
                             "The audio is saved inside presets and projects.");
    clearButton.setTooltip ("Remove the imported audio from this oscillator (the built-in source is used again).");
    importButton.onClick = [this] { chooseFile(); };
    clearButton.onClick = [this]
    {
        if (shownEngine == OscEngine::Wavetable)
            ctx.processor.removeWavetable (oscIndex);
        else
            ctx.processor.removeSample (oscIndex);
    };
    sourceCaption.setColour (colours::text);

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
    knob (OscField::SampleRoot, "ROOT");
    knob (OscField::GrainSize, "SIZE");
    knob (OscField::GrainDensity, "DENSITY");
    knob (OscField::GrainSpray, "SPRAY");
    knob (OscField::GrainPitchSpray, "PITCH");

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

void OscillatorPanel::chooseFile()
{
    const bool wavetable = shownEngine == OscEngine::Wavetable;
    chooser = std::make_unique<juce::FileChooser> (wavetable ? "Import a wavetable into OSC " + juce::String (oscIndex + 1)
                                                             : "Import a sample into OSC " + juce::String (oscIndex + 1),
                                                   lastDirectory, assets::getAudioFileWildcard());

    juce::Component::SafePointer<OscillatorPanel> safeThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis, wavetable] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;

                              safeThis->lastDirectory = file.getParentDirectory();
                              auto& processor = safeThis->ctx.processor;
                              const auto error = wavetable ? processor.importWavetable (safeThis->oscIndex, file)
                                                           : processor.importSample (safeThis->oscIndex, file);
                              if (error.isNotEmpty())
                                  juce::NativeMessageBox::showAsync (juce::MessageBoxOptions()
                                                                         .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                         .withTitle ("Import failed")
                                                                         .withMessage (error)
                                                                         .withButton ("OK"),
                                                                     nullptr);
                          });
}

void OscillatorPanel::updateSourceCaption()
{
    const auto& slot = ctx.processor.getOscillatorAssets().slots[(size_t) oscIndex];

    if (shownEngine == OscEngine::Wavetable)
    {
        const bool imported = ctx.params().getInt (p (OscField::Table)) == kImportedWavetableChoice;
        clearButton.setEnabled (slot.wavetable != nullptr);
        if (imported)
            sourceCaption.setText (slot.wavetable != nullptr ? slot.wavetable->name + "  (" + juce::String (slot.wavetable->numFrames) + " frames)"
                                                             : "No wavetable imported: using Basic Shapes");
        return;
    }

    clearButton.setEnabled (slot.sample != nullptr);
    if (slot.sample != nullptr)
        sourceCaption.setText (slot.sample->name + "  " + juce::String ((double) slot.sample->length / slot.sample->sampleRate, 1) + " s");
}

void OscillatorPanel::editorTick()
{
    const auto engine = ctx.params().getChoice<OscEngine> (p (OscField::Engine));
    const auto wave = ctx.params().getChoice<AnalogWave> (p (OscField::Wave));
    const bool tableImported = ctx.params().getInt (p (OscField::Table)) == kImportedWavetableChoice;
    const int assetsVersion = ctx.processor.getAssetsVersion();

    if (engine != shownEngine || wave != shownWave || tableImported != shownTableImported)
    {
        shownEngine = engine;
        shownWave = wave;
        shownTableImported = tableImported;
        shownAssetsVersion = assetsVersion;
        resized();
    }
    else if (assetsVersion != shownAssetsVersion)
    {
        shownAssetsVersion = assetsVersion;
        resized();   // a sample may have been imported or removed: caption vs built-in source list
    }
}

void OscillatorPanel::resized()
{
    for (auto& [field, k] : knobs)
        k->setVisible (false);
    for (auto* comp : std::initializer_list<juce::Component*> { &waveChoice, &tableChoice, &noiseChoice, &algoChoice, &routeChoice, &fmSourceChoice,
                                                                &op1RatioChoice, &op2RatioChoice, &syncToggle, &loopToggle, &importButton,
                                                                &clearButton, &sourceCaption, &sourceChoice, &fmCaption, &unisonCaption,
                                                                &operatorCaption, &grainCaption })
        comp->setVisible (false);

    const bool usesSample = shownEngine == OscEngine::Granular || shownEngine == OscEngine::Sample;
    knobs[OscField::WtPos]->setLabel (shownEngine == OscEngine::Granular ? "POSITION" : shownEngine == OscEngine::Sample ? "START" : "WT POS");

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
        case OscEngine::Noise:     place (noiseChoice, selector); break;
        case OscEngine::FM:        place (algoChoice, selector); break;
        case OscEngine::Wavetable:
            if (! compact)
            {
                place (importButton, selector.removeFromRight (58));
                selector.removeFromRight (4);
            }
            place (tableChoice, selector);
            break;
        case OscEngine::Granular:
        case OscEngine::Sample:
        {
            if (! compact)
            {
                place (clearButton, selector.removeFromRight (48));
                selector.removeFromRight (4);
                place (importButton, selector.removeFromRight (58));
                selector.removeFromRight (6);
            }
            const bool imported = ctx.processor.getOscillatorAssets().slots[(size_t) oscIndex].sample != nullptr;
            place (imported ? static_cast<juce::Component&> (sourceCaption) : static_cast<juce::Component&> (sourceChoice), selector);
            break;
        }
    }

    if (! compact && shownEngine == OscEngine::Wavetable && shownTableImported)
    {
        r.removeFromTop (2);
        auto line = r.removeFromTop (18);
        place (clearButton, line.removeFromRight (48));
        line.removeFromRight (4);
        place (sourceCaption, line);
    }
    updateSourceCaption();
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

    const OscField shape = shownEngine == OscEngine::Wavetable || usesSample ? OscField::WtPos
                         : shownEngine == OscEngine::FM ? OscField::Op1Amount
                         : OscField::PulseWidth;

    if (compact)
    {
        const OscField second = shownEngine == OscEngine::Granular ? OscField::GrainSize : OscField::Detune;
        show ({ OscField::Level, shape, OscField::Semi, second }, row());
        show ({ OscField::Unison, OscField::Pan, OscField::FmAmount, OscField::Fine }, row());
        knobs[OscField::PulseWidth]->setEnabled (! (shownEngine == OscEngine::Analog && shownWave != AnalogWave::Pulse));
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
    else if (shownEngine == OscEngine::Granular)
    {
        place (grainCaption, r.removeFromTop (16));
        show ({ OscField::WtPos, OscField::GrainSize, OscField::GrainDensity, OscField::GrainSpray, OscField::GrainPitchSpray }, row());
    }
    else if (shownEngine == OscEngine::Sample)
    {
        auto sampleRow = row();
        place (loopToggle, sampleRow.removeFromRight (64).withSizeKeepingCentre (64, 20));
        show ({ OscField::WtPos, OscField::SampleRoot }, sampleRow);
    }

    if (shownEngine == OscEngine::Noise)
    {
        place (unisonCaption, r.removeFromTop (16));
        show ({ OscField::Spread }, row());
    }
    else if (shownEngine == OscEngine::Granular)
    {
        place (unisonCaption, r.removeFromTop (16));
        show ({ OscField::SampleRoot, OscField::Spread }, row());
    }
    else
    {
        place (unisonCaption, r.removeFromTop (16));
        show ({ OscField::Unison, OscField::Detune, OscField::Spread, OscField::Phase, OscField::PhaseRandom }, row());
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
