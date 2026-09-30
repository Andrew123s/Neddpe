#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters/ParamSnapshot.h"
#include "Synth/EngineShared.h"
#include "Synth/SynthEngine.h"

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

    /** Sets every parameter to its default and clears structured sound data. */
    void resetToInitPatch();

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

    // MIDI learn (message thread)
    void armMidiLearn (int paramIndex) { learnTarget = paramIndex; }
    int getMidiLearnTarget() const noexcept { return learnTarget; }
    void clearMidiMapping (int paramIndex);
    int getMidiMappingFor (int paramIndex) const;   // CC number or -1
    std::function<void()> onMidiLearnChanged;

    const juce::String& getCurrentPresetName() const noexcept { return currentPresetName; }
    void setCurrentPresetName (const juce::String& name) { currentPresetName = name; }

    /** Current parameter values (plain) read from the tree, for the message thread. */
    void readParameters (ParamSnapshot& out) const { reader.read (out); }
    void setParameterPlain (int index, float plainValue);
    juce::RangedAudioParameter* getParameterByIndex (int index) const { return parameters[(size_t) index]; }

    static constexpr int kStateVersion = 1;

private:
    void timerCallback() override;
    void publishLfoShapes();
    void publishTuning();
    void publishMorphTarget();
    TransportInfo readTransport (int numSamples);

    juce::UndoManager undoManager { 30000, 30 };
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

    std::array<int, 128> ccToParam {};   // -1 = unmapped
    int learnTarget = -1;

    double internalPpq = 0.0;
    juce::String currentPresetName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeddPEAudioProcessor)
};

} // namespace nedd
