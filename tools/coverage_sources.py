#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Group LCOV sources by the repository coverage-policy categories."""

import argparse
from collections import defaultdict
import fnmatch
import json
import re
from pathlib import Path


def _matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def _excluded_range_lines(source_lines: list[str]) -> set[int]:
    """Returns lines inside explicit source coverage exclusion ranges."""
    result: set[int] = set()
    start: int | None = None
    for number, line in enumerate(source_lines, start=1):
        if "LCOV_EXCL_START" in line:
            if start is not None:
                raise ValueError(f"nested LCOV_EXCL_START at line {number}")
            start = number
        if start is not None:
            result.add(number)
        if "LCOV_EXCL_STOP" in line:
            if start is None:
                raise ValueError(f"LCOV_EXCL_STOP without LCOV_EXCL_START at line {number}")
            start = None
    if start is not None:
        raise ValueError(f"LCOV_EXCL_START at line {start} has no LCOV_EXCL_STOP")
    return result


def _function_markers(source_lines: list[str]) -> tuple[set[int], dict[int, int]]:
    excluded: set[int] = set()
    merged: dict[int, int] = {}
    for index, line in enumerate(source_lines):
        exclude = "LCOV_EXCL_FUNC_LINE" in line
        merge = "LCOV_MERGE_FUNC_LINE" in line
        if not exclude and not merge:
            continue
        for continuation in range(index, len(source_lines)):
            if exclude:
                excluded.add(continuation + 1)
            if merge:
                merged[continuation + 1] = index + 1
            if "{" in source_lines[continuation]:
                break
    return excluded, merged


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


def _normalize_record(record: str, source: Path) -> str:
    """Applies source coverage directives to one raw LCOV record."""
    source_lines = source.read_text(encoding="utf-8").splitlines()
    excluded_ranges = _excluded_range_lines(source_lines)
    excluded_lines = excluded_ranges | {
        number
        for number, line in enumerate(source_lines, start=1)
        if "LCOV_EXCL_LINE" in line
    }
    excluded_branches = excluded_lines | {
        number
        for number, line in enumerate(source_lines, start=1)
        if "LCOV_EXCL_BR_LINE" in line
    }
    excluded_functions, merged_functions = _function_markers(source_lines)
    excluded_functions |= excluded_ranges
    merged_branches = _branch_merge_markers(source_lines)

    raw_lines = record.splitlines()
    headers = [
        line
        for line in raw_lines
        if not re.match(r"^(?:FN|FNDA|FNF|FNH|DA|LF|LH|BRDA|BRF|BRH):", line)
    ]
    definitions: list[tuple[int, str]] = []
    hits_by_name: dict[str, int] = defaultdict(int)
    data_lines: list[str] = []
    branches: list[tuple[int, str, str, str]] = []
    for line in raw_lines:
        if line.startswith("FN:"):
            definition = line[3:].split(",")
            definitions.append((int(definition[0]), definition[-1]))
        elif line.startswith("FNDA:"):
            hits, name = line[5:].split(",", 1)
            hits_by_name[name] += int(hits)
        elif line.startswith("DA:"):
            number = int(line[3:].split(",", 1)[0])
            if number not in excluded_lines:
                data_lines.append(line)
        elif line.startswith("BRDA:"):
            number, block, branch, taken = line[5:].split(",")
            if int(number) not in excluded_branches:
                branches.append((int(number), block, branch, taken))

    functions: list[tuple[int, str, int]] = []
    function_groups: dict[int, list[tuple[int, str, int]]] = defaultdict(list)
    explicit_function_groups = set(merged_functions.values())
    for line, name in definitions:
        if line in excluded_functions:
            continue
        value = (line, name, hits_by_name.get(name, 0))
        if line in merged_functions:
            function_groups[merged_functions[line]].append(value)
        else:
            function_groups[line].append(value)
    for group, values in sorted(function_groups.items()):
        if len(values) == 1 and values[0][0] == group and group not in explicit_function_groups:
            functions.append(values[0])
        else:
            functions.append(
                (
                    min(line for line, _, _ in values),
                    f"__mbo_lcov_merged_function_at_line_{group}",
                    max(hits for _, _, hits in values),
                )
            )

    ordinary_branches: list[tuple[int, str, str, str]] = []
    branch_groups: dict[int, list[str]] = defaultdict(list)
    for line, block, branch, taken in branches:
        if line in merged_branches:
            branch_groups[line].append(taken)
        else:
            ordinary_branches.append((line, block, branch, taken))
    for line, taken_values in sorted(branch_groups.items()):
        widths = merged_branches[line]
        width = next(
            (
                candidate
                for candidate in sorted(widths, reverse=True)
                if candidate > 0 and len(taken_values) % candidate == 0
            ),
            None,
        )
        if width is None:
            raise ValueError(
                f"{source}:{line}: LCOV_MERGE_BR_LINE "
                f"{','.join(str(candidate) for candidate in widths)} cannot merge "
                f"{len(taken_values)} branch records"
            )
        for index in range(width):
            values = [value for value in taken_values[index::width] if value != "-"]
            taken = str(sum(int(value) for value in values)) if values else "-"
            ordinary_branches.append((line, "0", str(index), taken))

    function_hits = sum(hits > 0 for _, _, hits in functions)
    branch_hits = sum(taken not in ("-", "0") for _, _, _, taken in ordinary_branches)
    line_hits = sum(int(line.split(",")[1]) > 0 for line in data_lines)
    result = headers
    result.extend(f"FN:{line},{name}" for line, name, _ in functions)
    result.extend(f"FNDA:{hits},{name}" for _, name, hits in functions)
    result.extend((f"FNF:{len(functions)}", f"FNH:{function_hits}"))
    result.extend(f"BRDA:{line},{block},{branch},{taken}" for line, block, branch, taken in ordinary_branches)
    result.extend((f"BRF:{len(ordinary_branches)}", f"BRH:{branch_hits}"))
    result.extend(data_lines)
    result.extend((f"LF:{len(data_lines)}", f"LH:{line_hits}"))
    return "\n".join(result) + "\n"


