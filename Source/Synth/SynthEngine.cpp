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
    effects.prepare ((float) sampleRate, maxBlock);
    midiOut.ensureSize (32768);
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
    effects.reset();
    sequencer.reset();
    arpeggiator.reset();
    clipPlayer.reset();
    midiOutput.reset();
    midiOut.clear();
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
    const auto* assets = shared.assets.acquire();

    // Voices keep pointers into the imported content between control updates; once it has
    // been replaced, the old content may be freed, so every voice refreshes before rendering.
    if (assets != currentAssets)
    {
        currentAssets = assets;
        voiceManager.forceControlUpdate();
    }
    ctx.assets = assets;

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

void SynthEngine::handleClipCommands() noexcept
{
    const auto command = (ClipCommand) shared.clipCommand.exchange ((int) ClipCommand::None);
    switch (command)
    {
        case ClipCommand::Play:
            if (! clipTransport.playing)
                clipTransport.position = 0.0;
            clipTransport.playing = true;
            clipTransport.recording = false;
            break;
        case ClipCommand::Stop:
            clipTransport.playing = false;
            clipTransport.recording = false;
            break;
        case ClipCommand::Record:
            if (! clipTransport.playing)
                clipTransport.position = 0.0;
            clipTransport.playing = true;
            clipTransport.recording = true;
            break;
        case ClipCommand::StopRecord:
            clipTransport.recording = false;   // keep playing what was recorded
            break;
        case ClipCommand::None:
            break;
    }
    clipTransport.loop = shared.clipLoop.load (std::memory_order_relaxed);
    clipTransport.syncToHost = shared.clipSyncToHost.load (std::memory_order_relaxed);
}

void SynthEngine::recordLiveEvents (const TransportInfo& transport) noexcept
{
    if (! clipTransport.recording)
        return;

    const auto* clip = shared.clip.current();
    const bool looping = clipTransport.loop && clip != nullptr && ! clip->notes.empty();
    const double length = clip != nullptr ? clip->lengthBeats : 16.0;

    for (const auto& e : inputEvents)
    {
        if (e.origin != NoteOrigin::Live)
            continue;

        RecordedEvent r;
        r.noteId = e.noteId;
        r.noteNumber = e.noteNumber;
        r.value = e.value;
        r.beat = clipTransport.position + transport.beatsPerSample * e.sampleOffset;
        if (looping)
            r.beat = std::fmod (r.beat, length);

        switch (e.type)
        {
            case NoteEvent::Type::NoteOn:
                r.type = RecordedEvent::NoteOn;
                r.pitch = e.pitch;
                r.pressure = e.pressure;
                r.slide = e.slide;
                break;
            case NoteEvent::Type::NoteOff:    r.type = RecordedEvent::NoteOff; break;
            case NoteEvent::Type::Expression: r.type = RecordedEvent::Expression; r.dim = (uint8_t) e.dim; break;
            case NoteEvent::Type::Global:
            case NoteEvent::Type::AllNotesOff:
                continue;
        }
        shared.recorded.push (r);
    }
}

