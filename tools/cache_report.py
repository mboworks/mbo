#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Report per-run Bazel uploads and repository-wide compressed cache storage."""

import argparse
import json
from pathlib import Path
import re
import sys

from align_markdown_tables import align_text

REPOSITORY_LIMIT = 10_000_000_000


def uploaded_size(key, inventory):
    matches = [item["sizeInBytes"] for item in inventory
               if item["key"] == key and item["ref"] == "refs/heads/main"]
    return matches[0] if len(matches) == 1 else None


def render(metrics, inventory):
    total = sum(item["sizeInBytes"] for item in inventory)
    remaining = REPOSITORY_LIMIT - total
    lines = ["## Cache storage", "",
             f"Repository caches: **{total:,} bytes ({total / REPOSITORY_LIMIT:.2%} of 10 GB)** "
             f"across {len(inventory):,} entries.", "",
             f"{'Remaining' if remaining >= 0 else 'Over budget'}: **{abs(remaining):,} bytes**.", "",
             "Sizes below are bytes. Repository usage includes all listed caches and any overlapping "
             "generations at this snapshot; artifacts and downloaded toolchains are excluded.", ""]
    if not metrics:
        lines.extend(["No per-job cache measurements were available for this run.", ""])
    else:
        lines.extend(["| Configuration | Before trimming | Uncompressed retained | Compressed upload | Evicted files |",
                      "| :--- | ---: | ---: | ---: | ---: |"])
        for item in sorted(metrics, key=lambda value: value["key"]):
            match = re.fullmatch(r"bazel-actions-v2-(.+)-[0-9a-f]{64}-\d+-\d+", item["key"])
            name = match[1] if match else "Unknown configuration"
            size = item.get("compressed")
            if size is None:
                size = uploaded_size(item["key"], inventory)
            compressed = f"{size:,}" if size is not None else "Unavailable"
            lines.append(f"| {name} | {item['before']:,} | {item['uncompressed']:,} | "
                         f"{compressed} | {item['evicted']:,} |")
        lines.extend(["", "Uncompressed retained is measured before upload, not proof of upload success. "
                      "Unavailable means the exact upload was not found in the inventory. "
                      "Jobs without measurements may have skipped saving or failed before measurement.", ""])
    return align_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--metrics", type=Path)
    mode.add_argument("--record", type=Path)
    parser.add_argument("--inventory", type=Path)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text()) if args.inventory else json.load(sys.stdin)
    if args.record:
        metric = json.loads(args.record.read_text())
        metric["compressed"] = uploaded_size(metric["key"], inventory)
        args.record.write_text(json.dumps(metric) + "\n")
    else:
        metrics = [json.loads(path.read_text()) for path in args.metrics.glob("*.json")]
        print(render(metrics, inventory), end="")


if __name__ == "__main__":
    main()
