# Building NeddPE

NeddPE builds as a **VST3 instrument** and a **Standalone** app from one CMake project. Windows 10/11 x64 is the
primary platform; the code has no Windows-specific DSP or UI, so macOS (VST3/AU) is a CMake option away (see the end).

## 1. Install the dependencies

| Tool | Version | Notes |
|---|---|---|
| Visual Studio 2022 | 17.x | Workload **Desktop development with C++** (MSVC v143 + Windows 10/11 SDK). Community edition is fine. |
| CMake | 3.22 or newer | Visual Studio ships one (`Developer PowerShell` has it on `PATH`), or install from cmake.org. |
| Git | any | Needed so CMake can fetch JUCE. |
| JUCE | 8.0.9 | **Downloaded automatically** by CMake (FetchContent). No manual install. |

Nothing else is required: no VST3 SDK download (JUCE bundles it), no Python, no package manager.

To build against an existing JUCE checkout instead of downloading it, pass
`-DNEDDPE_JUCE_DIR=C:/path/to/JUCE` when configuring.

## 2. Configure

From the repository root, in a **Developer PowerShell for VS 2022** (or any shell where `cmake` is on `PATH`):

```bash
cmake --preset vs2022
```

This creates a Visual Studio solution in `build/` and downloads JUCE into `build/_deps/` the first time
(~200 MB, a few minutes).

Options (add as `-DNAME=VALUE`):

| Option | Default | Meaning |
|---|---|---|
| `NEDDPE_BUILD_TESTS` | `ON` | Build the `NeddPETests` console app. |
| `NEDDPE_BUILD_STANDALONE` | `ON` | Also build the Standalone application. |
| `NEDDPE_COPY_PLUGIN` | `OFF` | Copy the VST3 into `C:\Program Files\Common Files\VST3` after each build (needs an elevated shell). |
| `NEDDPE_JUCE_DIR` | empty | Use a local JUCE checkout. |

## 3. Build

```bash
cmake --build --preset release
```

Use `--preset debug` for a Debug build. You can also open `build/NeddPE.sln` in Visual Studio and build the
`NeddPE_VST3`, `NeddPE_Standalone` or `NeddPETests` projects.

A faster single-configuration alternative from a Developer PowerShell:

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release
```

## 4. Run the tests

```bash
ctest --preset release
```

or run the binary directly for the full report: `build/NeddPETests_artefacts/Release/NeddPETests.exe`.
See [TESTING.md](TESTING.md) for the extra modes (VST3 host validation, benchmark, UI snapshots).

## 5. Where the VST3 is

| Build | Location |
|---|---|
| Visual Studio preset, Release | `build/NeddPE_artefacts/Release/VST3/NeddPE.vst3` |
| Visual Studio preset, Debug | `build/NeddPE_artefacts/Debug/VST3/NeddPE.vst3` |
| Ninja Release | `build-ninja/NeddPE_artefacts/Release/VST3/NeddPE.vst3` |
| Standalone app | `.../NeddPE_artefacts/<Config>/Standalone/NeddPE.exe` |

`NeddPE.vst3` is a folder (a VST3 bundle); copy the whole folder.

## 6. Install

Copy the `NeddPE.vst3` folder to the system VST3 folder (administrator rights needed):

```
C:\Program Files\Common Files\VST3\
```

Or configure with `-DNEDDPE_COPY_PLUGIN=ON` and build from an elevated shell to have CMake do it.
Before distributing a binary, read the licensing note in the [README](../README.md#licence).

## 7. Open it in a DAW

1. Rescan plugins (most DAWs do this on start-up). NeddPE appears as an **Instrument** from **Nedd Audio**.
2. Put it on an instrument/MIDI track.
3. For MPE controllers, enable MPE on the track in the host (see the host notes in [MPE.md](MPE.md#daw-setup)).
   NeddPE's default MPE mode is *Lower Zone* with a 48-semitone per-note bend range.
4. With no controller, play the on-screen keyboard at the bottom of the window: drag sideways to bend a single
   note, drag vertically for slide, use the mouse wheel while holding a key for pressure.
5. Save the project, close and reopen it: every parameter, the modulation matrix, sequencer/arp patterns, the
   recorded clip, tuning, MIDI-learn mappings and the window size are restored.

## Troubleshooting

- **CMake cannot find a compiler**: open a *Developer PowerShell for VS 2022*, or use the `vs2022` preset, which
  locates MSVC itself.
- **JUCE download fails** (offline or behind a proxy): clone `https://github.com/juce-framework/JUCE` at tag
  `8.0.9` and pass `-DNEDDPE_JUCE_DIR=...`.
- **`MSB8066` / "Build step for juce failed" while configuring with the Visual Studio generator**: the checkout path is
  too long for MSBuild's 260-character limit (FetchContent creates deep paths). Clone to a short path such as
  `C:\dev\NeddPE`, or use the Ninja presets.
- **The DAW does not list NeddPE**: make sure the whole `NeddPE.vst3` folder was copied, then force a rescan.
  You can check the bundle without a DAW: `NeddPETests.exe --validate-vst3 path\to\NeddPE.vst3`.
- **Copy step fails** with `NEDDPE_COPY_PLUGIN=ON`: the build shell needs administrator rights to write to
  `Program Files`.

## macOS (not yet tested)

The project uses only portable JUCE modules. On macOS, adding `AU` to `NEDDPE_FORMATS` in `CMakeLists.txt` and
configuring with `-G Xcode` should work, but this has not been built or tested yet.
