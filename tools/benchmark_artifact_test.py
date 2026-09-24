# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Tests for benchmark_artifact."""

import copy
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
            "context": {
                "compiler": "clang-22",
                "compiler_name": "Clang",
                "compiler_version": "22.1.8",
                "compiler_version_extra": "Clang 22.1.8",
                "cplusplus": "202302",
                "cxx_standard_requested": "c++23",
                "standard_library": "libc++",
                "standard_library_version": "220108",
                "library_build_type": "release",
            },
            "benchmarks": [
                {
                    "name": "BM_Example",
                    "run_name": "BM_Example",
                    "run_type": "iteration",
                    "iterations": 100,
                    "repetition_index": repetition,
                    "cpu_time": 1.0,
                    "real_time": 1.25,
                    "time_unit": "ns",
                }
                for repetition in range(9)
            ],
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
            raw=copy.deepcopy(self.raw),
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
                "warmup_time": 1.0,
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
        self.assertEqual(artifact["toolchain"]["cxx_standard"], "c++23")
        self.assertEqual(artifact["toolchain"]["cplusplus"], "202302")
        self.assertEqual(artifact["toolchain"]["standard_library"], "libc++")
        self.assertEqual(len(artifact["content_sha256"]), 64)

    def test_validation_rejects_changed_content(self):
        artifact = self.make()
        artifact["component"] = "Changed"
        with self.assertRaisesRegex(ValueError, "content_sha256"):
            subject.validate(artifact)

    def test_validation_accepts_schema_v1_cxx20_artifact(self):
        artifact = self.make()
        artifact["toolchain"] = {
            "compiler": "clang-22",
            "compiler_version": "Clang 22.1.8",
            "cxx_standard": "c++20",
            "bazel_version": "bazel 9.2.0",
            "library_build_type": "release",
            "bazel_configurations": ["clang", "opt_apple_m5"],
            "build_flags": "benchmark --flags",
        }
        artifact["google_benchmark"]["context"] = {
            "compiler": "clang-22",
            "compiler_version": "Clang 22.1.8",
            "cxx_standard": "c++20",
            "library_build_type": "release",
        }
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )

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

    def test_valid_measurement_requires_clean_git_checkout(self):
        artifact = self.make()
        artifact["git"]["dirty"] = True
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )

        with self.assertRaisesRegex(ValueError, "clean git checkout"):
            subject.validate(artifact)

    def test_validation_requires_declared_raw_repetition_count(self):
        artifact = self.make()
        artifact["google_benchmark"]["benchmarks"].pop()
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )

        with self.assertRaisesRegex(ValueError, "8 raw repetitions; expected 9"):
            subject.validate(artifact)

    def test_validation_rejects_invalid_iteration_rows(self):
        for field, value, message in (
            ("iterations", 0, "positive iterations"),
            ("error_occurred", True, "errored iteration"),
        ):
            with self.subTest(field=field):
                artifact = self.make()
                artifact["google_benchmark"]["benchmarks"][0][field] = value
                artifact["content_sha256"] = subject._sha256_json(
                    {key: item for key, item in artifact.items() if key != "content_sha256"}
                )
                with self.assertRaisesRegex(ValueError, message):
                    subject.validate(artifact)

    def test_validation_requires_unique_complete_repetition_indices(self):
        artifact = self.make()
        artifact["google_benchmark"]["benchmarks"][-1]["repetition_index"] = 0
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )

        with self.assertRaisesRegex(ValueError, "repetition_index values"):
            subject.validate(artifact)

    def test_validation_requires_valid_iteration_timings(self):
        for field, value, message in (
            ("cpu_time", float("nan"), "finite nonnegative cpu_time"),
            ("real_time", -1, "finite nonnegative real_time"),
            ("time_unit", "", "non-empty time_unit"),
        ):
            with self.subTest(field=field):
                artifact = self.make()
                artifact["google_benchmark"]["benchmarks"][0][field] = value
                artifact["content_sha256"] = subject._sha256_json(
                    {key: item for key, item in artifact.items() if key != "content_sha256"}
                )
                with self.assertRaisesRegex(ValueError, message):
                    subject.validate(artifact)

    def test_validation_preserves_invalid_failed_run(self):
        artifact = self.make()
        failed = artifact["google_benchmark"]["benchmarks"][0]
        failed["error_occurred"] = True
        failed["iterations"] = 0
        artifact["google_benchmark"]["benchmarks"] = [failed]
        artifact["validity"] = {"status": "invalid", "note": "benchmark failed"}
        artifact["content_sha256"] = subject._sha256_json(
            {key: item for key, item in artifact.items() if key != "content_sha256"}
        )

        subject.validate(artifact)

    def test_warmup_is_numeric_seconds_as_required_by_google_benchmark(self):
        args = subject.parser().parse_args(
            [
                "run",
                "--component",
                "Arena",
                "--target",
                "//mbo/memory:arena_benchmark",
                "--output",
                "result.json",
                "--warmup-time",
                "0.25",
                "--",
                "benchmark",
            ]
        )
        self.assertEqual(args.warmup_time, 0.25)
        self.assertIsInstance(args.warmup_time, float)
        self.assertEqual(args.cxx_standard, "c++23")

    @mock.patch.object(
        subject,
        "_optional_command",
        return_value="Bazelisk version: v1.29.0\nBuild label: 9.2.0\nBuild target: @@//src/main/java/com/google/devtools/build/lib/bazel:BazelServer",
    )
    def test_bazel_version_uses_bazel_build_label(self, optional_command):
        self.assertEqual(subject._bazel_version(), "9.2.0")
        optional_command.assert_called_once_with(["bazel", "version", "--gnu_format"])

    @mock.patch.object(subject, "_optional_command", return_value="bazel 9.2.0")
    def test_bazel_version_accepts_gnu_fallback(self, optional_command):
        self.assertEqual(subject._bazel_version(), "9.2.0")
        optional_command.assert_called_once_with(["bazel", "version", "--gnu_format"])

    def test_live_build_context_requires_cxx23_executable_provenance(self):
        subject._validate_live_build_context(self.raw["context"])

        stale = dict(self.raw["context"], cxx_standard_requested="c++20", cplusplus="202002")
        with self.assertRaisesRegex(ValueError, "expected 'c\\+\\+23'"):
            subject._validate_live_build_context(stale)

        missing = dict(self.raw["context"])
        missing.pop("standard_library")
        with self.assertRaisesRegex(ValueError, "standard_library"):
            subject._validate_live_build_context(missing)

        empty = dict(self.raw["context"], compiler_name="")
        with self.assertRaisesRegex(ValueError, "compiler_name"):
            subject._validate_live_build_context(empty)

    @mock.patch.object(subject.platform, "system", return_value="Darwin")
    @mock.patch.object(subject.platform, "processor", return_value="arm")
    @mock.patch.object(
        subject,
        "_optional_command",
        return_value="Hardware:\n\n    Hardware Overview:\n\n      Chip: Apple M5 Pro\n      Serial Number: secret",
    )
    def test_cpu_model_uses_apple_chip_instead_of_generic_architecture(
        self, optional_command, processor, system
    ):
        self.assertEqual(subject._cpu_model(), "Apple M5 Pro")
        optional_command.assert_called_once_with(
            ["system_profiler", "SPHardwareDataType", "-detailLevel", "mini"]
        )
        processor.assert_not_called()
        system.assert_called_once_with()


if __name__ == "__main__":
    unittest.main()
