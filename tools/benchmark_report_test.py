# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for raw-repetition reporting safeguards."""

import unittest
import json
import tempfile
import copy
import contextlib
import io
from pathlib import Path

from tools import benchmark_report as subject
from tools import benchmark_artifact_test


class BenchmarkReportTest(unittest.TestCase):
    def setUp(self):
        fixture = benchmark_artifact_test.BenchmarkArtifactTest()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        fixture.raw["benchmarks"] = [
            {"name": "Example", "cpu_time": index + 1, "time_unit": "us",
             "iterations": 10, "repetition_index": index}
            for index in range(9)
        ]
        self.data = fixture.make()

    def rehash(self):
        unhashed = dict(self.data)
        unhashed.pop("content_sha256")
        self.data["content_sha256"] = subject.benchmark_artifact._sha256_json(unhashed)

    def test_explicit_normalization_and_best_three(self):
        row = subject.summarize(self.data, operations_per_iteration=10)["benchmarks"][0]
        self.assertEqual(row["median_cpu_ns"], 500)
        self.assertEqual(row["best_three_mean_cpu_ns"], 200)
        self.assertEqual(row["samples"], 9)

    def test_measurement_provenance_is_preserved_without_aliasing(self):
        report = subject.summarize(self.data)
        self.assertEqual(report["measurement"]["host"], self.data["host"])
        self.assertEqual(report["measurement"]["toolchain"], self.data["toolchain"])
        report["measurement"]["host"]["cpu_model"] = "Changed"
        self.assertEqual(self.data["host"]["cpu_model"], "Test CPU")

    def test_missing_and_duplicate_repetitions_are_rejected(self):
        self.data["google_benchmark"]["benchmarks"].pop()
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)
        self.data["google_benchmark"]["benchmarks"].append(dict(self.data["google_benchmark"]["benchmarks"][0]))
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)

    def test_dirty_source_is_rejected(self):
        self.data["git"]["dirty"] = True
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)

    def test_debug_library_is_rejected(self):
        self.data["google_benchmark"]["context"]["library_build_type"] = "debug"
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)

    def test_nonfinite_times_are_rejected(self):
        self.data["google_benchmark"]["benchmarks"][0]["cpu_time"] = float("inf")
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)

    def test_unit_conversion_overflow_is_rejected(self):
        self.data["google_benchmark"]["benchmarks"][0]["cpu_time"] = 1e308
        self.data["google_benchmark"]["benchmarks"][0]["time_unit"] = "s"
        self.rehash()
        with self.assertRaisesRegex(ValueError, "normalized CPU time"):
            subject.summarize(self.data)

    def test_even_sample_median_does_not_overflow(self):
        rows = self.data["google_benchmark"]["benchmarks"]
        rows.append(dict(rows[0], repetition_index=9))
        for row in rows:
            row.update(cpu_time=1e308, time_unit="ns")
        self.rehash()
        report = subject.summarize(self.data)
        self.assertEqual(report["benchmarks"][0]["median_cpu_ns"], 1e308)
        self.assertEqual(report["benchmarks"][0]["best_three_mean_cpu_ns"], 1e308)

    def test_svg_scale_does_not_overflow_for_subnormal_times(self):
        for row in self.data["google_benchmark"]["benchmarks"]:
            row.update(cpu_time=5e-324, time_unit="ns")
        self.rehash()
        svg = subject.render_svg(subject.summarize(self.data), title="Small times", names=["Example"])
        self.assertIn('width="480.000"', svg)
        self.assertNotIn("inf", svg)
        self.assertNotIn("nan", svg)

    def test_invalid_observation_fields_are_rejected(self):
        original = copy.deepcopy(self.data)
        for field, value in [("iterations", True), ("iterations", 0), ("iterations", "10"),
                             ("repetition_index", True), ("repetition_index", -1),
                             ("repetition_index", None), ("cpu_time", True),
                             ("cpu_time", 0), ("time_unit", "cycles"), ("error_occurred", True)]:
            with self.subTest(field=field, value=value):
                self.data = copy.deepcopy(original)
                self.data["google_benchmark"]["benchmarks"][0][field] = value
                self.rehash()
                with self.assertRaises(ValueError):
                    subject.summarize(self.data)

    def test_aggregate_rows_do_not_change_summaries(self):
        expected = subject.summarize(self.data)["benchmarks"]
        self.data["google_benchmark"]["benchmarks"].append(
            {"name": "Example_mean", "run_type": "aggregate", "aggregate_name": "mean", "cpu_time": 999})
        self.rehash()
        self.assertEqual(subject.summarize(self.data)["benchmarks"], expected)
        self.data["google_benchmark"]["benchmarks"] = self.data["google_benchmark"]["benchmarks"][-1:]
        self.rehash()
        with self.assertRaises(ValueError):
            subject.summarize(self.data)

    def test_invalid_normalization_is_rejected(self):
        for value in [0, -1, True, 1.5]:
            with self.subTest(value=value), self.assertRaises(ValueError):
                subject.summarize(self.data, operations_per_iteration=value)

    def test_svg_is_escaped_and_selection_is_explicit(self):
        report = subject.summarize(self.data)
        svg = subject.render_svg(report, title="Compare <CPU>", names=["Example"])
        self.assertIn("Compare &lt;CPU&gt;", svg)
        self.assertIn(report["source_content_sha256"], svg)
        self.assertIn("not confidence intervals", svg)
        self.assertIn("Test CPU", svg)
        self.assertIn(self.data["git"]["commit"], svg)
        with self.assertRaises(ValueError):
            subject.render_svg(report, title="Compare", names=["Missing"])

    def test_cli_preserves_input_and_rejects_existing_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "raw.json"
            summary = root / "summary.json"
            svg = root / "chart.svg"
            original = json.dumps(self.data)
            source.write_text(original)
            args = [str(source), "--summary", str(summary), "--svg", str(svg), "--name", "Example"]
            self.assertEqual(subject.main(args), 0)
            self.assertEqual(source.read_text(), original)
            self.assertIn("Source artifact", svg.read_text())
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                subject.main(args)
            self.assertEqual(source.read_text(), original)

    def test_invalid_chart_creates_no_output_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "raw.json"
            source.write_text(json.dumps(self.data))
            summary, svg = root / "summary.json", root / "chart.svg"
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                subject.main([str(source), "--summary", str(summary), "--svg", str(svg), "--name", "Missing"])
            self.assertFalse(summary.exists())
            self.assertFalse(svg.exists())

    def test_missing_svg_directory_creates_no_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "raw.json"
            original = json.dumps(self.data)
            source.write_text(original)
            summary = root / "summary.json"
            svg = root / "missing" / "chart.svg"
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                subject.main([str(source), "--summary", str(summary), "--svg", str(svg), "--name", "Example"])
            self.assertFalse(summary.exists())
            self.assertFalse(svg.exists())
            self.assertEqual(source.read_text(), original)


if __name__ == "__main__":
    unittest.main()
