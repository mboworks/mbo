#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Regenerate the Frozen guide tables and CSV summaries from retained raw repetitions."""

import argparse
import collections
import csv
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import statistics

import align_markdown_tables

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "mbo/container/experimental/measurements/2026-10-04-apple-m5-pro"
GUIDE = ROOT / "mbo/container/experimental/FROZEN.md"
LAYOUTS = (
    ("linear", "Linear scan"), ("limited", "Limited"),
    ("minimal", "Frozen minimal"), ("sparse", "Frozen sparse"),
    ("std_unordered", "STL unordered"), ("absl_flat", "Abseil flat"),
    ("absl_node", "Abseil node"),
)


def summarize(raw, repetitions):
    if repetitions < 2:
        raise ValueError("at least two repetitions are required")
    groups = collections.defaultdict(list)
    for row in raw["benchmarks"]:
        if row.get("error_occurred"):
            raise ValueError("benchmark contains an error")
        if row.get("run_type") == "iteration":
            groups[row["run_name"]].append(row)
    if not groups:
        raise ValueError("no raw repetitions")
    summaries = {}
    factors = {"ns": 1, "us": 1_000, "ms": 1_000_000, "s": 1_000_000_000}
    for name, rows in sorted(groups.items()):
        if sorted(row["repetition_index"] for row in rows) != list(range(repetitions)):
            raise ValueError(f"{name}: missing or duplicate repetitions")
        times = [row["cpu_time"] * factors[row["time_unit"]] for row in rows]
        if not all(math.isfinite(value) and value > 0 for value in times):
            raise ValueError(f"{name}: invalid timing")
        if not all(row["iterations"] > 0 for row in rows):
            raise ValueError(f"{name}: no measured iterations")
        median = statistics.median(times)
        elements = int(name.split("/")[2]) if name.endswith("/iterate") else None
        summaries[name] = {
            "repetitions": repetitions,
            "median_cpu_ns": median,
            "cpu_cv": statistics.stdev(times) / statistics.mean(times),
            "median_cpu_ns_per_element": median / elements if elements else None,
        }
        for counter in ("object_bytes", "elements", "buckets", "load_factor", "construction_work"):
            values = {row.get(counter) for row in rows}
            if len(values) != 1:
                raise ValueError(f"{name}: inconsistent {counter}")
            summaries[name][counter] = values.pop()
    return summaries


def read_data(directory, name):
    metadata = json.loads((directory / "provenance.json").read_text())["runs"][name]
    compressed = (directory / metadata["raw"]).read_bytes()
    raw_bytes = gzip.decompress(compressed)
    patch = (directory / metadata["source_patch"]).read_bytes()
    for data, field in ((compressed, "compressed_sha256"), (raw_bytes, "raw_sha256"),
                        (patch, "source_patch_sha256")):
        if hashlib.sha256(data).hexdigest() != metadata[field]:
            raise ValueError(f"{name}: {field} mismatch")
    raw = json.loads(raw_bytes)
    summaries = summarize(raw, metadata["repetitions"])
    if len(summaries) != metadata["cases"]:
        raise ValueError(f"{name}: unexpected case count")
    if sum(row["repetitions"] for row in summaries.values()) != metadata["samples"]:
        raise ValueError(f"{name}: unexpected sample count")
    return summaries


def csv_text(summaries):
    output = io.StringIO(newline="")
    fields = ["name", "repetitions", "median_cpu_ns", "cpu_cv", "median_cpu_ns_per_element",
              "object_bytes", "elements", "buckets", "load_factor", "construction_work"]
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for name, values in summaries.items():
        writer.writerow({"name": name, **values})
    return output.getvalue()


def read_table(summaries):
    lines = [
        "| Container | Map `find` mixed (ns/query) | Set `find` mixed (ns/query) | Map `at` hit (ns/query) | Map iteration (ns/element) |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for layout, label in LAYOUTS:
        names = (f"Map/string/64/{layout}/find/mixed", f"Set/string/64/{layout}/find/mixed",
                 f"Map/string/64/{layout}/at/hit", f"Map/string/64/{layout}/iterate")
        values = [summaries[name]["median_cpu_ns"] for name in names[:3]]
        values.append(summaries[names[3]]["median_cpu_ns_per_element"])
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
    return align_markdown_tables.align_text("\n".join(lines) + "\n").rstrip()


def hash_tables(summaries):
    lines = [
        "| Hash alone (ns/key) | `FrozenHash` | libc++ `std::hash` | Abseil default |",
        "| --- | ---: | ---: | ---: |",
    ]
    for key, label in (("string", "10-byte string view"), ("int", "Integer")):
        values = [summaries[f"Hash/{key}/{hasher}"]["median_cpu_ns"] for hasher in ("frozen", "std", "absl")]
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
    lines += ["", "| Map, 64 string keys | Default hasher (ns/query) | Supplied `FrozenHash` (ns/query) |",
              "| --- | ---: | ---: |"]
    for native, custom, label in (("std_unordered", "std_frozen_hash", "STL unordered"),
                                   ("absl_flat", "absl_flat_frozen_hash", "Abseil flat"),
                                   ("absl_node", "absl_node_frozen_hash", "Abseil node")):
        default = summaries[f"Map/string/64/{native}/find/mixed"]["median_cpu_ns"]
        shared = summaries[f"Diagnostic/Map/string/64/{custom}/find/mixed"]["median_cpu_ns"]
        lines.append(f"| {label} | {default:.2f} | {shared:.2f} |")
    sparse = summaries["Map/string/64/sparse/find/mixed"]["median_cpu_ns"]
    lines += ["", f"Sparse FrozenMap took **{sparse:.2f} ns/query** with its default `FrozenHash` in this same run."]
    return align_markdown_tables.align_text("\n".join(lines) + "\n").rstrip()


def replace_block(text, label, content):
    begin = f"<!-- BEGIN FROZEN {label} -->"
    end = f"<!-- END FROZEN {label} -->"
    if text.count(begin) != 1 or text.count(end) != 1:
        raise ValueError(f"expected one {label} block")
    before, rest = text.split(begin, 1)
    _, after = rest.split(end, 1)
    return before + begin + "\n\n" + content + "\n\n" + end + after


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=DATA)
    parser.add_argument("--guide", type=Path, default=GUIDE)
    args = parser.parse_args()
    reads = read_data(args.data, "reads")
    hashes = read_data(args.data, "hashes")
    (args.data / "read-summary.csv").write_text(csv_text(reads))
    (args.data / "hash-summary.csv").write_text(csv_text(hashes))
    guide = replace_block(args.guide.read_text(), "READ RESULTS", read_table(reads))
    args.guide.write_text(replace_block(guide, "HASH RESULTS", hash_tables(hashes)))


if __name__ == "__main__":
    main()
