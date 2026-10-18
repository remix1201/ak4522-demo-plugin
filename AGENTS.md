# AGENTS.md: ak4522 demo plugins

## 1. What this project is

Two JUCE plugins that show the [ak4522](https://github.com/remix1201/ak4522) library in use, as any outside user would use it:

- **AK4522 DAC** (`Source/effect/`): an audio effect built on `ak4522::HostProcessor`.
- **AK4522 Synth** (`Source/synth/`): an instrument built on `ak4522::DacStream`. It is a small test synth that runs at its own clock.

The library is the product; this project is one of its users. It defines no chip behavior. A gap found here is fixed in the library repository, released, and then picked up here by moving the pinned tag.

## 2. Rules

- The library is consumed only through a tagged release, by FetchContent (`GIT_TAG vX.Y.Z`) or `find_package`. Never use its private headers (`src/`), its build tree, or an untagged commit in a committed build. `FETCHCONTENT_SOURCE_DIR_AK4522` is for local testing of a library change before it is released, never for a commit here.
- JUCE is pinned by commit and SHA-256 in `CMakeLists.txt`, and used under its AGPLv3 option. Update `LICENSES/` and `THIRD_PARTY_NOTICES.md` whenever the JUCE pin moves.
- Chip behavior comes from the library. Anything the plugins add is host behavior: output gain, mix, delta listen, bypass, the test synth, and the parameter-to-event mapping. Label it so in the docs, and never present it as AK4522 behavior.
- The library's adapter requirements apply here: REQ-VST-01, REQ-VST-02, and REQ-LAT-03 in the library's AGENTS.md. In short:
  - stereo only, and float and double processing;
  - versioned state, with incompatible state rejected rather than reinterpreted;
  - the reported latency matches the emitted audio;
  - latency-compensated bypass and mix;
  - a declared tail, infinite while noise plays;
  - no silent-input shortcuts.
- Real-time safety: no allocation, locks, I/O, or blocking on the audio thread. Configuration changes re-prepare the chip on the message thread, under `suspendProcessing`.
- Saved state carries `schema`. Bump `schemaVersion` in `Source/common/ChipPlugin.h` whenever a parameter is added, removed, or changes meaning, or its ID changes, and update `docs/parameters.md` in the same change. Changing a parameter's display name, description, or group does not change the schema. Alpha: no migration of older state.
- Each parameter has one description, in `Source/common/Descriptions.h`. It is the editor's tooltip, and the section "What each control does" in `docs/parameters.md` uses the same wording; the tests check this.
  - Every description is labeled with its origin: AK4522 behavior, a library feature that is not a chip control, or plugin behavior.
  - A chip or library description is one line. `docs/parameters.md` links it to the library's documentation at the pinned tag, and it never restates chip behavior beyond that line. If the library's documentation is missing or unclear, fix it in the library, not here.
  - A plugin description is written here in full and says it is plugin behavior, not AK4522 behavior.
  - Tooltips carry no links; the links are in `docs/parameters.md` only.
  - When the pinned tag moves, update the links and check that the descriptions still match the library.
- Keep `docs/parameters.md`, `docs/features.md`, `README.md`, and the parameter descriptions in step with the code in every branch.
- Create a feature branch for each change. Merge and push only when the project owner asks.
- Commit messages and PR descriptions carry no AI attribution trailers.
- Documentation uses the Oxford comma and no em dashes.

## 3. Gates

A change is complete when all of these pass:

1. The build, with JUCE's recommended warning flags, and no warnings in project code.
2. `ctest --test-dir build` (both test programs).
3. pluginval at strictness level 10 on both VST3 plugins, and on macOS both AU plugins too.
4. `auval` on both AU plugins (macOS).
5. CI on macOS and Windows.

Smoke tests in hosts (FL Studio and Ableton Live) are done by the project owner. Record their results in `docs/features.md` with the host version and the plugin's version line (shown at the bottom of the editor), and never record a host result that was not reported.
