# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Coverage retention expires source details, never aggregate measurements."""

import datetime
import gzip
import json
from pathlib import Path
import tempfile
import unittest
import shutil

import coverage_storage as storage


class CoverageStorageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / 'coverage'
        self.now = datetime.datetime(2026, 10, 3, tzinfo=datetime.timezone.utc)

    def report(self, target, days):
        path = self.root / target
        path.joinpath('lcov').mkdir(parents=True)
        path.joinpath('coverage-meta.json').write_text(json.dumps({'source': {
            'completed_at': (self.now - datetime.timedelta(days=days)).isoformat(), 'head_sha': 'actual-commit'}}))
        path.joinpath('coverage-summary.json').write_text('{"aggregate":42,"patch":17}\n')
        path.joinpath('index.html').write_text('original aggregate table')
        path.joinpath('lcov/index.html').write_text('<html>Source\n<a href="file.cc.gcov.html#L3">file</a></html>')
        path.joinpath('lcov/file.cc.gcov.html').write_text('<html>Source\n<span id="L3">return 42;</span></html>')
        return path

    def test_seven_day_boundary_alias_deduplication_and_expiration_preserve_all_summaries(self):
        recent = self.report('runs/12/1', 7)
        alias = self.report('pr/7', 7)
        expired = self.report('runs/11/1', 7.01)
        summaries = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file() and 'lcov' not in p.parts}
        storage.compact(self.root, self.now)
        first = json.loads(recent.joinpath('details.json').read_text())
        self.assertEqual(first, json.loads(alias.joinpath('details.json').read_text()))
        archive = self.root / first['archive']
        self.assertTrue(archive.exists())
        self.assertNotIn('archive', json.loads(expired.joinpath('details.json').read_text()))
        self.assertEqual(list(recent.joinpath('lcov').iterdir()), [recent / 'lcov/index.html'])
        for path, data in summaries.items():
            self.assertEqual(path.read_bytes(), data)
        storage.compact(self.root, self.now + datetime.timedelta(days=1))
        self.assertFalse(archive.exists())
        self.assertEqual(list(self.root.glob('assets/reports/bases/*.json.gz')), [])
        for path, data in summaries.items():
            self.assertEqual(path.read_bytes(), data)

    def test_shared_base_and_edits_reconstruct_every_source_byte(self):
        assets = self.root / 'assets/reports'
        bases = {}
        initial = {'file.cc.gcov.html': ['text/html', 'line 1\nline 2\nline 3\n']}
        original = storage.pack_report(initial, assets, bases)
        changed = {'file.cc.gcov.html': ['text/html', 'first\nline 2\nline 3\nlast without newline']}
        packed = storage.pack_report(changed, assets, bases)
        record = packed['files']['file.cc.gcov.html'][1]
        self.assertEqual(record['base'], original['files']['file.cc.gcov.html'][1]['base'])
        base = json.loads(gzip.decompress((assets / 'bases' / (record['base'] + '.json.gz')).read_bytes()))
        lines = base.splitlines(keepends=True)
        actual = ''.join(edit if isinstance(edit, str) else ''.join(lines[edit[0]:edit[1]]) for edit in record['edits'])
        self.assertEqual(actual, changed['file.cc.gcov.html'][1])

    def test_alias_keeps_shared_bases_until_the_last_recent_reference_expires(self):
        expired_first = self.report('runs/11/1', 7)
        recent = self.report('pr/7', 1)
        storage.compact(self.root, self.now)
        original = json.loads(recent.joinpath('details.json').read_text())
        storage.compact(self.root, self.now + datetime.timedelta(days=1))
        self.assertNotIn('archive', json.loads(expired_first.joinpath('details.json').read_text()))
        self.assertEqual(json.loads(recent.joinpath('details.json').read_text()), original)
        self.assertTrue((self.root / original['archive']).is_file())
        self.assertTrue(list(self.root.glob('assets/reports/bases/*.json.gz')))

    def test_undated_legacy_and_old_replayed_reports_do_not_renew_details(self):
        report = self.report('runs/11/1', 8)
        legacy = self.report('tag/0.1.0', 1)
        legacy.joinpath('coverage-meta.json').write_text('{"source":{}}')
        storage.compact(self.root, self.now)
        # Replaying the original artifact restores its HTML but keeps its original timestamp.
        report.joinpath('lcov/file.cc.gcov.html').write_text('old source uploaded again')
        storage.compact(self.root, self.now)
        for path in (report, legacy):
            self.assertNotIn('archive', json.loads(path.joinpath('details.json').read_text()))
            self.assertTrue(path.joinpath('coverage-summary.json').is_file())
            self.assertFalse(path.joinpath('lcov/file.cc.gcov.html').exists())

    def test_missing_base_fails_without_deleting_remaining_report_sources(self):
        report = self.report('runs/12/1', 1)
        storage.compact(self.root, self.now)
        shutil.rmtree(self.root / 'assets/reports/bases')
        report.joinpath('lcov/file.cc.gcov.html').write_text('recoverable source')
        with self.assertRaisesRegex(ValueError, 'missing coverage base'):
            storage.compact(self.root, self.now)
        self.assertEqual(report.joinpath('lcov/file.cc.gcov.html').read_text(), 'recoverable source')


if __name__ == '__main__':
    unittest.main()
