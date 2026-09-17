#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for cache_cleanup.py."""

from __future__ import annotations

import unittest

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import cache_cleanup


class CacheCleanupTest(unittest.TestCase):

  def test_selects_entries_at_or_above_limit(self):
    self.assertEqual(
        cache_cleanup.oversized_cache_ids(
            [
                {"id": 1, "sizeInBytes": cache_cleanup.MAX_CACHE_BYTES - 1},
                {"id": 2, "sizeInBytes": cache_cleanup.MAX_CACHE_BYTES},
                {"id": 3, "sizeInBytes": cache_cleanup.MAX_CACHE_BYTES + 1},
            ]
        ),
        [2, 3],
    )

  def test_empty_inventory_needs_no_cleanup(self):
    self.assertEqual(cache_cleanup.oversized_cache_ids([]), [])

  def test_closed_prs_and_tags_are_removed_before_other_cleanup(self):
    caches = [
        {"id": 1, "ref": "refs/heads/main", "sizeInBytes": 1, "key": "trunk"},
        {"id": 2, "ref": "refs/pull/839/merge", "sizeInBytes": 1, "key": "trunk"},
        {"id": 3, "ref": "refs/pull/840/merge", "sizeInBytes": 1, "key": "trunk"},
        {"id": 4, "ref": "refs/tags/v0.5.0", "sizeInBytes": 1, "key": "release"},
        {"id": 5, "ref": "refs/heads/refs/tags/v0.4.0", "sizeInBytes": 1, "key": "release"},
        {"id": 6, "ref": "refs/heads/main", "sizeInBytes": cache_cleanup.MAX_CACHE_BYTES, "key": "old"},
    ]
    self.assertEqual(cache_cleanup.obsolete_cache_ids(caches, {839}), [2, 4, 5, 6])

  def test_retains_newest_usable_generation_per_configuration_and_ref(self):
    def entry(number, namespace="Linux-X64-default", ref="refs/heads/main", size=10):
      return {"id": number, "ref": ref, "sizeInBytes": size,
              "createdAt": f"2026-09-{number:02d}T00:00:00Z",
              "key": f"bazel-actions-v2-{namespace}-{'a' * 64}-{number}-1"}
    caches = [entry(1), entry(2), entry(3, size=cache_cleanup.MAX_CACHE_BYTES),
              entry(4, "macOS-ARM64-default"), entry(5, "Linux-X64-asan"),
              entry(6, ref="refs/pull/840/merge")]
    # A newer rejected upload must not cause the last usable main generation to be deleted.
    self.assertEqual(cache_cleanup.obsolete_cache_ids(caches, set()), [1, 3])

  def test_empty_generation_does_not_displace_previous_usable_cache(self):
    old = {"id": 1, "ref": "refs/heads/main", "sizeInBytes": 10,
           "createdAt": "2026-09-01T00:00:00Z",
           "key": f"bazel-actions-v2-Linux-X64-coverage-{'a' * 64}-1-1"}
    empty = dict(old, id=2, sizeInBytes=0, createdAt="2026-09-02T00:00:00Z",
                 key=f"bazel-actions-v2-Linux-X64-coverage-{'a' * 64}-2-1")
    self.assertEqual(cache_cleanup.obsolete_cache_ids([old, empty], set()), [])

  def test_immediate_retirement_is_scoped_and_preserves_newer_uploads(self):
    def entry(number, namespace="Linux-X64-asan", ref="refs/heads/main", size=10):
      return {"id": number, "ref": ref, "sizeInBytes": size,
              "createdAt": f"2026-09-{number:02d}T00:00:00Z",
              "key": f"bazel-actions-v2-{namespace}-{'a' * 64}-{number}-1"}
    caches = [entry(1), entry(2), entry(3), entry(4, "Linux-X64-tsan"),
              entry(5, "macOS-ARM64-asan"), entry(6, ref="refs/pull/843/merge")]
    self.assertEqual(cache_cleanup.replaced_cache_ids(caches, caches[1]["key"]), [1])
    self.assertEqual(cache_cleanup.replaced_cache_ids(caches, "missing"), [])
    self.assertEqual(cache_cleanup.replaced_cache_ids(caches, caches[5]["key"]), [])
    for size in (0, 1_000_000_001):
      with self.subTest(size=size):
        caches[1]["sizeInBytes"] = size
        self.assertEqual(cache_cleanup.replaced_cache_ids(caches, caches[1]["key"]), [])

  def test_retirement_waits_for_inventory_and_preserves_equal_timestamps(self):
    old = {"id": 1, "ref": "refs/heads/main", "sizeInBytes": 100,
           "createdAt": "2026-09-01T00:00:00Z",
           "key": f"bazel-actions-v2-Linux-X64-coverage-{'a' * 64}-1-1"}
    new = dict(old, id=2, sizeInBytes=600_000_000, createdAt="2026-09-02T00:00:00Z",
               key=f"bazel-actions-v2-Linux-X64-coverage-{'b' * 64}-2-1")
    self.assertEqual(cache_cleanup.replaced_cache_ids([old], new["key"]), [])
    self.assertEqual(cache_cleanup.replaced_cache_ids([old, new], new["key"]), [1])
    old["createdAt"] = new["createdAt"]
    self.assertEqual(cache_cleanup.replaced_cache_ids([old, new], new["key"]), [])

  def test_cache_budget_is_unchanged_and_upload_bound_is_lower(self):
    import bazel_cache
    self.assertEqual(cache_cleanup.MAX_CACHE_BYTES, 700_000_000)
    self.assertLess(bazel_cache.MAX_BYTES, cache_cleanup.MAX_CACHE_BYTES)


