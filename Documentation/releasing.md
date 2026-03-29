# Releasing Aegis

## Scope

This document describes the recommended release process for Aegis firmware builds.

Current baseline:

- Aegis version comes from [version.txt](../version.txt)
- current upstream base: `Tactility v0.7.0-dev`

This process assumes that Aegis firmware binaries may be distributed to users and therefore need
to stay aligned with the repository's licensing and attribution requirements.

## Pre-Release Checklist

1. Update [version.txt](../version.txt) to the intended Aegis release version.
2. Verify that the release notes identify the correct upstream base version.
3. Review [README.md](../README.md), [aegis-compliance.md](aegis-compliance.md), and [aegis-differences.md](aegis-differences.md) for any user-visible changes that need to be documented.
4. Review [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) if new libraries, fonts, icons, or assets were added.
5. Make sure the boot logo, launcher assets, and product naming are Aegis-branded for redistributed builds.
6. Confirm the board bring-up and flashing guides are still correct for supported hardware.

## Build Verification

1. Build the target firmware from a clean working tree or a clearly versioned release branch.
2. Verify that the generated firmware reports the intended Aegis version.
3. Open the System Info About view and confirm it shows:
   - `Aegis v<version>`
   - `Based on Tactility v0.7.0-dev`
   - `ESP-IDF v<major>.<minor>.<patch>`
4. Test the localized UI surfaces that are part of the release.
5. Test the Aegis launcher and any product-specific interaction changes.
6. Test the final built-in app set on the supported device list.

## Release Artifacts

For every public release, keep these artifacts together:

- firmware binaries
- matching source tag or release commit
- release notes
- [LICENSE.md](../LICENSE.md)
- [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)
- flashing/install instructions for supported boards

For T-Deck-based releases, the current board setup guide is:

- [get-started-lilygo-tdeck.md](get-started-lilygo-tdeck.md)

## Source and Tagging

1. Create a Git tag for the release commit.
2. Prefer tags in the format `vX.Y.Z` or `vX.Y.Z-suffix` if your automation depends on the `v` prefix.
3. Ensure the release page or release notes clearly map:
   - Aegis version
   - release tag
   - upstream Tactility base version
   - supported device targets

## Binary Distribution Requirements

If binaries are published outside the repository, make sure users can still obtain:

- the matching source code
- the applicable license texts
- the third-party notice file
- the install or flashing instructions needed to load the firmware onto supported hardware

The easiest way to stay consistent is to publish binaries and source together for each release.

## Release Notes Template

Every release should summarize:

- Aegis version
- upstream Tactility base version
- major UI or launcher changes
- localization changes
- built-in app changes
- supported boards
- known limitations

## Post-Release Checks

1. Verify that the published source matches the binary release.
2. Verify that release notes and version strings match.
3. Verify that download pages do not present the build as an official Tactility release.
4. Verify that documentation links for build, flash, and compliance are reachable.
5. Verify that the release package contains the required license and notice files.
 
