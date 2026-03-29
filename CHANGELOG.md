# Changelog

All notable changes to Aegis will be documented in this file.

## 0.0.1-alpha - 2026-03-29

Upstream base: `Tactility v0.7.0-dev`

### Highlights

- Introduced the Aegis product identity, release documentation, and compliance notes.
- Added a LilyGO T-Deck getting-started guide and packaged a releasable T-Deck firmware build.
- Replaced the boot and launcher branding with Aegis PNG assets.
- Added Simplified Chinese support with PSRAM-gated runtime CJK font loading for supported devices.
- Localized core app names and user-facing content for the launcher, files, settings, and settings-category apps.
- Exposed Aegis, upstream base, and ESP-IDF version information in System Info.

### Commit Summary

- `2cd6ab4e` Add T-Deck getting started guide and PNG launcher assets
  - Added a beginner-friendly Windows and ESP-IDF bring-up guide for LilyGO T-Deck.
  - Switched the boot logo and launcher shortcut icons to PNG-based Aegis assets.
  - Added build output ignores for generated build directories.

- `0f33d335` Add Aegis branding, compliance docs, and zh-CN support
  - Introduced the Aegis README, compliance notes, release process notes, and differences-from-Tactility documentation.
  - Added initial `zh-CN` locale resources and runtime language selection support.
  - Updated System Info to show `Aegis v0.0.1-alpha`, `Based on Tactility v0.7.0-dev`, and the ESP-IDF version.

- `b21e793e` Add PSRAM-gated runtime CJK font support
  - Added runtime CJK font subsetting and packaging scripts.
  - Enabled runtime Chinese font loading through `tiny_ttf` with PSRAM preload on supported devices.
  - Gated Chinese language availability and font packaging so non-PSRAM targets keep the previous behavior.

- `bca9f2ca` Add app-level localization for Apps, Files, and Settings
  - Added per-app localization resources for `Apps`, `Files`, and `Settings`.
  - Localized manifest-driven app display names and toolbar titles.
  - Added English and Simplified Chinese resources for Files app user-facing actions and prompts.

- `3018f8bb` Localize settings app content and prompts
  - Added English and Simplified Chinese resources for settings-category apps including Development, Display, GPS, Keyboard, Locale, Power, Time & Date, Trackball, USB, and Wi-Fi.
  - Localized settings labels, prompts, status messages, and option text inside those apps.
  - Added shared localized app-name resolution for settings app entries.

### Supported Device Targets In This Release

- `lilygo-tdeck`

### Release Artifacts

The firmware artifacts prepared for this release are published in:

- `Releases/0.0.1-alpha/lilygo-tdeck/`

This release bundle contains:

- `bootloader.bin`
- `partition-table.bin`
- `Tactility.bin`
- `system.bin`
- `data.bin`
- `flash_args`
- `SHA256SUMS.txt`
- release usage notes

### Notes

- `zh-CN` is enabled only on devices that support PSRAM-backed runtime CJK font loading.
- The release firmware bundle in this version is prepared for `lilygo-tdeck`.