#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Bound a stopped Bazel disk cache before uploading it to GitHub Actions."""

import argparse
import json
from pathlib import Path


MAX_BYTES = 600_000_000


def trim(root: Path, max_bytes: int = MAX_BYTES) -> tuple[int, int, int]:
    """Evict largest blobs first, preserving more reusable outputs within the budget."""
    if root.is_symlink():
        raise ValueError("cache root must not be a symlink")
    files = sorted((-path.stat().st_size, path.stat().st_mtime_ns, path.as_posix(), path)
                   for part in ("ac", "cas") if not (root / part).is_symlink()
                   for path in (root / part).rglob("*")
                   if path.is_file() and not path.is_symlink())
    before = sum(-negative_size for negative_size, _, _, _ in files)
    remaining = before
    removed = 0
    for negative_size, _, _, path in files:
        if remaining <= max_bytes:
            break
        path.unlink()
        remaining += negative_size
        removed += 1
    return before, remaining, removed


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--max-bytes", type=int, default=MAX_BYTES)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--key", default="")
    args = parser.parse_args()
    if args.max_bytes < 0:
        parser.error("--max-bytes must be nonnegative")
    before, after, removed = trim(args.cache, args.max_bytes)
    if args.report:
        args.report.write_text(json.dumps({"key": args.key, "before": before, "uncompressed": after,
                                           "evicted": removed, "limit": args.max_bytes}) + "\n")
    print(f"Bazel disk cache: {before:,} -> {after:,} bytes; evicted {removed} files")


if __name__ == "__main__":
    main()
