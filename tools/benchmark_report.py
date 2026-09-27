# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Summarize retained benchmark repetitions without trusting aggregate rows."""

import math
import statistics
import argparse
import json
import copy
from html import escape
from pathlib import Path

from tools import benchmark_artifact


def summarize(data, *, operations_per_iteration=1):
    """Return CPU ns/operation summaries; normalization is explicit, never inferred."""
    benchmark_artifact.validate(data)
    if data["validity"]["status"] != "valid" or data["git"]["dirty"]:
        raise ValueError("publication requires a valid, clean-source measurement")
    if data["build"]["mode"] != "opt":
        raise ValueError("publication requires an optimized build")
    if data["google_benchmark"]["context"].get("library_build_type", "").lower() != "release":
        raise ValueError("publication requires a release benchmark library")
    if not isinstance(operations_per_iteration, int) or isinstance(operations_per_iteration, bool) or operations_per_iteration <= 0:
        raise ValueError("operations_per_iteration must be a positive integer")
    units = {"ns": 1, "us": 1000, "ms": 1000000, "s": 1000000000}
    groups = {}
    for row in data["google_benchmark"]["benchmarks"]:
        if row.get("run_type") == "aggregate" or "aggregate_name" in row:
            continue
        name = row.get("run_name", row.get("name"))
        if not isinstance(name, str) or not name or row.get("error_occurred"):
            raise ValueError("invalid benchmark observation")
        value = row.get("cpu_time")
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise ValueError("CPU time must be finite and positive")
        iterations = row.get("iterations")
        if row.get("time_unit") not in units or isinstance(iterations, bool) or not isinstance(iterations, int) or iterations <= 0:
            raise ValueError("invalid observation units or iteration count")
        normalized = value * units[row["time_unit"]] / operations_per_iteration
        if not math.isfinite(normalized) or normalized <= 0:
            raise ValueError("normalized CPU time must be finite and positive")
        groups.setdefault(name, []).append((row.get("repetition_index"), normalized))
    if not groups:
        raise ValueError("no raw observations")
    result = []
    for name, observations in sorted(groups.items()):
        indices = [index for index, _ in observations]
        if len(observations) < 9 or any(isinstance(index, bool) or not isinstance(index, int) or index < 0 for index in indices) or len(set(indices)) != len(indices):
            raise ValueError(f"{name}: at least nine uniquely indexed raw repetitions are required")
        values = sorted(value for _, value in observations)
        middle = len(values) // 2
        median = values[middle] if len(values) % 2 else statistics.mean(values[middle - 1:middle + 1])
        result.append({"name": name, "samples": len(values), "median_cpu_ns": median,
                       "best_three_mean_cpu_ns": statistics.mean(values[:3]), "minimum_cpu_ns": values[0],
                       "maximum_cpu_ns": values[-1], "operations_per_iteration": operations_per_iteration})
    return {
        "source_content_sha256": data["content_sha256"],
        "measurement": {key: copy.deepcopy(data[key]) for key in
                        ("component", "git", "source_relation", "host", "toolchain", "build", "timing", "controls", "validity", "provenance")},
        "benchmarks": result,
    }


