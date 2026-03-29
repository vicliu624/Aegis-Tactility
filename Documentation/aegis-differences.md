# Aegis Differences From Tactility

## Purpose

This document summarizes the major ways Aegis currently differs from its upstream base, Tactility.

Current baseline:

- Aegis version: `0.0.1-alpha`
- Upstream base: `Tactility v0.7.0-dev`

## Branding and Identity

Aegis uses its own product identity.

Current branding differences include:

- Aegis product naming
- Aegis boot logo and related startup assets
- Aegis-specific visual direction for user-facing surfaces

The project should not be presented as an official Tactility release.

## Localization

Aegis adds Simplified Chinese localization work on top of the current Tactility localization system.

Current direction:

- `zh-CN` has been added as a supported locale in the repository
- translation resources are generated and shipped through the existing `.i18n` pipeline
- the language settings flow has been extended to expose `zh-CN`

Important note:

- localization coverage is still constrained by the current upstream architecture
- screens that still rely on hardcoded English strings must be migrated to the i18n pipeline before they become fully localizable
- CJK font support must be kept aligned with the active UI font sizes used by the product

## Launcher and Interaction Model

Aegis does not keep the upstream launcher experience unchanged.

Current launcher-related differences include:

- a custom launcher layout
- Aegis-owned launcher assets
- a different visual structure from the upstream launcher
- product-specific interaction adjustments

As a result, users should not assume that upstream Tactility screenshots or walkthroughs will always match the Aegis UI.

## Built-In Application Set

Aegis is expected to tailor the built-in app set to product needs.

This means the project may:

- keep some upstream apps unchanged
- modify some built-in apps
- remove apps that do not fit the Aegis product direction
- add or expose apps differently from upstream

Any user-facing app availability differences should be reflected in release notes and support documentation.

## System Information and Runtime Attribution

The preferred runtime attribution model in Aegis is:

- display the Aegis version as the primary product version
- display the upstream Tactility base version as factual project attribution
- display the active ESP-IDF version used for the build

This keeps the product identity clear while preserving technical traceability.

## Release and Support Implications

Because Aegis is diverging in localization, branding, and launcher behavior:

- release notes must describe user-visible changes
- support documentation must reference Aegis UI behavior rather than relying solely on upstream guides
- internal testing should verify Aegis-specific app sets and navigation paths, not only upstream behavior

## Planned Areas of Continued Divergence

These are the expected long-term areas where Aegis may continue to differ from upstream:

- localization coverage and CJK font support
- launcher design and navigation behavior
- built-in app composition
- product branding and user-visible assets
- board-specific defaults and product-specific workflow tuning
