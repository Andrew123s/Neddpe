# NeddPE prebuilt binaries (Windows x64)

Version 0.2.0, Release build (MSVC, JUCE 8.0.9, static C++ runtime). Built from the source in this repository.

| File | What it is |
|---|---|
| `NeddPE.exe` | Standalone app. Run it directly; set audio and MIDI devices under *Options > Audio/MIDI Settings*. |
| `NeddPE.vst3` | VST3 plugin (a folder). Copy the whole folder to `C:\Program Files\Common Files\VST3` and rescan in your DAW. |

The binaries are not code-signed, so Windows SmartScreen may ask for confirmation (*More info > Run anyway*).

Version 0.2 builds without warnings but has not yet been through the test suite or the benchmark (see the Status
section of the main [README](../README.md#status)).

Licence: GNU AGPLv3 (see [LICENSE](../LICENSE)). The complete corresponding source is this repository.