def render_svg(report, *, title, names):
    """Render an explicitly selected comparison, with observed min/max whiskers."""
    if not names or len(names) > 20 or len(set(names)) != len(names):
        raise ValueError("select between one and twenty distinct benchmark names")
    available = {row["name"]: row for row in report["benchmarks"]}
    if any(name not in available for name in names):
        raise ValueError("selected benchmark is absent from report")
    rows = [available[name] for name in names]
    maximum = max(row["maximum_cpu_ns"] for row in rows)
    labels = [[row["name"][start:start + 80] for start in range(0, len(row["name"]), 80)] for row in rows]
    row_heights = [max(48, 16 * len(lines) + 16) for lines in labels]
    height = 156 + sum(row_heights)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="{height}" role="img">',
             f'<title>{escape(title)}</title>',
             '<rect width="100%" height="100%" fill="white"/>',
             f'<text x="16" y="28" font-family="sans-serif" font-size="18">{escape(title)}</text>',
             '<text x="16" y="50" font-family="sans-serif" font-size="12">CPU ns/operation: median bars; observed min–max whiskers (not confidence intervals)</text>']
    y = 76
    for row, lines, row_height in zip(rows, labels, row_heights):
        left = 580 + (row["minimum_cpu_ns"] / maximum) * 480
        right = 580 + (row["maximum_cpu_ns"] / maximum) * 480
        parts.append(f'<g><title>{escape(row["name"])}</title>')
        for line_index, line in enumerate(lines):
            parts.append(f'<text x="16" y="{y + 16 + line_index * 16}" font-family="monospace" font-size="11">{escape(line)}</text>')
        parts.extend([
            f'<rect x="580" y="{y}" width="{(row["median_cpu_ns"] / maximum) * 480:.3f}" height="24" fill="#2563eb"/>',
            f'<path d="M {left:.3f} {y + 12} H {right:.3f} M {left:.3f} {y + 6} V {y + 18} M {right:.3f} {y + 6} V {y + 18}" stroke="#111827" fill="none"/>',
            f'<text x="1080" y="{y + 16}" font-family="sans-serif" font-size="12">{row["median_cpu_ns"]:.3g} ns; n={row["samples"]}</text>',
            '</g>'])
        y += row_height
    measurement = report["measurement"]
    label = " / ".join(str(value) for value in
                       (measurement["host"]["cpu_model"], measurement["toolchain"]["compiler"],
                        measurement["toolchain"]["cxx_standard"], measurement["build"]["mode"]))
    parts.append(f'<text x="16" y="{height - 52}" font-family="sans-serif" font-size="11">{escape(label)}</text>')
    parts.append(f'<text x="16" y="{height - 34}" font-family="monospace" font-size="11">Measured commit: {escape(measurement["git"]["commit"])}</text>')
    parts.append(f'<text x="16" y="{height - 16}" font-family="monospace" font-size="11">Source artifact SHA-256: {escape(report["source_content_sha256"])}</text>')
    return "\n".join([*parts, "</svg>"]) + "\n"


def select_benchmarks(report, names):
    """Return a report containing exactly the explicitly selected comparisons."""
    if not names:
        return report
    if len(names) > 20 or len(set(names)) != len(names):
        raise ValueError("select between one and twenty distinct benchmark names")
    available = {row["name"]: row for row in report["benchmarks"]}
    if any(name not in available for name in names):
        raise ValueError("selected benchmark is absent from report")
    selected = copy.deepcopy(report)
    selected["benchmarks"] = [copy.deepcopy(available[name]) for name in names]
    return selected


def main(argv=None):
    """Generate a derived report without overwriting the retained input artifact."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--summary", required=True, type=Path)
    parser.add_argument("--svg", type=Path)
    parser.add_argument("--title", default="Benchmark comparison")
    parser.add_argument("--name", action="append", default=[])
    parser.add_argument("--operations-per-iteration", type=int, default=1)
    args = parser.parse_args(argv)
    paths = [args.artifact.resolve(), args.summary.resolve()]
    if args.svg:
        paths.append(args.svg.resolve())
    if len(set(paths)) != len(paths):
        parser.error("artifact, summary, and SVG must have distinct paths")
    # Never silently destroy previous observations or derived reports.
    if args.summary.exists() or (args.svg and args.svg.exists()):
        parser.error("output paths must not already exist")
    outputs = [args.summary] + ([args.svg] if args.svg else [])
    for output in outputs:
        if not output.parent.is_dir():
            parser.error(f"output directory does not exist: {output.parent}")
    try:
        data = json.loads(args.artifact.read_text())
        report = select_benchmarks(
            summarize(data, operations_per_iteration=args.operations_per_iteration), args.name
        )
        svg = render_svg(report, title=args.title, names=args.name) if args.svg else None
    except (ValueError, OSError) as error:
        parser.error(str(error))
    # Validate every requested output before creating either file.
    with args.summary.open("x") as stream:
        stream.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if args.svg:
        with args.svg.open("x") as stream:
            stream.write(svg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
