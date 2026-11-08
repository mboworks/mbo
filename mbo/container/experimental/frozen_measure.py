#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Measure identical standalone lookup commands for all four table layouts.

Uses frozen_probe's real compilation database entry. Results are local artifacts,
not machine-independent performance promises. Clang time traces are required.
"""

import argparse
import json
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import time

from frozen_compile_test import compiler_command


def run(command, directory):
    started = time.perf_counter_ns()
    result = subprocess.run(command, cwd=directory, text=True, capture_output=True, check=False)
    elapsed = (time.perf_counter_ns() - started) / 1e6
    if result.returncode:
        raise RuntimeError(f"Command failed: {command!r}\n{result.stdout}\n{result.stderr}")
    return result.stdout, elapsed


def sections(text):
    result = {}
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and (fields[0].startswith(".") or fields[0].startswith("__")):
            result[fields[0]] = int(fields[1], 0)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, default=64)
    parser.add_argument("--build-repetitions", type=int, default=3)
    parser.add_argument("--startup-repetitions", type=int, default=50)
    parser.add_argument("--llvm-size", default="llvm-size")
    parser.add_argument("--llvm-nm", default="llvm-nm")
    args = parser.parse_args()
    if not 1 <= args.size <= 4096 or min(args.build_repetitions, args.startup_repetitions) < 1:
        parser.error("size must be 1..4096 and repetition counts must be positive")
    for tool in (args.llvm_size, args.llvm_nm):
        if not shutil.which(tool):
            parser.error(f"required tool is missing: {tool}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    command, directory = compiler_command()
    version, _ = run([command[0], "--version"], directory)
    if "clang" not in version.lower():
        parser.error("this measurement uses Clang -ftime-trace; correctness also supports GCC")
    source = directory / "mbo/container/experimental/frozen_probe.cc"
    records = []
    for layout, name in enumerate(("linear", "limited", "minimal", "sparse")):
        flags = [*command, "-O2", "-g0", f"-DMBO_FROZEN_PROBE_LAYOUT={layout}",
                 f"-DMBO_FROZEN_PROBE_SIZE={args.size}"]
        builds = []
        evaluations = []
        for repetition in range(args.build_repetitions):
            obj = output / f"{name}-{repetition}.o"
            trace = output / f"{name}-{repetition}.json"
            _, elapsed = run([*flags, f"-ftime-trace={trace}", "-ftime-trace-granularity=0",
                              "-c", str(source), "-o", str(obj)], directory)
            builds.append(elapsed)
            events = json.loads(trace.read_text())["traceEvents"]
            evaluations.append({event["name"]: event["dur"] / 1000
                                for event in events if event["name"].startswith("Total Evaluate")})
        section_text, _ = run([args.llvm_size, "-A", str(obj)], directory)
        (output / f"{name}-sections.txt").write_text(section_text)
        symbols, _ = run([args.llvm_nm, "--defined-only", str(obj)], directory)
        (output / f"{name}-symbols.txt").write_text(symbols)
        section_sizes = sections(section_text)
        readonly = sum(size for section, size in section_sizes.items()
                       if section.startswith((".rodata", ".data.rel.ro", "__const", "__cstring", "__literal")))
        executable = output / name
        run([*flags, str(source), "-o", str(executable)], directory)
        # Exercise both outcomes before timing the success path.
        run([str(executable), "key-000000"], directory)
        missing = subprocess.run([str(executable), "unknown"], check=False)
        if missing.returncode != 1:
            raise RuntimeError(f"{name}: miss returned {missing.returncode}, expected 1")
        startup = [run([str(executable), "key-000000"], directory)[1]
                   for _ in range(args.startup_repetitions)]
        records.append({
            "layout": name,
            "compile_ms": builds,
            "compile_median_ms": statistics.median(builds),
            "constexpr_totals_ms": evaluations,
            "object_bytes": obj.stat().st_size,
            "readonly_section_bytes": readonly,
            "sections": section_sizes,
            "dynamic_initializer": any(marker in symbols for marker in (
                "__cxx_global_var_init", "_GLOBAL__sub_I", "__static_initialization_and_destruction")),
            "startup_ms": startup,
            "startup_median_ms": statistics.median(startup),
            "compile_command": flags,
        })
        print(f"{name}: compile {statistics.median(builds):.1f} ms, object {obj.stat().st_size} B, "
              f"readonly {readonly} B, startup {statistics.median(startup):.3f} ms", flush=True)
    report = {"compiler": version, "platform": platform.platform(), "size": args.size, "results": records}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
