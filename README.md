# ak4522 demo plugins

Two JUCE plugins that show the [ak4522](https://github.com/remix1201/ak4522) library in use. The library emulates the AKM AK4522VF audio codec from its datasheet.

| Plugin           | Type       | Built on                  | What it does                                                                                       |
| ---------------- | ---------- | ------------------------- | -------------------------------------------------------------------------------------------------- |
| **AK4522 DAC**   | effect     | `ak4522::HostProcessor`   | Runs host audio through the emulated DAC, the DAC into the ADC, or the ADC alone, at a chosen chip rate |
| **AK4522 Synth** | instrument | `ak4522::DacStream`       | A small test synth that generates samples at its own clock, such as 112 MHz / 2304, played through the emulated DAC |

**Status: alpha, not released.** Both plugins build and pass their tests, pluginval at strictness level 10, and strict `auval`, on macOS. Windows builds run in CI. Both plugins open in FL Studio 26.1.7 and Ableton Live 12 Lite 12.4.6; the rest of the host smoke tests are pending. See [docs/features.md](docs/features.md).

Formats: VST3 on macOS and Windows, and AU on macOS. The interface lists the controls in groups, with a status line showing the configuration and latency. Hover over a control for a one-line description that says whether it is AK4522 behavior, a library feature, or plugin behavior; [docs/parameters.md](docs/parameters.md#what-each-control-does) has the same descriptions with links to the library's documentation. The bottom line names the build: plugin version, library version, and the git commit it was built from, marked "-dirty" if built with uncommitted changes. Quote it when you report a host result.

## What the plugins add

Chip behavior comes from the library: its filters, delays, de-emphasis, soft mute, ADC, and optional NominalAnalog imperfections. The plugins add host behavior around it, which is not part of the AK4522:

- output gain, a wet/dry mix against the latency-aligned input, a delta listen mode (processed minus aligned input), and compensated bypass (effect);
- a polyphonic sine, saw, and square test synth with attack and release (instrument);
- the mapping from parameters to the chip's DEM and SMUTE pins, and from MIDI to the source's clock.

Every parameter, its library mapping, the latency and tail policy, and the saved-state format are listed in [docs/parameters.md](docs/parameters.md).

## Building

Requirements: CMake 3.24 or later and a C++20 compiler (Xcode on macOS, Visual Studio 2022 or later on Windows). JUCE 9.0.4 and the ak4522 library are downloaded at configure time.

```sh
git clone https://github.com/remix1201/ak4522-demo-plugin.git
cd ak4522-demo-plugin
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

The plugins are in `build/AK4522Effect_artefacts/Release/` and `build/AK4522Synth_artefacts/Release/`. Configure with `-DAK4522_DEMO_COPY_PLUGINS=ON` to copy them to your user plugin folders after each build. On macOS, AU plugins must be in `~/Library/Audio/Plug-Ins/Components` before hosts or `auval` can find them.

Options:

- `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` uses a local JUCE checkout instead of downloading it; it must be JUCE 9.0.4.
- `-DFETCHCONTENT_SOURCE_DIR_AK4522=/path/to/ak4522` builds against a local library checkout. This is for testing a library change before it is released; committed builds always use a tagged release.
- `-DAK4522_DEMO_TESTS=OFF` skips the tests.

## Licence

Copyright (C) 2026 Ross Baker. The project's own code is licensed under the GNU General Public License, version 3 or later ([LICENSE](LICENSE)). JUCE is used under its AGPLv3 option, so distributed plugin binaries are covered by the AGPLv3 as well. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for every component and what distribution requires.

AKM and AK4522 identify the hardware the library models. This is an independent project, not affiliated with or endorsed by the manufacturer.
