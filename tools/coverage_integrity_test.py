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

    def test_source_directive_regressions_rejects_added_or_changed_directives(self):
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
                "int added();  // LCOV_MERGE_FUNC_LINE: template.\n",
                encoding="utf-8",
            )

            self.assertEqual(
                [
                    "source coverage directive was added or changed: "
                    "mbo/a.h:1: LCOV_EXCL_LINE",
                    "source coverage directive was added or changed: "
                    "mbo/a.h:3: LCOV_MERGE_FUNC_LINE",
                ],
                coverage_integrity.source_directive_regressions(
                    candidate, base, {"include": ["mbo/**"]}
                ),
            )

    def test_source_directive_regressions_allows_removal_and_ignores_excluded_files(self):
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
            (candidate / "a_test.cc").write_text(
                "int test();  // LCOV_EXCL_LINE\n", encoding="utf-8"
            )
            (candidate / "archive.tgz").write_bytes(b"\xff LCOV_EXCL_LINE")

            self.assertEqual(
                [],
                coverage_integrity.source_directive_regressions(
                    candidate,
                    base,
                    {"include": ["mbo/**"], "exclude": ["mbo/*_test.cc"]},
                ),
            )


if __name__ == "__main__":
    unittest.main()
