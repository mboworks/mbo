#!/usr/bin/env python3
"""Reject pull-request changes that weaken the repository coverage contract."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import coverage as coverage_tool


def scope_regressions(candidate: dict, base: dict) -> list[str]:
    """Reports changes to the set of source files measured by coverage."""
    candidate_scope = coverage_tool.baseline_scope(candidate)
    base_scope = coverage_tool.baseline_scope(base)
    return [] if candidate_scope == base_scope else ["coverage measurement scope was changed"]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-policy", type=Path, required=True)
    parser.add_argument("--base-baseline", type=Path, required=True)
    parser.add_argument("--candidate-policy", type=Path, required=True)
    parser.add_argument("--candidate-baseline", type=Path, required=True)
    args = parser.parse_args(argv)

    base_policy = json.loads(args.base_policy.read_text(encoding="utf-8"))
    base_baseline = json.loads(args.base_baseline.read_text(encoding="utf-8"))
    candidate_policy = json.loads(args.candidate_policy.read_text(encoding="utf-8"))
    candidate_baseline = json.loads(args.candidate_baseline.read_text(encoding="utf-8"))

    errors = (
        scope_regressions(candidate_policy, base_policy)
        + coverage_tool.policy_regressions(candidate_policy, base_policy)
        + coverage_tool.baseline_regressions(candidate_baseline, base_baseline)
    )
    for error in errors:
        print(f"coverage integrity failed: {error}")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