def normalized(report: str, workspace: Path = Path.cwd()) -> str:
    """Returns an LCOV report with source coverage directives applied."""
    result = []
    for record in report.split("end_of_record\n"):
        match = re.search(r"(?m)^SF:(.+)$", record)
        if not match:
            continue
        physical = Path(match.group(1))
        source = physical if physical.is_absolute() else workspace / physical
        if not source.is_file() and "/mbo/" in physical.as_posix():
            source = workspace / "mbo" / physical.as_posix().rsplit("/mbo/", 1)[1]
        normalized_record = _normalize_record(record, source) if source.is_file() else record
        result.append(normalized_record + "end_of_record\n")
    return "".join(result)


def grouped(report: str, policy: dict, source_root: Path, workspace: Path = Path.cwd()) -> str:
    """Returns selected LCOV records rooted below their one policy category."""
    includes = policy.get("include", ["mbo/**"])
    excludes = policy.get("exclude", [])
    categories = [(name, value["include"]) for name, value in policy["categories"].items()]
    result = []
    for record in report.split("end_of_record\n"):
        match = re.search(r"(?m)^SF:(.+)$", record)
        if not match:
            continue
        if not re.search(r"(?m)^(LF|FNF|BRF):[1-9][0-9]*$", record):
            continue
        physical = match.group(1)
        logical = physical.removeprefix(str(workspace.resolve()) + "/")
        if not _matches(logical, includes) or _matches(logical, excludes):
            continue
        matches = [name for name, patterns in categories if _matches(logical, patterns)]
        if len(matches) != 1:
            raise ValueError(f"coverage source {logical!r} belongs to {len(matches)} policy categories")
        source = workspace / logical
        normalized = _normalize_record(record, source)
        linked = source_root.resolve() / matches[0] / logical
        linked.parent.mkdir(parents=True, exist_ok=True)
        if not linked.exists() and not linked.is_symlink():
            linked.symlink_to(source.resolve())
        result.append(normalized.replace(f"SF:{physical}", f"SF:{linked}", 1) + "end_of_record\n")
    return "".join(result)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--normalize-only", action="store_true")
    parser.add_argument("--policy", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    raw_report = args.input.read_text(encoding="utf-8")
    if args.normalize_only:
        report = normalized(raw_report)
    else:
        if args.policy is None or args.source_root is None:
            parser.error("--policy and --source-root are required unless --normalize-only is used")
        report = grouped(
            raw_report,
            json.loads(args.policy.read_text(encoding="utf-8")),
            args.source_root,
        )
    args.output.write_text(report, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
