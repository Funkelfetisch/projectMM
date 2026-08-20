#!/usr/bin/env python3
"""Fetch selected Electromage PatternDB sources for PixelblazeCompatEffect."""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path


PATTERNDB = "https://patterndb.electromage.com"


def fetch_json(url: str) -> object:
    req = urllib.request.Request(url, headers={"User-Agent": "projectMM-pattern-importer"})
    with urllib.request.urlopen(req, timeout=20) as response:
        return json.loads(response.read().decode("utf-8"))


def records_from_top(limit: int) -> list[dict]:
    data = fetch_json(f"{PATTERNDB}/top")
    records: list[dict] = []
    for key in ("popular", "newest"):
        for record in data.get(key, []):
            if len(records) >= limit:
                return records
            records.append(record)
    return records


def normalize(record: dict) -> dict:
    file_obj = record.get("file") or {}
    sources = file_obj.get("sources") or {}
    source = sources.get("main") or ""
    description = record.get("description") or file_obj.get("description") or ""
    license_hint = "unknown"
    lowered = f"{source}\n{description}".lower()
    if "mit license" in lowered:
        license_hint = "MIT mentioned in source"
    elif "gpl" in lowered:
        license_hint = "GPL mentioned in source"
    return {
        "id": record.get("id") or file_obj.get("id"),
        "name": record.get("name") or file_obj.get("name"),
        "votes": record.get("votes", 0),
        "updatedAt": record.get("updatedAt"),
        "licenseHint": license_hint,
        "sources": {"main": source},
        "description": description,
        "sourceUrl": f"https://electromage.com/patterns/{record.get('id') or file_obj.get('id')}",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--top", type=int, default=25, help="number of top PatternDB records to fetch")
    parser.add_argument("--out", type=Path, default=Path("electromage-patterns.json"))
    args = parser.parse_args()

    try:
        records = [normalize(record) for record in records_from_top(args.top)]
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        print(f"import failed: {exc}", file=sys.stderr)
        return 1

    args.out.write_text(json.dumps({"source": PATTERNDB, "patterns": records}, indent=2), encoding="utf-8")
    print(f"wrote {len(records)} patterns to {args.out}")
    print("Review each pattern's licenseHint before redistributing a saved bundle.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
