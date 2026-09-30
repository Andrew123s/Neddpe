#pragma once

#include "DSP/Lfo.h"
#include "Modulation/Monitors.h"
#include "Parameters/ParamSnapshot.h"
#include "Sequencer/Generators.h"
#include "Synth/OscillatorAssets.h"
#include "Synth/Tuning.h"
#include "Utilities/RealtimeExchange.h"
#include "Utilities/SpscFifo.h"

namespace nedd
{
/** The stored "B" side of A/B morphing (A is always the live patch). */
struct MorphTarget
{
    bool valid = true;
    ParamSnapshot plain;
    NormalisedPatch normalised {};
};

/** A MIDI message injected by the UI (on-screen keyboard / expression pad). */
struct UiMidiMessage
{
    uint8_t bytes[3] {};
    int size = 0;
};

/** A controller received on the audio thread, forwarded to the message thread for MIDI learn. */
struct ControllerEvent
{
    int channel = 1;
    int cc = 0;
    float value = 0.0f;
};

/** Clip transport commands sent from the UI to the audio thread. */
enum class ClipCommand : int { None = 0, Play, Stop, Record };

/**
    State shared between the message thread (processor / editor) and the audio engine.
    Every member is either lock-free or an immutable-object exchange.
*/
struct EngineShared
{
    RealtimeExchange<TuningData> tuning;
    RealtimeExchange<dsp::LfoCustomShapes> lfoShapes;
    RealtimeExchange<MorphTarget> morphTarget;
    RealtimeExchange<SequencerPattern> pattern;
    RealtimeExchange<ArpPattern> arpPattern;
    RealtimeExchange<NoteClip> clip;
    RealtimeExchange<OscillatorAssets> assets;

    SpscFifo<RecordedEvent, 16384> recorded;
    std::atomic<int> clipCommand { (int) ClipCommand::None };
    std::atomic<bool> clipLoop { true };
    std::atomic<bool> clipSyncToHost { false };

    SpscFifo<UiMidiMessage, 1024> uiMidi;
    SpscFifo<ControllerEvent, 1024> controllers;

    EngineTelemetry telemetry;
};

} // namespace nedd
