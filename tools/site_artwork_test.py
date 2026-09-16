# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Verify favicons across nested release, reference, and coverage pages."""

from pathlib import Path
import tempfile
import sys
import unittest

sys.path.insert(0, str(Path(__file__).parent))
import site_artwork


class SiteArtworkTest(unittest.TestCase):
    def test_both_publishers_decorate_only_the_complete_deployment_copy(self):
        root = Path(__file__).resolve().parent.parent
        for name in ("pages", "coverage_pages"):
            workflow = (root / f".github/workflows/{name}.yml").read_text()
            stage = workflow.index("rsync --archive --exclude='.git' site/ public/")
            decorate = workflow.index("python3 source/tools/site_artwork.py source/docs/assets public")
            upload = workflow.index("uses: actions/upload-pages-artifact@")
            self.assertLess(stage, decorate)
            self.assertLess(decorate, upload)

    def test_nested_pages_and_idempotence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            assets = Path(__file__).resolve().parent.parent / "docs/assets"
            documents = {
                "index.html": '<!doctype html><html lang="en"><meta charset="utf-8"><body>Home</body></html>',
                "site/tag/1.2.5/README.html": '<!doctype html><html><head><title>Help</title></head><body>Help</body></html>',
                "coverage/pr/835/index.html": '<HTML><HEAD><TITLE>Coverage</TITLE></HEAD><BODY>Report</BODY></HTML>',
                "fragment.html": '<p>A fragment</p>',
            }
            for name, text in documents.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            site_artwork.decorate(assets, root)
            for name, original in documents.items():
                path = root / name
                decorated = path.read_text()
                if name == "fragment.html":
                    self.assertEqual(decorated, original)
                    continue
                prefix = "." if name == "index.html" else "../../.."
                for icon in site_artwork.ICONS:
                    self.assertIn(f'href="{prefix}/{icon}"', decorated)
                    self.assertEqual((path.parent / prefix / icon).read_bytes(),
                                     (assets / icon).read_bytes())
                self.assertIn('sizes="32x32"', decorated)
                self.assertIn('sizes="180x180"', decorated)
                self.assertEqual(decorated.count(site_artwork.MARKER), 1)
            before = {name: (root / name).read_bytes() for name in documents}
            site_artwork.decorate(assets, root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in documents})


if __name__ == "__main__":
    unittest.main()
