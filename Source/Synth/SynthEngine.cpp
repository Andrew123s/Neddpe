#include "SynthEngine.h"

namespace nedd
{
SynthEngine::SynthEngine (EngineShared& s) : shared (s)
{
    ctx.wavetables = &WavetableBank::getInstance();
}

int SynthEngine::controlBlockSizeFor (Quality q) noexcept
{
    switch (q)
    {
        case Quality::Eco:    return 64;
        case Quality::Normal: return 32;
        case Quality::High:   return 16;
        case Quality::Ultra:  return 8;
    }
    return 32;
}

void SynthEngine::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    maxBlock = std::max (1, maxBlockSize);

    mainBus.setSize (2, maxBlock);
    delayBus.setSize (2, maxBlock);
    reverbBus.setSize (2, maxBlock);

    voiceManager.prepare ((float) sampleRate);
    limiter.prepare ((float) sampleRate);
    masterGain.reset (sampleRate, 0.03);

    ctx.sampleRate = (float) sampleRate;
    for (size_t l = 0; l < globalLfos.size(); ++l)
        globalLfos[l].reset (0.0f, 0xABCDu + (uint32_t) l);

    reset();
}

void SynthEngine::reset()
{
    voiceManager.reset();
    mpeInput.reset();
    limiter.reset();
    globalDest.fill (0.0f);
    ctx.globalSources.fill (0.0f);
}

void SynthEngine::beginBlock (const ParamSnapshot& p, const TransportInfo& transport) noexcept
{
    currentParams = &p;
    routing.build (p);

    const auto* tuning = shared.tuning.acquire();
    const auto* shapes = shared.lfoShapes.acquire();
    const auto* morphTarget = shared.morphTarget.acquire();

    ctx.params = &p;
    ctx.routing = &routing;
    ctx.tuning = tuning != nullptr ? tuning : &defaultTuning;
    ctx.lfoShapes = shapes != nullptr ? shapes : &defaultLfoShapes;
    ctx.controlBlockSize = controlBlockSizeFor (p.getChoice<Quality> (pid::global (GlobalField::Quality)));
    ctx.customTuning = p.getBool (pid::global (GlobalField::CustomTuning));

    const int scaleType = p.getInt (pid::global (GlobalField::ScaleType));
    ctx.scaleActive = scaleType != 0;
    ctx.scaleRoot = p.getInt (pid::global (GlobalField::ScaleRoot));
    ctx.scaleMask = Scales::mask (scaleType, ctx.tuning->userScale);

    // Morph: A = live patch, B = stored target.
    morph.enabled = p.getBool (pid::global (GlobalField::MorphOn)) && morphTarget != nullptr && morphTarget->valid;
    morph.position = p[pid::global (GlobalField::MorphPosition)];
    if (morph.enabled)
    {
        morph.targetPlain = morphTarget->plain;
        morph.targetNormalised = morphTarget->normalised;
        for (int index : getVoiceMorphParams())
            morph.liveNormalised[(size_t) index] = getParamDef (index).range.convertTo0to1 (p[index]);
        for (int index : getGlobalMorphParams())
            morph.liveNormalised[(size_t) index] = getParamDef (index).range.convertTo0to1 (p[index]);
    }
    ctx.morph = &morph;

    // Tempo-dependent rates.
    for (int l = 0; l < kNumLfos; ++l)
    {
        const bool sync = p.getBool (pid::lfo (l, LfoField::Sync));
        const double beats = divisionToBeats (p.getInt (pid::lfo (l, LfoField::Division)));
        ctx.lfoRateHz[(size_t) l] = sync ? (float) (transport.bpm / 60.0 / beats) : p[pid::lfo (l, LfoField::Rate)];
    }
    ctx.sampleHoldRateHz = (float) (transport.bpm / 60.0 / divisionToBeats (p.getInt (pid::global (GlobalField::SampleHoldDivision))));

    for (int m = 0; m < kNumMacros; ++m)
        ctx.globalSources[(size_t) ModSource::Macro1 + (size_t) m] = p[pid::macro (m)];

    mpeInput.setConfig (p.getChoice<MpeMode> (pid::global (GlobalField::MpeMode)),
                        (float) p.getInt (pid::global (GlobalField::MpeBendRange)),
                        (float) p.getInt (pid::global (GlobalField::MasterBendRange)));

    voiceManager.setConfig (p.getChoice<VoiceMode> (pid::global (GlobalField::VoiceMode)),
                            p.getInt (pid::global (GlobalField::Polyphony)),
                            p.getChoice<GlideMode> (pid::global (GlobalField::GlideMode)));

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (p[pid::global (GlobalField::MasterVolume)], -60.0f));
}

