# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Published summaries retain their original data through repeated compaction."""

import gzip
from pathlib import Path
import tempfile
import unittest

import site_storage


class SiteStorageTest(unittest.TestCase):
    def test_summary_bytes_links_and_repeated_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            report = root / 'coverage/runs/12/1'
            report.mkdir(parents=True)
            summary = b'{ "measurements": {"covered": 42}, "policy": [80, 90] }\n'
            (report / 'coverage-summary.json').write_bytes(summary)
            (report / 'coverage-meta.json').write_text('{"source":{"run_id":12}}\n')
            markup = '<html><body><a href="coverage-summary.json">JSON</a><p>42</p></body></html>'
            (report / 'index.html').write_text(markup)
            site_storage.compact_summaries(root / 'coverage')
            self.assertEqual(gzip.decompress((report / 'coverage-summary.json.gz').read_bytes()), summary)
            self.assertFalse((report / 'coverage-summary.json').exists())
            self.assertEqual(gzip.decompress((report / 'page.html.gz').read_bytes()).decode(),
                             markup.replace('coverage-summary.json', 'coverage-summary.json.gz'))
            self.assertIn('src="../../../../site-page.js"', (report / 'index.html').read_text())
            before = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            site_storage.compact_summaries(root / 'coverage')
            self.assertEqual(before, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
            # A fresh report or rebuilt index replaces its prior packed presentation.
            (report / 'index.html').write_text(markup.replace('42', '43'))
            site_storage.compact_summaries(root / 'coverage')
            self.assertIn(b'<p>43</p>', gzip.decompress((report / 'page.html.gz').read_bytes()))
            self.assertEqual(gzip.decompress((report / 'coverage-summary.json.gz').read_bytes()), summary)


if __name__ == '__main__':
    unittest.main()
