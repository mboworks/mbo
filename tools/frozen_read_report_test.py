# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Check Frozen measurement integrity, repetition handling, and published summaries."""

import gzip
import hashlib
import json
import math
from pathlib import Path
import tempfile
import unittest

import frozen_read_report as report


def raw_repetitions(name="Map/string/64/sparse/iterate"):
    return {"benchmarks": [
        {"run_name": name, "run_type": "iteration", "repetition_index": index,
         "cpu_time": value, "time_unit": unit, "iterations": 10, "elements": 64}
        for index, (value, unit) in enumerate(((1, "us"), (2_000, "ns"), (0.003, "ms")))
    ]}


class FrozenReadReportTest(unittest.TestCase):
    def test_default_verification_matches_raw_measurements(self):
        summaries = report.read_mbo_data(report.DEFAULT_DATA, "frozen-fambo-default-v1")
        self.assertEqual((report.DEFAULT_DATA / "summary.csv").read_text(), report.csv_text(summaries))
        guide = report.GUIDE.read_text()
        self.assertEqual(guide, report.replace_block(guide, "DEFAULT HASH RESULTS", report.default_hash_table(summaries)))
        with self.assertRaisesRegex(ValueError, "unexpected experiment"):
            report.read_mbo_data(report.DEFAULT_DATA)

    def test_mbo_published_results_match_raw_measurements(self):
        summaries = report.read_mbo_data(report.MBO_DATA)
        self.assertEqual((report.MBO_DATA / "summary.csv").read_text(), report.csv_text(summaries))
        guide = report.GUIDE.read_text()
        blocks = (("MBO READ RESULTS", report.read_table(summaries, report.MBO_LAYOUTS)),
                  ("MBO HASH RESULTS", report.mbo_hash_tables(summaries)),
                  ("MBO SIZE RESULTS", report.mbo_size_table(summaries)))
        for label, table in blocks:
            with self.subTest(label=label):
                self.assertEqual(guide, report.replace_block(guide, label, table))

    def test_mbo_artifact_checks_provenance_and_integrity(self):
        metadata = json.loads((report.MBO_DATA / "provenance.json").read_text())
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / metadata["artifact"]).write_bytes((report.MBO_DATA / metadata["artifact"]).read_bytes())
            for field, value, message in (
                    ("source_commit", "0" * 40, "source commit mismatch"),
                    ("compressed_sha256", "0" * 64, "compressed_sha256 mismatch"),
                    ("artifact_sha256", "0" * 64, "artifact_sha256 mismatch"),
                    ("cases", 1, "unexpected case count")):
                with self.subTest(field=field):
                    (directory / "provenance.json").write_text(json.dumps(dict(metadata, **{field: value})))
                    with self.assertRaisesRegex(ValueError, message):
                        report.read_mbo_data(directory)

    def test_csv_is_stable_across_last_bit_rounding(self):
        summaries = report.summarize(raw_repetitions(), 3)
        expected = report.csv_text(summaries)
        summary = next(iter(summaries.values()))
        summary["cpu_cv"] = math.nextafter(summary["cpu_cv"], math.inf)
        self.assertEqual(report.csv_text(summaries), expected)

    def test_units_and_iteration_normalization(self):
        raw = raw_repetitions()
        raw["benchmarks"].append({"run_type": "aggregate", "cpu_time": 999})
        summary = report.summarize(raw, 3)["Map/string/64/sparse/iterate"]
        self.assertEqual(summary["median_cpu_ns"], 2_000)
        self.assertEqual(summary["median_cpu_ns_per_element"], 31.25)
        self.assertEqual(summary["cpu_cv"], 0.5)
        summary = report.summarize(raw_repetitions("Map/string/64/sparse/find/mixed"), 3)
        self.assertIsNone(next(iter(summary.values()))["median_cpu_ns_per_element"])

    def test_invalid_repetitions_and_measurements_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "at least two"):
            report.summarize(raw_repetitions(), 1)
        with self.assertRaisesRegex(ValueError, "no raw"):
            report.summarize({"benchmarks": []}, 3)
        with self.assertRaisesRegex(ValueError, "missing or duplicate"):
            report.summarize(raw_repetitions(), 4)
        for field, value, message in (
                ("repetition_index", 0, "missing or duplicate"),
                ("cpu_time", float("nan"), "invalid timing"),
                ("cpu_time", 0, "invalid timing"),
                ("iterations", 0, "no measured iterations"),
                ("elements", 32, "inconsistent elements"),
                ("error_occurred", True, "contains an error")):
            with self.subTest(field=field, value=value):
                raw = raw_repetitions()
                raw["benchmarks"][-1][field] = value
                with self.assertRaisesRegex(ValueError, message):
                    report.summarize(raw, 3)

    def test_changed_raw_data_and_source_are_rejected(self):
        raw = json.dumps(raw_repetitions()).encode()
        compressed = gzip.compress(raw, mtime=0)
        patch = b"source snapshot\n"
        metadata = {
            "raw": "raw.json.gz", "source_patch": "source.patch", "repetitions": 3,
            "cases": 1, "samples": 3, "compressed_sha256": hashlib.sha256(compressed).hexdigest(),
            "raw_sha256": hashlib.sha256(raw).hexdigest(),
            "source_patch_sha256": hashlib.sha256(patch).hexdigest(),
        }
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "raw.json.gz").write_bytes(compressed)
            (directory / "source.patch").write_bytes(patch)

            def write_metadata(values):
                (directory / "provenance.json").write_text(json.dumps({"runs": {"reads": values}}))

            write_metadata(metadata)
            self.assertEqual(len(report.read_data(directory, "reads")), 1)
            for field in ("compressed_sha256", "raw_sha256", "source_patch_sha256"):
                with self.subTest(field=field):
                    write_metadata(dict(metadata, **{field: "0" * 64}))
                    with self.assertRaisesRegex(ValueError, field + " mismatch"):
                        report.read_data(directory, "reads")
            for field, value, message in (("cases", 2, "case count"), ("samples", 4, "sample count")):
                write_metadata(dict(metadata, **{field: value}))
                with self.assertRaisesRegex(ValueError, message):
                    report.read_data(directory, "reads")

    def test_published_results_match_retained_raw_measurements(self):
        guide = report.GUIDE.read_text()
        for run, filename, label, render in (
                ("reads", "read-summary.csv", "READ RESULTS", report.read_table),
                ("hashes", "hash-summary.csv", "HASH RESULTS", report.hash_tables)):
            with self.subTest(run=run):
                summaries = report.read_data(report.DATA, run)
                self.assertEqual((report.DATA / filename).read_text(), report.csv_text(summaries))
                self.assertEqual(guide, report.replace_block(guide, label, render(summaries)))


if __name__ == "__main__":
    unittest.main()
