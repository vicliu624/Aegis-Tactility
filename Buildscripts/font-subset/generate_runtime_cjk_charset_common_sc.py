#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Iterable

from fontTools.ttLib import TTFont


def is_han(ch: str) -> bool:
    cp = ord(ch)
    return (
        0x3400 <= cp <= 0x4DBF
        or 0x4E00 <= cp <= 0x9FFF
        or 0xF900 <= cp <= 0xFAFF
        or 0x20000 <= cp <= 0x2EBEF
    )


def classify_char(ch: str) -> str:
    cp = ord(ch)

    if is_han(ch):
        return "han"
    if 0x3040 <= cp <= 0x30FF or 0x31F0 <= cp <= 0x31FF:
        return "kana"
    if 0x1100 <= cp <= 0x11FF or 0x3130 <= cp <= 0x318F or 0xAC00 <= cp <= 0xD7AF:
        return "hangul"
    if 0x3000 <= cp <= 0x303F or 0xFF00 <= cp <= 0xFFEF:
        return "fullwidth_cjk_punct"
    if cp <= 0x024F or 0x1E00 <= cp <= 0x1EFF:
        return "latin"
    return "other"


def ordered_unique(chars: Iterable[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for ch in chars:
        if ch in seen:
            continue
        seen.add(ch)
        out.append(ch)
    return out


def parse_plain_han_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    return [ch for ch in text if is_han(ch)]


def scan_runtime_i18n_chars(root: Path) -> list[str]:
    chars: list[str] = []
    for path in sorted(root.rglob("*.i18n")):
        text = path.read_text(encoding="utf-8", errors="ignore")
        chars.extend(ch for ch in text if ch not in "\r\n\t")
    return chars


def iter_source_font_chars(font_path: Path) -> list[str]:
    font = TTFont(font_path)
    try:
        cmap = font.getBestCmap() or {}
        return [chr(cp) for cp in sorted(cmap)]
    finally:
        font.close()


def write_charset(path: Path, chars: list[str], metadata: list[str]) -> None:
    lines = ["# " + line for line in metadata]
    lines.append("")
    chunk_size = 100
    for index in range(0, len(chars), chunk_size):
        lines.append("".join(chars[index : index + chunk_size]))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the curated runtime CJK charset file for Aegis."
    )
    parser.add_argument("--source-font", required=True, type=Path)
    parser.add_argument("--sources-dir", required=True, type=Path)
    parser.add_argument("--runtime-text-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    simplified_files = [
        args.sources_dir / "common-standard-level-1.txt",
        args.sources_dir / "common-standard-level-2.txt",
        args.sources_dir / "common-standard-level-3.txt",
    ]
    source_font_chars = iter_source_font_chars(args.source_font)
    available_chars = set(source_font_chars)
    available_han = {ch for ch in source_font_chars if is_han(ch)}

    simplified = ordered_unique(
        ch for path in simplified_files for ch in parse_plain_han_file(path) if ch in available_han
    )

    runtime_chars_all = ordered_unique(scan_runtime_i18n_chars(args.runtime_text_root))
    missing_runtime_chars = [ch for ch in runtime_chars_all if ch not in available_chars]
    if missing_runtime_chars:
        preview = "".join(missing_runtime_chars[:32])
        raise SystemExit(
            "The source font is missing characters already used by the runtime i18n resources. "
            f"Missing count: {len(missing_runtime_chars)}. Sample: {preview!r}"
        )

    runtime_chars = ordered_unique(ch for ch in runtime_chars_all if ch in available_chars)

    support_categories = {"latin", "kana", "hangul", "fullwidth_cjk_punct"}
    support_chars = ordered_unique(
        ch for ch in source_font_chars if classify_char(ch) in support_categories
    )

    final_chars = ordered_unique(simplified + support_chars + runtime_chars)

    counts = {
        "han": sum(1 for ch in final_chars if classify_char(ch) == "han"),
        "kana": sum(1 for ch in final_chars if classify_char(ch) == "kana"),
        "hangul": sum(1 for ch in final_chars if classify_char(ch) == "hangul"),
        "fullwidth_cjk_punct": sum(
            1 for ch in final_chars if classify_char(ch) == "fullwidth_cjk_punct"
        ),
        "latin": sum(1 for ch in final_chars if classify_char(ch) == "latin"),
        "other": sum(1 for ch in final_chars if classify_char(ch) == "other"),
    }

    metadata = [
        "Generated file. Do not edit by hand.",
        "Rule: common simplified Han + source-font support categories + runtime visible chars",
        f"Simplified source count: {len(simplified)}",
        f"Runtime visible char count: {len(runtime_chars)}",
        f"Final Han count: {counts['han']}",
        f"Final Kana count: {counts['kana']}",
        f"Final Hangul count: {counts['hangul']}",
        f"Final fullwidth/CJK punctuation count: {counts['fullwidth_cjk_punct']}",
        f"Final Latin count: {counts['latin']}",
        f"Final other count: {counts['other']}",
        "Priority order: common simplified -> source-font support categories -> runtime visible chars",
    ]
    write_charset(args.output, final_chars, metadata)

    print(
        "Wrote curated runtime charset "
        f"with {len(final_chars)} visible chars and {counts['han']} Han chars to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
