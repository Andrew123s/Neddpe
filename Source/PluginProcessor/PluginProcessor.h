#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParamSnapshot.h"
#include "Synth/EngineShared.h"
#include "Synth/SynthEngine.h"
#include "Sequencer/ClipTools.h"
#include "Presets/PatchRandomizer.h"
#include "Presets/PresetManager.h"
#include "Presets/PresetState.h"

namespace nedd
{
/**
    Host integration: owns the parameter tree, the engine and all non-parameter state.

    Threading
      - processBlock (audio thread) reads parameters through cached atomics and structured
        data through RealtimeExchange objects; it never locks or allocates.
      - Everything else here runs on the message thread. A 30 Hz timer drains the audio
        thread's controller FIFO (MIDI learn / CC mapping) and frees retired objects.
*/
class NeddPEAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    NeddPEAudioProcessor();
    ~NeddPEAudioProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NeddPE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String& name) override { currentPresetName = name; }

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- NeddPE ----
    juce::AudioProcessorValueTreeState& getState() noexcept { return state; }
    juce::UndoManager& getUndoManager() noexcept { return undoManager; }
    EngineShared& getShared() noexcept { return shared; }
    const SynthEngine& getEngine() const noexcept { return engine; }

    /** Full plugin state (parameters + structured data) as a ValueTree. */
    juce::ValueTree createStateTree (bool includePerformanceData) const;
    /** Restores a tree made by createStateTree(). Missing sections fall back to defaults. */
    void applyStateTree (const juce::ValueTree& tree, bool includePerformanceData);

    /** Sets every parameter to its default and clears structured sound data (undoable). */
    void resetToInitPatch();

    /** The current sound as a preset (parameters + structured sound data). */
    PresetState captureState() const;
    /** Applies a sound. A non-empty undoName makes the whole change a single undo step. */
    void applyState (const PresetState& state, const juce::String& undoName);

    // Presets and sound generation (message thread)
    PresetManager& getPresetManager();
    const MutationHistory& getMutationHistory() const noexcept { return mutationHistory; }
    void randomise (PatchRandomizer::Mode mode);
    void mutate (float amount);
    void recallMutation (int index);

    /** Changes whenever structured sound data (names, curves, patterns, morph) changes. */
    int getSoundVersion() const noexcept { return soundVersion; }

    // Structured sound data (message thread). Setters publish to the audio thread.
    const juce::String& getMacroName (int index) const { return macroNames[(size_t) juce::jlimit (0, kNumMacros - 1, index)]; }
    void setMacroName (int index, const juce::String& name);

    const TuningData& getTuning() const noexcept { return tuning; }
    void setTuning (const TuningData& newTuning);

    const dsp::LfoCustomShapes& getLfoShapes() const noexcept { return lfoShapes; }
    void setLfoShapes (const dsp::LfoCustomShapes& shapes);

    /** Morph B snapshot. */
    bool hasMorphTarget() const noexcept { return morphTarget != nullptr; }
    void captureMorphTarget();                      // B := current patch
    void setMorphTarget (const ParamSnapshot& plain);
    void clearMorphTarget();
    void swapMorphAB();                             // live patch <-> B
    const ParamSnapshot* getMorphTarget() const noexcept { return morphTarget.get(); }

    // Imported oscillator content (message thread). Imports and removals are undoable.
    const OscillatorAssets& getOscillatorAssets() const noexcept { return oscAssets; }
    int getAssetsVersion() const noexcept { return assetsVersion; }
    /** Loads an audio file as the sample of an oscillator and switches it to the Granular engine
        unless it already uses Sample or Granular. Returns an error message, empty on success. */
    juce::String importSample (int osc, const juce::File& file);
    /** Loads a wavetable file into an oscillator and selects it ("Imported", Wavetable engine). */
    juce::String importWavetable (int osc, const juce::File& file);
    void removeSample (int osc);
    void removeWavetable (int osc);

    // Sequencer, arpeggiator pattern and the performance clip (message thread).
    // A non-empty undoName makes the change undoable.
    const SequencerPattern& getPattern() const noexcept { return pattern; }
    void setPattern (const SequencerPattern& newPattern, const juce::String& undoName = {});
    const ArpPattern& getArpPattern() const noexcept { return arpPattern; }
    void setArpPattern (const ArpPattern& newPattern, const juce::String& undoName = {});
    const NoteClip& getClip() const noexcept { return clip; }
    void setClip (const NoteClip& newClip, const juce::String& undoName = {});
    int getClipVersion() const noexcept { return clipVersion; }
    int getPatternVersion() const noexcept { return patternVersion; }

    // Performance recorder / clip transport
    void clipPlay();
    void clipStop();
    void clipRecord (bool overdub);
    void setClipLoop (bool loop) { shared.clipLoop.store (loop); }
    bool getClipLoop() const { return shared.clipLoop.load(); }
    void setClipSyncToHost (bool sync) { shared.clipSyncToHost.store (sync); }
    bool getClipSyncToHost() const { return shared.clipSyncToHost.load(); }
    bool isRecorderActive() const noexcept { return recorder.isActive(); }
    /** Drains recorded events and finalises a stopped recording (normally called by the timer). */
    void serviceRecorder();

    // MIDI learn (message thread)
    void armMidiLearn (int paramIndex) { learnTarget = paramIndex; }
    int getMidiLearnTarget() const noexcept { return learnTarget; }
    void clearMidiMapping (int paramIndex);
    int getMidiMappingFor (int paramIndex) const;   // CC number or -1
    std::function<void()> onMidiLearnChanged;

    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float s) { editorScale = juce::jlimit (0.75f, 1.5f, s); }

    const juce::String& getCurrentPresetName() const noexcept { return currentPresetName; }
    const juce::String& getCurrentPresetCategory() const noexcept { return currentPresetCategory; }
    void setCurrentPresetName (const juce::String& name, const juce::String& category = "User")
    {
        currentPresetName = name;
        currentPresetCategory = category;
    }

    /** Current parameter values (plain) read from the tree, for the message thread. */
    void readParameters (ParamSnapshot& out) const { reader.read (out); }
    void setParameterPlain (int index, float plainValue);
    juce::RangedAudioParameter* getParameterByIndex (int index) const { return parameters[(size_t) index]; }

    static constexpr int kStateVersion = 1;

    /** Sets several parameters as one undo step (e.g. adding a modulation route). */
    void setParametersUndoable (const std::vector<std::pair<int, float>>& changes, const juce::String& undoName);

    /** Suppresses gesture-based undo recording while alive (bulk changes create their own undo step). */
    struct ScopedUndoSuppression
    {
        explicit ScopedUndoSuppression (NeddPEAudioProcessor& p) : owner (p) { ++owner.undoSuppression; }
        ~ScopedUndoSuppression() { --owner.undoSuppression; }
        NeddPEAudioProcessor& owner;
    };

