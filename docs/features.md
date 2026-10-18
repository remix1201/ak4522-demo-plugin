# Feature record

What exists in this repository, as distinct from what [AGENTS.md](../AGENTS.md) and the library's adapter requirements ask for. Implementation, validation, approval, and availability are tracked separately. Approval is recorded only when the project owner gives it.

Last substantive update: 2026-10-18. Library: ak4522 `v0.1.0`. JUCE: 9.0.4.

## Summary

| Feature                    | Implementation | Validation                            | Approval     | Availability     |
| -------------------------- | -------------- | ------------------------------------- | ------------ | ---------------- |
| Build and packaging        | implemented    | macOS passing; Windows in CI          | not approved | development only |
| AK4522 DAC (effect)        | implemented    | tests, pluginval, and auval passing   | not approved | development only |
| AK4522 Synth (instrument)  | implemented    | tests, pluginval, and auval passing   | not approved | development only |
| Parameter descriptions     | implemented    | tests, pluginval, and auval passing   | not approved | development only |
| Host smoke tests           | in progress    | both plugins open in FL Studio and Live | not approved | not applicable   |

## Build and packaging

- **Purpose:** build both plugins as VST3, and AU on macOS, from pinned dependencies.
- **Status:** implemented.
- **Details:**
  - JUCE 9.0.4 is fetched by commit and SHA-256.
  - The library comes from its `v0.1.0` tag through FetchContent, the documented outside-user path.
  - JUCE's recommended warning flags are on; project code builds without warnings.
  - The editor's bottom line shows the plugin version, the library version (`ak4522::versionString()`), and the git commit, refreshed at every build by `cmake/BuildInfo.cmake`. "-dirty" marks uncommitted changes to tracked files; "unknown" means the source is not a git checkout.
- **Validation:** macOS (Apple Silicon, AppleClang), Release build. Windows: by CI.
- **Limitations:** no installers and no code signing or notarization. Plugins are built for the build machine's architecture only.
- **Updated:** 2026-10-18.

## AK4522 DAC (effect)

