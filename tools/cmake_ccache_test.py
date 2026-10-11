#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify CMake compiler-cache bounds, measurements, and workflow policy."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]
SAVE = (ROOT / ".github/actions/cmake-ccache-save/action.yml").read_text()
MEASURE = textwrap.dedent(SAVE.split("<<'PY'\n", 1)[1].split("\n        PY\n", 1)[0])


class CMakeCcacheTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.cache = self.root / "cache"
        self.cache.mkdir()
        self.record = self.root / "measurement.json"
        self.key = f"cmake-ccache-v1-Linux-X64-gcc-15.2.0-{'a' * 64}-123-1"
        fake = self.root / "ccache"
        fake.write_text(
            f"#!{sys.executable}\n"
            "import os, pathlib, sys\n"
            "assert sys.argv[1:] == ['--cleanup']\n"
            "if 'CCACHE_TEST_EXIT' in os.environ:\n"
            "    sys.exit(int(os.environ['CCACHE_TEST_EXIT']))\n"
            "discard = pathlib.Path(os.environ['CCACHE_DIR']) / 'discard'\n"
            "if discard.exists():\n"
            "    discard.unlink()\n"
        )
        fake.chmod(0o755)

    def measure(self, **environment):
        env = dict(
            os.environ,
            PATH=f"{self.root}{os.pathsep}{os.environ['PATH']}",
            CCACHE_DIR=str(self.cache),
            **environment,
        )
        return subprocess.run(
            [
                sys.executable,
                "-c",
                MEASURE,
                str(self.cache),
                self.key,
                str(self.record),
            ],
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_measures_retained_payload_after_synchronous_cleanup(self):
        (self.cache / "keep").write_bytes(b"1234")
        (self.cache / "discard").write_bytes(b"123456")
        result = self.measure()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(self.record.read_text()),
            {"key": self.key, "before": 10, "uncompressed": 4, "evicted": 1},
        )
        self.assertEqual((self.cache / "keep").read_bytes(), b"1234")

    def test_payload_at_upload_limit_is_rejected(self):
        with (self.cache / "oversized").open("wb") as stream:
            stream.truncate(600_000_000)
        result = self.measure()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exceeds its upload budget", result.stderr)
        self.assertFalse(self.record.exists())

    def test_failed_cleanup_cannot_produce_a_save_measurement(self):
        result = self.measure(CCACHE_TEST_EXIT="42")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.record.exists())

    def test_workflow_uses_launchers_and_saves_only_successful_main_builds(self):
        workflow = (ROOT / ".github/workflows/main.yml").read_text()
        job = workflow.split("  cmake-gcc:\n", 1)[1].split("\n  test-gcc:\n", 1)[0]
        for setting in (
            "CCACHE_COMPILERCHECK: content",
            "CCACHE_MAXSIZE: 500MB",
            "CMAKE_C_COMPILER_LAUNCHER: ccache",
            "CMAKE_CXX_COMPILER_LAUNCHER: ccache",
        ):
            self.assertIn(setting, job)
        self.assertIn("path: ${{ env.CCACHE_DIR }}", job)
        self.assertIn("success() && github.ref == 'refs/heads/main'", job)
        self.assertIn("direct_cache_hit", job)
        self.assertIn("preprocessed_cache_hit", job)
        self.assertIn("exit (hits == 0)", job)
        self.assertEqual(SAVE.count("path: ${{ env.CCACHE_DIR }}"), 1)
        self.assertEqual(SAVE.count("uses: actions/cache/save@"), 1)
        self.assertIn("if: github.ref == 'refs/heads/main'", SAVE)
        self.assertIn("steps.bound.outcome == 'success'", SAVE)
        self.assertIn("--replacement-key", SAVE)
        self.assertNotIn("_deps", SAVE)
        self.assertNotIn("build-cmake", SAVE)


if __name__ == "__main__":
    unittest.main()
