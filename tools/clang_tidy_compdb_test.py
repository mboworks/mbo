#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

import unittest

from tools import clang_tidy_compdb


def entry(path: str, configuration: str, *extra: str) -> dict[str, object]:
    return {
        "directory": "/checkout",
        "file": path,
        "arguments": [
            "clang++",
            "-std=c++20",
            *extra,
            f"-frandom-seed=bazel-out/{configuration}/bin/object.o",
            "-iquote",
            f"bazel-out/{configuration}/bin",
            "-MF",
            f"bazel-out/{configuration}/bin/object.d",
            "-c",
            path,
            "-o",
            f"bazel-out/{configuration}/bin/object.o",
        ],
    }


class ClangTidyCompdbTest(unittest.TestCase):
    def test_prefers_ordinary_entry_over_equivalent_fuzz_transition(self):
        ordinary = entry("mbo/file/glob.cc", "k8-fastbuild")
        fuzz = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-deadbeef",
            clang_tidy_compdb.FUZZ_DEFINE,
        )

        filtered, removed = clang_tidy_compdb.deduplicate([ordinary, fuzz])

        self.assertEqual(filtered, [ordinary])
        self.assertEqual(removed, 1)

    def test_deduplicates_meaningfully_different_commands(self):
        ordinary = entry("mbo/file/glob.cc", "k8-fastbuild")
        different = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-deadbeef",
            clang_tidy_compdb.FUZZ_DEFINE,
            "-DDIFFERENT_SEMANTICS",
        )

        filtered, removed = clang_tidy_compdb.deduplicate([different, ordinary])

        self.assertEqual(filtered, [ordinary])
        self.assertEqual(removed, 1)

    def test_retains_first_entry_when_all_duplicates_are_fuzz_commands(self):
        first = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-first",
            clang_tidy_compdb.FUZZ_DEFINE,
        )
        second = entry(
            "mbo/file/glob.cc",
            "k8-fastbuild-ST-second",
            clang_tidy_compdb.FUZZ_DEFINE,
            "-DDIFFERENT_SEMANTICS",
        )

        filtered, removed = clang_tidy_compdb.deduplicate([first, second])

        self.assertEqual(filtered, [first])
        self.assertEqual(removed, 1)

    def test_retains_unique_entries(self):
        first = entry("mbo/file/file.cc", "k8-fastbuild")
        second = entry("mbo/log/demangle.cc", "k8-fastbuild")

        filtered, removed = clang_tidy_compdb.deduplicate([first, second])

        self.assertEqual(filtered, [first, second])
        self.assertEqual(removed, 0)


if __name__ == "__main__":
    unittest.main()
