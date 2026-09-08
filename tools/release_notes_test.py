# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Exercise the release-note renderer with the actual repository template."""

from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = "mbo"


class ReleaseNotesTest(unittest.TestCase):
    def render(self, tag):
        with tempfile.TemporaryDirectory() as cwd:
            return subprocess.run(
                ["bash", str(ROOT / "tools/release_notes.sh"), tag],
                cwd=cwd, text=True, capture_output=True, check=False)

    def test_release_resources(self):
        for tag in ("1.2.3", "v1.2.3", "v1.2.3-rc.1"):
            with self.subTest(tag=tag):
                result = self.render(tag)
                self.assertEqual(result.returncode, 0, result.stderr)
                notes = result.stdout
                base = f"https://mboworks.github.io/{REPO}"
                self.assertIn(f"{base}/site/tag/{tag}/", notes)
                self.assertNotIn("@TAG@", notes)
                self.assertNotIn("@VERSION@", notes)
                version = tag.removeprefix("v")
                if REPO in ("mbo", "xff", "carve"):
                    self.assertIn(f"{base}/coverage/tag/{version}/", notes)
                else:
                    self.assertNotIn("/coverage/", notes)
                if REPO == "xff":
                    self.assertIn(f"{base}/releases/{version}/)", notes)
                    self.assertIn(f"{base}/releases/{version}/XFF.md", notes)
                if REPO == "coderef":
                    self.assertIn(f"/blob/{tag}/CHANGELOG.md", notes)
                    self.assertIn(f"{base}/site/tag/{tag}/schema/v1.json", notes)

    def test_reject_invalid_tags_without_partial_notes(self):
        for tag in ("", "main", "../1.2.3", "v1.2.3|bad", "1.2.3\nmain"):
            with self.subTest(tag=tag):
                result = self.render(tag)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
