#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Build and run independent consumers of an actual release archive.

Packaging uses a disposable clone of the source's committed HEAD. No release tag,
remote write, caller index modification, or publication is performed.
"""

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(arguments, directory, **kwargs):
    subprocess.run(arguments, cwd=directory, check=True, **kwargs)


def dependency_version(module, name):
    match = re.search(
        rf'\b(?:bazel_dep|module)\(\s*name\s*=\s*"{re.escape(name)}",\s*version\s*=\s*"([^"]+)"',
        module,
    )
    if not match:
        raise ValueError(f"missing literal module version for {name}")
    return match[1]


def prepare(source, directory):
    head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=source, text=True
    ).strip()
    checkout = directory / "checkout"
    environment = dict(os.environ, GIT_LFS_SKIP_SMUDGE="1")
    run(["git", "clone", "--quiet", "--shared", "--no-checkout", str(source), str(checkout)],
        directory, env=environment)
    run(["git", "-c", "advice.detachedHead=false", "checkout", "--quiet", "--detach", head],
        checkout, env=environment)
    version = dependency_version((checkout / "MODULE.bazel").read_text(), "mboworks_mbo")
    print(f"Packaging committed HEAD {head} as {version}", flush=True)
    run(["bash", ".github/workflows/release_prep.sh"], checkout,
        env=dict(environment, GITHUB_REF_NAME=version))
    return checkout / f"mbo-{version}.tar.gz"


def extract(archive, directory):
    with tarfile.open(archive) as package:
        members = package.getmembers()
        roots = {PurePosixPath(member.name).parts[0] for member in members if member.name}
        if len(roots) != 1 or not re.fullmatch(r"mbo-[0-9]+\.[0-9]+\.[0-9]+", next(iter(roots))):
            raise ValueError("archive must have one numeric-version mbo prefix")
        prefix = next(iter(roots))
        for member in members:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts or not (member.isfile() or member.isdir()):
                raise ValueError(f"unsafe archive member: {member.name}")
            if len(path.parts) == 2 and path.name.casefold() == "version":
                raise ValueError("VERSION shadows the standard <version> header")
        # All paths, entry types, and the unique prefix were checked before extraction.
        package.extractall(directory)
    source = directory / prefix
    module = (source / "MODULE.bazel").read_text()
    version = dependency_version(module, "mboworks_mbo")
    if prefix != f"mbo-{version}" or (source / "VERSION.txt").read_text().strip() != version:
        raise ValueError("archive prefix, MODULE.bazel, and VERSION.txt must agree")
    for name in ("LICENSE", "NOTICE", "CMakeLists.txt", "mbo/strings/string_interner.h",
                 "mbo/container/experimental/hamt/hamt_flat_map.h"):
        if not (source / name).is_file():
            raise ValueError(f"release archive is missing {name}")
    if (source / "bazelmod/BUILD.bazel").exists():
        raise ValueError("release archive exposes the dev-only Hedron package")
    return source


def consumer(source, directory):
    directory.mkdir()
    shutil.copy2(ROOT / "tools/release_consumer.cc", directory / "consumer.cc")
    module = (source / "MODULE.bazel").read_text()
    version = dependency_version(module, "mboworks_mbo")
    rules_cc = dependency_version(module, "rules_cc")
    (directory / "MODULE.bazel").write_text(
        'module(name = "mbo_release_consumer")\n'
        f'bazel_dep(name = "mboworks_mbo", version = {json.dumps(version)})\n'
        f'bazel_dep(name = "rules_cc", version = {json.dumps(rules_cc)})\n'
        'local_path_override(module_name = "mboworks_mbo", '
        f'path = {json.dumps(str(source))})\n'
    )
    (directory / "BUILD.bazel").write_text(
        'load("@rules_cc//cc:cc_binary.bzl", "cc_binary")\n'
        'cc_binary(name = "consumer", srcs = ["consumer.cc"], deps = [\n'
        '    "@mboworks_mbo//mbo/container:limited_map_cc",\n'
        '    "@mboworks_mbo//mbo/container:segmented_deque_cc",\n'
        '    "@mboworks_mbo//mbo/container:segmented_vector_cc",\n'
        '    "@mboworks_mbo//mbo/container/experimental:frozen_map_cc",\n'
        '    "@mboworks_mbo//mbo/container/experimental/hamt:hamt_flat_map_cc",\n'
        '    "@mboworks_mbo//mbo/strings:string_interner_cc",\n'
        '    "@mboworks_mbo//mbo/types:string_or_view_cc",\n'
        '])\n'
    )
    (directory / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.28)\n'
        'project(mbo_release_consumer LANGUAGES CXX)\n'
        'set(MBO_BUILD_TESTING OFF CACHE BOOL "" FORCE)\n'
        'set(MBO_BUILD_BINARIES OFF CACHE BOOL "" FORCE)\n'
        f'add_subdirectory({json.dumps(str(source))} mbo)\n'
        'add_executable(consumer consumer.cc)\n'
        'target_link_libraries(consumer PRIVATE mbo::container mbo::strings mbo::types)\n'
        'enable_testing()\n'
        'add_test(NAME release_consumer COMMAND consumer)\n'
    )


def build(source, directory, system, jobs, bazel_version):
    consumer(source, directory)
    if system in ("bazel", "all"):
        environment = dict(os.environ, USE_BAZEL_VERSION=bazel_version)
        flags = ["--cxxopt=-std=c++23", "--host_cxxopt=-std=c++23", f"--jobs={jobs}"]
        if sys.platform == "darwin":
            flags.extend(["--macos_minimum_os=11.0", "--host_macos_minimum_os=11.0"])
        run(["bazel", "--batch", "run", *flags, "//:consumer"], directory, env=environment)
        # This must also load packages recursively without referencing absent dev repositories.
        run(["bazel", "--batch", "query", "@mboworks_mbo//...", "--output=label"],
            directory, env=environment, stdout=subprocess.DEVNULL)
    if system in ("cmake", "all"):
        run(["cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"], directory)
        run(["cmake", "--build", "build", "--parallel", str(jobs)], directory)
        run(["ctest", "--test-dir", "build", "--output-on-failure"], directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--archive", type=Path, help="validate an already packaged release")
    parser.add_argument("--build-system", choices=("bazel", "cmake", "all", "none"), default="all")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--bazel-version", default=(ROOT / ".bazelversion").read_text().strip())
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    with tempfile.TemporaryDirectory(prefix="mbo-release-consumer-") as temporary:
        directory = Path(temporary)
        archive = args.archive.resolve() if args.archive else prepare(args.source.resolve(), directory)
        source = extract(archive, directory)
        if args.build_system != "none":
            build(source, directory / "consumer", args.build_system, args.jobs, args.bazel_version)
        print(f"Release artifact validated: {archive.name} ({args.build_system})")


if __name__ == "__main__":
    main()
