#include "PluginProcessor.h"
#include "PluginEditor/PluginEditor.h"

namespace nedd
{
namespace ids
{
    const juce::Identifier root { "NeddPE" };
    const juce::Identifier params { "PARAMS" };
    const juce::Identifier id { "id" };
    const juce::Identifier midiMap { "MidiMap" };
    const juce::Identifier map { "Map" };
    const juce::Identifier cc { "cc" };
    const juce::Identifier tuningTree { "Tuning" };
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
        int getSizeInUnits() override { return 20; }

    private:
        std::function<void (const T&)> apply;
        T before, after;
    };
    /** Undo step for one or more parameter changes. */
    class ParameterChangeAction : public juce::UndoableAction
    {
    public:
        struct Change { int index; float before, after; };

        ParameterChangeAction (NeddPEAudioProcessor& p, std::vector<Change> c) : processor (p), changes (std::move (c)) {}

        bool perform() override { apply (true); return true; }
        bool undo() override { apply (false); return true; }
        int getSizeInUnits() override { return 1 + (int) changes.size() / 8; }

    private:
        void apply (bool forward)
        {
            const NeddPEAudioProcessor::ScopedUndoSuppression suppress (processor);
            for (const auto& ch : changes)
                processor.setParameterPlain (ch.index, forward ? ch.after : ch.before);
        }

        NeddPEAudioProcessor& processor;
        std::vector<Change> changes;
    };

    /** Undo step for a whole sound (preset load, randomise, mutate, init). */
    class StateSwapAction : public juce::UndoableAction
    {
    public:
        StateSwapAction (std::function<void (const PresetState&)> applyFn, PresetState b, PresetState a)
            : apply (std::move (applyFn)), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { apply (after); return true; }
        bool undo() override { apply (before); return true; }
        int getSizeInUnits() override { return 50; }

    private:
        std::function<void (const PresetState&)> apply;
        PresetState before, after;
    };
} // namespace

/**
    Turns UI gestures into undo steps. A gesture group starts when the first gesture begins and
    every parameter changed during it lands in the same undo transaction, so dragging an envelope
    node (several parameters) is a single step. Host automation (no gestures) is never recorded.
*/
class NeddPEAudioProcessor::ParameterUndoRecorder : private juce::AudioProcessorParameter::Listener
{
public:
    explicit ParameterUndoRecorder (NeddPEAudioProcessor& p) : processor (p)
    {
        for (const auto& d : getParamDefs())
            processor.parameters[(size_t) d.index]->addListener (this);
    }

    ~ParameterUndoRecorder() override
    {
        for (const auto& d : getParamDefs())
            processor.parameters[(size_t) d.index]->removeListener (this);
    }

private:
    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int parameterIndex, bool starting) override
    {
        if (processor.undoSuppression > 0 || ! juce::MessageManager::existsAndIsCurrentThread())
            return;

        const int index = findIndex (parameterIndex);
        if (index < 0)
            return;

        auto* param = processor.parameters[(size_t) index];
        const float plain = param->convertFrom0to1 (param->getValue());

        if (starting)
        {
            if (active.empty())
                processor.undoManager.beginNewTransaction (getParamDef (index).name);
            active[index] = plain;
            return;
        }

        const auto it = active.find (index);
        if (it == active.end())
            return;
        const float before = it->second;
        active.erase (it);

        if (std::abs (plain - before) > 1.0e-6f)
            processor.undoManager.perform (new ParameterChangeAction (processor, { { index, before, plain } }));
    }

    int findIndex (int processorParameterIndex)
    {
        // AudioProcessor parameter order equals the definition order.
        return juce::isPositiveAndBelow (processorParameterIndex, pid::count) ? processorParameterIndex : -1;
    }

    NeddPEAudioProcessor& processor;
    std::map<int, float> active;
};

NeddPEAudioProcessor::NeddPEAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, ids::params, createParameterLayout())
{
    for (const auto& d : getParamDefs())
    {
        parameters[(size_t) d.index] = state.getParameter (d.id);
        jassert (parameters[(size_t) d.index] != nullptr);
    }

    reader.attach (state);
    undoRecorder = std::make_unique<ParameterUndoRecorder> (*this);
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
    undoRecorder.reset();
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
    applyState (PresetState(), "Init patch");
}

PresetManager& NeddPEAudioProcessor::getPresetManager()
{
    if (presetManager == nullptr)
        presetManager = std::make_unique<PresetManager> (*this);
    return *presetManager;
}

