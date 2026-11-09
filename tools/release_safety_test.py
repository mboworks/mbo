# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Keep every threaded HAMT ownership test inside the scoped TSan gate."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ReleaseSafetyTest(unittest.TestCase):
    def test_scoped_tsan_job_covers_every_threaded_hamt_test(self):
        workflow = (ROOT / ".github/workflows/main.yml").read_text()
        job = workflow.split("  thread-sanitizer:\n", 1)[1].split("\n  test-clang:", 1)[0]
        threaded = []
        for source in (ROOT / "mbo/container/experimental/hamt").rglob("*_test.cc"):
            if re.search(r"std::(?:jthread|thread)\b", source.read_text()):
                threaded.append(source.stem)
        self.assertTrue(threaded)
        for target in threaded:
            with self.subTest(target=target):
                self.assertIn(f"//mbo/container/experimental/hamt:{target}", job)
        self.assertIn("--config=tsan", job)
        self.assertNotIn("//...", job)
        self.assertIn("namespace: tsan-llvm23", job)

    def test_tsan_halts_on_races_and_clang_enforces_annotations(self):
        configuration = (ROOT / ".bazelrc").read_text()
        self.assertIn("common:tsan --test_env=TSAN_OPTIONS=halt_on_error=1", configuration)
        self.assertIn("common:tsan --copt=-fsanitize=thread", configuration)
        self.assertIn("common:tsan --linkopt=-fsanitize=thread", configuration)
        self.assertIn("--cxxopt=-Wthread-safety --host_cxxopt=-Wthread-safety", configuration)


if __name__ == "__main__":
    unittest.main()
