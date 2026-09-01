#!/usr/bin/env python3
"""Normalize a CSV or WordTyper-style JSON wordbook for the ESP32 Vocab Lab."""
from __future__ import annotations
import argparse
import csv
import json
from pathlib import Path

ALIASES = {
    "word": ("word", "headword"),
    "phonetic": ("phonetic", "ipa"),
    "meaning": ("meaning", "meaning_cn", "translation", "translations"),
    "example": ("example", "examples", "sentence"),
}


def clean(value) -> str:
    if value is None:
        return ""
    if isinstance(value, list):
        return "; ".join(clean(x) for x in value if clean(x))
    return " ".join(str(value).replace("\r", " ").replace("\n", " ").split())


def pick(row: dict, field: str) -> str:
    lower = {str(k).strip().lower(): v for k, v in row.items()}
    for key in ALIASES[field]:
        if key in lower:
            return clean(lower[key])
    return ""


def iter_csv(path: Path):
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            yield row


def iter_json(path: Path):
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(data, dict):
        data = data.get("words", data.get("items", []))
    if not isinstance(data, list):
        raise ValueError("JSON must be a list or contain a 'words' list")
    for row in data:
        if isinstance(row, dict):
            yield row


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert a wordbook to ESP32 Vocab Lab CSV")
    ap.add_argument("input", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()

    source = iter_json(args.input) if args.input.suffix.lower() == ".json" else iter_csv(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with args.output.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["word", "phonetic", "meaning", "example"])
        for row in source:
            word = pick(row, "word")
            meaning = pick(row, "meaning")
            if not word or not meaning:
                continue
            writer.writerow([word, pick(row, "phonetic"), meaning, pick(row, "example")])
            count += 1
    print(f"Wrote {count} words -> {args.output}")
    return 0 if count else 2


if __name__ == "__main__":
    raise SystemExit(main())
