# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for benchmark_artifact."""

import os
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchmark_artifact as subject


class BenchmarkArtifactTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.raw = {
            "context": {"compiler": "Clang 22", "library_build_type": "release"},
            "benchmarks": [{"name": "BM_Example", "real_time": 1.25, "time_unit": "ns"}],
        }
        self.git = mock.patch.object(
            subject,
            "_git",
            return_value={
                "repository": "https://github.com/mboworks/mbo.git",
                "commit": "a" * 40,
                "branch": "feature/example",
                "dirty": False,
            },
        )
        self.host = mock.patch.object(
            subject,
            "_host",
            return_value={
                "hostname": "test",
                "os": "TestOS",
                "os_release": "1",
                "architecture": "test64",
                "cpu_model": "Test CPU",
                "physical_cpu_count": 4,
                "logical_cpu_count": 8,
                "memory_bytes": 1024,
                "load_average": [0.0, 0.0, 0.0],
            },
        )
        self.git.start()
        self.host.start()
        self.addCleanup(self.git.stop)
        self.addCleanup(self.host.stop)

    def make(self):
        return subject.artifact(
            component="Arena",
            target="//mbo/memory:arena_benchmark",
            raw=self.raw,
            command="benchmark --flags",
            configurations=["clang", "opt_apple_m5"],
            baseline="b" * 40,
            started="2026-09-10T10:00:00+00:00",
            ended="2026-09-10T10:01:00+00:00",
            duration_seconds=60.0,
            root=self.root,
            controls={
                "repetitions": 9,
                "minimum_time": "1s",
                "warmup_time": "1s",
                "random_interleaving": True,
            },
            bazel_version="bazel 9.2.0",
        )

    def test_artifact_preserves_raw_results_and_provenance(self):
        artifact = self.make()
        self.assertEqual(artifact["google_benchmark"], self.raw)
        self.assertEqual(artifact["git"]["commit"], "a" * 40)
        self.assertEqual(artifact["source_relation"]["baseline_commit"], "b" * 40)
        self.assertEqual(artifact["controls"]["repetitions"], 9)
        self.assertEqual(len(artifact["content_sha256"]), 64)

    def test_validation_rejects_changed_content(self):
        artifact = self.make()
        artifact["component"] = "Changed"
        with self.assertRaisesRegex(ValueError, "content_sha256"):
            subject.validate(artifact)

    def test_validation_rejects_weak_measurement_controls(self):
        for field, value, message in (
            ("repetitions", 8, "9 repetitions"),
            ("random_interleaving", False, "random interleaving"),
            ("minimum_time", "", "minimum_time"),
            ("warmup_time", "", "warmup_time"),
        ):
            with self.subTest(field=field):
                artifact = self.make()
                artifact["controls"][field] = value
                artifact["content_sha256"] = subject._sha256_json(
                    {key: item for key, item in artifact.items() if key != "content_sha256"}
                )
                with self.assertRaisesRegex(ValueError, message):
                    subject.validate(artifact)

    def test_validate_command_accepts_serialized_artifact(self):
        destination = self.root / "artifact.json"
        destination.write_text(json.dumps(self.make()))
        self.assertEqual(subject.main(["validate", str(destination)]), 0)

    def test_validation_rejects_empty_benchmark_results(self):
        artifact = self.make()
        artifact["google_benchmark"]["benchmarks"] = []
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )
        with self.assertRaisesRegex(ValueError, "must be non-empty"):
            subject.validate(artifact)


if __name__ == "__main__":
    unittest.main()
