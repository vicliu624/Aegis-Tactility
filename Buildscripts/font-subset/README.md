# Runtime CJK Font Subsetting

This directory contains the assets and scripts used to generate the runtime
Chinese font subset for Aegis.

The current strategy is:

1. Keep the runtime font file in the writable FAT data partition instead of
   compiling the entire CJK font into `Tactility.bin`.
2. Keep the Latin UI font stack unchanged and use the runtime font as a CJK
   fallback through LVGL `tiny_ttf`.
3. Keep a curated runtime charset instead of an arbitrary target size. The
   current charset preserves:
   - common Simplified Chinese Han characters
   - all source-font Kana characters
   - all source-font Hangul characters
   - all source-font fullwidth and CJK punctuation
   - the Latin support ranges used by the source font
   - every visible character that appears in the checked-in `.i18n` resources

Files in this directory:

- `sources/`: vendored character list sources used to build the charset.
- `generate_runtime_cjk_charset_common_sc.py`: maintenance script that
  rebuilds the curated runtime charset file.
- `runtime-cjk-charset-common-sc.txt`: generated charset file used by the
  build.
- `subset_runtime_cjk_font.py`: build-time script that subsets the source TTF
  into the runtime font shipped in the data partition.

Character source priority:

1. Simplified Chinese characters from the Common Standard Chinese Characters
   Table (`level-1`, `level-2`, `level-3`).
2. Non-Han support categories from the source font itself:
   - Kana
   - Hangul
   - fullwidth and CJK punctuation
   - Latin support ranges
3. Visible characters already used by the runtime `.i18n` resources.

This keeps the common Simplified Chinese coverage strong, preserves the
characters already used by the firmware, and avoids padding the Han set with
additional characters that are not needed by the current product direction.
