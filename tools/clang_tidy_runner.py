#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run clang-tidy concurrently while reporting deterministic progress."""

import argparse
import concurrent.futures
import dataclasses
import os
import signal
import subprocess
import sys
import threading
import time
from typing import IO, Optional, Sequence


@dataclasses.dataclass(frozen=True)
class Task:
    path: str
    checks: Optional[str] = None


@dataclasses.dataclass(frozen=True)
class Result:
    task: Task
    returncode: int
    output: str
    seconds: float


class ProcessRegistry:
    """Tracks active children so interruption terminates the complete pool."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._processes: set[subprocess.Popen[str]] = set()
        self._stopping = False

    def start(self, command: Sequence[str]) -> Optional[subprocess.Popen[str]]:
        # Starting and registration are atomic with respect to shutdown: queued
        # tasks cannot launch a child after the stop signal has been observed.
        with self._lock:
            if self._stopping:
                return None
            process = subprocess.Popen(
                command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            )
            self._processes.add(process)
            return process

    def remove(self, process: subprocess.Popen[str]) -> None:
        with self._lock:
            self._processes.discard(process)

    def terminate_all(self) -> None:
        with self._lock:
            self._stopping = True
            processes = list(self._processes)
        for process in processes:
            if process.poll() is None:
                process.terminate()
        deadline = time.monotonic() + 2.0
        for process in processes:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                pass
        for process in processes:
            if process.poll() is None:
                process.kill()


def run_task(
    task: Task,
    clang_tidy: str,
    compile_database: str,
    registry: ProcessRegistry,
) -> Result:
    command = [clang_tidy, "--quiet", "--header-filter=(^|/)mbo/"]
    if task.checks:
        command.append(f"--checks={task.checks}")
    command.extend(["-p", compile_database, task.path])
    started = time.monotonic()
    process = registry.start(command)
    if process is None:
        return Result(task, 130, "", time.monotonic() - started)
    try:
        output, _ = process.communicate()
    finally:
        registry.remove(process)
    return Result(task, process.returncode, output, time.monotonic() - started)


def progress_line(completed: int, total: int, result: Result) -> str:
    width = len(str(total))
    percent = 100.0 * completed / total
    status = "PASS" if result.returncode == 0 else "FAIL"
    return (
        f"[{completed:{width}d}/{total} {percent:5.1f}%] "
        f"{status} {result.task.path} ({result.seconds:.1f}s)"
    )


def run_all(
    tasks: Sequence[Task],
    clang_tidy: str,
    compile_database: str,
    jobs: int,
    output_path: str,
    stream: IO[str] = sys.stdout,
) -> int:
    total = len(tasks)
    if total == 0:
        print("clang-tidy: no translation units selected", file=stream, flush=True)
        return 0

    workers = max(1, min(jobs, total))
    print(
        f"clang-tidy: {total} translation unit(s), {workers} worker(s)",
        file=stream,
        flush=True,
    )
    registry = ProcessRegistry()
    failed = 0
    started = time.monotonic()

    def interrupt(_signum: int, _frame: object) -> None:
        raise KeyboardInterrupt

    old_sigterm = signal.signal(signal.SIGTERM, interrupt)
    try:
        with open(output_path, "w", encoding="utf-8") as output_file:
            with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
                futures = {
                    executor.submit(
                        run_task, task, clang_tidy, compile_database, registry
                    ): task
                    for task in tasks
                }
                try:
                    for completed, future in enumerate(
                        concurrent.futures.as_completed(futures), start=1
                    ):
                        result = future.result()
                        print(progress_line(completed, total, result), file=stream, flush=True)
                        if result.output:
                            output_file.write(result.output)
                            output_file.flush()
                        if result.returncode != 0:
                            failed += 1
                            if result.output:
                                print(result.output, end="", file=stream, flush=True)
                except BaseException as error:
                    registry.terminate_all()
                    for future in futures:
                        future.cancel()
                    if not isinstance(error, KeyboardInterrupt):
                        raise
                    print("clang-tidy: interrupted; worker pool terminated", file=stream, flush=True)
                    return 130
    finally:
        signal.signal(signal.SIGTERM, old_sigterm)

    elapsed = time.monotonic() - started
    passed = total - failed
    print(
        f"clang-tidy: {passed} passed, {failed} failed in {elapsed:.1f}s",
        file=stream,
        flush=True,
    )
    return 1 if failed else 0


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang-tidy", required=True)
    parser.add_argument("--compile-database", required=True)
    parser.add_argument("--jobs", type=int, default=max(1, min(2, (os.cpu_count() or 1) - 1)))
    parser.add_argument("--output", required=True)
    parser.add_argument("--source", action="append", default=[])
    parser.add_argument("--test", action="append", default=[])
    parser.add_argument("--test-disabled-checks", required=True)
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if args.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    tasks = [Task(path) for path in args.source]
    tasks.extend(Task(path, args.test_disabled_checks) for path in args.test)
    return run_all(tasks, args.clang_tidy, args.compile_database, args.jobs, args.output)


if __name__ == "__main__":
    sys.exit(main())
