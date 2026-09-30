# Testing NeddPE

All tests live in `Source/Tests` and build into one console app, **`NeddPETests`**, which compiles the same
sources as the plugin. It needs no DAW and no audio device.

```bash
ctest --preset release                                   # via CTest
build/NeddPETests_artefacts/Release/NeddPETests.exe      # full report
NeddPETests.exe --test "MPE"                             # only suites whose name contains "MPE"
```

The exit code is non-zero if any check fails.

## Unit and integration suites

| Suite | What it proves |
|---|---|
| **MPE handling** | Per-note pitch bend moves only its note; pressure and slide have no cross-talk between voices (A = 0.8, B = 0.2, then changing A leaves B untouched); expression sent before note-on becomes the initial value; master-channel bend reaches the whole zone; Legacy mode; bend range from the parameter and from RPN 0; the MPE Configuration Message; sustain pedal and release velocity; a re-used channel starts a fresh note id. |
| **Voice allocation** | Notes sound and release to silence; polyphony limit and stealing; a stolen voice stops following its note's expression; stealing does not click; legato falls back to the held note; mono follows the top note's expression; glide. |
| **DSP stability** | Envelope stage timing and click-free retrigger; every oscillator engine x waveform x pitch (20 Hz-18 kHz, 7-voice unison, FM, feedback) stays finite and bounded; wavetables are normalised and the mip selection is right; every filter mode stays stable while sweeping at full resonance and drive; 24 dB low-pass attenuation; the whole engine with every engine, unison, cross-FM, ring and sync. |
| **Modulation matrix** | Curve and polarity shaping; routes sum per destination; pressure > cutoff and slide > wavetable position are evaluated per note; LFO rate and host tempo sync; macros; per-note A/B morph driven by slide. |
| **Effects** | Every effect maxed at every CPU quality stays finite with no runaway; the full engine with every effect maxed never exceeds full scale (limiter); delay timing and feedback; exact-integer delay reads across the buffer wrap (regression test for a real out-of-bounds read found during development); reverb tail and decay; per-note delay send from pressure. |
| **Sequencer, arpeggiator and clip** | 16 sixteenths per bar with per-step note, pressure, slide and pitch glide; ratchet, probability and swing; arp order across octaves; arp notes follow the finger that produced them, in isolation from other keys; clip playback timing and expression curves; recording live MPE into a clip (unit and through the engine); quantize/humanize/duplicate; clip serialisation; MPE MIDI out channel allocation and expression routing. |
| **Presets and state** | >= 30 factory presets covering every category, every one plays audibly and within full scale; preset round trip including macros, patterns, LFO curves and morph B; older presets load (missing IDs default, unknown IDs ignored); the randomiser produces valid, audible patches in every mode without touching the MPE setup and always assigns pressure and slide; mutation is proportional to its amount; **full state restore in a new plugin instance** (the DAW-reopen case); preset load is one undo step including structured data; knob gestures are single undo steps and host automation is never recorded. |

Current result: **662 checks, 0 failures**, in Release and in Debug (where JUCE assertions are also counted as failures; none fire). MSVC 19.37, Windows 11.

## VST3 host validation

Loads the built binary through JUCE's VST3 hosting, as a DAW would:

```bash
NeddPETests.exe --validate-vst3 build/NeddPE_artefacts/Release/VST3/NeddPE.vst3
```

Checks: the module exposes exactly one plugin, named NeddPE, registered as an instrument; it instantiates, accepts
MIDI, has a stereo output and exposes all parameters; it is silent without notes; three MPE notes (with pressure,
slide and a per-note bend) produce audio within full scale; it renders faster than real time; state saved from one
instance restores into a new one; the editor opens at a usable size.

## UI snapshots

```bash
NeddPETests.exe --snapshots out_dir
```

Opens the real editor offscreen, plays an expressive chord and loads a demo clip, then writes every page to
`out_dir/page-NN.png`. Used during development to review the interface without a host; `docs/images` was produced
this way.

## Performance

```bash
NeddPETests.exe --bench
```

16 simultaneous MPE notes with moving slide, 48 kHz, 256-sample blocks, 10 s, single core (Release, measured on the
development machine):

| Patch | Eco | Normal | High | Ultra |
|---|---|---|---|---|
| 3 analog oscillators, no unison, ladder filter, no FX | 8.7% | 9.1% | 10.5% | 13.0% |
| 3 wavetable oscillators x 8-voice unison, all FX | 27.7% | 27.0% | 31.4% | 37.2% |
| 3 FM oscillators x 8-voice unison (72 operators per note), all FX | 60.8% | 62.6% | 64.9% | 71.7% |

Through the VST3 wrapper, three notes of the default patch render 10 s of audio in about 0.1 s (~1% of real time).

The dominant cost is the per-sample voice loop (about 0.5% of a core per voice for three oscillators and a ladder
filter). The planned optimisation is block-based, SIMD-across-voices rendering; the control-rate work is already
small (the Eco-to-Ultra spread above).

## Documentation generators

```bash
NeddPETests.exe --dump-params docs/PARAMETERS.md     # parameter reference from the central table
NeddPETests.exe --export-presets presets             # factory presets as .neddpe files
```

## Manual checks before a release

- Load the VST3 in at least one MPE-capable DAW, play an MPE controller and confirm per-note bend, pressure and slide.
- Save and reopen a project; confirm the sound, clip and MIDI-learn mappings are restored.
- Resize the window (75-150%) and step through every page.
- Record a performance in the note editor, edit a curve, undo/redo.
