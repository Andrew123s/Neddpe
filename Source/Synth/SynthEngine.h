#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Effects/EffectsChain.h"
#include "Effects/Limiter.h"
#include "EngineShared.h"
#include "MPE/MpeInputProcessor.h"
#include "Synth/Tempo.h"
#include "Voices/VoiceManager.h"

namespace nedd
{
/**
    The complete audio engine, independent of the plugin wrapper so it can be driven by tests.

    Per block:
      1. MIDI (host + on-screen keyboard) -> MpeInputProcessor -> note events keyed by note id
      2. note routing (arpeggiator / sequencer / clip player merge in here)
      3. sample-accurate rendering: voices run between events, events are applied in order
      4. global modulation, then the global effect chain, master volume and limiter
*/
class SynthEngine
{
public:
    explicit SynthEngine (EngineShared& shared);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const ParamSnapshot& params,
                  const TransportInfo& transport) noexcept;

    const VoiceManager& getVoiceManager() const noexcept { return voiceManager; }
    const ClipTransport& getClipTransport() const noexcept { return clipTransport; }
    const MpeInputProcessor& getMpeInput() const noexcept { return mpeInput; }
    const std::array<float, (size_t) kNumModDests>& getGlobalDestMods() const noexcept { return globalDest; }
    double getSampleRate() const noexcept { return sampleRate; }

    static int controlBlockSizeFor (Quality q) noexcept;

private:
    void beginBlock (const ParamSnapshot& params, const TransportInfo& transport) noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int bufferStart, const juce::MidiBuffer& midi,
                       const ParamSnapshot& params, const TransportInfo& transport) noexcept;
    void collectInput (const juce::MidiBuffer& midi, int chunkStart, int numSamples, bool includeUi) noexcept;
    void handleClipCommands() noexcept;
    void recordLiveEvents (const TransportInfo& transport) noexcept;
    void generateNotes (const ParamSnapshot& p, const TransportInfo& transport, int bufferStart) noexcept;
    void applyEvent (const NoteEvent& e) noexcept;
    void renderVoices (int start, int num) noexcept;
    void evaluateGlobalModulation (const TransportInfo& transport, int numSamples) noexcept;
    void writeTelemetry() noexcept;
    const ParamSnapshot& morphedEffectParams (const ParamSnapshot& p) noexcept;

    EngineShared& shared;
    double sampleRate = 44100.0;
    int maxBlock = 512;

    NoteIdAllocator noteIds;
    MpeInputProcessor mpeInput { noteIds };
    VoiceManager voiceManager;
    NoteEventList inputEvents, voiceEvents;

    ModRouting routing;
    MorphContext morph;
    VoiceContext ctx;
    std::array<float, (size_t) kNumModDests> globalDest {};
    std::array<float, (size_t) kNumModSlots> globalSlots {};
    std::array<dsp::Lfo, (size_t) kNumLfos> globalLfos;
    std::array<float, (size_t) kNumLfos> globalLfoValues {};
    dsp::LfoCustomShapes defaultLfoShapes;
    TuningData defaultTuning;

    juce::AudioBuffer<float> mainBus, delayBus, reverbBus;
    juce::SmoothedValue<float> masterGain;
    fx::EffectsChain effects;

    // Note generators
    StepSequencer sequencer;
    Arpeggiator arpeggiator;
    ClipPlayer clipPlayer;
    ClipTransport clipTransport;
    MpeMidiOutput midiOutput;
    juce::MidiBuffer midiOut;
    dsp::Random32 generatorRandom;
    SequencerPattern defaultPattern;
    ArpPattern defaultArpPattern;
    bool arpWasOn = false;
    bool midiOutWasOn = false;
    fx::Limiter limiter;
    ParamSnapshot effectParams;   // morphed copy of the global parameters when A/B morph is active

    const ParamSnapshot* currentParams = nullptr;
    const OscillatorAssets* currentAssets = nullptr;
};

} // namespace nedd