void SynthEngine::collectInput (const juce::MidiBuffer& midi, int chunkStart, int numSamples, bool includeUi) noexcept
{
    inputEvents.clear();
    auto& t = shared.telemetry;

    auto handle = [&] (const juce::MidiMessage& m, int offset)
    {
        if (m.isNoteOn())             t.lastVelocity.store (m.getFloatVelocity(), std::memory_order_relaxed);
        else if (m.isPitchWheel())    t.lastPitchBend.store ((float) (m.getPitchWheelValue() - 8192) / 8192.0f, std::memory_order_relaxed);
        else if (m.isChannelPressure()) t.lastPressure.store ((float) m.getChannelPressureValue() / 127.0f, std::memory_order_relaxed);
        else if (m.isAftertouch())    t.lastPressure.store ((float) m.getAfterTouchValue() / 127.0f, std::memory_order_relaxed);
        else if (m.isController() && m.getControllerNumber() == 74)
            t.lastSlide.store ((float) m.getControllerValue() / 127.0f, std::memory_order_relaxed);

        t.midiActivity.fetch_add (1, std::memory_order_relaxed);
        mpeInput.process (m, juce::jlimit (0, numSamples - 1, offset), inputEvents);
    };

    UiMidiMessage ui;
    while (includeUi && shared.uiMidi.pop (ui))
        handle (juce::MidiMessage (ui.bytes, ui.size), 0);

    for (const auto metadata : midi)
        if (metadata.samplePosition >= chunkStart && metadata.samplePosition < chunkStart + numSamples)
            handle (metadata.getMessage(), metadata.samplePosition - chunkStart);

    inputEvents.sortByTime();
}

void SynthEngine::applyEvent (const NoteEvent& e) noexcept
{
    switch (e.type)
    {
        case NoteEvent::Type::NoteOn:
        {
            const int sounding = ctx.scaleActive ? Scales::snapNote (e.noteNumber, ctx.scaleMask, ctx.scaleRoot) : e.noteNumber;
            voiceManager.noteOn (e, sounding, ctx);
            break;
        }
        case NoteEvent::Type::NoteOff:     voiceManager.noteOff (e); break;
        case NoteEvent::Type::Expression:  voiceManager.expression (e); break;
        case NoteEvent::Type::AllNotesOff: voiceManager.allNotesOff (false); break;
        case NoteEvent::Type::Global:
            switch (e.control)
            {
                case GlobalControl::ModWheel:   ctx.globalSources[(size_t) ModSource::ModWheel] = e.value; break;
                case GlobalControl::PitchBend:  ctx.globalSources[(size_t) ModSource::PitchBend] = e.value; break;
                case GlobalControl::Aftertouch: ctx.globalSources[(size_t) ModSource::Aftertouch] = e.value; break;
                case GlobalControl::ControlChange:
                    shared.controllers.push ({ e.channel, e.ccNumber, e.value });
                    break;
            }
            break;
    }
}

void SynthEngine::renderVoices (int start, int num) noexcept
{
    if (num <= 0)
        return;

    VoiceBuses buses;
    buses.mainL = mainBus.getWritePointer (0);
    buses.mainR = mainBus.getWritePointer (1);
    buses.delayL = delayBus.getWritePointer (0);
    buses.delayR = delayBus.getWritePointer (1);
    buses.reverbL = reverbBus.getWritePointer (0);
    buses.reverbR = reverbBus.getWritePointer (1);

    voiceManager.render (ctx, buses, start, num);
}

void SynthEngine::evaluateGlobalModulation (const TransportInfo& transport, int numSamples) noexcept
{
    const auto& p = *currentParams;

    // Global LFO instances: host-locked when synced and the host is playing, free-running otherwise.
    for (int l = 0; l < kNumLfos; ++l)
    {
        dsp::Lfo::Settings s;
        s.shape = p.getChoice<LfoShape> (pid::lfo (l, LfoField::Shape));
        s.phaseOffset = p[pid::lfo (l, LfoField::Phase)];
        s.customTable = ctx.lfoShapes->tables[(size_t) l].data();

        auto& lfo = globalLfos[(size_t) l];
        if (p.getBool (pid::lfo (l, LfoField::Sync)) && transport.hostPlaying)
        {
            const double beats = divisionToBeats (p.getInt (pid::lfo (l, LfoField::Division)));
            lfo.setPhase ((float) std::fmod (transport.ppqAtBlockStart / beats, 1.0));
            globalLfoValues[(size_t) l] = lfo.evaluate (s);
        }
        else
        {
            globalLfoValues[(size_t) l] = lfo.advance (s, ctx.lfoRateHz[(size_t) l], numSamples, (float) sampleRate);
        }

        ctx.globalLfoPhase[(size_t) l] = lfo.getPhase();
        globalLfoValues[(size_t) l] *= p[pid::lfo (l, LfoField::Amount)];
    }

    // Global destinations read per-note sources from the most recently played voice.
    std::array<float, (size_t) kNumModSources> sources = ctx.globalSources;
    const int focus = voiceManager.getFocusVoiceIndex();
    if (focus >= 0)
    {
        const auto& voiceSources = voiceManager.getVoice (focus).getSources();
        for (int s = 0; s < kNumModSources; ++s)
            if (getModSourceInfo ((ModSource) s).perVoice)
                sources[(size_t) s] = voiceSources[(size_t) s];
    }
    else
    {
        for (int l = 0; l < kNumLfos; ++l)
            sources[(size_t) ModSource::Lfo1 + (size_t) l] = globalLfoValues[(size_t) l];
    }

    globalSlots.fill (0.0f);
    evaluateModMatrix (routing, sources.data(), globalDest.data(), ModScope::Global, globalSlots.data());

    auto& t = shared.telemetry;
    for (int s = 0; s < kNumModSources; ++s)
        t.sourceValue[(size_t) s].store (sources[(size_t) s], std::memory_order_relaxed);
}

