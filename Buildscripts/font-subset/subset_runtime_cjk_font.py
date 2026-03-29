#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path


try:
    from fontTools import subset
    from fontTools.ttLib import TTFont
except ImportError as exc:
    raise SystemExit(
        "fonttools is required to build the runtime CJK font subset. "
        "Install it in the active ESP-IDF Python environment with "
        "`python -m pip install fonttools`."
    ) from exc


def load_curated_chars(path: Path) -> list[str]:
    chars: list[str] = []
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        chars.extend(ch for ch in line if ch not in "\r\n\t")
    return chars


def scan_runtime_text_chars(root: Path) -> set[str]:
    chars: set[str] = set()
    for path in sorted(root.rglob("*.i18n")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        chars.update(ch for ch in text if ch not in "\r\n\t")
    return chars


def main() -> int:
    parser = argparse.ArgumentParser(description="Subset the runtime CJK font shipped in the data partition.")
    parser.add_argument("--source-font", required=True, type=Path)
    parser.add_argument("--output-font", required=True, type=Path)
    parser.add_argument("--charset", required=True, type=Path)
    parser.add_argument("--runtime-text-root", required=True, type=Path)
    args = parser.parse_args()

    curated_chars = set(load_curated_chars(args.charset))
    runtime_chars = scan_runtime_text_chars(args.runtime_text_root)
    final_chars = curated_chars | runtime_chars

    font = TTFont(args.source_font)
    try:
        available_chars = {chr(cp) for cp in (font.getBestCmap() or {})}
    finally:
        font.close()

    missing_chars = sorted(final_chars - available_chars)
    if missing_chars:
        preview = "".join(missing_chars[:32])
        raise SystemExit(
            "The source font is missing characters required by the curated runtime charset. "
            f"Missing count: {len(missing_chars)}. Sample: {preview!r}"
        )

    unicodes = sorted({ord(ch) for ch in final_chars})

    args.output_font.parent.mkdir(parents=True, exist_ok=True)

    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.name_legacy = True
    options.notdef_outline = True
    options.recommended_glyphs = True
    options.recalc_timestamp = False
    options.symbol_cmap = True
    options.legacy_cmap = True
    options.hinting = True

    font = subset.load_font(str(args.source_font), options)
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)
    subset.save_font(font, str(args.output_font), options)

    print(
        "Generated runtime CJK subset font "
        f"with {len(curated_chars)} curated chars and {len(final_chars)} total visible chars: {args.output_font}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
