#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for cache measurement handoff and final storage reporting."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parent))
import cache_report


class CacheReportTest(unittest.TestCase):
    def test_report_counts_all_refs_and_uses_exact_main_upload(self):
        key = f"bazel-actions-v2-Linux-X64-coverage-{'a' * 64}-123-1"
        metrics = [{"key": key, "before": 4_200_000_000, "uncompressed": 4_000_000_000, "evicted": 2}]
        inventory = [{"key": key, "ref": "refs/heads/main", "sizeInBytes": 1_000_000_000},
                     {"key": key, "ref": "refs/pull/1/merge", "sizeInBytes": 100_000_000},
                     {"key": "trunk", "ref": "refs/heads/main", "sizeInBytes": 200_000_000}]
        report = cache_report.render(metrics, inventory)
        self.assertIn("1,300,000,000 bytes (13.00% of 10 GB)", report)
        self.assertIn("8,700,000,000 bytes", report)
        rows = [line for line in report.splitlines() if line.startswith("|")]
        self.assertEqual([cell.strip() for cell in rows[-1].split("|")[1:-1]],
                         ["Linux-X64-coverage", "4,200,000,000", "4,000,000,000", "1,000,000,000", "2"])
        self.assertEqual(len({tuple(i for i, char in enumerate(row) if char == "|") for row in rows}), 1)

    def test_missing_measurements_and_over_budget_are_explicit(self):
        report = cache_report.render([], [{"sizeInBytes": 11_000_000_000}])
        self.assertIn("110.00%", report)
        self.assertIn("Over budget: **1,000,000,000 bytes**", report)
        self.assertIn("No per-job cache measurements", report)
        metric = {"key": "missing", "before": 10, "uncompressed": 9, "evicted": 1}
        self.assertIn("Unavailable", cache_report.render([metric], []))
        metric["compressed"] = 3
        self.assertNotIn("| Unavailable", cache_report.render([metric], []))

    def test_cli_measure_record_and_render_survive_cache_retirement(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            (cache / "cas").mkdir(parents=True)
            (cache / "cas/output").write_bytes(b"1234")
            metrics = root / "metrics"
            metrics.mkdir()
            record = metrics / "measurement.json"
            key = f"bazel-actions-v2-Linux-X64-coverage-{'a' * 64}-123-1"
            scripts = Path(__file__).resolve().parent
            subprocess.run([sys.executable, str(scripts / "bazel_cache.py"), str(cache),
                            "--key", key, "--report", str(record)], check=True, capture_output=True)
            self.assertEqual(json.loads(record.read_text())["uncompressed"], 4)
            inventory = [{"key": key, "ref": "refs/heads/main", "sizeInBytes": 2}]
            subprocess.run([sys.executable, str(scripts / "cache_report.py"), "--record", str(record)],
                           input=json.dumps(inventory), text=True, check=True, capture_output=True)
            self.assertEqual(json.loads(record.read_text())["compressed"], 2)
            result = subprocess.run([sys.executable, str(scripts / "cache_report.py"), "--metrics", str(metrics)],
                                    input="[]", text=True, check=True, capture_output=True)
            self.assertIn("Linux-X64-coverage", result.stdout)
            self.assertNotIn("| Unavailable", result.stdout)

    def test_main_done_reports_even_when_a_dependency_fails(self):
        root = Path(__file__).resolve().parent.parent
        workflow = (root / ".github/workflows/main.yml").read_text()
        done = workflow.split("  done:", 1)[1]
        self.assertIn("if: always() && github.ref == 'refs/heads/main'", done)
        self.assertLess(done.index("Report cache storage"), done.index("Fail if any dependency"))
        self.assertIn("pattern: cache-metrics-*", done)
        self.assertIn("--limit 10000", done)
        save = (root / ".github/actions/bazel-cache-save/action.yml").read_text()
        self.assertIn("steps.bound.outcome == 'success'", save)
        self.assertIn("retention-days: 1", save)


if __name__ == "__main__":
    unittest.main()
