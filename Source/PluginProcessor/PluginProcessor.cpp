#include "PluginProcessor.h"
#include "PluginEditor/PluginEditor.h"

namespace nedd
{
namespace ids
{
    const juce::Identifier root { "NeddPE" };
    const juce::Identifier version { "version" };
    const juce::Identifier preset { "preset" };
    const juce::Identifier params { "PARAMS" };
    const juce::Identifier param { "PARAM" };
    const juce::Identifier id { "id" };
    const juce::Identifier value { "value" };
    const juce::Identifier macros { "Macros" };
    const juce::Identifier macro { "Macro" };
    const juce::Identifier index { "index" };
    const juce::Identifier name { "name" };
    const juce::Identifier lfoShapes { "LfoShapes" };
    const juce::Identifier lfo { "Lfo" };
    const juce::Identifier points { "points" };
    const juce::Identifier morphB { "MorphB" };
    const juce::Identifier midiMap { "MidiMap" };
    const juce::Identifier map { "Map" };
    const juce::Identifier cc { "cc" };
    const juce::Identifier tuningTree { "Tuning" };
    const juce::Identifier sequencer { "Sequencer" };
    const juce::Identifier arpPatternTree { "ArpPattern" };
    const juce::Identifier clipTree { "Clip" };
} // namespace ids

namespace
{
    /** Undo step that swaps a whole structured value (pattern, clip) between two states. */
    template <typename T>
    class ValueSwapAction : public juce::UndoableAction
    {
    public:
        ValueSwapAction (std::function<void (const T&)> applyFn, T beforeValue, T afterValue)
            : apply (std::move (applyFn)), before (std::move (beforeValue)), after (std::move (afterValue)) {}

        bool perform() override { apply (after); return true; }
        bool undo() override { apply (before); return true; }
        int getSizeInUnits() override { return (int) sizeof (T); }

    private:
        std::function<void (const T&)> apply;
        T before, after;
    };
} // namespace

NeddPEAudioProcessor::NeddPEAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, &undoManager, ids::params, createParameterLayout())
{
    for (const auto& d : getParamDefs())
    {
        parameters[(size_t) d.index] = state.getParameter (d.id);
        jassert (parameters[(size_t) d.index] != nullptr);
    }

    reader.attach (state);
    snapshot.setToDefaults();
    ccToParam.fill (-1);

    publishLfoShapes();
    publishTuning();
    publishPattern();
    publishArpPattern();
    publishClip();
    startTimerHz (30);
}

NeddPEAudioProcessor::~NeddPEAudioProcessor()
{
    stopTimer();
}

void NeddPEAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, std::max (samplesPerBlock, 32));
    setLatencySamples (0);
    isPrepared = true;
}

void NeddPEAudioProcessor::releaseResources() {}

bool NeddPEAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

TransportInfo NeddPEAudioProcessor::readTransport (int numSamples)
{
    TransportInfo t;
    t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    t.numSamples = numSamples;

    juce::Optional<double> hostPpq;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                t.bpm = *bpm;
            t.hostPlaying = position->getIsPlaying();
            hostPpq = position->getPpqPosition();
            t.hostTransportAvailable = hostPpq.hasValue();
        }
    }

    t.bpm = juce::jlimit (20.0, 999.0, t.bpm);
    t.beatsPerSample = t.bpm / 60.0 / t.sampleRate;

    if (t.hostPlaying && hostPpq.hasValue())
        t.ppqAtBlockStart = *hostPpq;
    else
        t.ppqAtBlockStart = internalPpq;

    internalPpq = t.ppqAtBlockStart + t.beatsPerSample * numSamples;
    return t;
}

void NeddPEAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const auto startTicks = juce::Time::getHighResolutionTicks();

    reader.read (snapshot);
    const auto transport = readTransport (buffer.getNumSamples());
    engine.process (buffer, midi, snapshot, transport);

    // Fraction of the real-time budget used by this block, smoothed for display.
    const double elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double budget = buffer.getNumSamples() / std::max (1.0, getSampleRate());
    cpuLoad += 0.1f * ((float) (elapsed / budget) - cpuLoad);
    shared.telemetry.cpuLoad.store (cpuLoad, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* NeddPEAudioProcessor::createEditor()
{
    return new NeddPEEditor (*this);
}

// ---------------------------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------------------------
void NeddPEAudioProcessor::setParameterPlain (int index, float plainValue)
{
    if (auto* p = parameters[(size_t) index])
    {
        const float normalised = p->convertTo0to1 (plainValue);
        if (std::abs (p->getValue() - normalised) > 1.0e-7f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    }
}

void NeddPEAudioProcessor::resetToInitPatch()
{
    for (const auto& d : getParamDefs())
        setParameterPlain (d.index, d.defaultValue);

    macroNames = { "MOVEMENT", "TONE", "SPACE", "DRIVE" };
    lfoShapes = dsp::LfoCustomShapes();
    publishLfoShapes();
    clearMorphTarget();
    setPattern (SequencerPattern());
    setArpPattern (ArpPattern());
    currentPresetName = "Init";
}

// ---------------------------------------------------------------------------------------------
// Structured data
// ---------------------------------------------------------------------------------------------
void NeddPEAudioProcessor::setMacroName (int index, const juce::String& name)
{
    macroNames[(size_t) juce::jlimit (0, kNumMacros - 1, index)] = name.substring (0, 24).toUpperCase();
}

void NeddPEAudioProcessor::setTuning (const TuningData& newTuning)
{
    tuning = newTuning;
    publishTuning();
}

void NeddPEAudioProcessor::publishTuning()
{
    shared.tuning.publish (std::make_unique<TuningData> (tuning));
}

void NeddPEAudioProcessor::setLfoShapes (const dsp::LfoCustomShapes& shapes)
{
    lfoShapes = shapes;
    lfoShapes.rebuildTables();
    publishLfoShapes();
}

void NeddPEAudioProcessor::publishLfoShapes()
{
    shared.lfoShapes.publish (std::make_unique<dsp::LfoCustomShapes> (lfoShapes));
}

void NeddPEAudioProcessor::captureMorphTarget()
{
    ParamSnapshot current;
    reader.read (current);
    setMorphTarget (current);
}

void NeddPEAudioProcessor::setMorphTarget (const ParamSnapshot& plain)
{
    morphTarget = std::make_unique<ParamSnapshot> (plain);
    publishMorphTarget();
}

void NeddPEAudioProcessor::clearMorphTarget()
{
    morphTarget.reset();
    publishMorphTarget();
}

void NeddPEAudioProcessor::swapMorphAB()
{
    if (morphTarget == nullptr)
        return;

    ParamSnapshot current;
    reader.read (current);
    const ParamSnapshot target = *morphTarget;

    undoManager.beginNewTransaction ("Swap Morph A/B");
    for (const auto& d : getParamDefs())
        if (d.morphable)
            setParameterPlain (d.index, target[d.index]);

    setMorphTarget (current);
}

void NeddPEAudioProcessor::publishMorphTarget()
{
    if (morphTarget == nullptr)
    {
        auto empty = std::make_unique<MorphTarget>();
        empty->valid = false;
        shared.morphTarget.publish (std::move (empty));
        return;
    }

    auto target = std::make_unique<MorphTarget>();
    target->plain = *morphTarget;
    for (const auto& d : getParamDefs())
        target->normalised[(size_t) d.index] = d.range.convertTo0to1 (juce::jlimit (d.range.start, d.range.end, (*morphTarget)[d.index]));
    shared.morphTarget.publish (std::move (target));
}

// ---------------------------------------------------------------------------------------------
// Sequencer data and clip
// ---------------------------------------------------------------------------------------------
void NeddPEAudioProcessor::publishPattern() { shared.pattern.publish (std::make_unique<SequencerPattern> (pattern)); }
void NeddPEAudioProcessor::publishArpPattern() { shared.arpPattern.publish (std::make_unique<ArpPattern> (arpPattern)); }
void NeddPEAudioProcessor::publishClip() { shared.clip.publish (std::make_unique<NoteClip> (clip)); }

void NeddPEAudioProcessor::setPattern (const SequencerPattern& newPattern, const juce::String& undoName)
{
    auto apply = [this] (const SequencerPattern& p) { pattern = p; ++patternVersion; publishPattern(); };
    if (undoName.isNotEmpty() && ! (newPattern == pattern))
    {
        undoManager.beginNewTransaction (undoName);
        undoManager.perform (new ValueSwapAction<SequencerPattern> (apply, pattern, newPattern));
    }
    else
        apply (newPattern);
}

void NeddPEAudioProcessor::setArpPattern (const ArpPattern& newPattern, const juce::String& undoName)
{
    auto apply = [this] (const ArpPattern& p) { arpPattern = p; ++patternVersion; publishArpPattern(); };
    if (undoName.isNotEmpty() && ! (newPattern == arpPattern))
    {
        undoManager.beginNewTransaction (undoName);
        undoManager.perform (new ValueSwapAction<ArpPattern> (apply, arpPattern, newPattern));
    }
    else
        apply (newPattern);
}

void NeddPEAudioProcessor::setClip (const NoteClip& newClip, const juce::String& undoName)
{
    auto apply = [this] (const NoteClip& c) { clip = c; ++clipVersion; publishClip(); };
    if (undoName.isNotEmpty())
    {
        undoManager.beginNewTransaction (undoName);
        undoManager.perform (new ValueSwapAction<NoteClip> (apply, clip, newClip));
    }
    else
        apply (newClip);
}

void NeddPEAudioProcessor::clipPlay()
{
    if (recorder.isActive())
        return;
    shared.clipCommand.store ((int) ClipCommand::Play);
}

void NeddPEAudioProcessor::clipStop()
{
    if (recorder.isActive())
        recordStopPending = true;
    shared.clipCommand.store ((int) ClipCommand::Stop);
}

void NeddPEAudioProcessor::clipRecord (bool overdub)
{
    if (recorder.isActive())
        return;
    RecordedEvent stale;
    while (shared.recorded.pop (stale)) {}
    recorder.begin (clip, overdub, shared.clipLoop.load());
    recordStopPending = false;
    shared.clipCommand.store ((int) ClipCommand::Record);
}

void NeddPEAudioProcessor::serviceRecorder()
{
    RecordedEvent e;
    int processed = 0;
    while (processed++ < 8192 && shared.recorded.pop (e))
        recorder.consume (e);

    // Finalise once the audio thread has actually stopped recording, so no events are lost.
    const bool audioStopped = ! shared.telemetry.clipRecording.load() || ! isPrepared;
    if (recorder.isActive() && recordStopPending && audioStopped)
    {
        while (shared.recorded.pop (e))
            recorder.consume (e);
        setClip (recorder.finish (shared.telemetry.clipPosition.load()), "Record performance");
        recordStopPending = false;
    }
}

// ---------------------------------------------------------------------------------------------
// MIDI learn
// ---------------------------------------------------------------------------------------------
void NeddPEAudioProcessor::clearMidiMapping (int paramIndex)
{
    for (auto& p : ccToParam)
        if (p == paramIndex)
            p = -1;
    if (onMidiLearnChanged) onMidiLearnChanged();
}

int NeddPEAudioProcessor::getMidiMappingFor (int paramIndex) const
{
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParam[(size_t) cc] == paramIndex)
            return cc;
    return -1;
}

void NeddPEAudioProcessor::timerCallback()
{
    serviceRecorder();
    shared.pattern.collectGarbage();
    shared.arpPattern.collectGarbage();
    shared.clip.collectGarbage();
    shared.tuning.collectGarbage();
    shared.lfoShapes.collectGarbage();
    shared.morphTarget.collectGarbage();

    ControllerEvent e;
    int processed = 0;
    while (processed++ < 512 && shared.controllers.pop (e))
    {
        const int cc = juce::jlimit (0, 127, e.cc);

        if (learnTarget >= 0)
        {
            for (auto& p : ccToParam)
                if (p == learnTarget) p = -1;
            ccToParam[(size_t) cc] = learnTarget;
            learnTarget = -1;
            if (onMidiLearnChanged) onMidiLearnChanged();
        }

        const int target = ccToParam[(size_t) cc];
        if (target >= 0)
            if (auto* p = parameters[(size_t) target])
                p->setValueNotifyingHost (e.value);
    }
}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------
juce::ValueTree NeddPEAudioProcessor::createStateTree (bool includePerformanceData) const
{
    juce::ValueTree root (ids::root);
    root.setProperty (ids::version, kStateVersion, nullptr);
    root.setProperty (ids::preset, currentPresetName, nullptr);

    juce::ValueTree params (ids::params);
    for (const auto& d : getParamDefs())
    {
        juce::ValueTree p (ids::param);
        p.setProperty (ids::id, d.id, nullptr);
        p.setProperty (ids::value, parameters[(size_t) d.index]->convertFrom0to1 (parameters[(size_t) d.index]->getValue()), nullptr);
        params.appendChild (p, nullptr);
    }
    root.appendChild (params, nullptr);

    juce::ValueTree macros (ids::macros);
    for (int m = 0; m < kNumMacros; ++m)
    {
        juce::ValueTree node (ids::macro);
        node.setProperty (ids::index, m, nullptr);
        node.setProperty (ids::name, macroNames[(size_t) m], nullptr);
        macros.appendChild (node, nullptr);
    }
    root.appendChild (macros, nullptr);

    juce::ValueTree shapes (ids::lfoShapes);
    for (int l = 0; l < kNumLfos; ++l)
    {
        juce::StringArray pts;
        for (const auto& pt : lfoShapes.points[(size_t) l])
            pts.add (juce::String (pt.x, 4) + "," + juce::String (pt.y, 4));
        juce::ValueTree node (ids::lfo);
        node.setProperty (ids::index, l, nullptr);
        node.setProperty (ids::points, pts.joinIntoString (";"), nullptr);
        shapes.appendChild (node, nullptr);
    }
    root.appendChild (shapes, nullptr);

    if (morphTarget != nullptr)
    {
        juce::ValueTree morph (ids::morphB);
        for (const auto& d : getParamDefs())
            if (d.morphable)
                morph.setProperty (juce::Identifier (d.id), (*morphTarget)[d.index], nullptr);
        root.appendChild (morph, nullptr);
    }

    root.appendChild (pattern.toValueTree(), nullptr);
    root.appendChild (arpPattern.toValueTree(), nullptr);

    if (includePerformanceData)
    {
        auto clipTree = clip.toValueTree();
        clipTree.setProperty ("loop", shared.clipLoop.load(), nullptr);
        clipTree.setProperty ("sync", shared.clipSyncToHost.load(), nullptr);
        root.appendChild (clipTree, nullptr);
        root.appendChild (tuning.toValueTree(), nullptr);

        juce::ValueTree midiMap (ids::midiMap);
        for (int cc = 0; cc < 128; ++cc)
        {
            if (ccToParam[(size_t) cc] < 0)
                continue;
            juce::ValueTree node (ids::map);
            node.setProperty (ids::cc, cc, nullptr);
            node.setProperty (ids::id, getParamDef (ccToParam[(size_t) cc]).id, nullptr);
            midiMap.appendChild (node, nullptr);
        }
        root.appendChild (midiMap, nullptr);
        root.setProperty ("editorScale", editorScale, nullptr);
    }

    return root;
}

void NeddPEAudioProcessor::applyStateTree (const juce::ValueTree& root, bool includePerformanceData)
{
    if (! root.hasType (ids::root))
        return;

    // Parameters: anything missing from the tree returns to its default, so old presets load cleanly.
    std::vector<float> values ((size_t) pid::count);
    for (const auto& d : getParamDefs())
        values[(size_t) d.index] = d.defaultValue;

    const auto params = root.getChildWithName (ids::params);
    for (const auto& p : params)
    {
        const int index = findParamIndex (p.getProperty (ids::id).toString());
        if (index >= 0)
            values[(size_t) index] = (float) p.getProperty (ids::value);
    }

    for (const auto& d : getParamDefs())
        setParameterPlain (d.index, juce::jlimit (d.range.start, d.range.end, values[(size_t) d.index]));

    macroNames = { "MOVEMENT", "TONE", "SPACE", "DRIVE" };
    for (const auto& node : root.getChildWithName (ids::macros))
    {
        const int m = node.getProperty (ids::index);
        if (m >= 0 && m < kNumMacros)
            macroNames[(size_t) m] = node.getProperty (ids::name).toString();
    }

    lfoShapes = dsp::LfoCustomShapes();
    for (const auto& node : root.getChildWithName (ids::lfoShapes))
    {
        const int l = node.getProperty (ids::index);
        if (l < 0 || l >= kNumLfos)
            continue;
        std::vector<dsp::LfoCustomShapes::Point> pts;
        for (const auto& token : juce::StringArray::fromTokens (node.getProperty (ids::points).toString(), ";", ""))
        {
            const auto x = token.upToFirstOccurrenceOf (",", false, false).getFloatValue();
            const auto y = token.fromFirstOccurrenceOf (",", false, false).getFloatValue();
            pts.push_back ({ juce::jlimit (0.0f, 1.0f, x), juce::jlimit (-1.0f, 1.0f, y) });
        }
        if (pts.size() >= 2 && pts.size() <= (size_t) dsp::LfoCustomShapes::kMaxPoints)
            lfoShapes.points[(size_t) l] = pts;
    }
    lfoShapes.rebuildTables();
    publishLfoShapes();

    const auto morph = root.getChildWithName (ids::morphB);
    if (morph.isValid())
    {
        ParamSnapshot target;
        for (const auto& d : getParamDefs())
            target[d.index] = values[(size_t) d.index];
        for (const auto& d : getParamDefs())
            if (morph.hasProperty (juce::Identifier (d.id)))
                target[d.index] = (float) morph.getProperty (juce::Identifier (d.id));
        setMorphTarget (target);
    }
    else
    {
        clearMorphTarget();
    }

    SequencerPattern loadedPattern;
    loadedPattern.fromValueTree (root.getChildWithName (ids::sequencer));
    setPattern (loadedPattern);
    ArpPattern loadedArp;
    loadedArp.fromValueTree (root.getChildWithName (ids::arpPatternTree));
    setArpPattern (loadedArp);

    if (includePerformanceData)
    {
        const auto clipTree = root.getChildWithName (ids::clipTree);
        NoteClip loadedClip;
        loadedClip.fromValueTree (clipTree);
        setClip (loadedClip);
        shared.clipLoop.store ((bool) clipTree.getProperty ("loop", true));
        shared.clipSyncToHost.store ((bool) clipTree.getProperty ("sync", false));

        tuning.fromValueTree (root.getChildWithName (ids::tuningTree));
        publishTuning();

        ccToParam.fill (-1);
        for (const auto& node : root.getChildWithName (ids::midiMap))
        {
            const int cc = node.getProperty (ids::cc);
            const int index = findParamIndex (node.getProperty (ids::id).toString());
            if (cc >= 0 && cc < 128 && index >= 0)
                ccToParam[(size_t) cc] = index;
        }
        if (onMidiLearnChanged) onMidiLearnChanged();
        setEditorScale ((float) root.getProperty ("editorScale", 1.0f));
    }

    currentPresetName = root.getProperty (ids::preset, "Init").toString();
}

void NeddPEAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree (true).createXml())
        copyXmlToBinary (*xml, destData);
}

void NeddPEAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        applyStateTree (juce::ValueTree::fromXml (*xml), true);
        undoManager.clearUndoHistory();
    }
}

} // namespace nedd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nedd::NeddPEAudioProcessor();
}