class WorkflowCachePolicyTest(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).parents[1]
        self.main = (root / ".github/workflows/main.yml").read_text(encoding="utf-8")
        self.runner = (root / ".github/workflows/test.yml").read_text(
            encoding="utf-8"
        )

    def test_reusable_runner_caches_only_bounded_disk_outputs(self):
        self.assertNotIn('path: "~/.cache/bazel"', self.runner)
        self.assertIn("uses: ./.github/actions/bazel-cache-restore", self.runner)
        self.assertNotIn("--experimental_disk_cache_gc_max_size", self.runner)
        self.assertIn("!cancelled() && github.ref == 'refs/heads/main'", self.runner)
        self.assertIn("timeout-minutes: 20", self.runner)

    def test_heavy_jobs_use_separate_bounded_caches(self):
        self.assertIn("namespace: clang-tidy", self.main)
        self.assertIn("bazel_config: coverage", self.main)
        self.assertIn("namespace: ${{ env.CACHE_KEY_PREFIX }}", self.runner)
        self.assertEqual(
            self.main.count("--experimental_disk_cache_gc_max_size"), 0
        )
        self.assertGreaterEqual(self.main.count("timeout-minutes: 20"), 1)

    def test_setup_bazel_does_not_own_build_caches(self):
        for workflow in (self.main, self.runner):
            setup_count = workflow.count("uses: bazel-contrib/setup-bazel@0.19.0")
            self.assertGreater(setup_count, 0)
            for setting in (
                "bazelisk-cache: false",
                "disk-cache: false",
                "external-cache: false",
                "repository-cache: false",
            ):
                self.assertEqual(workflow.count(setting), setup_count)
            self.assertNotIn("bazelbuild/setup-bazelisk", workflow)

    def test_preparation_profiles_are_scoped_and_preserved_after_failure(self):
        for workflow, collect_name, condition in (
            (self.runner, "Collect coverage", "always() && inputs.bazel_config == 'coverage'"),
        ):
            with self.subTest(workflow=collect_name):
                collect = workflow.split(f"      - name: {collect_name}\n", 1)[1]
                collect = collect.split("      - ", 1)[0]
                invocation = collect.split("          cp ", 1)[0]
                self.assertEqual(workflow.count("--profile="), 2)
                self.assertIn('--profile="${RUNNER_TEMP}/coverage-preparation.json.gz"', invocation)
                for task in ("repository_fetch", "starlark_repository_fn", "starlark_builtin_fn", "fetch"):
                    self.assertIn(f"--experimental_profile_additional_tasks={task} ", invocation)
                artifact = workflow.split("      - name: Preserve coverage preparation profiles\n", 1)[1]
                artifact = artifact.split("      - ", 1)[0]
                self.assertIn(f"if: {condition}\n", artifact)
                self.assertIn("name: coverage-preparation-profile\n", artifact)
                self.assertIn("path: ${{ runner.temp }}/coverage*-preparation.json.gz\n", artifact)
                self.assertIn("retention-days: 7\n", artifact)
                self.assertIn("if-no-files-found: warn\n", artifact)

    def test_coverage_starts_immediately_but_stays_in_final_gate(self):
        coverage = self.main.split("  coverage:\n", 1)[1].split("  test-bcr:\n", 1)[0]
        self.assertNotIn("needs:", coverage)
        done = self.main.split("  done:\n", 1)[1]
        self.assertRegex(done, r"needs:\s*\[[^\]]*\bcoverage\b")
        self.assertIn("contains(needs.*.result, 'failure')", done)
        self.assertIn("contains(needs.*.result, 'cancelled')", done)


if __name__ == "__main__":
  unittest.main()
