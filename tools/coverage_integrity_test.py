#!/usr/bin/env python3
"""Tests for tools/coverage_integrity.py."""

import sys
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


if __name__ == "__main__":
    unittest.main()