private:
    class ParameterUndoRecorder;
    friend class ParameterUndoRecorder;

    void applyStateDirect (const PresetState& state);
    void applyStructured (const PresetState& s);

    void timerCallback() override;
    void publishLfoShapes();
    void publishTuning();
    void publishMorphTarget();
    void publishPattern();
    void publishArpPattern();
    void publishClip();
    void publishAssets();
    void setAssets (const OscillatorAssets& newAssets, const juce::String& undoName,
                    const std::vector<std::pair<int, float>>& paramChanges = {});
    TransportInfo readTransport (int numSamples);

    juce::UndoManager undoManager { 5000, 30 };
    juce::AudioProcessorValueTreeState state;
    std::array<juce::RangedAudioParameter*, (size_t) pid::count> parameters {};
    ParamReader reader;
    ParamSnapshot snapshot;

    EngineShared shared;
    SynthEngine engine { shared };

    std::array<juce::String, (size_t) kNumMacros> macroNames { "MOVEMENT", "TONE", "SPACE", "DRIVE" };
    TuningData tuning;
    dsp::LfoCustomShapes lfoShapes;
    std::unique_ptr<ParamSnapshot> morphTarget;
    SequencerPattern pattern;
    ArpPattern arpPattern;
    NoteClip clip;
    OscillatorAssets oscAssets;
    int assetsVersion = 0;
    PerformanceRecorder recorder;
    bool recordStopPending = false;
    bool isPrepared = false;
    int clipVersion = 0, patternVersion = 0;

    std::array<int, 128> ccToParam {};   // -1 = unmapped
    int learnTarget = -1;

    double internalPpq = 0.0;
    float cpuLoad = 0.0f;
    float editorScale = 1.0f;
    juce::String currentPresetName { "Init" };
    juce::String currentPresetCategory { "User" };
    int soundVersion = 0;
    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<ParameterUndoRecorder> undoRecorder;
    int undoSuppression = 0;
    MutationHistory mutationHistory;
    uint32_t generatorSeed = 0x5EED;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeddPEAudioProcessor)
};

} // namespace nedd