void NeddPEAudioProcessor::randomise (PatchRandomizer::Mode mode)
{
    auto current = captureState();
    if (mutationHistory.entries.empty())
    {
        current.name = "Origin: " + current.name;
        mutationHistory.push (current);
    }

    generatorSeed = generatorSeed * 1664525u + 1013904223u + (uint32_t) juce::Time::getMillisecondCounter();
    auto next = PatchRandomizer::randomise (captureState(), mode, generatorSeed);
    next.name = "Random " + PatchRandomizer::getModeNames()[(int) mode] + " " + juce::String ((int) mutationHistory.entries.size());
    applyState (next, "Randomise " + PatchRandomizer::getModeNames()[(int) mode]);
    mutationHistory.push (next);
}

void NeddPEAudioProcessor::mutate (float amount)
{
    auto current = captureState();
    if (mutationHistory.entries.empty())
    {
        current.name = "Origin: " + current.name;
        mutationHistory.push (current);
    }

    generatorSeed = generatorSeed * 1664525u + 1013904223u + (uint32_t) juce::Time::getMillisecondCounter();
    auto next = PatchRandomizer::mutate (captureState(), amount, generatorSeed);
    next.name = "Mutation " + juce::String ((int) mutationHistory.entries.size());
    applyState (next, "Mutate");
    mutationHistory.push (next);
}

void NeddPEAudioProcessor::recallMutation (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) mutationHistory.entries.size()))
        return;
    applyState (mutationHistory.entries[(size_t) index], "Recall " + mutationHistory.entries[(size_t) index].name);
    mutationHistory.current = index;
}

// ---------------------------------------------------------------------------------------------
// Structured data
// ---------------------------------------------------------------------------------------------
void NeddPEAudioProcessor::setMacroName (int index, const juce::String& name)
{
    macroNames[(size_t) juce::jlimit (0, kNumMacros - 1, index)] = name.substring (0, 24).toUpperCase();
    ++soundVersion;
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

    std::vector<std::pair<int, float>> changes;
    for (const auto& d : getParamDefs())
        if (d.morphable)
            changes.push_back ({ d.index, target[d.index] });
    setParametersUndoable (changes, "Swap Morph A/B");

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
PresetState NeddPEAudioProcessor::captureState() const
{
    PresetState st;
    st.name = currentPresetName;
    st.category = currentPresetCategory;
    for (const auto& d : getParamDefs())
    {
        const auto* p = parameters[(size_t) d.index];
        st.params[d.index] = p->convertFrom0to1 (p->getValue());
    }
    st.macroNames = macroNames;
    st.lfoShapes = lfoShapes;
    st.pattern = pattern;
    st.arpPattern = arpPattern;
    st.hasMorphTarget = morphTarget != nullptr;
    if (morphTarget != nullptr)
        st.morphTarget = *morphTarget;
    return st;
}

void NeddPEAudioProcessor::applyStructured (const PresetState& s)
{
    currentPresetName = s.name;
    currentPresetCategory = s.category;
    macroNames = s.macroNames;
    lfoShapes = s.lfoShapes;
    lfoShapes.rebuildTables();
    publishLfoShapes();
    pattern = s.pattern;
    arpPattern = s.arpPattern;
    ++patternVersion;
    publishPattern();
    publishArpPattern();
    if (s.hasMorphTarget)
        morphTarget = std::make_unique<ParamSnapshot> (s.morphTarget);
    else
        morphTarget.reset();
    publishMorphTarget();
    ++soundVersion;
}

void NeddPEAudioProcessor::applyStateDirect (const PresetState& st)
{
    const ScopedUndoSuppression suppress (*this);
    for (const auto& d : getParamDefs())
        setParameterPlain (d.index, juce::jlimit (d.range.start, d.range.end, st.params[d.index]));
    applyStructured (st);
}

void NeddPEAudioProcessor::applyState (const PresetState& st, const juce::String& undoName)
{
    if (undoName.isEmpty())
    {
        applyStateDirect (st);
        return;
    }

    undoManager.beginNewTransaction (undoName);
    undoManager.perform (new StateSwapAction ([this] (const PresetState& v) { applyStateDirect (v); }, captureState(), st));
}

void NeddPEAudioProcessor::setParametersUndoable (const std::vector<std::pair<int, float>>& changes, const juce::String& undoName)
{
    std::vector<ParameterChangeAction::Change> list;
    for (const auto& [index, value] : changes)
    {
        const auto* p = parameters[(size_t) index];
        list.push_back ({ index, p->convertFrom0to1 (p->getValue()), value });
    }
    undoManager.beginNewTransaction (undoName);
    undoManager.perform (new ParameterChangeAction (*this, std::move (list)));
}

juce::ValueTree NeddPEAudioProcessor::createStateTree (bool includePerformanceData) const
{
    auto root = captureState().toValueTree();

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

    applyState (PresetState::fromValueTree (root), {});

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
