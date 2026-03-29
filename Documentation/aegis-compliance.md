# Aegis Compliance Notes

## Purpose

This document is an engineering-oriented compliance summary for the Aegis firmware project.

It is not legal advice. Its purpose is to help contributors and release managers ship Aegis in a
way that is consistent with the licenses and attribution requirements already present in this repository.

## Project Status

Aegis is a derivative firmware based on Tactility.

- Aegis product version: `0.0.1-alpha`
- Upstream base: `Tactility v0.7.0-dev`

Aegis is not an official Tactility release.

## License Structure

This repository is not a single-license codebase.

The root [LICENSE.md](../LICENSE.md) explains the current split:

- `Tactility`: GPLv3
- `TactilityCore`: GPLv3
- `Devices/*`: GPLv3
- `Tests`: GPLv3
- `TactilityC`: Apache 2.0
- `TactilityFreeRTOS`: Apache 2.0
- `TactilityKernel`: Apache 2.0
- `Platforms/*`: Apache 2.0
- `Drivers/*`: varies by directory

The license file closest to a directory applies to that directory tree.

## What Aegis Must Keep Doing

### 1. Keep upstream license files

Do not remove or replace the upstream license files already included in the repository.

At minimum, distributed source trees and release archives should continue to include:

- [LICENSE.md](../LICENSE.md)
- [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)
- the license files present inside subprojects and third-party folders

### 2. Preserve notices and attribution

When modifying files that already contain copyright or license notices:

- keep the existing notices intact
- add your own notices where appropriate
- do not rewrite history in a way that obscures the upstream origin

When adding new third-party code, fonts, assets, or libraries:

- verify the incoming license is compatible
- include the relevant license text if required
- update [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)

### 3. Mark Aegis as a modified derivative

Public-facing materials should clearly state that Aegis is based on Tactility.

Recommended wording:

- `Aegis is based on Tactility`
- `Aegis is a derivative firmware built on top of Tactility`

Do not present Aegis as an official Tactility firmware release.

### 4. Provide corresponding source when distributing binaries

If Aegis firmware binaries are distributed to users, the matching source code must also be made
available in a way that stays aligned with the GPL-covered parts of the codebase.

The safest operational model is:

- every published binary maps to a specific Git tag or commit
- the matching source remains accessible
- the build instructions remain accessible
- the flashing/install steps remain accessible

Practical repository assets that already help with this:

- [version.txt](../version.txt)
- [Documentation/get-started-lilygo-tdeck.md](get-started-lilygo-tdeck.md)
- [Documentation/releasing.md](releasing.md)

### 5. Provide installation information for shipped user devices

If Aegis is delivered to end-users on a physical device, contributors should treat flashing and
installation instructions as release artifacts, not optional extras.

At release time, make sure users can obtain:

- the matching source tree
- the firmware images
- the board-specific flashing steps
- the information needed to install a modified build on supported hardware

For current development on the T-Deck, the board bring-up reference is:

- [Documentation/get-started-lilygo-tdeck.md](get-started-lilygo-tdeck.md)

## Branding and Logo Use

The root [LICENSE.md](../LICENSE.md) contains a separate logo policy for the Tactility logo.

Operationally, Aegis should follow this rule:

- use Aegis branding for Aegis firmware builds and redistributed binaries
- do not ship redistributed Aegis builds with the original Tactility logo unless explicit permission has been granted

Using an Aegis-specific logo for Aegis-branded redistributed firmware is the correct direction.

## Runtime Attribution

Aegis does not need to put `Powered by Tactility` on the boot splash screen.

The preferred attribution model is:

- Aegis branding on the boot screen and launcher
- factual upstream attribution in documentation and release notes
- factual upstream attribution in an About or system information view

For the user interface, the recommended About wording is:

- `Aegis v<version>`
- `Based on Tactility v0.7.0-dev`
- `ESP-IDF v<major>.<minor>.<patch>`

## Recommended Release Bundle Contents

Every public Aegis release should include or point to:

- the firmware binaries
- the matching source tag or repository snapshot
- [LICENSE.md](../LICENSE.md)
- [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)
- release notes that describe the Aegis-specific changes
- board-specific installation instructions

## Maintainer Checklist

Before publishing a release, confirm:

- The release identifies itself as Aegis, not as an official Tactility build.
- The Tactility base version is documented.
- The source corresponding to the distributed binaries is accessible.
- The license and third-party notice files are included.
- Newly added third-party components are reflected in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
- Board flashing instructions are up to date.
