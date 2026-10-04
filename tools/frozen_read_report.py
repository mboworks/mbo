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
import lzma
import math
from pathlib import Path
import statistics

import align_markdown_tables
import benchmark_artifact

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "mbo/container/experimental/measurements/2026-10-04-apple-m5-pro"
MBO_DATA = ROOT / "mbo/container/experimental/measurements/2026-10-04-apple-m5-pro-mbo-hashes"
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
        # Different Python/libm versions can differ in the last bit of stdev. Keep
        # reproducible summaries at nine significant digits; raw JSON retains full precision.
        formatted = {key: format(value, ".9g") if isinstance(value, float) else value
                     for key, value in values.items()}
        writer.writerow({"name": name, **formatted})
    return output.getvalue()


def read_table(summaries, layouts=LAYOUTS):
    lines = [
        "| Container | Map `find` mixed (ns/query) | Set `find` mixed (ns/query) | Map `at` hit (ns/query) | Map iteration (ns/element) |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for layout, label in layouts:
        names = (f"Map/string/64/{layout}/find/mixed", f"Set/string/64/{layout}/find/mixed",
                 f"Map/string/64/{layout}/at/hit", f"Map/string/64/{layout}/iterate")
        values = [summaries[name]["median_cpu_ns"] for name in names[:3]]
        values.append(summaries[names[3]]["median_cpu_ns_per_element"])
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
    return align_markdown_tables.align_text("\n".join(lines) + "\n").rstrip()


def hash_tables(summaries):
    lines = [
        "| Hash alone (ns/key) | Original `FrozenHash` | libc++ `std::hash` | Abseil default |",
        "| --- | ---: | ---: | ---: |",
    ]
    for key, label in (("string", "10-byte string view"), ("int", "Integer")):
        values = [summaries[f"Hash/{key}/{hasher}"]["median_cpu_ns"] for hasher in ("frozen", "std", "absl")]
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
    lines += ["", "| Map, 64 string keys | Default hasher (ns/query) | Supplied original `FrozenHash` (ns/query) |",
              "| --- | ---: | ---: |"]
    for native, custom, label in (("std_unordered", "std_frozen_hash", "STL unordered"),
                                   ("absl_flat", "absl_flat_frozen_hash", "Abseil flat"),
                                   ("absl_node", "absl_node_frozen_hash", "Abseil node")):
        default = summaries[f"Map/string/64/{native}/find/mixed"]["median_cpu_ns"]
        shared = summaries[f"Diagnostic/Map/string/64/{custom}/find/mixed"]["median_cpu_ns"]
        lines.append(f"| {label} | {default:.2f} | {shared:.2f} |")
    sparse = summaries["Map/string/64/sparse/find/mixed"]["median_cpu_ns"]
    lines += ["", f"Sparse FrozenMap took **{sparse:.2f} ns/query** with its original default `FrozenHash` in this same run."]
    return align_markdown_tables.align_text("\n".join(lines) + "\n").rstrip()


def read_mbo_data(directory):
    metadata = json.loads((directory / "provenance.json").read_text())
    compressed = (directory / metadata["artifact"]).read_bytes()
    raw_bytes = lzma.decompress(compressed)
    for data, field in ((compressed, "compressed_sha256"), (raw_bytes, "artifact_sha256")):
        if hashlib.sha256(data).hexdigest() != metadata[field]:
            raise ValueError(f"MBO hashes: {field} mismatch")
    artifact = json.loads(raw_bytes)
    benchmark_artifact.validate(artifact)
    if artifact["google_benchmark"]["context"].get("experiment") != "frozen-mbo-hashes-v1":
        raise ValueError("MBO hashes: unexpected experiment")
    if artifact["git"]["commit"] != metadata["source_commit"]:
        raise ValueError("MBO hashes: source commit mismatch")
    summaries = summarize(artifact["google_benchmark"], artifact["controls"]["repetitions"])
    if len(summaries) != metadata["cases"]:
        raise ValueError("MBO hashes: unexpected case count")
    return summaries


MBO_LAYOUTS = (
    ("sparse", "Frozen sparse / former FNV"),
    ("sparse_mumbo", "Frozen sparse / mumbo"),
    ("sparse_fambo", "Frozen sparse / fambo"),
    ("sparse_dumbo", "Frozen sparse / dumbo"),
    ("std_unordered", "STL unordered / default"),
    ("absl_flat", "Abseil flat / default"),
    ("absl_node", "Abseil node / default"),
)


def mbo_hash_tables(summaries):
    lines = ["| Hash alone, 10-byte string | ns/key |", "| --- | ---: |"]
    for name, label in (("frozen", "Former FrozenHash (FNV-1a + mix)"), ("mumbo", "mumbo"),
                        ("fambo", "fambo"), ("dumbo", "dumbo"),
                        ("std", "libc++ default"), ("absl", "Abseil default")):
        value = summaries[f"Hash/string/{name}"]["median_cpu_ns"]
        lines.append(f"| {label} | {value:.2f} |")
    lines += ["", "All containers below receive the **same named hasher**: mixed map lookup, 64 string keys, ns/query.", "",
              "| Supplied hash | Frozen minimal | Frozen sparse | STL unordered | Abseil flat | Abseil node |",
              "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for suffix, label in (("", "Former FrozenHash"), ("_mumbo", "mumbo"), ("_fambo", "fambo"), ("_dumbo", "dumbo")):
        algorithm = suffix.removeprefix("_") if suffix else "frozen_hash"
        names = [f"Map/string/64/{layout}{suffix}/find/mixed" for layout in ("minimal", "sparse")]
        names += [f"Diagnostic/Map/string/64/{layout}_{algorithm}/find/mixed"
                  for layout in ("std", "absl_flat", "absl_node")]
        values = [summaries[name]["median_cpu_ns"] for name in names]
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
    return align_markdown_tables.align_text("\n".join(lines) + "\n").rstrip()


def mbo_size_table(summaries):
    lines = ["| Container | Map 8 | Map 64 | Map 256 | Set 8 | Set 64 | Set 256 |",
             "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for layout, label in MBO_LAYOUTS:
        names = [f"{kind}/string/{size}/{layout}/find/mixed"
                 for kind in ("Map", "Set") for size in (8, 64, 256)]
        values = [summaries[name]["median_cpu_ns"] for name in names]
        lines.append(f"| {label} | " + " | ".join(f"{value:.2f}" for value in values) + " |")
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
    parser.add_argument("--mbo-data", type=Path, default=MBO_DATA)
    parser.add_argument("--guide", type=Path, default=GUIDE)
    args = parser.parse_args()
    reads = read_data(args.data, "reads")
    hashes = read_data(args.data, "hashes")
    mbo_hashes = read_mbo_data(args.mbo_data)
    (args.mbo_data / "summary.csv").write_text(csv_text(mbo_hashes))
    (args.data / "read-summary.csv").write_text(csv_text(reads))
    (args.data / "hash-summary.csv").write_text(csv_text(hashes))
    guide = replace_block(args.guide.read_text(), "READ RESULTS", read_table(reads))
    guide = replace_block(guide, "HASH RESULTS", hash_tables(hashes))
    guide = replace_block(guide, "MBO READ RESULTS", read_table(mbo_hashes, MBO_LAYOUTS))
    guide = replace_block(guide, "MBO HASH RESULTS", mbo_hash_tables(mbo_hashes))
    args.guide.write_text(replace_block(guide, "MBO SIZE RESULTS", mbo_size_table(mbo_hashes)))


if __name__ == "__main__":
    main()
