#!/usr/bin/env python3
"""Reject pull-request changes that weaken the repository coverage contract."""

from __future__ import annotations

import argparse
import difflib
import fnmatch
import json
import re
from pathlib import Path

import coverage as coverage_tool


_COVERAGE_EXCLUSION = re.compile(r"\bLCOV_EXCL_[A-Z_]+")
_SOURCE_SUFFIXES = frozenset(
    {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".mope"}
)
_ALLOWED_EXCLUDE_ADDITIONS = frozenset({"mbo/**/*_test_util.h"})


def scope_regressions(candidate: dict, base: dict) -> list[str]:
    """Reports changes to the set of source files measured by coverage."""
    candidate_scope = coverage_tool.baseline_scope(candidate)
    base_scope = coverage_tool.baseline_scope(base)
    candidate_excludes = set(candidate_scope.pop("exclude"))
    base_excludes = set(base_scope.pop("exclude"))
    added_excludes = candidate_excludes - base_excludes
    if candidate_scope != base_scope or not added_excludes <= _ALLOWED_EXCLUDE_ADDITIONS:
        return ["coverage measurement scope was changed"]
    return []


def source_exclusion_regressions(
    candidate_root: Path, base_root: Path, policy: dict
) -> list[str]:
    """Reports added or modified source-owned coverage exclusions."""
    includes = policy.get("include", ["mbo/**"])
    excludes = policy.get("exclude", [])
    result = []
    for candidate in sorted(
        path
        for path in candidate_root.rglob("*")
        if path.is_file() and path.suffix in _SOURCE_SUFFIXES
    ):
        logical = "mbo/" + candidate.relative_to(candidate_root).as_posix()
        if not any(fnmatch.fnmatchcase(logical, pattern) for pattern in includes):
            continue
        if any(fnmatch.fnmatchcase(logical, pattern) for pattern in excludes):
            continue
        candidate_lines = candidate.read_text(encoding="utf-8").splitlines()
        base = base_root / candidate.relative_to(candidate_root)
        base_lines = base.read_text(encoding="utf-8").splitlines() if base.is_file() else []
        matcher = difflib.SequenceMatcher(a=base_lines, b=candidate_lines, autojunk=False)
        for operation, _, _, candidate_start, candidate_end in matcher.get_opcodes():
            if operation == "equal" or operation == "delete":
                continue
            for index in range(candidate_start, candidate_end):
                match = _COVERAGE_EXCLUSION.search(candidate_lines[index])
                if match:
                    result.append(
                        f"source coverage exclusion was added or changed: "
                        f"{logical}:{index + 1}: {match.group()}"
                    )
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-policy", type=Path, required=True)
    parser.add_argument("--base-baseline", type=Path, required=True)
    parser.add_argument("--candidate-policy", type=Path, required=True)
    parser.add_argument("--candidate-baseline", type=Path, required=True)
    parser.add_argument("--base-source", type=Path, required=True)
    parser.add_argument("--candidate-source", type=Path, required=True)
    args = parser.parse_args(argv)

    base_policy = json.loads(args.base_policy.read_text(encoding="utf-8"))
    base_baseline = json.loads(args.base_baseline.read_text(encoding="utf-8"))
    candidate_policy = json.loads(args.candidate_policy.read_text(encoding="utf-8"))
    candidate_baseline = json.loads(args.candidate_baseline.read_text(encoding="utf-8"))

    errors = (
        scope_regressions(candidate_policy, base_policy)
        + source_exclusion_regressions(args.candidate_source, args.base_source, base_policy)
        + coverage_tool.policy_regressions(candidate_policy, base_policy)
        + coverage_tool.baseline_regressions(candidate_baseline, base_baseline)
    )
    for error in errors:
        print(f"coverage integrity failed: {error}")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
