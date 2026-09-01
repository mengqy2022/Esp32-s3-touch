#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convert LCSC/JLC purchase statement CSV/ZIP files to compact UTF-8 INVENTORY.CSV."""
from __future__ import annotations
import argparse, csv, io, zipfile
from pathlib import Path

REQUIRED = ("商品编号", "商品型号", "订购数量")

def decode(raw: bytes) -> str:
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("gb18030")

def extract(raw: bytes, source: str):
    rows = list(csv.reader(io.StringIO(decode(raw))))
    for i, row in enumerate(rows):
        if all(k in row for k in REQUIRED):
            h = row
            p, m, q = (h.index(k) for k in REQUIRED)
            for r in rows[i + 1:]:
                if len(r) <= max(p, m, q):
                    continue
                product_no, model = r[p].strip(), r[m].strip()
                if not product_no and not model:
                    continue
                try:
                    qty = int(float(r[q].strip()))
                except ValueError:
                    continue
                yield product_no, model, qty, source
            return
    raise ValueError(f"{source}: 找不到 商品编号/商品型号/订购数量 表头")

def iter_inputs(path: Path):
    if path.suffix.lower() == ".zip":
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                if name.lower().endswith(".csv"):
                    yield from extract(z.read(name), f"{path.name}:{name}")
    else:
        yield from extract(path.read_bytes(), path.name)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+", type=Path, help="purchase CSV or ZIP")
    ap.add_argument("-o", "--output", type=Path, default=Path("INVENTORY.CSV"))
    args = ap.parse_args()

    merged = {}
    raw_count = 0
    for path in args.inputs:
        for product_no, model, qty, _ in iter_inputs(path):
            raw_count += 1
            key = product_no or model
            if key not in merged:
                merged[key] = [product_no, model, 0]
            merged[key][2] += qty

    def key(v):
        p = v[0]
        if p[:1].upper() == "C" and p[1:].isdigit():
            return (0, int(p[1:]))
        return (1, p, v[1])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["PRODUCT_NO", "MODEL", "QTY"])
        w.writerows(sorted(merged.values(), key=key))
    print(f"OK: {raw_count} lines -> {len(merged)} items -> {args.output}")

if __name__ == "__main__":
    main()
