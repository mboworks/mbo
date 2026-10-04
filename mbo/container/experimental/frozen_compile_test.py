#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Compiler-level frozen-container contract tests using the actual Bazel compile flags.

Run after ./compile_commands-update.sh. A successful baseline compilation is required
before accepting any expected failure, so missing toolchains/headers cannot masquerade
as a passing negative test. No compiler constexpr limits are raised.
"""

import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def compiler_command(source="mbo/container/experimental/frozen_probe.cc"):
    """Return the repository compiler and flags without action-owned outputs."""
    rows = json.loads((ROOT / "compile_commands.json").read_text())
    row = next(entry for entry in rows if entry["file"].endswith(source))
    arguments = row.get("arguments") or shlex.split(row["command"])
    result = []
    skip = False
    for argument in arguments:
        if skip:
            skip = False
        elif argument in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
        elif argument in ("-c", "-MD", "-MMD") or argument == row["file"]:
            continue
        elif argument.startswith(("-frandom-seed=", "-fdiagnostics-color=")):
            continue
        else:
            result.append(argument)
    return result, Path(row["directory"])


PREFIX = """
#include <array>
#include <string_view>
#include "mbo/container/experimental/frozen_map.h"
#include "mbo/container/experimental/frozen_set.h"
using namespace mbo::container::experimental;
struct IdentityHash {
  constexpr unsigned long long operator()(int key) const { return key; }
};
struct ConstantHash {
  constexpr unsigned long long operator()(int) const { return 0; }
};
"""

CASES = {
    "conflicting_duplicate": (
        'constexpr FrozenMap<int, int, 2> kTable({{1, 2}, {1, 3}});',
        "Frozen conflicting duplicate key",
    ),
    "hash_collision": (
        'constexpr FrozenSet<int, 2, ConstantHash> kTable({1, 2});',
        "Frozen distinct keys have identical hashes",
    ),
    "seed_exhaustion": (
        'constexpr FrozenSet<int, FrozenOptions{.capacity=2, .max_seed=0}, IdentityHash> kTable({0, 2});',
        "Frozen perfect hash seed search exhausted",
    ),
    "occupied_slot_collision": (
        'constexpr FrozenSet<int, FrozenOptions{.capacity=2, .slots=2, .max_seed=1}, IdentityHash> kTable({2, 4});',
        "Frozen perfect hash seed search exhausted",
    ),
    "work_exhaustion": (
        'constexpr FrozenSet<int, FrozenOptions{.capacity=2, .max_work=0}> kTable({1});',
        "Frozen construction work budget exhausted",
    ),
    "byte_exhaustion": (
        'constexpr FrozenSet<std::string_view, FrozenOptions{.capacity=1, .max_key_bytes=2}> kTable({"abc"});',
        "Frozen key byte budget exhausted",
    ),
    "element_capacity": (
        'constexpr FrozenSet<int, 1> kTable({1, 2});',
        "Frozen element capacity exceeded",
    ),
    "slot_capacity": (
        'constexpr FrozenSet<int, FrozenOptions{.capacity=2, .slots=1}> kTable({1, 2});',
        "Frozen slot capacity exceeded",
    ),
    "negative_capacity": (
        'constexpr FrozenSet<int, -1> kTable;',
        "Frozen capacity must be nonnegative",
    ),
    "excessive_capacity": (
        'constexpr FrozenSet<int, 4097> kTable;',
        "Frozen capacity exceeds construction bound",
    ),
    "reference_key": (
        'static_assert(sizeof(FrozenMap<int&, int, 1, IdentityHash>) > 0);',
        "Frozen keys must be object types",
    ),
    "reference_value": (
        'static_assert(sizeof(FrozenMap<int, int&, 1>) > 0);',
        "Frozen mapped values must be object types",
    ),
    "dangling_view": (
        'constexpr auto kTable = [] { const char bytes[] = "abc"; '
        'return FrozenSet<std::string_view, 1>({std::string_view(bytes, 3)}); }();',
        "constant expression",
    ),
}


class FrozenCompileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.command, cls.directory = compiler_command()
        cls.temporary = tempfile.TemporaryDirectory(prefix="mbo-frozen-compile-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.source = Path(cls.temporary.name) / "case.cc"
        cls.source.write_text(PREFIX + 'constexpr FrozenSet<int, 2> kTable({1, 2}); static_assert(kTable.size() == 2);')
        baseline = cls.compile()
        if baseline.returncode:
            raise RuntimeError("baseline compilation failed:\n" + baseline.stderr)

    @classmethod
    def compile(cls, extra=()):
        return subprocess.run(
            [*cls.command, *extra, "-fsyntax-only", str(cls.source)],
            cwd=cls.directory, text=True, capture_output=True, check=False,
        )

    def test_construction_diagnostics(self):
        for name, (source, message) in CASES.items():
            with self.subTest(name=name):
                self.source.write_text(PREFIX + source)
                result = self.compile()
                self.assertNotEqual(result.returncode, 0, "invalid construction compiled")
                self.assertIn(message, result.stderr)
                self.assertNotIn("maximum step limit", result.stderr)

    def test_cpp26_interface(self):
        self.source.write_text(PREFIX + """
constexpr FrozenMap<int, int, 2> kMap({{1, 2}, {3, 4}});
constexpr FrozenSet<int, 2> kSet({1, 3});
static_assert(kMap.at(1) == 2 && *kSet.lookup(3) == 3);
static_assert(kMap.contains(3) && kSet.count(1) == 1);
static_assert(kSet.bucket_size(kSet.bucket(1)) == 1);
static_assert(std::forward_iterator<decltype(kMap)::iterator>);
static_assert(std::forward_iterator<decltype(kSet)::local_iterator>);
""")
        result = self.compile(("-std=c++26",))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_sparse_maximum_capacity_uses_default_constexpr_limits(self):
        self.source.write_text(PREFIX + """
constexpr FrozenSet<int, 4096> kEmpty;
constexpr FrozenSet<int, 4096> kSingleton({42});
static_assert(kEmpty.empty());
static_assert(kSingleton.size() == 1 && kSingleton.contains(42));
static_assert(!kSingleton.contains(43));
""")
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_entire_public_suite_compiles_in_cpp26(self):
        command, directory = compiler_command("mbo/container/experimental/frozen_test.cc")
        result = subprocess.run(
            [*command, "-std=c++26", "-fsyntax-only",
             str(directory / "mbo/container/experimental/frozen_test.cc")],
            cwd=directory, text=True, capture_output=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
