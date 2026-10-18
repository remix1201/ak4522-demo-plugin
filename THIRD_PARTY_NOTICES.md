# Third-party components and licences

Source files marked `SPDX-License-Identifier: GPL-3.0-or-later` are this project's own and are licensed under the GNU General Public License, version 3 or later; see [LICENSE](LICENSE).

| Component | Version and source | Licence |
| --------- | ------------------ | ------- |
| ak4522 library | release `v0.1.0`, https://github.com/remix1201/ak4522, fetched at configure time | GPL-3.0-or-later |
| JUCE | 9.0.4, commit `3fb5cbc0340758596dac39f7c8de47d562b4896e`, fetched at configure time and checked by SHA-256 | used under its AGPLv3 option; see [LICENSES/AGPL-3.0.txt](LICENSES/AGPL-3.0.txt) and [LICENSES/JUCE-9.0.4.md](LICENSES/JUCE-9.0.4.md) |
| JUCE's bundled dependencies | listed in [LICENSES/JUCE-9.0.4.spdx.json](LICENSES/JUCE-9.0.4.spdx.json) | as listed there; keep their notice files when distributing JUCE's source |
| VST3 SDK | bundled with JUCE 9.0.4 | MIT; see [LICENSES/VST3-SDK-MIT.txt](LICENSES/VST3-SDK-MIT.txt) |
| Catch2 | v3.16.0, fetched for the tests only | BSL-1.0; not part of the plugins |

The plugins combine GPL-3.0-or-later code with JUCE under the AGPLv3. GPLv3 section 13 permits that combination, and distributed plugin binaries are then covered by the AGPLv3 terms as well. Anyone who distributes a binary must also offer the complete corresponding source: this repository at the matching commit, the pinned dependencies, and the build scripts, together with these notices. A binary on its own is not a complete distribution.

This notice summarizes; it does not replace the licence texts.

AKM and AK4522 identify the hardware the library models. This is an independent project, not affiliated with or endorsed by the manufacturer.
