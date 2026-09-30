# MPE in NeddPE

NeddPE is built around one rule: **expression belongs to a note, never to a channel or to the whole synth.**
Every sounding voice carries its own pitch, pressure, slide, velocity and release velocity, and every part of the
engine that reacts to expression (oscillators, filter, amp, per-note effect sends, A/B morph) evaluates it for that
voice alone.

## From MIDI to notes

`MpeInputProcessor` (Source/MPE) turns MIDI into note events addressed by a **note id**, not by channel:

| MIDI | Handling |
|---|---|
| Note on / off | Allocates a note id; release velocity is kept for the release-velocity source. |
| Pitch bend on a member channel | Per-note bend: `value x member bend range`, plus the zone's master bend. |
| Pitch bend on a master channel | Bends every note of that zone (and drives the global *Pitch Bend* source). |
| Channel pressure on a member channel | Per-note pressure. |
| Polyphonic aftertouch | Pressure of that exact note (overrides channel pressure for it). |
| CC74 on a member channel | Per-note slide (timbre). |
| Channel pressure on a master channel | Global *Aftertouch* source. |
| CC64 | Sustain: released notes keep sounding until the pedal is lifted. |
| CC1 | Global *Mod Wheel* source. All CCs are available to MIDI learn. |
| RPN 0 | Pitch-bend sensitivity. On a member channel it sets the zone's per-note range, on a master channel the master range. Overrides the parameter until the parameter is changed. |
| RPN 6 (MCM) | MPE Configuration Message: sets the number of member channels of the lower (ch 1) or upper (ch 16) zone. Resets the per-note range to 48, as the spec requires. Ignored in Legacy mode. |
| All notes off / All sound off | Releases every note. |

Expression received on a channel *before* its note-on (as the MPE spec asks controllers to send it) becomes the
note's initial value.

### Zone modes (`mpe_mode`)

| Mode | Master | Members |
|---|---|---|
| Lower Zone (default) | 1 | 2-16 |
| Upper Zone | 16 | 1-15 |
| Both Zones | 1 and 16 | 2-8 and 9-15 |
| Off (Legacy) | none | every channel is its own expression channel, bent by the master bend range |

Legacy mode also suits ordinary keyboards: the wheel and channel aftertouch reach all notes of that channel, so the
*MPE Pitch* and *MPE Pressure* sources still work.

## Voices and the note id

`VoiceManager` starts one voice per note. Each voice stores `noteId` (the note it plays) and `exprId` (the note whose
expression it follows). Expression events are delivered only to voices whose `exprId` matches: there is no path by
which one note's expression can change another voice.

- **Stealing**: when the polyphony limit is hit, the quietest released voice (or else the oldest) is stolen. It fades
  out over 3 ms on a spare physical voice and *immediately* stops following its note id, so late expression for the
  stolen note cannot leak into the note that replaced it.
- **Mono / Legato**: one voice follows the note on top of a note stack; when that key is released it returns to the
  previous key, including that key's current expression.
- **Arpeggiator**: arp notes get a new `noteId` but keep the held key's id as `exprId`. Pressing, sliding or bending
  a held key therefore shapes every arp note that key produces, and nothing else.
- **Sequencer and note-editor clip**: generated notes carry their own expression (step values or drawn curves).

## Expression sources

| Source | Value |
|---|---|
| MPE Pitch | per-note bend (semitones / 48), bipolar. Routed 1:1 to *Pitch* in slot 1 of every patch, so bends play in tune; route it elsewhere too, or remove it for bend-free patches. |
| MPE Pressure | per-note pressure, 0..1, after the pressure curve |
| MPE Slide | per-note CC74, 0..1, after the slide curve |
| Velocity | 0..1 after the velocity curve |
| Release Velocity | 0 while held, the note-off velocity after release (e.g. to shorten or lengthen the release) |

Per-note expression is smoothed (`mpe_smooth`, default 8 ms; pitch at half that) so 7-bit controllers do not
zipper. **Pitch Quantize** (`scale_bendq`) pulls bends towards the nearest degree of the selected scale.

Destinations marked *per note* in [PARAMETERS.md](PARAMETERS.md) are computed for every voice. Global destinations
(the insert effects, arp gate/probability) read per-note sources from the most recently played note; the per-note
**Delay Send**, **Reverb Send**, **Filter Drive** and **Morph** destinations are the per-note alternatives.

## Calibration

MPE page > **CALIBRATION** shows the raw incoming pitch bend, pressure, slide and velocity with the response curve
of each, the in/out value, the zone layout, bend ranges, pitch sensitivity and smoothing. Controllers differ; the
goal is that your lightest and firmest touch cover the full 0..1 range after the curve.

Typical settings:

| Controller | MPE mode | Bend range |
|---|---|---|
| ROLI Seaboard / Lumi | Lower Zone | 48 |
| LinnStrument | Lower Zone | 24 (or whatever the device is set to) |
| Sensel Morph, Joué | Lower Zone | 48 |
| Standard keyboard with aftertouch | Off (Legacy) | Master bend 2 |

If the controller sends an MCM and RPN 0, NeddPE follows them automatically.

## Visualisation

- **Expression field** (MAIN and MPE pages): each sounding note is a circle at its real pitch (key + bend) and
  slide, sized by pressure, glowing by velocity, with a 1.5 s trail.
- **Voice lanes**: one row per voice with pitch, pressure, slide, velocity, release velocity, amp level and how
  much modulation that voice is receiving.
- **Timeline**: the last 5 s of pitch, pressure and slide per note.
- **Filter response**: a marker at each sounding note's own cutoff.
- **Knobs**: the violet dot is where the most recent note's modulation puts the value right now.

## Recording and editing performances

NOTE EDITOR > **REC** records live MPE: every note keeps its own pitch, pressure and slide curves (thinned to the
points that matter). Recordings can overdub a looping clip. The piano roll draws pressure inside each note and the
real bend as a line; the expression lane below redraws pitch, pressure, slide or velocity per note. Clips play back
through the same per-note path and can be sent to other instruments with MPE MIDI out.

## MPE MIDI out

With **Send generated notes as MPE MIDI** (`midi_out`) on, notes produced by the arpeggiator, sequencer and clip are
also sent to NeddPE's MIDI output as MPE: an MCM for a 15-channel lower zone, bend range 48, one member channel per
note, and pitch bend / channel pressure / CC74 on that note's channel. Arp notes keep following their finger.
Live input is not echoed.

## DAW setup

VST3 has no raw MIDI CC or pitch-bend events. Hosts deliver per-channel pitch bend, channel pressure and CCs to
VST3 plugins through MIDI-mapping parameters, which NeddPE exposes via JUCE (you will see a large parameter count in
generic host views; those are the per-channel MIDI mappings MPE needs).

NeddPE's VST3 has been validated by loading it through JUCE's VST3 host (see [TESTING.md](TESTING.md)); it has
**not yet been tested inside each commercial DAW**. The notes below are general host guidance:

- **Bitwig Studio**: MPE works directly; set the per-note bend range to match NeddPE (48).
- **Ableton Live 11/12**: enable *MPE* on the track's MIDI input / plugin device.
- **Cubase / Nuendo**: works via MIDI; VST3 Note Expression is not used by NeddPE.
- **Reaper**: works; make sure the track sends all 16 MIDI channels.
- **FL Studio**: set the controller's MIDI input to *omni* and route all channels to the plugin.

If per-note bends arrive as whole-keyboard bends, the host is collapsing channels: check its MPE / multi-channel
setting, or switch NeddPE to Legacy mode.
