# NeddPE architecture

## Layers

```
Source/
  PluginProcessor/   host integration: parameters, state, undo, MIDI learn, recorder service
  PluginEditor/      the window: scaling, page navigation
  Parameters/        central parameter table (IDs, ranges, formatting, flags) + snapshot reader
  MPE/               MIDI -> per-note events (zones, MCM/RPN, sustain), event list types
  Voices/            Voice (per-note synthesis), Oscillator, VoiceManager (allocation, stealing, mono/legato)
  DSP/               building blocks: envelopes, filters, LFOs, wavetables, maths
  Modulation/        sources/destinations, routing + evaluation, telemetry for the UI
  Effects/           global effect units and the chain
  Sequencer/         step sequencer, arpeggiator, clip player, MPE MIDI out, recorder, clip model
  Synth/             SynthEngine (the audio engine), tuning, tempo, shared state
  Presets/           PresetState (serialisation), factory library, preset manager, randomiser
  UI/                design system, controls, displays, MPE visualiser, pages
  Tests/             unit tests, VST3 host validation, benchmark, doc/preset generators
```

`SynthEngine` has no dependency on the plugin wrapper or UI, so the tests drive it directly.

## Audio thread: one block

```
processBlock
  ParamReader.read(snapshot)                 338 relaxed atomic loads -> plain values
  read host transport (bpm, ppq, playing)    internal clock when the host is stopped
  SynthEngine.process (in chunks <= prepared block size)
    beginBlock          routing from the matrix params, acquire exchanged data (tuning, LFO curves,
                        morph B, patterns, clip), tempo-derived rates, voice/MPE config
    collectInput        host MIDI + on-screen keyboard FIFO -> MpeInputProcessor -> note events
    evaluateGlobalModulation   global LFOs, global matrix destinations (FX, arp)
    handleClipCommands / recordLiveEvents     transport commands; live events -> recorder FIFO
    generateNotes       arp (consumes live notes) | pass-through, + sequencer + clip player,
                        sorted by time; MPE MIDI out for generated notes
    render              sample-accurate: voices render up to each event, then the event applies
    effects             insert chain + delay/reverb returns fed by per-voice sends
    master volume, limiter, meters, telemetry
```

### Per-voice processing

Each voice runs a **control-rate** update every 8/16/32/64 samples (CPU quality Ultra/High/Normal/Eco):
expression smoothing, glide, per-voice LFOs and envelopes, the voice-scope matrix, per-note A/B morph, then derived
oscillator frequencies, filter targets, gains and sends. Between updates it runs a per-sample loop (oscillators
with cross-FM/sync/ring, filter, amp envelope) with every target linearly interpolated, so control-rate modulation
never steps audibly.

### Voice vs global processing

| Per voice (per note) | Global |
|---|---|
| 3 oscillators, unison, cross-FM, sync, ring | Distortion, saturation, bitcrush |
| Filter (+ drive, key tracking, env) | Chorus, phaser, flanger |
| Amp / filter / mod envelopes | Delay and reverb (inputs are per-voice sends) |
| Per-voice LFOs (retriggered or phase-locked) | EQ, compressor, limiter, master volume |
| Expression smoothing, glide | Global LFO instances, global matrix destinations |
| Voice-scope matrix destinations | Arp and sequencer clocks |
| Per-note A/B morph | Effect-parameter morph (base position + most recent note) |
| Delay / reverb send level | |

## Threads and data flow

Nothing on the audio thread allocates, locks or does file I/O after `prepareToPlay`.

| Data | Writer | Mechanism |
|---|---|---|
| Parameters | host / UI | APVTS atomics, read once per block into a `ParamSnapshot` |
| Tuning, LFO curves, morph B, sequencer pattern, arp pattern, clip | message thread | `RealtimeExchange<T>`: publish a new immutable object; the audio thread swaps a pointer at block start; retired objects go back through a lock-free queue and are freed on the message thread |
| On-screen keyboard MIDI | UI | `SpscFifo<UiMidiMessage>` |
| Controllers for MIDI learn | audio | `SpscFifo<ControllerEvent>`, drained by a 30 Hz timer that applies mappings with `setValueNotifyingHost` |
| Recorded performance events | audio | `SpscFifo<RecordedEvent>` (16k), drained by the processor timer into `PerformanceRecorder` |
| Clip transport commands | UI | atomics (`clipCommand`, loop, host sync) |
| Voice, modulation and meter telemetry | audio | per-field relaxed atomics in `EngineTelemetry`, read by the UI at 30 Hz |

The event list (4096 events), voices (40 physical for a 32-note limit), effect buffers and oversamplers are all
sized in `prepareToPlay`.

## Parameters

`Parameters/ParameterDefs.cpp` declares every parameter once: stable string ID, name, group, type, range/skew,
default, formatter, automatable flag, morphable flag and scope. The same table builds the APVTS layout, the
PARAMETERS.md reference, the UI tooltips/formatting, morph and mutation sets, and the preset format. Indices come
from compile-time field enums (`pid::osc (1, OscField::Level)`); the builder asserts that declaration order matches.

Matrix routing choices are registered as non-automatable; amounts, macros and sound parameters are automatable.

## State, presets and undo

`PresetState` is the only serialisation path (see [PRESET_FORMAT.md](PRESET_FORMAT.md)):

- **Presets** = `PresetState` (parameters, macro names, LFO curves, sequencer + arp patterns, morph B).
- **DAW project state** = `PresetState` + performance data (recorded clip and its transport settings, tuning table
  and user scale, MIDI-learn map, window size).

Undo (`UndoManager` in the processor, UNDO/REDO in the header, Ctrl+Z/Y in the note editor):

- A parameter **gesture** (knob drag, combo change, toggle, envelope-node drag) is one step, recorded by a
  gesture listener on every parameter. Host automation has no gestures and is never recorded.
- Preset load, init, randomise, mutate, morph swap and multi-parameter matrix edits are single steps.
- Sequencer, arp-pattern and clip edits (including a whole recording) are single steps.

## UI

The editor lays out everything at a fixed design size (1280x820) inside a root component that is scaled as a whole
(75-150%, saved with the project). `EditorContext` gives every widget a 30 Hz tick, a message-thread copy of the
parameters and routing, and helpers to add/remove modulation. Pages are created on first visit.

Modulation is always visible: knobs draw their base value, the reach of every route into them, and the live value
for the most recent note; the matrix inspector breaks a destination down into base + each route = result.

## Extension points

- **New oscillator engine** (e.g. granular, sample playback): append to `OscEngine` and the engine choice list, add a
  branch in `Oscillator::renderUnison`, add controls to `OscillatorPanel`. The voice, matrix and morph pick it up.
- **New modulation source/destination**: append to `ModSource`/`ModDest` and their info tables, fill the source in
  `Voice::updateControl` (or the global evaluation), apply the destination where the parameter is used, and map it
  to its knob in `ParamModMapping.cpp` for visualisation.
- **New parameter**: add a field enum value and declare it in `ParameterDefs.cpp`. Never rename an existing ID.