- **Purpose:** host audio through the emulated chip (REQ-VST-01, REQ-VST-02, REQ-LAT-03 in the library's AGENTS.md).
- **Status:** implemented.
- **Usage:** insert on a stereo track. Choose the DAC rate, input resolution, path (DAC, DAC into ADC, or ADC), profile, and analog options; automate de-emphasis and soft mute. All parameters: [parameters.md](parameters.md).
- **Validation** (`ak4522_demo_effect_tests`, 20 test cases, all passing):
  - stereo-only layouts;
  - reported latency against the emitted audio on all three paths at 44.1, 48, and 96 kHz hosts, by the phase of a 1 kHz tone, within 0.05 sample;
  - bypass, both the parameter and the host's, equal to the input delayed by the reported latency;
  - mix at 0 % equal to the aligned dry signal;
  - delta below -30 dB for a passband tone;
  - state round trip, including the noise seed;
  - rejection of state with a newer schema, from the other plugin, or of another kind;
  - float and double agreeing within 1e-6;
  - block-size independence within 1e-12 on the DAC and ADC paths;
  - two independent instances;
  - no allocation while processing, including pin changes and NominalAnalog;
  - silence and a status line at an unsupported host rate;
  - re-preparation after a configuration change;
  - the declared tail covering the impulse response to -120 dB, and an infinite tail with noise;
  - parameter descriptions and groups, the analog group greyed out under Specification, DC offset greyed out on the ADC path, the version line, the editor opening, and the editor polling rather than listening (see below).

  Also passing: pluginval at strictness level 10 (VST3 and AU), and `auval -strict`. pluginval's one AU warning, "Current program is -1", reflects that the plugins have no factory presets.
- **Limitations:**
  - parameter changes act at block starts, not inside blocks;
  - a configuration change silences the plugin briefly while the chip is rebuilt;
  - mono tracks are not supported;
  - the Windows build has not been run outside CI.
- **Updated:** 2026-10-18.

## AK4522 Synth (instrument)

- **Purpose:** a source at its own clock through `DacStream` in pull mode, as an emulated sound chip would use the library.
- **Status:** implemented.
- **Usage:** insert on an instrument track and play MIDI. Choose the source clock (32, 44.1, or 48 kHz, or 112 MHz / 2304 with the extended range), the 20-bit or 16-bit sample format, waveform, attack, and release. All parameters: [parameters.md](parameters.md).
- **Validation** (`ak4522_demo_synth_tests`, 13 test cases, all passing):
  - stereo-output layout only;
  - every source clock prepares at all six host rates, with the extended range flagged only for 112 MHz / 2304;
  - exact silence before the first note;
  - bitwise block-size independence with MIDI at arbitrary offsets, for 44.1 kHz and 112 MHz / 2304 at 44.1 and 96 kHz hosts;
  - a one-host-frame MIDI shift maps to the DAC frame given by the exact ratio;
  - 16-bit output;
  - float and double agreeing within 1e-6;
  - no allocation while rendering, including MIDI and pin changes;
  - state round trip, and rejection of the effect's state;
  - parameter descriptions and groups, the analog group greyed out under Specification, the version line, and the editor opening (see below).

  Also passing: pluginval at strictness level 10 (VST3 and AU), and `auval -strict`. pluginval's one AU warning, "Current program is -1", reflects that the plugins have no factory presets.
- **Limitations:**
  - eight voices; no pitch bend, modulation, or velocity curve;
  - MIDI beyond 1024 queued events per block is dropped and counted.
- **Updated:** 2026-10-18.

## Parameter descriptions

- **Purpose:** say what each control does, and where its behavior comes from, in the plugin and in the docs, without restating the library's documentation.
- **Status:** implemented.
- **Details:**
  - One description per parameter in `Source/common/Descriptions.h`, each labeled as AK4522 behavior, a library feature that is not a chip control, or plugin behavior.
  - The editor shows each description as the tooltip of its control, and groups the controls under headings, such as "Analog imperfections (NominalAnalog only)" and "Plugin (not part of the chip)". The same groups reach hosts through the parameter tree.
  - [parameters.md](parameters.md#what-each-control-does) carries the same wording, with links to the library's documentation at `v0.1.0`.
  - The editor greys out controls that do nothing with the current settings, and says why in their tooltips: the "Analog imperfections" group under the Specification profile, and, in the AK4522 DAC, DC offset when Path is ADC. Each plugin decides this in `inactiveReason()`. Values are kept.
  - The editor is the plugins' own small one: it replaces JUCE's generic editor, which has no tooltips, and keeps the host's right-click menu for each parameter.
  - Like JUCE's generic editor, it registers no parameter listeners: a timer reads the parameters 30 times a second on the message thread. A listener would run on the audio thread under automation, and JUCE's attachments post messages from there, which may block. Edits reach the host as complete change gestures, from the message thread only.
- **Validation:** a test in each test program checks that every parameter is in a group, has a description, and appears in `docs/parameters.md` with the same wording and origin, and that the editor opens. Changing one word in the docs fails the test. Others check that the analog group is greyed out under Specification and active under NominalAnalog, and that the effect's DC offset is greyed out on the ADC path only; making either always active fails its test. Two more check that the editor shows values changed on another thread only when it polls, so a listener added back fails the test, and that edits in the editor reach the parameters with one complete gesture each. pluginval at strictness level 10 (VST3 and AU) and `auval -strict` pass on macOS.
- **Limitations:** tooltips have not been checked in a host. Parameter IDs and saved state are unchanged, so the schema stays at 1.
- **Updated:** 2026-10-18.

## Host smoke tests

- **Purpose:** the gate in the library's demo plugin section: FL Studio and Ableton Live load both plugins, play, automate, save and reload, and the latency agrees with emitted audio.
- **Status:** in progress; run by the project owner.
- **Results reported by the project owner:**

  | Host                          | Version                                | AK4522 DAC | AK4522 Synth | Date reported |
  | ----------------------------- | -------------------------------------- | ---------- | ------------ | ------------- |
  | FL Studio                     | 26.1.7 [build 5421]                    | opens      | opens        | 2026-10-18    |
  | Ableton Live 12 Lite          | 12.4.6 (Build: 2026-09-10_0de5c8fa9a)  | opens      | opens        | 2026-10-18    |

- **Not yet reported:** playback, automation, save and reload, and latency against emitted audio, in either host. Plugin format (VST3 or AU) and operating system were not stated. These results predate the editor's version line, so the plugin build they used is not recorded.
- **Updated:** 2026-10-18.
