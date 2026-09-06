#!/usr/bin/env python3
"""Apply the repository coverage policy to a Bazel LCOV report."""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

import coverage_policy


@dataclass
class FileCoverage:
    lines: dict[int, int] = field(default_factory=dict)
    functions: list[tuple[int, int]] = field(default_factory=list)
    branches: list[tuple[int, bool]] = field(default_factory=list)


def _repo_path(value: str) -> str | None:
    value = value.replace("\\", "/")
    marker = "/mbo/"
    if value.startswith("mbo/"):
        return value
    if marker in value:
        return "mbo/" + value.split(marker, 1)[1]
    return None


def _branch_merge_markers(source_lines: list[str]) -> dict[int, tuple[int, ...]]:
    """Maps instrumented lines to acceptable logical branch widths."""
    result: dict[int, tuple[int, ...]] = {}
    for index, source_line in enumerate(source_lines):
        match = re.search(r"LCOV_MERGE_BR_LINE\s+(\d+(?:\s*,\s*\d+)*)", source_line)
        if not match:
            continue
        widths = tuple(int(value.strip()) for value in match.group(1).split(","))
        result[index + 1] = widths
        if not source_line.lstrip().startswith("//"):
            continue
        for continuation in range(index + 1, len(source_lines)):
            result[continuation + 1] = widths
            if "{" in source_lines[continuation]:
                break
    return result


def parse_lcov(path: Path, source_root: Path = Path(".")) -> dict[str, FileCoverage]:
    result: dict[str, FileCoverage] = {}
    current: FileCoverage | None = None
    function_lines: dict[str, int] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("SF:"):
            name = _repo_path(raw[3:])
            current = result.setdefault(name, FileCoverage()) if name else None
            function_lines = {}
        elif current is not None and raw.startswith("FN:"):
            definition = raw[3:].split(",")
            function_lines[definition[-1]] = int(definition[0])
        elif current is not None and raw.startswith("DA:"):
            line, hits, *_ = raw[3:].split(",")
            current.lines[int(line)] = current.lines.get(int(line), 0) + int(hits)
        elif current is not None and raw.startswith("FNDA:"):
            hits, name = raw[5:].split(",", 1)
            current.functions.append((function_lines.get(name, 0), int(hits)))
        elif current is not None and raw.startswith("BRDA:"):
            line, _, _, taken = raw[5:].split(",")
            current.branches.append((int(line), taken not in ("-", "0")))
        elif raw == "end_of_record":
            current = None
    for name, data in result.items():
        source = source_root / name
        if not source.is_file():
            continue
        source_lines = source.read_text(encoding="utf-8").splitlines()
        excluded_lines = {
            number
            for number, line in enumerate(source_lines, start=1)
            if "LCOV_EXCL_LINE" in line
        }
        excluded_branches = excluded_lines | {
            number
            for number, line in enumerate(source_lines, start=1)
            if "LCOV_EXCL_BR_LINE" in line
        }
        excluded_functions: set[int] = set()
        merged_function_groups: dict[int, int] = {}
        merged_branch_groups = _branch_merge_markers(source_lines)
        for index, line in enumerate(source_lines):
            exclude = "LCOV_EXCL_FUNC_LINE" in line
            merge = "LCOV_MERGE_FUNC_LINE" in line
            if not exclude and not merge:
                continue
            for continuation in range(index, len(source_lines)):
                if exclude:
                    excluded_functions.add(continuation + 1)
                if merge:
                    merged_function_groups[continuation + 1] = index + 1
                if "{" in source_lines[continuation]:
                    break
        data.lines = {line: hits for line, hits in data.lines.items() if line not in excluded_lines}
        data.functions = [function for function in data.functions if function[0] not in excluded_functions]
        merged_hits: dict[int, int] = {}
        ordinary_functions: list[tuple[int, int]] = []
        for line, hits in data.functions:
            if line in merged_function_groups:
                group = merged_function_groups[line]
                merged_hits[group] = max(merged_hits.get(group, 0), hits)
            else:
                ordinary_functions.append((line, hits))
        data.functions = ordinary_functions + sorted(merged_hits.items())
        ordinary_branches: list[tuple[int, bool]] = []
        branches_by_line: dict[int, list[bool]] = defaultdict(list)
        for line, taken in data.branches:
            if line in excluded_branches:
                continue
            if line in merged_branch_groups:
                branches_by_line[line].append(taken)
            else:
                ordinary_branches.append((line, taken))
        for line, branches in sorted(branches_by_line.items()):
            widths = merged_branch_groups[line]
            width = next(
                (
                    candidate
                    for candidate in sorted(widths, reverse=True)
                    if candidate > 0 and len(branches) % candidate == 0
                ),
                None,
            )
            if width is None:
                raise ValueError(
                    f"{name}:{line}: LCOV_MERGE_BR_LINE "
                    f"{','.join(str(candidate) for candidate in widths)} cannot merge "
                    f"{len(branches)} branch records"
                )
            ordinary_branches.extend(
                (line, any(branches[index::width])) for index in range(width)
            )
        data.branches = ordinary_branches
    return result