void SynthEngine::writeTelemetry() noexcept
{
    auto& t = shared.telemetry;
    int active = 0;

    for (int i = 0; i < kPhysicalVoices; ++i)
    {
        const auto& v = voiceManager.getVoice (i);
        v.writeTelemetry (t.voices[(size_t) i]);
        if (v.isActive())
            ++active;
    }

    t.activeVoices.store (active, std::memory_order_relaxed);
    const int focus = voiceManager.getFocusVoiceIndex();
    t.focusVoice.store (focus, std::memory_order_relaxed);

    // Per-voice destinations and slots come from the focus voice, global ones from the global pass.
    const auto* voiceDest = focus >= 0 ? &voiceManager.getVoice (focus).getDestMods() : nullptr;
    const auto* voiceSlots = focus >= 0 ? &voiceManager.getVoice (focus).getSlotContributions() : nullptr;

    for (int d = 0; d < kNumModDests; ++d)
    {
        const bool global = getModDestInfo ((ModDest) d).scope == ModScope::Global;
        const float value = global ? globalDest[(size_t) d] : (voiceDest != nullptr ? (*voiceDest)[(size_t) d] : 0.0f);
        t.destModulation[(size_t) d].store (value, std::memory_order_relaxed);
    }

    for (int s = 0; s < kNumModSlots; ++s)
    {
        float value = globalSlots[(size_t) s];
        if (voiceSlots != nullptr && std::abs ((*voiceSlots)[(size_t) s]) > std::abs (value))
            value = (*voiceSlots)[(size_t) s];
        t.slotContribution[(size_t) s].store (value, std::memory_order_relaxed);
    }
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const ParamSnapshot& params,
                           const TransportInfo& transport) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    // Hosts may exceed the block size announced in prepareToPlay: render in chunks.
    for (int start = 0; start < total; start += maxBlock)
    {
        TransportInfo chunkTransport = transport;
        chunkTransport.numSamples = std::min (maxBlock, total - start);
        chunkTransport.ppqAtBlockStart = transport.ppqAt (start);
        processChunk (buffer, start, midi, params, chunkTransport);
    }

    midi.clear();
    writeTelemetry();
}

void SynthEngine::processChunk (juce::AudioBuffer<float>& buffer, int bufferStart, const juce::MidiBuffer& midi,
                                const ParamSnapshot& params, const TransportInfo& transport) noexcept
{
    const int numSamples = transport.numSamples;
    if (numSamples <= 0)
        return;

    beginBlock (params, transport);

    mainBus.clear (0, numSamples);
    delayBus.clear (0, numSamples);
    reverbBus.clear (0, numSamples);

    collectInput (midi, bufferStart, numSamples, bufferStart == 0);

    voiceEvents.clear();
    for (const auto& e : inputEvents)
        voiceEvents.add (e);
    voiceEvents.sortByTime();

    evaluateGlobalModulation (transport, numSamples);

    int position = 0;
    for (const auto& e : voiceEvents)
    {
        const int at = juce::jlimit (0, numSamples, e.sampleOffset);
        renderVoices (position, at - position);
        position = at;
        applyEvent (e);
    }
    renderVoices (position, numSamples - position);
    voiceManager.advanceSampleCounter (numSamples);

    // Master volume and output protection.
    auto* l = mainBus.getWritePointer (0);
    auto* r = mainBus.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = masterGain.getNextValue();
        l[i] *= g;
        r[i] *= g;
    }

    if (params.getBool (pid::fx (FxField::LimiterOn)))
        limiter.process (l, r, numSamples);

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        peakL = std::max (peakL, std::abs (l[i]));
        peakR = std::max (peakR, std::abs (r[i]));
    }
    auto& t = shared.telemetry;
    t.peakLeft.store (std::max (peakL, t.peakLeft.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    t.peakRight.store (std::max (peakR, t.peakRight.load (std::memory_order_relaxed)), std::memory_order_relaxed);

    const int outChannels = buffer.getNumChannels();
    if (outChannels >= 2)
    {
        buffer.copyFrom (0, bufferStart, mainBus, 0, 0, numSamples);
        buffer.copyFrom (1, bufferStart, mainBus, 1, 0, numSamples);
        for (int c = 2; c < outChannels; ++c)
            buffer.clear (c, bufferStart, numSamples);
    }
    else if (outChannels == 1)
    {
        buffer.copyFrom (0, bufferStart, mainBus.getReadPointer (0), numSamples, 0.5f);
        buffer.addFrom (0, bufferStart, mainBus, 1, 0, numSamples, 0.5f);
    }
}

} // namespace nedd