void SynthEngine::generateNotes (const ParamSnapshot& p, const TransportInfo& transport, int bufferStart) noexcept
{
    const auto* pattern = shared.pattern.acquire();
    const auto* arpPattern = shared.arpPattern.acquire();
    const auto* clip = shared.clip.acquire();

    voiceEvents.clear();

    // Arpeggiator: live notes become held keys; it emits the notes the voices play.
    const bool arpOn = p.getBool (pid::arp (ArpField::On));
    if (arpOn != arpWasOn)
    {
        if (arpOn)
            voiceEvents.add (NoteEvent::allNotesOff (0));   // notes held before the switch would never be released
        else
            arpeggiator.releaseAll (0, voiceEvents);
        arpWasOn = arpOn;
    }

    if (arpOn)
        arpeggiator.process (inputEvents, voiceEvents, p, arpPattern != nullptr ? *arpPattern : defaultArpPattern,
                             globalDest[(size_t) ModDest::ArpGate], globalDest[(size_t) ModDest::ArpProbability],
                             transport, noteIds, generatorRandom);
    else
        for (const auto& e : inputEvents)
            voiceEvents.add (e);

    // Step sequencer
    const bool seqRunning = p.getBool (pid::seq (SeqField::On))
                         && (p.getChoice<SeqClock> (pid::seq (SeqField::Clock)) == SeqClock::FreeRun || transport.hostPlaying);
    sequencer.process (pattern != nullptr ? *pattern : defaultPattern, p, transport, seqRunning, noteIds, generatorRandom, voiceEvents);

    // Performance clip (note editor / recorder)
    clipPlayer.process (clip, clipTransport, transport, noteIds, voiceEvents);

    voiceEvents.sortByTime();

    // MPE MIDI out for generated notes
    const bool midiOutOn = p.getBool (pid::global (GlobalField::MidiOut));
    constexpr float kOutputBendRange = 48.0f;
    if (midiOutOn && ! midiOutWasOn)
        midiOutput.sendConfiguration (midiOut, bufferStart, kOutputBendRange);
    if (midiOutOn)
        midiOutput.process (voiceEvents, midiOut, bufferStart, kOutputBendRange);
    else if (midiOutWasOn)
        midiOutput.allNotesOff (midiOut, bufferStart);
    midiOutWasOn = midiOutOn;
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

const ParamSnapshot& SynthEngine::morphedEffectParams (const ParamSnapshot& p) noexcept
{
    if (! morph.enabled)
        return p;

    // Effects are global: they morph with the base position plus the most recent note's morph modulation.
    float position = morph.position;
    const int focus = voiceManager.getFocusVoiceIndex();
    if (focus >= 0)
        position += voiceManager.getVoice (focus).getDestMods()[(size_t) ModDest::Morph];
    position = dsp::clamp01 (position);

    effectParams = p;
    for (int index : getGlobalMorphParams())
    {
        const auto& def = getParamDef (index);
        if (def.type == ParamType::Float)
            effectParams[index] = def.range.convertFrom0to1 (dsp::lerp (morph.liveNormalised[(size_t) index], morph.targetNormalised[(size_t) index], position));
        else if (position >= 0.5f)
            effectParams[index] = morph.targetPlain[index];
    }
    return effectParams;
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
    t.arpStep.store (arpeggiator.getCurrentStep(), std::memory_order_relaxed);
    t.arpHeld.store (arpeggiator.getHeldCount(), std::memory_order_relaxed);
    t.seqStep.store (sequencer.getCurrentStep(), std::memory_order_relaxed);
    t.clipPosition.store (clipTransport.position, std::memory_order_relaxed);
    t.clipPlaying.store (clipTransport.playing, std::memory_order_relaxed);
    t.clipRecording.store (clipTransport.recording, std::memory_order_relaxed);
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

    // Replace the host's input MIDI with what NeddPE generated (MPE out), without allocating.
    midi.swapWith (midiOut);
    midiOut.clear();
    writeTelemetry();
}

void SynthEngine::processChunk (juce::AudioBuffer<float>& buffer, int bufferStart, const juce::MidiBuffer& midi,
                                const ParamSnapshot& params, const TransportInfo& transport) noexcept
{
    const int numSamples = transport.numSamples;
    if (numSamples <= 0)
        return;

    beginBlock (params, transport);
    shared.telemetry.ppq.store (transport.ppqAtBlockStart, std::memory_order_relaxed);
    shared.telemetry.bpm.store (transport.bpm, std::memory_order_relaxed);
    shared.telemetry.hostPlaying.store (transport.hostPlaying, std::memory_order_relaxed);
    shared.telemetry.hostTransportAvailable.store (transport.hostTransportAvailable, std::memory_order_relaxed);

    mainBus.clear (0, numSamples);
    delayBus.clear (0, numSamples);
    reverbBus.clear (0, numSamples);

    collectInput (midi, bufferStart, numSamples, bufferStart == 0);
    evaluateGlobalModulation (transport, numSamples);
    if (bufferStart == 0)
        handleClipCommands();
    recordLiveEvents (transport);
    generateNotes (params, transport, bufferStart);

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

    effects.process (mainBus.getWritePointer (0), mainBus.getWritePointer (1),
                     delayBus.getReadPointer (0), delayBus.getReadPointer (1),
                     reverbBus.getReadPointer (0), reverbBus.getReadPointer (1), numSamples,
                     morphedEffectParams (params), globalDest, transport, params.getChoice<Quality> (pid::global (GlobalField::Quality)));

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
