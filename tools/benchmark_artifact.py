#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Run and validate reproducible Google Benchmark JSON measurements."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys
import tempfile
import time


SCHEMA = "dev.mboworks.benchmark-artifact"
SCHEMA_VERSION = 1
DEFAULT_REPETITIONS = 9
DEFAULT_MIN_TIME = "1s"
DEFAULT_WARMUP_TIME = 1.0


def _run(command, cwd):
    return subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def _git(root):
    status = _run(["git", "status", "--porcelain"], root)
    return {
        "repository": _run(["git", "config", "--get", "remote.origin.url"], root),
        "commit": _run(["git", "rev-parse", "HEAD"], root),
        "branch": _run(["git", "rev-parse", "--abbrev-ref", "HEAD"], root),
        "dirty": bool(status),
    }


def _memory_bytes():
    try:
        return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (AttributeError, OSError, ValueError):
        return None


def _optional_command(command):
    try:
        return _run(command, None)
    except (OSError, subprocess.CalledProcessError):
        return None


def _cpu_model():
    model = platform.processor()
    if model:
        return model
    for command in (["sysctl", "-n", "machdep.cpu.brand_string"], ["sysctl", "-n", "hw.model"]):
        model = _optional_command(command)
        if model:
            return model
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.lower().startswith("model name"):
                return line.partition(":")[2].strip()
    except OSError:
        pass
    return None


def _physical_cpu_count():
    value = _optional_command(["sysctl", "-n", "hw.physicalcpu"])
    if value and value.isdigit():
        return int(value)
    value = _optional_command(["lscpu", "--parse=core,socket"])
    if value:
        cores = {line for line in value.splitlines() if line and not line.startswith("#")}
        if cores:
            return len(cores)
    return None


def _host():
    uname = platform.uname()
    load = list(os.getloadavg()) if hasattr(os, "getloadavg") else None
    return {
        "hostname": uname.node,
        "os": uname.system,
        "os_release": uname.release,
        "architecture": uname.machine,
        "cpu_model": _cpu_model(),
        "physical_cpu_count": _physical_cpu_count(),
        "logical_cpu_count": os.cpu_count(),
        "memory_bytes": _memory_bytes(),
        "load_average": load,
    }


def _utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def _sha256_json(value):
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def artifact(*, component, target, raw, command, configurations, baseline, started, ended,
             duration_seconds, root, controls, cxx_standard="c++20", bazel_version=None,
             validity="valid", validity_note=None, runner_version=SCHEMA_VERSION):
    context = raw.get("context")
    benchmarks = raw.get("benchmarks")
    if not isinstance(context, dict) or not isinstance(benchmarks, list):
        raise ValueError("Google Benchmark JSON requires object context and array benchmarks")
    result = {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "component": component,
        "git": _git(root),
        "source_relation": {"baseline_commit": baseline},
        "host": _host(),
        "toolchain": {
            "compiler": context.get("compiler") or context.get("compiler_version"),
            "compiler_version": context.get("compiler_version"),
            "cxx_standard": cxx_standard,
            "bazel_version": bazel_version,
            "library_build_type": context.get("library_build_type"),
            "bazel_configurations": configurations,
            "build_flags": command,
        },
        "build": {"target": target, "mode": "opt"},
        "timing": {
            "started_utc": started,
            "ended_utc": ended,
            "duration_seconds": duration_seconds,
            "time_source": "google-benchmark steady clock",
        },
        "controls": controls,
        "validity": {"status": validity, "note": validity_note},
        "provenance": {
            "runner": "tools/benchmark_artifact.py",
            "runner_version": runner_version,
            "command": command,
        },
        "google_benchmark": raw,
    }
    result["content_sha256"] = _sha256_json(result)
    validate(result)
    return result


def _require(condition, message, errors):
    if not condition:
        errors.append(message)


