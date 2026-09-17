#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Select closed-PR, tag, superseded, and oversized Actions caches for cleanup."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Iterable
from pathlib import Path
from typing import Any


MAX_CACHE_BYTES = 700_000_000


def oversized_cache_ids(caches: Iterable[dict[str, Any]]) -> list[int]:
  """Return entries at or above mbo's compressed per-entry ceiling."""
  return [cache["id"] for cache in caches if cache["sizeInBytes"] >= MAX_CACHE_BYTES]


def obsolete_cache_ids(caches: list[dict[str, Any]], closed_prs: set[int]) -> list[int]:
  """Remove closed PRs and tags first, then obsolete generations and oversized entries."""
  closed = []
  candidates = []
  for cache in caches:
    match = re.fullmatch(r"refs/pull/(\d+)/(?:merge|head)", cache["ref"])
    if ((match and int(match[1]) in closed_prs)
        or cache["ref"].startswith(("refs/tags/", "refs/heads/refs/tags/"))):
      closed.append(cache["id"])
    else:
      candidates.append(cache)
  oversized = oversized_cache_ids(candidates)
  generations = {}
  for cache in candidates:
    if cache["id"] in oversized or cache["sizeInBytes"] <= 0:
      continue
    match = re.fullmatch(r"(bazel-actions-v2-.+)-[0-9a-f]{64}-\d+-\d+", cache["key"])
    if match:
      generations.setdefault((cache["ref"], match[1]), []).append(cache)
  superseded = []
  for entries in generations.values():
    entries.sort(key=lambda item: (item["createdAt"], item["id"]), reverse=True)
    superseded.extend(entry["id"] for entry in entries[1:])
  return closed + superseded + oversized


def replaced_cache_ids(caches: list[dict[str, Any]], replacement_key: str) -> list[int]:
  """Retire older main generations only after finding a usable replacement."""
  pattern = r"(bazel-actions-v2-.+)-[0-9a-f]{64}-\d+-\d+"
  match = re.fullmatch(pattern, replacement_key)
  if not match:
    return []
  replacements = [cache for cache in caches
                  if cache["key"] == replacement_key and cache["ref"] == "refs/heads/main"]
  if len(replacements) != 1 or oversized_cache_ids(replacements):
    return []
  replacement = replacements[0]
  if replacement["sizeInBytes"] <= 0:
    return []
  result = []
  for cache in caches:
    candidate = re.fullmatch(pattern, cache["key"])
    if (cache["ref"] == "refs/heads/main" and candidate and candidate[1] == match[1]
        and cache["createdAt"] < replacement["createdAt"]):
      result.append(cache["id"])
  return result


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  mode = parser.add_mutually_exclusive_group(required=True)
  mode.add_argument("--replacement-key", help="Retire older main generations after this upload")
  mode.add_argument("--closed-prs", type=Path,
                      help="Paginated GitHub closed-PR JSON from gh api --paginate --slurp")
  args = parser.parse_args()
  caches = json.load(sys.stdin)
  if not isinstance(caches, list):
    raise ValueError("GitHub cache JSON must be an array")
  if args.replacement_key:
    obsolete = replaced_cache_ids(caches, args.replacement_key)
  else:
    closed_prs = {pr["number"] for page in json.loads(args.closed_prs.read_text()) for pr in page}
    obsolete = obsolete_cache_ids(caches, closed_prs)
  for cache_id in obsolete:
    print(cache_id)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
