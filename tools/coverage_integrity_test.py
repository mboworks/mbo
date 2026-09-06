#!/usr/bin/env python3
"""Tests for tools/coverage_integrity.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_integrity  # noqa: E402


class CoverageIntegrityTest(unittest.TestCase):
    def test_scope_regressions_rejects_exclusions(self):
        base = {
            "include": ["mbo/**"],
            "exclude": ["mbo/**/*_test.cc"],
            "categories": {"types": {"include": ["mbo/types/**"]}},
        }
        candidate = {
            **base,
            "exclude": [*base["exclude"], "mbo/types/generated.h"],
        }

        self.assertEqual(
            ["coverage measurement scope was changed"],
            coverage_integrity.scope_regressions(candidate, base),
        )

    def test_scope_regressions_accepts_identical_scope(self):
        base = {
            "include": ["mbo/**"],
            "exclude": ["mbo/**/*_test.cc"],
            "categories": {"types": {"include": ["mbo/types/**"]}},
        }
        candidate = {
            **base,
            "minimum": {"lines": 95, "functions": 95, "branches": 85},
        }

        self.assertEqual([], coverage_integrity.scope_regressions(candidate, base))

    def test_scope_regressions_accepts_test_utility_header_exclusion(self):
        base = {
            "include": ["mbo/**"],
            "exclude": ["mbo/**/*_test.cc"],
            "categories": {"types": {"include": ["mbo/types/**"]}},
        }
        candidate = {
            **base,
            "exclude": [*base["exclude"], "mbo/**/*_test_util.h"],
        }

        self.assertEqual([], coverage_integrity.scope_regressions(candidate, base))

    def test_source_exclusion_regressions_rejects_added_or_changed_exclusions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            candidate = root / "candidate"
            base.mkdir()
            candidate.mkdir()
            (base / "a.h").write_text(
                "int old();  // LCOV_EXCL_LINE: unreachable.\nint stable();\n",
                encoding="utf-8",
            )
            (candidate / "a.h").write_text(
                "int old();  // LCOV_EXCL_LINE: changed reason.\n"
                "int stable();\n"
                "int added();  // LCOV_EXCL_FUNC_LINE: generated.\n",
                encoding="utf-8",
            )

            self.assertEqual(
                [
                    "source coverage exclusion was added or changed: "
                    "mbo/a.h:1: LCOV_EXCL_LINE",
                    "source coverage exclusion was added or changed: "
                    "mbo/a.h:3: LCOV_EXCL_FUNC_LINE",
                ],
                coverage_integrity.source_exclusion_regressions(
                    candidate, base, {"include": ["mbo/**"]}
                ),
            )

    def test_source_exclusion_regressions_allows_safe_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            candidate = root / "candidate"
            base.mkdir()
            candidate.mkdir()
            (base / "a.h").write_text(
                "int old();  // LCOV_EXCL_LINE: unreachable.\n", encoding="utf-8"
            )
            (candidate / "a.h").write_text("int old();\n", encoding="utf-8")
            (candidate / "merged.h").write_text(
                "int value();  // LCOV_MERGE_FUNC_LINE\n", encoding="utf-8"
            )
            (candidate / "a_test.cc").write_text(
                "int test();  // LCOV_EXCL_LINE\n", encoding="utf-8"
            )
            (candidate / "archive.tgz").write_bytes(b"\xff LCOV_EXCL_LINE")

            self.assertEqual(
                [],
                coverage_integrity.source_exclusion_regressions(
                    candidate,
                    base,
                    {"include": ["mbo/**"], "exclude": ["mbo/*_test.cc"]},
                ),
            )


if __name__ == "__main__":
    unittest.main()
