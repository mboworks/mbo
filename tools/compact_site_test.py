# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Both publishers compact history and measure the complete deployment."""

import contextlib
import datetime
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import compact_site


class CompactSiteTest(unittest.TestCase):
    def test_counts_entire_payload_without_changing_release_or_benchmark_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            retained = {'site/tag/1.2.3/index.html': b'release documentation',
                        'benchmarks/report.json': b'{"observations":[1,2,3]}',
                        'coverage/pr/1/index.html': b'legacy aggregate report',
                        'coverage/pr/1/source.cc.gcov.html': b'legacy source without metadata'}
            for name, data in {**retained, '.git/objects/private': b'not deployed'}.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            with contextlib.redirect_stdout(io.StringIO()):
                report = compact_site.compact(root, datetime.datetime.now(datetime.timezone.utc))
            self.assertEqual(report['sections']['site'], {'files': 1, 'bytes': 21})
            self.assertNotIn('.git', report['sections'])
            self.assertEqual(report['total_bytes'], sum(p.stat().st_size for p in root.rglob('*')
                             if p.is_file() and '.git' not in p.parts and p.name != 'storage-report.json'))
            self.assertEqual(json.loads((root / 'storage-report.json').read_text()), report)
            for name, data in retained.items():
                self.assertEqual((root / name).read_bytes(), data)
            self.assertFalse((root / 'coverage/pr/1/coverage-meta.json').exists())

    def test_advisory_budget_and_packaging_ceiling(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            now = datetime.datetime.now(datetime.timezone.utc)
            for size, warns, fails in ((199_999_999, False, False), (200_000_000, True, False),
                                       (9_000_000_000, True, False), (9_000_000_001, True, True)):
                with self.subTest(size=size), mock.patch.object(
                        compact_site, 'sizes', return_value={'site': {'files': 1, 'bytes': size}}):
                    output = io.StringIO()
                    with contextlib.redirect_stdout(output):
                        if fails:
                            with self.assertRaisesRegex(ValueError, 'deployment ceiling'):
                                compact_site.compact(root, now)
                        else:
                            compact_site.compact(root, now)
                    self.assertEqual('::warning::' in output.getvalue(), warns)

    def test_publishers_compact_before_commit_and_after_deployment_artwork(self):
        repository = Path(__file__).resolve().parent.parent
        for name in ('coverage_pages.yml', 'pages.yml'):
            with self.subTest(workflow=name):
                source = (repository / '.github/workflows' / name).read_text()
                self.assertLess(source.index('compact_site.py site'), source.index('git -C site commit'))
                self.assertLess(source.index('site_artwork.py'), source.index('compact_site.py public'))
                self.assertLess(source.index('compact_site.py public'), source.index('uses: actions/upload-pages-artifact@'))
                self.assertIn('git -C site add --all', source)
                if name == 'pages.yml':
                    self.assertIn('git fetch --no-tags --depth=1 origin', source)
                    self.assertNotIn('git fetch origin\n', source)
                else:
                    self.assertIn('fetch-depth: 0\n          filter: blob:none', source)


if __name__ == '__main__':
    unittest.main()
