# Aegis

## Overview

Aegis is a custom ESP32 firmware based on the Tactility project.

This repository is not an official Tactility release. It is a derivative firmware that keeps
the upstream Tactility platform as its base while adapting the user experience and built-in
feature set for Aegis product requirements.

The current Aegis work focuses on:

- Simplified Chinese localization support
- A custom launcher and interaction flow
- A curated built-in application set
- Aegis branding, assets, and product-specific UX changes

## Upstream Relationship

Aegis is currently based on Tactility `v0.7.0-dev`.

When describing the project publicly, the recommended wording is:

- `Aegis is based on Tactility`
- `Aegis is a derivative firmware built on top of Tactility`

Avoid presenting Aegis as an official Tactility firmware release.

## Getting Started

The main documented bring-up flow in this repository is:

- [LilyGO T-Deck Getting Started](Documentation/get-started-lilygo-tdeck.md)

Additional project documentation:

- [Aegis Compliance Notes](Documentation/aegis-compliance.md)
- [Aegis Differences From Tactility](Documentation/aegis-differences.md)
- [Release Process](Documentation/releasing.md)
- [Third-Party Notices](THIRD-PARTY-NOTICES.md)

## Versioning

- Current Aegis version: `0.0.1-alpha`
- Upstream base version: `Tactility v0.7.0-dev`

The Aegis product version is defined in [version.txt](version.txt).

## Licensing

This repository contains a mix of GPLv3 and Apache 2.0 subprojects inherited from Tactility.
The exact license depends on the directory or subproject being used.

Start here:

- [License Grant and Project Overview](LICENSE.md)
- [Third-Party Notices](THIRD-PARTY-NOTICES.md)

If you distribute Aegis firmware binaries, review the release and compliance documents before shipping:

- [Aegis Compliance Notes](Documentation/aegis-compliance.md)
- [Release Process](Documentation/releasing.md)
