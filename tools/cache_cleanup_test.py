#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for main-branch GitHub Actions cache selection."""

from __future__ import annotations

import io
import json
import os
import sys
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cache_cleanup  # noqa: E402


_PREFIX = "ubuntu-latest-bzlmod_gcc_14__opt_bazel9.2.0-refs/heads/main"
_ROOT = Path(__file__).resolve().parents[1]
_CACHES = [
    {"id": 3, "key": f"{_PREFIX}-new", "createdAt": "2026-08-16T12:00:00Z"},
    {"id": 1, "key": f"{_PREFIX}-old", "createdAt": "2026-08-14T12:00:00Z"},
    {"id": 4, "key": f"{_PREFIX}-other", "createdAt": "2026-08-15T12:00:00Z"},
    {"id": 2, "key": "unrelated-prefix", "createdAt": "2026-08-13T12:00:00Z"},
]


class ExpiredCacheIdsTest(unittest.TestCase):
    def test_deletes_matching_caches_except_the_newest(self):
        self.assertEqual(cache_cleanup.expired_cache_ids(_CACHES, _PREFIX), [1, 4])

    def test_does_not_match_a_literal_shell_placeholder(self):
        self.assertEqual(cache_cleanup.expired_cache_ids(_CACHES, "${cacheKeyPrefix}"), [])

    def test_keeps_a_single_matching_cache(self):
        self.assertEqual(cache_cleanup.expired_cache_ids(_CACHES[:1], _PREFIX), [])

    def test_keeps_everything_when_nothing_matches(self):
        self.assertEqual(cache_cleanup.expired_cache_ids(_CACHES, "macos-26-"), [])


class MainTest(unittest.TestCase):
    def test_prints_one_expired_id_per_line(self):
        original_stdin = sys.stdin
        sys.stdin = io.StringIO(json.dumps(_CACHES))
        try:
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(cache_cleanup.main(["cache_cleanup.py", _PREFIX]), 0)
        finally:
            sys.stdin = original_stdin
        self.assertEqual(output.getvalue(), "1\n4\n")


class WorkflowCachePolicyTest(unittest.TestCase):
    def test_uses_setup_bazel_without_legacy_cache(self):
        for path in (".github/workflows/main.yml", ".github/workflows/test.yml"):
            with self.subTest(path=path):
                workflow = (_ROOT / path).read_text(encoding="utf-8")
                self.assertIn("uses: bazel-contrib/setup-bazel@0.19.0", workflow)
                self.assertNotIn("uses: bazelbuild/setup-bazelisk", workflow)


if __name__ == "__main__":
    unittest.main()