def _matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def select_files(report: dict[str, FileCoverage], policy: dict) -> dict[str, FileCoverage]:
    include = policy.get("include", ["mbo/**"])
    exclude = policy.get("exclude", [])
    return {
        path: data
        for path, data in report.items()
        if _matches(path, include) and not _matches(path, exclude)
    }


def counts(files: dict[str, FileCoverage], changed: dict[str, set[int]] | None = None) -> dict:
    values = {"lines": [0, 0], "functions": [0, 0], "branches": [0, 0]}
    for path, data in files.items():
        lines = changed.get(path, set()) if changed is not None else None
        for line, hits in data.lines.items():
            if lines is None or line in lines:
                values["lines"][1] += 1
                values["lines"][0] += hits > 0
        if changed is None:
            values["functions"][1] += len(data.functions)
            values["functions"][0] += sum(hits > 0 for _, hits in data.functions)
        for line, taken in data.branches:
            if lines is None or line in lines:
                values["branches"][1] += 1
                values["branches"][0] += taken
    return {
        metric: {
            "covered": value[0],
            "total": value[1],
            "percent": round(100.0 * value[0] / value[1], 2) if value[1] else None,
        }
        for metric, value in values.items()
    }


def changed_lines(base: str) -> dict[str, set[int]]:
    diff = subprocess.run(
        ["git", "diff", "--unified=0", f"{base}...HEAD", "--", "mbo"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    changed: dict[str, set[int]] = defaultdict(set)
    path: str | None = None
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            path = line[6:]
        elif path and line.startswith("@@"):
            match = re.search(r"\+(\d+)(?:,(\d+))?", line)
            if match:
                start, length = int(match.group(1)), int(match.group(2) or 1)
                changed[path].update(range(start, start + length))
    return changed


def measurements(files: dict[str, FileCoverage], policy: dict) -> dict:
    result = {"overall": counts(files)}
    for name, category in policy.get("categories", {}).items():
        selected = {p: d for p, d in files.items() if _matches(p, category["include"])}
        result[name] = counts(selected)
    return result


def baseline_scope(policy: dict) -> dict:
    """Returns the measurement scope that must match a recorded baseline."""
    return {
        "include": policy.get("include", ["mbo/**"]),
        "exclude": policy.get("exclude", []),
        "categories": {
            name: {"include": category["include"]}
            for name, category in policy.get("categories", {}).items()
        },
    }


def baseline_failures(measured: dict, baseline: dict, policy: dict) -> list[str]:
    """Reports measurement regressions and incompatible baseline data."""
    if baseline.get("schema") != 2:
        return ["schema is not 2; regenerate coverage_baseline.json"]
    if baseline.get("scope") != baseline_scope(policy):
        return ["measurement scope differs from coverage_policy.json; regenerate the baseline"]
    recorded = baseline.get("measurements")
    if not isinstance(recorded, dict):
        return ["measurements are missing; regenerate coverage_baseline.json"]
    tolerances = coverage_policy.baseline_tolerances(policy)
    result = []
    for category, metrics in measured.items():
        previous = recorded.get(category)
        if not isinstance(previous, dict):
            result.append(f"{category}: category is missing from the baseline")
            continue
        for metric in coverage_policy.METRICS:
            previous_metric = previous.get(metric)
            if not isinstance(previous_metric, dict) or "percent" not in previous_metric:
                result.append(f"{category} {metric}: metric is missing from the baseline")
                continue
            expected = previous_metric["percent"]
            actual = metrics[metric]["percent"]
            if expected is None:
                continue
            if not isinstance(expected, (int, float)) or isinstance(expected, bool):
                result.append(f"{category} {metric}: baseline percentage is not a number")
                continue
            if actual is None:
                result.append(f"{category} {metric}: no data; baseline is {expected:.2f}%")
                continue
            tolerance = tolerances[metric]
            if expected - actual > tolerance + 1e-9:
                result.append(
                    f"{category} {metric}: {actual:.2f}% is below the {expected:.2f}% baseline "
                    f"by more than {tolerance:.2f} percentage points"
                )
    return result


def policy_regressions(candidate: dict, base: dict) -> list[str]:
    """Reports coverage enforcement weakened relative to the base branch."""
    candidate_policies = coverage_policy.policies(candidate)
    base_policies = coverage_policy.policies(base)
    result = []
    for category, base_metrics in base_policies.items():
        candidate_metrics = candidate_policies.get(category)
        if candidate_metrics is None:
            result.append(f"{category}: category was removed from the coverage policy")
            continue
        for metric, base_value in base_metrics.items():
            candidate_value = candidate_metrics[metric]
            base_boundary = (
                base_value.minimum if base_value.enforce == "medium" else base_value.target
            )
            candidate_boundary = (
                candidate_value.minimum
                if candidate_value.enforce == "medium"
                else candidate_value.target
            )
            if candidate_boundary < base_boundary:
                result.append(
                    f"{category} {metric}: enforced boundary was lowered from "
                    f"{base_boundary:g}% to {candidate_boundary:g}%"
                )
    candidate_tolerances = coverage_policy.baseline_tolerances(candidate)
    for metric, base_tolerance in coverage_policy.baseline_tolerances(base).items():
        candidate_tolerance = candidate_tolerances[metric]
        if candidate_tolerance > base_tolerance:
            result.append(
                f"{metric}: baseline tolerance was raised from {base_tolerance:g} to "
                f"{candidate_tolerance:g} percentage points"
            )
    return result


def baseline_regressions(candidate: dict, base: dict) -> list[str]:
    """Reports stored baseline percentages lowered relative to the base branch."""
    candidate_measurements = candidate.get("measurements", {})
    result = []
    for category, base_metrics in base.get("measurements", {}).items():
        candidate_metrics = candidate_measurements.get(category)
        if not isinstance(candidate_metrics, dict):
            result.append(f"{category}: category was removed from the coverage baseline")
            continue
        for metric, base_value in base_metrics.items():
            candidate_value = candidate_metrics.get(metric)
            if not isinstance(candidate_value, dict) or "percent" not in candidate_value:
                result.append(f"{category} {metric}: metric was removed from the coverage baseline")
                continue
            base_percent = base_value.get("percent")
            candidate_percent = candidate_value["percent"]
            if base_percent is None:
                continue
            if candidate_percent is None or candidate_percent < base_percent:
                rendered = "no data" if candidate_percent is None else f"{candidate_percent:.2f}%"
                result.append(
                    f"{category} {metric}: stored baseline was lowered from "
                    f"{base_percent:.2f}% to {rendered}"
                )
    return result


def json_at_ref(ref: str, path: Path) -> dict:
    """Reads a repository JSON file from a Git revision."""
    content = subprocess.run(
        ["git", "show", f"{ref}:{path.as_posix()}"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    return json.loads(content)


def thresholds(policy: dict) -> tuple[dict, dict]:
    """Returns effective medium and high boundaries for every report row."""
    resolved = coverage_policy.policies(policy)
    minimums = {
        category: {metric: value.minimum for metric, value in values.items()}
        for category, values in resolved.items()
    }
    targets = {
        category: {metric: value.target for metric, value in values.items()}
        for category, values in resolved.items()
    }
    return minimums, targets


def failures(measured: dict, policies: dict) -> list[str]:
    result = []
    for category, values in policies.items():
        for metric, policy in values.items():
            actual = measured[category][metric]["percent"]
            if not coverage_policy.passes(actual, policy):
                boundary = policy.minimum if policy.enforce == "medium" else policy.target
                result.append(
                    f"{category} {metric}: {actual}% is below enforced {policy.enforce} "
                    f"boundary {boundary:g}%"
                )
    return result


def coverage_status(metrics: dict, policy: dict) -> str:
    if not policy:
        return "N/A"
    abbreviations = {"lines": "L", "functions": "F", "branches": "B"}
    problems: dict[str, list[str]] = {"NO DATA": [], "BAD": []}
    for metric in ("lines", "functions", "branches"):
        if metric not in policy:
            continue
        actual = metrics[metric]["percent"]
        if actual is None:
            problems["NO DATA"].append(abbreviations[metric])
        elif not coverage_policy.passes(actual, policy[metric]):
            problems["BAD"].append(abbreviations[metric])
    labels = [f'{name}: {"/".join(values)}' for name, values in problems.items() if values]
    if labels:
        return f'**{"; ".join(labels)}**'
    if all(
        coverage_policy.rating(metrics[metric]["percent"], value) == "high"
        for metric, value in policy.items()
    ):
        return "GOOD"
    return "OK"


def has_coverage(metrics: dict, names: tuple[str, ...]) -> bool:
    return any(metrics[name]["total"] for name in names)


def policies_with_data(metrics: dict, policies: dict) -> dict:
    """Returns only policies for metrics represented in a measurement row."""
    return {metric: value for metric, value in policies.items() if metrics[metric]["total"]}


def uncovered_patch_locations(
    files: dict[str, FileCoverage], changed: dict[str, set[int]]
) -> tuple[list[str], list[str]]:
    lines = []
    branches = set()
    for path, data in files.items():
        changed_in_file = changed.get(path, set())
        lines.extend(f"{path}:{line}" for line, hits in data.lines.items() if line in changed_in_file and not hits)
        branches.update(
            f"{path}:{line}" for line, taken in data.branches if line in changed_in_file and not taken
        )
    return sorted(lines), sorted(branches)


def markdown(measured: dict, policies: dict) -> str:
    headers = (
        "Category",
        "Status",
        "Lines",
        "Covered",
        "Total",
        "Functions",
        "Covered",
        "Total",
        "Branches",
        "Covered",
        "Total",
    )
    values = []
    for category, metrics in measured.items():
        cells = [
            category,
            coverage_status(metrics, policies.get(category, {})),
        ]
        for metric in ("lines", "functions", "branches"):
            value = metrics[metric]
            percent = "n/a" if value["percent"] is None else f'{value["percent"]:.2f}%'
            cells.extend((percent, str(value["covered"]), str(value["total"])))
        values.append(tuple(cells))
    widths = tuple(max(len(header), *(len(row[index]) for row in values)) for index, header in enumerate(headers))

    def row(cells: tuple[str, ...]) -> str:
        padded = [cell.ljust(width) for cell, width in zip(cells[:2], widths[:2])]
        padded.extend(cell.rjust(width) for cell, width in zip(cells[2:], widths[2:]))
        return "| " + " | ".join(padded) + " |"

    separators = (
        "-" * widths[0],
        "-" * widths[1],
        *("-" * (width - 1) + ":" for width in widths[2:]),
    )
    rows = [row(headers), row(separators), *(row(value) for value in values)]
    return "\n".join(rows) + "\n"


def json_summary(
    measured: dict,
    policies: dict,
    policy: dict,
    patch: dict | None = None,
    patch_policy: dict | None = None,
) -> dict:
    """Returns the stable machine-readable input consumed by the HTML index."""
    result = {
        "schema": 2,
        "measurements": measured,
        "minimums": {
            category: {metric: value.minimum for metric, value in values.items()}
            for category, values in policies.items()
        },
        "targets": {
            category: {metric: value.target for metric, value in values.items()}
            for category, values in policies.items()
        },
        "enforcement": {
            category: {metric: value.enforce for metric, value in values.items()}
            for category, values in policies.items()
        },
        "reasons": {
            name: category["reason"]
            for name, category in policy.get("categories", {}).items()
            if category.get("reason")
        },
    }
    if patch is not None and has_coverage(patch, ("lines", "branches")):
        result["patch"] = patch
        assert patch_policy is not None
        result["patch_policy"] = coverage_policy.serializable(patch_policy)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lcov", type=Path, required=True)
    parser.add_argument("--policy", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--write-baseline", action="store_true")
    parser.add_argument("--base-ref")
    parser.add_argument("--json-summary", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args(argv)
    if args.write_baseline and not args.baseline:
        parser.error("--write-baseline requires --baseline")
    policy = json.loads(args.policy.read_text(encoding="utf-8"))
    report = parse_lcov(args.lcov)
    files = select_files(report, policy)
    measured = measurements(files, policy)
    if not files:
        print("coverage report contains no files selected by policy", file=sys.stderr)
        return 2
    if args.baseline and args.write_baseline:
        baseline = {
            "schema": 2,
            "description": (
                "Bazel LCOV with the primary hermetic Clang toolchain; scope and "
                "exclusions are defined by coverage_policy.json"
            ),
            "scope": baseline_scope(policy),
            "measurements": measured,
        }
        args.baseline.write_text(json.dumps(baseline, indent=2) + "\n", encoding="utf-8")
    effective = {
        category: policies_with_data(measured[category], values)
        for category, values in coverage_policy.policies(policy).items()
    }
    text = markdown(measured, effective)
    patch_failures: list[str] = []
    patch: dict | None = None
    patch_policy: dict | None = None
    uncovered_lines: list[str] = []
    uncovered_branches: list[str] = []
    if args.base_ref:
        changed = changed_lines(args.base_ref)
        patch = counts(files, changed)
        patch_policy = coverage_policy.resolve(policy.get("patch", {}), effective["overall"])
        patch_policy = {metric: patch_policy[metric] for metric in ("lines", "branches")}
        if has_coverage(patch, ("lines", "branches")):
            enforced_patch_policy = policies_with_data(patch, patch_policy)
            text += "\n### Changed coverable lines\n\n" + markdown(
                {"patch": patch}, {"patch": enforced_patch_policy}
            )
            patch_failures = failures({"patch": patch}, {"patch": enforced_patch_policy})
            if patch_failures:
                uncovered_lines, uncovered_branches = uncovered_patch_locations(files, changed)
    print(text, end="")
    if args.summary:
        args.summary.write_text(text, encoding="utf-8")
    if args.json_summary:
        args.json_summary.write_text(
            json.dumps(json_summary(measured, effective, policy, patch, patch_policy), indent=2) + "\n",
            encoding="utf-8",
        )
    baseline_errors = []
    if args.baseline and not args.write_baseline:
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        baseline_errors = baseline_failures(measured, baseline, policy)
    base_errors = []
    if args.base_ref and args.baseline:
        base_policy = json_at_ref(args.base_ref, args.policy)
        base_baseline = json_at_ref(args.base_ref, args.baseline)
        base_files = select_files(report, base_policy)
        base_measured = measurements(base_files, base_policy)
        base_effective = {
            category: policies_with_data(base_measured[category], values)
            for category, values in coverage_policy.policies(base_policy).items()
        }
        base_errors = (
            policy_regressions(policy, base_policy)
            + baseline_regressions(
                json.loads(args.baseline.read_text(encoding="utf-8")), base_baseline
            )
            + failures(base_measured, base_effective)
            + baseline_failures(base_measured, base_baseline, base_policy)
        )
    errors = failures(measured, effective) + patch_failures
    for error in errors:
        print(f"coverage threshold failed: {error}", file=sys.stderr)
    for error in baseline_errors:
        print(f"coverage baseline failed: {error}", file=sys.stderr)
    for error in base_errors:
        print(f"coverage base-branch guard failed: {error}", file=sys.stderr)
    for location in uncovered_lines:
        print(f"uncovered patch line: {location}", file=sys.stderr)
    for location in uncovered_branches:
        print(f"uncovered patch branch: {location}", file=sys.stderr)
    return bool(errors or baseline_errors or base_errors)


if __name__ == "__main__":
    sys.exit(main())
