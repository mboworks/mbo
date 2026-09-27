# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for repetition handling and units in the generated deque report."""

import copy
import math
import unittest
from unittest import mock

import segmented_deque_report as report


def repetitions():
    return [{"name": "example", "run_type": "iteration", "repetition_index": index,
             "work_items": 10, "cpu_time": index + 1, "time_unit": "us"}
            for index in range(9)]


class SegmentedDequeReportTest(unittest.TestCase):
    def test_statistics_normalize_time_and_work(self):
        summary = report.summarize(repetitions())["example"]
        self.assertEqual(summary["minimum"], 1_000)
        self.assertEqual(summary["fast_three"], 2_000)
        self.assertEqual(summary["median"], 5_000)
        self.assertEqual(summary["mean"], 5_000)
        self.assertEqual(summary["maximum"], 9_000)
        self.assertEqual(summary["per_item"], 500)
        self.assertAlmostEqual(summary["sd"], math.sqrt(7.5) * 1_000)

    def test_aggregate_rows_do_not_change_repetition_count(self):
        rows = repetitions() + [{"run_type": "aggregate", "name": "example_mean", "cpu_time": 5}]
        self.assertEqual(report.summarize(rows)["example"]["median"], 5_000)

    def test_missing_or_duplicate_repetitions_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "nine distinct"):
            report.summarize(repetitions()[:-1])
        rows = repetitions()
        rows[-1]["repetition_index"] = 0
        with self.assertRaisesRegex(ValueError, "nine distinct"):
            report.summarize(rows)

    def test_mismatched_work_units_are_rejected(self):
        rows = repetitions()
        rows[-1]["work_items"] = 20
        with self.assertRaisesRegex(ValueError, "normalization"):
            report.summarize(rows)

    def test_variable_counter_is_reported_as_range(self):
        self.assertEqual(report.counter([{"bytes": 16}, {"bytes": 32}], "bytes"), "16..32")
        self.assertEqual(report.counter([{}], "bytes"), "n/a")

    def test_render_preserves_provenance_and_matches_only_same_workload(self):
        rows = []
        for family, multiplier in (("SegmentedDeque/S256/Indexed/Offset0/16384", 2),
                                   ("SegmentedVector/S256/Indexed/Offset0/16384", 1),
                                   ("StdDeque/Indexed/Offset0/16384", 4)):
            for row in repetitions():
                rows.append(dict(row, name=family, cpu_time=row["cpu_time"] * multiplier))
        envelope = {
            "git": {"commit": "a" * 40},
            "host": {"cpu_model": "Example CPU", "os": "TestOS", "architecture": "test64"},
            "toolchain": {"compiler_name": "Clang", "compiler_version": "22", "cxx_standard": "c++23",
                          "standard_library": "libc++", "bazel_version": "9.2.0"},
            "google_benchmark": {"benchmarks": rows},
        }
        # Envelope validation has its own tests; isolate report rendering with fixed metadata.
        with mock.patch.object(report.benchmark_artifact, "validate") as validate:
            text = report.render(envelope, "example.json")
        validate.assert_called_once_with(envelope)
        self.assertIn("Source: `" + "a" * 40 + "`", text)
        self.assertIn("[example.json](example.json)", text)
        self.assertRegex(text, r"1000\.000\s*\|\s*2\.000\s*\|\s*0\.500")
        self.assertIn("3 of 3 families exceed 10% CV", text)

        second = copy.deepcopy(envelope)
        for row in second["google_benchmark"]["benchmarks"]:
            row["name"] = row["name"].replace("Indexed", "Permuted")
        envelope["toolchain"]["build_flags"] = "bazel run -c opt -- --benchmark_filter=Indexed --benchmark_out=/tmp/a"
        second["toolchain"]["build_flags"] = "bazel run -c opt -- --benchmark_filter=Permuted --benchmark_out=/tmp/b"
        with mock.patch.object(report.benchmark_artifact, "validate"):
            text = report.render([envelope, second], ["first.json", "second.json"])
            self.assertIn("6 of 6 families", text)
            self.assertIn("[second.json](second.json)", text)
            with self.assertRaisesRegex(ValueError, "nine distinct"):
                report.render([envelope, envelope], ["first.json", "duplicate.json"])
            for field, value in (("git", {"commit": "b" * 40}),
                                 ("host", dict(second["host"], architecture="other")),
                                 ("toolchain", dict(second["toolchain"], build_flags="bazel run -c dbg"))):
                with self.subTest(field=field):
                    changed = dict(second, **{field: value})
                    with self.assertRaisesRegex(ValueError, "same source"):
                        report.render([envelope, changed], ["first.json", "changed.json"])


if __name__ == "__main__":
    unittest.main()