def validate(data):
    errors = []
    _require(data.get("schema") == SCHEMA, f"schema must be {SCHEMA!r}", errors)
    _require(data.get("schema_version") == SCHEMA_VERSION,
             f"schema_version must be {SCHEMA_VERSION}", errors)
    _require(isinstance(data.get("component"), str) and bool(data.get("component")),
             "component must be a non-empty string", errors)
    for section in ("git", "source_relation", "host", "toolchain", "build", "timing",
                    "controls", "validity", "provenance", "google_benchmark"):
        _require(isinstance(data.get(section), dict), f"{section} must be an object", errors)
    if errors:
        raise ValueError("; ".join(errors))

    git = data["git"]
    _require(isinstance(git.get("commit"), str) and len(git["commit"]) == 40,
             "git.commit must be a full SHA", errors)
    _require(isinstance(git.get("dirty"), bool), "git.dirty must be boolean", errors)
    host = data["host"]
    for name in ("os", "architecture", "cpu_model", "logical_cpu_count"):
        _require(host.get(name) not in (None, ""), f"host.{name} is required", errors)
    toolchain = data["toolchain"]
    for name in ("compiler", "cxx_standard", "bazel_version", "build_flags"):
        _require(toolchain.get(name) not in (None, ""), f"toolchain.{name} is required", errors)
    controls = data["controls"]
    _require(controls.get("repetitions", 0) >= 9, "at least 9 repetitions are required", errors)
    _require(controls.get("random_interleaving") is True,
             "random interleaving must be enabled", errors)
    _require(bool(controls.get("minimum_time")), "minimum_time is required", errors)
    _require(bool(controls.get("warmup_time")), "warmup_time is required", errors)
    validity = data["validity"]
    _require(validity.get("status") in ("valid", "suspect", "invalid"),
             "validity.status must be valid, suspect, or invalid", errors)
    _require(validity.get("status") == "valid" or bool(validity.get("note")),
             "non-valid measurements require a validity note", errors)
    raw = data["google_benchmark"]
    _require(isinstance(raw.get("context"), dict), "google_benchmark.context is required", errors)
    _require(isinstance(raw.get("benchmarks"), list) and bool(raw.get("benchmarks")),
             "google_benchmark.benchmarks must be non-empty", errors)
    expected_hash = data.get("content_sha256")
    unhashed = dict(data)
    unhashed.pop("content_sha256", None)
    _require(expected_hash == _sha256_json(unhashed), "content_sha256 does not match", errors)
    if errors:
        raise ValueError("; ".join(errors))


def _write_json(path, value):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(destination)


def _load(path):
    with Path(path).open() as stream:
        return json.load(stream)


def command_run(args):
    root = Path(_run(["git", "rev-parse", "--show-toplevel"], None))
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        raise ValueError("run requires a command after --")
    controls = {
        "repetitions": args.repetitions,
        "minimum_time": args.minimum_time,
        "warmup_time": args.warmup_time,
        "random_interleaving": True,
    }
    with tempfile.TemporaryDirectory(prefix="mbo-benchmark-") as temporary:
        raw_path = Path(temporary) / "raw.json"
        benchmark_flags = [
            f"--benchmark_out={raw_path}",
            "--benchmark_out_format=json",
            f"--benchmark_repetitions={args.repetitions}",
            f"--benchmark_min_time={args.minimum_time}",
            f"--benchmark_min_warmup_time={args.warmup_time}",
            "--benchmark_enable_random_interleaving=true",
            "--benchmark_report_aggregates_only=false",
        ]
        started = _utc_now()
        monotonic_start = time.monotonic()
        subprocess.run([*command, *benchmark_flags], cwd=root, check=True)
        duration = time.monotonic() - monotonic_start
        ended = _utc_now()
        result = artifact(
            component=args.component,
            target=args.target,
            raw=_load(raw_path),
            command=shlex.join([*command, *benchmark_flags]),
            configurations=args.config,
            baseline=args.baseline_commit,
            started=started,
            ended=ended,
            duration_seconds=duration,
            root=root,
            controls=controls,
            cxx_standard=args.cxx_standard,
            bazel_version=args.bazel_version or _optional_command(["bazel", "--version"]),
            validity=args.validity,
            validity_note=args.validity_note,
        )
        _write_json(args.output, result)


def command_validate(args):
    for name in args.files:
        validate(_load(name))


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="operation", required=True)
    run = commands.add_parser("run", help="run a benchmark and create an artifact")
    run.add_argument("--component", required=True)
    run.add_argument("--target", required=True)
    run.add_argument("--output", required=True)
    run.add_argument("--baseline-commit")
    run.add_argument("--config", action="append", default=[])
    run.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS)
    run.add_argument("--minimum-time", default=DEFAULT_MIN_TIME)
    run.add_argument("--warmup-time", type=float, default=DEFAULT_WARMUP_TIME)
    run.add_argument("--cxx-standard", default="c++20")
    run.add_argument("--bazel-version")
    run.add_argument("--validity", choices=("valid", "suspect", "invalid"), default="valid")
    run.add_argument("--validity-note")
    run.add_argument("command", nargs=argparse.REMAINDER)
    run.set_defaults(function=command_run)
    verify = commands.add_parser("validate", help="validate measurement artifacts")
    verify.add_argument("files", nargs="+")
    verify.set_defaults(function=command_validate)
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    try:
        args.function(args)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"benchmark_artifact: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
