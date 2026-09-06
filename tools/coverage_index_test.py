#!/usr/bin/env python3
"""Tests for tools/coverage_index.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import coverage_index  # noqa: E402


def _summary(percent: float) -> dict:
    metric = {"covered": 9, "total": 10, "percent": percent}
    return {
        "measurements": {"overall": {name: metric for name in ("lines", "functions", "branches")}},
        "minimums": {"overall": {name: 80 for name in ("lines", "functions", "branches")}},
        "targets": {"overall": {name: 90 for name in ("lines", "functions", "branches")}},
        "enforcement": {"overall": {name: "medium" for name in ("lines", "functions", "branches")}},
    }


class CoverageIndexTest(unittest.TestCase):
    def test_report_contains_policy_table_and_lcov_link(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn("mbo coverage: pr/42", rendered)
        self.assertIn("95.00%", rendered)
        self.assertIn('href="lcov/"', rendered)
        self.assertIn('href="coverage-summary.json"', rendered)
        self.assertIn('href="coverage-meta.json"', rendered)
        self.assertIn('href="../../"', rendered)

    def test_report_status_is_ok_between_minimum_and_target(self):
        rendered = coverage_index.render_report(_summary(85.0), "pr/42")
        self.assertIn('<td class="status-ok">OK</td>', rendered)
        self.assertIn('<td class="medium" title="medium; enforce medium;', rendered)
        self.assertIn('<td class="policyCell">80/90&middot;M</td>', rendered)

    def test_report_status_identifies_metrics_below_minimum(self):
        summary = _summary(95.0)
        summary["measurements"]["overall"]["functions"] = {"covered": 7, "total": 10, "percent": 70.0}
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: F</td>', rendered)

    def test_report_supports_category_metrics_without_coverage_data(self):
        summary = _summary(95.0)
        summary["measurements"]["config"] = {
            "lines": {"covered": 11, "total": 11, "percent": 100.0},
            "functions": {"covered": 0, "total": 0, "percent": None},
            "branches": {"covered": 0, "total": 0, "percent": None},
        }
        for name in ("minimums", "targets", "enforcement"):
            summary[name]["config"] = {"lines": summary[name]["overall"]["lines"]}

        rendered = coverage_index.render_report(summary, "pr/42")

        self.assertIn("<td>n/a</td><td>0</td><td>0</td>", rendered)
        self.assertIn('<td class="policyCell">n/a</td>', rendered)
        self.assertIn('<td class="status-good">GOOD</td>', rendered)

    def test_high_enforcement_rejects_a_medium_rating(self):
        summary = _summary(85.0)
        summary["enforcement"]["overall"]["branches"] = "high"
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: B</td>', rendered)

    def test_dense_table_shows_effective_override_policy_per_row(self):
        summary = _summary(95.0)
        summary["measurements"]["extensions / new"] = {
            name: {"covered": 8, "total": 10, "percent": 80.0}
            for name in ("lines", "functions", "branches")
        }
        summary["minimums"]["extensions / new"] = {
            "lines": 75,
            "functions": 80,
            "branches": 60,
        }
        summary["targets"]["extensions / new"] = {
            "lines": 90,
            "functions": 90,
            "branches": 85,
        }
        summary["enforcement"]["extensions / new"] = {
            "lines": "medium",
            "functions": "medium",
            "branches": "high",
        }
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: B</td>', rendered)
        self.assertIn('<td class="policyCell policy-weaker">lower<br>75/90&middot;M</td>', rendered)
        self.assertIn('<td class="policyCell policy-mixed">mixed<br>60/85&middot;H</td>', rendered)
        self.assertIn('<td class="policyCell">default</td>', rendered)
        self.assertIn(">8</td>", rendered)
        self.assertIn(">10</td>", rendered)
        self.assertLess(rendered.index('colspan="3">Lines'), rendered.index('colspan="3">Branches'))
        self.assertLess(rendered.index('colspan="3">Branches'), rendered.index('colspan="3">Functions'))

    def test_rate_covered_and_total_share_the_rating_background(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn(
            '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">'
            "95.00%</td>",
            rendered,
        )
        self.assertGreaterEqual(
            rendered.count(
                '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">9</td>'
            ),
            3,
        )
        self.assertGreaterEqual(
            rendered.count(
                '<td class="high" title="high; enforce medium; medium at 80%; high at 90%">10</td>'
            ),
            3,
        )
        self.assertIn(".coverageTable { border-collapse: separate; border-spacing: 1px;", rendered)
        self.assertIn("background: #6688d4; color: #fff", rendered)
        self.assertIn(".coverageTable thead tr:first-child th { font-size: 120%; text-align: center; }", rendered)

    def test_policy_footnote_explains_notation_and_comparison(self):
        rendered = coverage_index.render_report(_summary(95.0), "pr/42")
        self.assertIn("Policy vs default<sup>*</sup>", rendered)
        self.assertIn("medium/high&middot;enforced-band", rendered)
        self.assertIn("90/92&middot;M", rendered)
        self.assertIn("compare policy strictness", rendered)
        self.assertIn("not\n    measured coverage", rendered)
        self.assertIn(".policyNote { font-size: 12px;", rendered)
        self.assertIn("text-align: left;", rendered)

    def test_equal_boundaries_use_one_compact_limit(self):
        policy = coverage_index.coverage_policy.MetricPolicy(98, 98, "high")
        self.assertEqual("98&middot;H", coverage_index._compact_policy(policy))

    def test_patch_uses_its_resolved_policy(self):
        summary = _summary(95.0)
        summary["patch"] = {
            "lines": {"covered": 9, "total": 10, "percent": 90.0},
            "functions": {"covered": 0, "total": 0, "percent": None},
            "branches": {"covered": 9, "total": 10, "percent": 90.0},
        }
        summary["patch_policy"] = {
            "minimum": {"lines": 80, "branches": 80},
            "target": {"lines": 95, "branches": 95},
            "enforce": {"lines": "high", "branches": "medium"},
        }
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('<td class="status-bad">BAD: L</td>', rendered)
        self.assertEqual(2, rendered.count('<td class="medium">90.00%</td>'))

    def test_override_reason_is_available_on_the_compact_policy_row(self):
        summary = _summary(95.0)
        summary["measurements"]["extensions / new"] = summary["measurements"]["overall"]
        summary["minimums"]["extensions / new"] = {name: 70 for name in coverage_index._METRICS}
        summary["targets"]["extensions / new"] = {name: 90 for name in coverage_index._METRICS}
        summary["enforcement"]["extensions / new"] = {name: "medium" for name in coverage_index._METRICS}
        summary["reasons"] = {"extensions / new": "New module onboarding."}
        rendered = coverage_index.render_report(summary, "pr/42")
        self.assertIn('title="New module onboarding.">extensions / new</td>', rendered)

    def test_site_shows_all_metrics_with_main_first_and_numeric_sorting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for report in ("main", "pr/9", "pr/42", "tag/0.9.0", "tag/0.10.0"):
                target = root / report
                target.mkdir(parents=True)
                (target / "index.html").touch()
                (target / "coverage-summary.json").write_text(json.dumps(_summary(95.0)))
                metadata = coverage_index.report_metadata(
                    _summary(95.0),
                    report,
                    "2026-08-22T09:59:00Z",
                    "2026-08-22T10:00:00Z",
                    "2026-08-22T10:01:00Z",
                    1,
                    1,
                    "abc",
                )
                (target / "coverage-meta.json").write_text(json.dumps(metadata))

            rendered = coverage_index.render_site(root)

            self.assertIn("PR 42", rendered)
            self.assertIn("PR 9", rendered)
            self.assertIn("release 0.10.0", rendered)
            self.assertIn("release 0.9.0", rendered)
            self.assertIn(
                'href="https://github.com/mboworks/mbo/releases/tag/v0.10.0">release v0.10.0</a>',
                rendered,
            )
            self.assertIn('href="https://github.com/mboworks/mbo/pull/42">PR #42</a>', rendered)
            self.assertIn('href="pr/42/coverage-summary.json">JSON</a>', rendered)
            self.assertIn("2026-08-22 10:01:00 UTC", rendered)
            self.assertIn('href="https://github.com/mboworks/mbo/commit/abc"><code>abc</code></a>', rendered)
            self.assertIn('href="https://github.com/mboworks/mbo/actions/runs/1">run 1</a>', rendered)
            self.assertIn("font-variant-numeric: tabular-nums", rendered)
            self.assertLess(rendered.index('href="main/"'), rendered.index('href="tag/0.10.0/"'))
            self.assertLess(rendered.index('href="tag/0.10.0/"'), rendered.index('href="tag/0.9.0/"'))
            self.assertLess(rendered.index('href="tag/0.9.0/"'), rendered.index('href="pr/42/"'))
            self.assertLess(rendered.index('href="pr/42/"'), rendered.index('href="pr/9/"'))
            self.assertNotIn("<ul>", rendered)

    def test_empty_site_says_no_reports_are_available(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIn("No coverage reports are available", coverage_index.render_site(Path(directory)))

    def test_legacy_report_does_not_link_placeholder_metadata(self):
        metadata = coverage_index.report_metadata(
            _summary(95.0),
            "pr/9",
            "1970-01-01T00:00:00Z",
            "1970-01-01T00:00:00Z",
            "1970-01-01T00:00:00Z",
            0,
            0,
            "legacy",
        )
        row = coverage_index._short_row(metadata)
        self.assertIn('href="https://github.com/mboworks/mbo/pull/9">PR #9</a>', row)
        self.assertNotIn("actions/runs/0", row)
        self.assertNotIn("commit/legacy", row)

    def test_newer_report_wins_even_when_an_older_run_finishes_later(self):
        summary = _summary(95.0)
        old = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:00:00Z",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:10:00Z",
            100,
            1,
            "old",
        )
        new = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:02:00Z",
            "2026-08-22T10:03:00Z",
            101,
            1,
            "new",
        )
        rerun = coverage_index.report_metadata(
            summary,
            "pr/42",
            "2026-08-22T10:01:00Z",
            "2026-08-22T10:11:00Z",
            "2026-08-22T10:12:00Z",
            101,
            2,
            "new",
        )
        self.assertTrue(coverage_index.is_newer(new, old))
        self.assertFalse(coverage_index.is_newer(old, new))
        self.assertTrue(coverage_index.is_newer(rerun, new))
        self.assertEqual(
            {"pr/42": rerun}, coverage_index.latest_metadata([new, old, rerun, new])
        )
        same_tick_new_run = {
            **old,
            "source": {**old["source"], "run_id": 102, "run_attempt": 1},
        }
        same_tick_old_rerun = {
            **old,
            "source": {**old["source"], "run_id": 100, "run_attempt": 9},
        }
        self.assertTrue(
            coverage_index.is_newer(same_tick_new_run, same_tick_old_rerun)
        )


if __name__ == "__main__":
    unittest.main()
