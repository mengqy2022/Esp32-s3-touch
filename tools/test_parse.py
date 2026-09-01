#!/usr/bin/env python3
"""Small repository-local regression test for the standard inventory CSV parser."""
from __future__ import annotations
import csv
import io
from pathlib import Path
import sys

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
import inventory_tool as it  # noqa: E402

path = ROOT / "sdcard_template" / "INVENTORY.CSV"
raw = path.read_bytes()
text = it.decode_bytes(raw)
reader = csv.reader(io.StringIO(text), delimiter=it.sniff_delimiter(text))
rows = list(it.parse_rows_from_reader(reader))
fmt = it.detect_format(rows)
assert fmt == "standard", f"unexpected format: {fmt}"
items = it.parse_standard(rows)
assert items, "no inventory rows parsed"
assert items[0]["qty"] > 0
print(f"OK: {len(items)} standard inventory rows parsed from {path.name}")
